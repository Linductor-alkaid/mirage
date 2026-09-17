// @vitest-environment jsdom
/// HarnessStore 集成面（jsdom + 最小脚本化 transport，eventsSupported=false 走
/// 轮询分支）。覆盖：连接生命周期、submitExec 事实流、演示审批决断、会话
/// 管理（容量/删除导航/重命名）、事件面 seq、chat 模拟流的 store 集成。

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { IpcRequestError } from '@mirage/contracts';
import type {
    EventListener,
    InspectTask,
    MockTransport,
    ServerEvent,
    ServiceIdentity,
    StepView,
    SubmitTaskInput,
    TaskProgress,
    TaskSummary,
} from '@mirage/contracts';

import { MAX_SESSIONS } from '../src/state/harness-mock.js';
import type { ChatMessage } from '../src/state/model.js';
import { HarnessStore } from '../src/state/store.js';
import type { SubmitStepInput } from '../src/state/store.js';

// ---- 脚本化 transport（最小 MirageTransport 形状，promise 语义与 wire 一致） --

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

class ScriptedTransport {
    readonly label = 'scripted';
    readonly eventsSupported: boolean;
    /** 可注入的 hello 结果（默认成功）。 */
    helloImpl: () => Promise<ServiceIdentity>;
    /** 可注入的 submit 拒绝（默认成功）。 */
    submitError: Error | null = null;
    readonly submittedRequests: SubmitTaskInput[] = [];
    readonly cancelledIds: string[] = [];

    private nextTaskNumber = 1;
    private readonly inspectTable = new Map<string, InspectTask>();
    private listener: EventListener | null = null;

    constructor(options: { eventsSupported?: boolean } = {}) {
        this.eventsSupported = options.eventsSupported ?? false;
        this.helloImpl = () => Promise.resolve(makeIdentity(this.eventsSupported));
    }

    hello(): Promise<ServiceIdentity> {
        return this.helloImpl();
    }

    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        if (this.submitError !== null) {
            return Promise.reject(this.submitError);
        }
        this.submittedRequests.push(request);
        const task_id = `t-${String(this.nextTaskNumber).padStart(4, '0')}`;
        this.nextTaskNumber += 1;
        // 默认快照：首步 running（驱动审批演示路径），其余 pending。
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
        const detail = this.inspectTable.get(taskId);
        return detail !== undefined
            ? Promise.resolve(detail)
            : Promise.reject(new IpcRequestError('not_found', `no scripted task '${taskId}'`));
    }

    cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        this.cancelledIds.push(taskId);
        return Promise.resolve({ task_id: taskId, progress: 'Cancelling' });
    }

    shutdown(): Promise<void> {
        return Promise.resolve();
    }

    subscribe(listener: EventListener): Promise<void> {
        this.listener = listener;
        return Promise.resolve();
    }

    unsubscribe(): Promise<void> {
        this.listener = null;
        return Promise.resolve();
    }

    close(): Promise<void> {
        return Promise.resolve();
    }

    setInspect(taskId: string, detail: InspectTask): void {
        this.inspectTable.set(taskId, detail);
    }

    emit(event: ServerEvent): void {
        this.listener?.(event);
    }

    /** 当前订阅者（测试断言用）。 */
    get subscribedListener(): EventListener | null {
        return this.listener;
    }
}

// HarnessStore 只消费 MirageTransport 表面；脚本化实现按构造兼容。
function makeStore(eventsSupported = false): { store: HarnessStore; transport: ScriptedTransport } {
    const transport = new ScriptedTransport({ eventsSupported });
    const store = new HarnessStore(transport as unknown as MockTransport);
    currentStore = store;
    return { store, transport };
}

function requireKind<T extends ChatMessage['kind']>(
    message: ChatMessage,
    kind: T,
): Extract<ChatMessage, { kind: T }> {
    if (message.kind !== kind) {
        throw new Error(`expected a '${kind}' message but got '${message.kind}'`);
    }
    return message as Extract<ChatMessage, { kind: T }>;
}

async function startReady(store: HarnessStore): Promise<void> {
    store.start();
    await vi.waitFor(() => {
        expect(store.get().connection).toBe('ready');
    });
}

// ---- 生命周期 ------------------------------------------------------------

let currentStore: HarnessStore | null = null;

beforeEach(() => {
    window.location.hash = '#/chat';
});

afterEach(() => {
    currentStore?.dispose();
    currentStore = null;
    vi.restoreAllMocks();
    vi.useRealTimers();
});

