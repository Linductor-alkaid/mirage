#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mirage::runtime::ipc {

/// Wire protocol version (DEC-007). Requests and responses both carry it;
/// a mismatch is a protocol error, never a best-effort decode.
inline constexpr int kProtocolVersion = 1;

/// Wire projection budget for one observation semantic snapshot (DEC-026,
/// RULE-07): the desktop.observe response stays well inside the 1 MiB frame
/// cap even alongside a full visual generation, so the service projects at
/// most this many semantic nodes and marks the remainder through
/// ObservationSemantic::truncated. The service-side snapshot keeps the
/// assembler's own capture budget; this bounds only the wire view.
inline constexpr std::size_t kObservationNodeWireBudget = 1024;

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
    /// Optional session binding (DEC-021): the task's goal lands in this
    /// session's conversation and its turns/output publish under it. Absent
    /// means the primary session, preserving the M1 wire form (old clients
    /// are unaffected).
    std::optional<std::string> session_id;
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

/// Answers one pending permission confirmation (DEC-020). Any connection may
/// respond; the first response that moves the request out of pending wins,
/// later ones (duplicate, already decided or expired ids) surface the stable
/// not_found error. The confirmation surface itself is optional service
/// equipment: without it the service answers `unavailable`.
struct RespondPermissionRequest {
    std::string request_id;
    bool approved = false;
};

/// Requests the pending-confirmation snapshot (DEC-020): the resync face of
/// the permission confirmation stream (events are notifications, snapshots
/// are the source of truth). Connection-independent; empty `pending` means
/// nothing awaits confirmation right now.
struct ListPermissionsRequest {};

/// Lists the service's sessions (DEC-021): id, projected state and creation
/// time for every session in the registry, including the primary session
/// opened at service start. The snapshot face of the session event stream.
struct ListSessionsRequest {};

/// Opens one more session on the hosted pinned runtime (DEC-021), bound to
/// the same desktop environment. Fails with the stable `unavailable` error
/// when the service-side session capacity is exhausted; pinned rejections
/// surface in the passthrough shape.
struct OpenSessionRequest {};

/// Requests one session's conversation history (DEC-021): the pinned
/// conversation projection (user messages and task outcomes) capped to the
/// newest `limit` entries (service default and cap apply when absent).
/// `truncated` marks older entries beyond the budget. Unknown session ids
/// are a stable not_found error.
struct SessionHistoryRequest {
    std::string session_id;
    std::optional<int> limit;
};

/// Lists the service's workflow catalog (DEC-023): product identity, head
/// version digest, validation and runnability for every workflow the service
/// saved or published. The pinned library stays the execution-side authority;
/// the catalog is the product index of what passed through this service.
struct WorkflowListRequest {};

/// Appends one draft version of the definition (DEC-023): strict pinned IR
/// decode, then a NotValidated library record — resolvable but not runnable
/// (W-04). `definition_json` carries the serialized Workflow IR v1 JSON
/// object; the decoder canonicalizes it, the service enforces the 256 KiB
/// byte budget.
struct WorkflowSaveRequest {
    std::string definition_json;
};

/// Runs the pinned publish gate on the definition (DEC-023): structural
/// validation, the empty-parameter DryRun drive and a DryRunPassed library
/// record with content-derived evidence. A head record with the same content
/// and evidence settles as an idempotent NoOp.
struct WorkflowPublishRequest {
    std::string definition_json;
};

/// Removes the product catalog entry (DEC-023). The pinned append-only
/// version history is untouched by design (runs resolve their creation-time
/// version by digest); runs already created are unaffected. Fails closed
/// while a non-terminal run exists for the workflow.
struct WorkflowDeleteRequest {
    std::string workflow_id;
};

/// The exposed view of the hosted BuiltIn tool registry (DEC-022 decision 1,
/// DEC-023): the atom catalog's pinned projection. Empty until desktop
/// capability tools are registered (M5-05 second round).
struct WorkflowAtomCatalogRequest {};

/// Snapshot of the service's workflow run registry (DEC-023) with the live
/// per-run state projected from the pinned runtime. The authoritative face
/// for resync after `workflow.run_updated` event gaps.
struct WorkflowRunsRequest {};

