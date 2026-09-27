#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include <mirage/integration/mira_adapter.hpp>
#include <mirage/integration/workflow_event_bridge.hpp>

namespace executor {
class Executor;
}

namespace mirage::runtime {

/// Version of the pinned Mira core this binary was linked against. Sourced
/// from <mira/version.hpp> in the pinned third_party/mira checkout; the
/// runtime layer is the only place that includes Mira headers for identity
/// reporting (integration/mira holds the contract-facing adapter).
struct MiraCoreVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

MiraCoreVersion mira_core_version();

std::string mira_core_version_string();

/// True when the linked Mira core satisfies the major.minor Mirage was built
/// and verified against. Patch differences are accepted.
bool mira_core_compatible_with(int expected_major, int expected_minor);

/// Lifecycle state of the hosted Mira instance. Frozen set for M1 (design
/// doc section 11); states only move along
///
///   Stopped -> Starting -> Running -> Stopping -> Stopped
///                |                       |
///                +--> Failed <-----------+
///
/// with Failed also reachable from Running on a fatal hosting error.
/// Stopped (normal end) and Failed are terminal and idempotent: no
/// transition revives a terminal host, and start()/shutdown() on a terminal
/// host fail closed or re-report the recorded outcome instead of moving
/// state.
enum class HostStatus {
    Stopped,  ///< initial and normal terminal state; not hosting
    Starting, ///< start() admitted; pinned runtime initializing / session opening
    Running,  ///< hosting with a bound environment; tasks may be submitted
    Stopping, ///< shutdown() admitted; draining in-flight pinned work
    Failed,   ///< terminal: initialization, binding or hosting failed
};

/// Stable identifier of a HostStatus for logs and IPC payloads; never
/// translated, so the product layer can match on it.
const char *host_status_name(HostStatus status);

/// Error surfaced by host operations. `code` is a stable identifier from the
/// mirage.host domain ("invalid_state", "invalid_argument", "not_found",
/// "pinned_runtime"); `message` is safe for logs and UI.
struct HostError {
    std::string code;
    std::string message;
};

/// Outcome of a host operation that carries no other payload.
struct HostOutcome {
    bool ok = false;
    HostError error; ///< meaningful only when ok is false
};

/// Configuration of the hosted pinned runtime. Mirrors the pinned
/// RuntimeConfig capacities; the pinned runtime owns its internal executors,
/// so these values bound admission and queueing inside it (AGENTS.md:
/// Mirage does not run a second concurrency fabric next to it).
struct HostConfig {
    std::size_t worker_threads = 2;
    std::size_t queue_capacity = 64;
    std::size_t max_in_flight = 1024;
    /// Upper bound for waiting on one pinned control-plane outcome.
    std::chrono::milliseconds command_wait{2000};
    /// Upper bound for the drain wait inside shutdown().
    std::chrono::milliseconds shutdown_drain{5000};
};

/// Opaque task identity handed out by submit_task(); the id is the pinned
/// contract's task id rendered as 32 lowercase hex characters.
struct TaskIdentity {
    std::string id;
};

/// Opaque session identity (DEC-021): the pinned session id rendered as 32
/// lowercase hex characters. The primary session is opened by start();
/// further sessions are opened through open_session().
struct SessionIdentity {
    std::string id;
};

/// Result of open_session(); `session` is meaningful only when ok is true.
struct SessionOpenResult {
    bool ok = false;
    SessionIdentity session;
    HostError error;
};

/// Projected snapshot of one session (pinned session_snapshot projection):
/// `state` is the pinned SessionState in its stable lowercase wire form
/// ("opening" / "autonomous" / "takeover_pending" / "human_controlled" /
/// "resuming" / "closing" / "closed" / "failed") and `environment_epoch` the
/// pinned epoch the snapshot observed.
struct SessionView {
    std::string id;
    std::string state;
    std::uint64_t environment_epoch = 0;
};

/// Result of session_view(); `view` is meaningful only when ok is true.
struct SessionViewResult {
    bool ok = false;
    SessionView view;
    HostError error;
};

/// Identifier of one admitted desktop operation (design doc section 11.1:
/// the harness-side driver loop brackets every desktop action with the
/// pinned operation boundary). The id fields are the pinned identifiers
/// rendered as 32 lowercase hex characters; the epoch anchors the ticket to
/// one task era so completions from a cancelled or re-driven era settle as
/// stale instead of reviving settled work.
struct OperationTicket {
    std::string task_id;
    std::uint64_t task_epoch = 0;
    std::string step_id;
    std::string operation_id;
};

/// Result of begin_operation(); `ticket` is meaningful only when ok is true.
struct OperationBeginResult {
    bool ok = false;
    OperationTicket ticket;
    HostError error;
};

/// Result of submit_task(); `task` is meaningful only when ok is true.
struct TaskSubmissionResult {
    bool ok = false;
    TaskIdentity task;
    HostError error;
};

/// M1 projection of the pinned task state set onto product-visible progress
/// (design doc section 11). The full pinned state machine stays authoritative
/// inside the pinned runtime; the host only translates.
enum class TaskProgress {
    Unknown,    ///< task not found or state not representable
    Idle,       ///< admitted, not yet picked up by the agent loop
    Active,     ///< observing / reasoning / planning / acting / verifying / recovering
    Paused,     ///< pause family (pausing, paused, takeover settling/suspended)
    Cancelling, ///< cancellation admitted, not settled yet
    Completed,  ///< terminal
    Failed,     ///< terminal
    Cancelled,  ///< terminal
};

/// Observable task state as of the last host query.
struct TaskView {
    TaskProgress progress = TaskProgress::Unknown;
    /// Meaningful when progress is a terminal state; false marks a failed
    /// completion.
    bool success = false;
};

/// Result of task_view(); `view` is meaningful only when ok is true.
struct TaskViewResult {
    bool ok = false;
    TaskView view;
    HostError error;
};

/// Result of an ordered shutdown; recorded and re-reported by later
/// idempotent shutdown() calls.
struct HostShutdownReport {
    bool clean = false;
    std::size_t pending_commands = 0;
    std::string diagnostic;
};

/// Result of shutdown(); `report` is meaningful only when ok is true.
struct ShutdownResult {
    bool ok = false;
    HostShutdownReport report;
    HostError error;
};

/// Result of save_workflow_definition() / publish_workflow_definition()
/// (DEC-023): the definition's pinned identity, content digest and product
/// name; publish additionally reports the gate drive id and idempotency.
struct WorkflowDefinitionResult {
    bool ok = false;
    std::string workflow_id;
    std::string digest;
    std::string name;
    std::string dry_run_id; ///< publish only; empty for save
    bool idempotent = false; ///< publish only
    HostError error;
};

/// Result of start_workflow_run() (DEC-023): the new run's identity.
struct WorkflowRunStartResult {
    bool ok = false;
    std::string run_id;
    HostError error;
};

/// Result of cancel_workflow_run() (DEC-023): the post-call run state as the
/// pinned view reports it (terminal states included; cancel is idempotent).
struct WorkflowRunCancelResult {
    bool ok = false;
    std::string run_id;
    std::string state;
    HostError error;
};

/// Pinned-free projection of one workflow run snapshot (DEC-023): `state` is
/// the pinned WorkflowRunState stable lowercase name.
struct WorkflowRunView {
    std::string run_id;
    std::string workflow_id;
    std::string state;
    std::uint64_t run_epoch = 0;
};

/// Result of workflow_run_view(); `view` is meaningful only when ok is true.
struct WorkflowRunViewResult {
    bool ok = false;
    WorkflowRunView view;
    HostError error;
};

/// Host for one long-running pinned Mira instance inside the Mirage runtime
/// (design doc section 11). The host owns the pinned runtime instance, drives
/// its initialization and ordered shutdown, and exposes a pinned-free task
/// surface for the product layer.
///
/// Threading: one owner thread drives start/submit/.../shutdown (the Runtime
/// Service in M1). status() is safe from any thread. The host never spawns
/// threads; all asynchronous work happens inside the pinned runtime's own
/// executor fabric.
class MiraHost {
  public:
    explicit MiraHost(HostConfig config = {});
    ~MiraHost();
    MiraHost(const MiraHost &) = delete;
    MiraHost &operator=(const MiraHost &) = delete;

