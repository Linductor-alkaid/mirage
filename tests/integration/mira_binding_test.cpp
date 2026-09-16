#include "../support/test.hpp"

#include <mira/environment.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace {

namespace desktop = mirage::desktop;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::HostStatus;
using mirage::runtime::MiraHost;
using mirage::runtime::TaskProgress;

/// Temporary workspace for provider fixtures; removed on scope exit.
class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m1-03-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, ec);
    }
    ~TempWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempWorkspace(const TempWorkspace &) = delete;
    TempWorkspace &operator=(const TempWorkspace &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
}

bool is_32_lowercase_hex(const std::string &id) {
    if (id.size() != 32) {
        return false;
    }
    for (const char character : id) {
        const bool digit = character >= '0' && character <= '9';
        const bool lowercase = character >= 'a' && character <= 'f';
        if (!digit && !lowercase) {
            return false;
        }
    }
    return true;
}

void scenario_binding_identity_and_honest_capabilities() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    const auto info = environment->info();
    MIRAGE_CHECK(info.platform == "linux");
    MIRAGE_CHECK(!info.name.empty());
    MIRAGE_CHECK(std::string_view(binding.binding_name()) == "mirage.desktop.linux-v1");

    const auto capabilities = binding.capabilities();
    MIRAGE_CHECK(!capabilities.screen_capture);
    MIRAGE_CHECK(!capabilities.ui_tree);
    MIRAGE_CHECK(!capabilities.foreground_app);
    MIRAGE_CHECK(!capabilities.device_state);
    MIRAGE_CHECK(!capabilities.atomic_observation);
    MIRAGE_CHECK(!capabilities.discrete_input);
    MIRAGE_CHECK(!capabilities.input_release);
    MIRAGE_CHECK(!capabilities.epoch_invalidation);
    MIRAGE_CHECK(capabilities.perception_sources == 0);
}

void scenario_observe_fails_closed_on_unsupported_components() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.screen = true;
    request.required.structure = true;
    const auto refused = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(!refused.has_value());
    MIRAGE_CHECK(refused.error().code == mira::ErrorCode::UnsupportedCapability);
    MIRAGE_CHECK(refused.error().safe_message.find("screen") != std::string::npos);
    MIRAGE_CHECK(refused.error().safe_message.find("structure") != std::string::npos);

    // An empty requirement is deliverable: a minimal component-free
    // observation is returned instead of silently incomplete content.
    mira::ObservationRequest empty_request;
    const auto observation = binding.observe(empty_request, mira::make_control_context());
    MIRAGE_CHECK(observation.has_value());
    MIRAGE_CHECK(!observation.value().id.is_nil());
    MIRAGE_CHECK(!observation.value().screen.has_value());
    MIRAGE_CHECK(!observation.value().structure.has_value());
    MIRAGE_CHECK(observation.value().perception.empty());
}

void scenario_execute_rejects_and_interrupt_is_idempotent() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    integration::MiraEnvironmentBinding binding(environment);

    mira::InputSequence sequence;
    sequence.events.push_back(mira::InputEvent{"tap", "0.5,0.5"});
    const auto receipt = binding.execute(sequence, mira::make_control_context());
    MIRAGE_CHECK(receipt.has_value());
    MIRAGE_CHECK(receipt.value().status == mira::ExecutionStatus::Rejected);
    MIRAGE_CHECK(!receipt.value().side_effect_may_have_occurred);
    MIRAGE_CHECK(!receipt.value().safe_message.empty());

    const auto first = binding.interrupt(mira::make_control_context());
    const auto second = binding.interrupt(mira::make_control_context());
    MIRAGE_CHECK(first.has_value());
    MIRAGE_CHECK(second.has_value());
}

