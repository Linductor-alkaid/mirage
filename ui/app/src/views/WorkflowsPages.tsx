/// 工作流页（与 agent harness 同壳布局）：左栏 = 已保存 workflow 列表
/// （WorkflowSidebar），主区 = RPA 工程式编辑器（无选中）或最近运行台。
/// 运行详情独立路由。定义/运行数据经 WorkflowBackend（DEC-023 IPC 适配器）
/// 来自契约面；数据源在页头标注（Mock 传输 = 模拟数据，其余 = 真实 IPC）。

import { Square, Timer } from 'lucide-react';

import { useHarness, useNow } from '../hooks.js';
import { duration, relativeTime } from '../lib/labels.js';
import type { WorkflowRunStatus } from '../state/model.js';
import { WorkflowSidebar } from './workflows/WorkflowSidebar.js';
import { WorkflowEditor } from './workflows/Editor.js';

const RUN_STATUS: Record<WorkflowRunStatus, { label: string; tone: string; blink: boolean }> = {
    queued: { label: '已创建', tone: 'muted', blink: false },
    running: { label: '运行中', tone: 'info', blink: true },
    paused: { label: '已挂起', tone: 'warning', blink: false },
    completed: { label: '已完成', tone: 'success', blink: false },
    failed: { label: '已失败', tone: 'danger', blink: false },
    cancelled: { label: '已取消', tone: 'muted', blink: false },
};

export function WorkflowsPage(): React.ReactElement {
    const { state } = useHarness();
    const editing = state.route.view === 'workflow-editor' ? state.route.workflowId : undefined;
    const simulated = state.transportLabel === 'Mock';
    const now = useNow();
    if (!state.workflowsSupported) {
        return (
            <div className="chat" data-testid="workflows-page">
                <div className="thread-col">
                    <div className="main-scroll" style={{ padding: 'var(--mir-space-5) var(--mir-space-6)' }}>
                        <div className="page-head">
                            <h1>工作流</h1>
                            <span className="sub">服务未提供工作流能力（hello 无 workflows 位）。</span>
                        </div>
                    </div>
                </div>
            </div>
        );
    }
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
                            <span className="sub">agent 在会话中可调用的自动化能力 · 编辑器遵循 RPA 工程范式 · <span className="sim-note">{simulated ? '模拟数据' : `数据源：${state.transportLabel}`}</span></span>
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
                                            <td className="muted">{relativeTime(r.startedAt, now)}</td>
                                            <td className="mono muted">
                                                {r.steps.length > 0
                                                    ? `${r.steps.filter((st) => st.status === 'ok').length}/${r.steps.length}`
                                                    : '—'}
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
    const active = run.status === 'running' || run.status === 'queued';
    return (
        <div className="main-scroll">
            <div className="page-head">
                <h1>{run.workflowName}</h1>
                <span className={`badge is-${status.tone} ${status.blink ? 'is-blink' : ''}`}>{status.label}</span>
                <span className="sub mono">{run.id}</span>
                {active && (
                    <button type="button" className="btn btn-danger" style={{ marginLeft: 'auto' }} onClick={() => cancelWorkflowRun(run.id)}>
                        <Square size={12} /> 取消运行
                    </button>
                )}
            </div>
            <div className="stack" style={{ maxWidth: 860 }}>
                <div className="run-banner">
                    <Timer size={14} /> 开始于 {new Date(run.startedAt).toLocaleString('zh-CN')}
                    {run.steps.length > 0 && <> · {run.steps.filter((s) => s.status === 'ok').length}/{run.steps.length} 步成功</>}
                </div>
                {run.summary !== undefined && (
                    <div className="card">
                        <h2>结果摘要</h2>
                        <p className="muted" style={{ fontSize: 12 }}>{run.summary}</p>
                    </div>
                )}
                <div className="card">
                    <h2>步骤时间线</h2>
                    {run.steps.length > 0 ? (
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
                    ) : (
                        <p className="muted" style={{ fontSize: 12 }}>
                            步级事件尚未进入 wire（DEC-023 挂账）；运行状态以 workflow.runs 快照为准，
                            步级时间线随观察台事件面（M5-06）进入。
                        </p>
                    )}
                </div>
            </div>
        </div>
    );
}
