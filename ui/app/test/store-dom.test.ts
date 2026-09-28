// @vitest-environment jsdom
/// HarnessStore 集成面（jsdom + 最小脚本化 transport）。覆盖：连接生命周期、
/// submitExec 会话绑定事实流（DEC-025：user 消息以 journal 投影为准）、
/// 会话面契约路径（open / list / history / 容量显式拒绝）、会话事件增量
/// （message 入线程 + 派生标题、turn/output 进观察流）、事件面 seq、
/// 工作流 DEC-023 契约路径。模拟域（演示审批 / chat 模拟器 / 会话种子）
/// 自 DEC-025 退役，相应行为不再存在。

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { createMockTransport, IpcRequestError } from '@mirage/contracts';
import type {
    EventListener,
    InspectTask,
    MockTransport,
    ServerEvent,
    ServiceIdentity,
    SessionSummary,
    StepView,
    SubmitTaskInput,
    TaskProgress,
    TaskSummary,
} from '@mirage/contracts';

import type { ChatMessage } from '../src/state/model.js';
import { deriveSessionTitle, HarnessStore } from '../src/state/store.js';
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
        sessions: true,
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
    /** 可注入的 open 拒绝（默认成功）。 */
    openError: Error | null = null;
    readonly submittedRequests: SubmitTaskInput[] = [];
    readonly cancelledIds: string[] = [];

    private nextTaskNumber = 1;
    private readonly inspectTable = new Map<string, InspectTask>();
    private listener: EventListener | null = null;
    /** DEC-021 会话面：主会话入册，submitTask 落 user 日志条目。 */
    private readonly sessionList: SessionSummary[] = [
        { id: 's-primary', state: 'autonomous', created_at_ms: 1_000 },
    ];
    private readonly journal = new Map<string, Array<{ kind: 'user' | 'outcome'; text: string; sequence: number; recorded_at_ms: number }>>();

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
        // 默认快照：首步 running，其余 pending。
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
        const sessionId = request.session_id ?? 's-primary';
        const entries = this.journal.get(sessionId) ?? [];
        entries.push({
            kind: 'user',
            text: request.goal,
            sequence: entries.length + 1,
            recorded_at_ms: 2_000,
        });
        this.journal.set(sessionId, entries);
        return Promise.resolve({ task_id });
    }

    listSessions(): Promise<SessionSummary[]> {
        return Promise.resolve([...this.sessionList]);
    }

    openSession(): Promise<{ session_id: string }> {
        if (this.openError !== null) {
            return Promise.reject(this.openError);
        }
        this.sessionList.push({ id: 's-opened', state: 'autonomous', created_at_ms: 3_000 });
        return Promise.resolve({ session_id: 's-opened' });
    }

    sessionHistory(input: { session_id: string }): Promise<{
        session_id: string;
        entries: Array<{ kind: 'user' | 'outcome'; text: string; sequence: number; recorded_at_ms: number }>;
        truncated: boolean;
    }> {
        const entries = this.journal.get(input.session_id) ?? [];
        return Promise.resolve({ session_id: input.session_id, entries, truncated: false });
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
        expect(store.get().sessionsSupported).toBe(true);
        // 会话列表快照已加载（DEC-025：session.list 为事实源）。
        expect(store.get().sessions.map((s) => s.id)).toEqual(['s-primary']);
        expect(store.get().sessions[0]?.title).toBe(deriveSessionTitle('s-primary', undefined));
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
    it('binds session_id, links task to session, tracks it active and mirrors the thread', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);

        store.setDraft('s-primary', '待发送草稿');
        const steps: SubmitStepInput[] = [
            { kind: 'filesystem.read', argument: 'docs/ipc.md' },
            { kind: 'process.execute', argument: 'make card' },
        ];
        await store.submitExec('s-primary', '  整理 IPC 速查卡  ', steps);

        const state = store.get();
        const session = state.sessions.find((s) => s.id === 's-primary');
        expect(session?.taskId).toBe('t-0001');
        expect(state.activeTaskIds).toContain('t-0001');
        expect(state.taskInputs.get('t-0001')).toEqual(steps);
        expect(state.drafts.get('s-primary')).toBe('');

        // wire 映射：goal 去首尾空白；steps → { op, arg }；session 绑定显式携带
        expect(transport.submittedRequests[0]).toEqual({
            goal: '整理 IPC 速查卡',
            steps: [
                { op: 'filesystem.read', arg: 'docs/ipc.md' },
                { op: 'process.execute', arg: 'make card' },
            ],
            session_id: 's-primary',
        });

        // 线程：user 消息来自 journal 投影（提交后 loadHistory 快照），
        // 派生标题以首条 user 消息更新，系统回执含任务号。
        const thread = state.messages.get('s-primary') ?? [];
        const user = thread.find((m) => m.kind === 'user');
        expect(requireKind(user!, 'user').text).toBe('整理 IPC 速查卡');
        expect(requireKind(user!, 'user').sequence).toBe(1);
        expect(state.sessions.find((s) => s.id === 's-primary')?.title).toBe('整理 IPC 速查卡');
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
        const before = (store.get().messages.get('s-primary') ?? []).length;

        await store.submitExec('s-primary', '   ', [{ kind: 'filesystem.read', argument: 'x' }]);
        await store.submitExec('s-primary', '合法目标', []);
        await store.submitExec('s-primary', '', [{ kind: 'filesystem.read', argument: 'x' }]);

        expect(transport.submittedRequests).toHaveLength(0);
        expect((store.get().messages.get('s-primary') ?? []).length).toBe(before);
        expect(store.get().activeTaskIds).toHaveLength(0);
    });

    it('rejected submit appends an error system note with the wire code', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        transport.submitError = new IpcRequestError('invalid_state', 'mira host is not running');

        await store.submitExec('s-primary', '合法目标', [{ kind: 'filesystem.read', argument: 'x' }]);

        const thread = store.get().messages.get('s-primary') ?? [];
        const last = thread[thread.length - 1]!;
        expect(last.kind).toBe('system');
        const note = requireKind(last, 'system');
        expect(note.tone).toBe('error');
        expect(note.text).toContain('提交被拒绝');
        expect(note.text).toContain('invalid_state');
        expect(store.get().activeTaskIds).toHaveLength(0);
    });
});

