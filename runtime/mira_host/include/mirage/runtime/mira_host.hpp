#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include <mirage/integration/mira_adapter.hpp>

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
const char* host_status_name(HostStatus status);

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
    MiraHost(const MiraHost&) = delete;
    MiraHost& operator=(const MiraHost&) = delete;

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
    TaskSubmissionResult submit_task(const std::string& goal);

    /// Requests cooperative cancellation of a task. Cancelling an already
    /// terminal task surfaces the pinned rejection instead of reviving it.
    HostOutcome cancel_task(const TaskIdentity& task);

    /// Settles a task from the harness side (design doc section 11: Verify
    /// decides goal success). Only legal pinned transitions are admitted;
    /// late completions of settled tasks surface as rejections and never
    /// revive a terminal task.
    HostOutcome complete_task(const TaskIdentity& task, bool success,
                              const std::string& safe_error = "");

    /// Observes the current task state; Unknown progress for malformed or
    /// unknown identities.
    TaskViewResult task_view(const TaskIdentity& task) const;

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
