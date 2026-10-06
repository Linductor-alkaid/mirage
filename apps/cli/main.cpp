#include <mirage/platform/platform_info.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <iterator>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view kProgramName = "mirage";
constexpr std::string_view kVersion = MIRAGE_VERSION;

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitUnreachable = 3;

constexpr auto kDefaultCallTimeout = std::chrono::milliseconds{10000};

// --- argument helpers -------------------------------------------------------

struct GlobalOptions {
    std::string socket_path;
    std::chrono::milliseconds call_timeout = kDefaultCallTimeout;
};

bool parse_common_option(const std::string &name, const std::string &value,
                         GlobalOptions &options) {
    if (name == "--socket") {
        options.socket_path = value;
        return true;
    }
    if (name == "--timeout") {
        options.call_timeout = std::chrono::milliseconds{std::stol(value)};
        return true;
    }
    return false;
}

bool takes_value(const std::string &name) {
    return name == "--socket" || name == "--timeout" || name == "--goal" || name == "--read" ||
           name == "--exec" || name == "--step-timeout" || name == "--wait" ||
           name == "--read-root" || name == "--perm" || name == "--confirm" || name == "--config" ||
           name == "--state-dir" || name == "--session" || name == "--limit" || name == "--file" ||
           name == "--parameters-file" || name == "--digest" || name == "--policy" ||
           name == "--shell" || name == "--tray";
}

/// Splits `--name value` / `--name=value` pairs; returns false on usage
/// errors (unknown flag, missing value) with a message printed. Operands
/// (non-flag words) collect into `operands`.
bool consume_options(int argc, char **argv, int &index,
                     const std::function<bool(const std::string &, const std::string &)> &on_option,
                     std::vector<std::string> &operands) {
    while (index < argc) {
        std::string argument = argv[index];
        if (!argument.starts_with("--")) {
            operands.push_back(argument);
            ++index;
            continue;
        }
        std::string name = argument;
        std::string value;
        const auto equals = argument.find('=');
        bool has_value = false;
        if (equals != std::string::npos) {
            name = argument.substr(0, equals);
            value = argument.substr(equals + 1);
            has_value = true;
        }
        if (!takes_value(name)) {
            if (has_value) {
                std::cerr << kProgramName << ": option '" << name << "' takes no value\n";
                return false;
            }
            if (!on_option(name, "")) {
                std::cerr << kProgramName << ": unknown option '" << name << "'\n";
                return false;
            }
            ++index;
            continue;
        }
        if (!has_value) {
            if (index + 1 >= argc) {
                std::cerr << kProgramName << ": option '" << name << "' requires a value\n";
                return false;
            }
            value = argv[++index];
        }
        if (!on_option(name, value)) {
            std::cerr << kProgramName << ": unknown option '" << name << "'\n";
            return false;
        }
        ++index;
    }
    return true;
}

// --- ipc plumbing -----------------------------------------------------------

mirage::runtime::ipc::IpcClient client_for(const GlobalOptions &options) {
    return mirage::runtime::ipc::IpcClient(options.socket_path.empty()
                                               ? mirage::runtime::ipc::default_socket_path()
                                               : options.socket_path);
}

int report_response(const mirage::runtime::ipc::Response &response) {
    if (!response.ok) {
        std::cerr << kProgramName << ": service error (" << response.error.code
                  << "): " << response.error.message << '\n';
        return response.error.code == "unavailable" ? kExitUnreachable : kExitFailure;
    }
    return kExitOk;
}

void print_identity(const mirage::runtime::ipc::ServiceIdentity &identity,
                    const std::string &endpoint) {
    std::cout << "service: " << identity.name << '\n'
              << "mirage: " << identity.mirage_version << '\n'
              << "mira core: " << identity.mira_core_version << '\n'
              << "host status: " << identity.host_status << '\n'
              << "protocol: " << identity.protocol << '\n'
              << "endpoint: " << endpoint << '\n';
}

// --- service commands -------------------------------------------------------

int command_service_status(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response = client.call(mirage::runtime::ipc::HelloRequest{}, options.call_timeout);
    if (!response.ok) {
        std::cerr << kProgramName << ": service not reachable (" << response.error.code
                  << "): " << response.error.message << '\n';
        return kExitUnreachable;
    }
    print_identity(std::get<mirage::runtime::ipc::ServiceIdentity>(response.payload),
                   options.socket_path.empty() ? mirage::runtime::ipc::default_socket_path()
                                               : options.socket_path);
    return kExitOk;
}

int command_service_shutdown(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::ShutdownRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "service shutdown requested\n";
    return kExitOk;
}

