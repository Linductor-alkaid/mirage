#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>

#include <bridge_loop.hpp>
#include <websocket.hpp>

#include <mirage/runtime/ipc/endpoint.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

constexpr std::string_view kProgramName = "mirage-devbridge";
constexpr std::string_view kVersion = MIRAGE_VERSION;
constexpr int kDefaultPort = 8787;
constexpr std::size_t kDefaultMaxSessions = 4;

// DEC-012 decision 6 / AGENTS.md rule 7: SIGINT/SIGTERM reach the bridge
// loop through a self-pipe registered as its shutdown fd; the handler stays
// async-signal-safe (one write).
int g_signal_pipe_write = -1;

extern "C" void on_signal(int) {
    if (g_signal_pipe_write >= 0) {
        const char token = 's';
        const ssize_t written = ::write(g_signal_pipe_write, &token, 1);
        (void)written; // async-signal-safe best effort
    }
}

void print_usage(std::ostream &out) {
    out << "Usage: " << kProgramName << " [--socket PATH] [--port PORT]\n"
        << "                     [--max-sessions N]\n"
        << "\n"
        << "Development-time bridge (M1.5-03, DEC-012 decision 6) that\n"
        << "forwards Local IPC length-prefixed frames between a WebSocket\n"
        << "client (the browser UI) and a real mirage-service Unix domain\n"
        << "socket. One binary WebSocket message carries exactly one IPC\n"
        << "frame; payloads pass through verbatim. Development tool only:\n"
        << "it ships in no product package and binds to 127.0.0.1.\n"
        << "\n"
        << "Options:\n"
        << "  --socket PATH          Upstream IPC endpoint (default: XDG\n"
        << "                         runtime dir, same as mirage-service)\n"
        << "  --port PORT            WebSocket listen port on 127.0.0.1\n"
        << "                         (default: " << kDefaultPort << "; 0 picks a free port)\n"
        << "  --max-sessions N       Concurrent browser sessions (default: " << kDefaultMaxSessions
        << ")\n"
        << "  --version              Print versions\n"
        << "  --help                 Print this help\n";
}

void print_version() { std::cout << kProgramName << ' ' << kVersion << '\n'; }

/// Creates the non-blocking listening TCP socket on 127.0.0.1:`port`;
/// returns -1 with `diagnostic` on failure and reports the bound port back.
int bind_loopback(int port, int &bound_port, std::string &diagnostic) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        diagnostic = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    int reuse = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        diagnostic =
            std::string("bind 127.0.0.1:") + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) != 0) {
        diagnostic = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    sockaddr_in resolved{};
    socklen_t resolved_size = sizeof(resolved);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&resolved), &resolved_size) != 0) {
        diagnostic = std::string("getsockname: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    bound_port = ntohs(resolved.sin_port);
    return fd;
}

} // namespace

