/// 工作流页三联：库（卡片网格）→ 编辑器（IR v1 语义只读展示 + 参数面板 +
/// 诊断位）→ 运行详情（步骤时间线）。IR 管理面尚无 IPC（设计规范 §4），
/// 定义数据为模拟域并标注。

import { ChevronRight, Play, Square, Timer } from 'lucide-react';

import { useHarness } from '../hooks.js';
import { duration, relativeTime } from '../lib/labels.js';
import type { WorkflowRunStatus } from '../state/model.js';

const RUN_STATUS: Record<WorkflowRunStatus, { label: string; tone: string; blink: boolean }> = {
    running: { label: '运行中', tone: 'info', blink: true },
    completed: { label: '已完成', tone: 'success', blink: false },
    failed: { label: '已失败', tone: 'danger', blink: false },
    cancelled: { label: '已取消', tone: 'muted', blink: false },
};

export function WorkflowLibrary(): React.ReactElement {
    const { state, navigate, runWorkflow } = useHarness();
    return (
        <div className="main-scroll">
            <div className="page-head">
                <h1>工作流库</h1>
                <span className="sub">可复用的多步骤自动化 · <span className="sim-note">模拟数据</span></span>
            </div>
            <div className="wf-grid">
                {state.workflows.map((w) => (
                    <div
                        key={w.id}
                        className="wf-card"
                        role="link"
                        tabIndex={0}
                        onClick={() => navigate({ view: 'workflow-editor', workflowId: w.id })}
                        onKeyDown={(e) => {
                            if (e.key === 'Enter') {
                                navigate({ view: 'workflow-editor', workflowId: w.id });
                            }
                        }}
                    >
                        <span className="wf-name">
                            {w.name}
                            <span className="wf-ver">{w.version}</span>
                        </span>
                        <span className="wf-desc">{w.description}</span>
                        <span className="wf-meta">
                            <span className={`badge is-${w.lastRunStatus === 'running' ? 'info' : w.lastRunStatus === 'failed' ? 'danger' : w.lastRunStatus === undefined ? 'muted' : 'success'}`}>
                                {w.lastRunStatus === undefined ? '未运行' : RUN_STATUS[w.lastRunStatus].label}
                            </span>
                            <span>成功率 {Math.round(w.successRate * 100)}%</span>
                            <span style={{ marginLeft: 'auto', display: 'inline-flex', alignItems: 'center', gap: 6 }}>
                                <button
                                    type="button"
                                    className="btn btn-primary"
                                    style={{ height: 24 }}
                                    onClick={(e) => {
                                        e.stopPropagation();
                                        runWorkflow(w.id);
                                    }}
                                >
                                    <Play size={12} /> 运行
                                </button>
                                <ChevronRight size={14} />
                            </span>
                        </span>
                    </div>
                ))}
            </div>

            <div className="card" style={{ marginTop: 'var(--mir-space-5)' }}>
                <div className="card-title-row">
                    <h2>最近运行</h2>
                </div>
                <table className="table">
                    <thead>
                        <tr>
                            <th>工作流</th>
                            <th>状态</th>
                            <th>开始</th>
                            <th>步骤</th>
                        </tr>
                    </thead>
                    <tbody>
                        {state.workflowRuns.slice(0, 8).map((r) => (
                            <tr key={r.id} onClick={() => navigate({ view: 'workflow-run', workflowId: r.workflowId, runId: r.id })}>
                                <td>{r.workflowName}</td>
                                <td>
                                    <span className={`badge is-${RUN_STATUS[r.status].tone} ${RUN_STATUS[r.status].blink ? 'is-blink' : ''}`}>
                                        {RUN_STATUS[r.status].label}
                                    </span>
                                </td>
                                <td className="muted">{relativeTime(r.startedAt, Date.now())}</td>
                                <td className="mono muted">
                                    {r.steps.filter((s) => s.status === 'ok').length}/{r.steps.length}
                                </td>
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>
        </div>
    );
}

export function WorkflowEditor({ workflowId }: { workflowId: string }): React.ReactElement | null {
    const { state, runWorkflow } = useHarness();
    const wf = state.workflows.find((w) => w.id === workflowId);
    if (wf === undefined) {
        return null;
    }
    return (
        <div className="main-scroll">
            <div className="page-head">
                <h1>{wf.name}</h1>
                <span className="wf-ver mono">{wf.version}</span>
                <span className="sub">Workflow IR v1 语义 · <span className="sim-note">模拟数据</span></span>
                <span style={{ marginLeft: 'auto', display: 'flex', gap: 8 }}>
                    <button type="button" className="btn btn-primary" onClick={() => runWorkflow(wf.id)}>
                        <Play size={13} /> 运行
                    </button>
                </span>
            </div>
            <div className="stack" style={{ maxWidth: 860 }}>
                <div className="card">
                    <h2>步骤序列</h2>
                    <div className="wf-steps">
                        {wf.steps.map((s, i) => (
                            <div className="wf-step" key={i}>
                                <span className="w-idx" aria-hidden />
                                <div className="w-body">
                                    <div className="w-title">
                                        {s.title}
                                        <span className="badge is-muted">{s.kind === 'display.observe' ? '屏幕观察' : s.kind === 'filesystem.read' ? '文件读取' : '命令执行'}</span>
                                    </div>
                                    <div className="w-detail">{s.detail}</div>
                                    {s.skipIf !== undefined && <div className="w-skip">跳过条件：{s.skipIf}</div>}
                                </div>
                            </div>
                        ))}
                    </div>
                </div>
                <div className="card">
                    <h2>参数</h2>
                    <table className="table">
                        <thead>
                            <tr>
                                <th>名称</th>
                                <th>必填</th>
                                <th>说明</th>
                            </tr>
                        </thead>
                        <tbody>
                            {wf.params.length === 0 && (
                                <tr>
                                    <td colSpan={3} className="muted">无参数。</td>
                                </tr>
                            )}
                            {wf.params.map((p) => (
                                <tr key={p.name}>
                                    <td className="mono">{`{"$${p.name}"}`}</td>
                                    <td>{p.required ? '是' : '否'}</td>
                                    <td className="muted">{p.description}</td>
                                </tr>
                            ))}
                        </tbody>
                    </table>
                </div>
                <div className="card">
                    <h2>诊断</h2>
                    <p className="muted" style={{ fontSize: 'var(--mir-text-sm)' }}>
                        IR 校验（fail-closed）无错误。工作流定义的管理面（草稿 / 发布 / diff）依赖
                        <span className="mono"> workflow.*</span> IPC 面（设计规范 §4 前瞻依赖），当前为只读展示。
                    </p>
                </div>
            </div>
        </div>
    );
}

export function WorkflowRunPage({ workflowId, runId }: { workflowId: string; runId: string }): React.ReactElement | null {
    const { state, cancelWorkflowRun } = useHarness();
    const run = state.workflowRuns.find((r) => r.id === runId && r.workflowId === workflowId);
    if (run === undefined) {
        return null;
    }
    const status = RUN_STATUS[run.status];
    return (
        <div className="main-scroll">
            <div className="page-head">
                <h1>{run.workflowName}</h1>
                <span className={`badge is-${status.tone} ${status.blink ? 'is-blink' : ''}`}>{status.label}</span>
                <span className="sub mono">{run.id}</span>
                {run.status === 'running' && (
                    <button type="button" className="btn btn-danger" style={{ marginLeft: 'auto' }} onClick={() => cancelWorkflowRun(run.id)}>
                        <Square size={12} /> 取消运行
                    </button>
                )}
            </div>
            <div className="stack" style={{ maxWidth: 860 }}>
                <div className="run-banner">
                    <Timer size={14} /> 开始于 {new Date(run.startedAt).toLocaleString('zh-CN')} ·{' '}
                    {run.steps.filter((s) => s.status === 'ok').length}/{run.steps.length} 步成功
                </div>
                <div className="card">
                    <h2>步骤时间线</h2>
                    <div className="timeline">
                        {run.steps.map((s, i) => (
                            <div key={i} className={`tl-step is-${s.status}`}>
                                <span className="tl-dot" aria-hidden />
                                <div className="tl-label">
                                    <div className="tl-title">
                                        <span>
                                            {i + 1} · {s.title}
                                        </span>
                                        <span className="tl-dur">{duration(s.durationMs)}</span>
                                    </div>
                                    {s.log !== undefined && <div className="tl-log">{s.log}</div>}
                                </div>
                            </div>
                        ))}
                    </div>
                </div>
            </div>
        </div>
    );
}