int command_service_start(int argc, char **argv) {
    GlobalOptions options;
    std::chrono::milliseconds wait{10000};
    std::vector<std::string> read_roots;
    std::vector<std::string> perm_flags;
    std::string confirm_mode;
    std::string confirm_wait_ms;
    std::string config_path;
    std::string state_dir;
    bool no_recovery = false;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--wait") {
                    wait = std::chrono::seconds{std::stol(value)};
                    return true;
                }
                if (name == "--read-root") {
                    read_roots.push_back(value);
                    return true;
                }
                if (name == "--config") {
                    config_path = value;
                    return true;
                }
                if (name == "--state-dir") {
                    state_dir = value;
                    return true;
                }
                if (name == "--no-recovery") {
                    no_recovery = true;
                    return true;
                }
                if (name == "--perm") {
                    // Validated here so a typo fails fast instead of
                    // surfacing as a service startup failure (DEC-010).
                    const auto equals = value.find('=');
                    const std::string_view text{value};
                    if (equals == std::string::npos ||
                        !mirage::runtime::permission::capability_from_name(
                            text.substr(0, equals)) ||
                        !mirage::runtime::permission::rule_from_name(text.substr(equals + 1))) {
                        std::cerr << kProgramName
                                  << ": --perm expects "
                                     "CAPABILITY=allow|confirm|deny (got '"
                                  << value << "')\n";
                        return false;
                    }
                    perm_flags.push_back(value);
                    return true;
                }
                if (name == "--confirm") {
                    // M5-03 (DEC-020): `ipc` selects the service's async
                    // confirmation surface and is passed through verbatim.
                    if (value != "allow" && value != "deny" && value != "ipc") {
                        std::cerr << kProgramName << ": --confirm expects allow|deny|ipc (got '"
                                  << value << "')\n";
                        return false;
                    }
                    confirm_mode = value;
                    return true;
                }
                if (name == "--confirm-wait-ms") {
                    // Forwarded to mirage-service --confirm ipc verbatim;
                    // positivity is validated by the service.
                    const std::string_view text{value};
                    if (text.empty() ||
                        text.find_first_not_of("0123456789") != std::string_view::npos) {
                        std::cerr << kProgramName
                                  << ": --confirm-wait-ms expects a positive integer (got '"
                                  << value << "')\n";
                        return false;
                    }
                    confirm_wait_ms = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    // Locate the sibling mirage-service binary next to this executable.
#ifdef _WIN32
    wchar_t self_wide[4096];
    const DWORD self_length =
        ::GetModuleFileNameW(nullptr, self_wide, static_cast<DWORD>(std::size(self_wide)));
    if (self_length == 0 || self_length >= std::size(self_wide)) {
        std::cerr << kProgramName << ": cannot resolve the executable path\n";
        return kExitFailure;
    }
    std::filesystem::path service_path =
        std::filesystem::path(self_wide).parent_path() / "mirage-service.exe";
    if (!std::filesystem::exists(service_path)) {
        service_path = std::filesystem::path(self_wide).parent_path() / "mirage-service";
    }
    const std::string service_binary = service_path.string();
    const std::string service_display = service_path.string();
#else
    char self_path[4096];
    const ssize_t length = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (length <= 0) {
        std::cerr << kProgramName << ": cannot resolve the executable path\n";
        return kExitFailure;
    }
    self_path[length] = '\0';
    const std::string self{self_path};
    const auto slash = self.rfind('/');
    const std::string service_binary =
        (slash == std::string::npos ? std::string(".") : self.substr(0, slash)) + "/mirage-service";
    const std::string &service_display = service_binary;
#endif

    const std::string socket = options.socket_path.empty()
                                   ? mirage::runtime::ipc::default_socket_path()
                                   : options.socket_path;
    std::optional<long> child_pid;
#ifdef _WIN32
    std::string arguments = "\"" + service_binary + "\" --socket \"" + socket + "\"";
    for (const std::string &root : read_roots) {
        arguments += " --read-root \"" + root + "\"";
    }
    for (const std::string &perm : perm_flags) {
        arguments += " --perm " + perm;
    }
    if (!confirm_mode.empty()) {
        arguments += " --confirm " + confirm_mode;
    }
    if (!config_path.empty()) {
        arguments += " --config \"" + config_path + "\"";
    }
    if (!state_dir.empty()) {
        arguments += " --state-dir \"" + state_dir + "\"";
    }
    if (no_recovery) {
        arguments += " --no-recovery";
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessA(nullptr, arguments.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                          &startup, &process)) {
        std::cerr << kProgramName << ": CreateProcess for " << service_binary
                  << " failed (Win32 error " << ::GetLastError() << ")\n";
        return kExitFailure;
    }
    child_pid = static_cast<long>(process.dwProcessId);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
#else
    const pid_t child = ::fork();
    if (child < 0) {
        std::cerr << kProgramName << ": fork failed\n";
        return kExitFailure;
    }
    child_pid = static_cast<long>(child);
    if (child == 0) {
        // Daemon discipline (BUG-20260916-001, M5-08): the long-lived child
        // must not hold the caller's stdio — a pipe reader would otherwise
        // wait for the service's lifetime EOF. Redirect all three standard
        // descriptors to /dev/null before exec; the readiness probe talks
        // over the socket, not over stdio.
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                ::close(null_fd);
            }
        }
        // Child: become the service (DEC-007 item 6). The parent exits as
        // soon as the endpoint answers hello.
        std::vector<char *> argv_child;
        argv_child.push_back(const_cast<char *>(service_binary.c_str()));
        argv_child.push_back(const_cast<char *>("--socket"));
        argv_child.push_back(const_cast<char *>(socket.c_str()));
        for (const std::string &root : read_roots) {
            argv_child.push_back(const_cast<char *>("--read-root"));
            argv_child.push_back(const_cast<char *>(root.c_str()));
        }
        for (const std::string &perm : perm_flags) {
            argv_child.push_back(const_cast<char *>("--perm"));
            argv_child.push_back(const_cast<char *>(perm.c_str()));
        }
        if (!confirm_mode.empty()) {
            argv_child.push_back(const_cast<char *>("--confirm"));
            argv_child.push_back(const_cast<char *>(confirm_mode.c_str()));
        }
        if (!confirm_wait_ms.empty()) {
            argv_child.push_back(const_cast<char *>("--confirm-wait-ms"));
            argv_child.push_back(const_cast<char *>(confirm_wait_ms.c_str()));
        }
        if (!config_path.empty()) {
            argv_child.push_back(const_cast<char *>("--config"));
            argv_child.push_back(const_cast<char *>(config_path.c_str()));
        }
        if (!state_dir.empty()) {
            argv_child.push_back(const_cast<char *>("--state-dir"));
            argv_child.push_back(const_cast<char *>(state_dir.c_str()));
        }
        if (no_recovery) {
            argv_child.push_back(const_cast<char *>("--no-recovery"));
        }
        argv_child.push_back(nullptr);
        ::execv(service_binary.c_str(), argv_child.data());
        ::_exit(127);
    }
