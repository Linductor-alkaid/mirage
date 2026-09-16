/// In-memory mock of the M1 Runtime Service (M1.5-04): a scripted task state
/// machine plus a DEC-012-draft event surface, letting the UI run the full
/// submit -> step-level progress -> cancel -> terminal flow in the browser
/// with no backend. Scope discipline: this simulates observable *wire*
/// behaviour (states, stable error shapes, event seq/overflow semantics);
/// it does not reimplement service internals. The `fail:` step convention
/// below is mock-only, documented, and never produced by the real service.

import { BoundedEventQueue } from '../events.js';
import type { EventListener, MirageTransport, SubmitTaskInput } from '../transport.js';
import { IpcRequestError, TransportClosedError } from '../transport.js';
import type {
    HostStatus,
    InspectTask,
    ServerEvent,
    ServiceIdentity,
    StepView,
    TaskProgress,
    TaskSummary,
} from '../types.js';
import { PROTOCOL_VERSION } from '../types.js';

export interface MockServiceOptions {
    /** Wall time per step, milliseconds (default 700; 0 advances immediately). */
    stepDurationMs?: number;
    /** starting -> running delay, milliseconds (default 500). */
    hostStartDelayMs?: number;
    /** stopping -> stopped delay on shutdown, milliseconds (default 300). */
    shutdownDelayMs?: number;
    /** Task registry capacity; submit refuses when full (default 256). */
    taskCapacity?: number;
    /** Per-subscription event queue capacity, drop-oldest (default 64). */
    eventQueueCapacity?: number;
    /** When false, hello omits the `events` capability and subscribe()
     * fails with `unsupported` — drives the UI degradation path (M1.5-05). */
    eventsCapability?: boolean;
    /** 'auto' (default) flushes queues via microtasks; 'manual' only
     * enqueues until flush() is called — the deterministic hook for
     * overflow tests. */
    flushMode?: 'auto' | 'manual';
}

/** Event frame without the per-subscription seq, which is assigned at
 * enqueue time so delivery order matches seq order. */
type DistributiveOmit<T, K extends keyof never> = T extends unknown ? Omit<T, K> : never;
type EventFrame = DistributiveOmit<ServerEvent, 'seq'>;

interface MockStep {
    kind: StepView['kind'];
    argument: string;
    status: StepView['status'];
    operationId: string;
    exitCode: number;
    result: string;
    resultTruncated: boolean;
    error: string;
}

interface MockTask {
    id: string;
    goal: string;
    steps: MockStep[];
    progress: TaskProgress;
    hasSuccess: boolean;
    success: boolean;
    cancelRequested: boolean;
    timer: ReturnType<typeof setTimeout> | null;
}

interface Subscription {
    listener: EventListener;
    queue: BoundedEventQueue<ServerEvent>;
    nextSeq: number;
    droppedSinceOverflow: number;
    flushScheduled: boolean;
}

const TERMINAL_PROGRESS: readonly TaskProgress[] = ['Completed', 'Failed', 'Cancelled'];

function isTerminal(progress: TaskProgress): boolean {
    return TERMINAL_PROGRESS.includes(progress);
}

/** Mock-only failure convention: a process.execute step whose argument
 * starts with `fail:` fails after its simulated run (exit code 1). */
function failureReason(step: MockStep): string | null {
    if (step.kind === 'process.execute' && step.argument.startsWith('fail:')) {
        return `mock step failure: ${step.argument.slice('fail:'.length).trim() || 'injected failure'}`;
    }
    return null;
}

/**
 * Semantic core of the mock: host lifecycle, task registry, scripted step
 * driver and the DEC-012-draft event bus (per-subscription seq, bounded
 * drop-oldest queues, `events.overflow` markers).
 */
export class MockMirageService {
    private hostStatus: HostStatus = 'stopped';
    private hostTimer: ReturnType<typeof setTimeout> | null = null;
    private readonly tasks = new Map<string, MockTask>();
    private readonly subscriptions = new Set<Subscription>();
    private nextTaskNumber = 1;
    private nextOperationNumber = 1;
    private closed = false;
    private readonly options: Required<
        Pick<MockServiceOptions, 'stepDurationMs' | 'hostStartDelayMs' | 'shutdownDelayMs' | 'taskCapacity' | 'eventQueueCapacity' | 'eventsCapability' | 'flushMode'>
    >;

