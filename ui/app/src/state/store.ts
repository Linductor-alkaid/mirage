/// Harness 应用状态（React 侧唯一事实源）。
///
/// 分层纪律：
/// - 契约事实（host 身份、任务状态机、步骤、事件 seq/overflow）只来自
///   `MirageTransport`（当前为 contracts MockTransport）；事件仅触发快照
///   重取（DEC-012 决策 4：事件是通知，不是状态本体）。
/// - 会话 / 消息 / 审批 / 工作流为模拟域（`harness-mock.ts`），界面标注
///   「模拟」；不回写、不冒充契约事实。
/// - 终态幂等：已取消 / 已完成任务不因迟到事件复活。

import type {
    HostStatus,
    InspectTask,
    ServiceIdentity,
    StepView,
    TaskProgress,
} from '@mirage/contracts';
import { IpcRequestError } from '@mirage/contracts';

import type { MockTransport } from '@mirage/contracts';
import {
    ChatSimulator,
    exportSessionMarkdown,
    newSessionMeta,
    seedContext,
    seedSessions,
    seedWorkflows,
    MAX_MESSAGES_PER_SESSION,
    MAX_SESSIONS,
} from './harness-mock.js';
import type {
    ApprovalRequest,
    ChatMessage,
    ContextUsage,
    SessionMeta,
    StepDisplay,
    ToastNote,
    WorkflowDef,
    WorkflowRun,
} from './model.js';

// ---------------------------------------------------------------------------
// 路由（设计规范 §3.2）
// ---------------------------------------------------------------------------

export type Route =
    | { view: 'chat'; sessionId?: string }
    | { view: 'workflows' }
    | { view: 'workflow-editor'; workflowId: string }
    | { view: 'workflow-run'; workflowId: string; runId: string }
    | { view: 'resources' }
    | { view: 'settings'; category: string };

export const SETTINGS_CATEGORIES = [
    'general',
    'appearance',
    'models',
    'memory',
    'skills',
    'mcp',
    'permissions',
    'runtime',
] as const;

