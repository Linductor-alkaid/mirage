#include "../support/test.hpp"

#include <mira/adapters/simulator/simulator_environment.hpp>
#include <mira/environment.hpp>

#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mirage/integration/mira_adapter.hpp>
#include <mirage/integration/workflow_event_bridge.hpp>
#include <mirage/runtime/mira_host.hpp>

#include "../support/fake_desktop_environment.hpp"

#include <executor/executor.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

// --- workflow surface (M5-05, DEC-023) ----------------------------------------

/// True once `predicate` holds, polling up to `timeout_ms`; the workflow drive
/// runs on executor workers, so terminal-state observation is asynchronous.
template <typename Predicate> bool wait_until(Predicate predicate, int timeout_ms = 5000) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

/// Prints the host rejection before recording the failure so a pinned
/// passthrough is actionable from the log alone.
template <typename Result> bool check_ok(const char *what, const Result &result) {
    if (!result.ok) {
        std::fprintf(stderr, "[mira_host_test] %s failed: %s: %s\n", what,
                     result.error.code.c_str(), result.error.message.c_str());
    }
    MIRAGE_CHECK(result.ok);
    return result.ok;
}

bool is_64_lowercase_hex(const std::string &digest) {
    if (digest.size() != 64) {
        return false;
    }
    for (const char character : digest) {
        const bool digit = character >= '0' && character <= '9';
        const bool lowercase = character >= 'a' && character <= 'f';
        if (!digit && !lowercase) {
            return false;
        }
    }
    return true;
}

/// Minimal IR v1 definition bound to the given workflow id: one Verify step
/// whose predicate references an absent parameter (NotEvaluable, counted by
/// the pinned honesty counter, RULE-10), so the definition passes the strict
/// pinned decode, runs without a tool registry and settles the publish
/// gate's empty DryRun as Completed.
std::string minimal_definition(const std::string &workflow_id, const std::string &step_id,
                               const std::string &name) {
    // The name parameter varies content: the pinned library resolves versions
    // by content digest, and a same-content draft record shadows a later
    // runnable record (MIRA-20260927-001), so the flow under test publishes
    // different content than the draft.
    return R"({"schema_version":{"major":1,"minor":0},"workflow_id":")" + workflow_id +
           R"(","name":")" + name + R"(","parameters":[],"steps":[{"step_id":")" + step_id +
           R"(","kind":"verify","verification":{"signal":"run_parameter:x","op":"eq","value":"y"}}],)"
           R"("default_policy":"strict","allowed_policies":["strict","dry_run"]})";
}

