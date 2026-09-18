/// Harness 模拟域（仅 mock，非真实服务行为）。
///
/// 会话 / 消息 / 审批 / 工作流管理尚无 IPC 面（设计规范 §4 前瞻依赖），
/// 本文件以可辨识的模拟先行，支撑视觉与交互定型。纪律：
/// - 不伪造契约层事实：任务状态机 / 步骤 / 事件仍以 `@mirage/contracts`
///   的 MockTransport 为事实源；本层只做「会话叙事 + 演示权限请求 + 模拟
///   快照」的叠加，且在界面上标注「模拟」。
/// - 演示约定（mock 专属，真实服务不产生）：执行模式提交的目标含
///   `approve:` 步骤参数时，该步骤运行期间线程内出现一张模拟权限请求卡，
///   拒止不改变协议层任务走向（M2+ 权限事件面前为演示语义）。
/// - 有界：会话数、每会话消息数、工作流与运行数均有容量上限；所有定时器
///   可随 `dispose()` / 紧急停止回收。

import type {
    ApprovalRequest,
    ChatMessage,
    ContextUsage,
    SessionMeta,
    WorkflowDef,
    WorkflowRun,
} from './model.js';

export const MAX_SESSIONS = 64;
export const MAX_MESSAGES_PER_SESSION = 200;

const MIN = 60_000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;

let nextId = 0;
function uid(prefix: string): string {
    nextId += 1;
    return `${prefix}-${Date.now().toString(36)}-${nextId}`;
}

// ---------------------------------------------------------------------------
// 预置数据（演示内容；全部为合成材料）
// ---------------------------------------------------------------------------

