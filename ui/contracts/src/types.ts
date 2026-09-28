/// TypeScript mirror of the Mirage Local IPC protocol v1 wire contract.
///
/// Sources of truth (kept in sync by golden-vector tests, M1.5-01):
/// - C++ structures: `runtime/ipc/include/mirage/runtime/ipc/protocol.hpp`
/// - wire schema doc: `docs/design/mirage-ipc-protocol-v1.md` (M1.5-01)
/// - event extension: DEC-012 draft (`docs/decisions/DEC-012-*.md`, Proposed).
///   The event surface here follows the DEC-012 message table draft, which
///   M1.5-04 is explicitly allowed to consume before the decision is
///   Accepted; the C++ service side lands in M1.5-02.

/** Wire protocol version (DEC-007). Mismatch is a protocol error. */
export const PROTOCOL_VERSION = 1 as const;

/** Stable step kind names on the wire (`step_kind_name` in protocol.hpp). */
export type StepKind = 'filesystem.read' | 'process.execute';

/** One scripted desktop action of a task; wire members are `op` + `arg`. */
export interface TaskStep {
    op: StepKind;
    arg: string;
}

/** Host five-state set (DEC-004), stable lowercase wire form. */
export type HostStatus = 'stopped' | 'starting' | 'running' | 'stopping' | 'failed';

/** Product-layer task progress projection of the pinned TaskState (DEC-004). */
export type TaskProgress =
    | 'Idle'
    | 'Active'
    | 'Paused'
    | 'Cancelling'
    | 'Completed'
    | 'Failed'
    | 'Cancelled'
    | 'Unknown';

/** Lifecycle of one scripted step (`step_status` namespace in task_registry.hpp). */
export type StepStatus = 'pending' | 'running' | 'ok' | 'failed' | 'skipped' | 'cancelled';

/** Settled-step status vocabulary carried by `session.turn` (DEC-021): turns
 * publish on settlement only, so the in-flight names are absent. */
export type TurnStatus = 'ok' | 'failed' | 'cancelled' | 'skipped';

/** Session state projection of the pinned SessionState (DEC-021), stable
 * lowercase wire form. */
export type SessionState =
    | 'opening'
    | 'autonomous'
    | 'takeover_pending'
    | 'human_controlled'
    | 'resuming'
    | 'closing'
    | 'closed'
    | 'failed';

/** Conversation-entry vocabulary (DEC-021): "user" marks a task goal landing
 * in the session, "outcome" a task settlement summary. */
export type SessionMessageKind = 'user' | 'outcome';

/** Workflow run state projection of the pinned WorkflowRunState (DEC-023),
 * stable lowercase wire form. */
export type WorkflowRunState =
    | 'created'
    | 'running'
    | 'paused'
    | 'waiting_user'
    | 'waiting_agent'
    | 'completed'
    | 'failed'
    | 'cancelled';

/** Closed execution policy vocabulary (DEC-023), stable lowercase wire form;
 * the optional `workflow.run` member may be omitted to use the definition
 * default. */
export type WorkflowPolicyName = 'strict' | 'recoverable' | 'agent_assisted' | 'interactive' | 'dry_run';

/** Visual-region provenance vocabulary (DEC-026): the desktop layer's
 * VisualRegionSource set in stable lowercase wire form. */
export type ObservationRegionSource = 'ocr' | 'detector' | 'template' | 'geometry';

/** Workflow validation vocabulary (DEC-023): the pinned
 * WorkflowValidationResult of a workflow's head version; only
 * `dry_run_passed` / `validated` heads are runnable (W-04). */
export type WorkflowValidation = 'not_validated' | 'dry_run_passed' | 'validated' | 'rejected';

// ---------------------------------------------------------------------------
// Requests (client -> service)
// ---------------------------------------------------------------------------