    /// Current lifecycle state; observable from any thread.
    HostStatus status() const;

    /// Binds the desktop environment bridge and starts hosting: initializes
    /// the pinned runtime, then opens the primary session on it. `binding`
    /// must be a mirage::integration::DesktopEnvironmentBinding whose most
    /// derived type also implements the pinned environment contract; a
    /// binding that does not fails start() closed (invalid_argument).
    /// Fails on any non-Stopped state.
    HostOutcome start(std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding);

    /// Admits a task with the given goal. Requires Running.
    TaskSubmissionResult submit_task(const std::string &goal);

    /// Admits a task with the given goal into the given session (DEC-021).
    /// Requires Running; an unknown or malformed session identity surfaces
    /// the pinned rejection (not_found shape) instead of a fallback.
    TaskSubmissionResult submit_task(const SessionIdentity &session, const std::string &goal);

    /// The session start() opened on the pinned runtime; empty id before a
    /// successful start(). Every session.list includes it (DEC-021).
    SessionIdentity primary_session() const;

    /// Opens one more session on the hosted pinned runtime, bound to the
    /// same desktop environment (DEC-021). Requires Running.
    SessionOpenResult open_session();

    /// Observes the current session state; the pinned snapshot projected
    /// onto the stable state name. Unknown or malformed identities surface
    /// the pinned rejection.
    SessionViewResult session_view(const SessionIdentity &session) const;

