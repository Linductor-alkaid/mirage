#include "tray_core.hpp"

#include <mirage/desktop/tray_carrier.hpp>

#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <mirage/platform/windows/windows_desktop_environment.hpp>
#else
#include <csignal>

#include <mirage/platform/linux/linux_desktop_environment.hpp>
#endif

namespace {

constexpr std::string_view kProgramName = "mirage-tray";

std::atomic<bool> g_quit{false};

#ifdef _WIN32
// Windows shutdown: the console ctrl handler runs on its own thread —
// exactly the contract of the atomic quit flag.
BOOL WINAPI on_console_ctrl(DWORD) {
    g_quit.store(true);
    return TRUE;
}
#else
extern "C" void on_signal(int) {
    g_quit.store(true); // async-signal-safe: one atomic store
}
#endif

void print_usage(std::ostream &out) {
    out << "Usage: " << kProgramName << " [--socket PATH] [--shell PATH] [--version]\n"
        << "\n"
        << "The Mirage tray process (M5-10, DEC-030): a notification-area /\n"
        << "status-indicator icon showing the service's run state, with the\n"
        << "task pause / resume actions and 快速进入 Mirage (launches the\n"
        << "desktop shell). Talks to mirage-service exclusively over Local\n"
        << "IPC (EXEC-02); the carrier degrades honestly per platform\n"
        << "capability — without a notification area or an indicator host\n"
        << "this process reports and exits.\n"
        << "\n"
        << "Options:\n"
        << "  --socket PATH  IPC endpoint (default: DEC-007 default path)\n"
        << "  --shell PATH   Desktop shell binary for 打开 Mirage\n"
        << "                 (default: the mirage-desktop sibling of this\n"
        << "                 executable)\n"
        << "  --version      Print version and exit\n"
        << "  --help         Print this help\n";
}

/// 快速进入 Mirage default discovery: the desktop shell binary next to this
/// executable (the product layout M5-11 packages; an empty result keeps the
/// menu entry disabled — declared in DEC-030 decision 5).
std::string default_shell_path(const char *argv0) {
    const std::filesystem::path self = std::filesystem::absolute(argv0);
    const std::filesystem::path sibling = self.parent_path()
#ifdef _WIN32
                                          / "mirage-desktop.exe";
#else
                                          / "mirage-desktop";
#endif
    std::error_code error;
    if (std::filesystem::exists(sibling, error) && !error) {
        return sibling.string();
    }
    return {};
}

} // namespace

int main(int argc, char **argv) {
    const std::string version = "mirage-tray 0.1.0";
    std::string socket_path;
    std::string shell_path;
    bool shell_from_flags = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--version") {
            std::cout << version << '\n';
            return 0;
        }
        if (argument == "--socket" && index + 1 < argc) {
            socket_path = argv[++index];
            continue;
        }
        if (argument == "--shell" && index + 1 < argc) {
            shell_path = argv[++index];
            shell_from_flags = true;
            continue;
        }
        std::cerr << kProgramName << ": unknown argument '" << argument << "'\n";
        print_usage(std::cerr);
        return 2;
    }
    if (!shell_from_flags) {
        shell_path = default_shell_path(argc > 0 ? argv[0] : "mirage-tray");
    }

#ifdef _WIN32
    ::SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#else
    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);
    ::signal(SIGPIPE, SIG_IGN);
#endif

    // The platform tray carrier (capability honesty, DEC-030): a session
    // without the platform's indicator facility has no tray surface, and a
    // process that cannot present anything exits loudly instead of
    // pretending.
#ifdef _WIN32
    auto carrier = mirage::platform::windows_backend::open_tray_carrier();
#else
    auto carrier = mirage::platform::linux_backend::open_tray_carrier();
#endif
    if (carrier == nullptr) {
        std::cerr << kProgramName
                  << ": no tray carrier on this session (no notification area / no "
                     "StatusNotifierWatcher indicator host); nothing to present, exiting\n";
        return 1;
    }

    executor::Executor executor;
    const auto initialized = executor.initialize_ex(executor::ExecutorConfig{});
    if (!initialized.ok) {
        std::cerr << kProgramName << ": executor initialization failed: " << initialized.message
                  << '\n';
        return 1;
    }

    mirage::tray::TrayCore::Dependencies dependencies;
    dependencies.socket_path = socket_path;
    dependencies.shell_path = shell_path;
    dependencies.carrier = carrier.get();
    dependencies.quit_requested = [] { return g_quit.load(); };
    dependencies.on_quit = [] { g_quit.store(true); };
    mirage::tray::TrayCore core(std::move(dependencies), executor);

    // The carrier pump: the presentation loop runs as an Executor blocking
    // worker and stops when the quit flag (menu Quit or SIGINT/SIGTERM)
    // rises.
    executor::BlockingWorkerSpec pump_spec;
    pump_spec.name = "mirage-tray-carrier";
    pump_spec.config.thread_name = "mirage-tray-carrier";
    auto pump =
        std::make_unique<mirage::tray::TrayPumpWorker>(carrier.get(), [] { return g_quit.load(); });
    pump->load_state = [&core] { return core.state(); };
    pump->on_action = [&core](const mirage::desktop::TrayAction action) { core.on_action(action); };
    // A carrier loop that ends on its own (indicator refused, host
    // vanished) takes the process with it — loudly; the quit-flag path is
    // the clean exit and reports nothing.
    pump->on_exit = [](const mirage::desktop::TrayCarrier::RunReport &report) {
        if (!report.clean && !g_quit.load()) {
            std::cerr << "mirage-tray: tray carrier exited: " << report.diagnostic << '\n';
        }
        g_quit.store(true);
    };
    pump_spec.worker = std::move(pump);
    const executor::WorkerHandle pump_worker = executor.start_worker(std::move(pump_spec));
    if (!pump_worker.started()) {
        std::cerr << kProgramName
                  << ": carrier pump start failed: " << pump_worker.start_result().message << '\n';
        executor.shutdown(false);
        return 1;
    }

    if (!core.start()) {
        // The reconnect cadence keeps trying; the tray presents the
        // degraded state meanwhile (DEC-030 decision 6).
        std::cerr << kProgramName
                  << ": not connected yet; the tray keeps retrying in the background\n";
    }

    std::cout << kProgramName << " serving the indicator (service at "
              << (socket_path.empty() ? std::string("default endpoint") : socket_path) << ")\n";
    // Block until quit: the pump's exit is the ordered stop signal. The
    // pump worker is joined by its handle's stop below once quit rose.
    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Ordered teardown (AGENTS.md rule 7): stop the presentation loop,
    // stop the session state machine and its worker, drain, shut down.
    executor::WorkerHandle stop_handle = pump_worker;
    stop_handle.stop();
    core.shutdown();
    executor.shutdown(true);
    std::cout << kProgramName << " stopped\n";
    return 0;
}