describe('HarnessStore connection lifecycle', () => {
    it('hello failure lands connection in error with the message', async () => {
        const { store, transport } = makeStore();
        transport.helloImpl = () => Promise.reject(new Error('bridge down'));
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('error'));
        expect(store.get().connectionError).toBe('bridge down');
        expect(store.get().identity).toBeUndefined();
    });

    it('hello success becomes ready with identity; no-events peer installs the 2s poll and dispose clears it', async () => {
        const intervalSpy = vi.spyOn(window, 'setInterval');
        const clearSpy = vi.spyOn(window, 'clearInterval');
        const { store, transport } = makeStore(false);
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('ready'));

        expect(store.get().identity?.service).toBe('mirage-runtime');
        expect(store.get().hostStatus).toBe('running');
        expect(store.get().eventsSupported).toBe(false);
        expect(transport.subscribedListener).toBeNull(); // 降级轮询，未订阅事件流
        expect(intervalSpy).toHaveBeenCalledWith(expect.any(Function), 2000);

        const handle: unknown = intervalSpy.mock.results[0]?.value;
        store.dispose();
        currentStore = null;
        expect(clearSpy).toHaveBeenCalledWith(handle);
    });

    it('events-capable peer subscribes and never installs a poll interval', async () => {
        const intervalSpy = vi.spyOn(window, 'setInterval');
        const { store, transport } = makeStore(true);
        store.start();
        await vi.waitFor(() => expect(transport.subscribedListener).not.toBeNull());
        expect(store.get().eventsSupported).toBe(true);
        expect(intervalSpy).not.toHaveBeenCalled();
    });
});

// ---- submitExec ------------------------------------------------------------

describe('submitExec (contract task facts)', () => {
    it('links task to session, tracks it active, remembers inputs and mirrors the thread', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);

        store.setDraft('s-ipc', '待发送草稿');
        const steps: SubmitStepInput[] = [
            { kind: 'filesystem.read', argument: 'docs/ipc.md' },
            { kind: 'process.execute', argument: 'make card' },
        ];
        await store.submitExec('s-ipc', '  整理 IPC 速查卡  ', steps);

        const state = store.get();
        const session = state.sessions.find((s) => s.id === 's-ipc');
        expect(session?.taskId).toBe('t-0001');
        expect(state.activeTaskIds).toContain('t-0001');
        expect(state.taskInputs.get('t-0001')).toEqual(steps);
        expect(state.drafts.get('s-ipc')).toBe('');

        // wire 映射：goal 去首尾空白；steps → { op, arg }
        expect(transport.submittedRequests[0]).toEqual({
            goal: '整理 IPC 速查卡',
            steps: [
                { op: 'filesystem.read', arg: 'docs/ipc.md' },
                { op: 'process.execute', arg: 'make card' },
            ],
        });

        // 线程：用户消息 + 系统 note（含任务号）+ 步骤卡（argument 按 index 合并）
        const thread = state.messages.get('s-ipc') ?? [];
        expect(
            thread.some((m) => m.kind === 'user' && m.text === '整理 IPC 速查卡'),
        ).toBe(true);
        const systemNotes = thread.filter((m) => m.kind === 'system');
        expect(systemNotes.length).toBeGreaterThanOrEqual(1);
        expect(requireKind(systemNotes[systemNotes.length - 1]!, 'system').text).toContain('t-0001');
        const stepArgs = thread
            .filter((m) => m.kind === 'step')
            .map((m) => requireKind(m, 'step').step.argument);
        expect(stepArgs).toEqual(['docs/ipc.md', 'make card']);
    });

    it('empty goal or empty steps is a local no-op: transport untouched, thread unchanged', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        const before = (store.get().messages.get('s-ipc') ?? []).length;

        await store.submitExec('s-ipc', '   ', [{ kind: 'filesystem.read', argument: 'x' }]);
        await store.submitExec('s-ipc', '合法目标', []);
        await store.submitExec('s-ipc', '', [{ kind: 'filesystem.read', argument: 'x' }]);

        expect(transport.submittedRequests).toHaveLength(0);
        expect((store.get().messages.get('s-ipc') ?? []).length).toBe(before);
        expect(store.get().activeTaskIds).toHaveLength(0);
    });

    it('rejected submit appends an error system note with the wire code', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        transport.submitError = new IpcRequestError('invalid_state', 'mira host is not running');

        await store.submitExec('s-ipc', '合法目标', [{ kind: 'filesystem.read', argument: 'x' }]);

        const thread = store.get().messages.get('s-ipc') ?? [];
        const last = thread[thread.length - 1]!;
        expect(last.kind).toBe('system');
        const note = requireKind(last, 'system');
        expect(note.tone).toBe('error');
        expect(note.text).toContain('提交被拒绝');
        expect(note.text).toContain('invalid_state');
        expect(store.get().activeTaskIds).toHaveLength(0);
    });
});

// ---- decideApproval ----------------------------------------------------------

