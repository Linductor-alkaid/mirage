/// 线程消息渲染器：按消息 kind 分发（契约路径，DEC-025）。
/// - user / outcome：会话面投影（session.history 基线 + session.message 增量）
/// - step：工具调用卡（kindLabel 键控 + 失败样式 + 结果/错误详情）
/// - activity / system：活动行 / 系统行
/// 模拟域渲染器（assistant / approval / snapshot / workflow-call）随模拟域
/// 退役；批准卡属 M5-07 真实批准面。

import { CircleAlert, Eye, FileSearch, Terminal } from 'lucide-react';

import { clock, kindLabel, progressLabel, stepStatusLabel } from '../../lib/labels.js';
import { outcomeTone } from '../../state/store.js';
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

export function MessageView({ m }: { m: ChatMessage }): React.ReactElement {
    switch (m.kind) {
        case 'user':
            return (
                <div className="msg msg-user" data-testid="msg-user">
                    <div className="body">{m.text}</div>
                </div>
            );
        case 'outcome':
            return (
                <div className={`system-line is-${outcomeTone(m.text)}`} data-testid="msg-outcome">
                    <CircleAlert size={13} aria-hidden style={{ verticalAlign: -2, marginRight: 4 }} />
                    {m.text}
                    <span className="muted" style={{ marginLeft: 6 }}>
                        {clock(m.at)}
                    </span>
                </div>
            );
        case 'step':
            return <StepCard m={m} />;
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
                <div className={`system-line is-${m.tone}`}>
                    {m.text}
                    <span className="muted" style={{ marginLeft: 6 }}>
                        {clock(m.at)}
                    </span>
                </div>
            );
    }
}
