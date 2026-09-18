// @vitest-environment jsdom
/// HarnessStore 连接面（M1.5-05）：降级（无 events 能力 / subscribe unsupported）、
/// 事件 seq 跳跃与 overflow 的 resync、断线重连（退避 + 工厂 + 重连 resync）、
/// 无工厂断线的显式 error 态、dispose 的定时器与 transport 清理。jsdom +
/// vi.useFakeTimers 控制轮询与退避；脚本化 transport 完整实现 MirageTransport
/// 表面（含可选 onConnectionLost），无真实网络。

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { IpcRequestError } from '@mirage/contracts';
import type {
    EventListener,
    InspectTask,
    MirageTransport,
    ServerEvent,
    ServiceIdentity,
    StepView,
    SubmitTaskInput,
    TaskProgress,
    TaskSummary,
} from '@mirage/contracts';

import { HarnessStore } from '../src/state/store.js';
import type { SubmitStepInput } from '../src/state/store.js';

// ---- 脚本化 transport（完整 MirageTransport 形状，promise 语义与 wire 一致） --

function makeIdentity(events: boolean): ServiceIdentity {
    const identity: ServiceIdentity = {
        service: 'mirage-runtime',
        mirage_version: '0.1.0',
        mira_core_version: '0.1.0',
        host_status: 'running',
        protocol: 1,
    };
    if (events) {
        identity.events = true;
    }
    return identity;
}

let opSeq = 0;
function stepView(partial: Partial<StepView> & Pick<StepView, 'index'>): StepView {
    opSeq += 1;
    return {
        kind: 'process.execute',
        status: 'pending',
        operation_id: `op-${opSeq}`,
        permission: 'allowed',
        ok: false,
        exit_code: -1,
        result: '',
        result_truncated: false,
        error: '',
        ...partial,
    };
}

function inspectOf(taskId: string, progress: TaskProgress, steps: StepView[]): InspectTask {
    return {
        id: taskId,
        goal: 'scripted goal',
        progress,
        has_success: progress === 'Completed',
        steps,
    };
}

function hostEvent(seq: number, status: Extract<ServerEvent, { event: 'host.status' }>['status']): ServerEvent {
    return { v: 1, seq, event: 'host.status', status };
}

class ScriptedTransport implements MirageTransport {
    readonly label = 'scripted';
    readonly eventsSupported: boolean;
    helloImpl: () => Promise<ServiceIdentity>;
    subscribeImpl: (() => Promise<void>) | null = null;
    closeImpl: (() => Promise<void>) | null = null;

    helloCount = 0;
    subscribeCount = 0;
    closeCount = 0;
    readonly inspectCalls: string[] = [];
    readonly submittedRequests: SubmitTaskInput[] = [];

    private nextTaskNumber = 1;
    private readonly inspectTable = new Map<string, InspectTask>();
    private listener: EventListener | null = null;
    private lostListener: (() => void) | null = null;

    constructor(options: { events?: boolean; subscribeUnsupported?: boolean } = {}) {
        const events = options.events ?? false;
        this.eventsSupported = events;
        this.helloImpl = () => Promise.resolve(makeIdentity(events));
        if (options.subscribeUnsupported === true) {
            this.subscribeImpl = () =>
                Promise.reject(
                    new IpcRequestError('unsupported', 'peer did not advertise the events capability'),
                );
        }
    }