#endif
    // Parent: wait for readiness by probing the endpoint.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    auto probe_client = client_for(options);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto probe =
            probe_client.call(mirage::runtime::ipc::HelloRequest{}, std::chrono::milliseconds{500});
        if (probe.ok) {
            std::cout << "service ready at " << socket << " (pid " << *child_pid << ")\n";
            print_identity(std::get<mirage::runtime::ipc::ServiceIdentity>(probe.payload), socket);
            return kExitOk;
        }
#ifdef _WIN32
        ::Sleep(100);
#else
        ::usleep(100 * 1000);
#endif
    }
    std::cerr << kProgramName << ": service did not become ready within "
              << std::chrono::duration_cast<std::chrono::seconds>(wait).count() << "s (check "
              << service_display << " output)\n";
    return kExitFailure;
}

// --- product start -----------------------------------------------------------

/// Resolves a sibling product binary next to this executable (the same
/// layout rule the service spawn uses).
std::optional<std::string> sibling_binary(const std::string &name) {
#ifdef _WIN32
    wchar_t self_wide[4096];
    const DWORD self_length =
        ::GetModuleFileNameW(nullptr, self_wide, static_cast<DWORD>(std::size(self_wide)));
    if (self_length == 0 || self_length >= std::size(self_wide)) {
        return std::nullopt;
    }
    std::filesystem::path path = std::filesystem::path(self_wide).parent_path() / (name + ".exe");
    if (!std::filesystem::exists(path)) {
        path = std::filesystem::path(self_wide).parent_path() / name;
    }
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    return path.string();
#else
    char self_path[4096];
    const ssize_t length = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    self_path[length] = '\0';
    const std::string self{self_path};
    const auto slash = self.rfind('/');
    const std::string path =
        (slash == std::string::npos ? std::string(".") : self.substr(0, slash)) + "/" + name;
    if (!std::filesystem::exists(path)) {
        const auto development = std::filesystem::path(path).parent_path() /
                                 (name == "mirage-tray" ? "tray" : "native") / name;
        if ((name == "mirage-native" || name == "mirage-tray") &&
            std::filesystem::exists(development))
            return development.string();
        return std::nullopt;
    }
    return path;
#endif
}

/// Detached product-face spawn: the child must outlive this short-lived
/// launcher and must not hold its stdio (the same pipe-EOF discipline as the
/// service daemon, BUG-20260916-001). Returns the child pid, or nullopt when
/// the spawn itself failed.
std::optional<long> spawn_product_tray(const std::string &binary, const std::string &socket_path,
                                       const std::string &shell, bool open_frontend) {
#ifdef _WIN32
    auto quote = [](const std::string &value) {
        std::string result = "\"";
        std::size_t slashes = 0;
        for (char character : value) {
            if (character == '\\') {
                ++slashes;
                continue;
            }
            result.append(slashes * (character == '"' ? 2 : 1), '\\');
            slashes = 0;
            if (character == '"')
                result += '\\';
            result += character;
        }
        result.append(slashes * 2, '\\');
        return result + "\"";
    };
    std::string command = quote(binary);
    if (!shell.empty())
        command += " --shell " + quote(shell);
    if (!open_frontend)
        command += " --no-shell";
    if (!socket_path.empty())
        command += " --socket " + quote(socket_path);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    const auto *previous = std::getenv("MIRAGE_NATIVE_SOCKET");
    const std::string previous_value = previous ? previous : "";
    if (!socket_path.empty())
        (void)::_putenv_s("MIRAGE_NATIVE_SOCKET", socket_path.c_str());
    const BOOL created = ::CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, flags,
                                          nullptr, nullptr, &startup, &process);
    if (!socket_path.empty())
        (void)::_putenv_s("MIRAGE_NATIVE_SOCKET", previous_value.c_str());
    if (!created) {
        return std::nullopt;
    }
    const long pid = static_cast<long>(process.dwProcessId);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return pid;
