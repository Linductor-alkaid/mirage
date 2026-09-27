/// Composer：对话 / 执行双模式。
/// - 对话：模型循环未引入（DEC-008 迁移路径第二步），入口如实降级——
///   禁用并标注原因，不以本地模拟冒充会话面（DEC-025 决策 3）。
/// - 执行：目标 + 步骤构建器（task.submit 会话绑定事实流）。
/// - 运行中变为「停止」。演示权限 dock 随模拟域退役，真实批准面属 M5-07。

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
    { kind: 'process.execute', argument: '' },
];

export function Composer({ sessionId }: { sessionId: string }): React.ReactElement {
    const { state, setDraft, submitExec, stopSession } = useHarness();
    const meta = state.sessions.find((s) => s.id === sessionId);
    const [chatPreferred, setChatPreferred] = useState(false);
    const [steps, setSteps] = useState<StepRow[]>([...DEFAULT_STEPS]);
    const [submitting, setSubmitting] = useState(false);

    // 模式状态保留信息架构（设计规范 §3.3）；对话模式当前不可用，
    // 选择它时呈现降级说明而不是输入面。
    const mode: 'chat' | 'exec' = chatPreferred ? 'chat' : 'exec';

    const text = state.drafts.get(sessionId) ?? '';
    const hasText = text.trim().length > 0;

    const setText = (v: string): void => setDraft(sessionId, v);

    const submit = (): void => {
        if (mode === 'chat') {
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

    const isBusy = submitting || (meta?.taskId !== undefined && state.activeTaskIds.includes(meta.taskId));

    return (
        <div className="composer-wrap">
            <div className={`composer ${hasText && mode === 'exec' ? 'is-live' : ''}`}>
                <div className="composer-modes" role="tablist" aria-label="会话模式">
                    <button
                        type="button"
                        role="tab"
                        aria-selected={mode === 'chat'}
                        className={`mode-btn ${mode === 'chat' ? 'is-active' : ''}`}
                        onClick={() => setChatPreferred(true)}
                    >
                        <MessageSquare size={12} /> 对话
                    </button>
                    <button
                        type="button"
                        role="tab"
                        aria-selected={mode === 'exec'}
                        className={`mode-btn ${mode === 'exec' ? 'is-active' : ''}`}
                        onClick={() => setChatPreferred(false)}
                    >
                        <Play size={12} /> 执行
                    </button>
                </div>

                {mode === 'chat' && (
                    <div className="system-line is-warn" data-testid="chat-degraded" style={{ margin: '4px 8px' }}>
                        对话模式不可用：模型循环尚未接入（DEC-008 迁移路径第二步）。
                        要让 Agent 实际操作桌面，请切到执行模式提交目标。
                    </div>
                )}

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
                                    placeholder={i === 0 ? '如 ci/logs/nightly.log' : '如 npm test'}
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

                {mode === 'exec' && (
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
                            placeholder="描述目标……（Ctrl+Enter 提交为协议任务）"
                            aria-label="执行目标输入"
                            data-testid="composer-input"
                        />
                    </div>
                )}
                <div className="composer-foot">
                    <span className="hint">
                        {mode === 'chat' ? '对话模式不可用' : 'Ctrl+Enter 提交'} · <span className="kbd">Ctrl K</span> 命令面板
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
                            disabled={mode === 'chat' || !hasText || steps.every((s) => s.argument.trim().length === 0)}
                            onClick={submit}
                        >
                            提交任务
                        </button>
                    )}
                </div>
            </div>
        </div>
    );
}