export function seedSessions(now: number): { sessions: SessionMeta[]; messages: Map<string, ChatMessage[]> } {
    const sessions: SessionMeta[] = [
        { id: 's-build', title: '构建日志整理与 flaky 测试修复', pinned: true, createdAt: now - 3 * DAY, updatedAt: now - 40 * MIN, mode: 'exec', taskId: 't-0107' },
        { id: 's-weekly', title: '把桌面截图整理成周报', pinned: false, createdAt: now - 2 * DAY, updatedAt: now - 5 * HOUR, mode: 'exec', taskId: 't-0106' },
        { id: 's-mcp', title: '调研 MCP 权限模型并出对比表', pinned: false, createdAt: now - 26 * HOUR, updatedAt: now - 3 * HOUR, mode: 'chat' },
        { id: 's-ipc', title: '整理 Mirage IPC 契约速查卡', pinned: false, createdAt: now - 20 * HOUR, updatedAt: now - 90 * MIN, mode: 'chat' },
        { id: 's-downloads', title: '清理下载目录并归档安装包', pinned: false, createdAt: now - 5 * DAY, updatedAt: now - 4 * DAY, mode: 'exec', taskId: 't-0098' },
        { id: 's-draft', title: '周报草稿：桌面自动化进展', pinned: false, createdAt: now - 6 * DAY, updatedAt: now - 5 * DAY, mode: 'chat' },
        { id: 's-win', title: 'Windows 截屏权限调研笔记', pinned: false, createdAt: now - 9 * DAY, updatedAt: now - 8 * DAY, mode: 'chat' },
    ];

    const messages = new Map<string, ChatMessage[]>();
    messages.set('s-build', [
        { id: 'm-b1', kind: 'user', at: now - 42 * MIN, text: '昨晚 CI 又红了。把构建日志按失败原因归组，找出 flaky 的测试，给出修复建议。' },
        { id: 'm-b2', kind: 'activity', at: now - 41 * MIN, taskId: 't-0107', progress: 'Active', note: '已创建任务 t-0107（3 个步骤）' },
        { id: 'm-b3', kind: 'step', at: now - 41 * MIN, taskId: 't-0107', step: { index: 0, kind: 'filesystem.read', argument: 'ci/logs/nightly-0917.log', status: 'ok', operationId: 'op-a1', exitCode: 0, result: '读取 48,213 行；定位到 3 个失败用例', resultTruncated: false, error: '' } },
        { id: 'm-b4', kind: 'approval', at: now - 40 * MIN, approval: { id: 'ap-b1', kind: 'process.execute', summary: '重跑失败用例 3 次（定位 flaky）', detail: 'npm test -- --filter auth-session --repeat 3', status: 'approved', decidedAt: now - 40 * MIN } },
        { id: 'm-b5', kind: 'step', at: now - 40 * MIN, taskId: 't-0107', step: { index: 1, kind: 'process.execute', argument: 'npm test -- --filter auth-session --repeat 3', status: 'failed', operationId: 'op-a2', exitCode: 1, result: '第 2 次运行超时（30s），其余两次通过', resultTruncated: false, error: 'exit 1' } },
        { id: 'm-b6', kind: 'snapshot', at: now - 39 * MIN, snapshot: { label: '终端 · 测试输出', detail: 'auth-session › 在第 2 次重复中等待 30s 超时；计时器未清理', grid: [12, 8] } },
        { id: 'm-b7', kind: 'step', at: now - 38 * MIN, taskId: 't-0107', step: { index: 2, kind: 'process.execute', argument: 'apply patch: tests/auth-session.cleanup.ts', status: 'ok', operationId: 'op-a3', exitCode: 0, result: '补丁已应用：清理 pending 计时器', resultTruncated: false, error: '' } },
        { id: 'm-b8', kind: 'activity', at: now - 37 * MIN, taskId: 't-0107', progress: 'Completed' },
        {
            id: 'm-b9',
            kind: 'assistant',
            at: now - 37 * MIN,
            thinking: { text: '三次重复中仅第二次超时 → 与执行顺序无关，指向资源未清理。日志第 38,902 行出现 "timer still pending"，与 auth-session 的刷新计时器吻合。', elapsedMs: 42_000 },
            text: '**结论：`auth-session` 为 flaky 用例，根因是刷新计时器未清理。**\n\n- 3 次重跑仅第 2 次超时，排除执行顺序问题\n- 失败时日志出现 `timer still pending`（第 38,902 行）\n- 已应用补丁：用例结束时清理 pending 计时器\n\n建议：合并后在 CI 观察 3 个窗口期，确认无复发后关闭。',
        },
    ]);
    messages.set('s-weekly', [
        { id: 'm-w1', kind: 'user', at: now - 5 * HOUR, text: '把最近 7 天的桌面截图按项目分组，挑 6 张有代表性的，生成周报草稿。' },
        {
            id: 'm-w0',
            kind: 'workflow-call',
            at: now - 5 * HOUR,
            call: { workflowId: 'wf-shot-report', workflowName: '截图周报生成', version: 'v2', params: { days: '7', limit: '6' }, runId: 'r-2397', status: 'completed' },
        },
        { id: 'm-w2', kind: 'approval', at: now - 5 * HOUR, approval: { id: 'ap-w1', kind: 'desktop.action', summary: '读取图片目录 ~/Pictures/mirage-shots', detail: 'filesystem.read · 82 个文件', status: 'approved', decidedAt: now - 5 * HOUR } },
        { id: 'm-w3', kind: 'snapshot', at: now - 5 * HOUR, snapshot: { label: '文件管理器 · 截图目录', detail: '按修改时间排序，选中 6 张：覆盖 IPC、主题、工作流三个主题', grid: [10, 7] } },
        { id: 'm-w4', kind: 'assistant', at: now - 5 * HOUR, thinking: { text: '截图时间分布集中在两天；按主题聚类比按天更可读。', elapsedMs: 18_000 }, text: '周报草稿已生成（6 张配图，3 个主题小节）。草稿在编辑器中打开，需要我调整篇幅吗？' },
    ]);
    messages.set('s-mcp', [
        { id: 'm-m1', kind: 'user', at: now - 3 * HOUR, text: '对比 MCP 与 Mirage DEC-010 权限模型的差异，重点看工具级 allow/ask/deny。' },
        { id: 'm-m2', kind: 'assistant', at: now - 3 * HOUR, thinking: { text: 'MCP 的授权在服务器握手层；DEC-010 在 Provider × 范围矩阵，粒度更细。', elapsedMs: 21_000 }, text: '核心差异一句话：**MCP 把授权收敛在连接层，Mirage 把它下沉到每次桌面动作**。已生成对比表（4 个维度），要我贴出来还是存为速查卡？' },
    ]);
    messages.set('s-ipc', [
        { id: 'm-i1', kind: 'user', at: now - 90 * MIN, text: '把 IPC 协议 v1 的请求/事件整理成一张速查卡。' },
        { id: 'm-i2', kind: 'assistant', at: now - 89 * MIN, text: '速查卡已就绪：6 个请求面 + 3 个事件面，含 seq 单调与 drop-oldest 语义。需要导出 Markdown 吗？' },
    ]);
    messages.set('s-downloads', [
        { id: 'm-d1', kind: 'user', at: now - 4 * DAY, text: '下载目录按类型归档，安装包单独放 archived/。' },
        { id: 'm-d2', kind: 'step', at: now - 4 * DAY, taskId: 't-0098', step: { index: 0, kind: 'filesystem.read', argument: '~/Downloads', status: 'ok', operationId: 'op-d1', exitCode: 0, result: '扫描 217 项', resultTruncated: false, error: '' } },
        { id: 'm-d3', kind: 'approval', at: now - 4 * DAY, approval: { id: 'ap-d1', kind: 'filesystem.write', summary: '移动 14 个安装包到 ~/archives/installers', detail: '含 3 个无法识别来源的 .exe（将列入报告）', status: 'denied', decidedAt: now - 4 * DAY } },
        { id: 'm-d4', kind: 'activity', at: now - 4 * DAY, taskId: 't-0098', progress: 'Cancelled', note: '用户取消：来源不明的安装包先不移动' },
        { id: 'm-d5', kind: 'system', at: now - 4 * DAY, tone: 'warn', text: '任务已取消；未开始步骤被跳过，已产生的移动保留。' },
    ]);
    messages.set('s-draft', [
        { id: 'm-f1', kind: 'user', at: now - 5 * DAY, text: '起个周报框架：桌面自动化进展。' },
        { id: 'm-f2', kind: 'assistant', at: now - 5 * DAY, text: '框架已建：本周结论 / 三个里程碑 / 风险与依赖。数据位先留空，等你确认口径再填。' },
    ]);
    messages.set('s-win', [
        { id: 'm-n1', kind: 'user', at: now - 8 * DAY, text: 'Windows 下截屏有哪些权限路径？' },
        { id: 'm-n2', kind: 'assistant', at: now - 8 * DAY, text: '三条路径：桌面复制 API（无用户提示）、Graphics Capture（一次性授权）、浏览器 getDisplayMedia（每次询问）。产品建议已记入调研笔记。' },
    ]);
    return { sessions, messages };
}

