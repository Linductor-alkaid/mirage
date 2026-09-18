/// Workflow 编辑器的后端接口缝（RPA 工程界面的事实层）。
///
/// 工作流管理 / 原子动作目录尚无 IPC 面（设计规范 §4 前瞻依赖）。本文件把
/// 编辑器需要的全部后端能力收敛为一个接口，UI 只依赖此接口；当前由
/// `MockWorkflowBackend` 内存实现（模拟域，可辨识），未来 IPC 适配器实现同
/// 一接口即可接入真实服务，视图层零改动。
///
/// 预期的 IPC 面映射（登记为协议演进输入，见 docs/design 前端设计规范 §4）：
///   listDefs / saveDraft / publish  → `workflow.list` / `workflow.save` /
///                                      `workflow.publish`（IR v1 JSON 序列化）
///   listRuns / run / cancelRun      → `workflow.runs` / `workflow.run` /
///                                      `workflow.cancel`
///   atomCatalog                     → `workflow.atom.catalog`（原子动作目录，
///                                      依赖 Platform Backend 能力上报）
///   remove                          → `workflow.delete`

import type { WorkflowDef, WorkflowParam, WorkflowRun, WorkflowStepDef } from './model.js';

// ---------------------------------------------------------------------------
// 原子动作目录
// ---------------------------------------------------------------------------

export type AtomCategory =
    | '文件'
    | '命令'
    | '桌面观察'
    | '窗口与输入'
    | '剪贴板'
    | '流程控制'
    | '子流程';

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
    id: string;
    name: string;
    category: AtomCategory;
    /** 落到 Workflow IR v1 的步骤类别（control = 流程控制回跳/跳过语义）。 */
    kind: WorkflowStepDef['kind'];
    description: string;
    params: readonly WorkflowAtomParamSpec[];
    /** 运行后的产出（输出变量提示，展示面）。 */
    produces?: string;
    /**
     * `available` = 当前能力面即可执行；`planned` = 依赖 M2+ Platform Backend
     * （如键鼠输入），目录可见、可拖入编排，运行面待后端交付（以 mock 演示）。
     */
    availability: 'available' | 'planned';
}