#else
    const pid_t child = ::fork();
    if (child < 0) {
        return std::nullopt;
    }
    if (child == 0) {
        ::setsid();
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                ::close(null_fd);
            }
        }
        if (!socket_path.empty() && ::setenv("MIRAGE_NATIVE_SOCKET", socket_path.c_str(), 1) != 0)
            ::_exit(127);
        if (open_frontend)
            ::execl(binary.c_str(), binary.c_str(), "--socket", socket_path.c_str(), "--shell",
                    shell.c_str(), static_cast<char *>(nullptr));
        else
            ::execl(binary.c_str(), binary.c_str(), "--socket", socket_path.c_str(), "--shell",
                    shell.c_str(), "--no-shell", static_cast<char *>(nullptr));
        ::_exit(127);
    }
    return static_cast<long>(child);
#endif
}

/// Liveness probe for a spawned face pid. On POSIX the child is reaped
/// first (WNOHANG): an already-exited child is a zombie, and a bare
/// kill(pid, 0) reports zombies — e.g. a failed exec — as alive.
bool spawned_face_alive(long pid) {
#ifdef _WIN32
    const HANDLE handle =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    DWORD code = 0;
    const bool alive = ::GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    ::CloseHandle(handle);
    return alive;
#else
    return ::waitpid(static_cast<pid_t>(pid), nullptr, WNOHANG) == 0;
#endif
}