    constructor(options: MockServiceOptions = {}) {
        this.options = {
            stepDurationMs: options.stepDurationMs ?? 700,
            hostStartDelayMs: options.hostStartDelayMs ?? 500,
            shutdownDelayMs: options.shutdownDelayMs ?? 300,
            taskCapacity: options.taskCapacity ?? 256,
            eventQueueCapacity: options.eventQueueCapacity ?? 64,
            eventsCapability: options.eventsCapability ?? true,
            flushMode: options.flushMode ?? 'auto',
        };
        this.setHostStatus('starting');
        if (this.options.hostStartDelayMs === 0) {
            this.setHostStatus('running');
        } else {
            this.hostTimer = setTimeout(() => {
                this.hostTimer = null;
                this.setHostStatus('running');
            }, this.options.hostStartDelayMs);
        }
    }

    // -- lifecycle ---------------------------------------------------------

    /** Releases the mock: further requests are refused, subscriptions are
     * dropped, in-flight timers are cancelled. Idempotent. */
    close(): void {
        this.closed = true;
        if (this.hostTimer !== null) {
            clearTimeout(this.hostTimer);
            this.hostTimer = null;
        }
        for (const task of this.tasks.values()) {
            if (task.timer !== null) {
                clearTimeout(task.timer);
                task.timer = null;
            }
        }
        this.subscriptions.clear();
    }

    private assertOpen(): void {
        if (this.closed) {
            throw new TransportClosedError('mock service is closed');
        }
    }

    // -- request surface ----------------------------------------------------

    hello(): ServiceIdentity {
        this.assertOpen();
        const identity: ServiceIdentity = {
            service: 'mirage-runtime',
            mirage_version: '0.1.0',
            mira_core_version: '0.1.0',
            host_status: this.hostStatus,
            protocol: PROTOCOL_VERSION,
        };
        if (this.options.eventsCapability) {
            identity.events = true;
        }
        return identity;
    }

    submit(request: SubmitTaskInput): { task_id: string } {
        this.assertOpen();
        if (this.hostStatus !== 'running') {
            throw new IpcRequestError(
                'invalid_state',
                `mira host is not running (status: ${this.hostStatus})`,
            );
        }
        const goal = request.goal.trim();
        if (goal.length === 0) {
            throw new IpcRequestError('invalid_argument', "task.submit requires a non-empty 'goal'");
        }
        if (this.tasks.size >= this.options.taskCapacity) {
            throw new IpcRequestError(
                'invalid_state',
                `task registry at capacity (${this.options.taskCapacity})`,
            );
        }
        const steps: MockStep[] = request.steps.map((step) => {
            if (step.arg.length === 0) {
                throw new IpcRequestError(
                    'invalid_argument',
                    "task.submit step requires a non-empty 'arg'",
                );
            }
            return {
                kind: step.op,
                argument: step.arg,
                status: 'pending',
                operationId: '',
                exitCode: -1,
                result: '',
                resultTruncated: false,
                error: '',
            };
        });
        const id = `task-${String(this.nextTaskNumber).padStart(4, '0')}`;
        this.nextTaskNumber += 1;
        const task: MockTask = {
            id,
            goal,
            steps,
            progress: 'Active',
            hasSuccess: false,
            success: false,
            cancelRequested: false,
            timer: null,
        };
        this.tasks.set(id, task);
        this.publishTaskUpdated(task);
        this.advance(task);
        return { task_id: id };
    }

    list(): TaskSummary[] {
        this.assertOpen();
        return [...this.tasks.values()].map((task) => ({
            id: task.id,
            goal: task.goal,
            progress: task.progress,
        }));
    }

    inspect(taskId: string): InspectTask {
        this.assertOpen();
        const task = this.tasks.get(taskId);
        if (task === undefined) {
            throw new IpcRequestError('not_found', `task '${taskId}' was not found`);
        }
        return {
            id: task.id,
            goal: task.goal,
            progress: task.progress,
            has_success: task.hasSuccess,
            success: task.hasSuccess ? task.success : undefined,
            steps: task.steps.map((step, index) => ({
                index,
                kind: step.kind,
                status: step.status,
                operation_id: step.operationId,
                permission: 'allowed',
                ok: step.status === 'ok',
                exit_code: step.exitCode,
                result: step.result,
                result_truncated: step.resultTruncated,
                error: step.error,
            })),
        };
    }

