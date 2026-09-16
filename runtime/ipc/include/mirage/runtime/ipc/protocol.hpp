#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mirage::runtime::ipc {

/// Wire protocol version (DEC-007). Requests and responses both carry it;
/// a mismatch is a protocol error, never a best-effort decode.
inline constexpr int kProtocolVersion = 1;

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

/// One scripted desktop action of an M1 task (DEC-007 item 5). The M1
/// service-side driver loop executes steps in order, bracketing each with
/// the host operation surface; the step set mirrors the M1 desktop
/// capabilities (DEC-008).
enum class StepKind {
    FilesystemRead, ///< `argument` is the file path
    ProcessExecute, ///< `argument` is the shell command line
};

struct TaskStep {
    StepKind kind = StepKind::FilesystemRead;
    std::string argument;
};

struct HelloRequest {};

struct SubmitTaskRequest {
    std::string goal;
    std::vector<TaskStep> steps;
    /// Optional wall-clock budget for one process.execute step; clamped by
    /// the service's own cap. Filesystem reads are bounded by the provider
    /// contract, not by this field.
    std::optional<std::chrono::milliseconds> step_timeout;
};

struct ListTasksRequest {};

struct InspectTaskRequest {
    std::string task_id;
};

/// Requests cooperative cancellation of a task (M1-05 cancellation path):
/// the service interrupts the in-flight desktop action, marks the pending
/// steps cancelled/skipped and lets the pinned runtime settle the task as
/// Cancelled. Unknown ids are not_found; already terminal tasks surface the
/// pinned rejection verbatim (code "pinned_runtime", message prefixed
/// "invalid_state:" — the same passthrough shape as task.submit) instead of
/// reviving anything.
struct CancelTaskRequest {
    std::string task_id;
};

struct ShutdownRequest {};

/// Subscribes the connection to the service event stream (DEC-012 decision
/// 2). No parameters; the subscription is connection-scoped state that dies
/// with the connection. A pre-M1.5 server rejects the unknown op with a
/// stable protocol_error, which clients translate into polling fallback.
struct SubscribeEventsRequest {};

/// Drops the connection's event subscription; idempotent. Events already
/// queued for the connection may still arrive after the acknowledgement.
struct UnsubscribeEventsRequest {};

using Request = std::variant<HelloRequest, SubmitTaskRequest, ListTasksRequest, InspectTaskRequest,
                             CancelTaskRequest, ShutdownRequest, SubscribeEventsRequest,
                             UnsubscribeEventsRequest>;

// ---------------------------------------------------------------------------
// Responses
// ---------------------------------------------------------------------------

struct ServiceIdentity {
    std::string name;
    std::string mirage_version;
    std::string mira_core_version;
    std::string host_status;
    int protocol = kProtocolVersion;
    /// DEC-012 event-capability advertisement. An event-capable server
    /// always encodes the member (true); the decoder leaves it disengaged
    /// when the wire form omits it, so consumers read `value_or(false)` —
    /// the optional preserves wire presence for byte-exact round-trips.
    std::optional<bool> events;
};

struct TaskSubmitted {
    std::string task_id;
};

struct TaskSummary {
    std::string id;
    std::string goal;
    std::string progress;
};

struct TaskList {
    std::vector<TaskSummary> tasks;
};

/// One executed (or pending) step of a task as reported by task.inspect.
/// `operation_id` is the pinned OperationRecord identity the driver's
/// begin_operation returned, tying the step result to the control plane
/// trace (DEC-007 item 5). Structured results are carried in `result`,
/// capped by the service; `result_truncated` marks the cap. `permission`
/// is the RULE-05 permission outcome (DEC-010): "allowed" / "confirmed" /
/// "denied" / "confirmation_rejected" once judged, empty before that; a
/// denied step carries no operation id because no desktop action was
/// admitted.
struct StepView {
    int index = 0;
    std::string kind; ///< "filesystem.read" or "process.execute"
    /// "pending" / "running" / "ok" / "failed" / "skipped" / "cancelled"
    std::string status;
    std::string operation_id;
    std::string permission;
    bool ok = false;
    int exit_code = -1; ///< process.execute only; -1 otherwise
    std::string result; ///< file content or captured output, capped
    bool result_truncated = false;
    std::string error; ///< stable failure summary, safe for UI
};

