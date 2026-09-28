/// Harness 应用状态（React 侧唯一事实源）。
///
/// 分层纪律：
/// - 契约事实（host 身份、任务状态机、步骤、事件 seq/overflow、会话投影、
///   工作流）只来自 `MirageTransport`（mock 或真实传输，视图不感知）；事件
///   仅触发快照重取（DEC-012 决策 4：事件是通知，不是状态本体）。
/// - 事件纪律：EventSequencer 跟踪每连接 seq；seq 跳跃 / overflow 触发快照
///   resync 并留显式记录。订阅不可用（hello 无 `events` 能力或 subscribe 返回
///   `unsupported`）时自动降级为 `task.inspect` 轮询。连接断开后按有界退避
///   自动重连（需 transport 工厂），成功后重置 seq 基线并 resync。
/// - 会话 / 消息 / 工作流全部为契约路径（DEC-025：模拟域退出会话页）。
///   会话标题与任务徽标是展示层派生事实；wire 没有的管理面（重命名 / 置顶 /
///   删除 / 导出 / fork）不呈现。
/// - 终态幂等：已取消 / 已完成任务不因迟到事件复活。

import type {
    ChatTurnEntry,
    HostStatus,
    InspectTask,
    MirageTransport,
    ObservationView,
    PendingPermission,
    PolicyView,
    ServerEvent,
    ServiceIdentity,
    StepView,
    TaskProgress,
} from '@mirage/contracts';
import { EventSequencer, IpcRequestError } from '@mirage/contracts';
import { kindLabel, stepStatusLabel } from '../lib/labels.js';
import { CONTROL_CONSTRUCTS, IpcWorkflowBackend, newWorkflowId } from './workflow-backend.js';
import type { WorkflowAtom } from './workflow-backend.js';
import { workflowDefToIr } from './workflow-ir.js';
import type {
    ChatMessage,
    ObsFrame,
    SessionMeta,
    StepDisplay,
    ToastNote,
    WorkflowDef,
    WorkflowParam,
    WorkflowRun,
    WorkflowPredicateView,
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

/** 每会话观察流缓冲上限（通知面尾随，DEC-025 决策 5）。 */
export const OBS_FEED_CAPACITY = 40;
/** 每会话线程缓冲上限（本地视图预算，非契约面）。 */
export const MAX_MESSAGES_PER_SESSION = 200;

export interface HarnessState {
    connection: ConnectionStatus;
    connectionError?: string;
    identity?: ServiceIdentity;
    hostStatus: HostStatus;
    /** 最近一次事件帧的 seq（状态栏显示）。 */
    eventSeq: number;
    transportLabel: string;
    eventsSupported: boolean;
    /** DEC-023 工作流能力位（hello `workflows`；false 时工作流页不可用）。 */
    workflowsSupported: boolean;
    /** DEC-021 会话面能力位（hello `sessions`；false 时会话页呈现不可用）。 */
    sessionsSupported: boolean;
    /** DEC-026 观察面能力位（hello `observation`；false 时桌面状态面板不可用）。 */
    observationSupported: boolean;
    /** DEC-027 对话面能力位（hello `chat`；模型层已配置时为 true）。 */
    chatSupported: boolean;
    /** M5-07 策略面能力位（hello `policy`；恒 true 但保留位判别）。 */
    policySupported: boolean;
    /** M5-07 待批准确认（DEC-020 异步确认面；permission.list 为事实源）。 */
    pendingApprovals: readonly PendingPermission[];
    /** M5-07 策略面视图（policy.get / policy.set 的事实源）；undefined = 未
     * 读取（连接初期或策略面缺席）。 */
    policy?: PolicyView;
    /** 会话显示别名（DEC-026 挂账⑤落地：展示层产品状态，localStorage 持
     * 久化——非服务端条目，wire 无标题成员）。 */
    sessionAliases: ReadonlyMap<string, string>;
    /** DEC-020 异步确认面能力位（hello `permissions`；hub 已配置时为 true）。 */
    permissionsSupported: boolean;
    /** 每会话在途对话轮 turn id（session.chat 已受理、未结算）。 */
    pendingChats: ReadonlyMap<string, string>;
    resyncNote?: { reason: string; at: number };

    route: Route;

    sessions: readonly SessionMeta[];
    messages: ReadonlyMap<string, readonly ChatMessage[]>;
    /** 每会话 `session.history` 是否存在更早条目（truncated 投影）。 */
    historyTruncated: ReadonlyMap<string, boolean>;
    /** 每会话输入草稿。 */
    drafts: ReadonlyMap<string, string>;

    /** 契约层任务快照（RunDrawer 直连事实）。 */
    taskDetails: ReadonlyMap<string, InspectTask>;
    /** 本地提交入参记忆（wire StepView 不携带参数，展示层按 index 合并）。 */
    taskInputs: ReadonlyMap<string, readonly SubmitStepInput[]>;
    /** 正在运行（非终态）的任务 id 集合。 */
    activeTaskIds: readonly string[];

    /** 观察流（每会话有界帧缓冲；session.turn / output / message 尾随）。 */
    obsFeed: ReadonlyMap<string, readonly ObsFrame[]>;

    /** 桌面状态观察（DEC-026 观察面：desktop.observe 按需快照，无事件形态）。
     * visualRequested 记录成功视图的视觉组件请求状态；错误呈现稳定错误串。 */
    observation: {
        status: 'idle' | 'loading' | 'ready' | 'error';
        visualRequested: boolean;
        view?: ObservationView;
        error?: string;
    };

    /** 紧急停止（Human Takeover）闩锁。 */
    takeover: boolean;

    workflows: readonly WorkflowDef[];
    workflowRuns: readonly WorkflowRun[];
    /** 原子动作目录（wire exposed view + 编辑器控制构造，RPA 右栏拖入源）。 */
    atoms: readonly WorkflowAtom[];

    toasts: readonly ToastNote[];
}

export interface SubmitStepInput {
    kind: StepView['kind'];
    argument: string;
}

export interface HarnessActions {
    navigate(route: Route): void;
    /** 新建会话（session.open；容量饱和显式失败）。 */
    newSession(): void;
    selectSession(id: string): void;
    /** 删除会话（session.close，DEC-026 挂账②兑现）：关闭并移除注册表条目，
     * 关联任务由服务端取消；主会话被服务端以 invalid_state 拒绝。 */
    deleteSession(id: string): void;
    /** 会话重命名（DEC-026 挂账⑤落地）：展示层别名，localStorage 持久化；
     * 空名清除别名回落派生标题。 */
    renameSession(id: string, title: string): void;
    setDraft(sessionId: string, text: string): void;
    /** 执行模式提交（task.submit 会话绑定事实流）。 */
    submitExec(sessionId: string, goal: string, steps: SubmitStepInput[], timeoutMs?: number): Promise<void>;
    /** 对话模式提交（session.chat 对话面，DEC-027）。 */
    sendDialog(sessionId: string, text: string): Promise<void>;
    /** 批准中心（DEC-020 异步确认面）：响应一条挂起确认（先到先得）。 */
    respondApproval(requestId: string, approved: boolean): Promise<void>;
    /** 重取挂起确认快照（permission.list）。 */
    loadApprovals(): Promise<void>;
    /** 设置页策略面（M5-07）：应用新规则集（立即生效 + 持久化）。 */
    setPolicy(rules: Record<string, string>, readRoots?: string[]): Promise<void>;
    /** 停止会话关联任务。 */
    stopSession(sessionId: string): void;
    engageEstop(): void;
    releaseEstop(): void;
    cancelTask(taskId: string): Promise<void>;
    /** 桌面状态观察（DEC-026：desktop.observe 按需快照）。 */
    refreshObservation(visual: boolean): void;
    runWorkflow(workflowId: string): void;
    cancelWorkflowRun(runId: string): void;
    /** 打开编辑器：无内容副本的定义先经 workflow.get 回读（DEC-026）。 */
    openWorkflowEditor(workflowId: string): void;
    /** RPA 编辑器：新建/改名/删除/发布/导出与步骤序列变更（草稿即改即存）。 */
    createWorkflow(): void;
    renameWorkflow(id: string, name: string): void;
    setWorkflowDescription(id: string, description: string): void;
    setWorkflowParams(id: string, params: WorkflowParam[]): void;
    deleteWorkflow(id: string): void;
    publishWorkflow(id: string): void;
    mutateSteps(id: string, mutate: (steps: WorkflowDef['steps']) => WorkflowDef['steps']): void;
    setStepParams(id: string, index: number, params: Record<string, string>): void;
    setStepSkipIf(id: string, index: number, skipIf: WorkflowPredicateView | undefined): void;
    setStepLoopMax(id: string, index: number, loopMax: number | undefined): void;
    exportWorkflowJson(id: string): void;
    dismissToast(id: string): void;
    resync(): Promise<void>;
}

type Listener = () => void;

const now = (): number => Date.now();

const SESSION_ALIASES_KEY = 'mirage.session-aliases';

/** 会话别名持久化（DEC-026 挂账⑤：展示层产品状态，localStorage 承载——
 * 跨刷新存活；wire 无标题成员，不进任何契约面）。 */
function loadSessionAliases(): Map<string, string> {
    const out = new Map<string, string>();
    try {
        const raw = window.localStorage.getItem(SESSION_ALIASES_KEY);
        if (raw !== null) {
            for (const [key, value] of Object.entries(JSON.parse(raw) as Record<string, string>)) {
                out.set(key, value);
            }
        }
    } catch {
        // 损坏的本地状态按空别名继续（展示层事实，可重建）。
    }
    return out;
}

function saveSessionAliases(map: ReadonlyMap<string, string>): void {
    try {
        window.localStorage.setItem(SESSION_ALIASES_KEY, JSON.stringify(Object.fromEntries(map)));
    } catch {
        // 存储配额/隐私模式：别名退化为会话内状态，不影响契约事实。
    }
}


/** 降级轮询周期（事件能力不可用时以 task.inspect 刷新活动任务）。 */
const POLL_INTERVAL_MS = 2000;
/** 断线重连退避：500ms 起倍增，封顶 8s（开发期工具，不无限加速）。 */
const RECONNECT_BASE_MS = 500;
const RECONNECT_MAX_MS = 8000;

/** 线程内会话日志条目的 id 前缀（session.history 重建的去重锚点）。 */
const JOURNAL_PREFIX = 'jr-';

/** 派生会话标题：首条 user 消息文本（截断），缺省用会话 id 前缀（DEC-025）。 */
export function deriveSessionTitle(id: string, firstUserText: string | undefined): string {
    const text = (firstUserText ?? '').replace(/\s+/g, ' ').trim();
    if (text.length === 0) {
        return `会话 ${id.slice(0, 8)}`;
    }
    return text.length > 32 ? `${text.slice(0, 32)}…` : text;
}

function journalMessageId(sessionId: string, sequence: number): string {
    return `${JOURNAL_PREFIX}${sessionId}-${sequence}`;
}

/** 对话轮快照条目 → 线程投影（用户行 + 助手行，DEC-027）。 */
function mapChatTurn(turn: ChatTurnEntry): ChatMessage[] {
    const at = turn.recorded_at_ms;
    const out: ChatMessage[] = [
        { id: `dlg-u-${turn.turn_id}`, kind: 'user', at, text: turn.user_text },
    ];
    if (turn.status === 'failed') {
        out.push({
            id: `dlg-a-${turn.turn_id}`,
            kind: 'assistant',
            at,
            turnId: turn.turn_id,
            status: 'failed',
            text: turn.error ?? '对话轮失败',
            userText: turn.user_text,
        });
    } else {
        out.push({
            id: `dlg-a-${turn.turn_id}`,
            kind: 'assistant',
            at,
            turnId: turn.turn_id,
            status: turn.status,
            text: turn.reply_text ?? '',
            userText: turn.user_text,
        });
    }
    return out;
}

/** outcome 句式的语气投影（`loop settled: <progress> (steps N)`，DEC-021）。 */
export function outcomeTone(text: string): 'info' | 'warn' | 'error' {
    if (text.includes('Completed')) {
        return 'info';
    }
    if (text.includes('Cancelled')) {
        return 'warn';
    }
    return 'error';
}

export class HarnessStore {
    private state: HarnessState;
    private readonly listeners = new Set<Listener>();
    private transport: MirageTransport;
    /** 工作流后端缝（DEC-023 IPC 适配器；随传输重建）。 */
    private workflowBackend: IpcWorkflowBackend;
    private readonly reconnectFactory: (() => MirageTransport) | null;
    private unsubscribeTransport: (() => void) | null = null;
    private pollTimer: number | null = null;
    private reconnectTimer: number | null = null;
    private reconnectAttempt = 0;
    /** 事件 seq 纪律（DEC-012 决策 4）：seq 跳跃/overflow 触发快照 resync；
     * 每条连接（含重连后的新连接）重置基线。 */
    private sequencer = new EventSequencer();
    private disposed = false;
    private systemSeq = 0;

    constructor(transport: MirageTransport, options: { reconnect?: () => MirageTransport } = {}) {
        this.transport = transport;
        this.workflowBackend = new IpcWorkflowBackend(transport);
        this.reconnectFactory = options.reconnect ?? null;
        this.state = {
            connection: 'connecting',
            hostStatus: 'starting',
            eventSeq: 0,
            transportLabel: transport.label,
            eventsSupported: transport.eventsSupported,
            workflowsSupported: transport.workflowsSupported,
            sessionsSupported: transport.sessionsSupported,
            observationSupported: transport.observationSupported,
            chatSupported: transport.chatSupported,
            policySupported: transport.policySupported,
            permissionsSupported: transport.permissionsSupported,
            pendingApprovals: [],
            sessionAliases: loadSessionAliases(),
            pendingChats: new Map(),
            route: parseRoute(window.location.hash),
            sessions: [],
            messages: new Map(),
            historyTruncated: new Map(),
            drafts: new Map(),
            taskDetails: new Map(),
            taskInputs: new Map(),
            activeTaskIds: [],
            obsFeed: new Map(),
            observation: { status: 'idle', visualRequested: false },
            takeover: false,
            workflows: [],
            workflowRuns: [],
            atoms: [],
            toasts: [],
        };
        window.addEventListener('hashchange', this.onHashChange);
    }

    // -- 生命周期 ------------------------------------------------------------

    start(): void {
        void this.establish(false);
    }

    dispose(): void {
        this.disposed = true;
        window.removeEventListener('hashchange', this.onHashChange);
        this.unsubscribeTransport?.();
        this.stopPolling();
        if (this.reconnectTimer !== null) {
            window.clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        void this.transport.close().catch(() => undefined);
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

    /** 建立连接（hello → 订阅或降级轮询 → 首次快照）。`resume` 为重连
     * 路径：成功后重置 seq 基线、刷新快照并留 resync 记录。 */
    private async establish(resume: boolean): Promise<void> {
        const transport = this.transport;
        try {
            transport.onConnectionLost?.(this.onConnectionLost);
            this.workflowBackend = new IpcWorkflowBackend(transport);
            const identity = await transport.hello();
            if (this.disposed) {
                return;
            }
            this.sequencer = new EventSequencer();
            this.set({
                connection: 'ready',
                connectionError: undefined,
                identity,
                hostStatus: identity.host_status,
                eventsSupported: identity.events === true,
                workflowsSupported: identity.workflows === true,
                sessionsSupported: identity.sessions === true,
                observationSupported: identity.observation === true,
                chatSupported: identity.chat === true,
                policySupported: identity.policy === true,
                permissionsSupported: identity.permissions === true,
            });
            if (resume) {
                this.reconnectAttempt = 0;
            }
            let subscribed = false;
            if (identity.events === true) {
                try {
                    await transport.subscribe(this.onTransportEvent);
                    subscribed = true;
                    this.unsubscribeTransport = (): void => {
                        transport.unsubscribe().catch(() => undefined);
                    };
                } catch (err) {
                    // 订阅被明确拒止（unsupported）：按降级处理；其余错误仍是
                    // 连接级失败，走 catch 的显式失败路径。
                    if (!(err instanceof IpcRequestError && err.code === 'unsupported')) {
                        throw err;
                    }
                }
            }
            if (!subscribed) {
                this.set({ eventsSupported: false });
                this.startPolling();
                if (identity.events === true) {
                    this.toast('事件订阅不可用（unsupported），已降级为 task.inspect 轮询', 'warn');
                } else if (!resume) {
                    this.toast('服务未提供事件能力，已降级为 task.inspect 轮询', 'info');
                }
            }
            const tasks = await transport.listTasks();
            if (this.disposed) {
                return;
            }
            const active = tasks
                .filter((t) => !isTerminalProgress(t.progress))
                .map((t) => t.id);
            this.set({ activeTaskIds: active });
            await this.refreshSessions();
            await this.refreshWorkflows();
            const routeSession = this.state.route.view === 'chat' ? this.state.route.sessionId : undefined;
            if (routeSession !== undefined) {
                await this.loadHistory(routeSession);
                await this.loadChatHistory(routeSession);
            }
            await this.refreshPolicy();
            await this.loadApprovals();
            if (resume) {
                await this.refreshActiveTasks();
                this.set({ resyncNote: { reason: '重连后重新同步任务快照', at: now() } });
            }
        } catch (err) {
            if (this.disposed) {
                return;
            }
            this.set({
                connection: 'error',
                connectionError: err instanceof Error ? err.message : String(err),
            });
            this.scheduleReconnect();
        }
    }

    /** 连接意外断开（hello 成功之后）：有工厂则进入重连循环（成功后 resync），
     * 无工厂则显式失败。 */
    private readonly onConnectionLost = (): void => {
        if (this.disposed) {
            return;
        }
        this.unsubscribeTransport = null;
        this.stopPolling();
        if (this.reconnectFactory === null) {
            this.set({ connection: 'error', connectionError: '连接中断' });
            return;
        }
        this.set({ connection: 'connecting', connectionError: '连接中断，正在重连…' });
        this.scheduleReconnect();
    };

    private scheduleReconnect(): void {
        const factory = this.reconnectFactory;
        if (this.disposed || factory === null || this.reconnectTimer !== null) {
            return;
        }
        const delay = Math.min(RECONNECT_BASE_MS * 2 ** Math.min(this.reconnectAttempt, 4), RECONNECT_MAX_MS);
        this.reconnectAttempt += 1;
        this.reconnectTimer = window.setTimeout(() => {
            this.reconnectTimer = null;
            if (this.disposed) {
                return;
            }
            this.transport = factory();
            void this.establish(true);
        }, delay);
    }

    private startPolling(): void {
        if (this.pollTimer !== null) {
            return;
        }
        this.pollTimer = window.setInterval(() => {
            if (this.state.activeTaskIds.length > 0) {
                void this.refreshActiveTasks();
            }
            // 无事件能力时的运行监控兜底：仍有未终态运行则刷新工作流快照。
            if (
                this.state.workflowsSupported &&
                this.state.workflowRuns.some((r) => r.status === 'running' || r.status === 'queued' || r.status === 'paused')
            ) {
                void this.refreshWorkflows();
            }
        }, POLL_INTERVAL_MS);
    }

    private stopPolling(): void {
        if (this.pollTimer !== null) {
            window.clearInterval(this.pollTimer);
            this.pollTimer = null;
        }
    }

    private readonly onTransportEvent = (event: ServerEvent): void => {
        const verdict = this.sequencer.push(event);
        switch (event.event) {
            case 'task.updated':
                this.set({ eventSeq: event.seq });
                break;
            case 'host.status':
                this.set({ hostStatus: event.status, eventSeq: event.seq });
                break;
            case 'workflow.run_updated':
                this.set({ eventSeq: event.seq });
                // workflow.runs 是运行快照事实源（DEC-023）：事件只做通知，
                // 状态以快照刷新回写。
                void this.refreshWorkflows();
                break;
            case 'session.chat_updated':
                this.set({ eventSeq: event.seq });
                this.onChatTurnUpdated(event);
                break;
            case 'session.updated':
                this.set({ eventSeq: event.seq });
                void this.refreshSessions();
                break;
            case 'session.message':
                this.set({ eventSeq: event.seq });
                this.onSessionMessage(event.session_id, event.kind, event.text, event.sequence);
                break;
            case 'session.turn':
                this.set({ eventSeq: event.seq });
                this.onSessionTurn(event.session_id, event.step, event.kind, event.status);
                break;
            case 'session.output':
                this.set({ eventSeq: event.seq });
                this.onSessionOutput(event.session_id, event.step, event.chunk, event.truncated);
                break;
            case 'permission.request': {
                this.set({ eventSeq: event.seq });
                // DEC-020：请求事件是通知，快照以 permission.list 重取（预算
                // 倒计时也来自快照），这里触发重取即可。
                void this.loadApprovals();
                break;
            }
            case 'events.overflow':
                break;
        }
        if (verdict.kind === 'resync') {
            const reason =
                verdict.reason === 'overflow'
                    ? `溢出丢弃 ${verdict.dropped} 帧`
                    : `事件序号跳跃（收到 seq ${event.seq}）`;
            this.set({ eventSeq: event.seq, resyncNote: { reason, at: now() } });
            this.toast(`事件流不连续（${reason}），已重新同步快照`, 'warn');
            void this.resyncSnapshots();
            return;
        }
        if (event.event === 'task.updated') {
            void this.refreshActiveTasks();
        }
    };

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
                }
            } catch (err) {
                if (err instanceof IpcRequestError && err.code === 'not_found') {
                    details.delete(id);
                }
            }
        }
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

    /** 把契约层步骤变化镜像进关联会话线程（步骤证据的快照事实源）。 */
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

    /** resync 纪律：任务快照 + 会话列表 + 工作流快照 + 打开会话的历史。 */
    private async resyncSnapshots(): Promise<void> {
        await this.refreshActiveTasks();
        await this.refreshSessions();
        await this.refreshWorkflows();
        const routeSession = this.state.route.view === 'chat' ? this.state.route.sessionId : undefined;
        if (routeSession !== undefined) {
            await this.loadHistory(routeSession);
        }
    }

    async resync(): Promise<void> {
        await this.resyncSnapshots();
        this.set({ resyncNote: { reason: '手动重新同步', at: now() } });
        this.toast('已重新同步事件流与快照', 'info');
    }

    // -- 会话面（DEC-021 / DEC-025） ------------------------------------------

    /** 会话列表快照：session.list 为事实源；派生字段（标题 / 最近活动 /
     * 任务绑定）对已知会话保留本地记忆。 */
    private async refreshSessions(): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready' || !this.state.sessionsSupported) {
            return;
        }
        try {
            const summaries = await this.transport.listSessions();
            if (this.disposed) {
                return;
            }
            const merged = summaries.map((summary): SessionMeta => {
                const prev = this.state.sessions.find((s) => s.id === summary.id);
                const list = this.state.messages.get(summary.id) ?? [];
                const firstUser = list.find((m): m is Extract<ChatMessage, { kind: 'user' }> => m.kind === 'user');
                const alias = this.state.sessionAliases.get(summary.id);
                return {
                    id: summary.id,
                    state: summary.state,
                    createdAt: summary.created_at_ms,
                    title: alias ?? prev?.title ?? deriveSessionTitle(summary.id, firstUser?.text),
                    lastActivityAt: prev?.lastActivityAt ?? summary.created_at_ms,
                    taskId: prev?.taskId,
                };
            });
            this.set({ sessions: merged });
        } catch (err) {
            this.toast(`会话列表刷新失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    /** 以 session.history（重同步快照事实源）重建线程的会话日志部分；
     * 步骤 / 活动 / 系统行保留，随后按时间归位。幂等：按 sequence 去重。 */
    private async loadHistory(sessionId: string): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready' || !this.state.sessionsSupported) {
            return;
        }
        try {
            const history = await this.transport.sessionHistory({ session_id: sessionId });
            if (this.disposed) {
                return;
            }
            const entries: ChatMessage[] = history.entries.map((entry) => ({
                id: journalMessageId(sessionId, entry.sequence),
                kind: entry.kind,
                at: entry.recorded_at_ms,
                text: entry.text,
                sequence: entry.sequence,
            }));
            // 快照重建：journal 条目以历史投影为准（幂等），步骤 / 活动 /
            // 系统行保留，随后按时间归位。
            const kept = (this.state.messages.get(sessionId) ?? []).filter(
                (m) => m.kind === 'step' || m.kind === 'activity' || m.kind === 'system',
            );
            const merged = [...kept, ...entries].sort((a, b) => a.at - b.at);
            const messages = new Map(this.state.messages);
            messages.set(sessionId, merged.slice(-MAX_MESSAGES_PER_SESSION));
            const historyTruncated = new Map(this.state.historyTruncated);
            historyTruncated.set(sessionId, history.truncated);
            this.set({ messages, historyTruncated });
            this.retitleFromMessages(sessionId);
        } catch (err) {
            if (err instanceof IpcRequestError && err.code === 'not_found') {
                // 未知会话（服务重启后注册表易失）：列表刷新会收敛视图。
                void this.refreshSessions();
                return;
            }
            this.toast(`会话历史加载失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    /** 用线程首条 user 消息刷新派生标题（幂等）。 */
    private retitleFromMessages(sessionId: string): void {
        const list = this.state.messages.get(sessionId) ?? [];
        const firstUser = list.find((m): m is Extract<ChatMessage, { kind: 'user' }> => m.kind === 'user');
        if (firstUser === undefined) {
            return;
        }
        const title = deriveSessionTitle(sessionId, firstUser.text);
        if (this.state.sessions.some((s) => s.id === sessionId && s.title !== title)) {
            this.set({
                sessions: this.state.sessions.map((s) => (s.id === sessionId ? { ...s, title } : s)),
            });
        }
    }

    private touchSession(sessionId: string): void {
        if (!this.state.sessions.some((s) => s.id === sessionId)) {
            return;
        }
        this.set({
            sessions: this.state.sessions.map((s) =>
                s.id === sessionId ? { ...s, lastActivityAt: now() } : s,
            ),
        });
    }

    newSession: HarnessActions['newSession'] = () => {
        if (!this.ensureSessions()) {
            return;
        }
        void this.transport
            .openSession()
            .then(({ session_id }) => {
                void this.refreshSessions();
                this.navigate({ view: 'chat', sessionId: session_id });
            })
            .catch((err: unknown) => {
                const message =
                    err instanceof IpcRequestError
                        ? err.code === 'unavailable'
                            ? '会话容量已满，无法新建（服务端显式拒绝）'
                            : `新建被拒绝（${err.code}）`
                        : err instanceof Error
                          ? err.message
                          : String(err);
                this.toast(message, 'error');
            });
    };

    renameSession: HarnessActions['renameSession'] = (id, title) => {
        const trimmed = title.trim();
        const aliases = new Map(this.state.sessionAliases);
        if (trimmed.length === 0) {
            aliases.delete(id);
        } else {
            aliases.set(id, trimmed);
        }
        saveSessionAliases(aliases);
        this.set({
            sessionAliases: aliases,
            sessions: this.state.sessions.map((s) =>
                s.id === id
                    ? { ...s, title: trimmed.length > 0 ? trimmed : deriveSessionTitle(id, undefined) }
                    : s,
            ),
        });
    };

    selectSession: HarnessActions['selectSession'] = (id) => {
        this.navigate({ view: 'chat', sessionId: id });
        void this.loadHistory(id);
        void this.loadChatHistory(id);
    };

    /** 删除会话（session.close，DEC-026 挂账②兑现）：服务端关闭会话、取消
     * 其关联任务并移除注册表条目；本地线程 / 草稿 / 观察流 / 提交入参记忆
     * 随注册表事实收敛清空。主会话被服务端以 invalid_state 拒绝（task.submit
     * 默认绑定锚点），稳定错误如实呈现。 */
    deleteSession: HarnessActions['deleteSession'] = (sessionId) => {
        if (!this.ensureSessions()) {
            return;
        }
        void this.transport
            .closeSession(sessionId)
            .then(() => {
                // 本地记忆随注册表事实收敛清空（线程 / 草稿 / 截断标记 /
                // 观察流 / 提交入参）。
                const messages = new Map(this.state.messages);
                messages.delete(sessionId);
                const drafts = new Map(this.state.drafts);
                drafts.delete(sessionId);
                const historyTruncated = new Map(this.state.historyTruncated);
                historyTruncated.delete(sessionId);
                const obsFeed = new Map(this.state.obsFeed);
                obsFeed.delete(sessionId);
                const taskInputs = new Map(this.state.taskInputs);
                taskInputs.delete(sessionId);
                this.set({
                    messages,
                    drafts,
                    historyTruncated,
                    obsFeed,
                    taskInputs,
                    sessions: this.state.sessions.filter((s) => s.id !== sessionId),
                });
                if (this.state.route.view === 'chat' && this.state.route.sessionId === sessionId) {
                    this.navigate({ view: 'chat' });
                }
                this.toast('会话已关闭并从列表移除（关联任务已取消）', 'info');
            })
            .catch((err: unknown) => {
                const message =
                    err instanceof IpcRequestError
                        ? err.code === 'invalid_state'
                            ? '主会话不可关闭（task.submit 的默认提交会话）'
                            : `删除被拒绝（${err.code}）`
                        : err instanceof Error
                          ? err.message
                          : String(err);
                this.toast(message, 'error');
            });
    };

    private ensureSessions(): boolean {
        if (!this.state.sessionsSupported) {
            this.toast('服务未提供会话面（hello 无 sessions 位）', 'warn');
            return false;
        }
        return true;
    }

    setDraft: HarnessActions['setDraft'] = (sessionId, text) => {
        const drafts = new Map(this.state.drafts);
        drafts.set(sessionId, text);
        this.set({ drafts });
    };

    // -- 会话事件增量（通知面；快照可重建，幂等去重） -------------------------

    private onSessionMessage(
        sessionId: string,
        kind: 'user' | 'outcome',
        text: string,
        sequence: number,
    ): void {
        const id = journalMessageId(sessionId, sequence);
        const list = this.state.messages.get(sessionId) ?? [];
        if (list.some((m) => m.id === id)) {
            return;
        }
        const message: ChatMessage =
            kind === 'user'
                ? { id, kind: 'user', at: now(), text, sequence }
                : { id, kind: 'outcome', at: now(), text, sequence };
        const messages = new Map(this.state.messages);
        messages.set(sessionId, [...list, message].slice(-MAX_MESSAGES_PER_SESSION));
        this.set({ messages });
        if (kind === 'user') {
            const title = deriveSessionTitle(sessionId, text);
            this.set({
                sessions: this.state.sessions.map((s) =>
                    s.id === sessionId ? { ...s, title, lastActivityAt: now() } : s,
                ),
            });
        } else {
            this.touchSession(sessionId);
        }
        this.pushObsFrame(sessionId, {
            id: `of-${id}`,
            at: now(),
            kind: 'message',
            text: `${kind === 'user' ? '用户' : '结算'} · ${text}`,
        });
    }

    private onSessionTurn(
        sessionId: string,
        step: number,
        kind: string,
        status: string,
    ): void {
        this.touchSession(sessionId);
        this.pushObsFrame(sessionId, {
            id: `of-turn-${sessionId}-${step}-${status}-${now()}`,
            at: now(),
            kind: 'turn',
            text: `轮次 ${step} · ${kindLabel(kind)} → ${stepStatusLabel(status)}`,
        });
        // 轮次结算先于或伴随 task.updated 到达：主动刷新快照，时间线不等待。
        void this.refreshActiveTasks();
    }

    private onSessionOutput(sessionId: string, step: number, chunk: string, truncated: boolean): void {
        const preview = chunk.length > 0 ? chunk.slice(0, 80) : '（空输出）';
        this.pushObsFrame(sessionId, {
            id: `of-output-${sessionId}-${step}-${now()}`,
            at: now(),
            kind: 'output',
            text: `输出 ${step}${truncated ? '（截断）' : ''} · ${preview}`,
        });
    }

/** 把对话轮投影进线程：pending 追加用户行 + 思考中的助手行，ok/failed
 * 按 turnId 原地收敛（DEC-027 对话面，快照事实源是 session.chat.history）。 */
    private onChatTurnUpdated(event: Extract<ServerEvent, { event: 'session.chat_updated' }>): void {
        const sessionId = event.session_id;
        const list = [...(this.state.messages.get(sessionId) ?? [])];
        const assistantIndex = list.findIndex(
            (m) => m.kind === 'assistant' && m.turnId === event.turn_id,
        );
        const at = now();
        if (assistantIndex === -1) {
            // pending 首投：用户行 + 思考中的助手行（快照重放同构）。
            list.push({
                id: `dlg-u-${event.turn_id}`,
                kind: 'user',
                at,
                text: event.user_text,
            });
            list.push({
                id: `dlg-a-${event.turn_id}`,
                kind: 'assistant',
                at,
                turnId: event.turn_id,
                status: event.status,
                text: event.reply_text ?? event.error ?? '',
                userText: event.user_text,
            });
        } else {
            const target = list[assistantIndex]!;
            if (target.kind === 'assistant') {
                list[assistantIndex] = {
                    ...target,
                    status: event.status,
                    text: event.reply_text ?? event.error ?? '',
                };
            }
        }
        const messages = new Map(this.state.messages);
        messages.set(sessionId, list.slice(-MAX_MESSAGES_PER_SESSION));
        const pendingChats = new Map(this.state.pendingChats);
        if (event.status === 'pending') {
            pendingChats.set(sessionId, event.turn_id);
        } else {
            pendingChats.delete(sessionId);
        }
        this.set({ messages, pendingChats });
    }

    /** 会话选中时重建对话线程投影（session.chat.history 快照事实源，
     * DEC-027）：与 journal 行按时间归位，幂等（按 turnId 去重）。 */
    private async loadChatHistory(sessionId: string): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready') {
            return;
        }
        if (!this.state.chatSupported) {
            return;
        }
        try {
            const snapshot = await this.transport.sessionChatHistory(sessionId);
            if (this.disposed) {
                return;
            }
            const additions: ChatMessage[] = [];
            for (const turn of snapshot.turns) {
                const mapped = mapChatTurn(turn);
                additions.push(...mapped);
            }
            const existing = new Map(
                (this.state.messages.get(sessionId) ?? []).map((m) => [m.id, m]),
            );
            const merged = [...existing.values()];
            for (const message of additions) {
                if (!existing.has(message.id)) {
                    merged.push(message);
                }
            }
            merged.sort((a, b) => a.at - b.at);
            const messages = new Map(this.state.messages);
            messages.set(sessionId, merged.slice(-MAX_MESSAGES_PER_SESSION));
            this.set({ messages });
        } catch (err) {
            if (err instanceof IpcRequestError && err.code === 'not_found') {
                return;
            }
            this.toast(`对话线程加载失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    // -- 批准中心与策略面（DEC-020 / M5-07） -----------------------------------

    /** 重取挂起确认快照（DEC-020：permission.list 是事实源）。 */
    async loadApprovals(): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready') {
            return;
        }
        if (!this.state.permissionsSupported) {
            return;
        }
        try {
            const pending = await this.transport.permissionList();
            if (this.disposed) {
                return;
            }
            this.set({ pendingApprovals: pending });
        } catch (err) {
            if (err instanceof IpcRequestError && err.code === 'unavailable') {
                return; // 确认面未启用（hub 未配置）
            }
            this.toast(`批准中心刷新失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    /** 响应一条挂起确认（DEC-020 first-response-wins）：成功后重取快照；
     * not_found（已决/已超时）静默收敛，其余稳定错误呈现。 */
    respondApproval: HarnessActions['respondApproval'] = async (requestId, approved) => {
        if (!this.state.permissionsSupported) {
            this.toast('服务未提供异步确认面（hello 无 permissions 位）', 'warn');
            return;
        }
        try {
            await this.transport.permissionRespond(requestId, approved);
            await this.loadApprovals();
        } catch (err) {
            if (err instanceof IpcRequestError && err.code === 'not_found') {
                await this.loadApprovals();
                return;
            }
            this.toast(`批准响应失败：${err instanceof Error ? err.message : String(err)}`, 'error');
        }
    };

    /** 策略面读取（M5-07）：设置页矩阵的事实源。 */
    private async refreshPolicy(): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready' || !this.state.policySupported) {
            return;
        }
        try {
            const view = await this.transport.policyGet();
            if (this.disposed) {
                return;
            }
            this.set({ policy: view });
        } catch (err) {
            if (err instanceof IpcRequestError && err.code === 'unavailable') {
                return;
            }
            this.toast(`策略读取失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    /** 应用新规则集（M5-07）：立即生效 + 合并文档持久化；回显写回状态。 */
    setPolicy: HarnessActions['setPolicy'] = async (rules, readRoots) => {
        if (!this.state.policySupported) {
            this.toast('服务未提供策略面（hello 无 policy 位）', 'warn');
            return;
        }
        try {
            const view = await this.transport.policySet(rules, readRoots);
            this.set({ policy: view });
            this.toast('权限策略已更新（规则立即生效）', 'info');
        } catch (err) {
            const message =
                err instanceof IpcRequestError
                    ? `策略应用被拒绝（${err.code}）`
                    : err instanceof Error
                      ? err.message
                      : String(err);
            this.toast(message, 'error');
        }
    };

    private pushObsFrame(sessionId: string, frame: ObsFrame): void {
        const feed = new Map(this.state.obsFeed);
        const next = [...(feed.get(sessionId) ?? []), frame].slice(-OBS_FEED_CAPACITY);
        feed.set(sessionId, next);
        this.set({ obsFeed: feed });
    }

    // -- 执行模式（task.submit 会话绑定） --------------------------------------

    submitExec: HarnessActions['submitExec'] = async (sessionId, goal, steps, timeoutMs) => {
        if (this.state.takeover) {
            this.toast('紧急停止中：先解除 Takeover 再提交任务', 'warn');
            return;
        }
        if (!this.ensureSessions()) {
            return;
        }
        const trimmedGoal = goal.trim();
        if (trimmedGoal.length === 0 || steps.length === 0) {
            return;
        }
        this.setDraft(sessionId, '');
        try {
            const { task_id } = await this.transport.submitTask({
                goal: trimmedGoal,
                steps: steps.map((s) => ({ op: s.kind, arg: s.argument })),
                step_timeout_ms: timeoutMs,
                session_id: sessionId,
            });
            const sessions = this.state.sessions.map((s) =>
                s.id === sessionId
                    ? { ...s, taskId: task_id, lastActivityAt: now(), title: s.title === deriveSessionTitle(sessionId, undefined) ? deriveSessionTitle(sessionId, trimmedGoal) : s.title }
                    : s,
            );
            const taskInputs = new Map(this.state.taskInputs);
            taskInputs.set(task_id, steps);
            this.set({
                sessions,
                taskInputs,
                activeTaskIds: [...this.state.activeTaskIds, task_id],
            });
            this.appendSystemLine(sessionId, `已提交协议任务 ${task_id}（${steps.length} 个步骤，事件流实时跟中）`, 'info');
            await this.refreshActiveTasks();
            // user 消息以服务端 journal 投影为准：事件面缺席（降级轮询）或
            // 通知丢帧时，历史快照保证线程仍完整。
            await this.loadHistory(sessionId);
        } catch (err) {
            const message = err instanceof IpcRequestError ? `提交被拒绝（${err.code}）` : err instanceof Error ? err.message : String(err);
            this.appendSystemLine(sessionId, `任务提交失败：${message}`, 'error');
        }
    };

    /** 对话模式提交（session.chat，DEC-027）：受理即回执 turn id，回复经
     * session.chat_updated 事件收敛进线程；拒绝以稳定错误如实呈现。 */
    sendDialog: HarnessActions['sendDialog'] = async (sessionId, text) => {
        if (!this.ensureSessions()) {
            return;
        }
        const trimmed = text.trim();
        if (trimmed.length === 0) {
            return;
        }
        if (this.state.pendingChats.has(sessionId)) {
            this.toast('上一轮对话仍在进行中', 'warn');
            return;
        }
        this.setDraft(sessionId, '');
        try {
            await this.transport.sessionChat(sessionId, trimmed);
        } catch (err) {
            const message =
                err instanceof IpcRequestError
                    ? err.code === 'unavailable'
                        ? '对话面不可用（服务端模型层未配置）'
                        : `对话提交被拒绝（${err.code}）`
                    : err instanceof Error
                      ? err.message
                      : String(err);
            this.toast(message, 'error');
        }
    };

    private appendSystemLine(sessionId: string, text: string, tone: 'info' | 'warn' | 'error'): void {
        this.systemSeq += 1;
        const message: ChatMessage = { id: `sys-${this.systemSeq}`, kind: 'system', at: now(), text, tone };
        const messages = new Map(this.state.messages);
        const list = messages.get(sessionId) ?? [];
        messages.set(sessionId, [...list, message].slice(-MAX_MESSAGES_PER_SESSION));
        this.set({ messages });
    }

    stopSession: HarnessActions['stopSession'] = (sessionId) => {
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

    // -- Takeover --------------------------------------------------------------

    engageEstop: HarnessActions['engageEstop'] = () => {
        if (this.state.takeover) {
            return;
        }
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

    // -- 工作流（DEC-023 契约面） --------------------------------------------

    /** 刷新工作流快照：列表摘要 + 运行快照（workflow.runs 是运行事实源）
     * + wire 原子目录（含编辑器控制构造）。内容副本以会话内持有 +
     * workflow.get 按需回读（DEC-026）两条路径取得。 */
    private async refreshWorkflows(): Promise<void> {
        if (this.disposed || this.state.connection !== 'ready' || !this.state.workflowsSupported) {
            return;
        }
        try {
            const [defs, runs, wireAtoms] = await Promise.all([
                this.workflowBackend.listDefs(),
                this.workflowBackend.listRuns(),
                this.workflowBackend.atomCatalog(),
            ]);
            if (this.disposed) {
                return;
            }
            const runsDesc = [...runs].sort((a, b) => b.startedAt - a.startedAt);
            const merged = defs.map((def) => {
                const prev = this.state.workflows.find((p) => p.id === def.id);
                const keep =
                    prev !== undefined && prev.contentKnown && prev.digest !== undefined && prev.digest === def.digest;
                const source = keep && prev !== undefined ? { ...prev, name: def.name, updatedAt: def.updatedAt } : def;
                return withRunInfo(source, runsDesc);
            });
            this.set({
                workflows: merged,
                workflowRuns: runsDesc.map((r) => ({
                    ...r,
                    workflowName: merged.find((d) => d.id === r.workflowId)?.name ?? r.workflowId,
                })),
                atoms: [...wireAtoms, ...CONTROL_CONSTRUCTS],
            });
        } catch (err) {
            this.toast(`工作流快照刷新失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
        }
    }

    private ensureWorkflows(): boolean {
        if (!this.state.workflowsSupported) {
            this.toast('服务未提供工作流能力（hello 无 workflows 位）', 'warn');
            return false;
        }
        return true;
    }

    /** 打开编辑器（DEC-026 定义读取面消费）：无内容副本的定义先经
     * workflow.get 回读 head 内容并重建可编辑副本；回读失败保持只读并
     * 显式提示（不用空内容遮蔽服务端 head，W-03）。 */
    openWorkflowEditor: HarnessActions['openWorkflowEditor'] = (workflowId) => {
        this.navigate({ view: 'workflow-editor', workflowId });
        const current = this.state.workflows.find((w) => w.id === workflowId);
        if (current === undefined || current.contentKnown || !this.state.workflowsSupported) {
            return;
        }
        void this.workflowBackend
            .getDefinition(workflowId)
            .then((hydrated) => {
                // 摘要投影字段（published / runnable / updatedAt / 最近运行）
                // 仍以 workflow.list 条目为准；回读只填充内容侧。
                this.set({
                    workflows: this.state.workflows.map((w) =>
                        w.id === workflowId
                            ? {
                                  ...w,
                                  name: hydrated.name,
                                  version: hydrated.version,
                                  description: hydrated.description,
                                  params: hydrated.params,
                                  steps: hydrated.steps,
                                  digest: hydrated.digest,
                                  contentKnown: true,
                              }
                            : w,
                    ),
                });
            })
            .catch((err: unknown) => {
                this.toast(`定义读取失败：${err instanceof Error ? err.message : String(err)}`, 'warn');
            });
    };

    /** 桌面状态观察（DEC-026）：按需快照，`visual` 显式请求视觉组件——
     * 请求即必须（服务端 fail closed），不可用即呈现稳定错误。 */
    refreshObservation: HarnessActions['refreshObservation'] = (visual) => {
        if (this.state.connection !== 'ready') {
            return;
        }
        if (!this.transport.observationSupported) {
            this.toast('服务未提供观察面（hello 无 observation 位）', 'warn');
            return;
        }
        this.set({ observation: { ...this.state.observation, status: 'loading' } });
        void this.transport
            .desktopObserve({ semantic: true, visual })
            .then((view) => {
                if (this.disposed) {
                    return;
                }
                this.set({ observation: { status: 'ready', visualRequested: visual, view } });
            })
            .catch((err: unknown) => {
                if (this.disposed) {
                    return;
                }
                const message =
                    err instanceof IpcRequestError
                        ? `${err.code}: ${err.message}`
                        : err instanceof Error
                          ? err.message
                          : String(err);
                // 保留上一次成功视图（若有），错误显式呈现——快照不被静默清空。
                this.set({
                    observation: {
                        status: 'error',
                        visualRequested: visual,
                        error: message,
                        view: this.state.observation.view,
                    },
                });
            });
    };

    runWorkflow: HarnessActions['runWorkflow'] = (workflowId) => {
        if (!this.ensureWorkflows()) {
            return;
        }
        const wf = this.state.workflows.find((w) => w.id === workflowId);
        if (wf === undefined) {
            return;
        }
        if (!wf.runnable) {
            this.toast('草稿不可运行：先发布生成可运行版本（W-04）', 'warn');
            return;
        }
        void this.workflowBackend
            .run(workflowId)
            .then((run) => {
                this.set({
                    workflowRuns: [run, ...this.state.workflowRuns],
                    workflows: this.state.workflows.map((w) =>
                        w.id === workflowId ? { ...w, lastRunAt: run.startedAt, lastRunStatus: 'running' } : w,
                    ),
                });
                this.navigate({ view: 'workflow-run', workflowId, runId: run.id });
                this.toast(`已启动工作流「${wf.name}」`, 'info');
            })
            .catch((err: unknown) => {
                this.toast(`运行失败：${err instanceof Error ? err.message : String(err)}`, 'error');
            });
    };

    /** 新建草稿（RPA：新流程从空序列开始，自动保存为草稿）。 */
    createWorkflow = (): void => {
        if (!this.ensureWorkflows()) {
            return;
        }
        const id = newWorkflowId();
        const draft: WorkflowDef = {
            id,
            name: '未命名工作流',
            version: '草稿',
            description: '',
            params: [],
            steps: [],
            successRate: 1,
            published: false,
            updatedAt: now(),
            runnable: false,
            contentKnown: true,
        };
        void this.workflowBackend.saveDraft(draft).then((saved) => {
            this.set({ workflows: [saved, ...this.state.workflows] });
            this.navigate({ view: 'workflow-editor', workflowId: saved.id });
        });
    }

    renameWorkflow = (id: string, name: string): void => {
        const trimmed = name.trim();
        if (trimmed.length === 0) {
            return;
        }
        void this.mutateDef(id, (d) => ({ ...d, name: trimmed }));
    }

    setWorkflowDescription = (id: string, description: string): void => {
        void this.mutateDef(id, (d) => ({ ...d, description }));
    };

    setWorkflowParams = (id: string, params: WorkflowParam[]): void => {
        void this.mutateDef(id, (d) => ({ ...d, params }));
    };

    deleteWorkflow = (id: string): void => {
        if (!this.ensureWorkflows()) {
            return;
        }
        void this.workflowBackend
            .remove(id)
            .then(() => {
                this.set({
                    workflows: this.state.workflows.filter((w) => w.id !== id),
                    workflowRuns: this.state.workflowRuns.filter((r) => r.workflowId !== id),
                });
                if (this.state.route.view === 'workflow-editor' && this.state.route.workflowId === id) {
                    this.navigate({ view: 'workflows' });
                }
                this.toast('工作流已删除（仅目录条目；版本历史保留在服务库）', 'info');
            })
            .catch((err: unknown) => {
                this.toast(`删除失败：${err instanceof Error ? err.message : String(err)}`, 'error');
            });
    }

    publishWorkflow = (id: string): void => {
        if (!this.ensureWorkflows()) {
            return;
        }
        const def = this.state.workflows.find((w) => w.id === id);
        if (def === undefined) {
            return;
        }
        if (!def.contentKnown) {
            this.toast('尚未取得定义内容（workflow.get 未回读），不可发布', 'warn');
            return;
        }
        void this.workflowBackend
            .publish(def)
            .then((published) => {
                this.set({
                    workflows: this.state.workflows.map((w) => (w.id === id ? published : w)),
                });
                this.toast(`已发布 ${published.name}（head ${published.version}）`, 'info');
            })
            .catch((err: unknown) => {
                this.toast(`发布失败：${err instanceof Error ? err.message : String(err)}`, 'error');
            });
    }

    /** 就地修改定义并持久化为草稿（编辑器每次编辑即保存）。仅限本会话
     * 持有内容副本的定义——否则保存会以空内容遮蔽服务端 head（W-03）。 */
    private mutateDef = async (id: string, mutate: (d: WorkflowDef) => WorkflowDef): Promise<void> => {
        const current = this.state.workflows.find((w) => w.id === id);
        if (current === undefined) {
            return;
        }
        if (!current.contentKnown) {
            this.toast('尚未取得定义内容（workflow.get 未回读），不可编辑', 'warn');
            return;
        }
        const next = mutate({ ...current });
        try {
            const saved = await this.workflowBackend.saveDraft(next);
            this.set({ workflows: this.state.workflows.map((w) => (w.id === id ? saved : w)) });
        } catch (err) {
            this.toast(`草稿保存失败：${err instanceof Error ? err.message : String(err)}`, 'error');
        }
    }

    /** 步骤序列变更（编辑器拖入/排序/改参/删除的统一入口）。 */
    mutateSteps = (id: string, mutate: (steps: WorkflowDef['steps']) => WorkflowDef['steps']): void => {
        void this.mutateDef(id, (d) => ({ ...d, steps: mutate([...d.steps]) }));
    };

    setStepParams = (id: string, index: number, params: Record<string, string>): void => {
        void this.mutateDef(id, (d) => ({
            ...d,
            steps: d.steps.map((s, i) => (i === index ? { ...s, params } : s)),
        }));
    }

    setStepSkipIf = (id: string, index: number, skipIf: WorkflowDef['steps'][number]['skipIf']): void => {
        void this.mutateDef(id, (d) => ({
            ...d,
            steps: d.steps.map((s, i) => (i === index ? { ...s, skipIf } : s)),
        }));
    }

    setStepLoopMax = (id: string, index: number, loopMax: number | undefined): void => {
        void this.mutateDef(id, (d) => ({
            ...d,
            steps: d.steps.map((s, i) => (i === index ? { ...s, loopMax } : s)),
        }));
    }

    exportWorkflowJson = (id: string): void => {
        const def = this.state.workflows.find((w) => w.id === id);
        if (def === undefined) {
            return;
        }
        if (!def.contentKnown) {
            this.toast('尚未取得定义内容（workflow.get 未回读），不可导出', 'warn');
            return;
        }
        const atomsById = new Map(this.state.atoms.map((a) => [a.id, a]));
        const blob = new Blob([JSON.stringify(workflowDefToIr(def, atomsById), null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${def.name}.${def.id.slice(0, 8)}.json`;
        a.click();
        URL.revokeObjectURL(url);
        this.toast('已导出 Workflow IR v1 JSON', 'info');
    }

    cancelWorkflowRun: HarnessActions['cancelWorkflowRun'] = (runId) => {
        if (!this.ensureWorkflows()) {
            return;
        }
        void this.workflowBackend
            .cancelRun(runId)
            .then(() => this.refreshWorkflows())
            .catch((err: unknown) => {
                this.toast(`取消失败：${err instanceof Error ? err.message : String(err)}`, 'error');
            });
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

    actions(): HarnessActions {
        return {
            navigate: this.navigate,
            newSession: this.newSession,
            selectSession: this.selectSession,
            deleteSession: (id) => this.deleteSession(id),
            renameSession: (id, title) => this.renameSession(id, title),
            setDraft: this.setDraft,
            submitExec: this.submitExec,
            sendDialog: (sessionId, text) => this.sendDialog(sessionId, text),
            respondApproval: (requestId, approved) => this.respondApproval(requestId, approved),
            loadApprovals: () => this.loadApprovals(),
            setPolicy: (rules, readRoots) => this.setPolicy(rules, readRoots),
            stopSession: this.stopSession,
            engageEstop: this.engageEstop,
            releaseEstop: this.releaseEstop,
            cancelTask: (taskId) => this.cancelTask(taskId),
            runWorkflow: this.runWorkflow,
            cancelWorkflowRun: this.cancelWorkflowRun,
            refreshObservation: (visual) => this.refreshObservation(visual),
            openWorkflowEditor: (workflowId) => this.openWorkflowEditor(workflowId),
            createWorkflow: () => this.createWorkflow(),
            renameWorkflow: this.renameWorkflow,
            setWorkflowDescription: this.setWorkflowDescription,
            setWorkflowParams: this.setWorkflowParams,
            deleteWorkflow: this.deleteWorkflow,
            publishWorkflow: this.publishWorkflow,
            mutateSteps: this.mutateSteps,
            setStepParams: this.setStepParams,
            setStepSkipIf: this.setStepSkipIf,
            setStepLoopMax: this.setStepLoopMax,
            exportWorkflowJson: this.exportWorkflowJson,
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

/** 用运行快照回填定义的最近运行信息（时间 / 状态 / 终态成功率）。 */
function withRunInfo(def: WorkflowDef, runsDesc: readonly WorkflowRun[]): WorkflowDef {
    const lastRun = runsDesc.find((r) => r.workflowId === def.id);
    const terminal = runsDesc.filter(
        (r) => r.workflowId === def.id && (r.status === 'completed' || r.status === 'failed' || r.status === 'cancelled'),
    );
    const completed = terminal.filter((r) => r.status === 'completed').length;
    return {
        ...def,
        lastRunAt: lastRun?.startedAt,
        lastRunStatus: lastRun?.status,
        successRate: terminal.length > 0 ? completed / terminal.length : 1,
    };
}

export function isTerminalProgress(progress: TaskProgress | string): boolean {
    return progress === 'Completed' || progress === 'Failed' || progress === 'Cancelled';
}
