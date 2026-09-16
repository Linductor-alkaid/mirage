#include "../support/test.hpp"

#include <mira/adapters/simulator/simulator_environment.hpp>
#include <mira/environment.hpp>

#include <mirage/integration/mira_adapter.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace {

using mirage::runtime::HostOutcome;
using mirage::runtime::HostStatus;
using mirage::runtime::MiraHost;
using mirage::runtime::TaskIdentity;
using mirage::runtime::TaskProgress;

// Binding used for the hosting scenarios. The pinned SimulatorEnvironment is
// `final`, so the pinned environment contract is recovered through
// composition: the most derived type implements both the mirage binding
// interface and mira::IEnvironment, delegating the four environment methods
// to the simulator member. MiraHost::start() cross-casts the binding to
// mira::IEnvironment; the dual base makes that dynamic_cast succeed.
class SimulatorBinding final : public mirage::integration::DesktopEnvironmentBinding,
                               public mira::IEnvironment {
  public:
    SimulatorBinding() : simulator_(mira::adapters::simulator::SimulatorSetup::single_display()) {}

    const char *binding_name() const override { return "test.desktop.sim-v1"; }

    mira::EnvironmentCapabilities capabilities() const override {
        return simulator_.capabilities();
    }
    mira::Result<mira::Observation> observe(const mira::ObservationRequest &request,
                                            const mira::OperationContext &context) override {
        return simulator_.observe(request, context);
    }
    mira::Result<mira::ExecutionReceipt> execute(const mira::InputSequence &input,
                                                 const mira::OperationContext &context) override {
        return simulator_.execute(input, context);
    }
    mira::Result<void> interrupt(const mira::OperationContext &context) override {
        return simulator_.interrupt(context);
    }

  private:
    mira::adapters::simulator::SimulatorEnvironment simulator_;
};

// Binding that deliberately does not implement the pinned environment
// contract: the most derived type has no mira::IEnvironment base, so the
// host's runtime cross-cast must fail and start() must fail closed.
class ContractFreeBinding final : public mirage::integration::DesktopEnvironmentBinding {
  public:
    const char *binding_name() const override { return "test.desktop.no-pinned-contract"; }
};

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

void scenario_start_null_binding_fails() {
    MiraHost host;
    const HostOutcome started = host.start(nullptr);
    MIRAGE_CHECK(!started.ok);
    MIRAGE_CHECK(started.error.code == "invalid_argument");
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);
}

void scenario_start_contract_free_binding_fails_closed() {
    MiraHost host;
    const auto binding = std::make_shared<ContractFreeBinding>();
    const HostOutcome started = host.start(binding);
    MIRAGE_CHECK(!started.ok);
    MIRAGE_CHECK(started.error.code == "invalid_argument");
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);
}

void scenario_start_submit_complete_shutdown() {
    MiraHost host;
    const auto binding = std::make_shared<SimulatorBinding>();
    const HostOutcome started = host.start(binding);
    MIRAGE_CHECK(started.ok);
    MIRAGE_CHECK(host.status() == HostStatus::Running);

    const auto submission = host.submit_task("summarize the desktop");
    MIRAGE_CHECK(submission.ok);
    MIRAGE_CHECK(is_32_lowercase_hex(submission.task.id));
    const TaskIdentity task = submission.task;

    const auto idle_view = host.task_view(task);
    MIRAGE_CHECK(idle_view.ok);
    MIRAGE_CHECK(idle_view.view.progress == TaskProgress::Idle);

    // Pinned behavior: completion is admitted from Idle because the frozen
    // transition graph contains a legal path Idle -> ... -> Verifying ->
    // Completed (runtime.cpp task_state_path_exists), even though the direct
    // Idle -> Completed edge is not in the table.
    const HostOutcome completed = host.complete_task(task, true);
    MIRAGE_CHECK(completed.ok);

    const auto done_view = host.task_view(task);
    MIRAGE_CHECK(done_view.ok);
    MIRAGE_CHECK(done_view.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(done_view.view.success);

    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);

    // Idempotent re-statement on the terminal Stopped host.
    const auto again = host.shutdown();
    MIRAGE_CHECK(again.ok);
    MIRAGE_CHECK(again.report.clean);

    const auto rejected = host.submit_task("after shutdown");
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_state");
}