// ---- 会话面（DEC-021 / DEC-025） ---------------------------------------------

describe('session face (contract path)', () => {
    it('newSession opens through session.open, refreshes the list and navigates', async () => {
        const { store } = makeStore(false);
        await startReady(store);

        store.newSession();
        await vi.waitFor(() => {
            expect(store.get().sessions.some((s) => s.id === 's-opened')).toBe(true);
            expect(window.location.hash).toBe('#/chat/s-opened');
        });
    });

    it('capacity exhaustion surfaces the explicit unavailable rejection as an error toast', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        transport.openError = new IpcRequestError('unavailable', 'session capacity exhausted (16)');

        store.newSession();
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('会话容量已满');
        });
        expect(window.location.hash).toBe('#/chat');
        expect(store.get().sessions.some((s) => s.id === 's-opened')).toBe(false);
    });

    it('without the sessions capability newSession is refused with a hint toast', async () => {
        const { store, transport } = makeStore(false);
        // 拔掉 sessions 能力位（wire 语义：hello 无 sessions 成员）。
        transport.helloImpl = () => {
            const identity = makeIdentity(false);
            delete (identity as { sessions?: boolean }).sessions;
            return Promise.resolve(identity);
        };
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('ready'));
        expect(store.get().sessionsSupported).toBe(false);
        expect(store.get().sessions).toEqual([]);

        store.newSession();
        expect(store.get().toasts.at(-1)?.text).toContain('会话面');
        expect(window.location.hash).toBe('#/chat');
    });

    it('selectSession loads the history snapshot and rebuilds the journal part idempotently', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        // 预置历史：两条 journal 条目。
        const entries = transport['journal'].get('s-primary') ?? [];
        entries.push(
            { kind: 'user', text: '第一条目标', sequence: 1, recorded_at_ms: 5_000 },
            { kind: 'outcome', text: 'loop settled: Completed (steps 1)', sequence: 2, recorded_at_ms: 6_000 },
        );
        transport['journal'].set('s-primary', entries);

        store.selectSession('s-primary');
        await vi.waitFor(() => {
            const thread = store.get().messages.get('s-primary') ?? [];
            expect(thread.filter((m) => m.kind === 'user' || m.kind === 'outcome')).toHaveLength(2);
        });
        const thread = store.get().messages.get('s-primary') ?? [];
        expect(requireKind(thread[0]!, 'user').text).toBe('第一条目标');
        expect(requireKind(thread[1]!, 'outcome').text).toContain('loop settled: Completed');
        // 派生标题跟随首条 user 消息。
        expect(store.get().sessions.find((s) => s.id === 's-primary')?.title).toBe('第一条目标');

        // 重复拉取幂等：journal 条目不重复。
        store.selectSession('s-primary');
        await vi.waitFor(() => {
            expect(
                (store.get().messages.get('s-primary') ?? []).filter((m) => m.kind === 'user'),
            ).toHaveLength(1);
        });
    });

    it('stopSession cancels the bound task only', async () => {
        const { store, transport } = makeStore(false);
        await startReady(store);
        await store.submitExec('s-primary', '目标', [{ kind: 'filesystem.read', argument: 'x' }]);

        store.stopSession('s-primary');
        await vi.waitFor(() => expect(transport.cancelledIds).toEqual(['t-0001']));
    });
});