    cancel(taskId: string): { task_id: string; progress: TaskProgress } {
        this.assertOpen();
        const task = this.tasks.get(taskId);
        if (task === undefined) {
            throw new IpcRequestError('not_found', `task '${taskId}' was not found`);
        }
        if (isTerminal(task.progress)) {
            // Same passthrough shape as the real service: the pinned
            // rejection is surfaced verbatim instead of reviving the task.
            throw new IpcRequestError(
                'pinned_runtime',
                `invalid_state: task '${taskId}' already settled as ${task.progress}`,
            );
        }
        if (task.progress === 'Cancelling') {
            throw new IpcRequestError(
                'pinned_runtime',
                `invalid_state: task '${taskId}' is already cancelling`,
            );
        }
        task.cancelRequested = true;
        task.progress = 'Cancelling';
        this.publishTaskUpdated(task);
        if (task.timer !== null) {
            clearTimeout(task.timer);
            task.timer = null;
            this.unwindCancelled(task);
        }
        // A step boundary was reached concurrently; advance() observes the
        // flag and unwinds there.
        return { task_id: task.id, progress: task.progress };
    }

    shutdown(): void {
        this.assertOpen();
        if (this.hostStatus === 'stopped' || this.hostStatus === 'stopping') {
            return;
        }
        // Shutdown converges to stopped from any non-terminal state: a
        // pending starting timer must not resurrect 'running' afterwards.
        if (this.hostTimer !== null) {
            clearTimeout(this.hostTimer);
            this.hostTimer = null;
        }
        this.setHostStatus('stopping');
        const settle = () => this.setHostStatus('stopped');
        if (this.options.shutdownDelayMs === 0) {
            settle();
        } else {
            setTimeout(settle, this.options.shutdownDelayMs);
        }
    }

    // -- event surface ------------------------------------------------------

    subscribe(listener: EventListener): void {
        this.assertOpen();
        if (!this.options.eventsCapability) {
            throw new IpcRequestError('unsupported', 'peer did not advertise the events capability');
        }
        const subscription: Subscription = {
            listener,
            queue: new BoundedEventQueue(this.options.eventQueueCapacity),
            nextSeq: 1,
            droppedSinceOverflow: 0,
            flushScheduled: false,
        };
        this.subscriptions.add(subscription);
        // Enqueue-after-subscribe ordering: the subscription starts at the
        // current host status so a fresh client resyncs its view at once.
        this.enqueue(subscription, { v: 1, event: 'host.status', status: this.hostStatus });
    }

    unsubscribe(listener: EventListener): void {
        for (const subscription of this.subscriptions) {
            if (subscription.listener === listener) {
                this.subscriptions.delete(subscription);
            }
        }
    }

    /** Drains pending queues when flushMode is 'manual'; a no-op otherwise. */
    flush(): void {
        for (const subscription of [...this.subscriptions]) {
            this.flushSubscription(subscription);
        }
    }

    /** Publishes one frame to every subscriber. Also the deterministic
     * injection point for overflow tests. */
    publishFrame(frame: EventFrame): void {
        for (const subscription of [...this.subscriptions]) {
            this.enqueue(subscription, frame);
        }
    }

    private enqueue(subscription: Subscription, frame: EventFrame): void {
        const event = { ...frame, seq: subscription.nextSeq } as ServerEvent;
        subscription.nextSeq += 1;
        // Dropped events still consumed a seq: the client observes the gap
        // and resyncs (DEC-012 decision 4), while the exact loss total is
        // reported by the events.overflow marker at flush time.
        const dropped = subscription.queue.push(event);
        if (dropped > 0) {
            subscription.droppedSinceOverflow += dropped;
        }
        if (this.options.flushMode === 'auto' && !subscription.flushScheduled) {
            subscription.flushScheduled = true;
            queueMicrotask(() => {
                subscription.flushScheduled = false;
                this.flushSubscription(subscription);
            });
        }
    }

    private flushSubscription(subscription: Subscription): void {
        for (const event of subscription.queue.drain()) {
            subscription.listener(event);
        }
        // The marker is delivered directly (never enqueued) so it cannot
        // evict queued events and its count stays exact.
        if (subscription.droppedSinceOverflow > 0) {
            const dropped = subscription.droppedSinceOverflow;
            subscription.droppedSinceOverflow = 0;
            subscription.listener({
                v: 1,
                seq: subscription.nextSeq,
                event: 'events.overflow',
                dropped,
            });
            subscription.nextSeq += 1;
        }
    }

    // -- host + driver internals -------------------------------------------