export type RequestBody =
    | { op: 'hello' }
    | {
          op: 'task.submit';
          goal: string;
          steps: TaskStep[];
          step_timeout_ms?: number;
          /** Session binding (DEC-021); absent = primary session. */
          session_id?: string;
      }
    | { op: 'task.list' }
    | { op: 'task.inspect'; task_id: string }
    | { op: 'task.cancel'; task_id: string }
    | { op: 'service.shutdown' }
    | { op: 'events.subscribe' }
    | { op: 'events.unsubscribe' }
    | { op: 'permission.respond'; request_id: string; approved: boolean }
    | { op: 'permission.list' }
    | { op: 'session.list' }
    | { op: 'session.open' }
    /** Dialog face (DEC-027, M5-06; DEC-025 backlog 3): one dialog turn to
     * the model layer — asynchronous, the reply rides the
     * `session.chat_updated` event stream. */
    | { op: 'session.chat'; session_id: string; text: string }
    /** The dialog thread's resync snapshot. */
    | { op: 'session.chat.history'; session_id: string; limit?: number }
    /** Permission policy face (M5-07, DEC-010/DEC-011): the full DEC-010
     * rule set plus the persisted read-roots resource scope. */
    | { op: 'policy.get' }
    | {
          op: 'policy.set';
          rules: Record<string, string>;
          read_roots?: string[];
      }
    /** Session management face (DEC-026 backlog item 2): closes the session
     * and removes its registry entry — the pinned close cancels the
     * session's non-terminal tasks. The primary session is refused with the
     * stable `invalid_state` error (task.submit's default binding). */
    | { op: 'session.close'; session_id: string }
    | { op: 'session.history'; session_id: string; limit?: number }
    | { op: 'workflow.list' }
    /** IR v1 JSON object (DEC-013 aligned); strict pinned decode, 256 KiB
     * budget at the service. */
    | { op: 'workflow.save'; definition: Record<string, unknown> }
    | { op: 'workflow.publish'; definition: Record<string, unknown> }
    | { op: 'workflow.delete'; workflow_id: string }
    | { op: 'workflow.atom.catalog' }
    | { op: 'workflow.runs' }
    | {
          op: 'workflow.run';
          workflow_id: string;
          /** Absent resolves to the catalog head digest. */
          digest?: string;
          parameters?: Record<string, unknown>;
          /** Absent uses the definition default policy. */
          policy?: WorkflowPolicyName;
      }
    | { op: 'workflow.cancel'; run_id: string }
    /** Definition read face (DEC-026, M5-06; DEC-023 backlog 1): the head
     * definition content the service last saved or published. */
    | { op: 'workflow.get'; workflow_id: string }
    /** On-demand desktop observation (DEC-026, M5-06). `semantic` defaults
     * on, `visual` off (DEC-016: the visual surface stays dark unless asked);
     * requested components are mandatory — an unavailable one fails the
     * request with the stable `unavailable` error. No event form exists: the
     * M1 driver form has no observation producer to stream from. */
    | { op: 'desktop.observe'; semantic?: boolean; visual?: boolean };

// ---------------------------------------------------------------------------
// Responses (service -> client)
// ---------------------------------------------------------------------------

/** hello payload; `events` is the DEC-012 capability flag, `permissions` the
 * DEC-020 async confirmation flag, `sessions` the DEC-021 session-face flag,
 * `workflows` the DEC-023 workflow-face flag, `observation` the DEC-026
 * observation-face flag and `chat` the DEC-027 dialog-face flag (absent =
 * false for all). */
export interface ServiceIdentity {
    service: string;
    mirage_version: string;
    mira_core_version: string;
    host_status: HostStatus;
    protocol: number;
    events?: boolean;
    permissions?: boolean;
    sessions?: boolean;
    workflows?: boolean;
    observation?: boolean;
    chat?: boolean;
    policy?: boolean;
}

export interface TaskSubmitted {
    task_id: string;
    /** Session the task's conversation landed in (DEC-021). */
    session_id?: string;
}

export interface TaskSummary {
    id: string;
    goal: string;
    progress: TaskProgress;
}

/** One executed (or pending) step as reported by task.inspect. */
export interface StepView {
    index: number;
    kind: StepKind;
    status: StepStatus;
    operation_id: string;
    permission: string;
    ok: boolean;
    /** process.execute only; -1 otherwise. */
    exit_code: number;
    /** File content or captured output, capped by the service. */
    result: string;
    result_truncated: boolean;
    error: string;
}

export interface InspectTask {
    id: string;
    goal: string;
    progress: TaskProgress;
    /** Meaningful only once the task reached a terminal progress state. */
    has_success: boolean;
    success?: boolean;
    steps: StepView[];
}

