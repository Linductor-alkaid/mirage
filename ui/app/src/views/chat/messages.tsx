/// 线程消息渲染器：按消息 kind 分发（DeepSeek/opencode/DSH 共性映射）。
/// - assistant：思维链折叠面板（"已思考 Ns"）+ markdown 正文（流式光标）
/// - step：工具调用卡（kindLabel 键控 + 失败样式 + 结果/错误详情）
/// - approval：批准卡（放行 / 拒止 —— 金色主动作线舞台）
/// - snapshot：观察快照卡（网格占位 + 模拟标注）
/// - activity / system：活动行 / 系统行

import { Check, ChevronRight, CircleAlert, Eye, FileSearch, Terminal, Workflow, X } from 'lucide-react';

import { useHarness } from '../../hooks.js';
import { renderMarkdown } from '../../lib/markdown.js';
import {
    approvalKindLabel,
    clock,
    kindLabel,
    progressLabel,
    stepStatusLabel,
    thinkingSeconds,
} from '../../lib/labels.js';
import type { ChatMessage } from '../../state/model.js';

function ToolIcon({ kind }: { kind: string }): React.ReactElement {
    if (kind === 'filesystem.read') {
        return <FileSearch size={13} />;
    }
    if (kind === 'process.execute') {
        return <Terminal size={13} />;
    }
    return <Eye size={13} />;
}

function ThinkingPanel({ m }: { m: Extract<ChatMessage, { kind: 'assistant' }> }): React.ReactElement | null {
    if (m.thinking === undefined) {
        return null;
    }
    const live = m.streaming === true && m.text.length === 0;
    return (
        <details className={`think ${live ? 'is-live' : ''}`} open={live}>
            <summary>
                <ChevronRight size={12} aria-hidden />
                {live ? '思考中…' : `已思考 ${thinkingSeconds(m.thinking.elapsedMs)} 秒`}
            </summary>
            <div className="think-body">{m.thinking.text}</div>
        </details>
    );
}

function StepCard({ m }: { m: Extract<ChatMessage, { kind: 'step' }> }): React.ReactElement {
    const step = m.step;
    const failed = step.status === 'failed';
    return (
        <div className={`toolcard ${failed ? 'is-failed' : ''}`} data-testid="tool-card">
            <div className="toolcard-head">
                <span className="t-kind">
                    <ToolIcon kind={step.kind} />
                    {kindLabel(step.kind)}
                </span>
                <span className="t-arg">{step.argument}</span>
                <span
                    className={`badge is-${
                        step.status === 'ok'
                            ? 'success'
                            : step.status === 'failed'
                              ? 'danger'
                              : step.status === 'running'
                                ? 'info'
                                : 'muted'
                    } ${step.status === 'running' ? 'is-blink' : ''}`}
                >
                    {stepStatusLabel(step.status)}
                </span>
            </div>
            {(step.result.length > 0 || step.error.length > 0 || step.exitCode !== 0) && (
                <details className="toolcard-detail" open={failed}>
                    <summary className="muted" style={{ cursor: 'pointer', fontSize: 12 }}>
                        执行证据
                    </summary>
                    <div className="kv">
                        <span className="k">exit</span>
                        <span className="mono">{step.exitCode}</span>
                    </div>
                    {step.result.length > 0 && (
                        <div className="result" title={step.result}>
                            {step.result}
                        </div>
                    )}
                    {step.error.length > 0 && <div className="result is-error">{step.error}</div>}
                </details>
            )}
        </div>
    );
}

function ApprovalCard({ m }: { m: Extract<ChatMessage, { kind: 'approval' }> }): React.ReactElement {
    const { decideApproval } = useHarness();
    const a = m.approval;
    const decided = a.status !== 'pending';
    return (
        <div
            className={`approval ${a.status === 'approved' ? 'is-approved' : a.status === 'denied' ? 'is-denied' : ''} ${decided ? 'is-decided' : ''}`}
            data-testid="approval-card"
        >
            <div className="approval-head">
                <CircleAlert size={14} style={{ color: 'var(--primary)' }} aria-hidden />
                <span>权限请求 · {a.summary}</span>
                <span className="a-kind">{approvalKindLabel(a.kind)}</span>
                <span className="sim-note" title="会话内权限请求为模拟语义（M2+ 权限事件面之前）">模拟</span>
            </div>
            {a.detail !== undefined && <div className="approval-detail">{a.detail}</div>}
            {!decided && (
                <div className="approval-actions">
                    <button type="button" className="btn btn-primary" data-testid="approve" onClick={() => decideApproval(a.id, true)}>
                        <Check size={14} /> 放行
                    </button>
                    <button type="button" className="btn btn-danger" data-testid="deny" onClick={() => decideApproval(a.id, false)}>
                        <X size={14} /> 拒止
                    </button>
                </div>
            )}
            {decided && (
                <span className="verdict" style={{ color: a.status === 'approved' ? 'var(--success)' : 'var(--destructive)' }}>
                    {a.status === 'approved' ? <Check size={13} /> : <X size={13} />}
                    {a.status === 'approved' ? '已放行' : '已拒止'}
                    {a.decidedAt !== undefined && <span className="muted">· {clock(a.decidedAt)}</span>}
                </span>
            )}
        </div>
    );
}

