#include <mirage/platform/platform_info.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <chrono>
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
           name == "--state-dir";
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

// --- entry ------------------------------------------------------------------

void print_usage(std::ostream &out) {
    out << "Usage: " << kProgramName << " <command>\n"
        << "\n"
        << "Mirage is the Linux/Windows desktop host for the Mira agent runtime.\n"
        << "\n"
        << "Commands:\n"
        << "  --version                    Print Mirage, Mira core and platform versions\n"
        << "  --help                       Print this help\n"
        << "  service start [--socket P] [--wait S] [--read-root DIR]...\n"
        << "                [--perm CAP=allow|confirm|deny]...\n"
        << "                [--confirm allow|deny] [--config PATH]\n"
        << "                [--state-dir PATH] [--no-recovery]\n"
        << "                               Start the background runtime service\n"
        << "  service status [--socket P]  Probe the running service\n"
        << "  service shutdown [--socket P]\n"
        << "                               Ask the running service to stop\n"
        << "  task submit --goal TEXT [--read PATH] [--exec CMD]\n"
        << "              [--step-timeout MS] [--socket P]\n"
        << "                               Submit an M1 scripted task\n"
        << "  task list [--socket P]       List submitted tasks\n"
        << "  task inspect <id> [--socket P]\n"
        << "                               Inspect one task's structured results\n"
        << "  task cancel <id> [--socket P]\n"
        << "                               Cancel a task and its running desktop action\n"
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
        std::cerr << kProgramName << ": unknown command '" << command << "'\n\n";
        print_usage(std::cerr);
        return 2;
    }
    print_usage(std::cout);
    return 0;
}