export interface TaskCancelled {
    task_id: string;
    progress: TaskProgress;
}

/** The live permission policy as reported by policy.get / policy.set
 * (M5-07): the full DEC-010 rule set (capability name → "allow" /
 * "confirm" / "deny") plus the read-roots resource scope. */
export interface PolicyView {
    rules: Record<string, string>;
    read_roots: string[];
}

/** One pending confirmation as reported by permission.list (DEC-020);
 * `timeout_ms` is the remaining wait budget at snapshot time (positive). */
export interface PendingPermission {
    request_id: string;
    capability: string;
    resource: string;
    task_id: string;
    timeout_ms: number;
}

/** One session as reported by session.list (DEC-021). `state` is the stable
 * SessionState projection; `created_at_ms` is the service-side registration
 * wall-clock time. */
export interface SessionSummary {
    id: string;
    state: SessionState;
    created_at_ms: number;
}

/** One conversation entry as reported by session.history (DEC-021);
 * `sequence` is the entry's position in the session's event sequence. */
export interface SessionHistoryEntry {
    kind: SessionMessageKind;
    text: string;
    sequence: number;
    recorded_at_ms: number;
}

/** One catalog entry as reported by workflow.list (DEC-023); `runnable`
 * projects W-04 (a head version a run may reference). */
export interface WorkflowSummary {
    workflow_id: string;
    name: string;
    head_digest: string;
    validation: WorkflowValidation;
    runnable: boolean;
    updated_at_ms: number;
}

/** One exposed tool as reported by workflow.atom.catalog (DEC-023): the
 * pinned BuiltIn registry's exposed view. */
export interface ExposedTool {
    wire_name: string;
    version: string;
    description: string;
    has_side_effects: boolean;
    parameters_schema: Record<string, unknown>;
}

/** One workflow run as reported by workflow.runs (DEC-023); `state` is
 * projected live from the pinned runtime, `failed` when a snapshot fails. */
export interface WorkflowRunSummary {
    run_id: string;
    workflow_id: string;
    state: WorkflowRunState;
    run_epoch: number;
    created_at_ms: number;
}

/** One workflow's head definition as reported by workflow.get (DEC-026):
 * the IR v1 JSON the service last saved or published, addressable by
 * `digest` (W-03). Editors rebuild their working copy from it — the
 * cross-session editing face. */
export interface WorkflowDefinitionView {
    workflow_id: string;
    digest: string;
    definition: Record<string, unknown>;
}

/** Rectangle in global desktop coordinates (DEC-026 observation projection;
 * the desktop layer's WindowGeometry as a plain wire object). */
export interface ObservationGeometry {
    x: number;
    y: number;
    width: number;
    height: number;
}

/** One node of the observation semantic snapshot (DEC-026). `parent` is the
 * node's index into the projected `nodes` array, -1 at roots. Producer-side
 * scoring fields stay off the wire (no UI consumer — DEC-023 discipline). */
export interface ObservationNode {
    ref: string;
    role: string;
    name: string;
    description: string;
    parent: number;
    geometry: ObservationGeometry;
    focused: boolean;
    enabled: boolean;
}

/** The semantic component of the observation projection (DEC-026);
 * `truncated` marks nodes beyond the service's 1024-node wire budget —
 * incompleteness is explicit, never silent. */
export interface ObservationSemantic {
    application: string;
    window_title: string;
    nodes: ObservationNode[];
    truncated: boolean;
}

/** One visual object of the observation projection (DEC-026): a region of
 * the published visual generation; `ref` is the executable "@vN" handle. */
export interface ObservationRegion {
    ref: string;
    source: ObservationRegionSource;
    geometry: ObservationGeometry;
    text: string;
    template_id: string;
}

/** One on-demand observation as reported by desktop.observe (DEC-026). The
 * frame members are always present; `semantic` is present when requested
 * and captured; `visual_snapshot_ref` + `visual_regions` are a co-present
 * pair (the M3 non-goal's "visual references enter the UI observation
 * face" carrier). Point-in-time capture — snapshots are the only truth. */