void scenario_filesystem_provider_reads_and_fails_closed() {
    TempWorkspace workspace;
    // M1-05: reads are scoped to the declared roots; the workspace root is
    // the one readable place in this scenario.
    linux_backend::LinuxDesktopEnvironment environment({workspace.root()});
    const auto goal_path = workspace.root() / "goal.txt";
    write_text_file(goal_path, "mirage says hello\n");

    const auto read = environment.read_text_file(goal_path);
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "mirage says hello\n");

    // Negative cases stay inside the scope: they exercise lookup and file
    // validation, not containment.
    const auto missing = environment.read_text_file(workspace.root() / "missing.txt");
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == "not_found");

    const auto directory = environment.read_text_file(workspace.root());
    MIRAGE_CHECK(!directory.ok);
    MIRAGE_CHECK(directory.error.code == "invalid_argument");

    const auto empty_path = environment.read_text_file({});
    MIRAGE_CHECK(!empty_path.ok);
    MIRAGE_CHECK(empty_path.error.code == "invalid_argument");

    // M1-05 fail-closed contract: a default-constructed environment declares
    // no read roots and must refuse every read with permission_denied, even
    // for an existing regular file. The refusal reports the requested path,
    // never any resolved target.
    linux_backend::LinuxDesktopEnvironment unscoped;
    const auto denied = unscoped.read_text_file(goal_path);
    MIRAGE_CHECK(!denied.ok);
    MIRAGE_CHECK(denied.error.code == "permission_denied");
    MIRAGE_CHECK(denied.error.message.find(goal_path.string()) != std::string::npos);
    MIRAGE_CHECK(denied.content.empty());
}

void scenario_process_provider_captures_streams_and_exit_code() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    const auto outcome =
        environment.execute("printf 'out-line'; printf 'err-line' 1>&2; exit 3", limits);
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.exited_normally);
    MIRAGE_CHECK(outcome.exit_code == 3);
    MIRAGE_CHECK(outcome.standard_output == "out-line");
    MIRAGE_CHECK(outcome.standard_error == "err-line");
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(!outcome.output_truncated);
}

void scenario_process_provider_enforces_budget() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{200};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute("sleep 30", limits);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "deadline_exceeded");
    MIRAGE_CHECK(elapsed < std::chrono::seconds{5});

    desktop::ProcessLimits invalid;
    invalid.timeout = std::chrono::milliseconds::zero();
    const auto zero_budget = environment.execute("true", invalid);
    MIRAGE_CHECK(!zero_budget.ok);
    MIRAGE_CHECK(zero_budget.error.code == "invalid_argument");

    const auto empty_command = environment.execute("", limits);
    MIRAGE_CHECK(!empty_command.ok);
    MIRAGE_CHECK(empty_command.error.code == "invalid_argument");
}

