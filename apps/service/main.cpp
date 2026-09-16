#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <fcntl.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

constexpr std::string_view kProgramName = "mirage-service";
constexpr std::string_view kVersion = MIRAGE_VERSION;

// DEC-007: SIGINT/SIGTERM reach the service through a self-pipe registered
// with the IPC loop; the handler itself stays async-signal-safe (one write).
int g_signal_pipe_write = -1;

extern "C" void on_signal(int) {
    if (g_signal_pipe_write >= 0) {
        const char token = 's';
        const ssize_t written = ::write(g_signal_pipe_write, &token, 1);
        (void)written; // async-signal-safe best effort
    }
}

void print_usage(std::ostream& out) {
    out << "Usage: " << kProgramName << " [--socket PATH]\n"
        << "\n"
        << "Hosts the Mirage background runtime service (design doc section\n"
        << "12): a pinned Mira instance bound to the local desktop\n"
        << "environment, serving the Local IPC surface (DEC-007). Runs in\n"
        << "the foreground until a service.shutdown IPC request or\n"
        << "SIGINT/SIGTERM.\n"
        << "\n"
        << "Options:\n"
        << "  --socket PATH   IPC endpoint (default: XDG runtime dir)\n"
        << "  --version       Print versions\n"
        << "  --help          Print this help\n";
}

void print_version() {
    std::cout << kProgramName << ' ' << kVersion << '\n';
    const mirage::runtime::MiraCoreVersion core =
        mirage::runtime::mira_core_version();
    std::cout << "mira core " << core.major << '.' << core.minor << '.'
              << core.patch << '\n';
}

} // namespace

int main(int argc, char** argv) {
    mirage::runtime::ServiceConfig config;
    config.mirage_version = std::string(kVersion);

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--version") {
            print_version();
            return 0;
        }
        if (argument == "--socket" && index + 1 < argc) {
            config.socket_path = argv[++index];
            continue;
        }
        std::cerr << kProgramName << ": unknown argument '" << argument
                  << "'\n";
        print_usage(std::cerr);
        return 2;
    }

    // The M1 reference topology binds the Linux backend (DEC-008 item 3:
    // development and test topologies until M1-05/M1-06 tighten the
    // providers).
    auto environment =
        std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>();
    auto binding =
        std::make_shared<mirage::integration::MiraEnvironmentBinding>(
            environment);

    ::signal(SIGPIPE, SIG_IGN);
    int signal_pipe[2] = {-1, -1};
    // Non-blocking: the loop drains it only on a poll event, and the signal
    // handler's single-byte write must never block either.
    if (::pipe2(signal_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::cerr << kProgramName << ": cannot create the signal pipe\n";
        return 1;
    }
    g_signal_pipe_write = signal_pipe[1];
    struct sigaction action {};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);

    mirage::runtime::RuntimeService service(config);
    service.register_shutdown_fd(signal_pipe[0]);

    const mirage::runtime::HostOutcome started = service.start(binding);
    if (!started.ok) {
        std::cerr << kProgramName << ": start failed ("
                  << started.error.code << "): " << started.error.message
                  << '\n';
        return 1;
    }
    std::cout << kProgramName << " serving at " << service.socket_path()
              << '\n'
              << std::flush;

    const mirage::runtime::ServiceRunReport report = service.run();
    if (!report.clean) {
        std::cerr << kProgramName << ": shutdown not clean: "
                  << report.diagnostic << '\n';
        return 1;
    }
    std::cout << kProgramName << " stopped cleanly\n";
    return 0;
}