// ---- 会话事件增量（通知面） ---------------------------------------------------

describe('session event increments', () => {
    it('session.message appends the thread entry, derives the title and feeds the observation stream', async () => {
        const { store, transport } = makeStore(true);
        await startReady(store);

        transport.emit({
            v: 1,
            seq: 4,
            event: 'session.message',
            session_id: 's-primary',
            task_id: 't-x',
            kind: 'user',
            text: '把桌面截图整理成周报',
            sequence: 1,
        });
        const thread = store.get().messages.get('s-primary') ?? [];
        const user = thread.find((m) => m.kind === 'user');
        expect(requireKind(user!, 'user').text).toBe('把桌面截图整理成周报');
        expect(store.get().sessions.find((s) => s.id === 's-primary')?.title).toBe('把桌面截图整理成周报');
        expect(store.get().obsFeed.get('s-primary')?.at(-1)?.text).toContain('用户');

        transport.emit({
            v: 1,
            seq: 5,
            event: 'session.message',
            session_id: 's-primary',
            task_id: 't-x',
            kind: 'outcome',
            text: 'loop settled: Failed (steps 1)',
            sequence: 2,
        });
        expect(
            (store.get().messages.get('s-primary') ?? []).some(
                (m) => m.kind === 'outcome' && m.text.includes('Failed'),
            ),
        ).toBe(true);
    });

    it('session.message is deduplicated against the journal snapshot by sequence', async () => {
        const { store, transport } = makeStore(true);
        await startReady(store);
        const event = {
            v: 1,
            seq: 4,
            event: 'session.message',
            session_id: 's-primary',
            task_id: 't-x',
            kind: 'user',
            text: '同一目标',
            sequence: 1,
        } as const;
        transport.emit({ ...event });
        transport.emit({ ...event, seq: 5 });
        expect(
            (store.get().messages.get('s-primary') ?? []).filter((m) => m.kind === 'user'),
        ).toHaveLength(1);
    });

    it('session.turn / session.output feed the observation stream and trigger a task refresh', async () => {
        const { store, transport } = makeStore(true);
        await startReady(store);
        await store.submitExec('s-primary', '目标', [{ kind: 'filesystem.read', argument: 'x' }]);
        const inspectSpy = vi.spyOn(transport, 'inspectTask');

        transport.emit({
            v: 1,
            seq: 10,
            event: 'session.turn',
            session_id: 's-primary',
            task_id: 't-0001',
            step: 1,
            kind: 'filesystem.read',
            status: 'ok',
        });
        transport.emit({
            v: 1,
            seq: 11,
            event: 'session.output',
            session_id: 's-primary',
            task_id: 't-0001',
            step: 1,
            chunk: '文件内容',
            truncated: false,
        });
        await vi.waitFor(() => expect(inspectSpy).toHaveBeenCalled());

        const feed = store.get().obsFeed.get('s-primary') ?? [];
        expect(feed.some((f) => f.kind === 'turn' && f.text.includes('轮次 1'))).toBe(true);
        expect(feed.some((f) => f.kind === 'output' && f.text.includes('文件内容'))).toBe(true);
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
        await store.submitExec('s-primary', '活跃任务', [{ kind: 'filesystem.read', argument: 'x' }]);
        // spy 在提交后挂上：只统计溢出触发的重取
        const inspectSpy = vi.spyOn(transport, 'inspectTask');

        transport.emit({ v: 1, seq: 15, event: 'events.overflow', dropped: 7 });
        expect(store.get().eventSeq).toBe(15);
        expect(store.get().resyncNote?.reason).toContain('7');
        expect(store.get().toasts.at(-1)?.text).toContain('溢出');
        await vi.waitFor(() => expect(inspectSpy).toHaveBeenCalledTimes(1));
    });
});

