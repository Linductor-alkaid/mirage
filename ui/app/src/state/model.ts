/// Harness 前端领域模型（视图层）。
///
/// 契约事实源是 `@mirage/contracts`（协议 v1）：host 身份、任务、步骤、事件、
/// 工作流全部来自 transport。会话 / 消息 / 审批由 app 层模拟先行 ——
/// `SessionStore` 的模拟域必须可辨识（见 `harness-mock.ts` 头注释），不得
/// 伪造契约层事实。工作流模型与 pinned Workflow IR v1 同构（DEC-013 对齐）。

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

/** agent 在会话上下文中调用工作流的工具卡视图（模拟域叙事）。 */
export interface WorkflowCallView {
    workflowId: string;
    workflowName: string;
    version: string;
    params: Record<string, string>;
    runId?: string;
    status: WorkflowRunStatus;
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
    | { id: string; kind: 'workflow-call'; at: number; call: WorkflowCallView }
    | { id: string; kind: 'approval'; at: number; approval: ApprovalRequest }
    | { id: string; kind: 'system'; at: number; text: string; tone: MessageTone };

// ---------------------------------------------------------------------------
// 工作流（契约面：DEC-023 wire + pinned Workflow IR v1，DEC-013 对齐）
// ---------------------------------------------------------------------------

export type WorkflowRunStatus =
    | 'queued'
    | 'running'
    | 'paused'
    | 'completed'
    | 'failed'
    | 'cancelled';

/** pinned WorkflowRunState 的视图投影（DEC-023：快照事实源是 workflow.runs）。 */
export const RUN_STATUS_OF_STATE: Record<string, WorkflowRunStatus> = {
    created: 'queued',
    running: 'running',
    paused: 'paused',
    waiting_user: 'paused',
    waiting_agent: 'paused',
    completed: 'completed',
    failed: 'failed',
    cancelled: 'cancelled',
};

export interface WorkflowParam {
    name: string;
    required: boolean;
    description: string;
    /** IR v1 参数类型（缺省 string；副作用验证谓词参数为 boolean）。 */
    type?: 'string' | 'integer' | 'number' | 'boolean';
}

/** IR v1 步骤类别（封闭集合，与 pinned workflow_ir.hpp 同构；编辑器当前
 * 产出 tool_call（原子动作）与 control（回跳构造）。 */
export type WorkflowStepKind = 'tool_call' | 'navigate' | 'verify' | 'control';

/** IR v1 谓词算子（封闭集合）。 */
export type WorkflowPredicateOpName =
    | 'eq'
    | 'ne'
    | 'lt'
    | 'le'
    | 'gt'
    | 'ge'
    | 'contains'
    | 'exists';

/** 结构化谓词（IR v1：signal 引用 + 算子 + 标量值）。value 为原始文本，
 * IR 构建时解析为 JSON 标量（"true"→true、数字→数值，否则字符串）。 */
export interface WorkflowPredicateView {
    signal: string;
    op: WorkflowPredicateOpName;
    value: string;
}

export interface WorkflowStepDef {
    /** IR step_id（32 hex，插入时生成，重排/改参保持稳定）。 */
    stepId: string;
    /** 原子动作目录引用（编辑器拖入的最小单元；来自 wire 目录或控制构造）。 */
    atomId: string;
    title: string;
    kind: WorkflowStepKind;
    detail: string;
    /** 结构化参数（键 = 原子动作参数名；值支持 {"$param"} 引用语法）。 */
    params?: Record<string, string>;
    /** 前置条件谓词（IR v1 precondition，跳过语义）。 */
    skipIf?: WorkflowPredicateView;
    /** 流程控制回跳上限（IR v1 max_iterations，仅 control 类步骤）。 */
    loopMax?: number;
}

export interface WorkflowDef {
    /** IR workflow_id（32 hex；真实面即 wire workflow_id）。 */
    id: string;
    name: string;
    /** 版本徽标（真实面 = head digest 前 8 位；草稿态显示「草稿」）。 */
    version: string;
    description: string;
    params: WorkflowParam[];
    steps: WorkflowStepDef[];
    lastRunAt?: number;
    lastRunStatus?: WorkflowRunStatus;
    successRate: number;
    /** 草稿/发布状态（真实面 = head validation ≠ not_validated）。 */
    published: boolean;
    updatedAt: number;
    /** W-04 投影（wire workflow.list 的 runnable）。 */
    runnable: boolean;
    /** head digest（保存/发布/列表投影时已知）。 */
    digest?: string;
    /** 本会话持有定义内容副本（wire 无定义读取面，仅有副本者可编辑）。 */
    contentKnown: boolean;
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
    /** wire 运行面暂无步级事件（DEC-023 挂账）；真实运行此处为空序列。 */
    steps: WorkflowRunStep[];
    /** 终态摘要（wire workflow.run_updated 的 summary，encode-when-set）。 */
    summary?: string;
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
