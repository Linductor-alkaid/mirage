#pragma once

// Internal service module surface (not installed, never included from
// public headers): the in-memory task registry, the shared service core and
// the loop/driver building blocks. Pinned executor types are confined to
// these internal headers and their translation units.

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/mira_host.hpp>

namespace mirage::runtime::detail {

/// Lifecycle of one scripted step inside the service-side driver.
namespace step_status {
inline constexpr const char *kPending = "pending";
inline constexpr const char *kRunning = "running";
inline constexpr const char *kOk = "ok";
inline constexpr const char *kFailed = "failed";
inline constexpr const char *kSkipped = "skipped";
/// The step was interrupted by a task cancellation (M1-05 cancellation
/// path): the desktop action did not finish, but the task did not fail.
inline constexpr const char *kCancelled = "cancelled";
} // namespace step_status

struct StepRecord {
    ipc::TaskStep spec;
    const char *status = step_status::kPending;
    std::string operation_id;
    /// Permission outcome for this step (RULE-05, DEC-010): a
    /// decision_name() string once judged, empty before that. A denied
    /// step carries no operation id — the action never reached the
    /// control plane.
    const char *permission = "";
    bool ok = false;
    int exit_code = -1;
    std::string result;
    bool result_truncated = false;
    std::string error;
};

struct TaskRecord {
    std::string id;
    std::string goal;
    /// Session the task's conversation lands in (DEC-021): the explicit
    /// binding at submit time or the primary session. Hydrated recovery
    /// records carry an empty id (their settlement predates this run), and
    /// the session faces skip them.
    std::string session_id;
    std::vector<StepRecord> steps;
    /// Wall-clock budget for one process.execute step, already clamped by
    /// the service cap at submit time (DEC-007 item 5).
    std::chrono::milliseconds step_timeout{30000};
    /// Cooperative cancellation flag the driver hands to every desktop
    /// action; the task.cancel handler and the ordered teardown request it
    /// so an in-flight provider operation ends promptly (M1-05).
    mirage::desktop::CancelToken cancel;
    /// True once the driver settled the task (or gave up on it).
    bool driver_done = false;
    /// Terminal progress name the task settled under (M1-07): "Completed"
    /// / "Failed" / "Cancelled" once the driver is done, empty before
    /// that. Hydrated tasks carry it straight from the recovery file —
    /// they have no live pinned counterpart, so this field (not the pinned
    /// runtime) is their progress source of truth.
    std::string final_progress;
    /// True only for tasks hydrated from the recovery file (M1-07): they
    /// have no live pinned counterpart in this service era, so task.cancel
    /// rejects them outright instead of surfacing a pinned not-found.
    bool from_recovery = false;
    /// Meaningful only once driver_done and the task reached a terminal
    /// pinned state; false for cancelled/failed settlements.
    bool has_success = false;
    bool success = false;
};

/// In-memory task registry (M1; persistence is M1-07). Explicitly bounded:
/// insert() refuses when capacity is reached instead of evicting silently
/// (RULE-07). All access happens under `mutex`.
struct TaskRegistry {
    std::mutex mutex;
    std::map<std::string, TaskRecord> tasks;
    std::size_t capacity = 256;

    bool full() const { return tasks.size() >= capacity; }
    std::vector<std::string> ids() const {
        std::vector<std::string> result;
        result.reserve(tasks.size());
        for (const auto &entry : tasks) {
            result.push_back(entry.first);
        }
        return result;
    }
};

/// Session registry (DEC-021): every session the service knows — the
/// primary session opened at start() and the ones admitted through
/// session.open — with its registration wall-clock time. Service-memory
/// state; the pinned runtime stays the session-state authority (session.list
/// projects live views). Bounded by `capacity`; admission refuses at the
/// bound instead of growing without bound (RULE-07). All access happens
/// under `mutex`.
struct SessionRegistry {
    std::mutex mutex;
    std::map<std::string, std::int64_t> created_at_ms;
    std::size_t capacity = 16;

