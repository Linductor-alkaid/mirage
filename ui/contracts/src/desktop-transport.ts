/// Real transport inside the CEF desktop shell (M5-02): the renderer talks to
/// the browser process through the controlled CEF message-router bridge
/// (`window.mirageQuery`), and the browser process relays protocol-v1
/// envelopes over Local IPC to mirage-service. One query carries exactly one
/// request envelope and answers with exactly one response envelope (the
/// bridge restores the renderer's correlation id); service events arrive
/// through the `__mirageOnEvent` hook and session loss through
/// `__mirageConnectionLost`. Implements the same `MirageTransport` surface as
/// the dev bridge and the mock, so the UI stays transport-blind (DEC-006: the
/// IPC contract is the only coupling face — this file adds no new protocol).

import { decodeEvent, decodeResponse, encodeRequest } from './codec.js';
import type { EventListener, MirageTransport, SubmitTaskInput } from './transport.js';
import { IpcRequestError, TransportClosedError } from './transport.js';
import type {
    InspectTask,
    RequestBody,
    ResponsePayload,
    ServiceIdentity,
    TaskProgress,
    TaskSummary,
} from './types.js';

/** A request that never settled within the bridge timeout. */
export class DesktopBridgeTimeoutError extends Error {
    constructor(message: string) {
        super(message);
        this.name = 'TransportTimeoutError';
    }
}

/** The CefMessageRouter query argument: one request envelope plus the
 * settlement callbacks (the CEF query function takes a single object). */
export interface DesktopBridgeQueryArgs {
    request: string;
    persistent: boolean;
    onSuccess: (response: string) => void;
    onFailure: (errorCode: number, errorMessage: string) => void;
}

/** The controlled bridge surface the shell injects into the renderer. The
 * hooks are installed by this transport; `mirageQuery` is the CefMessageRouter
 * query function and `mirageQueryCancel` cancels a pending query by id. */
export interface DesktopBridgeGlobal {
    mirageQuery(query: DesktopBridgeQueryArgs): number;
    mirageQueryCancel(queryId: number): void;
}

/** The one bridge function the transport drives. */
export type DesktopBridgeQuery = DesktopBridgeGlobal['mirageQuery'];

declare global {
    interface Window {
        mirageQuery?: DesktopBridgeGlobal['mirageQuery'];
        __mirageOnEvent?: (eventJson: string) => void;
        __mirageConnectionLost?: () => void;
    }
}

/** True when this renderer runs inside the Mirage shell (the bridge global
 * is injected before any page script runs). */
export function desktopBridgeAvailable(): boolean {
    return typeof window !== 'undefined' && typeof window.mirageQuery === 'function';
}

function bridgeFromWindow(): DesktopBridgeQuery {
    if (typeof window === 'undefined' || typeof window.mirageQuery !== 'function') {
        throw new TransportClosedError('desktop bridge is not available outside the Mirage shell');
    }
    return window.mirageQuery;
}

interface PendingEntry {
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
}

export class DesktopBridgeTransport implements MirageTransport {
    readonly label = 'Desktop shell';

    private identity: ServiceIdentity | null = null;
    private closed = false;
    private nextId = 1;
    private listener: EventListener | null = null;
    private readonly pending = new Map<number, PendingEntry>();
    private readonly lostListeners = new Set<() => void>();
    private hooksInstalled = false;

    constructor(
        private readonly bridge: DesktopBridgeQuery = bridgeFromWindow(),
        private readonly requestTimeoutMs = 30_000,
    ) {
        // The session-loss hook is process-wide; the transport owns it while
        // it is the live one and removes it on close() so a reconnect factory
        // never leaves a stale hook behind.
        if (typeof window !== 'undefined') {
            window.__mirageConnectionLost = () => {
                for (const listener of this.lostListeners) {
                    listener();
                }
            };
            this.hooksInstalled = true;
        }
    }

    get eventsSupported(): boolean {
        return this.identity?.events === true;
    }

    onConnectionLost(listener: () => void): void {
        this.lostListeners.add(listener);
    }

    // -- MirageTransport -----------------------------------------------------

    async hello(): Promise<ServiceIdentity> {
        const payload = await this.request({ op: 'hello' });
        const identity = (payload as { kind: 'identity'; value: ServiceIdentity }).value;
        this.identity = identity;
        return identity;
    }

