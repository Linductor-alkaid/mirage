/// Workflow 编辑器的后端接口缝与 IPC 适配器（RPA 工程界面的事实层）。
///
/// 设计规范 §3.5：编辑器全部数据经 `WorkflowBackend` 接口，UI 不感知传输
/// 差异。IPC 面映射（DEC-023 / DEC-026，协议 v1 golden v6）：
///   listDefs / getDefinition         → `workflow.list` / `workflow.get`
///                                      （DEC-026 定义读取面：跨会话回读
///                                      head 内容，重建可编辑副本）
///   saveDraft / publish              → `workflow.save` / `workflow.publish`
///                                      （IR v1 JSON，映射见 `workflow-ir.ts`；
///                                      发布为 DryRun 门禁 + 内容寻址幂等）
///   listRuns / run / cancelRun      → `workflow.runs` / `workflow.run` /
///                                      `workflow.cancel`（W-04：草稿拒跑）
///   atomCatalog                     → `workflow.atom.catalog`（pinned
///                                      BuiltIn 注册表 exposed view，
///                                      DEC-024：目录如实反映绑定环境）
///   remove                          → `workflow.delete`（产品目录条目）
///
/// 未经 getDefinition 回读的定义仍以摘要呈现（contentKnown = false），
/// 防止以空内容遮蔽服务端 head（W-03 内容寻址）。

import type { ExposedTool, MirageTransport, WorkflowRunState, WorkflowSummary } from '@mirage/contracts';

import { RUN_STATUS_OF_STATE } from './model.js';
import type { WorkflowDef, WorkflowRun, WorkflowStepKind } from './model.js';
import { irToWorkflowDef, newHexId, workflowDefToIr } from './workflow-ir.js';

// ---------------------------------------------------------------------------
// 原子动作目录（wire 目录投影 + 编辑器控制构造）
// ---------------------------------------------------------------------------

export type AtomCategory = '文件' | '命令' | '桌面观察' | '窗口与输入' | '剪贴板' | '应用与通知' | '其他';

export type AtomParamType = 'text' | 'number' | 'select' | 'boolean';

export interface WorkflowAtomParamSpec {
    name: string;
    type: AtomParamType;
    required: boolean;
    defaultValue?: string;
    options?: readonly string[];
    description: string;
}

export interface WorkflowAtom {
    /** wire 目录原子 = pinned wire 名（ToolCall `arguments.tool` 绑定值）；
     * 控制构造 = 编辑器内部 id。 */
    id: string;
    name: string;
    category: AtomCategory;
    /** 落到 IR v1 的步骤类别；wire 目录原子恒为 tool_call。 */
    kind: WorkflowStepKind;
    description: string;
    params: readonly WorkflowAtomParamSpec[];
    /** DEC-024 副作用分级：副作用原子在 IR 构建时自动落 W-02 验证谓词。 */
    hasSideEffects: boolean;
    /** wire 原子的语义版本；控制构造为编辑器版本。 */
    version: string;
}

/** 已知 wire 原子的展示标签（呈现层字典；能力以目录 schema 为准，不在此宣称）。 */
const ATOM_LABELS: Readonly<Record<string, string>> = {
    'desktop.window.list': '列出窗口',
    'desktop.window.front': '查询前台窗口',
    'desktop.window.activate': '聚焦窗口',
    'desktop.application.list': '列出应用',
    'desktop.application.launch': '启动应用',
    'desktop.application.terminate': '结束应用',
    'desktop.accessibility.semantic_snapshot': '读取界面树',
    'desktop.filesystem.read_text': '读取文本文件',
    'desktop.clipboard.read_text': '读取剪贴板',
    'desktop.clipboard.write_text': '写入剪贴板',
    'desktop.process.execute': '执行命令',
    'desktop.input.type_text': '键入文本',
    'desktop.notification.post': '发送通知',
};

const ATOM_CATEGORY_BY_PREFIX: readonly (readonly [prefix: string, category: AtomCategory])[] = [
    ['desktop.filesystem.', '文件'],
    ['desktop.process.', '命令'],
    ['desktop.accessibility.', '桌面观察'],
    ['desktop.window.', '窗口与输入'],
    ['desktop.input.', '窗口与输入'],
    ['desktop.clipboard.', '剪贴板'],
    ['desktop.application.', '应用与通知'],
    ['desktop.notification.', '应用与通知'],
];

