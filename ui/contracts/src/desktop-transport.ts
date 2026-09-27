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
import type {
    EventListener,
    MirageTransport,
    SessionHistoryInput,
    SubmitTaskInput,
    WorkflowDefinition,
    WorkflowStartInput,
} from './transport.js';
import { IpcRequestError, TransportClosedError } from './transport.js';
import type {
    ExposedTool,
    InspectTask,
    RequestBody,
    ResponsePayload,
    ServiceIdentity,
    SessionHistoryEntry,
    SessionSummary,
    TaskProgress,
    TaskSummary,
    WorkflowRunState,
    WorkflowRunSummary,
    WorkflowSummary,
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

    get workflowsSupported(): boolean {
        return this.identity?.workflows === true;
    }

    get sessionsSupported(): boolean {
        return this.identity?.sessions === true;
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
            ...(request.session_id !== undefined ? { session_id: request.session_id } : {}),
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

    // -- session face (DEC-021, consumed since DEC-025/M5-06) ------------------

    async listSessions(): Promise<SessionSummary[]> {
        const payload = await this.request({ op: 'session.list' });
        return (payload as { kind: 'session-list'; value: { sessions: SessionSummary[] } }).value.sessions;
    }

    async openSession(): Promise<{ session_id: string }> {
        const payload = await this.request({ op: 'session.open' });
        return (payload as { kind: 'session-opened'; value: { session_id: string } }).value;
    }

    async sessionHistory(input: SessionHistoryInput): Promise<{
        session_id: string;
        entries: SessionHistoryEntry[];
        truncated: boolean;
    }> {
        const payload = await this.request({
            op: 'session.history',
            session_id: input.session_id,
            ...(input.limit !== undefined ? { limit: input.limit } : {}),
        });
        return (payload as {
            kind: 'session-history';
            value: { session_id: string; entries: SessionHistoryEntry[]; truncated: boolean };
        }).value;
    }

    // -- workflow face (DEC-023) ----------------------------------------------

    async listWorkflows(): Promise<WorkflowSummary[]> {
        const payload = await this.request({ op: 'workflow.list' });
        return (payload as { kind: 'workflow-list'; value: { workflows: WorkflowSummary[] } }).value.workflows;
    }

    async saveWorkflow(
        definition: WorkflowDefinition,
    ): Promise<{ workflow_id: string; digest: string }> {
        const payload = await this.request({ op: 'workflow.save', definition });
        return (payload as { kind: 'workflow-saved'; value: { workflow_id: string; digest: string } }).value;
    }

    async publishWorkflow(definition: WorkflowDefinition): Promise<{
        workflow_id: string;
        digest: string;
        dry_run_id: string;
        idempotent: boolean;
    }> {
        const payload = await this.request({ op: 'workflow.publish', definition });
        return (payload as {
            kind: 'workflow-published';
            value: { workflow_id: string; digest: string; dry_run_id: string; idempotent: boolean };
        }).value;
    }

    async deleteWorkflow(workflowId: string): Promise<{ workflow_id: string }> {
        const payload = await this.request({ op: 'workflow.delete', workflow_id: workflowId });
        return (payload as { kind: 'workflow-deleted'; value: { workflow_id: string } }).value;
    }

    async workflowAtomCatalog(): Promise<ExposedTool[]> {
        const payload = await this.request({ op: 'workflow.atom.catalog' });
        return (payload as { kind: 'workflow-atom-catalog'; value: { tools: ExposedTool[] } }).value.tools;
    }

    async listWorkflowRuns(): Promise<WorkflowRunSummary[]> {
        const payload = await this.request({ op: 'workflow.runs' });
        return (payload as { kind: 'workflow-run-list'; value: { runs: WorkflowRunSummary[] } }).value.runs;
    }

    async startWorkflowRun(input: WorkflowStartInput): Promise<{ run_id: string }> {
        const payload = await this.request({
            op: 'workflow.run',
            workflow_id: input.workflow_id,
            ...(input.digest !== undefined ? { digest: input.digest } : {}),
            ...(input.parameters !== undefined ? { parameters: input.parameters } : {}),
            ...(input.policy !== undefined ? { policy: input.policy } : {}),
        });
        return (payload as { kind: 'workflow-run-started'; value: { run_id: string } }).value;
    }

    async cancelWorkflowRun(runId: string): Promise<{ run_id: string; state: WorkflowRunState }> {
        const payload = await this.request({ op: 'workflow.cancel', run_id: runId });
        return (payload as { kind: 'workflow-run-cancelled'; value: { run_id: string; state: WorkflowRunState } })
            .value;
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