export interface ObservationView {
    active_application: string;
    active_window: string;
    window_geometry: ObservationGeometry;
    window_focused: boolean;
    focused_element: string;
    pointer_x: number;
    pointer_y: number;
    environment_state: string;
    semantic?: ObservationSemantic;
    visual_snapshot_ref?: string;
    visual_regions?: ObservationRegion[];
}

/** Closed turn-status vocabulary of the dialog face (DEC-027): "pending"
 * marks an accepted turn whose model call is in flight, "ok" a settled turn
 * carrying `reply_text`, "failed" a settled turn carrying `error`. */
export type ChatTurnStatus = 'pending' | 'ok' | 'failed';

/** One dialog turn as reported by session.chat.history (DEC-027);
 * `reply_text` / `error` are present exactly at their statuses. */
export interface ChatTurnEntry {
    turn_id: string;
    status: ChatTurnStatus;
    user_text: string;
    reply_text?: string;
    error?: string;
    sequence: number;
    recorded_at_ms: number;
}

/** `session.chat_updated` payload (DEC-027): one dialog turn's lifecycle;
 * `reply_text` / `error` present exactly at ok / failed. The snapshot face
 * is session.chat.history. */
export interface ChatTurnUpdatedPayload {
    session_id: string;
    turn_id: string;
    status: ChatTurnStatus;
    user_text: string;
    reply_text?: string;
    error?: string;
    sequence: number;
}

/** Successful response payload, discriminated exactly like the C++ variant. */
export type ResponsePayload =
    | { kind: 'identity'; value: ServiceIdentity }
    | { kind: 'submitted'; value: TaskSubmitted }
    | { kind: 'list'; value: { tasks: TaskSummary[] } }
    | { kind: 'inspect'; value: InspectTask }
    | { kind: 'cancelled'; value: TaskCancelled }
    | { kind: 'shutdown-accepted' }
    | { kind: 'permission-responded'; value: { request_id: string } }
    | { kind: 'permission-list'; value: { pending: PendingPermission[] } }
    | { kind: 'policy-view'; value: PolicyView }
    | { kind: 'session-list'; value: { sessions: SessionSummary[] } }
    | { kind: 'session-opened'; value: { session_id: string } }
    | { kind: 'session-chat-accepted'; value: { turn_id: string } }
    | {
          kind: 'session-chat-history';
          value: { session_id: string; turns: ChatTurnEntry[]; truncated: boolean };
      }
    /** session.close reply (DEC-026 backlog item 2): the closed session's id
     * plus its post-close state (the SessionState vocabulary, "closed" when
     * the projected view is readable). Mirrors the workflow.cancel reply
     * shape and keeps the envelope distinguishable from session.open's. */
    | { kind: 'session-closed'; value: { session_id: string; state: SessionState } }
    | {
          kind: 'session-history';
          value: { session_id: string; entries: SessionHistoryEntry[]; truncated: boolean };
      }
    | { kind: 'workflow-list'; value: { workflows: WorkflowSummary[] } }
    | { kind: 'workflow-saved'; value: { workflow_id: string; digest: string } }
    | {
          kind: 'workflow-published';
          value: { workflow_id: string; digest: string; dry_run_id: string; idempotent: boolean };
      }
    | { kind: 'workflow-deleted'; value: { workflow_id: string } }
    | { kind: 'workflow-atom-catalog'; value: { tools: ExposedTool[] } }
    | { kind: 'workflow-run-list'; value: { runs: WorkflowRunSummary[] } }
    | { kind: 'workflow-run-started'; value: { run_id: string } }
    | { kind: 'workflow-run-cancelled'; value: { run_id: string; state: WorkflowRunState } }
    | { kind: 'workflow-get'; value: WorkflowDefinitionView }
    | { kind: 'observation-view'; value: ObservationView };

/** Stable error surface (DEC-007 item 4). `code` is from the mirage.ipc
 * domain; `pinned_runtime` is the verbatim passthrough shape used when the
 * pinned control plane rejects an operation (e.g. task.cancel on a settled
 * task, message prefixed "invalid_state:"). */
export interface IpcError {
    code:
        | 'protocol_error'
        | 'unsupported'
        | 'invalid_argument'
        | 'not_found'
        | 'invalid_state'
        | 'unavailable'
        | 'internal'
        | 'pinned_runtime';
    message: string;
}