    hello(): Promise<ServiceIdentity> {
        this.helloCount += 1;
        return this.helloImpl();
    }

    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        this.submittedRequests.push(request);
        const task_id = `t-${String(this.nextTaskNumber).padStart(4, '0')}`;
        this.nextTaskNumber += 1;
        this.inspectTable.set(
            task_id,
            inspectOf(
                task_id,
                'Active',
                request.steps.map((step, index) =>
                    stepView({ index, kind: step.op, status: index === 0 ? 'running' : 'pending' }),
                ),
            ),
        );
        return Promise.resolve({ task_id });
    }

    listTasks(): Promise<TaskSummary[]> {
        return Promise.resolve([]);
    }

    inspectTask(taskId: string): Promise<InspectTask> {
        this.inspectCalls.push(taskId);
        const detail = this.inspectTable.get(taskId);
        return detail !== undefined
            ? Promise.resolve(detail)
            : Promise.reject(new IpcRequestError('not_found', `no scripted task '${taskId}'`));
    }

    cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        return Promise.resolve({ task_id: taskId, progress: 'Cancelling' });
    }

    shutdown(): Promise<void> {
        return Promise.resolve();
    }

    subscribe(listener: EventListener): Promise<void> {
        this.subscribeCount += 1;
        if (this.subscribeImpl !== null) {
            return this.subscribeImpl();
        }
        this.listener = listener;
        return Promise.resolve();
    }

    unsubscribe(): Promise<void> {
        this.listener = null;
        return Promise.resolve();
    }

    onConnectionLost(listener: () => void): void {
        this.lostListener = listener;
    }

    close(): Promise<void> {
        this.closeCount += 1;
        if (this.closeImpl !== null) {
            return this.closeImpl();
        }
        return Promise.resolve();
    }

    // -- 测试驱动 ---------------------------------------------------------------

    /** 模拟 hello 成功后的意外断线（每连接恰好一次）。 */
    loseConnection(): void {
        this.lostListener?.();
    }

    emit(event: ServerEvent): void {
        this.listener?.(event);
    }

    setInspect(taskId: string, detail: InspectTask): void {
        this.inspectTable.set(taskId, detail);
    }

    get subscribedListener(): EventListener | null {
        return this.listener;
    }
}

// ---- 脚手架 --------------------------------------------------------------------

let currentStore: HarnessStore | null = null;

/** 排空 microtask 链（fake timers 不影响 Promise；多轮 await 覆盖较深的建立链）。 */
async function settle(): Promise<void> {
    for (let i = 0; i < 10; i += 1) {
        await Promise.resolve();
    }
}

async function startReady(store: HarnessStore): Promise<void> {
    store.start();
    await settle();
    expect(store.get().connection).toBe('ready');
}

async function submitExecTask(store: HarnessStore, argument = 'x'): Promise<void> {
    const steps: SubmitStepInput[] = [{ kind: 'filesystem.read', argument }];
    await store.submitExec('s-ipc', '活跃任务', steps);
}

beforeEach(() => {
    window.location.hash = '#/chat';
    vi.useFakeTimers();
});

afterEach(() => {
    currentStore?.dispose();
    currentStore = null;
    vi.useRealTimers();
});

// ---- 降级路径 --------------------------------------------------------------------

describe('degradation to task.inspect polling', () => {
    it('identity without the events capability: no subscription, 2s poll refreshes active tasks to terminal', async () => {
        const transport = new ScriptedTransport({ events: false });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);

        expect(store.get().eventsSupported).toBe(false);
        expect(transport.subscribeCount).toBe(0);
        expect(transport.subscribedListener).toBeNull();

        await submitExecTask(store);
        expect(store.get().activeTaskIds).toEqual(['t-0001']);
        expect(transport.inspectCalls).toContain('t-0001'); // 提交后的首次快照

        // 快照被服务端推进到终态：轮询必须看到它并清空活动集合。
        const inspectCallsBeforePoll = transport.inspectCalls.length;
        transport.setInspect(
            't-0001',
            inspectOf('t-0001', 'Completed', [
                stepView({ index: 0, kind: 'filesystem.read', status: 'ok', ok: true, exit_code: 0, result: 'done' }),
            ]),
        );
        await vi.advanceTimersByTimeAsync(2000);
        await settle();

        expect(transport.inspectCalls.length).toBeGreaterThan(inspectCallsBeforePoll);
        expect(store.get().taskDetails.get('t-0001')?.progress).toBe('Completed');
        expect(store.get().activeTaskIds).toEqual([]);
    });

    it('subscribe rejected with IpcRequestError(unsupported): poll fallback with eventsSupported=false', async () => {
        const transport = new ScriptedTransport({ events: true, subscribeUnsupported: true });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);

        expect(store.get().eventsSupported).toBe(false);
        expect(store.get().identity?.events).toBe(true); // hello 有能力，订阅被拒止
        expect(transport.subscribeCount).toBe(1);
        expect(transport.subscribedListener).toBeNull();
        expect(store.get().toasts.at(-1)?.text).toContain('unsupported');

        await submitExecTask(store);
        const inspectCallsBeforePoll = transport.inspectCalls.length;
        await vi.advanceTimersByTimeAsync(2000);
        await settle();

        expect(transport.inspectCalls.length).toBeGreaterThan(inspectCallsBeforePoll);
    });

    it('poll does not fire while there are no active tasks', async () => {
        const transport = new ScriptedTransport({ events: false });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);

        await vi.advanceTimersByTimeAsync(8000);
        await settle();
        expect(transport.inspectCalls).toHaveLength(0);
    });
});