// ---- 工作流 = DEC-023 契约路径（IpcWorkflowBackend over MockTransport） ------

describe('RPA workflow engineering (contract-backed)', () => {
    /** 直接用 contracts 的 MockTransport（wire 忠实工作流面）。 */
    function makeWorkflowStore(stepDurationMs = 10): HarnessStore {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        return store;
    }

    async function startWorkflowsReady(store: HarnessStore): Promise<void> {
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().workflowsSupported).toBe(true);
            expect(store.get().workflows.length).toBeGreaterThan(0);
            expect(store.get().atoms.length).toBeGreaterThan(0);
        });
    }

    const defOf = (store: HarnessStore, id: string) => store.get().workflows.find((w) => w.id === id);

    it('connect loads wire summaries, the atom catalog and the control constructs', async () => {
        const store = makeWorkflowStore();
        expect(store.get().workflows).toHaveLength(0); // 连接前为空
        await startWorkflowsReady(store);

        const state = store.get();
        expect(state.workflows).toHaveLength(3); // mock 服务种子：1 草稿 + 2 已发布
        for (const def of state.workflows) {
            expect(typeof def.updatedAt).toBe('number');
            expect(def.runnable).toBe(def.published); // W-04 投影
            // 列表条目只有摘要：内容副本须经定义读取面按需回读（DEC-026）
            expect(def.contentKnown).toBe(false);
        }
        // wire 目录 2 原子（M1 参考绑定）+ 1 编辑器控制构造
        expect(state.atoms.map((a) => a.id)).toEqual([
            'desktop.filesystem.read_text',
            'desktop.process.execute',
            'ctl.loop',
        ]);
        // 会话面随同建立：主会话在列表（DEC-025 消费）
        expect(state.sessions.length).toBe(1);
    });

    it('createWorkflow prepends a fresh editable draft and navigates to its editor route', async () => {
        const store = makeWorkflowStore();
        await startWorkflowsReady(store);
        const before = store.get().workflows.length;

        store.createWorkflow();
        await vi.waitFor(() => expect(store.get().workflows).toHaveLength(before + 1));

        const draft = store.get().workflows[0]!;
        expect(draft.name).toBe('未命名工作流');
        expect(draft.published).toBe(false); // 新流程即草稿
        expect(draft.runnable).toBe(false);
        expect(draft.contentKnown).toBe(true);
        expect(draft.id).toMatch(/^[0-9a-f]{32}$/); // IR workflow_id
        expect(draft.steps).toEqual([]);
        expect(window.location.hash).toBe(`#/workflows/${draft.id}`);
    });

    it('mutateSteps saves drafts in-session; side-effect steps carry their step identity', async () => {
        const store = makeWorkflowStore();
        await startWorkflowsReady(store);
        store.createWorkflow();
        await vi.waitFor(() => expect(store.get().workflows[0]?.contentKnown).toBe(true));
        const id = store.get().workflows[0]!.id;

        store.mutateSteps(id, (steps) => [
            ...steps,
            {
                stepId: 'b1000000000000000000000000000001',
                atomId: 'desktop.process.execute',
                title: '执行命令',
                kind: 'tool_call',
                detail: '',
                params: { command: 'echo hi' },
            },
        ]);
        await vi.waitFor(() => {
            expect(defOf(store, id)?.steps).toHaveLength(1);
            expect(defOf(store, id)?.steps[0]?.stepId).toBe('b1000000000000000000000000000001');
        });

        store.setStepParams(id, 0, { command: 'echo changed' });
        await vi.waitFor(() => {
            expect(defOf(store, id)?.steps[0]?.params).toEqual({ command: 'echo changed' });
        });
    });

    it('publish flips to published & runnable (head digest badge); drafts refuse to run (W-04)', async () => {
        const store = makeWorkflowStore(300); // 慢驱动：running 态可被 waitFor 观测
        await startWorkflowsReady(store);
        store.createWorkflow();
        await vi.waitFor(() => expect(store.get().workflows[0]?.contentKnown).toBe(true));
        const id = store.get().workflows[0]!.id;

        // 草稿直接运行被拒（W-04），不产生运行记录
        store.runWorkflow(id);
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('草稿不可运行');
        });
        expect(store.get().workflowRuns).toHaveLength(0);

        store.publishWorkflow(id);
        await vi.waitFor(() => {
            const def = defOf(store, id);
            expect(def?.published).toBe(true);
            expect(def?.runnable).toBe(true);
            expect(def?.version).toBe(def?.digest?.slice(0, 8));
        });

        store.runWorkflow(id);
        await vi.waitFor(() => {
            expect(store.get().workflowRuns[0]?.status).toBe('running');
            expect(window.location.hash).toBe(`#/workflows/${id}/runs/${store.get().workflowRuns[0]?.id}`);
        });
    });

    it('run completion arrives through the workflow.run_updated event path (snapshot refresh)', async () => {
        const store = makeWorkflowStore(300); // 慢驱动：running 态可被 waitFor 观测
        await startWorkflowsReady(store);
        const published = store.get().workflows.find((w) => w.published && w.runnable)!;

        store.runWorkflow(published.id);
        await vi.waitFor(() => expect(store.get().workflowRuns[0]?.status).toBe('running'));
        await vi.waitFor(
            () => {
                const run = store.get().workflowRuns.find((r) => r.workflowId === published.id);
                expect(run?.status === 'completed' || run?.status === 'failed').toBe(true);
            },
            { timeout: 4000 },
        );
    });

    it('deleteWorkflow removes the def and its runs; navigates back when its editor is open', async () => {
        const store = makeWorkflowStore(300);
        await startWorkflowsReady(store);
        const target = store.get().workflows[0]!;
        window.location.hash = `#/workflows/${target.id}`;
        store.navigate({ view: 'workflow-editor', workflowId: target.id });

        store.deleteWorkflow(target.id);
        await vi.waitFor(() => {
            const state = store.get();
            expect(state.workflows.some((w) => w.id === target.id)).toBe(false);
            expect(state.workflowRuns.some((r) => r.workflowId === target.id)).toBe(false);
        });
        expect(window.location.hash).toBe('#/workflows'); // 正选中 → 路由回退
        expect(store.get().toasts.at(-1)?.text).toContain('工作流已删除');
    });

    it('foreign defs without a content copy refuse mutations (no shadowing empty draft)', async () => {
        const store = makeWorkflowStore();
        await startWorkflowsReady(store);
        const foreign = store.get().workflows.find((w) => !w.contentKnown)!;
        const before = foreign.steps;

        store.mutateSteps(foreign.id, (steps) => steps);
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('不可编辑');
        });
        expect(defOf(store, foreign.id)?.steps).toEqual(before);
    });
});