/// Starts one workflow run from the library (DEC-023): the version is pinned
/// by digest (W-03) and must be runnable (W-04); the drive is asynchronous
/// (pinned capacity applies) and is monitored through `workflow.run_updated`
/// events and `workflow.runs`.
struct WorkflowRunRequest {
    std::string workflow_id;
    /// Empty resolves to the catalog head digest; an unknown workflow is a
    /// stable not_found error, a non-runnable head surfaces the pinned W-04
    /// rejection.
    std::string digest;
    /// Serialized JSON object of run parameters; empty means none.
    std::string parameters_json;
    /// Optional closed policy name (stable lowercase form); empty uses the
    /// definition default.
    std::string policy;
};

/// Requests cooperative cancellation of one workflow run (DEC-023). The
/// pinned cancel is idempotent and never revives a terminal run; the reply
/// carries the post-call run state.
struct WorkflowCancelRunRequest {
    std::string run_id;
};

/// Reads one catalog workflow's head definition (DEC-026, M5-06; DEC-023
/// backlog item 1): the serialized IR v1 JSON the service last saved or
/// published for the workflow. The pinned library keeps no definition-body
/// read API (append-only version records, W-03), so the service-side product
/// registry retains the head content it passed through — the same product
/// index that projects workflow.list. Unknown ids are a stable not_found.
struct WorkflowGetRequest {
    std::string workflow_id;
};

/// Assembles one on-demand desktop observation (DEC-026, M5-06; design doc
/// section 6): the observation projection enters the UI observation face,
/// including the semantic snapshot and the published visual generation
/// (`visual_snapshot_ref` + regions — the M3 non-goal "visual references
/// enter the UI observation face"). Components follow the assembler's
/// requested-means-mandatory discipline: the frame members are always
/// captured, `semantic` defaults on, the visual component is opt-in and
/// fails closed with the stable `unavailable` error when no visual
/// generation is available. There is no event form: the M1 driver form has
/// no observation producer, so a subscription stream would have nothing
/// honest to stream (DEC-026).
struct DesktopObserveRequest {
    /// Capture the focused window's accessibility snapshot (default true).
    bool semantic = true;
    /// Project the current visual generation from the bound visual registry
    /// (default false; DEC-016 decision 2 — the visual surface stays dark
    /// unless asked for).
    bool visual = false;
};

using Request = std::variant<
    HelloRequest, SubmitTaskRequest, ListTasksRequest, InspectTaskRequest, CancelTaskRequest,
    ShutdownRequest, SubscribeEventsRequest, UnsubscribeEventsRequest, RespondPermissionRequest,
    ListPermissionsRequest, ListSessionsRequest, OpenSessionRequest, SessionHistoryRequest,
    WorkflowListRequest, WorkflowSaveRequest, WorkflowPublishRequest, WorkflowDeleteRequest,
    WorkflowAtomCatalogRequest, WorkflowRunsRequest, WorkflowRunRequest, WorkflowCancelRunRequest,
    WorkflowGetRequest, DesktopObserveRequest>;

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
    /// DEC-020 async permission-confirmation advertisement: the
    /// `permission.respond` / `permission.list` request face is served. Same
    /// optional-encodes-when-set discipline as `events`.
    std::optional<bool> permissions;
    /// DEC-021 session-face advertisement: the `session.list` /
    /// `session.open` / `session.history` request face is served. Same
    /// optional-encodes-when-set discipline as `events`.
    std::optional<bool> sessions;
    /// DEC-023 workflow-face advertisement: the `workflow.*` request face is
    /// served. Same optional-encodes-when-set discipline as `events`.
    std::optional<bool> workflows;
    /// DEC-026 observation-face advertisement: the `desktop.observe` request
    /// face is served. Same optional-encodes-when-set discipline as `events`.
    std::optional<bool> observation;
};