// ---- 事件 seq 纪律 -----------------------------------------------------------------

describe('event seq discipline (resync on gap / overflow)', () => {
    it('a seq gap triggers resyncNote + snapshot refresh; the next event re-baselines silently', async () => {
        const transport = new ScriptedTransport({ events: true });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);
        await submitExecTask(store);

        const baselineInspects = transport.inspectCalls.length;
        transport.emit(hostEvent(1, 'starting'));
        transport.emit(hostEvent(2, 'running'));
        await settle();
        expect(store.get().resyncNote).toBeUndefined();
        expect(transport.inspectCalls.length).toBe(baselineInspects);

        // 第 3 帧跳跃到 4：resync 注记 + 快照重取（inspect 被调用）。
        transport.emit(hostEvent(4, 'stopping'));
        await settle();
        expect(store.get().resyncNote?.reason).toContain('4');
        expect(store.get().toasts.at(-1)?.text).toContain('事件流不连续');
        expect(store.get().hostStatus).toBe('stopping');
        expect(transport.inspectCalls.length).toBe(baselineInspects + 1);

        // seq 5 作为新基线被静默接受：不再连续 resync。
        const noteBefore = store.get().resyncNote;
        const inspectsAfterResync = transport.inspectCalls.length;
        transport.emit(hostEvent(5, 'running'));
        await settle();
        expect(store.get().resyncNote).toBe(noteBefore);
        expect(store.get().eventSeq).toBe(5);
        expect(store.get().hostStatus).toBe('running');
        expect(transport.inspectCalls.length).toBe(inspectsAfterResync);
    });

    it('an events.overflow marker triggers resync and the next normal event becomes the baseline', async () => {
        const transport = new ScriptedTransport({ events: true });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);
        await submitExecTask(store);

        transport.emit(hostEvent(1, 'running'));
        transport.emit(hostEvent(2, 'running'));
        await settle();

        const inspectsBefore = transport.inspectCalls.length;
        transport.emit({ v: 1, seq: 3, event: 'events.overflow', dropped: 5 });
        await settle();
        expect(store.get().resyncNote?.reason).toContain('5');
        expect(store.get().toasts.at(-1)?.text).toContain('溢出');
        expect(transport.inspectCalls.length).toBe(inspectsBefore + 1);

        // overflow 后的首帧作为新基线（seq 99 也被接受，不再触发 resync）。
        const noteBefore = store.get().resyncNote;
        transport.emit(hostEvent(99, 'stopping'));
        await settle();
        expect(store.get().hostStatus).toBe('stopping');
        expect(store.get().eventSeq).toBe(99);
        expect(store.get().resyncNote).toBe(noteBefore);
    });
});

// ---- 断线重连 --------------------------------------------------------------------