    private setHostStatus(status: HostStatus): void {
        this.hostStatus = status;
        this.publishFrame({ v: 1, event: 'host.status', status });
    }

    private publishTaskUpdated(task: MockTask): void {
        this.publishFrame({
            v: 1,
            event: 'task.updated',
            task_id: task.id,
            goal: task.goal,
            progress: task.progress,
            has_success: task.hasSuccess,
            success: task.success,
        });
    }

    private advance(task: MockTask): void {
        if (task.cancelRequested) {
            this.unwindCancelled(task);
            return;
        }
        const running = task.steps.find((step) => step.status === 'running');
        if (running !== undefined) {
            this.completeStep(task, running);
            return;
        }
        const pending = task.steps.find((step) => step.status === 'pending');
        if (pending === undefined) {
            this.settle(task, 'Completed', true, true);
            return;
        }
        pending.status = 'running';
        pending.operationId = `op-${String(this.nextOperationNumber).padStart(4, '0')}`;
        this.nextOperationNumber += 1;
        this.publishTaskUpdated(task);
        if (this.options.stepDurationMs === 0) {
            this.advance(task);
        } else {
            task.timer = setTimeout(() => {
                task.timer = null;
                this.advance(task);
            }, this.options.stepDurationMs);
        }
    }

    private completeStep(task: MockTask, step: MockStep): void {
        const failure = failureReason(step);
        if (failure !== null) {
            step.status = 'failed';
            step.exitCode = 1;
            step.error = failure;
            this.publishTaskUpdated(task);
            this.skipRemaining(task);
            this.settle(task, 'Failed', false, false);
            return;
        }
        step.status = 'ok';
        if (step.kind === 'process.execute') {
            step.exitCode = 0;
            step.result = `mock output of '${step.argument}'`;
        } else {
            step.result = `mock content of '${step.argument}'`;
        }
        this.publishTaskUpdated(task);
        this.advance(task);
    }

    private unwindCancelled(task: MockTask): void {
        const running = task.steps.find((step) => step.status === 'running');
        if (running !== undefined) {
            running.status = 'cancelled';
        }
        this.skipRemaining(task);
        this.publishTaskUpdated(task);
        this.settle(task, 'Cancelled', false, false);
    }

    private skipRemaining(task: MockTask): void {
        for (const step of task.steps) {
            if (step.status === 'pending') {
                step.status = 'skipped';
            }
        }
    }

    private settle(task: MockTask, progress: TaskProgress, hasSuccess: boolean, success: boolean): void {
        task.progress = progress;
        task.hasSuccess = hasSuccess;
        task.success = success;
        this.publishTaskUpdated(task);
    }
}

/** Mock-backed `MirageTransport`: same surface the real transports will
 * implement, so views and stores never special-case the mock. */
export class MockTransport implements MirageTransport {
    readonly label = 'Mock';

    private listener: EventListener | null = null;

    constructor(private readonly service: MockMirageService) {}

    /** All methods reject (never throw synchronously) so callers can rely
     * on promise handling uniformly, including after close(). */
    private call<T>(fn: () => T): Promise<T> {
        try {
            return Promise.resolve(fn());
        } catch (error) {
            return Promise.reject(error);
        }
    }

    get eventsSupported(): boolean {
        return this.service.hello().events === true;
    }

    hello(): Promise<ServiceIdentity> {
        return this.call(() => this.service.hello());
    }

    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        return this.call(() => this.service.submit(request));
    }

    listTasks(): Promise<TaskSummary[]> {
        return this.call(() => this.service.list());
    }

    inspectTask(taskId: string): Promise<InspectTask> {
        return this.call(() => this.service.inspect(taskId));
    }

    cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        return this.call(() => this.service.cancel(taskId));
    }

    shutdown(): Promise<void> {
        return this.call(() => {
            this.service.shutdown();
        });
    }

    subscribe(listener: EventListener): Promise<void> {
        return this.call(() => {
            this.listener = listener;
            this.service.subscribe(listener);
        });
    }

    unsubscribe(): Promise<void> {
        return this.call(() => {
            if (this.listener !== null) {
                this.service.unsubscribe(this.listener);
                this.listener = null;
            }
        });
    }

    close(): Promise<void> {
        return this.call(() => {
            this.service.close();
        });
    }
}

export function createMockTransport(options: MockServiceOptions = {}): {
    transport: MockTransport;
    service: MockMirageService;
} {
    const service = new MockMirageService(options);
    return { transport: new MockTransport(service), service };
}
