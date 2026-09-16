#include <mirage/platform/platform_info.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
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

bool parse_common_option(const std::string& name, const std::string& value,
                         GlobalOptions& options) {
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

bool takes_value(const std::string& name) {
    return name == "--socket" || name == "--timeout" || name == "--goal" ||
           name == "--read" || name == "--exec" || name == "--step-timeout" ||
           name == "--wait";
}

/// Splits `--name value` / `--name=value` pairs; returns false on usage
/// errors (unknown flag, missing value) with a message printed. Operands
/// (non-flag words) collect into `operands`.
bool consume_options(int argc, char** argv, int& index,
                     const std::function<bool(const std::string&,
                                              const std::string&)>& on_option,
                     std::vector<std::string>& operands) {
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
                std::cerr << kProgramName << ": option '" << name
                          << "' takes no value\n";
                return false;
            }
            if (!on_option(name, "")) {
                std::cerr << kProgramName << ": unknown option '" << name
                          << "'\n";
                return false;
            }
            ++index;
            continue;
        }
        if (!has_value) {
            if (index + 1 >= argc) {
                std::cerr << kProgramName << ": option '" << name
                          << "' requires a value\n";
                return false;
            }
            value = argv[++index];
        }
        if (!on_option(name, value)) {
            std::cerr << kProgramName << ": unknown option '" << name
                      << "'\n";
            return false;
        }
        ++index;
    }
    return true;
}

// --- ipc plumbing -----------------------------------------------------------

mirage::runtime::ipc::IpcClient client_for(const GlobalOptions& options) {
    return mirage::runtime::ipc::IpcClient(
        options.socket_path.empty()
            ? mirage::runtime::ipc::default_socket_path()
            : options.socket_path);
}

int report_response(const mirage::runtime::ipc::Response& response) {
    if (!response.ok) {
        std::cerr << kProgramName << ": service error ("
                  << response.error.code << "): " << response.error.message
                  << '\n';
        return response.error.code == "unavailable" ? kExitUnreachable
                                                    : kExitFailure;
    }
    return kExitOk;
}

void print_identity(const mirage::runtime::ipc::ServiceIdentity& identity,
                    const std::string& endpoint) {
    std::cout << "service: " << identity.name << '\n'
              << "mirage: " << identity.mirage_version << '\n'
              << "mira core: " << identity.mira_core_version << '\n'
              << "host status: " << identity.host_status << '\n'
              << "protocol: " << identity.protocol << '\n'
              << "endpoint: " << endpoint << '\n';
}

// --- service commands -------------------------------------------------------

int command_service_status(int argc, char** argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string& name, const std::string& value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response = client.call(
        mirage::runtime::ipc::HelloRequest{}, options.call_timeout);
    if (!response.ok) {
        std::cerr << kProgramName << ": service not reachable ("
                  << response.error.code << "): " << response.error.message
                  << '\n';
        return kExitUnreachable;
    }
    print_identity(std::get<mirage::runtime::ipc::ServiceIdentity>(
                       response.payload),
                   options.socket_path.empty()
                       ? mirage::runtime::ipc::default_socket_path()
                       : options.socket_path);
    return kExitOk;
}

int command_service_shutdown(int argc, char** argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string& name, const std::string& value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response = client.call(
        mirage::runtime::ipc::ShutdownRequest{}, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "service shutdown requested\n";
    return kExitOk;
}

int command_service_start(int argc, char** argv) {
    GlobalOptions options;
    std::chrono::milliseconds wait{10000};
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string& name, const std::string& value) {
                if (name == "--wait") {
                    wait = std::chrono::seconds{std::stol(value)};
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    // Locate the sibling mirage-service binary next to this executable.
    char self_path[4096];
    const ssize_t length =
        ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (length <= 0) {
        std::cerr << kProgramName << ": cannot resolve the executable path\n";
        return kExitFailure;
    }
    self_path[length] = '\0';
    const std::string self{self_path};
    const auto slash = self.rfind('/');
    const std::string service_binary =
        (slash == std::string::npos ? std::string(".")
                                    : self.substr(0, slash)) +
        "/mirage-service";

    const std::string socket = options.socket_path.empty()
                                   ? mirage::runtime::ipc::default_socket_path()
                                   : options.socket_path;
    const pid_t child = ::fork();
    if (child < 0) {
        std::cerr << kProgramName << ": fork failed\n";
        return kExitFailure;
    }
    if (child == 0) {
        // Child: become the service (DEC-007 item 6). The parent exits as
        // soon as the endpoint answers hello.
        std::vector<char*> argv_child;
        argv_child.push_back(const_cast<char*>(service_binary.c_str()));
        argv_child.push_back(const_cast<char*>("--socket"));
        argv_child.push_back(const_cast<char*>(socket.c_str()));
        argv_child.push_back(nullptr);
        ::execv(service_binary.c_str(), argv_child.data());
        ::_exit(127);
    }
    // Parent: wait for readiness by probing the endpoint.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    auto probe_client = client_for(options);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto probe = probe_client.call(
            mirage::runtime::ipc::HelloRequest{},
            std::chrono::milliseconds{500});
        if (probe.ok) {
            std::cout << "service ready at " << socket << " (pid " << child
                      << ")\n";
            print_identity(
                std::get<mirage::runtime::ipc::ServiceIdentity>(probe.payload),
                socket);
            return kExitOk;
        }
        ::usleep(100 * 1000);
    }
    std::cerr << kProgramName << ": service did not become ready within "
              << std::chrono::duration_cast<std::chrono::seconds>(wait).count()
              << "s (check " << service_binary << " output)\n";
    return kExitFailure;
}