export function seedWorkflows(now: number): { workflows: WorkflowDef[]; runs: WorkflowRun[] } {
    const workflows: WorkflowDef[] = [
        {
            id: 'wf-shot-report',
            name: '截图周报生成',
            version: 'v2',
            description: '聚合本周截图目录，按主题聚类并生成带配图的周报草稿。',
            params: [
                { name: 'days', required: false, description: '回溯天数（默认 7）' },
                { name: 'limit', required: false, description: '最多挑选的截图数（默认 6）' },
            ],
            steps: [
                { atomId: 'file.list-dir', title: '扫描截图目录', kind: 'filesystem.read', detail: '~/Pictures/mirage-shots 按 mtime 过滤', params: { dir: '~/Pictures/mirage-shots' } },
                { atomId: 'ctl.condition', title: '聚类挑选', kind: 'control', detail: '按主题聚类，取 {"$limit"} 张代表图', params: { predicate: 'count(shots) > 0' }, skipIf: 'count(shots) == 0' },
                { atomId: 'obs.screenshot', title: '屏幕复核', kind: 'display.observe', detail: '确认配图内容与聚类标签一致', params: { target: 'frontmost-window' } },
                { atomId: 'cmd.run', title: '写入草稿', kind: 'process.execute', detail: '生成 Markdown 周报并打开编辑器', params: { command: 'mirage report --out weekly-draft.md' } },
            ],
            lastRunAt: now - 5 * HOUR,
            lastRunStatus: 'completed',
            successRate: 0.94,
            published: true,
            updatedAt: now - 2 * DAY,
        },
        {
            id: 'wf-daily-standup',
            name: '每日站会简报',
            version: 'v3',
            description: '汇总昨日构建/任务状态与今日计划，输出站会要点。',
            params: [{ name: 'channel', required: true, description: '发布目标频道' }],
            steps: [
                { atomId: 'cmd.run', title: '读取构建状态', kind: 'process.execute', detail: 'ci status --since yesterday', params: { command: 'ci status --since yesterday' } },
                { atomId: 'file.read-text', title: '读取任务快照', kind: 'filesystem.read', detail: 'runtime/task-snapshots.json', params: { path: 'runtime/task-snapshots.json' } },
                { atomId: 'cmd.run', title: '生成简报', kind: 'process.execute', detail: '模板渲染 → 发送到 {"$channel"}', params: { command: 'notify send {"$channel"}' } },
            ],
            lastRunAt: now - 20 * HOUR,
            lastRunStatus: 'completed',
            successRate: 0.99,
            published: true,
            updatedAt: now - 3 * DAY,
        },
        {
            id: 'wf-downloads',
            name: '下载目录归档',
            version: 'v1',
            description: '按类型归档下载目录，安装包单独入库并生成来源报告。',
            params: [{ name: 'dryRun', required: false, description: '只生成清单不移动' }],
            steps: [
                { atomId: 'file.list-dir', title: '扫描目录', kind: 'filesystem.read', detail: '~/Downloads 全量清单', params: { dir: '~/Downloads' } },
                { atomId: 'file.move', title: '归类移动', kind: 'process.execute', detail: '按扩展名分组移动（dryRun={"$dryRun"}）', params: { source: '~/Downloads', target: '~/archives', dryRun: '{"$dryRun"}' }, skipIf: 'count(unknown) > 3' },
            ],
            lastRunAt: now - 4 * DAY,
            lastRunStatus: 'failed',
            successRate: 0.71,
            published: true,
            updatedAt: now - 6 * DAY,
        },
        {
            id: 'wf-regression',
            name: '构建回归巡检',
            version: 'v5',
            description: '定时巡检构建产物与冒烟测试，异常时附带日志摘要告警。',
            params: [{ name: 'suite', required: false, description: '测试套件（默认 smoke）' }],
            steps: [
                { atomId: 'file.read-text', title: '拉取产物', kind: 'filesystem.read', detail: 'build/latest/manifest.json', params: { path: 'build/latest/manifest.json' } },
                { atomId: 'cmd.run', title: '运行冒烟', kind: 'process.execute', detail: 'pytest -m {"$suite"} --maxfail 3', params: { command: 'pytest -m {"$suite"} --maxfail 3', timeoutSec: '120' } },
                { atomId: 'ctl.loop', title: '重试巡检', kind: 'control', detail: '失败回跳重试，上限 3 次', params: { maxIterations: '3' }, loopMax: 3 },
                { atomId: 'obs.screenshot', title: '截取现场', kind: 'display.observe', detail: '失败时截取终端现场', params: { target: 'screen' } },
            ],
            lastRunAt: now - 2 * HOUR,
            lastRunStatus: 'running',
            successRate: 0.97,
            published: true,
            updatedAt: now - 10 * HOUR,
        },
    ];

    const runs: WorkflowRun[] = [
        {
            id: 'r-2401', workflowId: 'wf-regression', workflowName: '构建回归巡检', status: 'running',
            startedAt: now - 2 * HOUR,
            steps: [
                { title: '拉取产物', status: 'ok', durationMs: 3_200, log: 'manifest: 2026-09-18T00:12Z · 42 个目标' },
                { title: '运行冒烟', status: 'running', log: '17 passed · 2 running…' },
                { title: '截取现场', status: 'pending' },
            ],
        },
        {
            id: 'r-2397', workflowId: 'wf-shot-report', workflowName: '截图周报生成', status: 'completed',
            startedAt: now - 5 * HOUR,
            steps: [
                { title: '扫描截图目录', status: 'ok', durationMs: 1_100 },
                { title: '聚类挑选', status: 'ok', durationMs: 8_400 },
                { title: '屏幕复核', status: 'ok', durationMs: 2_600 },
                { title: '写入草稿', status: 'ok', durationMs: 5_900, log: 'weekly-draft.md · 6 图' },
            ],
        },
        {
            id: 'r-2390', workflowId: 'wf-daily-standup', workflowName: '每日站会简报', status: 'completed',
            startedAt: now - 20 * HOUR,
            steps: [
                { title: '读取构建状态', status: 'ok', durationMs: 2_700 },
                { title: '读取任务快照', status: 'ok', durationMs: 900 },
                { title: '生成简报', status: 'ok', durationMs: 4_100 },
            ],
        },
        {
            id: 'r-2361', workflowId: 'wf-downloads', workflowName: '下载目录归档', status: 'failed',
            startedAt: now - 4 * DAY,
            steps: [
                { title: '扫描目录', status: 'ok', durationMs: 1_800 },
                { title: '归类移动', status: 'failed', durationMs: 640, log: '来源不明的 .exe × 3 —— 跳过条件未满足前用户中止' },
            ],
        },
        {
            id: 'r-2330', workflowId: 'wf-shot-report', workflowName: '截图周报生成', status: 'cancelled',
            startedAt: now - 7 * DAY,
            steps: [
                { title: '扫描截图目录', status: 'ok', durationMs: 1_200 },
                { title: '聚类挑选', status: 'cancelled' },
                { title: '屏幕复核', status: 'skipped' },
                { title: '写入草稿', status: 'skipped' },
            ],
        },
    ];
    return { workflows, runs };
}