void scenario_submit_before_start_rejected() {
    MiraHost host;
    const auto early = host.submit_task("too early");
    MIRAGE_CHECK(!early.ok);
    MIRAGE_CHECK(early.error.code == "invalid_state");

    // A host that never started is trivially clean and idempotent.
    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
}

void scenario_cancel_running_task() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);

    const auto submission = host.submit_task("cancellable goal");
    MIRAGE_CHECK(submission.ok);
    const TaskIdentity task = submission.task;

    const HostOutcome cancelled = host.cancel_task(task);
    MIRAGE_CHECK(cancelled.ok);

    const auto view = host.task_view(task);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.progress == TaskProgress::Cancelled);

    // Re-cancelling an already cancelled task must not revive it. Pinned
    // behavior: the same-terminal cancel settles as NoOp (receipt accepted,
    // outcome NoOp), so the host reports ok and the view stays Cancelled.
    const HostOutcome recancelled = host.cancel_task(task);
    MIRAGE_CHECK(recancelled.ok);
    const auto still = host.task_view(task);
    MIRAGE_CHECK(still.ok);
    MIRAGE_CHECK(still.view.progress == TaskProgress::Cancelled);

    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
}

void scenario_terminal_task_not_revived() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);

    const auto submission = host.submit_task("already settled");
    MIRAGE_CHECK(submission.ok);
    const TaskIdentity task = submission.task;
    MIRAGE_CHECK(host.complete_task(task, true).ok);

    // Observed pinned behavior (probe against runtime.cpp): cancel targets
    // Cancelled while the task sits in Completed; the frozen transition table
    // has no Completed -> Cancelled edge and the same-terminal NoOp only
    // applies to identical terminal states, so the pinned runtime rejects the
    // command with InvalidState and the host surfaces it as ok=false with
    // code "pinned_runtime". (A NoOp ok=true would also be spec-tolerated;
    // only a revived task is a defect.)
    const HostOutcome revived = host.cancel_task(task);
    MIRAGE_CHECK(!revived.ok);
    MIRAGE_CHECK(revived.error.code == "pinned_runtime");
    const auto view = host.task_view(task);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(view.view.success);

    // A conflicting late completion is also rejected without reviving.
    const HostOutcome late_fail = host.complete_task(task, false, "late harness error");
    MIRAGE_CHECK(!late_fail.ok);
    const auto settled = host.task_view(task);
    MIRAGE_CHECK(settled.ok);
    MIRAGE_CHECK(settled.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(settled.view.success);

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_invalid_inputs() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);

    // Double start on a Running host.
    const HostOutcome restarted = host.start(std::make_shared<SimulatorBinding>());
    MIRAGE_CHECK(!restarted.ok);
    MIRAGE_CHECK(restarted.error.code == "invalid_state");
    MIRAGE_CHECK(host.status() == HostStatus::Running);

    // Empty goal.
    const auto empty = host.submit_task("");
    MIRAGE_CHECK(!empty.ok);
    MIRAGE_CHECK(empty.error.code == "invalid_argument");

    // Malformed identities.
    const TaskIdentity malformed{"xyz"};
    const HostOutcome complete_bad = host.complete_task(malformed, true);
    MIRAGE_CHECK(!complete_bad.ok);
    MIRAGE_CHECK(complete_bad.error.code == "invalid_argument");
    const HostOutcome cancel_bad = host.cancel_task(malformed);
    MIRAGE_CHECK(!cancel_bad.ok);
    MIRAGE_CHECK(cancel_bad.error.code == "invalid_argument");
    const auto view_bad = host.task_view(malformed);
    MIRAGE_CHECK(!view_bad.ok);
    MIRAGE_CHECK(view_bad.error.code == "invalid_argument");

    // Well-formed but unknown identity (all-zero hex is never generated).
    const TaskIdentity unknown{"00000000000000000000000000000000"};
    const auto view_unknown = host.task_view(unknown);
    MIRAGE_CHECK(!view_unknown.ok);
    MIRAGE_CHECK(view_unknown.view.progress == TaskProgress::Unknown);
    MIRAGE_CHECK(view_unknown.error.code == "pinned_runtime");

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_failed_completion_projection() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);

    const auto submission = host.submit_task("doomed goal");
    MIRAGE_CHECK(submission.ok);
    const TaskIdentity task = submission.task;

    const HostOutcome failed = host.complete_task(task, false, "harness verified failure");
    MIRAGE_CHECK(failed.ok);

    const auto view = host.task_view(task);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.progress == TaskProgress::Failed);
    MIRAGE_CHECK(!view.view.success);

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_shutdown_drains_pending_task() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);

    const auto submission = host.submit_task("never finished");
    MIRAGE_CHECK(submission.ok);

    // Pinned behavior observed: request_shutdown cancels the non-terminal
    // task and closes the session (outcome Applied), then finish_shutdown
    // runs on the owner thread, so the executor drain completes and the
    // report is clean with zero pending commands; the host reaches Stopped.
    const auto shutdown = host.shutdown();
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(shutdown.report.clean);
    MIRAGE_CHECK(shutdown.report.pending_commands == 0);
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);

    const auto rejected = host.submit_task("after drain");
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_state");
}