void scenario_workflow_surface_lifecycle() {
    MiraHost host;
    executor::Executor executor;
    MIRAGE_CHECK(executor.initialize_ex(executor::ExecutorConfig{}).ok);

    // The pinned SimulatorBinding carries no mirage desktop environment, so
    // this surface attaches with the empty atom toolset (zero atoms; the
    // lifecycle scenarios run no ToolCall steps).
    const auto tools = mirage::integration::DesktopAtomToolset::build(nullptr, nullptr);
    auto bridge = std::make_shared<mirage::integration::WorkflowEventBridge>();
    // The publish gate drives also emit run events, so the scenario matches
    // events by run identity instead of counting deliveries.
    std::mutex events_mutex;
    std::vector<mirage::integration::WorkflowRunEventView> sink_events;
    bridge->set_sink([&](const mirage::integration::WorkflowRunEventView &view) {
        std::lock_guard lock(events_mutex);
        sink_events.push_back(view);
    });

    // Attach requires a Running host.
    const auto early = host.attach_workflow_surface(executor, bridge, tools);
    MIRAGE_CHECK(!early.ok);
    MIRAGE_CHECK(early.error.code == "invalid_state");

    const auto binding = std::make_shared<SimulatorBinding>();
    MIRAGE_CHECK(host.start(binding).ok);

    // A null toolset fails closed before the surface opens.
    const auto null_tools = host.attach_workflow_surface(executor, bridge, nullptr);
    MIRAGE_CHECK(!null_tools.ok);
    MIRAGE_CHECK(null_tools.error.code == "invalid_argument");

    // The surface attaches once on the Running host; a second attach fails
    // closed.
    const auto attached = host.attach_workflow_surface(executor, bridge, tools);
    MIRAGE_CHECK(attached.ok);
    const auto double_attach = host.attach_workflow_surface(executor, bridge, tools);
    MIRAGE_CHECK(!double_attach.ok);
    MIRAGE_CHECK(double_attach.error.code == "invalid_state");

    // Save lands a NotValidated draft (resolvable, not runnable; W-04).
    const std::string workflow_id = mira::WorkflowId::generate().to_string();
    const std::string step_id = mira::StepId::generate().to_string();
    const auto saved = host.save_workflow_definition(
        minimal_definition(workflow_id, step_id, "demo draft"), "test draft");
    check_ok("save", saved);
    MIRAGE_CHECK(saved.workflow_id == workflow_id);
    MIRAGE_CHECK(saved.name == "demo draft");
    MIRAGE_CHECK(is_64_lowercase_hex(saved.digest));

    // Publish gates a runnable version through the empty DryRun.
    const auto published = host.publish_workflow_definition(
        minimal_definition(workflow_id, step_id, "demo"), "test publish");
    check_ok("publish", published);
    MIRAGE_CHECK(published.workflow_id == workflow_id);
    MIRAGE_CHECK(!published.idempotent);

    // Republishing the same content settles idempotent.
    const auto republished = host.publish_workflow_definition(
        minimal_definition(workflow_id, step_id, "demo"), "test republish");
    MIRAGE_CHECK(republished.ok);
    MIRAGE_CHECK(republished.idempotent);

    // A draft digest is not runnable (W-04): the pinned rejection passes
    // through verbatim.
    const auto draft_run = host.start_workflow_run(workflow_id, saved.digest, "", "");
    MIRAGE_CHECK(!draft_run.ok);
    MIRAGE_CHECK(draft_run.error.code == "pinned_runtime");

    // Malformed inputs fail closed with stable host errors.
    MIRAGE_CHECK(!host.start_workflow_run("nothex", published.digest, "", "").ok);
    MIRAGE_CHECK(!host.start_workflow_run(workflow_id, "nothex", "", "").ok);
    MIRAGE_CHECK(!host.start_workflow_run(workflow_id, published.digest, "not json", "").ok);
    MIRAGE_CHECK(!host.start_workflow_run(workflow_id, published.digest, "", "yolo").ok);

    // Unknown workflow: pinned not_found passthrough.
    const auto unknown =
        host.start_workflow_run(mira::WorkflowId::generate().to_string(), published.digest, "", "");
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "pinned_runtime");

    // Run the published version under its DryRun policy; the single Verify
    // step settles at a step boundary and both run-level events land on the
    // bridge.
    const auto run = host.start_workflow_run(workflow_id, published.digest, "", "dry_run");
    check_ok("run", run);
    MIRAGE_CHECK(is_32_lowercase_hex(run.run_id));
    const bool settled_seen = wait_until([&] {
        std::lock_guard lock(events_mutex);
        for (const auto &event : sink_events) {
            if (event.run_id == run.run_id && event.state == "completed") {
                return true;
            }
        }
        return false;
    });
    MIRAGE_CHECK(settled_seen);
    MIRAGE_CHECK(bridge->sink_failures() == 0);

    const auto view = host.workflow_run_view(run.run_id);
    MIRAGE_CHECK(view.ok);
    if (view.view.state != "completed") {
        std::fprintf(stderr, "[mira_host_test] run settled as '%s' (epoch %llu)\n",
                     view.view.state.c_str(), static_cast<unsigned long long>(view.view.run_epoch));
    }
    MIRAGE_CHECK(view.view.state == "completed");
    MIRAGE_CHECK(view.view.workflow_id == workflow_id);

    // Cancelling a terminal run is the idempotent NoOp, never a revival.
    const auto cancelled = host.cancel_workflow_run(run.run_id);
    MIRAGE_CHECK(cancelled.ok);
    MIRAGE_CHECK(cancelled.state == "completed");

    // Surface shutdown closes the face; later calls reject with the stable
    // shape and a second shutdown is a NoOp.
    const auto surface_down = host.shutdown_workflow_surface();
    MIRAGE_CHECK(surface_down.ok);
    const auto after_down = host.save_workflow_definition(
        minimal_definition(workflow_id, step_id, "late draft"), "late");
    MIRAGE_CHECK(!after_down.ok);
    MIRAGE_CHECK(after_down.error.code == "invalid_state");
    MIRAGE_CHECK(host.shutdown_workflow_surface().ok);

    MIRAGE_CHECK(host.shutdown().ok);
    executor.shutdown(true);
}