struct TaskSubmitted {
    std::string task_id;
    /// Session the task's conversation landed in (DEC-021): the explicit
    /// binding when the request carried one, the primary session otherwise.
    /// Encode-when-set; legacy test doubles may omit it.
    std::optional<std::string> session_id;
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

/// Acknowledgement of permission.respond (DEC-020): the id is echoed so the
/// responder can correlate; a request that was not pending (unknown, already
/// decided or expired) is a not_found error instead, never a false ack.
struct PermissionResponded {
    std::string request_id;
};

/// One pending confirmation as reported by permission.list (DEC-020).
/// `timeout_ms` is the remaining wait budget at snapshot time (positive);
/// `resource` is the path or command line the capability would act on,
/// surfaced so an approver can see what is being approved (DEC-020 decision
/// 8's disclosure boundary).
struct PendingPermission {
    std::string request_id;
    std::string capability;
    std::string resource;
    std::string task_id;
    std::int64_t timeout_ms = 0;
};

/// Snapshot of the pending-confirmation set (DEC-020); may be empty. The
/// authoritative face for resync after event gaps (DEC-012 consistency
/// model).
struct PermissionPendingList {
    std::vector<PendingPermission> pending;
};

/// One session as reported by session.list (DEC-021). `state` is the stable
/// lowercase name of the pinned SessionState projection ("opening" /
/// "autonomous" / "takeover_pending" / "human_controlled" / "resuming" /
/// "closing" / "closed" / "failed"); a session whose pinned snapshot fails
/// surfaces the conservative `failed` name instead of an invented state.
/// `created_at_ms` is the service-side wall-clock registration time.
struct SessionSummary {
    std::string id;
    std::string state;
    std::int64_t created_at_ms = 0;
};

/// Snapshot of the session registry (DEC-021), primary session included;
/// may be empty. The authoritative face for resync after event gaps.
struct SessionList {
    std::vector<SessionSummary> sessions;
};

/// Acknowledgement of session.open (DEC-021): the new session's id, as it
/// appears in session.list and as a task.submit `session_id` binding.
struct SessionOpened {
    std::string session_id;
};

/// One conversation entry as reported by session.history (DEC-021).
/// `kind` is "user" (task goal landed in the session) or "outcome" (task
/// settlement summary); `sequence` is the entry's position in the session's
/// event sequence; `recorded_at_ms` is the wall-clock time the underlying
/// event was recorded.
struct SessionHistoryEntry {
    std::string kind;
    std::string text;
    std::uint64_t sequence = 0;
    std::int64_t recorded_at_ms = 0;
};

/// One session's conversation history (DEC-021): the newest `limit` entries
/// of the pinned conversation projection in session order; `truncated` marks
/// that older entries exist beyond the budget. The authoritative face for
/// rebuilding the conversation view after event gaps.
struct SessionHistory {
    std::string session_id;
    std::vector<SessionHistoryEntry> entries;
    bool truncated = false;
};

/// One catalog entry as reported by workflow.list (DEC-023). `validation` is
/// the pinned WorkflowValidationResult stable name of the head version
/// ("not_validated" / "dry_run_passed" / "validated" / "rejected");
/// `runnable` projects W-04 (a head a run may reference).
struct WorkflowSummary {
    std::string workflow_id;
    std::string name;
    std::string head_digest;
    std::string validation;
    bool runnable = false;
    std::int64_t updated_at_ms = 0;
};

/// Snapshot of the workflow catalog (DEC-023); may be empty. The service-side
/// product index over the pinned library.
struct WorkflowList {
    std::vector<WorkflowSummary> workflows;
};

/// Acknowledgement of workflow.save (DEC-023): the definition's identity and
/// the content digest of the appended NotValidated draft version.
struct WorkflowSaved {
    std::string workflow_id;
    std::string digest;
};

/// Acknowledgement of workflow.publish (DEC-023): the gated version's
/// identity, content digest and gate drive id; `idempotent` marks the NoOp
/// replay of a head record with the same content and evidence.
struct WorkflowPublished {
    std::string workflow_id;
    std::string digest;
    std::string dry_run_id;
    bool idempotent = false;
};

/// Acknowledgement of workflow.delete (DEC-023): the removed catalog entry.
/// The pinned version history is immutable by design and stays.
struct WorkflowDeleted {
    std::string workflow_id;
};

/// One exposed tool as reported by workflow.atom.catalog (DEC-023): the
/// pinned BuiltIn registry's exposed view. `version` is the semantic version
/// rendered as "major.minor.patch"; `parameters_schema_json` carries the
/// serialized JSON Schema object of the tool input.
struct ExposedTool {
    std::string wire_name;
    std::string version;
    std::string description;
    bool has_side_effects = false;
    std::string parameters_schema_json;
};

/// Snapshot of the exposed tool projection (DEC-023); may be empty until
/// desktop capability tools are registered.
struct WorkflowAtomCatalog {
    std::vector<ExposedTool> tools;
};

/// One workflow run as reported by workflow.runs (DEC-023). `state` is the
/// pinned WorkflowRunState stable lowercase name ("created" / "running" /
/// "paused" / "waiting_user" / "waiting_agent" / "completed" / "failed" /
/// "cancelled"), projected live from the pinned runtime; a run whose pinned
/// snapshot fails surfaces the conservative `failed` name.
struct WorkflowRunSummary {
    std::string run_id;
    std::string workflow_id;
    std::string state;
    std::uint64_t run_epoch = 0;
    std::int64_t created_at_ms = 0;
};

/// Snapshot of the workflow run registry (DEC-023); may be empty. The
/// authoritative face for resync after workflow.run_updated event gaps.
struct WorkflowRunList {
    std::vector<WorkflowRunSummary> runs;
};

/// Acknowledgement of workflow.run (DEC-023): the new run's id, as it appears
/// in workflow.runs and workflow.run_updated events.
struct WorkflowRunStarted {
    std::string run_id;
};

/// Acknowledgement of workflow.cancel (DEC-023) with the run's state as of
/// the reply — the pinned cancel_run view's current name, terminal states
/// included (idempotent re-cancels of a settled run return that terminal).
struct WorkflowRunCancelled {
    std::string run_id;
    std::string state;
};

/// One workflow's head definition as reported by workflow.get (DEC-026):
/// `definition_json` is the canonical serialization of the IR v1 JSON object
/// the service last saved or published for the workflow, `digest` the head
/// content digest it is addressable by (W-03). Editors rebuild their working
/// copy from it — the cross-session editing face DEC-023 deferred to the
/// definition read face.
struct WorkflowDefinitionView {
    std::string workflow_id;
    std::string digest;
    std::string definition_json;
};

/// Rectangle in global desktop coordinates as carried by the observation
/// projection (DEC-026): the desktop layer's WindowGeometry rendered as a
/// plain wire object, keeping the ipc layer free of desktop types.
struct ObservationGeometry {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

/// One node of the observation projection's semantic snapshot (DEC-026):
/// the desktop SemanticSnapshot node flattened for the wire. `parent` is
/// the node's index into the projected `nodes` array, -1 at roots (the
/// desktop layer's kNoParent sentinel has no wire form). Producer-side
/// confidence-like scoring fields stay off the wire (no UI consumer — the
/// DEC-023 add-what-is-consumed discipline).
struct ObservationNode {
    std::string ref; ///< "@e5"; stable within one snapshot only
    std::string role;
    std::string name;
    std::string description;
    std::int64_t parent = -1;
    ObservationGeometry geometry;
    bool focused = false;
    bool enabled = true;
};

/// The semantic component of the observation projection (DEC-026): the
/// captured SemanticSnapshot. `truncated` marks that more nodes exist in
/// the service-side snapshot than the wire projection budget carried —
/// incompleteness is explicit, never silent.
struct ObservationSemantic {
    std::string application;
    std::string window_title;
    std::vector<ObservationNode> nodes;
    bool truncated = false;
};

/// One visual object of the observation projection (DEC-026): a region of
/// the published visual generation. `source` is the closed provenance
/// vocabulary ("ocr" / "detector" / "template" / "geometry"); `ref` is the
/// executable "@vN" handle the visual reference registry issued.
struct ObservationRegion {
    std::string ref;
    std::string source;
    ObservationGeometry geometry;
    std::string text;
    std::string template_id;
};

/// One on-demand observation as reported by desktop.observe (DEC-026). The
/// frame members are always present; `semantic` is encode-when-set (the
/// component was requested and captured); `visual_snapshot_ref` and
/// `visual_regions` are an encode-when-set pair (the visual component was
/// requested and a generation was projected), the `visual_snapshot_ref`
/// scope handle being the M3 non-goal's "visual references enter the UI
/// observation face" carrier. Snapshots are the only truth — the response
/// is a point-in-time capture, not a subscription.
struct ObservationView {
    std::string active_application;
    std::string active_window;
    ObservationGeometry window_geometry;
    bool window_focused = false;
    std::string focused_element;
    std::int32_t pointer_x = 0;
    std::int32_t pointer_y = 0;
    std::string environment_state;
    std::optional<ObservationSemantic> semantic;
    std::optional<std::string> visual_snapshot_ref;
    std::optional<std::vector<ObservationRegion>> visual_regions;
};

using ResponsePayload =
    std::variant<ServiceIdentity, TaskSubmitted, TaskList, InspectTask, TaskCancelled,
                 ShutdownAccepted, PermissionResponded, PermissionPendingList, SessionList,
                 SessionOpened, SessionHistory, WorkflowList, WorkflowSaved, WorkflowPublished,
                 WorkflowDeleted, WorkflowAtomCatalog, WorkflowRunList, WorkflowRunStarted,
                 WorkflowRunCancelled, WorkflowDefinitionView, ObservationView>;

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

/// `permission.request` (DEC-020): a Confirm rule hit the async confirmation
/// surface. Broadcast to every subscribed connection; `timeout_ms` is the
/// remaining wait budget at publish time, after which the confirmation
/// converges fail closed. `capability` is from the DEC-010 vocabulary;
/// `resource` is the path or command line (DEC-020 decision 8 disclosure).
/// The judgment outcome surfaces through task.updated / task.inspect step
/// trace, not through this event.
struct PermissionRequestedEvent {
    std::string request_id;
    std::string capability;
    std::string resource;
    std::string task_id;
    std::int64_t timeout_ms = 0;
};

/// `session.updated` (DEC-021): a session entered the registry (open).
/// `state` uses the session state vocabulary of SessionSummary; gradual
/// in-session state changes are not broadcast (the list snapshot is the
/// source of truth).
struct SessionUpdatedEvent {
    std::string session_id;
    std::string state;
};

/// `session.message` (DEC-021): one entry joined the session's conversation
/// projection. `kind` is "user" (the task goal) or "outcome" (the task
/// settlement summary); `text` is the message body; `sequence` is the
/// entry's position in the session's event sequence. The history snapshot's
/// entries additionally carry the recorded-at time; events leave timestamp
/// semantics to the receiving client.
struct SessionMessageEvent {
    std::string session_id;
    std::string task_id;
    std::string kind;
    std::string text;
    std::uint64_t sequence = 0;
};

/// `session.turn` (DEC-021): one bounded unit of session work settled — a
/// scripted driver step in the M1 form, one agent-loop iteration once the
/// model loop lands. `kind` and `status` reuse the step trace vocabularies
/// (step_kind_name / settled step statuses). Turns publish on settlement
/// only; task.updated already covers the in-progress semantics.
struct SessionTurnEvent {
    std::string session_id;
    std::string task_id;
    int step = 0;
    std::string kind;
    std::string status;
};

/// `session.output` (DEC-021): one incremental output chunk of a step's
/// structured result, capped like the task.inspect result budget;
/// `truncated` marks the cap. The M1 driver emits one complete chunk per
/// settled step; streaming producers emit many, same shape.
struct SessionOutputEvent {
    std::string session_id;
    std::string task_id;
    int step = 0;
    std::string chunk;
    bool truncated = false;
};

/// `workflow.run_updated` (DEC-023): one workflow run's state published from
/// the pinned workflow event stream, translated by the service — never
/// guessed service-side. `state` uses the pinned WorkflowRunState vocabulary
/// of WorkflowRunSummary ("running" for a started run, the terminal name for
/// a settled one); `summary` is encode-when-set and carries the settled
/// pinned safe_summary only. `workflow.runs` is the snapshot source of truth;
/// events do not fire for intermediate control-plane states.
struct WorkflowRunUpdatedEvent {
    std::string run_id;
    std::string workflow_id;
    std::string state;
    std::uint64_t run_epoch = 0;
    std::optional<std::string> summary;
};

/// Closed event set; new events join additively (DEC-012).
using EventPayload =
    std::variant<TaskUpdatedEvent, HostStatusEvent, EventsOverflowEvent, PermissionRequestedEvent,
                 SessionUpdatedEvent, SessionMessageEvent, SessionTurnEvent, SessionOutputEvent,
                 WorkflowRunUpdatedEvent>;

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