describe('decideApproval (demo approval surface)', () => {
    async function submitApproveTask(
        store: HarnessStore,
        sessionId: string,
    ): Promise<string> {
        await store.submitExec(sessionId, '需要审批的目标', [
            { kind: 'process.execute', argument: 'approve:npm test' },
        ]);
        const approval = store.get().pendingApprovals[0];
        expect(approval, 'running approve: 步骤应生成待决审批').toBeDefined();
        return approval!.id;
    }

    it('pending approval can be approved once: card flips, queue drains, toast fires', async () => {
        const { store } = makeStore(false);
        await startReady(store);
        const approvalId = await submitApproveTask(store, 's-build');
        expect(store.get().pendingApprovals[0]?.summary).toBe('npm test');

        const threadBefore = (store.get().messages.get('s-build') ?? []).length;
        store.decideApproval(approvalId, true);

        expect(store.get().pendingApprovals).toHaveLength(0);
        const card = (store.get().messages.get('s-build') ?? []).find(
            (m) => m.kind === 'approval' && requireKind(m, 'approval').approval.id === approvalId,
        );
        expect(card).toBeDefined();
        expect(requireKind(card!, 'approval').approval.status).toBe('approved');
        expect(requireKind(card!, 'approval').approval.decidedAt).toBeDefined();
        // 批准不追加消息（线程长度不变，卡片原地更新）
        expect((store.get().messages.get('s-build') ?? []).length).toBe(threadBefore);
        expect(store.get().toasts.at(-1)?.text).toBe('已放行');
    });

    it('denial marks the card denied and appends a warn system note', async () => {
        const { store } = makeStore(false);
        await startReady(store);
        const approvalId = await submitApproveTask(store, 's-weekly');

        store.decideApproval(approvalId, false);

        const thread = store.get().messages.get('s-weekly') ?? [];
        const card = thread.find(
            (m) => m.kind === 'approval' && requireKind(m, 'approval').approval.id === approvalId,
        );
        expect(requireKind(card!, 'approval').approval.status).toBe('denied');
        const last = requireKind(thread[thread.length - 1]!, 'system');
        expect(last.tone).toBe('warn');
        expect(last.id).toBe(`mn-${approvalId}`);
        expect(store.get().toasts.at(-1)?.text).toBe('已拒止');
    });

    // 复验（修复后）：decideApproval 幂等门控 —— pendingApprovals 不含该 id
    // （已决或未知）时整体 no-op，无消息追加、无 toast。
    it('already-decided approvals cannot be re-decided: card stays, no duplicate warn/toast', async () => {
        const { store } = makeStore(false);
        await startReady(store);
        const approvalId = await submitApproveTask(store, 's-build');
        store.decideApproval(approvalId, true);

        const warnCount = () =>
            (store.get().messages.get('s-build') ?? []).filter(
                (m) => m.kind === 'system' && requireKind(m, 'system').text.includes('已拒止该权限请求'),
            ).length;
        const threadLength = (store.get().messages.get('s-build') ?? []).length;
        const toastCount = store.get().toasts.length;

        store.decideApproval(approvalId, false); // 重复决定（严格语义：应整体 no-op）

        const card = (store.get().messages.get('s-build') ?? []).find(
            (m) => m.kind === 'approval' && requireKind(m, 'approval').approval.id === approvalId,
        );
        expect(requireKind(card!, 'approval').approval.status).toBe('approved');
        expect(warnCount()).toBe(0);
        expect((store.get().messages.get('s-build') ?? []).length).toBe(threadLength);
        expect(store.get().toasts).toHaveLength(toastCount);
    });
});

// ---- 会话管理 ----------------------------------------------------------------

describe('session management', () => {
    it('newSession caps at MAX_SESSIONS=64 keeping the newest', () => {
        const { store } = makeStore();
        const created: string[] = [];
        for (let i = 0; i < 65; i += 1) {
            created.push(store.newSession('chat'));
        }
        expect(MAX_SESSIONS).toBe(64);
        const sessions = store.get().sessions;
        expect(sessions).toHaveLength(MAX_SESSIONS);
        expect(sessions[0]?.id).toBe(created[64]); // 最新在前
        const ids = new Set(sessions.map((s) => s.id));
        expect(ids.has(created[0]!)).toBe(false); // 最旧被裁剪
        expect(ids.has(created[1]!)).toBe(true);
        expect(store.get().messages.get(created[64]!)).toEqual([]); // 新线程已建
    });

    it('deleteSession drops the thread and navigates back to #/chat when it was selected', () => {
        window.location.hash = '#/chat/s-ipc';
        const { store } = makeStore();
        expect(store.get().route).toEqual({ view: 'chat', sessionId: 's-ipc' });

        store.deleteSession('s-ipc');

        expect(store.get().sessions.some((s) => s.id === 's-ipc')).toBe(false);
        expect(store.get().messages.has('s-ipc')).toBe(false);
        expect(window.location.hash).toBe('#/chat');
    });

    it('deleteSession keeps the route when another session is selected', () => {
        window.location.hash = '#/chat/s-ipc';
        const { store } = makeStore();
        store.deleteSession('s-build');
        expect(store.get().messages.has('s-build')).toBe(false);
        expect(window.location.hash).toBe('#/chat/s-ipc');
        expect(store.get().sessions.some((s) => s.id === 's-build')).toBe(false);
    });

    it('renameSession trims valid titles and ignores blank ones', () => {
        const { store } = makeStore();
        const original = store.get().sessions.find((s) => s.id === 's-ipc')?.title;

        store.renameSession('s-ipc', '   ');
        expect(store.get().sessions.find((s) => s.id === 's-ipc')?.title).toBe(original);

        store.renameSession('s-ipc', '  IPC 速查卡 v2  ');
        expect(store.get().sessions.find((s) => s.id === 's-ipc')?.title).toBe('IPC 速查卡 v2');
    });

    it('pinSession toggles the pin flag', () => {
        const { store } = makeStore();
        store.pinSession('s-ipc', true);
        expect(store.get().sessions.find((s) => s.id === 's-ipc')?.pinned).toBe(true);
        store.pinSession('s-ipc', false);
        expect(store.get().sessions.find((s) => s.id === 's-ipc')?.pinned).toBe(false);
    });
});