const SOM_BOXES: readonly { x: number; y: number; w: number; h: number }[] = [
    { x: 8, y: 10, w: 30, h: 20 },
    { x: 52, y: 8, w: 36, h: 16 },
    { x: 12, y: 48, w: 38, h: 26 },
    { x: 60, y: 44, w: 28, h: 34 },
];

/** agent 在会话上下文中调用工作流（工具卡，深链运行详情）。 */
function WorkflowCallCard({ m }: { m: Extract<ChatMessage, { kind: 'workflow-call' }> }): React.ReactElement {
    const { navigate } = useHarness();
    const c = m.call;
    const paramText = Object.entries(c.params)
        .map(([k, v]) => `${k}=${v}`)
        .join(' · ');
    const tone = c.status === 'completed' ? 'success' : c.status === 'failed' ? 'danger' : c.status === 'running' ? 'info' : 'muted';
    const label = c.status === 'completed' ? '已完成' : c.status === 'failed' ? '已失败' : c.status === 'running' ? '运行中' : '已取消';
    return (
        <div className="toolcard" data-testid="workflow-call-card">
            <button
                type="button"
                className="toolcard-head"
                style={{ width: '100%', textAlign: 'start', background: 'transparent', border: 'none', cursor: 'pointer' }}
                onClick={() => c.runId !== undefined && navigate({ view: 'workflow-run', workflowId: c.workflowId, runId: c.runId })}
                title={c.runId !== undefined ? '查看运行详情' : undefined}
            >
                <span className="t-kind">
                    <Workflow size={13} />
                    工作流
                </span>
                <span className="t-arg">
                    {c.workflowName} · {c.version}
                    {paramText.length > 0 ? ` · ${paramText}` : ''}
                </span>
                <span className={`badge is-${tone} ${c.status === 'running' ? 'is-blink' : ''}`}>{label}</span>
            </button>
            {c.runId !== undefined && (
                <div className="toolcard-detail">
                    <div className="kv">
                        <span className="k">run</span>
                        <span className="mono">{c.runId}</span>
                    </div>
                </div>
            )}
        </div>
    );
}

function SnapshotCard({ m }: { m: Extract<ChatMessage, { kind: 'snapshot' }> }): React.ReactElement {
    const s = m.snapshot;
    return (
        <figure className="snapcard" data-testid="snapshot-card">
            <div className="snap-grid">
                {SOM_BOXES.map((b, i) => (
                    <span
                        key={i}
                        className="snap-box"
                        style={{ left: `${b.x}%`, top: `${b.y}%`, width: `${b.w}%`, height: `${b.h}%` }}
                    >
                        <b>[{i + 1}]</b>
                    </span>
                ))}
                <span className="tag">SoM 标注 {s.grid[0]}×{s.grid[1]}</span>
            </div>
            <figcaption className="snap-cap">
                <Eye size={13} aria-hidden />
                <span className="s-label">{s.label}</span>
                <span>{s.detail}</span>
                <span className="snap-sim">模拟快照</span>
            </figcaption>
        </figure>
    );
}

export function MessageView({ m }: { m: ChatMessage }): React.ReactElement {
    switch (m.kind) {
        case 'user':
            return (
                <div className="msg msg-user" data-testid="msg-user">
                    <div className="body">{m.text}</div>
                </div>
            );
        case 'assistant':
            return (
                <div className="msg msg-assistant" data-testid="msg-assistant">
                    <ThinkingPanel m={m} />
                    <div
                        className={`body ${m.streaming === true ? 'caret-blink' : ''}`}
                        dangerouslySetInnerHTML={{ __html: renderMarkdown(m.text) }}
                    />
                </div>
            );
        case 'step':
            return <StepCard m={m} />;
        case 'workflow-call':
            return <WorkflowCallCard m={m} />;
        case 'approval':
            return <ApprovalCard m={m} />;
        case 'snapshot':
            return <SnapshotCard m={m} />;
        case 'activity':
            return (
                <div className="activity-line" data-testid="activity-line">
                    <span className="mono">{m.taskId}</span>
                    <span>
                        {progressLabel(m.progress)}
                        {m.note !== undefined ? ` · ${m.note}` : ''}
                    </span>
                    <span className="muted">{clock(m.at)}</span>
                </div>
            );
        case 'system':
            return (
                <div className={`system-line is-${m.tone === 'info' ? 'info' : m.tone}`}>{m.text}</div>
            );
    }
}
