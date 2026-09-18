/// Transport abstraction between the UI and a Runtime Service peer. The mock
/// transport (M1.5-04) and the future real transports (dev bridge WebSocket,
/// M1.5-03/M1.5-05) implement the same surface, so views never know which
/// one is behind them (DEC-006: the IPC contract is the only coupling face).

import type {
    InspectTask,
    ResponseEnvelop,
    ResponsePayload,
    ServerEvent,
    ServiceIdentity,
    TaskCancelled,
    TaskStep,
    TaskSummary,
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
}

export type EventListener = (event: ServerEvent) => void;

export interface MirageTransport {
    /** Human-visible transport label for the UI ("Mock", "Dev bridge", ...). */
    readonly label: string;

    /** True when hello advertised the DEC-012 events capability; false means
     * subscribe() will fail and the UI must fall back to task.inspect
     * polling (M1.5-05). */
    readonly eventsSupported: boolean;

    hello(): Promise<ServiceIdentity>;
    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }>;
    listTasks(): Promise<TaskSummary[]>;
    inspectTask(taskId: string): Promise<InspectTask>;
    cancelTask(taskId: string): Promise<TaskCancelled>;
    shutdown(): Promise<void>;

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