/// DEC-045: bootstrap/reuse the tray-owned Runtime; the tray owns its UI child.
/// This finite launcher waits for registration plus frontend readiness and exits.
int command_start(int argc, char **argv) {
    GlobalOptions options;
    std::chrono::milliseconds wait{10000};
    bool with_shell = true;
    bool with_tray = true;
    std::string shell_binary;
    std::string tray_binary;
    int index = 2; // skip the program name and "start"
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--wait") {
                    wait = std::chrono::seconds{std::stol(value)};
                    return true;
                }
                if (name == "--no-shell") {
                    with_shell = false;
                    return true;
                }
                if (name == "--no-tray") {
                    with_tray = false;
                    return true;
                }
                if (name == "--shell") {
                    shell_binary = value;
                    return true;
                }
                if (name == "--tray") {
                    tray_binary = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    if (!extra.empty()) {
        std::cerr << kProgramName << ": 'start' takes no operands\n";
        return kExitUsage;
    }

    if (wait <= std::chrono::milliseconds{0}) {
        std::cerr << "mirage: --wait must be positive\n";
        return kExitUsage;
    }
    if (!with_tray) {
        if (with_shell) {
            std::cerr << "mirage: frontend requires the tray; --no-tray only supports --no-shell\n";
            return kExitUsage;
        }
        // Explicit developer/headless mode. It never opens the product UI.
        auto probe = client_for(options);
        if (probe.call(mirage::runtime::ipc::HelloRequest{}, std::chrono::milliseconds{500}).ok)
            return kExitOk;
        std::vector<std::string> values{
            "mirage", "service", "start", "--wait",
            std::to_string(std::chrono::duration_cast<std::chrono::seconds>(wait).count())};
        if (!options.socket_path.empty()) {
            values.push_back("--socket");
            values.push_back(options.socket_path);
        }
        std::vector<char *> arguments;
        for (auto &value : values)
            arguments.push_back(value.data());
        return command_service_start(static_cast<int>(arguments.size()), arguments.data());
    }
    if (options.socket_path.empty())
        options.socket_path = mirage::runtime::ipc::default_socket_path();
    auto probe = client_for(options);
    auto hello = probe.call(mirage::runtime::ipc::HelloRequest{}, std::chrono::milliseconds{500});
    bool reused = false;
    std::optional<long> tray_pid;
    if (hello.ok) {
        const auto *identity = std::get_if<mirage::runtime::ipc::ServiceIdentity>(&hello.payload);
        if (!identity || !identity->tray.has_value()) {
            std::cerr << "mirage: endpoint is owned by a headless/old service; close that service "
                         "before starting the tray product\n";
            return kExitFailure;
        }
        reused = true;
    } else {
        const auto tray = tray_binary.empty() ? sibling_binary("mirage-tray")
                                              : std::optional<std::string>{tray_binary};
        const auto shell = shell_binary.empty() ? sibling_binary("mirage-native")
                                                : std::optional<std::string>{shell_binary};
        if (!tray || !std::filesystem::is_regular_file(*tray) || !shell ||
            !std::filesystem::is_regular_file(*shell)) {
            std::cerr << "mirage: tray/frontend executable missing; startup refused\n";
            return kExitFailure;
        }
        tray_pid = spawn_product_tray(*tray, options.socket_path, *shell, with_shell);
        if (!tray_pid) {
            std::cerr << "mirage: tray spawn failed\n";
            return kExitFailure;
        }
    }
    bool requested_open = !reused || !with_shell;
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (tray_pid && !spawned_face_alive(*tray_pid)) {
            std::cerr << "mirage: tray exited during startup (notification host, settings or "
                         "frontend failure)\n";
            return kExitFailure;
        }
        hello = probe.call(mirage::runtime::ipc::HelloRequest{}, std::chrono::milliseconds{500});
        const auto *identity = std::get_if<mirage::runtime::ipc::ServiceIdentity>(&hello.payload);
        if (hello.ok && identity && identity->tray.value_or(false)) {
            if (!requested_open) {
                const auto opened =
                    probe.call(mirage::runtime::ipc::ProductControlRequest{"open", 0},
                               std::chrono::milliseconds{2000});
                if (!opened.ok) {
                    std::cerr << "mirage: frontend open refused: " << opened.error.message << '\n';
                    return kExitFailure;
                }
                requested_open = true;
            }
            const auto response = probe.call(mirage::runtime::ipc::ProductControlRequest{},
                                             std::chrono::milliseconds{1000});
            const auto *product =
                std::get_if<mirage::runtime::ipc::ProductState>(&response.payload);
            if (response.ok && product && (!with_shell || product->frontend_ready)) {
                std::cout << (reused ? "tray runtime reused at " : "tray runtime ready at ")
                          << options.socket_path;
                if (tray_pid)
                    std::cout << " (pid " << *tray_pid << ")";
                std::cout << '\n';
                if (with_shell)
                    std::cout << "mirage-native running (pid " << product->frontend_pid << ")\n";
                return kExitOk;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    // Only the newly spawned child belongs to this launcher. Never stop a reused host.
    if (tray_pid) {
#ifdef _WIN32
        const HANDLE child =
            ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(*tray_pid));
        if (child) {
            ::TerminateProcess(child, 1);
            ::WaitForSingleObject(child, 3000);
            ::CloseHandle(child);
        }
#else
        ::kill(static_cast<pid_t>(*tray_pid), SIGTERM);
        const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (spawned_face_alive(*tray_pid) && std::chrono::steady_clock::now() < stop_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        if (spawned_face_alive(*tray_pid)) {
            ::kill(static_cast<pid_t>(*tray_pid), SIGKILL);
            (void)::waitpid(static_cast<pid_t>(*tray_pid), nullptr, 0);
        }
#endif
    }
    std::cerr << "mirage: tray/frontend readiness timed out\n";
    return kExitFailure;
}

// --- task commands ----------------------------------------------------------

int command_task_submit(int argc, char **argv) {
    GlobalOptions options;
    mirage::runtime::ipc::SubmitTaskRequest request;
    std::optional<std::chrono::milliseconds> step_timeout;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--goal") {
                    request.goal = value;
                    return true;
                }
                if (name == "--read") {
                    request.steps.push_back(
                        {mirage::runtime::ipc::StepKind::FilesystemRead, value});
                    return true;
                }
                if (name == "--exec") {
                    request.steps.push_back(
                        {mirage::runtime::ipc::StepKind::ProcessExecute, value});
                    return true;
                }
                if (name == "--step-timeout") {
                    step_timeout = std::chrono::milliseconds{std::stol(value)};
                    return true;
                }
                if (name == "--session") {
                    request.session_id = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    if (request.goal.empty()) {
        std::cerr << kProgramName << ": task submit requires --goal TEXT\n";
        return kExitUsage;
    }
    request.step_timeout = step_timeout;
    auto client = client_for(options);
    const auto response = client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "task " << std::get<mirage::runtime::ipc::TaskSubmitted>(response.payload).task_id
              << '\n';
    return kExitOk;
}

int command_task_list(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::ListTasksRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto list = std::get<mirage::runtime::ipc::TaskList>(response.payload);
    for (const auto &task : list.tasks) {
        std::cout << task.id << ' ' << task.progress << ' ' << task.goal << '\n';
    }
    std::cout << list.tasks.size() << " task(s)\n";
    return kExitOk;
}

int command_task_cancel(int argc, char **argv) {
    std::string task_id;
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> operands;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (operand.starts_with("--")) {
            std::cerr << kProgramName << ": unknown option '" << operand << "'\n";
            return kExitUsage;
        }
        if (!task_id.empty()) {
            std::cerr << kProgramName << ": cancel takes one task id\n";
            return kExitUsage;
        }
        task_id = operand;
    }
    if (task_id.empty()) {
        std::cerr << kProgramName << ": task cancel requires a task id\n";
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::CancelTaskRequest{task_id}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto cancelled = std::get<mirage::runtime::ipc::TaskCancelled>(response.payload);
    std::cout << "task " << cancelled.task_id << " cancel requested (" << cancelled.progress
              << ")\n";
    return kExitOk;
}

int command_task_inspect(int argc, char **argv) {
    std::string task_id;
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> operands;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (operand.starts_with("--")) {
            std::cerr << kProgramName << ": unknown option '" << operand << "'\n";
            return kExitUsage;
        }
        if (!task_id.empty()) {
            std::cerr << kProgramName << ": inspect takes one task id\n";
            return kExitUsage;
        }
        task_id = operand;
    }
    if (task_id.empty()) {
        std::cerr << kProgramName << ": task inspect requires a task id\n";
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::InspectTaskRequest{task_id}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto task = std::get<mirage::runtime::ipc::InspectTask>(response.payload);
    std::cout << "task " << task.id << '\n'
              << "goal: " << task.goal << '\n'
              << "progress: " << task.progress << '\n';
    if (task.has_success) {
        std::cout << "success: " << (task.success ? "yes" : "no") << '\n';
    }
    if (!task.steps.empty()) {
        std::cout << "steps:\n";
        for (const auto &step : task.steps) {
            std::cout << "  [" << step.index << "] " << step.kind << ' ' << step.status;
            if (!step.operation_id.empty()) {
                std::cout << " (op " << step.operation_id << ')';
            }
            if (!step.permission.empty()) {
                std::cout << " perm=" << step.permission;
            }
            if (step.kind == "process.execute") {
                std::cout << " exit=" << step.exit_code;
            }
            std::cout << '\n';
            if (!step.error.empty()) {
                std::cout << "      error: " << step.error << '\n';
            }
            if (!step.result.empty()) {
                std::cout << "      result" << (step.result_truncated ? " (truncated)" : "")
                          << ":\n";
                // Indent the structured result for readability.
                std::string line;
                for (const char character : step.result) {
                    if (character == '\n') {
                        std::cout << "      " << line << '\n';
                        line.clear();
                        continue;
                    }
                    line.push_back(character);
                }
                if (!line.empty()) {
                    std::cout << "      " << line << '\n';
                }
            }
        }
    }
    return kExitOk;
}

// --- session commands (DEC-021) ---------------------------------------------

int command_session_list(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "session" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::ListSessionsRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto list = std::get<mirage::runtime::ipc::SessionList>(response.payload);
    for (const auto &session : list.sessions) {
        std::cout << session.id << ' ' << session.state << '\n';
    }
    std::cout << list.sessions.size() << " session(s)\n";
    return kExitOk;
}

int command_session_open(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "session" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::OpenSessionRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "session "
              << std::get<mirage::runtime::ipc::SessionOpened>(response.payload).session_id << '\n';
    return kExitOk;
}

int command_session_history(int argc, char **argv) {
    std::string session_id;
    std::optional<int> limit;
    GlobalOptions options;
    int index = 3; // skip "session" and the subcommand
    std::vector<std::string> operands;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--limit") {
                    limit = std::stoi(value);
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (operand.starts_with("--")) {
            std::cerr << kProgramName << ": unknown option '" << operand << "'\n";
            return kExitUsage;
        }
        if (!session_id.empty()) {
            std::cerr << kProgramName << ": history takes one session id\n";
            return kExitUsage;
        }
        session_id = operand;
    }
    if (session_id.empty()) {
        std::cerr << kProgramName << ": session history requires a session id\n";
        return kExitUsage;
    }
    auto client = client_for(options);
    mirage::runtime::ipc::SessionHistoryRequest request;
    request.session_id = session_id;
    request.limit = limit;
    const auto response = client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto history = std::get<mirage::runtime::ipc::SessionHistory>(response.payload);
    for (const auto &entry : history.entries) {
        std::cout << '[' << entry.sequence << "] " << entry.kind << ": " << entry.text << '\n';
    }
    std::cout << history.entries.size() << " entr" << (history.entries.size() == 1 ? "y" : "ies")
              << (history.truncated ? " (older entries truncated)\n" : "\n");
    return kExitOk;
}

// --- workflow commands (DEC-023) ---------------------------------------------

/// Reads a small text file whole; empty output on failure (the caller prints
/// the usage error). Used for the workflow definition and run parameters.
std::string read_whole_file(const std::string &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0) {
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::string content(size, static_cast<char>(0));
    stream.read(content.data(), static_cast<std::streamsize>(size));
    content.resize(static_cast<std::size_t>(stream.gcount()));
    return content;
}

int command_workflow_list(int argc, char **argv) {
    GlobalOptions options;
    int index = 3; // skip "workflow" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::WorkflowListRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto list = std::get<mirage::runtime::ipc::WorkflowList>(response.payload);
    for (const auto &workflow : list.workflows) {
        std::cout << workflow.workflow_id << ' ' << workflow.name << ' ' << workflow.head_digest
                  << ' ' << workflow.validation << ' ' << (workflow.runnable ? "runnable" : "draft")
                  << '\n';
    }
    std::cout << list.workflows.size() << " workflow(s)\n";
    return kExitOk;
}

int command_workflow_save(int argc, char **argv) {
    std::string file;
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--file") {
                    file = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    if (file.empty()) {
        std::cerr << kProgramName << ": workflow save requires --file PATH\n";
        return kExitUsage;
    }
    const std::string definition = read_whole_file(file);
    if (definition.empty()) {
        std::cerr << kProgramName << ": cannot read workflow definition '" << file << "'\n";
        return kExitFailure;
    }
    auto client = client_for(options);
    mirage::runtime::ipc::WorkflowSaveRequest request;
    request.definition_json = definition;
    const auto response = client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto saved = std::get<mirage::runtime::ipc::WorkflowSaved>(response.payload);
    std::cout << "workflow " << saved.workflow_id << '\n' << "draft " << saved.digest << '\n';
    return kExitOk;
}

int command_workflow_publish(int argc, char **argv) {
    std::string file;
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--file") {
                    file = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    if (file.empty()) {
        std::cerr << kProgramName << ": workflow publish requires --file PATH\n";
        return kExitUsage;
    }
    const std::string definition = read_whole_file(file);
    if (definition.empty()) {
        std::cerr << kProgramName << ": cannot read workflow definition '" << file << "'\n";
        return kExitFailure;
    }
    auto client = client_for(options);
    mirage::runtime::ipc::WorkflowPublishRequest request;
    request.definition_json = definition;
    const auto response = client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto published = std::get<mirage::runtime::ipc::WorkflowPublished>(response.payload);
    std::cout << "workflow " << published.workflow_id << '\n'
              << "version " << published.digest << (published.idempotent ? " (unchanged)" : "")
              << '\n';
    return kExitOk;
}

int command_workflow_delete(int argc, char **argv) {
    std::string workflow_id;
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> operands;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (!workflow_id.empty()) {
            std::cerr << kProgramName << ": delete takes one workflow id\n";
            return kExitUsage;
        }
        workflow_id = operand;
    }
    if (workflow_id.empty()) {
        std::cerr << kProgramName << ": workflow delete requires a workflow id\n";
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::WorkflowDeleteRequest{workflow_id}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "deleted " << workflow_id << '\n';
    return kExitOk;
}

int command_workflow_atoms(int argc, char **argv) {
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::WorkflowAtomCatalogRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto catalog = std::get<mirage::runtime::ipc::WorkflowAtomCatalog>(response.payload);
    for (const auto &tool : catalog.tools) {
        std::cout << tool.wire_name << ' ' << tool.version << ' '
                  << (tool.has_side_effects ? "side-effects" : "read-only") << '\n';
    }
    std::cout << catalog.tools.size() << " atom(s)\n";
    return kExitOk;
}

int command_workflow_runs(int argc, char **argv) {
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::WorkflowRunsRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto list = std::get<mirage::runtime::ipc::WorkflowRunList>(response.payload);
    for (const auto &run : list.runs) {
        std::cout << run.run_id << ' ' << run.workflow_id << ' ' << run.state << '\n';
    }
    std::cout << list.runs.size() << " run(s)\n";
    return kExitOk;
}

int command_workflow_run(int argc, char **argv) {
    std::string workflow_id;
    std::string digest;
    std::string parameters_file;
    std::string policy;
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> operands;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string &name, const std::string &value) {
                if (name == "--digest") {
                    digest = value;
                    return true;
                }
                if (name == "--parameters-file") {
                    parameters_file = value;
                    return true;
                }
                if (name == "--policy") {
                    policy = value;
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (!workflow_id.empty()) {
            std::cerr << kProgramName << ": run takes one workflow id\n";
            return kExitUsage;
        }
        workflow_id = operand;
    }
    if (workflow_id.empty()) {
        std::cerr << kProgramName << ": workflow run requires a workflow id\n";
        return kExitUsage;
    }
    std::string parameters;
    if (!parameters_file.empty()) {
        parameters = read_whole_file(parameters_file);
        if (parameters.empty()) {
            std::cerr << kProgramName << ": cannot read run parameters '" << parameters_file
                      << "'\n";
            return kExitFailure;
        }
    }
    auto client = client_for(options);
    mirage::runtime::ipc::WorkflowRunRequest request;
    request.workflow_id = workflow_id;
    request.digest = digest;
    request.parameters_json = parameters;
    request.policy = policy;
    const auto response = client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "run "
              << std::get<mirage::runtime::ipc::WorkflowRunStarted>(response.payload).run_id
              << '\n';
    return kExitOk;
}

int command_workflow_cancel(int argc, char **argv) {
    std::string run_id;
    GlobalOptions options;
    int index = 3;
    std::vector<std::string> operands;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string &name, const std::string &value) {
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto &operand : operands) {
        if (!run_id.empty()) {
            std::cerr << kProgramName << ": cancel takes one run id\n";
            return kExitUsage;
        }
        run_id = operand;
    }
    if (run_id.empty()) {
        std::cerr << kProgramName << ": workflow cancel requires a run id\n";
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::WorkflowCancelRunRequest{run_id}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto cancelled = std::get<mirage::runtime::ipc::WorkflowRunCancelled>(response.payload);
    std::cout << "run " << cancelled.run_id << ' ' << cancelled.state << '\n';
    return kExitOk;
}

// --- entry ------------------------------------------------------------------

void print_usage(std::ostream &out) {
    out << "Usage: " << kProgramName << " <command>\n"
        << "\n"
        << "Mirage is the Linux/Windows desktop host for the Mira agent runtime.\n"
        << "\n"
        << "Commands:\n"
        << "  --version                    Print Mirage, Mira core and platform versions\n"
        << "  --help                       Print this help\n"
        << "  start [--socket P] [--wait S] [--no-tray] [--no-shell]\n"
        << "        [--tray PATH] [--shell PATH]\n"
        << "                               Start/reuse the tray-owned Agent Runtime\n"
        << "                               and its separate native frontend\n"
        << "  service start [--socket P] [--wait S] [--read-root DIR]...\n"
        << "                [--perm CAP=allow|confirm|deny]...\n"
        << "                [--confirm allow|deny] [--config PATH]\n"
        << "                [--state-dir PATH] [--no-recovery]\n"
        << "                               Start the background runtime service\n"
        << "  service status [--socket P]  Probe the running service\n"
        << "  service shutdown [--socket P]\n"
        << "                               Ask the running service to stop\n"
        << "  task submit --goal TEXT [--read PATH] [--exec CMD]\n"
        << "              [--step-timeout MS] [--session ID] [--socket P]\n"
        << "                               Submit an M1 scripted task\n"
        << "  task list [--socket P]       List submitted tasks\n"
        << "  task inspect <id> [--socket P]\n"
        << "                               Inspect one task's structured results\n"
        << "  task cancel <id> [--socket P]\n"
        << "                               Cancel a task and its running desktop action\n"
        << "  session list [--socket P]    List the service's sessions\n"
        << "  session open [--socket P]    Open one more session\n"
        << "  session history <id> [--limit N] [--socket P]\n"
        << "                               Print a session's conversation history\n"
        << "  workflow list [--socket P]   List the workflow catalog\n"
        << "  workflow save --file PATH [--socket P]\n"
        << "                               Append a draft version of the definition\n"
        << "  workflow publish --file PATH [--socket P]\n"
        << "                               Gate and publish the definition\n"
        << "  workflow delete <id> [--socket P]\n"
        << "                               Remove a workflow from the catalog\n"
        << "  workflow atoms [--socket P]  List the exposed atom (tool) catalog\n"
        << "  workflow runs [--socket P]   List workflow runs and their states\n"
        << "  workflow run <id> [--digest HEX] [--parameters-file PATH] [--policy NAME]\n"
        << "              [--socket P]     Start one asynchronous workflow run\n"
        << "  workflow cancel <run-id> [--socket P]\n"
        << "                               Cancel a workflow run\n"
        << "\n"
        << "Options usable after each subcommand: --socket PATH (Local IPC\n"
        << "endpoint), --timeout MS (call timeout).\n";
}

void print_version() {
    std::cout << kProgramName << ' ' << kVersion << '\n';

    const mirage::runtime::MiraCoreVersion core = mirage::runtime::mira_core_version();
    std::cout << "mira core " << core.major << '.' << core.minor << '.' << core.patch << '\n';

    std::cout << "platform " << mirage::platform::platform_info().os << '\n';

    const mirage::runtime::ServiceInfo service = mirage::runtime::runtime_service_info();
    std::cout << "runtime service " << service.name
              << " (background: " << (service.background_capable ? "yes" : "no") << ")\n";
}

} // namespace