export function seedContext(): ContextUsage {
    return {
        usedTokens: 46_800,
        budgetTokens: 200_000,
        breakdown: { system: 3_200, history: 31_400, tools: 12_200 },
    };
}

// ---------------------------------------------------------------------------
// 回复语料（chat 模式的本地模拟；有界集合）
// ---------------------------------------------------------------------------

const CHAT_REPLIES: readonly { match: RegExp; thinking: string; reply: string }[] = [
    {
        match: /权限|permission/i,
        thinking: '问题落在 DEC-010 的 Provider × 范围矩阵；需要把 allow/ask/deny 与默认模式的关系讲清楚。',
        reply: 'Mirage 的权限判定在 **Provider × 范围** 矩阵上逐格取 `allow / ask / deny`，未命中的格子回退到默认模式。批准流（ask）会把请求送进批准中心，不打断你正在看的视图。',
    },
    {
        match: /事件|event|订阅/i,
        thinking: '事件面有三个：task.updated、host.status、events.overflow；订阅语义是 seq 单调 + 有界队列 drop-oldest。',
        reply: '事件订阅的关键语义：**每订阅 `seq` 单调递增**；队列有界，溢出时丢最旧的并补发 `events.overflow`，前端据此重同步（resync）。服务端不支持订阅时前端降级为轮询。',
    },
    {
        match: /工作流|workflow/i,
        thinking: '工作流 IR v1 的语义：有序步骤、前置条件跳过、Control 回跳、参数引用。',
        reply: 'Workflow IR v1 只承诺四种结构：**有序步骤、前置条件跳过、`loop_head` 回跳（带 `max_iterations`）、`{"$param"}` 参数引用**。自由节点图属于扩展位，IR 未承诺前界面不出现。',
    },
    {
        match: /快照|观察|snapshot/i,
        thinking: 'Desktop Observation 由语义快照与视觉快照共同构成，ElementReference 从快照解析。',
        reply: '桌面观察 = **语义快照（可交互对象树）+ 视觉快照（截图 + SoM 标注）**。会话里的 `ElementReference` 是临时引用，执行时才解析成平台对象或坐标。',
    },
];

