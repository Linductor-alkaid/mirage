/// Transport abstraction between the UI and a Runtime Service peer. The mock
/// transport (M1.5-04) and the future real transports (dev bridge WebSocket,
/// M1.5-03/M1.5-05) implement the same surface, so views never know which
/// one is behind them (DEC-006: the IPC contract is the only coupling face).

import type {
    ChatTurnEntry,
    ExposedTool,
    InspectTask,
    ObservationView,
    ResponseEnvelop,
    ResponsePayload,
    ServerEvent,
    ServiceIdentity,
    SessionHistoryEntry,
    SessionState,
    SessionSummary,
    TaskCancelled,
    TaskStep,
    TaskSummary,
    WorkflowDefinitionView,
    WorkflowPolicyName,
    WorkflowRunState,
    WorkflowRunSummary,
    WorkflowSummary,
} from './types.js';

/** A failed response turned into an exception; `code` is the stable
 * mirage.ipc domain code from the wire error object. */
export class IpcRequestError extends Error {
    constructor(
        readonly code: string,
        message: string,
    ) {
        super(message);
        this.name = 'IpcRequestError';
    }
}

/** The peer closed the connection or the transport cannot reach it. */
export class TransportClosedError extends Error {
    constructor(message: string) {
        super(message);
        this.name = 'TransportClosedError';
    }
}

export interface SubmitTaskInput {
    goal: string;
    steps: TaskStep[];
    step_timeout_ms?: number;
    /** Optional DEC-021 session binding; absent lands the primary session
     * and the receipt echoes the resolved owner. */
    session_id?: string;
}

/** session.history request (DEC-021): `limit` defaults to the service's 50
 * and is clamped server-side; entries are the newest window in conversation
 * order with `truncated` marking earlier entries. */
export interface SessionHistoryInput {
    session_id: string;
    limit?: number;
}

/** workflow.run request body (DEC-023): `digest` defaults to the service
 * registry's head, `parameters` binds {"$param"} references, `policy` must
 * be a closed-vocabulary name. */
export interface WorkflowStartInput {
    workflow_id: string;
    digest?: string;
    parameters?: Record<string, unknown>;
    policy?: WorkflowPolicyName;
}

/** desktop.observe request body (DEC-026): `semantic` defaults on, `visual`
 * off (DEC-016 — the visual surface stays dark unless asked for). */
export interface DesktopObserveInput {
    semantic?: boolean;
    visual?: boolean;
}

/** A definition submitted to workflow.save / workflow.publish: an IR v1
 * document as produced by the pinned parser (schema_version {major,minor},
 * workflow_id, steps with closed-vocabulary kinds, ...). Byte budget
 * 256 KiB; the service re-validates strictly (fail closed). */
export type WorkflowDefinition = Record<string, unknown>;

export type EventListener = (event: ServerEvent) => void;

export interface MirageTransport {
    /** Human-visible transport label for the UI ("Mock", "Dev bridge", ...). */
    readonly label: string;

    /** True when hello advertised the DEC-012 events capability; false means
     * subscribe() will fail and the UI must fall back to task.inspect
     * polling (M1.5-05). */
    readonly eventsSupported: boolean;

    /** True when hello advertised the DEC-023 workflow face; false means the
     * workflow.* methods will fail and the UI must not present the workflow
     * surface as live data. */
    readonly workflowsSupported: boolean;

    /** True when hello advertised the DEC-021 session face; false means the
     * session.* methods will fail and the UI must present the session page
     * as unavailable (DEC-025: never fake it with local state). */
    readonly sessionsSupported: boolean;

    /** True when hello advertised the DEC-026 observation face; false means
     * desktop.observe will fail and the UI must not present the observation
     * console's desktop-state panel as live data. */
    readonly observationSupported: boolean;

    /** True when hello advertised the DEC-027 dialog face (a model layer is
     * configured); false means session.chat will fail and the Composer's
     * dialog mode stays disabled (DEC-025 honest degradation). */
    readonly chatSupported: boolean;