    bool full() const { return created_at_ms.size() >= capacity; }
    bool contains(const std::string &id) const { return created_at_ms.count(id) != 0; }
};

/// One workflow catalog entry (DEC-023): the product identity of a workflow
/// the service saved or published, with the head version's content digest
/// and validation. The pinned library stays the execution-side authority;
/// this is the product index (the pinned library has no enumeration API).
/// `definition_json` (DEC-026) retains the head definition content the
/// service passed through — the pinned library keeps version records only
/// (no definition-body read API, W-03), so workflow.get projects the head
/// content from here.
struct WorkflowCatalogEntry {
    std::string name;
    std::string head_digest;
    std::string validation;
    bool runnable = false;
    std::int64_t updated_at_ms = 0;
    std::string definition_json;
};

/// Workflow catalog registry (DEC-023): every workflow the service knows,
/// keyed by workflow id, bounded by `capacity` — workflow.save refuses at
/// the bound instead of growing without bound (RULE-07); save/publish of a
/// known id is an upsert that does not grow the registry. All access
/// happens under `mutex`.
struct WorkflowRegistry {
    std::mutex mutex;
    std::map<std::string, WorkflowCatalogEntry> workflows;
    std::size_t capacity = 128;

    bool full() const { return workflows.size() >= capacity; }
    bool contains(const std::string &id) const { return workflows.count(id) != 0; }
};

/// One run the service admitted through workflow.run (DEC-023): identity and
/// creation time; the state projects live from the pinned runtime at read
/// time (workflow.runs / workflow.run_updated).
struct WorkflowRunRecord {
    std::string workflow_id;
    std::int64_t created_at_ms = 0;
};

/// Workflow run registry (DEC-023): the runs the service admitted, keyed by
/// run id, bounded by `capacity`. Overflow evicts the oldest entries whose
/// pinned state is terminal (handlers re-project states live); admission is
/// refused only when nothing terminal remains to evict. All access happens
/// under `mutex`.
struct WorkflowRunRegistry {
    std::mutex mutex;
    std::map<std::string, WorkflowRunRecord> runs;
    std::size_t capacity = 256;

    bool full() const { return runs.size() >= capacity; }
};

/// One dialog turn of a session's thread (DEC-027): lifecycle "pending" →
/// "ok" / "failed"; reply and error are encode-when-set exactly at their
/// statuses (the wire vocabulary mirrors this record one to one).
struct DialogTurnRecord {
    std::optional<ipc::ContextUsage> context_usage;
    std::string turn_id;
    std::string agent_task_id;
    /// "pending" / "ok" / "failed"
    std::string status = "pending";
    std::string user_text;
    std::string reply_text;
    std::string error;
    std::uint64_t sequence = 0;
    std::int64_t recorded_at_ms = 0;
};

/// One session's dialog thread (DEC-027): the bounded turn log plus the
/// in-flight bookkeeping. Service-memory state (workflow-registry
/// discipline: declared volatile until the DEC-011 persistence items land);
/// dropped when the session closes. `total_recorded` counts every turn ever
/// appended so the snapshot's `truncated` flag stays truthful across
/// oldest-drop trimming.
struct SessionDialogLog {
    std::vector<DialogTurnRecord> turns;
    std::uint64_t next_sequence = 1;
    std::uint64_t total_recorded = 0;
    /// Non-empty while one turn's model call is in flight (the accepted
    /// turn's id); a second session.chat is refused invalid_state.
    std::string in_flight_turn_id;
    std::size_t max_turns = 200;
};

/// Dialog registry (DEC-027): per-session threads, bounded by `capacity`
/// (sessions) and each log by its per-session turn bound with oldest-drop
/// trimming. All access happens under `mutex`.
struct DialogRegistry {
    std::mutex mutex;
    std::map<std::string, SessionDialogLog> sessions;
    std::size_t capacity = 16;

    bool full() const { return sessions.size() >= capacity; }
};

/// Stable name of a MiraHost task progress state for IPC payloads.
const char *progress_name(TaskProgress progress);

} // namespace mirage::runtime::detail