    /// Requests cooperative cancellation of a task. Cancelling an already
    /// terminal task surfaces the pinned rejection instead of reviving it.
    HostOutcome cancel_task(const TaskIdentity &task);

    /// Settles a task from the harness side (design doc section 11: Verify
    /// decides goal success). Only legal pinned transitions are admitted;
    /// late completions of settled tasks surface as rejections and never
    /// revive a terminal task.
    HostOutcome complete_task(const TaskIdentity &task, bool success,
                              const std::string &safe_error = "");

    /// Observes the current task state; Unknown progress for malformed or
    /// unknown identities.
    TaskViewResult task_view(const TaskIdentity &task) const;

    /// Admits one desktop operation for the task so harness-driven desktop
    /// actions are visible in the pinned control plane. Requires Running;
    /// refused while the task is paused or under human takeover (pinned
    /// InvalidState).
    OperationBeginResult begin_operation(const TaskIdentity &task);

    /// Settles a previously admitted operation after its desktop action
    /// finished. Idempotent: re-statements of an already settled ticket and
    /// tickets from a cancelled or re-driven task era settle as pinned NoOps
    /// and are reported ok — the observable invariant is that the task state
    /// is never revived, which callers verify with task_view().
    HostOutcome admit_operation_completion(const OperationTicket &ticket);

    /// Opens the workflow execution surface (M5-05, DEC-023): constructs the
    /// pinned WorkflowRuntime over the hosted runtime, the primary session,
    /// the bound environment and the caller-owned Executor, and installs the
    /// event bridge as its event store. Requires Running; fails closed when
    /// the surface is already attached. The executor is the service process's
    /// only instance (EXEC-01); the bridge handle is kept by the host, so it
    /// must outlive shutdown_workflow_surface().
    HostOutcome attach_workflow_surface(executor::Executor &executor,
                                        std::shared_ptr<mirage::integration::WorkflowEventBridge>
                                            event_bridge);

    /// Appends one draft version of the IR v1 definition (DEC-023): strict
    /// pinned decode, then a NotValidated library record — resolvable but not
    /// runnable (W-04). Requires the workflow surface.
    WorkflowDefinitionResult save_workflow_definition(const std::string &definition_json,
                                                      const std::string &reason);

    /// Runs the pinned publish gate on the definition (DEC-023): structural
    /// validation, the empty-parameter DryRun drive (synchronous on the
    /// calling context) and a DryRunPassed library record with
    /// content-derived evidence; head-same-content replays settle idempotent.
    /// Requires the workflow surface.
    WorkflowDefinitionResult publish_workflow_definition(const std::string &definition_json,
                                                         const std::string &reason);

    /// Starts one asynchronous run from the library (DEC-023): the version is
    /// pinned by `digest_hex` (W-03) and must be runnable (W-04);
    /// `parameters_json` is a serialized JSON object (empty means none) and
    /// `policy_name` the optional closed policy name (empty uses the
    /// definition default). A drive admission failure cancels the created run
    /// best-effort and surfaces the pinned rejection.
    WorkflowRunStartResult start_workflow_run(const std::string &workflow_id_hex,
                                              const std::string &digest_hex,
                                              const std::string &parameters_json,
                                              const std::string &policy_name);

    /// Observes one run's current state (DEC-023). Unknown or malformed
    /// identities surface the pinned rejection.
    WorkflowRunViewResult workflow_run_view(const std::string &run_id_hex) const;

    /// Requests cooperative cancellation of one run (DEC-023); the pinned
    /// cancel is idempotent and never revives a terminal run.
    WorkflowRunCancelResult cancel_workflow_run(const std::string &run_id_hex);

    /// Ordered workflow-surface shutdown (DEC-023): stops run producers,
    /// cancels active runs through the control plane and drains drive futures
    /// within the pinned budget. Must be called before the owning Executor
    /// shuts down (pinned order: WorkflowRuntime shutdown → MiraRuntime stop
    /// → Executor shutdown); the host shutdown() path calls it defensively,
    /// and it is idempotent.
    HostOutcome shutdown_workflow_surface();

    /// Ordered shutdown: asks the pinned runtime to stop admitting work,
    /// waits up to shutdown_drain for the drain, then finishes the shutdown.
    /// Idempotent: on a Stopped host it re-reports the recorded result; on a
    /// Failed host it releases pinned resources without reviving the state.
    ShutdownResult shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::runtime
