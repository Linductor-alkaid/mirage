/// Real transport over the M1.5-03 dev bridge (M1.5-05): one WebSocket binary
/// message carries exactly one protocol frame (4-byte LE length prefix plus
/// the JSON payload); the bridge relays payloads byte-for-byte, so this side
/// reuses the shared framing rules (`makeFrame` / `tryExtractFrame`, see the
/// non-normative dev-time mapping in mirage-ipc-protocol-v1.md §1 from
/// DEC-012 decision 6). Implements the same `MirageTransport` surface as the
/// mock so the UI stays transport-blind (DEC-006: the IPC contract is the
/// only coupling face).
///
/// DEC-007 discipline: at most one outstanding request per connection — a
/// request is written only after the previous one settled (response, error,
/// or explicit timeout), and events may interleave freely between requests.
/// Every request is bounded by `requestTimeoutMs` so a stalled peer unblocks
/// the queue with an explicit failure instead of wedging the connection.

import { classifyFrame, decodeEvent, decodeResponse, encodeRequest } from './codec.js';
import { decodeUtf8, makeFrame, tryExtractFrame } from './framing.js';
import type {
    EventListener,
    MirageTransport,
    SubmitTaskInput,
} from './transport.js';
import { IpcRequestError, TransportClosedError } from './transport.js';
import type {
    InspectTask,
    RequestBody,
    ResponsePayload,
    ServiceIdentity,
    TaskProgress,
    TaskSummary,
} from './types.js';

/** A request that never settled within `requestTimeoutMs`. Unlike
 * TransportClosedError the connection may still be alive; the late response
 * is dropped when it arrives. */
export class TransportTimeoutError extends Error {
    constructor(message: string) {
        super(message);
        this.name = 'TransportTimeoutError';
    }
}

/** Structural subset of the browser WebSocket this transport drives; tests
 * inject a scripted fake through `socketFactory` instead of the global. */
export interface BridgeSocket {
    binaryType: 'blob' | 'arraybuffer';
    send(data: Uint8Array): void;
    close(code?: number, reason?: string): void;
    onopen: ((event: unknown) => void) | null;
    onmessage: ((event: { data: unknown }) => void) | null;
    onerror: ((event: unknown) => void) | null;
    onclose: ((event: unknown) => void) | null;
}

export interface WsTransportOptions {
    /** Socket factory override (defaults to the global WebSocket). */
    socketFactory?: (url: string) => BridgeSocket;
    /** Per-request bound in milliseconds (default 30 000). A request still
     * unacknowledged after this rejects with TransportTimeoutError and
     * releases the single-outstanding queue. */
    requestTimeoutMs?: number;
}

interface PendingEntry {
    expectedKind: ResponsePayload['kind'] | null;
    resolve: (payload: ResponsePayload) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
    /** Invoked when the response frame is routed, before the caller's await
     * resumes; hello() marks the connection as handshaked here so a drop
     * after hello is always reported as a connection loss. */
    onRouted?: () => void;
}

function concatBytes(a: Uint8Array, b: Uint8Array): Uint8Array {
    const out = new Uint8Array(a.byteLength + b.byteLength);
    out.set(a, 0);
    out.set(b, a.byteLength);
    return out;
}

export class WsBridgeTransport implements MirageTransport {
    readonly label = 'Dev bridge';

    private identity: ServiceIdentity | null = null;
    private handshaked = false;
    private state: 'idle' | 'connecting' | 'ready' | 'closed' = 'idle';
    private socket: BridgeSocket | null = null;
    private opening: Promise<void> | null = null;
    private openingReject: ((err: Error) => void) | null = null;
    private readonly buffer: { bytes: Uint8Array } = { bytes: new Uint8Array(0) };
    private readonly pending = new Map<number, PendingEntry>();
    private queueTail: Promise<void> = Promise.resolve();
    private nextId = 1;
    private listener: EventListener | null = null;
    private readonly lostListeners = new Set<() => void>();

    constructor(
        private readonly url: string,
        private readonly options: WsTransportOptions = {},
    ) {}

    get eventsSupported(): boolean {
        return this.identity?.events === true;
    }

    /** Registers a callback fired once per unexpected connection loss after
     * a successful hello (never for close()). Reconnection and resync are
     * UI-layer decisions (M1.5-05). */
    onConnectionLost(listener: () => void): void {
        this.lostListeners.add(listener);
    }

    // -- MirageTransport -----------------------------------------------------

    async hello(): Promise<ServiceIdentity> {
        const payload = await this.request({ op: 'hello' }, 'identity', () => {
            this.handshaked = true;
        });
        const identity = (payload as { kind: 'identity'; value: ServiceIdentity }).value;
        this.identity = identity;
        return identity;
    }