const FALLBACK_REPLY = {
    thinking: '先确认问题边界，再决定是否需要动用桌面工具；当前是对话模式，不产生桌面动作。',
    reply: '收到。当前是**对话模式**——我可以直接讨论、查笔记、出草稿；不会产生任何桌面动作。需要我实际操作桌面的话，切到执行模式再提交。',
};

// ---------------------------------------------------------------------------
// 会话模拟引擎
// ---------------------------------------------------------------------------

export interface ChatTurnEvents {
    onThinking(sessionId: string, messageId: string, block: { text: string; elapsedMs: number }): void;
    onAssistantChunk(sessionId: string, messageId: string, chunk: string): void;
    onAssistantDone(sessionId: string, messageId: string): void;
}

export type SimPhase =
    | { kind: 'idle' }
    | { kind: 'thinking'; sessionId: string; messageId: string; startedAt: number }
    | { kind: 'streaming'; sessionId: string; messageId: string; full: string; sent: number; timer: number }
    | { kind: 'cooldown'; timer: number };

/** chat 模式的本地对话模拟：思考块 → 流式回复。有状态、可取消。 */
export class ChatSimulator {
    private phase: SimPhase = { kind: 'idle' };

    constructor(private readonly events: ChatTurnEvents) {}

    busy(): boolean {
        return this.phase.kind !== 'idle';
    }