    async submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        const payload = await this.request({
            op: 'task.submit',
            goal: request.goal,
            steps: request.steps.map((step) => ({ op: step.op, arg: step.arg })),
            ...(request.step_timeout_ms !== undefined
                ? { step_timeout_ms: request.step_timeout_ms }
                : {}),
        });
        return (payload as { kind: 'submitted'; value: { task_id: string } }).value;
    }

    async listTasks(): Promise<TaskSummary[]> {
        const payload = await this.request({ op: 'task.list' });
        return (payload as { kind: 'list'; value: { tasks: TaskSummary[] } }).value.tasks;
    }

    async inspectTask(taskId: string): Promise<InspectTask> {
        const payload = await this.request({ op: 'task.inspect', task_id: taskId });
        return (payload as { kind: 'inspect'; value: InspectTask }).value;
    }

    async cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        const payload = await this.request({ op: 'task.cancel', task_id: taskId });
        return (payload as { kind: 'cancelled'; value: { task_id: string; progress: TaskProgress } })
            .value;
    }

    async shutdown(): Promise<void> {
        await this.request({ op: 'service.shutdown' });
    }

    async subscribe(listener: EventListener): Promise<void> {
        await this.request({ op: 'events.subscribe' }, () => {
            if (typeof window !== 'undefined') {
                window.__mirageOnEvent = (eventJson: string) => {
                    const decoded = decodeEvent(eventJson);
                    if (decoded.ok) {
                        this.listener?.(decoded.event);
                    }
                };
            }
            this.listener = listener;
        });
    }

    async unsubscribe(): Promise<void> {
        if (this.listener === null) {
            return;
        }
        await this.request({ op: 'events.unsubscribe' }, () => {
            this.listener = null;
            if (typeof window !== 'undefined') {
                delete window.__mirageOnEvent;
            }
        });
    }

    async close(): Promise<void> {
        if (this.closed) {
            return;
        }
        this.closed = true;
        this.failPending(new TransportClosedError('transport closed by client'));
        if (this.hooksInstalled && typeof window !== 'undefined') {
            delete window.__mirageConnectionLost;
            this.hooksInstalled = false;
        }
    }

    // -- request path ---------------------------------------------------------

    /** One bridge query carries one envelope and resolves with the matched
     * response payload. The shell serializes the Local IPC wire itself (the
     * DEC-012 single-outstanding discipline lives in the session client), so
     * concurrent queries queue in the shell instead of here. */
    private request(body: RequestBody, onRouted?: () => void): Promise<ResponsePayload> {
        if (this.closed) {
            return Promise.reject(new TransportClosedError('transport is closed'));
        }
        const id = this.nextId;
        this.nextId += 1;
        const envelope = encodeRequest(id, body);
        return new Promise<ResponsePayload>((resolve, reject) => {
            const entry: PendingEntry = {
                reject,
                timer: setTimeout(() => {
                    if (this.pending.get(id) !== entry) {
                        return;
                    }
                    this.pending.delete(id);
                    reject(
                        new DesktopBridgeTimeoutError(
                            `request '${body.op}' timed out after ${this.requestTimeoutMs} ms`,
                        ),
                    );
                }, this.requestTimeoutMs),
            };
            this.pending.set(id, entry);
            const settle = (run: () => void): void => {
                if (this.pending.get(id) !== entry) {
                    return; // late answer to a timed-out query: dropped
                }
                clearTimeout(entry.timer);
                this.pending.delete(id);
                run();
            };
            try {
                this.bridge({
                    request: envelope,
                    persistent: false,
                    onSuccess: (responseJson) =>
                        settle(() => {
                            const decoded = decodeResponse(responseJson);
                            if (!decoded.ok) {
                                reject(new TransportClosedError(`malformed response envelope: ${decoded.error}`));
                                return;
                            }
                            if (decoded.response.id !== id) {
                                reject(new TransportClosedError('response id does not echo the request'));
                                return;
                            }
                            if (!decoded.response.ok) {
                                reject(
                                    new IpcRequestError(
                                        decoded.response.error.code,
                                        decoded.response.error.message,
                                    ),
                                );
                                return;
                            }
                            onRouted?.();
                            resolve(decoded.response.payload);
                        }),
                    onFailure: (errorCode, errorMessage) =>
                        settle(() => {
                            reject(new TransportClosedError(`bridge failure ${errorCode}: ${errorMessage}`));
                        }),
                });
            } catch (err) {
                settle(() => {
                    reject(
                        new TransportClosedError(
                            `bridge query failed: ${err instanceof Error ? err.message : String(err)}`,
                        ),
                    );
                });
            }
        });
    }

    private failPending(err: TransportClosedError): void {
        const entries = [...this.pending.values()];
        this.pending.clear();
        for (const entry of entries) {
            clearTimeout(entry.timer);
            entry.reject(err);
        }
    }
}