/** 模拟域原子动作目录（有界：7 类 / 20 项）。 */
export const ATOM_CATALOG: readonly WorkflowAtom[] = [
    // 文件
    { id: 'file.read-text', name: '读取文本文件', category: '文件', kind: 'filesystem.read', description: '读取 UTF-8 文本内容', params: [{ name: 'path', type: 'text', required: true, description: '文件路径' }], produces: 'content', availability: 'available' },
    { id: 'file.list-dir', name: '列出目录', category: '文件', kind: 'filesystem.read', description: '枚举目录条目（名称/大小/mtime）', params: [{ name: 'dir', type: 'text', required: true, description: '目录路径' }, { name: 'pattern', type: 'text', required: false, description: 'glob 过滤' }], produces: 'entries', availability: 'available' },
    { id: 'file.move', name: '移动/归档文件', category: '文件', kind: 'process.execute', description: '按清单移动文件到目标目录', params: [{ name: 'source', type: 'text', required: true, description: '来源目录' }, { name: 'target', type: 'text', required: true, description: '目标目录' }, { name: 'dryRun', type: 'boolean', required: false, defaultValue: 'false', description: '只生成清单不移动' }], availability: 'available' },
    // 命令
    { id: 'cmd.run', name: '执行命令', category: '命令', kind: 'process.execute', description: '在 shell 中执行命令并捕获输出', params: [{ name: 'command', type: 'text', required: true, description: '命令行' }, { name: 'timeoutSec', type: 'number', required: false, defaultValue: '30', description: '超时秒数' }], produces: 'stdout', availability: 'available' },
    { id: 'cmd.script', name: '运行脚本', category: '命令', kind: 'process.execute', description: '执行脚本文件（bash/PowerShell）', params: [{ name: 'script', type: 'text', required: true, description: '脚本路径' }, { name: 'args', type: 'text', required: false, description: '参数' }], produces: 'stdout', availability: 'available' },
    // 桌面观察
    { id: 'obs.screenshot', name: '截取屏幕', category: '桌面观察', kind: 'display.observe', description: '全屏/窗口截图并生成视觉快照', params: [{ name: 'target', type: 'select', required: false, defaultValue: 'screen', options: ['screen', 'frontmost-window'], description: '截图对象' }], produces: 'snapshot', availability: 'available' },
    { id: 'obs.read-tree', name: '读取界面树', category: '桌面观察', kind: 'display.observe', description: '读取语义快照（可交互对象树）', params: [{ name: 'app', type: 'text', required: false, description: '目标应用（缺省前台）' }], produces: 'elements', availability: 'available' },
    { id: 'obs.wait-appear', name: '等待元素出现', category: '桌面观察', kind: 'display.observe', description: '轮询语义快照直至元素出现或超时', params: [{ name: 'selector', type: 'text', required: true, description: '元素描述' }, { name: 'timeoutSec', type: 'number', required: false, defaultValue: '10', description: '超时秒数' }], availability: 'available' },
    // 窗口与输入（M2+：依赖 Platform Backend 键鼠面）
    { id: 'ui.focus-window', name: '聚焦窗口', category: '窗口与输入', kind: 'process.execute', description: '按标题/类名激活窗口', params: [{ name: 'title', type: 'text', required: true, description: '窗口标题' }], availability: 'planned' },
    { id: 'ui.click', name: '鼠标点击', category: '窗口与输入', kind: 'process.execute', description: '按 ElementReference 或坐标点击', params: [{ name: 'target', type: 'text', required: true, description: '元素引用或坐标' }, { name: 'button', type: 'select', required: false, defaultValue: 'left', options: ['left', 'right', 'double'], description: '按键' }], availability: 'planned' },
    { id: 'ui.type-text', name: '键入文本', category: '窗口与输入', kind: 'process.execute', description: '向前台窗口键入文本', params: [{ name: 'text', type: 'text', required: true, description: '文本' }], availability: 'planned' },
    { id: 'ui.hotkey', name: '发送快捷键', category: '窗口与输入', kind: 'process.execute', description: '组合键（如 Ctrl+S）', params: [{ name: 'keys', type: 'text', required: true, description: '组合键' }], availability: 'planned' },
    // 剪贴板
    { id: 'clip.get', name: '读取剪贴板', category: '剪贴板', kind: 'filesystem.read', description: '读取剪贴板文本', params: [], produces: 'text', availability: 'available' },
    { id: 'clip.set', name: '写入剪贴板', category: '剪贴板', kind: 'process.execute', description: '写入文本到剪贴板', params: [{ name: 'text', type: 'text', required: true, description: '文本（支持 {"$param"} 引用）' }], availability: 'available' },
    // 流程控制
    { id: 'ctl.condition', name: '条件跳过', category: '流程控制', kind: 'control', description: '前置条件不满足时跳过后续步骤（IR v1 skipIf）', params: [{ name: 'predicate', type: 'text', required: true, description: '谓词表达式' }], availability: 'available' },
    { id: 'ctl.loop', name: '循环回跳', category: '流程控制', kind: 'control', description: '回跳到序列头部循环执行（IR v1 loop_head）', params: [{ name: 'maxIterations', type: 'number', required: true, defaultValue: '5', description: '最大迭代次数' }], availability: 'available' },
    { id: 'ctl.delay', name: '延时等待', category: '流程控制', kind: 'process.execute', description: '固定等待', params: [{ name: 'seconds', type: 'number', required: true, defaultValue: '1', description: '秒' }], availability: 'available' },
    // 子流程
    { id: 'sub.call', name: '调用子工作流', category: '子流程', kind: 'process.execute', description: '以参数调用另一个已发布工作流', params: [{ name: 'workflow', type: 'text', required: true, description: '工作流名称' }, { name: 'params', type: 'text', required: false, description: '参数 JSON' }], availability: 'available' },
    { id: 'sub.approve', name: '请求人工放行', category: '子流程', kind: 'process.execute', description: '产生一条待人工批准的权限请求（M2+ 权限事件面）', params: [{ name: 'summary', type: 'text', required: true, description: '请求摘要' }], availability: 'available' },
    { id: 'sub.agent', name: '委托 Agent 步骤', category: '子流程', kind: 'process.execute', description: '把该步骤目标交给 agent 自主完成并回传证据', params: [{ name: 'goal', type: 'text', required: true, description: '步骤目标' }], availability: 'planned' },
];