// ---- 事件面 -------------------------------------------------------------------

describe('event face (eventsSupported=true)', () => {
    it('host.status and task.updated events advance eventSeq monotonically and hostStatus', async () => {
        const { store, transport } = makeStore(true);
        await startReady(store);

        transport.emit({ v: 1, seq: 4, event: 'host.status', status: 'stopping' });
        expect(store.get().hostStatus).toBe('stopping');
        expect(store.get().eventSeq).toBe(4);

        transport.emit({
            v: 1,
            seq: 9,
            event: 'task.updated',
            task_id: 't-x',
            goal: 'g',
            progress: 'Active',
            has_success: false,
            success: false,
        });
        expect(store.get().eventSeq).toBe(9);
    });

    it('events.overflow records a resync note, warns via toast and retriggers task refresh', async () => {
        const { store, transport } = makeStore(true);
        await startReady(store);
        await store.submitExec('s-ipc', '活跃任务', [{ kind: 'filesystem.read', argument: 'x' }]);
        // spy 在提交后挂上：只统计溢出触发的重取
        const inspectSpy = vi.spyOn(transport, 'inspectTask');

        transport.emit({ v: 1, seq: 15, event: 'events.overflow', dropped: 7 });
        expect(store.get().eventSeq).toBe(15);
        expect(store.get().resyncNote?.reason).toContain('7');
        expect(store.get().toasts.at(-1)?.text).toContain('溢出');
        await vi.waitFor(() => expect(inspectSpy).toHaveBeenCalledTimes(1));
    });
});

// ---- sendChat 集成（chat 模拟流进入线程） -----------------------------------------

describe('sendChat (simulated chat stream into the thread)', () => {
    // 复验（修复后）：store 现以 begin() 返回的 id 追加占位 assistant 消息，
    // 模拟器回调按 id 原地推进（thinking/分片/streaming=false）。
    it('streams an assistant reply into the thread and clears the busy flag', async () => {
        vi.useFakeTimers();
        const { store } = makeStore(true); // 事件分支：避免轮询 interval 干扰 runAllTimersAsync
        store.start();
        await vi.advanceTimersByTimeAsync(0);
        expect(store.get().connection).toBe('ready');

        const assistantCountBefore = (store.get().messages.get('s-ipc') ?? []).filter(
            (m) => m.kind === 'assistant',
        ).length;

        store.sendChat('s-ipc', '桌面权限是怎么判定的？');
        expect(store.get().simBusySession).toBe('s-ipc');

        await vi.runAllTimersAsync(); // 思考 + 全部流片 + toast 定时器

        const thread = store.get().messages.get('s-ipc') ?? [];
        expect(
            thread.some((m) => m.kind === 'user' && m.text === '桌面权限是怎么判定的？'),
        ).toBe(true);
        // 流已跑完（busy 闩锁已被 onAssistantDone 释放）——证明模拟器回调链执行到收尾
        expect(store.get().simBusySession).toBeUndefined();
        // 期望：本轮生成一条新的 assistant 消息（begin 返回的 id 对应的消息进入线程），
        // 文本为分片拼接的完整回复，且流结束后不再处于 streaming。
        const assistants = thread.filter((m) => m.kind === 'assistant');
        expect(assistants, 'sendChat 后应新增一条 assistant 回复').toHaveLength(
            assistantCountBefore + 1,
        );
        const reply = assistants[assistants.length - 1];
        if (reply?.kind === 'assistant') {
            expect(reply.text).toContain('权限判定');
            expect(reply.streaming).toBe(false);
        }
    });
});