struct InspectTask {
    std::string id;
    std::string goal;
    std::string progress;
    /// Meaningful only once the task reached a terminal progress state.
    bool has_success = false;
    bool success = false;
    std::vector<StepView> steps;
};

/// Acknowledgement of task.cancel with the task's progress as of the
/// cancellation request (typically "Cancelling" or a terminal state).
struct TaskCancelled {
    std::string task_id;
    std::string progress;
};

struct ShutdownAccepted {};

using ResponsePayload = std::variant<ServiceIdentity, TaskSubmitted, TaskList, InspectTask,
                                     TaskCancelled, ShutdownAccepted>;

/// Stable error surface (DEC-007 item 4). `code` is from the mirage.ipc
/// domain ("protocol_error", "unsupported", "invalid_argument", "not_found",
/// "invalid_state", "unavailable", "internal"); `message` is safe for logs
/// and UI.
struct IpcError {
    std::string code;
    std::string message;
};

struct Response {
    bool ok = false;
    std::uint64_t id = 0; ///< correlation id echoed from the request
    ResponsePayload payload;
    IpcError error; ///< meaningful only when ok is false
};

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

/// Encodes one request envelope (protocol version + correlation id + body)
/// as a wire payload (no framing prefix; framing.hpp adds that).
std::string encode_request(std::uint64_t id, const Request &body);

struct RequestDecode {
    bool ok = false;
    std::uint64_t id = 0; ///< correlation id, meaningful when ok
    Request body = HelloRequest{};
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode of one request payload; any deviation (bad JSON, wrong
/// version, unknown op, malformed fields) fails with a stable reason.
RequestDecode decode_request(std::string_view payload);

/// Encodes one response envelope as a wire payload.
std::string encode_response(const Response &response);

struct ResponseDecode {
    bool ok = false;
    Response response;
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode of one response payload.
ResponseDecode decode_response(std::string_view payload);

// ---------------------------------------------------------------------------
// Events (DEC-012, wire semantics frozen with M1.5-02)
// ---------------------------------------------------------------------------

/// `task.updated` snapshot (DEC-012 decision 3): published on task creation,
/// progress advances and terminal settlement. `progress` carries the same
/// product projection as task.inspect; `has_success`/`success` are false
/// until a terminal state, then mirror task.inspect's success members.
struct TaskUpdatedEvent {
    std::string task_id;
    std::string goal;
    std::string progress;
    bool has_success = false;
    bool success = false;
};

/// `host.status`: the Mira Host five-state name (DEC-004, lowercase stable
/// form, same set as hello's host_status) published on every transition.
struct HostStatusEvent {
    std::string status;
};

/// `events.overflow`: synthetic marker published to one connection when its
/// bounded event queue dropped events; `dropped` counts the losses the
/// marker reports. Snapshots remain the source of truth after it.
struct EventsOverflowEvent {
    std::uint64_t dropped = 0;
};

/// Closed M1.5 event set; M2+ events join additively.
using EventPayload = std::variant<TaskUpdatedEvent, HostStatusEvent, EventsOverflowEvent>;

/// One decoded event frame minus its envelope bookkeeping: the per-connection
/// `seq` plus the payload. `seq` is assigned by the sender per connection,
/// starting at 1 and strictly monotonic.
struct Event {
    std::uint64_t seq = 0;
    EventPayload payload;
};

/// Stable event names ("task.updated" / "host.status" / "events.overflow").
const char *event_name(const EventPayload &payload);

/// Encodes one event envelope `{"v":1,"seq":N,"event":"<名称>",...载荷}` as a
/// wire payload (no framing prefix).
std::string encode_event(const Event &event);

struct EventDecode {
    bool ok = false;
    Event event;       ///< meaningful when ok
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode of one event payload; any deviation (bad JSON, wrong
/// version, non-positive seq, unknown event name, malformed payload fields)
/// fails with a stable reason.
EventDecode decode_event(std::string_view payload);

/// Stable string form of a StepKind ("filesystem.read" / "process.execute").
const char *step_kind_name(StepKind kind);

/// Parses a stable StepKind name; nullopt for anything else. Added for the
/// M1-07 recovery hydration (recovered step records carry the stable name
/// and must map back onto the TaskStep surface); no wire change.
std::optional<StepKind> step_kind_from_name(std::string_view name);

} // namespace mirage::runtime::ipc
