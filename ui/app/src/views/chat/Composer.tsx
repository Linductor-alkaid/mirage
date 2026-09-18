/// Composer：对话 / 执行双模式。
/// - 对话：纯交流，不产生桌面动作（本地模拟回复）。
/// - 执行：目标 + 步骤构建器（协议 task.submit 事实流）；`approve:` 前缀
///   的步骤参数会触发演示权限请求卡（mock 约定）。
/// - 运行中变为「停止」；Dock 区承载本会话待决权限请求。

import { useState } from 'react';
import { MessageSquare, Play, Plus, Square, Trash2 } from 'lucide-react';

import { useHarness } from '../../hooks.js';
import type { SubmitStepInput } from '../../state/store.js';

interface StepRow {
    kind: SubmitStepInput['kind'];
    argument: string;
}

const DEFAULT_STEPS: readonly StepRow[] = [
    { kind: 'filesystem.read', argument: '' },
    { kind: 'process.execute', argument: 'approve: npm test' },
];

export function Composer({ sessionId }: { sessionId: string }): React.ReactElement {
    const { state, setDraft, sendChat, submitExec, stopSession } = useHarness();
    const meta = state.sessions.find((s) => s.id === sessionId);
    const [mode, setMode] = useState<'chat' | 'exec'>(meta?.mode ?? 'exec');
    const [steps, setSteps] = useState<StepRow[]>([...DEFAULT_STEPS]);
    const [submitting, setSubmitting] = useState(false);

    const text = state.drafts.get(sessionId) ?? '';
    const hasText = text.trim().length > 0;
    const busy = state.simBusySession === sessionId;

    const setText = (v: string): void => setDraft(sessionId, v);

    const submit = (): void => {
        if (mode === 'chat') {
            sendChat(sessionId, text);
            return;
        }
        const usable = steps.filter((s) => s.argument.trim().length > 0);
        if (!hasText || usable.length === 0 || submitting) {
            return;
        }
        setSubmitting(true);
        void submitExec(sessionId, text, usable).finally(() => {
            setSubmitting(false);
            setSteps([...DEFAULT_STEPS]);
        });
    };

    const isBusy = busy || submitting || (meta?.taskId !== undefined && state.activeTaskIds.includes(meta.taskId));

    return (
        <div className="composer-wrap">
            <div className={`composer ${hasText || state.pendingApprovals.length > 0 ? 'is-live' : ''}`}>
                {state.pendingApprovals.length > 0 && (
                    <div className="composer-dock" data-testid="composer-dock">
                        {state.pendingApprovals.map((a) => (
                            <span key={a.id} className="dock-item">
                                ⚠ 待放行：{a.summary}（见线程中的批准卡或批准中心）
                            </span>
                        ))}
                    </div>
                )}
                <div className="composer-modes" role="tablist" aria-label="会话模式">
                    <button
                        type="button"
                        role="tab"
                        aria-selected={mode === 'chat'}
                        className={`mode-btn ${mode === 'chat' ? 'is-active' : ''}`}
                        onClick={() => setMode('chat')}
                    >
                        <MessageSquare size={12} /> 对话
                    </button>
                    <button
                        type="button"
                        role="tab"
                        aria-selected={mode === 'exec'}
                        className={`mode-btn ${mode === 'exec' ? 'is-active' : ''}`}
                        onClick={() => setMode('exec')}
                    >
                        <Play size={12} /> 执行
                    </button>
                </div>

                {mode === 'exec' && (
                    <div className="steps-builder" data-testid="steps-builder">
                        {steps.map((s, i) => (
                            <div className="step-row" key={i}>
                                <select
                                    className="select"
                                    value={s.kind}
                                    aria-label={`步骤 ${i + 1} 类型`}
                                    onChange={(e) =>
                                        setSteps(steps.map((row, j) =>
                                            j === i ? { ...row, kind: e.target.value as StepRow['kind'] } : row,
                                        ))
                                    }
                                >
                                    <option value="filesystem.read">文件读取</option>
                                    <option value="process.execute">命令执行</option>
                                </select>
                                <input
                                    className="input"
                                    value={s.argument}
                                    placeholder={i === 0 ? '如 ci/logs/nightly.log' : '如 npm test（approve: 前缀触发演示审批）'}
                                    aria-label={`步骤 ${i + 1} 参数`}
                                    onChange={(e) =>
                                        setSteps(steps.map((row, j) =>
                                            j === i ? { ...row, argument: e.target.value } : row,
                                        ))
                                    }
                                />
                                <button
                                    type="button"
                                    className="btn btn-ghost"
                                    style={{ height: 24 }}
                                    title="移除该步骤"
                                    aria-label={`移除步骤 ${i + 1}`}
                                    onClick={() => setSteps(steps.filter((_, j) => j !== i))}
                                >
                                    <Trash2 size={13} />
                                </button>
                            </div>
                        ))}
                        <button
                            type="button"
                            className="btn btn-ghost"
                            style={{ width: 'fit-content', height: 24 }}
                            onClick={() => setSteps([...steps, { kind: 'process.execute', argument: '' }])}
                        >
                            <Plus size={13} /> 添加步骤
                        </button>
                    </div>
                )}

                <div className="composer-main">
                    <textarea
                        value={text}
                        onChange={(e) => setText(e.target.value)}
                        onKeyDown={(e) => {
                            if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
                                e.preventDefault();
                                submit();
                            }
                        }}
                        placeholder={
                            mode === 'chat'
                                ? '继续对话……（对话模式不产生桌面动作）'
                                : '描述目标……（Ctrl+Enter 提交为协议任务）'
                        }
                        aria-label={mode === 'chat' ? '对话输入' : '执行目标输入'}
                        data-testid="composer-input"
                    />
                </div>
                <div className="composer-foot">
                    <span className="hint">
                        {mode === 'chat' ? 'Enter 发送' : 'Ctrl+Enter 提交'} · <span className="kbd">Ctrl K</span> 命令面板
                    </span>
                    {isBusy ? (
                        <button type="button" className="btn btn-danger" data-testid="stop" onClick={() => stopSession(sessionId)}>
                            <Square size={13} /> 停止
                        </button>
                    ) : (
                        <button
                            type="button"
                            className="btn btn-primary"
                            data-testid="send"
                            disabled={!hasText || (mode === 'exec' && steps.every((s) => s.argument.trim().length === 0))}
                            onClick={submit}
                        >
                            {mode === 'chat' ? '发送' : '提交任务'}
                        </button>
                    )}
                </div>
            </div>
        </div>
    );
}
