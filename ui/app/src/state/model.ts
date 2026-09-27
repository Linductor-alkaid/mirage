/// Harness 前端领域模型（视图层）。
///
/// 全部事实来自 `@mirage/contracts`（协议 v1）：host 身份、任务、步骤、事件、
/// 会话、工作流。模拟域自 M5-06（DEC-025）起退出会话页——标题为展示层派生
/// 事实（wire 无标题成员），不冒充服务端权威状态。工作流模型与 pinned
/// Workflow IR v1 同构（DEC-013 对齐）。

import type { SessionState, TaskProgress } from '@mirage/contracts';

// ---------------------------------------------------------------------------
// 会话与消息
// ---------------------------------------------------------------------------

/** 会话条目（视图投影）：wire `SessionSummary` 只有 {id, state, created_at_ms}
 * （DEC-021）；title / lastActivityAt / taskId 是展示层从会话事件与提交
 * 回执派生的本地事实，跨连接不承诺一致。 */
export interface SessionMeta {
    id: string;
    state: SessionState;
    createdAt: number;
    /** 派生标题：首条 user 消息文本，缺省 `会话 <id 前 8 位>`（DEC-025）。 */
    title: string;
    /** 最近一次会话活动（消息 / 轮次事件）的本地时点，用于签派栏排序。 */
    lastActivityAt: number;
    /** 关联的协议层任务（本应用会话内提交回执绑定；重连前历史不回填）。 */
    taskId?: string;
}

export type MessageTone = 'info' | 'warn' | 'error';

/** 步骤的展示形态：契约 StepView 不携带参数（wire 事实），展示层的
 * argument 来自本地提交入参记忆（store.taskInputs）。 */
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

/** 线程消息（M5-06 契约路径）：user / outcome 来自会话面（session.history
 * 基线 + session.message 增量），step / activity 来自任务快照投影，
 * system 是本地回执行。模型循环引入前不存在 assistant 消息（DEC-025
 * 决策 3），批准卡 / 快照卡随模拟域退役，批准中心属 M5-07。 */
export type ChatMessage =
    | { id: string; kind: 'user'; at: number; text: string; sequence: number }
    | { id: string; kind: 'outcome'; at: number; text: string; sequence: number }
    | { id: string; kind: 'activity'; at: number; taskId: string; progress: TaskProgress; note?: string }
    | { id: string; kind: 'step'; at: number; taskId: string; step: StepDisplay }
    | { id: string; kind: 'system'; at: number; text: string; tone: MessageTone };

/** 观察流帧（观察台实时尾随；通知面语义，丢帧不回补——DEC-025 决策 5）。 */
export interface ObsFrame {
    id: string;
    at: number;
    kind: 'turn' | 'output' | 'message' | 'task';
    text: string;
}

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
// 全局
// ---------------------------------------------------------------------------

export interface ToastNote {
    id: string;
    text: string;
    tone: MessageTone;
    at: number;
}
