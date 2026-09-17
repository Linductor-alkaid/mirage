/// Harness 前端领域模型（视图层）。
///
/// 契约事实源是 `@mirage/contracts`（协议 v1）：host 身份、任务、步骤、事件
/// 全部来自 transport。会话 / 消息 / 审批 / 工作流管理尚无 IPC 面（设计规范
/// §4 前瞻依赖），由 app 层模拟先行 —— `SessionStore` 的模拟域必须可辨识
/// （见 `harness-mock.ts` 头注释），不得伪造契约层事实。

import type { TaskProgress } from '@mirage/contracts';

// ---------------------------------------------------------------------------
// 会话与消息
// ---------------------------------------------------------------------------

export type SessionMode = 'chat' | 'exec';

export interface SessionMeta {
    id: string;
    title: string;
    pinned: boolean;
    createdAt: number;
    updatedAt: number;
    mode: SessionMode;
    /** 关联的协议层任务（执行模式提交后回填，契约事实）。 */
    taskId?: string;
}

export type MessageTone = 'info' | 'warn' | 'error';

/** 步骤的展示形态：契约 StepView 不携带参数（wire 事实），展示层的
 * argument 来自本地提交入参记忆（store.taskInputs）或会话叙事（模拟域）。 */
export interface StepDisplay {
    index: number;
    kind: string;
    status: string;
    operationId: string;
    argument: string;
    exitCode: number;
    result: string;
    resultTruncated: boolean;
    error: string;
}

/** 模拟 agent 的思维链块（"已思考 Ns" 折叠面板的数据面）。 */
export interface ThinkingBlock {
    text: string;
    elapsedMs: number;
}

export type ApprovalKind = 'desktop.action' | 'process.execute' | 'filesystem.write';

export interface ApprovalRequest {
    id: string;
    kind: ApprovalKind;
    summary: string;
    detail?: string;
    status: 'pending' | 'approved' | 'denied';
    requestedAt?: number;
    decidedAt?: number;
}

/** 观察快照（Desktop Observation 的消息化呈现；模拟域为文字 + 网格占位）。 */
export interface SnapshotView {
    label: string;
    detail: string;
    /** SoM 标注占位：网格尺寸（列×行），仅用于占位渲染。 */
    grid: [number, number];
}

export type ChatMessage =
    | { id: string; kind: 'user'; at: number; text: string }
    | {
        id: string;
        kind: 'assistant';
        at: number;
        text: string;
        streaming?: boolean;
        thinking?: ThinkingBlock;
    }
    | { id: string; kind: 'activity'; at: number; taskId: string; progress: TaskProgress; note?: string }
    | { id: string; kind: 'step'; at: number; taskId: string; step: StepDisplay }
    | { id: string; kind: 'snapshot'; at: number; snapshot: SnapshotView }
    | { id: string; kind: 'approval'; at: number; approval: ApprovalRequest }
    | { id: string; kind: 'system'; at: number; text: string; tone: MessageTone };

// ---------------------------------------------------------------------------
// 工作流（模拟域）
// ---------------------------------------------------------------------------

export type WorkflowRunStatus = 'running' | 'completed' | 'failed' | 'cancelled';

export interface WorkflowParam {
    name: string;
    required: boolean;
    description: string;
}

export interface WorkflowStepDef {
    title: string;
    kind: 'filesystem.read' | 'process.execute' | 'display.observe';
    detail: string;
    /** 前置条件谓词（IR v1 跳过语义的展示面）。 */
    skipIf?: string;
}

export interface WorkflowDef {
    id: string;
    name: string;
    version: string;
    description: string;
    params: WorkflowParam[];
    steps: WorkflowStepDef[];
    lastRunAt?: number;
    lastRunStatus?: WorkflowRunStatus;
    successRate: number;
}

export interface WorkflowRunStep {
    title: string;
    status: 'pending' | 'running' | 'ok' | 'failed' | 'skipped' | 'cancelled';
    durationMs?: number;
    log?: string;
}

export interface WorkflowRun {
    id: string;
    workflowId: string;
    workflowName: string;
    status: WorkflowRunStatus;
    startedAt: number;
    steps: WorkflowRunStep[];
}

// ---------------------------------------------------------------------------
// 上下文用量（模拟域，opencode/DSH 共性：占用可见且可分解）
// ---------------------------------------------------------------------------

export interface ContextUsage {
    usedTokens: number;
    budgetTokens: number;
    breakdown: { system: number; history: number; tools: number };
}

// ---------------------------------------------------------------------------
// 全局
// ---------------------------------------------------------------------------

export interface ToastNote {
    id: string;
    text: string;
    tone: MessageTone;
    at: number;
}