export function parseRoute(hash: string): Route {
    const path = hash.replace(/^#/, '');
    const segs = path.split('/').filter((s) => s.length > 0).map(decodeURIComponent);
    if (segs[0] === 'chat') {
        return { view: 'chat', sessionId: segs[1] };
    }
    if (segs[0] === 'workflows') {
        const workflowId = segs[1];
        if (workflowId !== undefined) {
            if (segs[2] === 'runs' && segs[3] !== undefined) {
                return { view: 'workflow-run', workflowId, runId: segs[3] };
            }
            return { view: 'workflow-editor', workflowId };
        }
        return { view: 'workflows' };
    }
    if (segs[0] === 'resources') {
        return { view: 'resources' };
    }
    if (segs[0] === 'settings') {
        const category = segs[1] ?? 'general';
        return {
            view: 'settings',
            category: (SETTINGS_CATEGORIES as readonly string[]).includes(category) ? category : 'general',
        };
    }
    return { view: 'chat' };
}

export function routeToHash(route: Route): string {
    switch (route.view) {
        case 'chat':
            return route.sessionId !== undefined ? `#/chat/${encodeURIComponent(route.sessionId)}` : '#/chat';
        case 'workflows':
            return '#/workflows';
        case 'workflow-editor':
            return `#/workflows/${encodeURIComponent(route.workflowId)}`;
        case 'workflow-run':
            return `#/workflows/${encodeURIComponent(route.workflowId)}/runs/${encodeURIComponent(route.runId)}`;
        case 'resources':
            return '#/resources';
        case 'settings':
            return `#/settings/${route.category}`;
    }
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

export type ConnectionStatus = 'connecting' | 'ready' | 'error';

export interface HarnessState {
    connection: ConnectionStatus;
    connectionError?: string;
    identity?: ServiceIdentity;
    hostStatus: HostStatus;
    /** 最近一次事件帧的 seq（状态栏显示）。 */
    eventSeq: number;
    transportLabel: string;
    eventsSupported: boolean;
    resyncNote?: { reason: string; at: number };

    route: Route;

    sessions: SessionMeta[];
    messages: ReadonlyMap<string, readonly ChatMessage[]>;
    /** 每会话输入草稿。 */
    drafts: ReadonlyMap<string, string>;

    /** 契约层任务快照（RunDrawer 直连事实）。 */
    taskDetails: ReadonlyMap<string, InspectTask>;
    /** 本地提交入参记忆（wire StepView 不携带参数，展示层按 index 合并）。 */
    taskInputs: ReadonlyMap<string, readonly SubmitStepInput[]>;
    /** 正在运行（非终态）的任务 id 集合。 */
    activeTaskIds: readonly string[];

    /** 全局待决审批（批准中心 + Dock + 壁挂屏灯阵）。 */
    pendingApprovals: readonly ApprovalRequest[];
    /** 紧急停止（Human Takeover）闩锁。 */
    takeover: boolean;
    /** 本地对话模拟进行中的会话（Composer 显示「停止」）。 */
    simBusySession?: string;

    workflows: readonly WorkflowDef[];
    workflowRuns: readonly WorkflowRun[];

    context: ReadonlyMap<string, ContextUsage>;
    toasts: readonly ToastNote[];
}

export interface SubmitStepInput {
    kind: StepView['kind'];
    argument: string;
}

export interface HarnessActions {
    navigate(route: Route): void;
    newSession(mode: SessionMeta['mode']): string;
    selectSession(id: string): void;
    renameSession(id: string, title: string): void;
    pinSession(id: string, pinned: boolean): void;
    deleteSession(id: string): void;
    exportSession(id: string): void;
    setDraft(sessionId: string, text: string): void;
    /** 对话模式发送（本地模拟）。 */
    sendChat(sessionId: string, text: string): void;
    /** 执行模式提交（契约 task.submit 事实流）。 */
    submitExec(sessionId: string, goal: string, steps: SubmitStepInput[], timeoutMs?: number): Promise<void>;
    /** 停止当前会话的生成 / 取消关联任务。 */
    stopSession(sessionId: string): void;
    decideApproval(approvalId: string, approve: boolean): void;
    engageEstop(): void;
    releaseEstop(): void;
    cancelTask(taskId: string): Promise<void>;
    runWorkflow(workflowId: string): void;
    cancelWorkflowRun(runId: string): void;
    dismissToast(id: string): void;
    resync(): Promise<void>;
}

type Listener = () => void;

const now = (): number => Date.now();

export class HarnessStore {
    private state: HarnessState;
    private readonly listeners = new Set<Listener>();
    private readonly simulator: ChatSimulator;
    private unsubscribeTransport: (() => void) | null = null;
    private pollTimer: number | null = null;
    private disposed = false;

    constructor(private readonly transport: MockTransport) {
        const t = now();
        const seeded = seedSessions(t);
        const seededWf = seedWorkflows(t);
        this.state = {
            connection: 'connecting',
            hostStatus: 'starting',
            eventSeq: 0,
            transportLabel: transport.label,
            eventsSupported: transport.eventsSupported,
            route: parseRoute(window.location.hash),
            sessions: seeded.sessions,
            messages: seeded.messages,
            drafts: new Map(),
            taskDetails: new Map(),
            taskInputs: new Map(),
            activeTaskIds: [],
            pendingApprovals: [],
            takeover: false,
            workflows: seededWf.workflows,
            workflowRuns: seededWf.runs,
            context: new Map([['default', seedContext()]]),
            toasts: [],
        };
        this.simulator = new ChatSimulator({
            onThinking: (sessionId, messageId, block) => {
                this.patchMessage(sessionId, messageId, (m) =>
                    m.kind === 'assistant' ? { ...m, thinking: { ...block, text: block.text } } : m,
                );
            },
            onAssistantChunk: (sessionId, messageId, chunk) => {
                this.patchMessage(sessionId, messageId, (m) =>
                    m.kind === 'assistant'
                        ? { ...m, text: m.text + chunk, streaming: true, thinking: m.thinking }
                        : m,
                );
            },
            onAssistantDone: (sessionId, messageId) => {
                this.patchMessage(sessionId, messageId, (m) =>
                    m.kind === 'assistant' ? { ...m, streaming: false } : m,
                );
                this.touchSession(sessionId);
                if (this.state.simBusySession === sessionId) {
                    this.set({ simBusySession: undefined });
                }
            },
        });
        window.addEventListener('hashchange', this.onHashChange);
    }

    // -- 生命周期 ------------------------------------------------------------

    start(): void {
        void this.connect();
    }

    dispose(): void {
        this.disposed = true;
        window.removeEventListener('hashchange', this.onHashChange);
        this.simulator.dispose();
        this.unsubscribeTransport?.();
        if (this.pollTimer !== null) {
            window.clearInterval(this.pollTimer);
        }
    }

    // -- React 绑定 ----------------------------------------------------------

    get = (): HarnessState => this.state;

    subscribe = (listener: Listener): (() => void) => {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    };

    private set(patch: Partial<HarnessState>): void {
        this.state = { ...this.state, ...patch };
        for (const listener of this.listeners) {
            listener();
        }
    }

    // -- 连接与事件 ----------------------------------------------------------

    private async connect(): Promise<void> {
        try {
            const identity = await this.transport.hello();
            if (this.disposed) {
                return;
            }
            this.set({
                connection: 'ready',
                identity,
                hostStatus: identity.host_status,
                eventsSupported: identity.events === true,
            });
            if (identity.events === true) {
                await this.transport.subscribe((event) => {
                    switch (event.event) {
                        case 'task.updated':
                            this.set({ eventSeq: event.seq });
                            void this.onTaskEvent();
                            break;
                        case 'host.status':
                            this.set({ hostStatus: event.status, eventSeq: event.seq });
                            break;
                        case 'events.overflow':
                            this.set({
                                eventSeq: event.seq,
                                resyncNote: { reason: `溢出丢弃 ${event.dropped} 帧`, at: now() },
                            });
                            this.toast(`事件流溢出，已重新同步（丢弃 ${event.dropped} 帧）`, 'warn');
                            void this.refreshActiveTasks();
                            break;
                    }
                });
                this.unsubscribeTransport = (): void => {
                    void this.transport.unsubscribe();
                };
            } else {
                // M1.5-05 降级：无事件能力时轮询活动任务。
                this.pollTimer = window.setInterval(() => {
                    if (this.state.activeTaskIds.length > 0) {
                        void this.refreshActiveTasks();
                    }
                }, 2000);
            }
            const tasks = await this.transport.listTasks();
            if (this.disposed) {
                return;
            }
            const active = tasks
                .filter((t) => !isTerminalProgress(t.progress))
                .map((t) => t.id);
            this.set({ activeTaskIds: active });
        } catch (err) {
            if (this.disposed) {
                return;
            }
            this.set({
                connection: 'error',
                connectionError: err instanceof Error ? err.message : String(err),
            });
        }
    }

    private async onTaskEvent(): Promise<void> {
        await this.refreshActiveTasks();
    }

    /** 重取活动任务快照；终态回写线程与抽屉（幂等）。 */
    private async refreshActiveTasks(): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready') {
            return;
        }
        const ids = [...this.state.taskDetails.keys(), ...this.state.activeTaskIds];
        const unique = [...new Set(ids)];
        if (unique.length === 0) {
            return;
        }
        const details = new Map(this.state.taskDetails);
        const stillActive: string[] = [];
        for (const id of unique) {
            try {
                const detail = await this.transport.inspectTask(id);
                details.set(id, detail);
                if (!isTerminalProgress(detail.progress)) {
                    stillActive.push(id);
                } else {
                    this.recordTerminalIfNeeded(detail);
                }
            } catch (err) {
                if (err instanceof IpcRequestError && err.code === 'not_found') {
                    details.delete(id);
                }
            }
        }
        // 追加新步骤消息到关联会话（幂等：按 operation_id 去重）。
        this.set({
            taskDetails: details,
            activeTaskIds: stillActive,
        });
        for (const id of unique) {
            const detail = details.get(id);
            if (detail !== undefined) {
                this.mirrorTaskMessages(detail);
            }
        }
    }

    /** 把契约层步骤变化镜像进关联会话线程（演示审批叠加见此）。 */
    private mirrorTaskMessages(detail: InspectTask): void {
        const sessionId = this.sessionIdOfTask(detail.id);
        if (sessionId === undefined) {
            return;
        }
        const list = [...(this.state.messages.get(sessionId) ?? [])];
        const indexByOp = new Map<string, number>();
        list.forEach((m, i) => {
            if (m.kind === 'step') {
                indexByOp.set(m.step.operationId, i);
            }
        });
        const additions: ChatMessage[] = [];
        let mutated = false;
        for (const step of displayStepsOf(detail, this.state.taskInputs.get(detail.id))) {
            const at = indexByOp.get(step.operationId);
            if (at === undefined) {
                additions.push({ id: `ms-${detail.id}-${step.operationId}`, kind: 'step', at: now(), taskId: detail.id, step });
                if (step.status === 'running' && step.argument.startsWith('approve:')) {
                    const approval: ApprovalRequest = {
                        id: `ap-${detail.id}-${step.operationId}`,
                        kind: 'process.execute',
                        summary: step.argument.replace(/^approve:/, '') || '执行命令',
                        detail: step.argument,
                        status: 'pending',
                        requestedAt: now(),
                    };
                    additions.push({ id: `${approval.id}-msg`, kind: 'approval', at: now(), approval });
                    this.set({ pendingApprovals: [...this.state.pendingApprovals, approval] });
                }
                continue;
            }
            // 已有步骤卡：状态/证据变化时原地更新（迟到的终态不复活任务）。
            const target = list[at]!;
            if (target.kind === 'step') {
                const prev = target.step;
                if (
                    prev.status !== step.status ||
                    prev.result !== step.result ||
                    prev.error !== step.error ||
                    prev.exitCode !== step.exitCode
                ) {
                    list[at] = { ...target, step };
                    mutated = true;
                }
            }
        }
        const progressChanged = this.lastProgressOf(sessionId, detail.id) !== detail.progress;
        if (progressChanged) {
            additions.push({ id: `ma-${detail.id}-${detail.progress}-${detail.steps.length}`, kind: 'activity', at: now(), taskId: detail.id, progress: detail.progress });
        }
        if (additions.length > 0) {
            list.push(...additions);
            mutated = true;
        }
        if (mutated) {
            const messages = new Map(this.state.messages);
            messages.set(sessionId, list.slice(-MAX_MESSAGES_PER_SESSION));
            this.set({ messages });
        }
    }

    private recordTerminalIfNeeded(detail: InspectTask): void {
        const key = `terminal-${detail.id}`;
        if (this.announced.has(key)) {
            return;
        }
        this.announced.add(key);
        if (this.announced.size > 512) {
            this.announced.clear();
        }
        const sessionId = this.sessionIdOfTask(detail.id);
        if (sessionId === undefined) {
            return;
        }
        const summary = summarizeTask(detail);
        this.appendMessages(sessionId, [
            {
                id: `mas-${detail.id}`,
                kind: 'assistant',
                at: now(),
                text: summary,
                thinking: { text: '汇总执行证据：步骤状态、退出码与失败原因（fail-fast）。', elapsedMs: 4_000 },
            },
        ]);
    }

    private readonly announced = new Set<string>();

    private lastProgressOf(sessionId: string, taskId: string): string | undefined {
        const list = this.state.messages.get(sessionId) ?? [];
        for (let i = list.length - 1; i >= 0; i -= 1) {
            const m = list[i]!;
            if (m.kind === 'activity' && m.taskId === taskId) {
                return m.progress;
            }
        }
        return undefined;
    }

    private sessionIdOfTask(taskId: string): string | undefined {
        return this.state.sessions.find((s) => s.taskId === taskId)?.id;
    }

    async resync(): Promise<void> {
        await this.refreshActiveTasks();
        this.set({ resyncNote: { reason: '手动重新同步', at: now() } });
        this.toast('已重新同步事件流与任务快照', 'info');
    }

    // -- 路由 ----------------------------------------------------------------

    private onHashChange = (): void => {
        this.set({ route: parseRoute(window.location.hash) });
    };

    navigate: HarnessActions['navigate'] = (route) => {
        const target = routeToHash(route);
        if (window.location.hash !== target) {
            window.location.hash = target;
        } else {
            this.set({ route });
        }
    };

    // -- 会话管理 ------------------------------------------------------------

    newSession: HarnessActions['newSession'] = (mode) => {
        const meta = newSessionMeta(mode, mode === 'exec' ? '新的执行会话' : '新的对话', now());
        const sessions = [meta, ...this.state.sessions].slice(0, MAX_SESSIONS);
        const messages = new Map(this.state.messages);
        messages.set(meta.id, []);
        this.set({ sessions, messages });
        this.navigate({ view: 'chat', sessionId: meta.id });
        return meta.id;
    };

    selectSession: HarnessActions['selectSession'] = (id) => {
        this.navigate({ view: 'chat', sessionId: id });
    };

    renameSession: HarnessActions['renameSession'] = (id, title) => {
        const title2 = title.trim();
        if (title2.length === 0) {
            return;
        }
        this.set({
            sessions: this.state.sessions.map((s) => (s.id === id ? { ...s, title: title2, updatedAt: now() } : s)),
        });
    };

    pinSession: HarnessActions['pinSession'] = (id, pinned) => {
        this.set({
            sessions: this.state.sessions.map((s) => (s.id === id ? { ...s, pinned } : s)),
        });
    };

    deleteSession: HarnessActions['deleteSession'] = (id) => {
        const sessions = this.state.sessions.filter((s) => s.id !== id);
        const messages = new Map(this.state.messages);
        messages.delete(id);
        this.set({ sessions, messages });
        if (this.state.route.view === 'chat' && this.state.route.sessionId === id) {
            this.navigate({ view: 'chat' });
        }
        this.toast('会话已删除', 'info');
    };

    exportSession: HarnessActions['exportSession'] = (id) => {
        const meta = this.state.sessions.find((s) => s.id === id);
        const messages = this.state.messages.get(id) ?? [];
        if (meta === undefined) {
            return;
        }
        const markdown = exportSessionMarkdown(meta.title, messages);
        const blob = new Blob([markdown], { type: 'text/markdown;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${meta.title}.md`;
        a.click();
        URL.revokeObjectURL(url);
        this.toast('已导出会话 Markdown', 'info');
    };

    setDraft: HarnessActions['setDraft'] = (sessionId, text) => {
        const drafts = new Map(this.state.drafts);
        drafts.set(sessionId, text);
        this.set({ drafts });
    };

    // -- 对话 / 执行 ----------------------------------------------------------

    sendChat: HarnessActions['sendChat'] = (sessionId, text) => {
        const trimmed = text.trim();
        if (trimmed.length === 0 || this.simulator.busy() || this.state.takeover) {
            return;
        }
        this.appendMessages(sessionId, [{ id: `mu-${now()}`, kind: 'user', at: now(), text: trimmed }]);
        this.setDraft(sessionId, '');
        this.touchSession(sessionId);
        const assistantId = this.simulator.begin(sessionId, trimmed);
        // 流式回复以占位消息入线程：模拟器回调按 id 原地推进（thinking/分片/完成）。
        this.appendMessages(sessionId, [{ id: assistantId, kind: 'assistant', at: now(), text: '', streaming: true }]);
        this.set({ simBusySession: sessionId });
    };

    submitExec: HarnessActions['submitExec'] = async (sessionId, goal, steps, timeoutMs) => {
        if (this.state.takeover) {
            this.toast('紧急停止中：先解除 Takeover 再提交任务', 'warn');
            return;
        }
        const trimmedGoal = goal.trim();
        if (trimmedGoal.length === 0 || steps.length === 0) {
            return;
        }
        this.appendMessages(sessionId, [{ id: `mu-${now()}`, kind: 'user', at: now(), text: trimmedGoal }]);
        this.setDraft(sessionId, '');
        this.touchSession(sessionId);
        try {
            const { task_id } = await this.transport.submitTask({
                goal: trimmedGoal,
                steps: steps.map((s) => ({ op: s.kind, arg: s.argument })),
                step_timeout_ms: timeoutMs,
            });
            const sessions = this.state.sessions.map((s) =>
                s.id === sessionId ? { ...s, taskId: task_id, updatedAt: now() } : s,
            );
            const taskInputs = new Map(this.state.taskInputs);
            taskInputs.set(task_id, steps);
            this.set({
                sessions,
                taskInputs,
                activeTaskIds: [...this.state.activeTaskIds, task_id],
            });
            this.appendMessages(sessionId, [
                { id: `man-${task_id}`, kind: 'system', at: now(), tone: 'info', text: `已提交协议任务 ${task_id}（${steps.length} 个步骤，事件流实时跟中）` },
            ]);
            await this.refreshActiveTasks();
        } catch (err) {
            const message = err instanceof IpcRequestError ? `提交被拒绝（${err.code}）` : err instanceof Error ? err.message : String(err);
            this.appendMessages(sessionId, [{ id: `me-${now()}`, kind: 'system', at: now(), tone: 'error', text: `任务提交失败：${message}` }]);
        }
    };

    stopSession: HarnessActions['stopSession'] = (sessionId) => {
        this.simulator.cancel();
        const meta = this.state.sessions.find((s) => s.id === sessionId);
        if (meta?.taskId !== undefined) {
            void this.cancelTask(meta.taskId);
        }
    };

    async cancelTask(taskId: string): Promise<void> {
        try {
            await this.transport.cancelTask(taskId);
            await this.refreshActiveTasks();
        } catch (err) {
            if (!(err instanceof IpcRequestError && err.code === 'mirage.task.not_cancellable')) {
                this.toast(`取消失败：${err instanceof Error ? err.message : String(err)}`, 'error');
            }
        }
    }

    // -- 审批 / Takeover ------------------------------------------------------

    decideApproval: HarnessActions['decideApproval'] = (approvalId, approve) => {
        // 幂等门控：不存在或已决的审批不再产生任何副作用（终态幂等）。
        if (!this.state.pendingApprovals.some((a) => a.id === approvalId)) {
            return;
        }
        const decide = (a: ApprovalRequest): ApprovalRequest =>
            a.id === approvalId && a.status === 'pending'
                ? { ...a, status: approve ? 'approved' : 'denied', decidedAt: now() }
                : a;
        const pending = this.state.pendingApprovals.filter((a) => a.id !== approvalId);
        this.set({ pendingApprovals: pending });
        const messages = new Map(this.state.messages);
        for (const [sid, list] of messages) {
            const idx = list.findIndex((m) => m.kind === 'approval' && m.approval.id === approvalId);
            if (idx >= 0) {
                const target = list[idx]!;
                if (target.kind === 'approval') {
                    const updated: ChatMessage = {
                        ...target,
                        approval: decide(target.approval),
                    };
                    messages.set(sid, [...list.slice(0, idx), updated, ...list.slice(idx + 1)]);
                }
                if (!approve) {
                    messages.set(sid, [
                        ...messages.get(sid)!,
                        { id: `mn-${approvalId}`, kind: 'system', at: now(), tone: 'warn', text: '已拒止该权限请求（模拟：M2+ 权限事件面前为演示语义，协议层任务不受影响）。' },
                    ]);
                }
                break;
            }
        }
        this.set({ messages });
        this.toast(approve ? '已放行' : '已拒止', approve ? 'info' : 'warn');
    };

    engageEstop: HarnessActions['engageEstop'] = () => {
        if (this.state.takeover) {
            return;
        }
        this.simulator.cancel();
        this.set({ takeover: true });
        for (const id of [...this.state.activeTaskIds]) {
            void this.transport.cancelTask(id).catch(() => undefined);
        }
        this.toast('紧急停止：已请求取消全部运行中任务，暂停自动动作', 'error');
    };

    releaseEstop: HarnessActions['releaseEstop'] = () => {
        if (!this.state.takeover) {
            return;
        }
        this.set({ takeover: false });
        this.toast('Takeover 已解除；恢复前建议重新观察环境', 'info');
    };

    // -- 工作流（模拟域） ------------------------------------------------------

    runWorkflow: HarnessActions['runWorkflow'] = (workflowId) => {
        const wf = this.state.workflows.find((w) => w.id === workflowId);
        if (wf === undefined) {
            return;
        }
        const run: WorkflowRun = {
            id: `r-${now().toString(36)}`,
            workflowId,
            workflowName: wf.name,
            status: 'running',
            startedAt: now(),
            steps: wf.steps.map((s) => ({ title: s.title, status: 'pending' })),
        };
        this.set({
            workflowRuns: [run, ...this.state.workflowRuns],
            workflows: this.state.workflows.map((w) =>
                w.id === workflowId ? { ...w, lastRunAt: run.startedAt, lastRunStatus: 'running' } : w,
            ),
        });
        this.advanceRun(run.id, 0);
        this.navigate({ view: 'workflow-run', workflowId, runId: run.id });
        this.toast(`已启动工作流「${wf.name}」（模拟运行）`, 'info');
    };

    private advanceRun(runId: string, index: number): void {
        if (this.disposed || this.state.takeover) {
            return;
        }
        if (index > 0) {
            const mark = (run: WorkflowRun): WorkflowRun => {
                if (run.id !== runId) {
                    return run;
                }
                const steps = [...run.steps];
                const prev = steps[index - 1];
                if (prev !== undefined && prev.status === 'running') {
                    steps[index - 1] = { ...prev, status: 'ok', durationMs: 900 + Math.round(Math.random() * 4200) };
                }
                const current = steps[index];
                if (current !== undefined) {
                    steps[index] = { ...current, status: 'running', log: current.log ?? '运行中…' };
                }
                return { ...run, steps };
            };
            this.set({ workflowRuns: this.state.workflowRuns.map(mark) });
        } else {
            const start = (run: WorkflowRun): WorkflowRun =>
                run.id === runId
                    ? { ...run, steps: run.steps.map((s, i) => (i === 0 ? { ...s, status: 'running' as const } : s)) }
                    : run;
            this.set({ workflowRuns: this.state.workflowRuns.map(start) });
        }
        const isLast = index + 1 >= (this.state.workflowRuns.find((r) => r.id === runId)?.steps.length ?? 0);
        window.setTimeout(() => {
            if (this.disposed || this.state.takeover) {
                return;
            }
            if (!isLast) {
                this.advanceRun(runId, index + 1);
            } else {
                const finish = (run: WorkflowRun): WorkflowRun => {
                    if (run.id !== runId) {
                        return run;
                    }
                    const steps = [...run.steps];
                    const last = steps[steps.length - 1];
                    if (last !== undefined && last.status === 'running') {
                        steps[steps.length - 1] = { ...last, status: 'ok', durationMs: 1_800 };
                    }
                    return { ...run, status: 'completed', steps };
                };
                this.set({
                    workflowRuns: this.state.workflowRuns.map(finish),
                    workflows: this.state.workflows.map((w) =>
                        w.id === (this.state.workflowRuns.find((r) => r.id === runId)?.workflowId)
                            ? { ...w, lastRunStatus: 'completed' }
                            : w,
                    ),
                });
            }
        }, 1600 + Math.round(Math.random() * 1800));
    }

    cancelWorkflowRun: HarnessActions['cancelWorkflowRun'] = (runId) => {
        const cancel = (run: WorkflowRun): WorkflowRun =>
            run.id === runId && run.status === 'running'
                ? { ...run, status: 'cancelled', steps: run.steps.map((s) => (s.status === 'running' || s.status === 'pending' ? { ...s, status: 'skipped' as const } : s)) }
                : run;
        this.set({ workflowRuns: this.state.workflowRuns.map(cancel) });
        this.toast('已请求取消该运行', 'warn');
    };

    // -- Toast ---------------------------------------------------------------

    dismissToast: HarnessActions['dismissToast'] = (id) => {
        this.set({ toasts: this.state.toasts.filter((t) => t.id !== id) });
    };

    toast(text: string, tone: ToastNote['tone']): void {
        const note: ToastNote = { id: uidToast(), text, tone, at: now() };
        this.set({ toasts: [...this.state.toasts, note].slice(-4) });
        window.setTimeout(() => this.dismissToast(note.id), 4200);
    }

    // -- 内部工具 -------------------------------------------------------------

    private touchSession(sessionId: string): void {
        this.set({
            sessions: this.state.sessions.map((s) => (s.id === sessionId ? { ...s, updatedAt: now() } : s)),
        });
    }

    private appendMessages(sessionId: string, additions: readonly ChatMessage[]): void {
        const messages = new Map(this.state.messages);
        const list = [...(messages.get(sessionId) ?? []), ...additions];
        messages.set(sessionId, list.slice(-MAX_MESSAGES_PER_SESSION));
        this.set({ messages });
    }

    private patchMessage(
        sessionId: string,
        messageId: string,
        patch: (m: ChatMessage) => ChatMessage,
    ): void {
        const messages = new Map(this.state.messages);
        const list = messages.get(sessionId);
        if (list === undefined) {
            return;
        }
        const idx = list.findIndex((m) => m.id === messageId);
        if (idx < 0) {
            return;
        }
        const updated = [...list];
        updated[idx] = patch(updated[idx]!);
        messages.set(sessionId, updated);
        this.set({ messages });
    }

    actions(): HarnessActions {
        return {
            navigate: this.navigate,
            newSession: this.newSession,
            selectSession: this.selectSession,
            renameSession: this.renameSession,
            pinSession: this.pinSession,
            deleteSession: this.deleteSession,
            exportSession: this.exportSession,
            setDraft: this.setDraft,
            sendChat: this.sendChat,
            submitExec: this.submitExec,
            stopSession: this.stopSession,
            decideApproval: this.decideApproval,
            engageEstop: this.engageEstop,
            releaseEstop: this.releaseEstop,
            cancelTask: (taskId) => this.cancelTask(taskId),
            runWorkflow: this.runWorkflow,
            cancelWorkflowRun: this.cancelWorkflowRun,
            dismissToast: this.dismissToast,
            resync: () => this.resync(),
        };
    }
}

let toastSeq = 0;
function uidToast(): string {
    toastSeq += 1;
    return `toast-${toastSeq}`;
}

/** 把契约 StepView 与本地提交入参合并为展示步骤（按 index 对齐）。 */
export function displayStepsOf(
    detail: InspectTask,
    inputs?: readonly SubmitStepInput[],
): StepDisplay[] {
    return detail.steps.map((s) => ({
        index: s.index,
        kind: s.kind,
        status: s.status,
        operationId: s.operation_id,
        argument: inputs?.[s.index]?.argument ?? '',
        exitCode: s.exit_code,
        result: s.result,
        resultTruncated: s.result_truncated,
        error: s.error,
    }));
}

export function isTerminalProgress(progress: TaskProgress | string): boolean {
    return progress === 'Completed' || progress === 'Failed' || progress === 'Cancelled';
}

function summarizeTask(detail: InspectTask): string {
    const ok = detail.steps.filter((s) => s.status === 'ok').length;
    const failed = detail.steps.filter((s) => s.status === 'failed').length;
    switch (detail.progress) {
        case 'Completed':
            return `任务完成：${ok}/${detail.steps.length} 个步骤成功。` + (detail.has_success && detail.success === true ? '' : '');
        case 'Failed':
            return `任务失败（fail-fast）：${failed} 个步骤失败，未开始的步骤已跳过。首个失败步骤见上方卡片，可修正后重试。`;
        case 'Cancelled':
            return '任务已取消：运行中的步骤被中断，未开始的步骤被跳过。';
        case 'Cancelling':
            return '正在取消…';
        default:
            return `任务状态：${detail.progress}`;
    }
}
