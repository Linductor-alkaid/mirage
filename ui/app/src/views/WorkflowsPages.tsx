/// 工作流页（与 agent harness 同壳布局）：左栏 = 已保存 workflow 列表
/// （WorkflowSidebar），主区 = RPA 工程式编辑器（无选中）或最近运行台。
/// 运行详情独立路由。定义/运行数据来自 WorkflowBackend（当前 mock，标注）。

import { Square, Timer } from 'lucide-react';

import { useHarness } from '../hooks.js';
import { duration, relativeTime } from '../lib/labels.js';
import type { WorkflowRunStatus } from '../state/model.js';
import { WorkflowSidebar } from './workflows/WorkflowSidebar.js';
import { WorkflowEditor } from './workflows/Editor.js';

const RUN_STATUS: Record<WorkflowRunStatus, { label: string; tone: string; blink: boolean }> = {
    running: { label: '运行中', tone: 'info', blink: true },
    completed: { label: '已完成', tone: 'success', blink: false },
    failed: { label: '已失败', tone: 'danger', blink: false },
    cancelled: { label: '已取消', tone: 'muted', blink: false },
};

export function WorkflowsPage(): React.ReactElement {
    const { state } = useHarness();
    const editing = state.route.view === 'workflow-editor' ? state.route.workflowId : undefined;
    return (
        <div className="chat" data-testid="workflows-page">
            <WorkflowSidebar />
            <div className="thread-col">
                {editing !== undefined ? (
                    <WorkflowEditor key={editing} workflowId={editing} />
                ) : (
                    <div className="main-scroll" style={{ padding: 'var(--mir-space-5) var(--mir-space-6)' }}>
                        <div className="page-head">
                            <h1>工作流</h1>
                            <span className="sub">agent 在会话中可调用的自动化能力 · 编辑器遵循 RPA 工程范式 · <span className="sim-note">模拟数据</span></span>
                        </div>
                        <div className="card" style={{ maxWidth: 760 }}>
                            <h2>最近运行</h2>
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
                                        <tr key={r.id} onClick={() => navigateToRun(r.workflowId, r.id)}>
                                            <td>{r.workflowName}</td>
                                            <td>
                                                <span className={`badge is-${RUN_STATUS[r.status].tone} ${RUN_STATUS[r.status].blink ? 'is-blink' : ''}`}>
                                                    {RUN_STATUS[r.status].label}
                                                </span>
                                            </td>
                                            <td className="muted">{relativeTime(r.startedAt, Date.now())}</td>
                                            <td className="mono muted">
                                                {r.steps.filter((st) => st.status === 'ok').length}/{r.steps.length}
                                            </td>
                                        </tr>
                                    ))}
                                    {state.workflowRuns.length === 0 && (
                                        <tr><td colSpan={4} className="muted">暂无运行记录。</td></tr>
                                    )}
                                </tbody>
                            </table>
                        </div>
                    </div>
                )}
            </div>
        </div>
    );
}

function navigateToRun(workflowId: string, runId: string): void {
    window.location.hash = `#/workflows/${encodeURIComponent(workflowId)}/runs/${encodeURIComponent(runId)}`;
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