// ---------------------------------------------------------------------------
// 后端接口
// ---------------------------------------------------------------------------

export interface WorkflowDraftPayload {
    id: string;
    name: string;
    version: string;
    description: string;
    params: WorkflowParam[];
    steps: WorkflowStepDef[];
}

export interface WorkflowBackend {
    /** 已保存工作流定义（含草稿与已发布）。 */
    listDefs(): Promise<WorkflowDef[]>;
    /** 保存草稿（不改变发布状态）。 */
    saveDraft(def: WorkflowDraftPayload): Promise<WorkflowDef>;
    /** 发布草稿（版本号进位由后端裁决）。 */
    publish(id: string): Promise<WorkflowDef>;
    remove(id: string): Promise<void>;
    /** 原子动作目录（右栏可拖入的最小单元）。 */
    atomCatalog(): Promise<readonly WorkflowAtom[]>;
    listRuns(): Promise<WorkflowRun[]>;
    run(defId: string): Promise<WorkflowRun>;
    cancelRun(runId: string): Promise<void>;
}

// ---------------------------------------------------------------------------
// Mock 实现（模拟域：内存 + 定时推进；可辨识、有界）
// ---------------------------------------------------------------------------

import { seedWorkflows } from './harness-mock.js';

export class MockWorkflowBackend implements WorkflowBackend {
    private defs: WorkflowDef[];
    private runs: WorkflowRun[];

    constructor(now: number) {
        const seeded = seedWorkflows(now);
        this.defs = seeded.workflows.map((w) => ({ ...w, published: true, updatedAt: now - 86_400_000 }));
        this.runs = seeded.runs;
    }

    async listDefs(): Promise<WorkflowDef[]> {
        return [...this.defs];
    }

    async saveDraft(def: WorkflowDraftPayload): Promise<WorkflowDef> {
        const existing = this.defs.find((d) => d.id === def.id);
        const next: WorkflowDef = {
            ...(existing ?? { lastRunAt: undefined, lastRunStatus: undefined, successRate: 1 }),
            ...def,
            // 版本只经 publish 进位：草稿保存不回退、不沿用调用方载荷里的旧版本。
            version: existing?.version ?? def.version,
            published: false,
            updatedAt: Date.now(),
        };
        this.defs = existing === undefined ? [...this.defs, next] : this.defs.map((d) => (d.id === def.id ? next : d));
        return next;
    }

    async publish(id: string): Promise<WorkflowDef> {
        const def = this.defs.find((d) => d.id === id);
        if (def === undefined) {
            throw new Error(`workflow '${id}' not found`);
        }
        // 发布进位：vN → vN+1（发布后工作副本回到已发布态）。
        const m = /^v(\d+)$/.exec(def.version);
        const next = m !== null ? `v${Number(m[1]) + 1}` : def.version;
        const published: WorkflowDef = { ...def, version: next, published: true, updatedAt: Date.now() };
        this.defs = this.defs.map((d) => (d.id === id ? published : d));
        return published;
    }

    async remove(id: string): Promise<void> {
        this.defs = this.defs.filter((d) => d.id !== id);
    }

    async atomCatalog(): Promise<readonly WorkflowAtom[]> {
        return ATOM_CATALOG;
    }

    async listRuns(): Promise<WorkflowRun[]> {
        return [...this.runs];
    }

    async run(defId: string): Promise<WorkflowRun> {
        const def = this.defs.find((d) => d.id === defId);
        if (def === undefined) {
            throw new Error(`workflow '${defId}' not found`);
        }
        const run: WorkflowRun = {
            id: `r-${Date.now().toString(36)}`,
            workflowId: defId,
            workflowName: def.name,
            status: 'running',
            startedAt: Date.now(),
            steps: def.steps.map((s) => ({ title: s.title, status: 'pending' })),
        };
        this.runs = [run, ...this.runs];
        return run;
    }

    async cancelRun(runId: string): Promise<void> {
        this.runs = this.runs.map((r) =>
            r.id === runId && r.status === 'running'
                ? { ...r, status: 'cancelled', steps: r.steps.map((s) => (s.status === 'running' || s.status === 'pending' ? { ...s, status: 'skipped' as const } : s)) }
                : r,
        );
    }
}