describe('connection loss and reconnect', () => {
    it('reconnects through the factory with backoff, resyncs the snapshot and resets the seq baseline', async () => {
        const first = new ScriptedTransport({ events: true });
        const second = new ScriptedTransport({ events: true });
        second.setInspect(
            't-0001',
            inspectOf('t-0001', 'Completed', [
                stepView({ index: 0, kind: 'filesystem.read', status: 'ok', ok: true, exit_code: 0, result: 'done' }),
            ]),
        );
        const factory = vi.fn(() => second);
        const store = new HarnessStore(first, { reconnect: factory });
        currentStore = store;
        await startReady(store);
        await submitExecTask(store);
        expect(store.get().activeTaskIds).toEqual(['t-0001']);

        // 断线前收两个事件（基线 seq 2），随后连接意外丢失。
        first.emit(hostEvent(1, 'running'));
        first.emit(hostEvent(2, 'running'));
        await settle();
        expect(store.get().eventSeq).toBe(2);

        first.loseConnection();
        await settle();
        expect(store.get().connection).toBe('connecting');
        expect(store.get().connectionError).toContain('重连');

        // 退避 500ms×2^0：定时器到达前工厂不得被调用。
        await vi.advanceTimersByTimeAsync(400);
        expect(factory).not.toHaveBeenCalled();
        await vi.advanceTimersByTimeAsync(100);
        expect(factory).toHaveBeenCalledTimes(1);
        await settle();

        expect(store.get().connection).toBe('ready');
        expect(store.get().resyncNote?.reason).toContain('重连');
        // 断线期间的任务快照经 resync 走新 transport 刷新到终态。
        expect(second.inspectCalls).toContain('t-0001');
        expect(store.get().taskDetails.get('t-0001')?.progress).toBe('Completed');
        expect(store.get().activeTaskIds).toEqual([]);

        // 新连接 seq 从 1 重新接受（旧基线是 2，重置后 seq 1 不是跳跃）。
        second.emit(hostEvent(1, 'running'));
        await settle();
        expect(store.get().eventSeq).toBe(1);
        expect(store.get().resyncNote?.reason).toContain('重连');
    });

    it('without a reconnect factory a lost connection lands in the explicit error state', async () => {
        const first = new ScriptedTransport({ events: true });
        const store = new HarnessStore(first);
        currentStore = store;
        await startReady(store);
        expect(first.helloCount).toBe(1);

        first.loseConnection();
        await settle();
        expect(store.get().connection).toBe('error');
        expect(store.get().connectionError).toBe('连接中断');

        await vi.advanceTimersByTimeAsync(10_000);
        await settle();
        expect(store.get().connection).toBe('error');
        expect(first.helloCount).toBe(1); // 无重连尝试
    });

    it('a failed reconnect attempt surfaces the error and retries with the doubled backoff', async () => {
        const first = new ScriptedTransport({ events: true });
        const broken = new ScriptedTransport({ events: true });
        broken.helloImpl = () => Promise.reject(new Error('bridge still down'));
        const second = new ScriptedTransport({ events: true });
        let factoryCalls = 0;
        const store = new HarnessStore(first, {
            reconnect: () => {
                factoryCalls += 1;
                return factoryCalls === 1 ? broken : second;
            },
        });
        currentStore = store;
        await startReady(store);

        first.loseConnection();
        await settle();
        expect(store.get().connection).toBe('connecting');

        await vi.advanceTimersByTimeAsync(500);
        await settle();
        expect(factoryCalls).toBe(1);
        // 第一次重连失败（hello 拒绝）→ 显式 error + 安排下一次退避。
        expect(store.get().connection).toBe('error');
        expect(store.get().connectionError).toBe('bridge still down');

        await vi.advanceTimersByTimeAsync(1000);
        await settle();
        expect(factoryCalls).toBe(2);
        expect(store.get().connection).toBe('ready');
        expect(second.helloCount).toBe(1);
        expect(store.get().resyncNote?.reason).toContain('重连');
    });
});

// ---- dispose -----------------------------------------------------------------

type RejectionSink = {
    on(event: 'unhandledRejection', listener: (reason: unknown) => void): void;
    off(event: 'unhandledRejection', listener: (reason: unknown) => void): void;
};

describe('dispose', () => {
    it('closes the transport, stops the poll timer and leaks no rejections from a hostile close', async () => {
        const transport = new ScriptedTransport({ events: false });
        const store = new HarnessStore(transport);
        currentStore = store;
        await startReady(store);
        await submitExecTask(store);
        const inspectCallsAtDispose = transport.inspectCalls.length;

        // dispose 后连 close() 的拒绝也必须被吞掉（无 unhandled rejection）。
        transport.closeImpl = () => Promise.reject(new Error('hostile close'));

        const nodeProcess = (globalThis as { process?: RejectionSink }).process;
        const unhandled: unknown[] = [];
        const onUnhandled = (reason: unknown): void => {
            unhandled.push(reason);
        };
        nodeProcess?.on('unhandledRejection', onUnhandled);
        try {
            store.dispose();
            currentStore = null;
            await vi.advanceTimersByTimeAsync(10_000);
            await settle();
            expect(unhandled).toEqual([]);
        } finally {
            nodeProcess?.off('unhandledRejection', onUnhandled);
        }

        expect(transport.closeCount).toBe(1);
        expect(transport.inspectCalls.length).toBe(inspectCallsAtDispose); // 轮询已停
    });

    it('cancels a pending reconnect timer: the factory is never invoked after dispose', async () => {
        const first = new ScriptedTransport({ events: true });
        let second: ScriptedTransport | null = null;
        const store = new HarnessStore(first, {
            reconnect: () => {
                second = new ScriptedTransport({ events: true });
                return second;
            },
        });
        currentStore = store;
        await startReady(store);

        first.loseConnection();
        await settle();
        expect(store.get().connection).toBe('connecting');

        store.dispose();
        currentStore = null;
        await vi.advanceTimersByTimeAsync(10_000);
        await settle();

        expect(second).toBeNull();
        expect(first.closeCount).toBe(1);
    });
});
