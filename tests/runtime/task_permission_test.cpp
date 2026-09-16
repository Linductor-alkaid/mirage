// M1-06 permission gate integration verification (independent verification
// pass). Drives a real RuntimeService over its Local IPC surface with the
// single-threaded test client and checks the RULE-05 gate end to end: the
// default policy lets read+execute steps complete, a denied process.execute
// fails the step before any desktop action (no pinned operation id, no
// forked side effect, fail-fast), a Confirm rule fails closed without a
// confirmation handler, an injected AllowAll handler lets the confirmed read
// complete, and a customized filesystem.write rule leaves the other rules
// untouched.

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/permission/permission.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace permission = mirage::runtime::permission;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::runtime::ServiceRunReport;
using mirage::testing::TempDir;

constexpr auto kCallBudget = std::chrono::seconds{5};
constexpr auto kTaskBudget = std::chrono::seconds{10};

ServiceConfig make_config(const TempDir &dir) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.5.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    return config;
}

/// Fresh binding over a fresh Linux desktop environment per service start;
/// the filesystem read scope is the test's temp directory.
std::shared_ptr<integration::MiraEnvironmentBinding>
make_binding(const TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

std::optional<std::string> submit_task(ipc::IpcClient &client,
                                       ipc::SubmitTaskRequest request) {
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        return std::nullopt;
    }
    return submitted->task_id;
}

/// Polls task.inspect until the task reports a terminal progress state.
std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response response =
            client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr) {
            if (inspect->progress == "Completed" || inspect->progress == "Failed" ||
                inspect->progress == "Cancelled") {
                return *inspect;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

// --- scenarios ---------------------------------------------------------------

void scenario_default_policy_allows_read_and_execute() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "permission default topology " + token + "\n";
    write_text_file(goal_file, content);
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read the goal file then touch the canary";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->has_success && done->success);
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() != 2) {
        return;
    }

    // Both steps report the policy-allowed outcome and carry a real pinned
    // operation identity: the gate admitted both desktop actions.
    for (const ipc::StepView &step : done->steps) {
        MIRAGE_CHECK(step.permission == "allowed");
        MIRAGE_CHECK(step.status == "ok");
        MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(step.operation_id));
    }
    MIRAGE_CHECK(done->steps[0].result == content);
    // The allowed execute really reached the desktop.
    MIRAGE_CHECK(std::filesystem::exists(canary));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_denied_execute_rejects_before_side_effect() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    config.permission_policy.rules[static_cast<std::size_t>(
        permission::Capability::ProcessExecute)] = permission::Rule::Deny;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "run a denied command then echo a marker";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "printf after-denied"});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Failed");
    MIRAGE_CHECK(done->has_success && !done->success);
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() != 2) {
        return;
    }

    // The denied step failed before any desktop action: no operation was
    // admitted and the shell command never ran.
    const ipc::StepView &denied = done->steps[0];
    MIRAGE_CHECK(denied.status == "failed");
    MIRAGE_CHECK(!denied.ok);
    MIRAGE_CHECK(denied.permission == "denied");
    MIRAGE_CHECK(denied.operation_id.empty());
    MIRAGE_CHECK(denied.error.rfind("permission_denied: ", 0) == 0);
    MIRAGE_CHECK(denied.error.find("process.execute denied by policy") !=
                 std::string::npos);
    MIRAGE_CHECK(denied.exit_code == -1);
    MIRAGE_CHECK(denied.result.empty());
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    // Fail-fast: the follower never ran either.
    const ipc::StepView &follower = done->steps[1];
    MIRAGE_CHECK(follower.status == "skipped");
    MIRAGE_CHECK(!follower.ok);
    MIRAGE_CHECK(follower.operation_id.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_confirm_without_handler_fails_closed() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    config.permission_policy.rules[static_cast<std::size_t>(
        permission::Capability::FilesystemRead)] = permission::Rule::Confirm;
    // config.confirmation stays null: the service must deny every
    // confirmation request (fail closed, DEC-010).
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "unreachable content\n");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a file the policy gates behind confirmation";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Failed");
    MIRAGE_CHECK(done->steps.size() == 1);
    if (done->steps.size() != 1) {
        return;
    }
    const ipc::StepView &step = done->steps[0];
    MIRAGE_CHECK(step.status == "failed");
    MIRAGE_CHECK(!step.ok);
    MIRAGE_CHECK(step.permission == "confirmation_rejected");
    MIRAGE_CHECK(step.operation_id.empty());
    MIRAGE_CHECK(step.error.rfind("permission_denied: ", 0) == 0);
    MIRAGE_CHECK(step.error.find("confirmation rejected for filesystem.read") !=
                 std::string::npos);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_confirm_with_allow_handler_completes() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    config.permission_policy.rules[static_cast<std::size_t>(
        permission::Capability::FilesystemRead)] = permission::Rule::Confirm;
    config.confirmation = std::make_shared<permission::AllowAllConfirmation>();
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "confirmed read " + token + "\n";
    write_text_file(goal_file, content);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a file the handler approves";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->has_success && done->success);
    MIRAGE_CHECK(done->steps.size() == 1);
    if (done->steps.size() != 1) {
        return;
    }
    const ipc::StepView &step = done->steps[0];
    MIRAGE_CHECK(step.status == "ok");
    MIRAGE_CHECK(step.permission == "confirmed");
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(step.operation_id));
    MIRAGE_CHECK(step.result == content);
    // The decision is observable on disk too: the read target is intact.
    MIRAGE_CHECK(read_text_file(goal_file) == content);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_filesystem_write_rule_configurable_without_side_effects() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    // M1 has no write step; flipping the filesystem.write rule (default Deny)
    // must be accepted by the config and must not disturb the other rules'
    // judgments on a read+execute regression task.
    config.permission_policy.rules[static_cast<std::size_t>(
        permission::Capability::FilesystemWrite)] = permission::Rule::Allow;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "write-rule regression " + token + "\n";
    write_text_file(goal_file, content);
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read then touch while the write rule is customized";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() == 2) {
        MIRAGE_CHECK(done->steps[0].permission == "allowed");
        MIRAGE_CHECK(done->steps[0].result == content);
        MIRAGE_CHECK(done->steps[1].permission == "allowed");
        MIRAGE_CHECK(done->steps[1].status == "ok");
    }
    MIRAGE_CHECK(std::filesystem::exists(canary));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[task_permission_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("default_policy_allows_read_and_execute",
                 scenario_default_policy_allows_read_and_execute);
    run_scenario("denied_execute_rejects_before_side_effect",
                 scenario_denied_execute_rejects_before_side_effect);
    run_scenario("confirm_without_handler_fails_closed",
                 scenario_confirm_without_handler_fails_closed);
    run_scenario("confirm_with_allow_handler_completes",
                 scenario_confirm_with_allow_handler_completes);
    run_scenario("filesystem_write_rule_configurable_without_side_effects",
                 scenario_filesystem_write_rule_configurable_without_side_effects);
    return mirage::testing::finish("task_permission_test");
}