namespace mirage::cli {
int command_update_check(int argc, char **argv);
int command_update_apply(int argc, char **argv);
} // namespace mirage::cli

int main(int argc, char **argv) {
    if (argc >= 2) {
        const std::string_view command{argv[1]};
        if (command == "--version") {
            print_version();
            return 0;
        }
        if (command == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (command == "update" && argc >= 3) {
            const std::string_view subcommand{argv[2]};
            if (subcommand == "check") {
                return mirage::cli::command_update_check(argc - 2, argv + 2);
            }
            if (subcommand == "apply") {
                return mirage::cli::command_update_apply(argc - 2, argv + 2);
            }
            std::cerr << kProgramName << ": unknown update subcommand '" << subcommand << "'\n";
            return kExitUsage;
        }
        if (command == "start") {
            return command_start(argc, argv);
        }
        if (command == "service" && argc >= 3) {
            const std::string_view subcommand{argv[2]};
            if (subcommand == "start") {
                return command_service_start(argc, argv);
            }
            if (subcommand == "status") {
                return command_service_status(argc, argv);
            }
            if (subcommand == "shutdown") {
                return command_service_shutdown(argc, argv);
            }
            std::cerr << kProgramName << ": unknown service subcommand '" << subcommand << "'\n";
            return kExitUsage;
        }
        if (command == "task" && argc >= 3) {
            const std::string_view subcommand{argv[2]};
            if (subcommand == "submit") {
                return command_task_submit(argc, argv);
            }
            if (subcommand == "list") {
                return command_task_list(argc, argv);
            }
            if (subcommand == "inspect") {
                return command_task_inspect(argc, argv);
            }
            if (subcommand == "cancel") {
                return command_task_cancel(argc, argv);
            }
            std::cerr << kProgramName << ": unknown task subcommand '" << subcommand << "'\n";
            return kExitUsage;
        }
        if (command == "workflow" && argc >= 3) {
            const std::string_view subcommand{argv[2]};
            if (subcommand == "list") {
                return command_workflow_list(argc, argv);
            }
            if (subcommand == "save") {
                return command_workflow_save(argc, argv);
            }
            if (subcommand == "publish") {
                return command_workflow_publish(argc, argv);
            }
            if (subcommand == "delete") {
                return command_workflow_delete(argc, argv);
            }
            if (subcommand == "atoms") {
                return command_workflow_atoms(argc, argv);
            }
            if (subcommand == "runs") {
                return command_workflow_runs(argc, argv);
            }
            if (subcommand == "run") {
                return command_workflow_run(argc, argv);
            }
            if (subcommand == "cancel") {
                return command_workflow_cancel(argc, argv);
            }
            std::cerr << kProgramName << ": unknown workflow subcommand '" << subcommand << "'\n";
            return kExitUsage;
        }
        if (command == "session" && argc >= 3) {
            const std::string_view subcommand{argv[2]};
            if (subcommand == "list") {
                return command_session_list(argc, argv);
            }
            if (subcommand == "open") {
                return command_session_open(argc, argv);
            }
            if (subcommand == "history") {
                return command_session_history(argc, argv);
            }
            std::cerr << kProgramName << ": unknown session subcommand '" << subcommand << "'\n";
            return kExitUsage;
        }
        std::cerr << kProgramName << ": unknown command '" << command << "'\n\n";
        print_usage(std::cerr);
        return 2;
    }
    print_usage(std::cout);
    return 0;
}
