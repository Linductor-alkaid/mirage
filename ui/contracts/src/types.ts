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

// ---------------------------------------------------------------------------
// Requests (client -> service)
// ---------------------------------------------------------------------------

export type RequestBody =
    | { op: 'hello' }
    | { op: 'task.submit'; goal: string; steps: TaskStep[]; step_timeout_ms?: number }
    | { op: 'task.list' }
    | { op: 'task.inspect'; task_id: string }
    | { op: 'task.cancel'; task_id: string }
    | { op: 'service.shutdown' };

// ---------------------------------------------------------------------------
// Responses (service -> client)
// ---------------------------------------------------------------------------

/** hello payload; `events` is the DEC-012 capability flag (absent = false). */
export interface ServiceIdentity {
    service: string;
    mirage_version: string;
    mira_core_version: string;
    host_status: HostStatus;
    protocol: number;
    events?: boolean;
}

export interface TaskSubmitted {
    task_id: string;
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

/** Successful response payload, discriminated exactly like the C++ variant. */
export type ResponsePayload =
    | { kind: 'identity'; value: ServiceIdentity }
    | { kind: 'submitted'; value: TaskSubmitted }
    | { kind: 'list'; value: { tasks: TaskSummary[] } }
    | { kind: 'inspect'; value: InspectTask }
    | { kind: 'cancelled'; value: TaskCancelled }
    | { kind: 'shutdown-accepted' };

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
// Events (service -> client, DEC-012 draft)
// ---------------------------------------------------------------------------

/** Closed M1.5 event set; M2+ events join additively, never by mutation. */
export type EventName = 'task.updated' | 'host.status' | 'events.overflow';

export const EVENT_NAMES: readonly EventName[] = ['task.updated', 'host.status', 'events.overflow'];

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

export type ServerEvent =
    | ({ v: 1; seq: number; event: 'task.updated' } & TaskUpdatedPayload)
    | ({ v: 1; seq: number; event: 'host.status' } & HostStatusPayload)
    | ({ v: 1; seq: number; event: 'events.overflow' } & EventsOverflowPayload);