function categoryOf(wireName: string): AtomCategory {
    for (const [prefix, category] of ATOM_CATEGORY_BY_PREFIX) {
        if (wireName.startsWith(prefix)) {
            return category;
        }
    }
    return '其他';
}

function schemaParams(tool: ExposedTool): WorkflowAtomParamSpec[] {
    const schema = tool.parameters_schema as {
        properties?: Record<string, { type?: unknown; description?: unknown }>;
        required?: unknown;
    };
    const required = new Set(
        Array.isArray(schema.required) ? schema.required.filter((r): r is string => typeof r === 'string') : [],
    );
    return Object.entries(schema.properties ?? {}).map(([name, property]) => {
        const type = property.type === 'integer' || property.type === 'number' ? 'number' : property.type === 'boolean' ? 'boolean' : 'text';
        return {
            name,
            type,
            required: required.has(name),
            description: typeof property.description === 'string' ? property.description : '',
        };
    });
}

/** wire `ExposedTool` → 编辑器原子卡（DEC-024：目录如实反映绑定环境）。 */
export function atomFromExposedTool(tool: ExposedTool): WorkflowAtom {
    return {
        id: tool.wire_name,
        name: ATOM_LABELS[tool.wire_name] ?? tool.wire_name,
        category: categoryOf(tool.wire_name),
        kind: 'tool_call',
        description: tool.description,
        params: schemaParams(tool),
        hasSideEffects: tool.has_side_effects,
        version: tool.version,
    };
}

/** 编辑器控制构造（IR v1 语义的编辑侧表达，非 wire 目录原子）：循环回跳 =
 * `control` 步骤 + 序列首步 `loop_head` 标注 + `max_iterations` 预算。 */
export const CONTROL_CONSTRUCTS: readonly WorkflowAtom[] = [
    {
        id: 'ctl.loop',
        name: '循环回跳',
        category: '其他',
        kind: 'control',
        description: '回跳到序列头部循环执行（IR v1 control + loop_head）',
        params: [{ name: 'maxIterations', type: 'number', required: true, defaultValue: '5', description: '最大迭代次数' }],
        hasSideEffects: false,
        version: '1.0.0',
    },
];

// ---------------------------------------------------------------------------
// 后端接口
// ---------------------------------------------------------------------------

/** 编辑器可提交的定义内容（会话内工作副本的提交形态）。 */
export interface WorkflowDraftPayload {
    id: string;
    name: string;
    version: string;
    description: string;
    params: WorkflowDef['params'];
    steps: WorkflowDef['steps'];
}

export interface WorkflowBackend {
    /** 已保存工作流定义（服务注册表投影；未持有内容副本者可经 getDefinition
     * 回读——DEC-026 定义读取面）。 */
    listDefs(): Promise<WorkflowDef[]>;
    /** 定义读取面（DEC-026，DEC-023 挂账①兑现）：回读 head 定义内容并
     * 重建可编辑副本（unknown id 以 not_found 拒绝）。 */
    getDefinition(id: string): Promise<WorkflowDef>;
    /** 保存草稿（追加 NotValidated 版本；head 回到不可运行——W-04）。 */
    saveDraft(def: WorkflowDraftPayload): Promise<WorkflowDef>;
    /** 发布当前草稿内容（DryRun 门禁；内容寻址幂等——W-03）。 */
    publish(def: WorkflowDraftPayload): Promise<WorkflowDef>;
    remove(id: string): Promise<void>;
    /** 原子动作目录（右栏可拖入的最小单元；wire exposed view）。 */
    atomCatalog(): Promise<readonly WorkflowAtom[]>;
    listRuns(): Promise<WorkflowRun[]>;
    run(defId: string): Promise<WorkflowRun>;
    cancelRun(runId: string): Promise<void>;
}

// ---------------------------------------------------------------------------
// 视图投影助手（store 的事件路径与适配器共用）
// ---------------------------------------------------------------------------

/** pinned WorkflowRunState → 视图运行状态（未知状态保守按 queued）。 */
export function runStatusOf(state: WorkflowRunState): WorkflowRun['status'] {
    return RUN_STATUS_OF_STATE[state] ?? 'queued';
}

export function defFromSummary(summary: WorkflowSummary): WorkflowDef {
    return {
        id: summary.workflow_id,
        name: summary.name,
        version: summary.head_digest.slice(0, 8),
        description: '',
        params: [],
        steps: [],
        successRate: 1,
        published: summary.validation !== 'not_validated',
        updatedAt: summary.updated_at_ms,
        runnable: summary.runnable,
        digest: summary.head_digest,
        contentKnown: false,
    };
}