// --- task commands ----------------------------------------------------------

int command_task_submit(int argc, char** argv) {
    GlobalOptions options;
    mirage::runtime::ipc::SubmitTaskRequest request;
    std::optional<std::chrono::milliseconds> step_timeout;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&](const std::string& name, const std::string& value) {
                if (name == "--goal") {
                    request.goal = value;
                    return true;
                }
                if (name == "--read") {
                    request.steps.push_back(
                        {mirage::runtime::ipc::StepKind::FilesystemRead,
                         value});
                    return true;
                }
                if (name == "--exec") {
                    request.steps.push_back(
                        {mirage::runtime::ipc::StepKind::ProcessExecute,
                         value});
                    return true;
                }
                if (name == "--step-timeout") {
                    step_timeout =
                        std::chrono::milliseconds{std::stol(value)};
                    return true;
                }
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    if (request.goal.empty()) {
        std::cerr << kProgramName
                  << ": task submit requires --goal TEXT\n";
        return kExitUsage;
    }
    request.step_timeout = step_timeout;
    auto client = client_for(options);
    const auto response =
        client.call(request, options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    std::cout << "task "
              << std::get<mirage::runtime::ipc::TaskSubmitted>(response.payload)
                     .task_id
              << '\n';
    return kExitOk;
}

int command_task_list(int argc, char** argv) {
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string& name, const std::string& value) {
                return parse_common_option(name, value, options);
            },
            extra)) {
        return kExitUsage;
    }
    auto client = client_for(options);
    const auto response =
        client.call(mirage::runtime::ipc::ListTasksRequest{},
                    options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto list = std::get<mirage::runtime::ipc::TaskList>(response.payload);
    for (const auto& task : list.tasks) {
        std::cout << task.id << ' ' << task.progress << ' ' << task.goal
                  << '\n';
    }
    std::cout << list.tasks.size() << " task(s)\n";
    return kExitOk;
}

int command_task_inspect(int argc, char** argv) {
    std::string task_id;
    GlobalOptions options;
    int index = 3; // skip "task/service" and the subcommand
    std::vector<std::string> operands;
    std::vector<std::string> extra;
    if (!consume_options(
            argc, argv, index,
            [&options](const std::string& name, const std::string& value) {
                return parse_common_option(name, value, options);
            },
            operands)) {
        return kExitUsage;
    }
    for (const auto& operand : operands) {
        if (operand.starts_with("--")) {
            std::cerr << kProgramName << ": unknown option '" << operand
                      << "'\n";
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
    const auto response = client.call(
        mirage::runtime::ipc::InspectTaskRequest{task_id},
        options.call_timeout);
    if (const int code = report_response(response); code != kExitOk) {
        return code;
    }
    const auto task =
        std::get<mirage::runtime::ipc::InspectTask>(response.payload);
    std::cout << "task " << task.id << '\n'
              << "goal: " << task.goal << '\n'
              << "progress: " << task.progress << '\n';
    if (task.has_success) {
        std::cout << "success: " << (task.success ? "yes" : "no") << '\n';
    }
    if (!task.steps.empty()) {
        std::cout << "steps:\n";
        for (const auto& step : task.steps) {
            std::cout << "  [" << step.index << "] " << step.kind << ' '
                      << step.status;
            if (!step.operation_id.empty()) {
                std::cout << " (op " << step.operation_id << ')';
            }
            if (step.kind == "process.execute") {
                std::cout << " exit=" << step.exit_code;
            }
            std::cout << '\n';
            if (!step.error.empty()) {
                std::cout << "      error: " << step.error << '\n';
            }
            if (!step.result.empty()) {
                std::cout << "      result" << (step.result_truncated
                                                    ? " (truncated)"
                                                    : "")
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

void print_usage(std::ostream& out) {
    out << "Usage: " << kProgramName << " <command>\n"
        << "\n"
        << "Mirage is the Linux/Windows desktop host for the Mira agent runtime.\n"
        << "\n"
        << "Commands:\n"
        << "  --version                    Print Mirage, Mira core and platform versions\n"
        << "  --help                       Print this help\n"
        << "  service start [--socket P] [--wait S]\n"
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
    std::cout << "runtime service " << service.name << " (background: "
              << (service.background_capable ? "yes" : "no") << ")\n";
}

} // namespace

int main(int argc, char** argv) {
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
            std::cerr << kProgramName << ": unknown service subcommand '"
                      << subcommand << "'\n";
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
            std::cerr << kProgramName << ": unknown task subcommand '"
                      << subcommand << "'\n";
            return kExitUsage;
        }
        std::cerr << kProgramName << ": unknown command '" << command << "'\n\n";
        print_usage(std::cerr);
        return 2;
    }
    print_usage(std::cout);
    return 0;
}