export type ResponseEnvelop =
    | { ok: true; id: number; payload: ResponsePayload }
    | { ok: false; id: number; error: IpcError };

// ---------------------------------------------------------------------------
// Events (service -> client, DEC-012 draft; permission.request is DEC-020,
// session.* are DEC-021)
// ---------------------------------------------------------------------------

/** Closed event set; new events join additively, never by mutation. */
export type EventName =
    | 'task.updated'
    | 'host.status'
    | 'events.overflow'
    | 'permission.request'
    | 'session.updated'
    | 'session.message'
    | 'session.turn'
    | 'session.output'
    | 'workflow.run_updated'
    | 'session.chat_updated';

export const EVENT_NAMES: readonly EventName[] = [
    'task.updated',
    'host.status',
    'events.overflow',
    'permission.request',
    'session.updated',
    'session.message',
    'session.turn',
    'session.output',
    'workflow.run_updated',
    'session.chat_updated',
];

/** `task.updated` snapshot payload; `progress` matches task.inspect semantics. */
export interface TaskUpdatedPayload {
    task_id: string;
    goal: string;
    progress: TaskProgress;
    has_success: boolean;
    success: boolean;
}

/** `host.status` payload; five-state set of DEC-004. */
export interface HostStatusPayload {
    status: HostStatus;
}

/** `events.overflow` synthetic marker; `dropped` counts drop-oldest losses. */
export interface EventsOverflowPayload {
    dropped: number;
}

/** `permission.request` payload (DEC-020): a Confirm rule hit the async
 * confirmation surface; `timeout_ms` is the remaining wait budget at publish
 * time. Respond with `permission.respond` before it expires. */
export interface PermissionRequestedPayload {
    request_id: string;
    capability: string;
    resource: string;
    task_id: string;
    timeout_ms: number;
}

/** `session.updated` payload (DEC-021): a session entered the registry. */
export interface SessionUpdatedPayload {
    session_id: string;
    state: SessionState;
}

/** `session.message` payload (DEC-021): one entry joined the session's
 * conversation projection. */
export interface SessionMessagePayload {
    session_id: string;
    task_id: string;
    kind: SessionMessageKind;
    text: string;
    sequence: number;
}

/** `session.turn` payload (DEC-021): one bounded unit of session work
 * settled (a scripted driver step in the M1 form). */
export interface SessionTurnPayload {
    session_id: string;
    task_id: string;
    step: number;
    kind: StepKind;
    status: TurnStatus;
}

/** `session.output` payload (DEC-021): one incremental output chunk of a
 * step's structured result; `truncated` marks the cap. */
export interface SessionOutputPayload {
    session_id: string;
    task_id: string;
    step: number;
    chunk: string;
    truncated: boolean;
}

/** `workflow.run_updated` payload (DEC-023): one run's state published from
 * the pinned workflow event stream, translated by the service; `summary` is
 * present only on settled runs. workflow.runs is the snapshot truth. */
export interface WorkflowRunUpdatedPayload {
    run_id: string;
    workflow_id: string;
    state: WorkflowRunState;
    run_epoch: number;
    summary?: string;
}

export type ServerEvent =
    | ({ v: 1; seq: number; event: 'task.updated' } & TaskUpdatedPayload)
    | ({ v: 1; seq: number; event: 'host.status' } & HostStatusPayload)
    | ({ v: 1; seq: number; event: 'events.overflow' } & EventsOverflowPayload)
    | ({ v: 1; seq: number; event: 'permission.request' } & PermissionRequestedPayload)
    | ({ v: 1; seq: number; event: 'session.updated' } & SessionUpdatedPayload)
    | ({ v: 1; seq: number; event: 'session.message' } & SessionMessagePayload)
    | ({ v: 1; seq: number; event: 'session.turn' } & SessionTurnPayload)
    | ({ v: 1; seq: number; event: 'session.output' } & SessionOutputPayload)
    | ({ v: 1; seq: number; event: 'workflow.run_updated' } & WorkflowRunUpdatedPayload)
    | ({ v: 1; seq: number; event: 'session.chat_updated' } & ChatTurnUpdatedPayload);