// ---------------------------------------------------------------------------
// IPC 适配器（契约路径；mock transport 同样经此接入）
// ---------------------------------------------------------------------------

export class IpcWorkflowBackend implements WorkflowBackend {
    private atomsPromise: Promise<readonly WorkflowAtom[]> | null = null;

    constructor(private readonly transport: MirageTransport) {}

    async listDefs(): Promise<WorkflowDef[]> {
        const summaries = await this.transport.listWorkflows();
        return summaries.map(defFromSummary);
    }

    async getDefinition(id: string): Promise<WorkflowDef> {
        const view = await this.transport.getWorkflow(id);
        return irToWorkflowDef(view);
    }

    async saveDraft(def: WorkflowDraftPayload): Promise<WorkflowDef> {
        const ir = await this.buildIr(def);
        const saved = await this.transport.saveWorkflow(ir);
        return {
            ...defAsDef(def),
            version: saved.digest.slice(0, 8),
            published: false,
            runnable: false,
            digest: saved.digest,
            updatedAt: Date.now(),
            contentKnown: true,
        };
    }

    async publish(def: WorkflowDraftPayload): Promise<WorkflowDef> {
        const ir = await this.buildIr(def);
        const published = await this.transport.publishWorkflow(ir);
        return {
            ...defAsDef(def),
            version: published.digest.slice(0, 8),
            published: true,
            runnable: true,
            digest: published.digest,
            updatedAt: Date.now(),
            contentKnown: true,
        };
    }

    async remove(id: string): Promise<void> {
        await this.transport.deleteWorkflow(id);
    }

    async atomCatalog(): Promise<readonly WorkflowAtom[]> {
        return this.loadAtoms();
    }

    async listRuns(): Promise<WorkflowRun[]> {
        const [summaries, names] = await Promise.all([
            this.transport.listWorkflowRuns(),
            this.transport.listWorkflows().catch(() => [] as WorkflowSummary[]),
        ]);
        const nameOf = new Map(names.map((w) => [w.workflow_id, w.name]));
        return summaries.map((run) => ({
            id: run.run_id,
            workflowId: run.workflow_id,
            workflowName: nameOf.get(run.workflow_id) ?? run.workflow_id,
            status: runStatusOf(run.state),
            startedAt: run.created_at_ms,
            steps: [],
        }));
    }

    async run(defId: string): Promise<WorkflowRun> {
        const started = await this.transport.startWorkflowRun({ workflow_id: defId });
        const name = (await this.transport.listWorkflows().catch(() => [] as WorkflowSummary[]))
            .find((w) => w.workflow_id === defId)?.name;
        return {
            id: started.run_id,
            workflowId: defId,
            workflowName: name ?? defId,
            status: 'running',
            startedAt: Date.now(),
            steps: [],
        };
    }

    async cancelRun(runId: string): Promise<void> {
        await this.transport.cancelWorkflowRun(runId);
    }

    /** wire 目录（懒取缓存）；IR 构建与目录渲染共用。 */
    private loadAtoms(): Promise<readonly WorkflowAtom[]> {
        if (this.atomsPromise === null) {
            this.atomsPromise = this.transport
                .workflowAtomCatalog()
                .then((tools) => tools.map(atomFromExposedTool))
                .catch((err: unknown) => {
                    this.atomsPromise = null; // 下次重取，不缓存失败
                    throw err;
                });
        }
        return this.atomsPromise;
    }

    private async buildIr(def: WorkflowDraftPayload): Promise<Record<string, unknown>> {
        const atoms = await this.loadAtoms();
        const atomsById = new Map([...atoms, ...CONTROL_CONSTRUCTS].map((a) => [a.id, a]));
        return workflowDefToIr(defAsDef(def), atomsById);
    }
}

function defAsDef(def: WorkflowDraftPayload): WorkflowDef {
    return {
        id: def.id,
        name: def.name,
        version: def.version,
        description: def.description,
        params: def.params,
        steps: def.steps,
        successRate: 1,
        published: false,
        updatedAt: 0,
        runnable: false,
        contentKnown: true,
    };
}

/** 编辑器新建草稿的身份（IR workflow_id，32 hex）。 */
export function newWorkflowId(): string {
    return newHexId();
}