    /** 开始一轮对话模拟；返回将要出现的 assistant 消息 id。 */
    begin(sessionId: string, userText: string): string {
        const hit = CHAT_REPLIES.find((r) => r.match.test(userText)) ?? FALLBACK_REPLY;
        const messageId = uid('ma');
        const startedAt = Date.now();
        const delay = 900 + Math.min(userText.length, 120) * 8;
        const timer = window.setTimeout(() => {
            if (this.phase.kind !== 'thinking') {
                return;
            }
            this.events.onThinking(sessionId, messageId, {
                text: hit.thinking,
                elapsedMs: Date.now() - startedAt,
            });
            this.startStream(sessionId, messageId, hit.reply);
        }, delay);
        this.thinkTimer = timer;
        this.phase = { kind: 'thinking', sessionId, messageId, startedAt };
        return messageId;
    }

    private thinkTimer: number | null = null;

    private startStream(sessionId: string, messageId: string, full: string): void {
        let sent = 0;
        const step = (): void => {
            if (sent >= full.length) {
                this.events.onAssistantDone(sessionId, messageId);
                this.phase = { kind: 'idle' };
                return;
            }
            const take = Math.min(full.length - sent, 2 + Math.floor(Math.random() * 4));
            const chunk = full.slice(sent, sent + take);
            sent += take;
            this.events.onAssistantChunk(sessionId, messageId, chunk);
            const timer = window.setTimeout(step, 24 + Math.random() * 40);
            this.phase = { kind: 'streaming', sessionId, messageId, full, sent, timer };
        };
        step();
    }

    cancel(): void {
        if (this.thinkTimer !== null) {
            window.clearTimeout(this.thinkTimer);
            this.thinkTimer = null;
        }
        if (this.phase.kind === 'streaming') {
            window.clearTimeout(this.phase.timer);
            // 流被取消：以已发送内容收尾，补「被停止」标记由调用方处理。
            this.events.onAssistantDone(this.phase.sessionId, this.phase.messageId);
        }
        this.phase = { kind: 'idle' };
    }

    dispose(): void {
        this.cancel();
    }
}

export function newApproval(partial: Omit<ApprovalRequest, 'id' | 'status'> & { status?: ApprovalRequest['status'] }): ApprovalRequest {
    return { id: uid('ap'), status: 'pending', ...partial };
}

export function newSessionMeta(mode: SessionMeta['mode'], title: string, now: number): SessionMeta {
    return {
        id: uid('s'),
        title,
        pinned: false,
        createdAt: now,
        updatedAt: now,
        mode,
    };
}

/** 会话导出（Markdown）。 */
export function exportSessionMarkdown(title: string, messages: readonly ChatMessage[]): string {
    const lines: string[] = [`# ${title}`, ''];
    for (const m of messages) {
        const time = new Date(m.at).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
        switch (m.kind) {
            case 'user':
                lines.push(`## ${time} · 用户`, '', m.text, '');
                break;
            case 'assistant':
                lines.push(`## ${time} · Mirage`, '', m.text, '');
                break;
            case 'approval':
                lines.push(`> ${time} · 权限请求（${m.approval.kind}）：${m.approval.summary} —— **${m.approval.status === 'approved' ? '已放行' : m.approval.status === 'denied' ? '已拒止' : '待处理'}**`, '');
                break;
            case 'workflow-call':
                lines.push(`> ${time} · 调用工作流：${m.call.workflowName}（${m.call.version}）→ ${m.call.status}`, '');
                break;
            case 'snapshot':
                lines.push(`> ${time} · 观察快照：${m.snapshot.label} —— ${m.snapshot.detail}`, '');
                break;
            case 'step':
                lines.push(`> ${time} · 步骤 ${m.step.index + 1}（${m.step.kind}）：${m.step.argument} → ${m.step.status}`, '');
                break;
            case 'activity':
                lines.push(`> ${time} · 任务 ${m.taskId}：${m.progress}${m.note ? `（${m.note}）` : ''}`, '');
                break;
            case 'system':
                lines.push(`> ${time} · ${m.text}`, '');
                break;
        }
    }
    return lines.join('\n');
}