void scenario_operation_surface_rejects_invalid_identities() {
    MiraHost host;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);

    const auto malformed = host.begin_operation(mirage::runtime::TaskIdentity{"xyz"});
    MIRAGE_CHECK(!malformed.ok);
    MIRAGE_CHECK(malformed.error.code == "invalid_argument");

    // Well-formed but unknown task.
    const auto unknown =
        host.begin_operation(mirage::runtime::TaskIdentity{"00000000000000000000000000000000"});
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "pinned_runtime");

    mirage::runtime::OperationTicket malformed_ticket{"xyz", 0, "abc", "def"};
    const auto rejected = host.admit_operation_completion(malformed_ticket);
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_argument");

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_end_to_end_task_reads_file_and_executes_shell() {
    TempWorkspace workspace;
    const auto goal_path = workspace.root() / "goal.txt";
    write_text_file(goal_path, "structured result payload\n");

    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{workspace.root()});
    auto binding = std::make_shared<integration::MiraEnvironmentBinding>(environment);
    MiraHost host;
    const auto started = host.start(binding);
    MIRAGE_CHECK(started.ok);
    MIRAGE_CHECK(host.status() == HostStatus::Running);

    const auto submission = host.submit_task("read the goal file and run a shell command");
    MIRAGE_CHECK(submission.ok);
    MIRAGE_CHECK(is_32_lowercase_hex(submission.task.id));
    const mirage::runtime::TaskIdentity task = submission.task;

    // Harness-side driver loop (DEC-008): each desktop action is bracketed by
    // the pinned operation boundary, so the work is visible in the control
    // plane.
    const auto read_operation = host.begin_operation(task);
    MIRAGE_CHECK(read_operation.ok);
    MIRAGE_CHECK(read_operation.ticket.task_id == task.id);
    MIRAGE_CHECK(is_32_lowercase_hex(read_operation.ticket.step_id));
    MIRAGE_CHECK(is_32_lowercase_hex(read_operation.ticket.operation_id));

    const auto file_read = environment->filesystem()->read_text_file(goal_path);
    MIRAGE_CHECK(file_read.ok);
    MIRAGE_CHECK(file_read.content == "structured result payload\n");
    MIRAGE_CHECK(host.admit_operation_completion(read_operation.ticket).ok);

    const auto shell_operation = host.begin_operation(task);
    MIRAGE_CHECK(shell_operation.ok);
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    const auto shell = environment->process()->execute("printf 'shell says mirage'", limits);
    MIRAGE_CHECK(shell.ok);
    MIRAGE_CHECK(shell.exit_code == 0);
    MIRAGE_CHECK(shell.standard_output == "shell says mirage");
    MIRAGE_CHECK(host.admit_operation_completion(shell_operation.ticket).ok);

    // The task is still non-terminal after two settled operations; the
    // harness decides goal success.
    const auto mid_view = host.task_view(task);
    MIRAGE_CHECK(mid_view.ok);
    MIRAGE_CHECK(mid_view.view.progress == TaskProgress::Idle);

    MIRAGE_CHECK(host.complete_task(task, true).ok);
    const auto done_view = host.task_view(task);
    MIRAGE_CHECK(done_view.ok);
    MIRAGE_CHECK(done_view.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(done_view.view.success);

    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);
}

void scenario_operation_completion_does_not_revive_cancelled_task() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);

    const auto submission = host.submit_task("cancelled mid-operation");
    MIRAGE_CHECK(submission.ok);
    const mirage::runtime::TaskIdentity task = submission.task;

    const auto operation = host.begin_operation(task);
    MIRAGE_CHECK(operation.ok);
    MIRAGE_CHECK(host.cancel_task(task).ok);

    // Pinned behavior: the ticket belongs to a cancelled task era, so the
    // completion settles as a stale NoOp. The host reports the idempotent
    // ok, but the observable invariant is that the task stays Cancelled —
    // a late completion never revives settled work.
    const auto late = host.admit_operation_completion(operation.ticket);
    MIRAGE_CHECK(late.ok);
    const auto view = host.task_view(task);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.progress == TaskProgress::Cancelled);

    MIRAGE_CHECK(host.shutdown().ok);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[mira_binding_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("binding_identity_and_honest_capabilities",
                 scenario_binding_identity_and_honest_capabilities);
    run_scenario("observe_fails_closed_on_unsupported_components",
                 scenario_observe_fails_closed_on_unsupported_components);
    run_scenario("execute_rejects_and_interrupt_is_idempotent",
                 scenario_execute_rejects_and_interrupt_is_idempotent);
    run_scenario("filesystem_provider_reads_and_fails_closed",
                 scenario_filesystem_provider_reads_and_fails_closed);
    run_scenario("process_provider_captures_streams_and_exit_code",
                 scenario_process_provider_captures_streams_and_exit_code);
    run_scenario("process_provider_enforces_budget", scenario_process_provider_enforces_budget);
    run_scenario("operation_surface_rejects_invalid_identities",
                 scenario_operation_surface_rejects_invalid_identities);
    run_scenario("end_to_end_task_reads_file_and_executes_shell",
                 scenario_end_to_end_task_reads_file_and_executes_shell);
    run_scenario("operation_completion_does_not_revive_cancelled_task",
                 scenario_operation_completion_does_not_revive_cancelled_task);
    return mirage::testing::finish("mira_binding_test");
}