int main(int argc, char **argv) {
    std::optional<std::string> flag_socket;
    int port = kDefaultPort;
    std::size_t max_sessions = kDefaultMaxSessions;

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
            flag_socket = argv[++index];
            continue;
        }
        if (argument == "--port" && index + 1 < argc) {
            const std::string raw{argv[++index]};
            const char *begin = raw.c_str();
            char *end = nullptr;
            const long parsed = std::strtol(begin, &end, 10);
            if (*begin == '\0' || end == nullptr || *end != '\0' || parsed < 0 || parsed > 65535) {
                std::cerr << kProgramName << ": --port expects 0..65535 (got '" << raw << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            port = static_cast<int>(parsed);
            continue;
        }
        if (argument == "--max-sessions" && index + 1 < argc) {
            const std::string raw{argv[++index]};
            const char *begin = raw.c_str();
            char *end = nullptr;
            const long parsed = std::strtol(begin, &end, 10);
            if (*begin == '\0' || end == nullptr || *end != '\0' || parsed <= 0 || parsed > 64) {
                std::cerr << kProgramName << ": --max-sessions expects 1..64 (got '" << raw
                          << "')\n";
                print_usage(std::cerr);
                return 2;
            }
            max_sessions = static_cast<std::size_t>(parsed);
            continue;
        }
        std::cerr << kProgramName << ": unknown argument '" << argument << "'\n";
        print_usage(std::cerr);
        return 2;
    }

    const std::string upstream_path =
        flag_socket.value_or(mirage::runtime::ipc::default_socket_path());

    int bound_port = 0;
    std::string diagnostic;
    const int listen_fd = bind_loopback(port, bound_port, diagnostic);
    if (listen_fd < 0) {
        std::cerr << kProgramName << ": cannot listen: " << diagnostic << '\n';
        return 1;
    }

    ::signal(SIGPIPE, SIG_IGN);
    int signal_pipe[2] = {-1, -1};
    // Non-blocking: the loop drains it only on a poll event, and the signal
    // handler's single-byte write must never block either.
    if (::pipe2(signal_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::cerr << kProgramName << ": cannot create the signal pipe\n";
        ::close(listen_fd);
        return 1;
    }
    g_signal_pipe_write = signal_pipe[1];
    struct sigaction action {};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);

    // This process's only Executor instance lives in main (AGENTS.md rule
    // 7): the bridge loop is its single blocking I/O worker, and shutdown
    // runs here on the non-worker thread.
    executor::Executor executor;
    executor::ExecutorConfig executor_config;
    executor_config.max_threads = 2;
    const auto initialized = executor.initialize_ex(executor_config);
    if (!initialized.ok) {
        std::cerr << kProgramName << ": executor initialization failed: " << initialized.message
                  << '\n';
        ::close(listen_fd);
        return 1;
    }

    mirage::devbridge::DevBridgeLoop::Dependencies dependencies;
    dependencies.listen_fd = listen_fd;
    dependencies.upstream_socket_path = upstream_path;
    dependencies.max_sessions = max_sessions;
    std::promise<mirage::devbridge::BridgeRunReport> exit_promise;
    auto exit_future = exit_promise.get_future();
    dependencies.on_exit = [&exit_promise](mirage::devbridge::BridgeRunReport report) {
        exit_promise.set_value(std::move(report));
    };
    auto owned_loop = std::make_unique<mirage::devbridge::DevBridgeLoop>(std::move(dependencies));
    owned_loop->register_shutdown_fd(signal_pipe[0]);

    executor::BlockingWorkerSpec spec;
    spec.name = "mirage-devbridge-loop";
    spec.config.thread_name = "mirage-devbridge-loop";
    // The executor's blocking worker owns the loop from here on.
    spec.worker = std::move(owned_loop);
    executor::WorkerHandle loop_worker = executor.start_worker(std::move(spec));
    if (!loop_worker.started()) {
        std::cerr << kProgramName
                  << ": blocking worker start failed: " << loop_worker.start_result().message
                  << '\n';
        ::close(listen_fd);
        executor.shutdown(false);
        return 1;
    }

    std::cout << kProgramName << " ws://127.0.0.1:" << bound_port << " -> ipc://" << upstream_path
              << '\n'
              << "press Ctrl-C to stop\n"
              << std::flush;

    // Blocks until the loop exits (SIGINT/SIGTERM via the self-pipe).
    mirage::devbridge::BridgeRunReport report;
    try {
        report = exit_future.get();
    } catch (const std::exception &error) {
        report.clean = false;
        report.diagnostic = std::string("bridge loop exit lost: ") + error.what();
    }

    // Ordered teardown (AGENTS.md rule 7): recover the worker, then shut
    // the executor down from this non-worker thread.
    loop_worker.stop();
    ::close(listen_fd);
    executor.shutdown(true);

    if (!report.clean) {
        std::cerr << kProgramName << ": shutdown not clean: " << report.diagnostic << '\n';
        return 1;
    }
    std::cout << kProgramName << " stopped cleanly\n";
    return 0;
}