    async submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        const payload = await this.request(
            {
                op: 'task.submit',
                goal: request.goal,
                steps: request.steps.map((step) => ({ op: step.op, arg: step.arg })),
                ...(request.step_timeout_ms !== undefined
                    ? { step_timeout_ms: request.step_timeout_ms }
                    : {}),
            },
            'submitted',
        );
        return (payload as { kind: 'submitted'; value: { task_id: string } }).value;
    }

    async listTasks(): Promise<TaskSummary[]> {
        const payload = await this.request({ op: 'task.list' }, 'list');
        return (payload as { kind: 'list'; value: { tasks: TaskSummary[] } }).value.tasks;
    }

    async inspectTask(taskId: string): Promise<InspectTask> {
        const payload = await this.request({ op: 'task.inspect', task_id: taskId }, 'inspect');
        return (payload as { kind: 'inspect'; value: InspectTask }).value;
    }

    async cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        const payload = await this.request({ op: 'task.cancel', task_id: taskId }, 'cancelled');
        return (payload as { kind: 'cancelled'; value: { task_id: string; progress: TaskProgress } })
            .value;
    }

    async shutdown(): Promise<void> {
        await this.request({ op: 'service.shutdown' }, null);
    }

    async subscribe(listener: EventListener): Promise<void> {
        await this.request({ op: 'events.subscribe' }, null, () => {
            this.listener = listener;
        });
    }

    async unsubscribe(): Promise<void> {
        if (this.listener === null) {
            return;
        }
        await this.request({ op: 'events.unsubscribe' }, null, () => {
            this.listener = null;
        });
    }

    async close(): Promise<void> {
        if (this.state === 'closed') {
            return;
        }
        this.state = 'closed';
        const socket = this.socket;
        this.detachSocket();
        const err = new TransportClosedError('transport closed by client');
        if (this.openingReject !== null) {
            this.openingReject(err);
            this.openingReject = null;
        }
        this.failPending(err);
        if (socket !== null) {
            try {
                socket.close(1000, 'client close');
            } catch {
                // Already closing/closed; nothing to wait for.
            }
        }
    }

    // -- connection lifecycle -------------------------------------------------

    /** Opens the socket once; every request chains behind this. */
    private ensureConnection(): Promise<void> {
        if (this.state === 'closed') {
            return Promise.reject(new TransportClosedError('transport is closed'));
        }
        if (this.opening === null) {
            this.state = 'connecting';
            this.opening = new Promise<void>((resolve, reject) => {
                this.openingReject = reject;
                let socket: BridgeSocket;
                try {
                    socket = this.socketFactory()(this.url);
                } catch (err) {
                    this.state = 'closed';
                    reject(new TransportClosedError(`cannot open WebSocket: ${err instanceof Error ? err.message : String(err)}`));
                    return;
                }
                socket.binaryType = 'arraybuffer';
                this.socket = socket;
                socket.onopen = () => {
                    if (this.state === 'connecting') {
                        this.state = 'ready';
                        this.openingReject = null;
                        resolve();
                    }
                };
                socket.onmessage = (event) => {
                    this.handleMessage(event.data);
                };
                socket.onerror = () => {
                    // The close event follows; surface one explicit error there.
                };
                socket.onclose = () => {
                    this.handleSocketClosed();
                };
            });
        }
        return this.opening;
    }

    private socketFactory(): (url: string) => BridgeSocket {
        return (
            this.options.socketFactory ??
            ((url: string) => new WebSocket(url) as unknown as BridgeSocket)
        );
    }

    private handleSocketClosed(): void {
        if (this.state === 'closed') {
            return;
        }
        const err = new TransportClosedError(
            this.handshaked ? 'connection lost' : 'connection closed before hello completed',
        );
        this.failConnection(err);
    }

    /** Tears the connection down after it was established or while connecting:
     * fails every pending request, closes the underlying socket (the frame
     * rules make a detected violation or loss terminal — mirage-ipc-protocol-v1
     * §1: a protocol error must close the connection), marks the transport
     * closed and reports the loss to registered listeners (only after a
     * successful hello). */
    private failConnection(err: TransportClosedError): void {
        if (this.state === 'closed') {
            return;
        }
        const wasHandshaked = this.handshaked;
        this.state = 'closed';
        const socket = this.socket;
        this.detachSocket();
        if (socket !== null) {
            try {
                socket.close(1002, 'protocol error');
            } catch {
                // Already closing/closed.
            }
        }
        if (this.openingReject !== null) {
            this.openingReject(err);
            this.openingReject = null;
        }
        this.failPending(err);
        if (wasHandshaked) {
            for (const listener of this.lostListeners) {
                listener();
            }
        }
    }

    private failPending(err: TransportClosedError): void {
        const entries = [...this.pending.values()];
        this.pending.clear();
        for (const entry of entries) {
            clearTimeout(entry.timer);
            entry.reject(err);
        }
    }

    private detachSocket(): void {
        const socket = this.socket;
        this.socket = null;
        if (socket !== null) {
            socket.onopen = null;
            socket.onmessage = null;
            socket.onerror = null;
            socket.onclose = null;
        }
    }

    // -- request path ---------------------------------------------------------

    private request(
        body: RequestBody,
        expectedKind: ResponsePayload['kind'] | null,
        onRouted?: () => void,
    ): Promise<ResponsePayload> {
        return this.ensureConnection().then(
            () =>
                new Promise<ResponsePayload>((resolve, reject) => {
                    if (this.state !== 'ready' || this.socket === null) {
                        reject(new TransportClosedError('connection is not open'));
                        return;
                    }
                    const id = this.nextId;
                    this.nextId += 1;
                    const entry: PendingEntry = {
                        expectedKind,
                        resolve,
                        reject,
                        timer: setTimeout(() => {
                            if (this.pending.get(id) !== entry) {
                                return;
                            }
                            this.pending.delete(id);
                            entry.reject(
                                new TransportTimeoutError(
                                    `request '${body.op}' timed out after ${this.requestTimeoutMs()} ms`,
                                ),
                            );
                        }, this.requestTimeoutMs()),
                        onRouted,
                    };
                    this.pending.set(id, entry);
                    // Single-outstanding queue (DEC-007): write the frame only
                    // after the previous request fully settled.
                    const settled = this.responseDone(entry);
                    this.queueTail = this.queueTail.then(() => {
                        if (this.state !== 'ready' || this.socket === null) {
                            return;
                        }
                        try {
                            this.socket.send(makeFrame(encodeRequest(id, body)));
                        } catch (err) {
                            this.failConnection(
                                new TransportClosedError(`frame send failed: ${err instanceof Error ? err.message : String(err)}`),
                            );
                        }
                        return settled;
                    });
                }),
        );
    }

    /** Promise that settles when `entry` settles; rides the queue so the next
     * request waits for this response (or its timeout/failure). */
    private responseDone(entry: PendingEntry): Promise<void> {
        return new Promise<void>((resolve) => {
            const originalResolve = entry.resolve;
            const originalReject = entry.reject;
            entry.resolve = (payload) => {
                resolve();
                originalResolve(payload);
            };
            entry.reject = (err) => {
                resolve();
                originalReject(err);
            };
        });
    }

    private requestTimeoutMs(): number {
        return this.options.requestTimeoutMs ?? 30_000;
    }

    // -- receive path -----------------------------------------------------------

    private handleMessage(data: unknown): void {
        let bytes: Uint8Array;
        if (typeof data === 'string') {
            this.failConnection(new TransportClosedError('text WebSocket message violates the dev bridge frame mapping'));
            return;
        } else if (data instanceof ArrayBuffer) {
            bytes = new Uint8Array(data);
        } else if (data instanceof Uint8Array) {
            bytes = data;
        } else {
            this.failConnection(new TransportClosedError('unsupported WebSocket message payload type'));
            return;
        }
        this.buffer.bytes = concatBytes(this.buffer.bytes, bytes);
        for (;;) {
            const extract = tryExtractFrame(this.buffer.bytes);
            if (extract.status === 'need-more-data') {
                return;
            }
            if (extract.status === 'protocol-error') {
                this.failConnection(new TransportClosedError(`protocol error: ${extract.reason}`));
                return;
            }
            this.buffer.bytes = extract.rest;
            this.handlePayload(decodeUtf8(extract.message));
            if (this.state === 'closed') {
                return;
            }
        }
    }

    private handlePayload(payload: string): void {
        const kind = classifyFrame(payload);
        if (kind === 'event') {
            const decoded = decodeEvent(payload);
            if (!decoded.ok) {
                this.failConnection(new TransportClosedError(`malformed event frame: ${decoded.error}`));
                return;
            }
            this.listener?.(decoded.event);
            return;
        }
        if (kind !== 'response') {
            this.failConnection(new TransportClosedError(`unexpected frame kind '${kind}' from the service`));
            return;
        }
        const decoded = decodeResponse(payload);
        if (!decoded.ok) {
            this.failConnection(new TransportClosedError(`malformed response frame: ${decoded.error}`));
            return;
        }
        const response = decoded.response;
        const entry = this.pending.get(response.id);
        if (entry === undefined) {
            // Late response to an already timed-out request, or an unknown id:
            // dropped without failing the connection.
            return;
        }
        this.pending.delete(response.id);
        clearTimeout(entry.timer);
        entry.onRouted?.();
        if (!response.ok) {
            entry.reject(new IpcRequestError(response.error.code, response.error.message));
            return;
        }
        if (entry.expectedKind !== null && response.payload.kind !== entry.expectedKind) {
            entry.reject(
                new TransportClosedError(
                    `response kind mismatch: expected '${entry.expectedKind}', got '${response.payload.kind}'`,
                ),
            );
            this.failConnection(new TransportClosedError('service sent a response payload that does not match the request'));
            return;
        }
        entry.resolve(response.payload);
    }
}