// ---- DEC-026 faces：定义读取面（编辑器跨会话编辑）+ 桌面状态观察面 ----------

describe('DEC-026 faces (definition read face + desktop observation)', () => {
    const defOf = (store: HarnessStore, id: string) => store.get().workflows.find((w) => w.id === id);

    function makeFreshWorkflowStore(stepDurationMs = 10): { store: HarnessStore; transport: MockTransport } {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        return { store, transport: created.transport };
    }

    async function startReady(store: HarnessStore): Promise<void> {
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().workflows.length).toBeGreaterThan(0);
        });
    }

    it('openWorkflowEditor hydrates a summary-only def into an editable copy (workflow.get)', async () => {
        const { store } = makeFreshWorkflowStore();
        await startReady(store);
        const foreign = store.get().workflows.find((w) => !w.contentKnown)!;
        expect(foreign.description).toBe('');

        store.openWorkflowEditor(foreign.id);
        expect(window.location.hash).toBe(`#/workflows/${foreign.id}`);
        await vi.waitFor(() => {
            const hydrated = defOf(store, foreign.id);
            expect(hydrated?.contentKnown).toBe(true);
            // 摘要投影字段（published / runnable）保持以 workflow.list 为准。
            expect(hydrated?.published).toBe(foreign.published);
            expect(hydrated?.runnable).toBe(foreign.runnable);
        });
    });

    it('openWorkflowEditor keeps the def read-only and toasts when the read face rejects', async () => {
        const { store, transport } = makeFreshWorkflowStore();
        await startReady(store);
        const foreign = store.get().workflows.find((w) => !w.contentKnown)!;
        vi.spyOn(transport, 'getWorkflow').mockRejectedValue(
            new IpcRequestError('not_found', 'unknown workflow id'),
        );

        store.openWorkflowEditor(foreign.id);
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('定义读取失败');
        });
        expect(defOf(store, foreign.id)?.contentKnown).toBe(false);
    });

    it('refreshObservation captures the desktop snapshot (semantic by default, visual on request)', async () => {
        const { store } = makeFreshWorkflowStore();
        await startReady(store);
        expect(store.get().observationSupported).toBe(true);
        expect(store.get().observation.status).toBe('idle');

        store.refreshObservation(false);
        await vi.waitFor(() => {
            expect(store.get().observation.status).toBe('ready');
        });
        const view = store.get().observation.view;
        expect(view?.semantic).toBeDefined();
        expect(view?.visual_snapshot_ref).toBeUndefined();
        expect(store.get().observation.visualRequested).toBe(false);

        store.refreshObservation(true);
        await vi.waitFor(() => {
            expect(store.get().observation.view?.visual_snapshot_ref).toBe('@vs1');
        });
        expect(store.get().observation.visualRequested).toBe(true);
    });

    it('refreshObservation surfaces the stable error when the face refuses (fail closed)', async () => {
        // hostStartDelayMs 拉长：连接就绪时 host 仍在 starting，桌面环境未
        // 绑定 → 观察 face 以 unavailable 拒绝（fail closed 如实呈现）。
        const created = createMockTransport({ hostStartDelayMs: 5_000, stepDurationMs: 0 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('ready'));

        store.refreshObservation(false);
        await vi.waitFor(() => {
            expect(store.get().observation.status).toBe('error');
        });
        expect(store.get().observation.error).toContain('unavailable');
    });
});