    hello(): Promise<ServiceIdentity>;
    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }>;
    listTasks(): Promise<TaskSummary[]>;
    inspectTask(taskId: string): Promise<InspectTask>;
    cancelTask(taskId: string): Promise<TaskCancelled>;
    shutdown(): Promise<void>;

    // -- session face (DEC-021, consumed since DEC-025/M5-06) ------------------

    /** Registered sessions as projected by the service registry (the primary
     * session included); snapshot fact source, `session.updated` is the
     * notification. */
    listSessions(): Promise<SessionSummary[]>;
    /** Opens a new session; rejects with IpcRequestError('unavailable') when
     * the registry is at capacity (DEC-021). */
    openSession(): Promise<{ session_id: string }>;
    /** Session management face (DEC-026 backlog item 2): closes the session
     * and removes its registry entry — the pinned close cancels the
     * session's non-terminal tasks. Rejects with IpcRequestError:
     * 'invalid_state' for the primary session (task.submit's default
     * binding), 'not_found' for unknown or already-removed ids. `state` is
     * the post-close pinned state ("closed" when the view is readable). */
    closeSession(sessionId: string): Promise<{ session_id: string; state: SessionState }>;
    /** The conversation projection's resync snapshot: newest window of
     * user/outcome entries in conversation order. */
    sessionHistory(input: SessionHistoryInput): Promise<{
        session_id: string;
        entries: SessionHistoryEntry[];
        truncated: boolean;
    }>;
    /** Dialog face (DEC-027): submits one dialog turn; the ack carries the
     * turn id and the settled reply/error rides the session.chat_updated
     * event stream. Rejects with IpcRequestError('unavailable') when no
     * model layer is configured, 'invalid_state' while a turn is in flight
     * for the session, 'not_found' for unknown sessions. */
    sessionChat(sessionId: string, text: string): Promise<{ turn_id: string }>;
    /** The dialog thread's resync snapshot (newest window, DEC-027). */
    sessionChatHistory(
        sessionId: string,
        limit?: number,
    ): Promise<{
        session_id: string;
        turns: ChatTurnEntry[];
        truncated: boolean;
    }>;

    // -- workflow face (DEC-023) ----------------------------------------------

    /** Saved definitions as projected by the service registry (summaries;
     * the wire has no definition read face — editors keep their working
     * copy in-session). */
    listWorkflows(): Promise<WorkflowSummary[]>;
    /** Appends a NotValidated draft version (parseable, not runnable — W-04). */
    saveWorkflow(definition: WorkflowDefinition): Promise<{ workflow_id: string; digest: string }>;
    /** Publishes through the DryRun gate; content-addressed and idempotent
     * (the caller always submits the full current definition — W-03). */
    publishWorkflow(
        definition: WorkflowDefinition,
    ): Promise<{ workflow_id: string; digest: string; dry_run_id: string; idempotent: boolean }>;
    /** Removes the product catalog entry (append-only version history is
     * untouched); rejected with invalid_state while non-terminal runs exist. */
    deleteWorkflow(workflowId: string): Promise<{ workflow_id: string }>;
    /** The pinned BuiltIn registry's exposed view (DEC-024): what the bound
     * environment can actually deliver, never a static claim. */
    workflowAtomCatalog(): Promise<ExposedTool[]>;
    listWorkflowRuns(): Promise<WorkflowRunSummary[]>;
    startWorkflowRun(input: WorkflowStartInput): Promise<{ run_id: string }>;
    /** Pinned cancel_run is idempotent; `state` is the receipt view. */
    cancelWorkflowRun(runId: string): Promise<{ run_id: string; state: WorkflowRunState }>;
    /** The head definition content (DEC-026) — the cross-session editing
     * face; rejects with IpcRequestError('not_found') for unknown ids. */
    getWorkflow(workflowId: string): Promise<WorkflowDefinitionView>;

    // -- observation face (DEC-026) --------------------------------------------

    /** One on-demand desktop observation (point-in-time capture, no event
     * form); rejects with IpcRequestError('unavailable') when a requested
     * component cannot be delivered (no environment bound, no visual
     * registry/generation, capture failure). */
    desktopObserve(input?: DesktopObserveInput): Promise<ObservationView>;

    /** Subscribes this connection to the event stream. Rejects with
     * IpcRequestError('unsupported') when the peer has no event surface. */
    subscribe(listener: EventListener): Promise<void>;

    /** Drops the connection-scoped subscription (idempotent). */
    unsubscribe(): Promise<void>;

    /** Registers a callback fired once per unexpected connection loss after a
     * successful hello (never for close()). Optional: only real transports
     * can lose their peer; the mock never fires it. Reconnection and resync
     * are UI-layer decisions (M1.5-05). */
    onConnectionLost?(listener: () => void): void;

    /** Releases the transport; further requests reject with
     * TransportClosedError. Idempotent. */
    close(): Promise<void>;
}

/** Normalizes a decoded response envelope into its payload or an
 * IpcRequestError, so transports stay thin. */
export function unwrapResponse(response: ResponseEnvelop): ResponsePayload {
    if (!response.ok) {
        throw new IpcRequestError(response.error.code, response.error.message);
    }
    return response.payload;
}