void scenario_host_is_not_revived_after_shutdown() {
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);
    MIRAGE_CHECK(host.shutdown().ok);

    // Observed behavior (probe): the host re-enters Starting from the
    // terminal Stopped state (the design table lists Starting as the
    // successor of Stopped), but the pinned runtime refuses to re-initialize
    // (InvalidState "runtime was already initialized"), so start() fails
    // closed with code "pinned_runtime" and the host parks in the terminal
    // Failed state. It never hosts again.
    const HostOutcome restarted = host.start(std::make_shared<SimulatorBinding>());
    MIRAGE_CHECK(!restarted.ok);
    MIRAGE_CHECK(restarted.error.code == "pinned_runtime");
    MIRAGE_CHECK(host.status() == HostStatus::Failed);
    const auto rejected = host.submit_task("after failed restart");
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_state");

    // shutdown() on the Failed host releases resources without reviving the
    // terminal state.
    MIRAGE_CHECK(host.shutdown().ok);
    MIRAGE_CHECK(host.status() == HostStatus::Failed);
}

void scenario_destructor_releases_started_runtime() {
    {
        MiraHost host;
        MIRAGE_CHECK(host.start(std::make_shared<SimulatorBinding>()).ok);
        MIRAGE_CHECK(host.submit_task("destroyed without shutdown").ok);
        // ~MiraHost performs the ordered request_shutdown -> finish_shutdown
        // release; leaks or crashes here surface under ASAN/TSAN.
    }
    {
        // A never-started host must also destruct cleanly.
        MiraHost host;
        MIRAGE_CHECK(host.status() == HostStatus::Stopped);
    }
}

void scenario_status_names() {
    MIRAGE_CHECK(std::string_view(mirage::runtime::host_status_name(HostStatus::Stopped)) ==
                 "stopped");
    MIRAGE_CHECK(std::string_view(mirage::runtime::host_status_name(HostStatus::Starting)) ==
                 "starting");
    MIRAGE_CHECK(std::string_view(mirage::runtime::host_status_name(HostStatus::Running)) ==
                 "running");
    MIRAGE_CHECK(std::string_view(mirage::runtime::host_status_name(HostStatus::Stopping)) ==
                 "stopping");
    MIRAGE_CHECK(std::string_view(mirage::runtime::host_status_name(HostStatus::Failed)) ==
                 "failed");
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[mira_host_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("start_null_binding_fails", scenario_start_null_binding_fails);
    run_scenario("start_contract_free_binding_fails_closed",
                 scenario_start_contract_free_binding_fails_closed);
    run_scenario("start_submit_complete_shutdown", scenario_start_submit_complete_shutdown);
    run_scenario("submit_before_start_rejected", scenario_submit_before_start_rejected);
    run_scenario("cancel_running_task", scenario_cancel_running_task);
    run_scenario("terminal_task_not_revived", scenario_terminal_task_not_revived);
    run_scenario("invalid_inputs", scenario_invalid_inputs);
    run_scenario("failed_completion_projection", scenario_failed_completion_projection);
    run_scenario("shutdown_drains_pending_task", scenario_shutdown_drains_pending_task);
    run_scenario("host_is_not_revived_after_shutdown", scenario_host_is_not_revived_after_shutdown);
    run_scenario("destructor_releases_started_runtime",
                 scenario_destructor_releases_started_runtime);
    run_scenario("status_names", scenario_status_names);
    return mirage::testing::finish("mira_host_test");
}