// ---- 会话管理面（session.close，DEC-026 挂账②兑现） ------------------------

describe('session close management face (DEC-026)', () => {
    it('deleteSession closes the session, drops local memory and refreshes the list', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().sessions.length).toBe(1);
        });

        const before = store.get().sessions.length;
        store.newSession();
        await vi.waitFor(() => {
            expect(store.get().sessions.length).toBe(before + 1);
            expect(store.get().route.view).toBe('chat');
        });
        // newSession 落点新会话；落点会话产生本地记忆（草稿），删除后随
        // 注册表事实收敛清空。
        const openedId = store.get().route.view === 'chat' ? store.get().route.sessionId : undefined;
        expect(openedId).toBeDefined();
        store.setDraft(openedId!, 'draft text');

        store.deleteSession(openedId);
        await vi.waitFor(() => {
            expect(store.get().sessions.some((s) => s.id === openedId)).toBe(false);
            expect(store.get().messages.has(openedId)).toBe(false);
            expect(store.get().drafts.has(openedId)).toBe(false);
        });
        expect(store.get().toasts.at(-1)?.text).toContain('会话已关闭');
        // 当前路由是被删会话时回退到默认会话路由。
        expect(window.location.hash).toBe('#/chat');
    });

    it('deleteSession surfaces the stable invalid_state for the primary session', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().sessions.length).toBe(1);
        });
        const primaryId = store.get().sessions[0]!.id;

        store.deleteSession(primaryId);
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('主会话不可关闭');
        });
        // 主会话仍在列表（服务端拒绝未触碰注册表）。
        expect(store.get().sessions.some((s) => s.id === primaryId)).toBe(true);
    });
});

// ---- DEC-027：对话模式（session.chat 面消费） -------------------------------