/// IR v1 definition with one ToolCall step bound to a desktop atom; the
/// reserved "tool" member names the wire atom, the remaining members are the
/// schema-validated input the handler receives. A side-effecting atom must
/// declare a verification predicate (pinned W-02); this one reads the
/// `confirm` run parameter, so the publish DryRun (empty parameters) counts
/// it NotEvaluable (RULE-10) while a real run binds it through
/// `workflow.run` parameters.
std::string tool_call_definition(const std::string &workflow_id, const std::string &step_id,
                                 const std::string &name, const std::string &text) {
    return R"({"schema_version":{"major":1,"minor":0},"workflow_id":")" + workflow_id +
           R"(","name":")" + name +
           R"(","parameters":[{"name":"confirm","type":"boolean","required":false}],)"
           R"("steps":[{"step_id":")" +
           step_id +
           R"(","kind":"tool_call","arguments":{"tool":"desktop.clipboard.write_text","text":")" +
           text +
           R"("},"verification":{"signal":"run_parameter:confirm","op":"eq","value":true})"
           R"(}],"default_policy":"strict","allowed_policies":["strict","dry_run"]})";
}

void scenario_tool_call_steps_dispatch_desktop_atoms() {
    MiraHost host;
    executor::Executor executor;
    MIRAGE_CHECK(executor.initialize_ex(executor::ExecutorConfig{}).ok);

    mirage::testing::FakeDesktopEnvironment environment;

    // A switchable RULE-05 gate: run A executes under allow, run B lands
    // after the flip and must fail closed without touching the clipboard.
    std::atomic<bool> allow{true};
    mirage::integration::AtomPermissionGate gate =
        [&allow](const std::string &, const std::string &,
                 const mirage::integration::AtomCancelProbe &) { return allow.load(); };
    const auto tools = mirage::integration::DesktopAtomToolset::build(&environment, gate);

    auto bridge = std::make_shared<mirage::integration::WorkflowEventBridge>();
    std::mutex events_mutex;
    std::vector<mirage::integration::WorkflowRunEventView> sink_events;
    bridge->set_sink([&](const mirage::integration::WorkflowRunEventView &view) {
        std::lock_guard lock(events_mutex);
        sink_events.push_back(view);
    });

    const auto binding = std::make_shared<SimulatorBinding>();
    MIRAGE_CHECK(host.start(binding).ok);
    const auto attached = host.attach_workflow_surface(executor, bridge, tools);
    MIRAGE_CHECK(attached.ok);

    // Publish gates a runnable version: the DryRun drive plans the ToolCall
    // step without dispatching it (the clipboard must stay untouched) and
    // its verification predicate reads NotEvaluable against the absent
    // step_result.
    const std::string workflow_id = mira::WorkflowId::generate().to_string();
    const std::string step_id = mira::StepId::generate().to_string();
    const auto published = host.publish_workflow_definition(
        tool_call_definition(workflow_id, step_id, "clipboard writer", "from workflow"),
        "atom publish");
    check_ok("publish tool-call definition", published);

    // Strict run: the step dispatches through the registry and the provider
    // side effect lands on the fake environment.
    const auto run =
        host.start_workflow_run(workflow_id, published.digest, R"({"confirm":true})", "strict");
    check_ok("strict tool-call run", run);
    bool allowed_seen = false;
    MIRAGE_CHECK(wait_until([&] {
        std::lock_guard lock(events_mutex);
        for (const auto &event : sink_events) {
            if (event.run_id == run.run_id && event.state == "completed") {
                allowed_seen = true;
            }
        }
        return allowed_seen;
    }));
    MIRAGE_CHECK(environment.clipboard_has_text);
    MIRAGE_CHECK(environment.clipboard_text == "from workflow");

    // Deny path: the same atom fails closed under a denying gate; the run
    // settles failed and the clipboard keeps the earlier content.
    allow.store(false);
    const auto denied_run =
        host.start_workflow_run(workflow_id, published.digest, R"({"confirm":true})", "strict");
    check_ok("denied tool-call run", denied_run);
    bool denied_seen = false;
    MIRAGE_CHECK(wait_until([&] {
        std::lock_guard lock(events_mutex);
        for (const auto &event : sink_events) {
            if (event.run_id == denied_run.run_id && event.state == "failed") {
                denied_seen = true;
            }
        }
        return denied_seen;
    }));
    MIRAGE_CHECK(environment.clipboard_text == "from workflow");

    // Terminal projections stay honest for both runs.
    const auto view = host.workflow_run_view(denied_run.run_id);
    MIRAGE_CHECK(view.ok);
    MIRAGE_CHECK(view.view.state == "failed");
    MIRAGE_CHECK(bridge->sink_failures() == 0);

    MIRAGE_CHECK(host.shutdown_workflow_surface().ok);
    MIRAGE_CHECK(host.shutdown().ok);
    executor.shutdown(true);
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
    run_scenario("workflow_surface_lifecycle", scenario_workflow_surface_lifecycle);
    run_scenario("tool_call_steps_dispatch_desktop_atoms",
                 scenario_tool_call_steps_dispatch_desktop_atoms);
    return mirage::testing::finish("mira_host_test");
}