describe('dialog mode (DEC-027 session.chat face)', () => {
    it('sendDialog appends the turn, converges on the ok event and clears pending', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().chatSupported).toBe(true);
        });
        store.selectSession(store.get().sessions[0]!.id);
        await vi.waitFor(() => expect(store.get().route.sessionId).toBe(store.get().sessions[0]!.id));

        // pending 投影是瞬态（模拟回复 15 ms 内结算）；以最终收敛态断言：
        // 线程含用户行 + ok 助手行，pending 闩锁清除。
        store.sendDialog(store.get().sessions[0]!.id, '列出当前应用');
        await vi.waitFor(() => {
            const list = store.get().messages.get(store.get().sessions[0]!.id) ?? [];
            const assistant = list.find((m) => m.kind === 'assistant');
            expect(assistant?.kind === 'assistant' && assistant.status === 'ok').toBe(true);
            expect(store.get().pendingChats.has(store.get().sessions[0]!.id)).toBe(false);
        });
        const list = store.get().messages.get(store.get().sessions[0]!.id) ?? [];
        expect(list.some((m) => m.kind === 'user' && m.text === '列出当前应用')).toBe(true);
        const settled = list.find((m) => m.kind === 'assistant');
        expect(settled?.kind === 'assistant' && settled.text).toContain('模拟回复');
    });

    it('surfaces the stable error when the dialog face refuses (invalid_state)', async () => {
        const created = createMockTransport({ hostStartDelayMs: 5_000, stepDurationMs: 0 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('ready'));

        store.sendDialog(store.get().sessions[0]!.id, 'hi');
        await vi.waitFor(() => {
            expect(store.get().toasts.at(-1)?.text).toContain('对话面不可用');
        });
    });
});

// ---- M5-07：批准中心与策略面（store 接线） ----------------------------------

describe('M5-07 approval center + policy face (store wiring)', () => {
    it('loads the policy snapshot, surfaces demo approvals and converges after respond', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().policySupported).toBe(true);
            expect(store.get().permissionsSupported).toBe(true);
        });

        // 连接即读取策略快照（设置页矩阵事实源）。
        await vi.waitFor(() => expect(store.get().policy).toBeDefined());
        expect(Object.keys(store.get().policy!.rules).length).toBeGreaterThan(0);

        // mock-only demo 钩子产生一条挂起确认（permission.request 事件驱动
        // permission.list 重取），批准中心出现该条目。
        created.service.demoPermissionRequest('filesystem.write', '/tmp/approval.txt');
        await vi.waitFor(() => expect(store.get().pendingApprovals).toHaveLength(1));
        const requestId = store.get().pendingApprovals[0]!.request_id;
        expect(store.get().pendingApprovals[0]!.capability).toBe('filesystem.write');

        // 响应（先到先得）后快照收敛为空。
        await store.respondApproval(requestId, true);
        await vi.waitFor(() => expect(store.get().pendingApprovals).toHaveLength(0));
    });

    it('setPolicy applies the new rule set and converges the store view', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => {
            expect(store.get().connection).toBe('ready');
            expect(store.get().policy).toBeDefined();
        });

        const rules = { ...store.get().policy!.rules, 'clipboard.write': 'deny' };
        await store.setPolicy(rules);
        expect(store.get().policy?.rules['clipboard.write']).toBe('deny');
        // 其余规则原样保留（全量覆盖语义在 store 侧以完整规则集提交）。
        expect(store.get().policy?.rules['filesystem.read']).toBe('allow');
    });
});

// ---- M5-08：会话重命名本地别名（DEC-026 挂账⑤） ------------------------------

describe('M5-08 session rename alias (display-layer state)', () => {
    it('renameSession persists the alias locally and an empty title clears it', async () => {
        const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const store = new HarnessStore(created.transport);
        currentStore = store;
        store.start();
        await vi.waitFor(() => expect(store.get().connection).toBe('ready'));
        const id = store.get().sessions[0]!.id;

        // 别名生效并持久化到 localStorage（展示层产品状态，不进契约面）。
        store.renameSession(id, '  自定义别名  ');
        expect(store.get().sessionAliases.get(id)).toBe('自定义别名');
        expect(store.get().sessions.find((s) => s.id === id)?.title).toBe('自定义别名');
        expect(JSON.parse(window.localStorage.getItem('mirage.session-aliases')!)[id]).toBe(
            '自定义别名',
        );

        // 空标题清除别名：回落派生标题，持久化条目一并移除。
        store.renameSession(id, '   ');
        expect(store.get().sessionAliases.has(id)).toBe(false);
        expect(store.get().sessions.find((s) => s.id === id)?.title).not.toBe('自定义别名');
        expect(window.localStorage.getItem('mirage.session-aliases')).toBe('{}');
    });
});
