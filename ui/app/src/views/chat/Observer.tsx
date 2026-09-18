/// 观察台（RunDrawer）：当前运行时间线 + 观察流 + 上下文用量。
/// 观察流实现 trigger-lock 语义：有活动任务时自动跟随最新帧；用户上滚
/// 即锁定，出现「已锁定 · 回到实时」cue；点击 cue 或回到底部解除锁定。

import { useEffect, useMemo, useRef, useState } from 'react';
import { Activity, Gauge, Pin, PinOff } from 'lucide-react';

import { useHarness } from '../../hooks.js';
import { duration, kindLabel, stepStatusLabel } from '../../lib/labels.js';
import { displayStepsOf, isTerminalProgress } from '../../state/store.js';
import { seedContext } from '../../state/harness-mock.js';

interface TimelineItem {
    key: string;
    title: string;
    status: 'pending' | 'running' | 'ok' | 'failed' | 'skipped';
    detail?: string;
    durationMs?: number;
}

function useTimeline(): { items: TimelineItem[]; live: boolean; source: 'task' | 'thread' } {
    const { state } = useHarness();
    return useMemo(() => {
        const sid = state.route.view === 'chat' ? state.route.sessionId : undefined;
        const session = state.sessions.find((s) => s.id === sid);
        if (session?.taskId !== undefined) {
            const detail = state.taskDetails.get(session.taskId);
            if (detail !== undefined) {
                return {
                    live: !isTerminalProgress(detail.progress),
                    source: 'task' as const,
                    items: displayStepsOf(detail, state.taskInputs.get(session.taskId)).map((s) => ({
                        key: s.operationId,
                        title: `${s.index + 1} · ${kindLabel(s.kind)}`,
                        status:
                            s.status === 'ok'
                                ? ('ok' as const)
                                : s.status === 'failed'
                                  ? ('failed' as const)
                                  : s.status === 'running'
                                    ? ('running' as const)
                                    : s.status === 'cancelled'
                                      ? ('skipped' as const)
                                      : ('pending' as const),
                        detail: s.argument,
                        durationMs: undefined,
                    })),
                };
            }
        }
        const list = sid !== undefined ? state.messages.get(sid) ?? [] : [];
        const steps = list.filter((m): m is Extract<(typeof list)[number], { kind: 'step' }> => m.kind === 'step');
        return {
            live: false,
            source: 'thread' as const,
            items: steps.map((m) => ({
                key: m.step.operationId,
                title: `${m.step.index + 1} · ${kindLabel(m.step.kind)}`,
                status:
                    m.step.status === 'ok'
                        ? ('ok' as const)
                        : m.step.status === 'failed'
                          ? ('failed' as const)
                          : m.step.status === 'running'
                            ? ('running' as const)
                            : ('pending' as const),
                detail: m.step.argument,
            })),
        };
    }, [state]);
}

/** 观察流：模拟桌面回流帧（有界滚动缓冲，配合 trigger-lock）。 */
function ObservationStream({ live }: { live: boolean }): React.ReactElement {
    const { state } = useHarness();
    const frames = useMemo(() => {
        const sid = state.route.view === 'chat' ? state.route.sessionId : undefined;
        const list = sid !== undefined ? state.messages.get(sid) ?? [] : [];
        const out: { key: string; at: number; text: string }[] = [];
        for (const m of list) {
            if (m.kind === 'step') {
                out.push({ key: m.id, at: m.at, text: `observe · ${kindLabel(m.step.kind)} → ${stepStatusLabel(m.step.status)}` });
            } else if (m.kind === 'snapshot') {
                out.push({ key: m.id, at: m.at, text: `snapshot · ${m.snapshot.label}` });
            } else if (m.kind === 'activity') {
                out.push({ key: m.id, at: m.at, text: `task · ${m.progress}` });
            }
        }
        return out.slice(-40);
    }, [state]);
    const [locked, setLocked] = useState(false);
    const boxRef = useRef<HTMLDivElement>(null);
    const followRef = useRef(true);

    useEffect(() => {
        if (!followRef.current) {
            return;
        }
        const box = boxRef.current;
        if (box !== null) {
            box.scrollTop = box.scrollHeight;
        }
    }, [frames.length]);

    useEffect(() => {
        if (!live) {
            followRef.current = true;
            setLocked(false);
        }
    }, [live]);

    const onScroll = (): void => {
        const box = boxRef.current;
        if (box === null) {
            return;
        }
        const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 24;
        followRef.current = atBottom;
        setLocked(!atBottom && live);
    };

    return (
        <div style={{ position: 'relative' }}>
            {locked && (
                <button
                    type="button"
                    className="obs-lock-cue"
                    onClick={() => {
                        followRef.current = true;
                        setLocked(false);
                        const box = boxRef.current;
                        if (box !== null) {
                            box.scrollTop = box.scrollHeight;
                        }
                    }}
                >
                    已锁定 · 回到实时
                </button>
            )}
            <div
                ref={boxRef}
                className="obs-stream"
                onScroll={onScroll}
                data-testid="obs-stream"
                aria-label="观察流"
            >
                {frames.length === 0 && <span className="muted">暂无观察帧。</span>}
                {frames.map((f) => (
                    <span key={f.key}>
                        <span style={{ opacity: 0.6 }}>{new Date(f.at).toLocaleTimeString('zh-CN', { hour12: false })}</span>{' '}
                        {f.text}
                    </span>
                ))}
            </div>
        </div>
    );
}

function ContextMeter(): React.ReactElement {
    const { state } = useHarness();
    const sid = state.route.view === 'chat' ? state.route.sessionId : undefined;
    const ctx = (sid !== undefined ? state.context.get(sid) : undefined) ?? seedContext();
    const pct = Math.min(100, Math.round((ctx.usedTokens / ctx.budgetTokens) * 100));
    const seg = (n: number): string => `${Math.min(100, (n / ctx.budgetTokens) * 100).toFixed(2)}%`;
    return (
        <div className="ctx-meter" data-testid="context-meter">
            <span className="sec-title caps" style={{ display: 'flex' }}>
                <Gauge size={12} /> 上下文占用 <span className="mono" style={{ marginLeft: 'auto' }}>{pct}%</span>
            </span>
            <div className="ctx-bar" role="img" aria-label={`上下文占用 ${pct}%`}>
                <i className="is-system" style={{ width: seg(ctx.breakdown.system) }} />
                <i className="is-history" style={{ width: seg(ctx.breakdown.history) }} />
                <i className="is-tools" style={{ width: seg(ctx.breakdown.tools) }} />
            </div>
            <div className="ctx-legend">
                <span className="li is-system">系统 {(ctx.breakdown.system / 1000).toFixed(1)}k</span>
                <span className="li is-history">历史 {(ctx.breakdown.history / 1000).toFixed(1)}k</span>
                <span className="li is-tools">工具 {(ctx.breakdown.tools / 1000).toFixed(1)}k</span>
            </div>
        </div>
    );
}

export function Observer({
    open,
    onToggle,
}: {
    open: boolean;
    onToggle(): void;
}): React.ReactElement | null {
    const { state } = useHarness();
    const { items, live, source } = useTimeline();
    if (!open) {
        return null;
    }
    const sid = state.route.view === 'chat' ? state.route.sessionId : undefined;
    const session = state.sessions.find((s) => s.id === sid);
    return (
        <aside className="observer" aria-label="运行观察台">
            <div className="observer-head">
                <span className="title">观察台</span>
                {live && <span className="badge is-info is-blink">跟随中</span>}
                <button type="button" className="btn btn-ghost" style={{ height: 22 }} onClick={onToggle} title="收起观察台">
                    <PinOff size={13} />
                </button>
            </div>
            <div className="observer-body">
                <div className="obs-section">
                    <span className="sec-title caps">
                        <Activity size={12} /> 运行时间线
                        <span className="muted" style={{ textTransform: 'none', letterSpacing: 0 }}>
                            {source === 'task' ? '· 契约快照' : '· 会话记录'}
                        </span>
                    </span>
                    {items.length === 0 ? (
                        <span className="muted" style={{ fontSize: 12 }}>
                            {session === undefined ? '没有选中会话。' : '该会话尚无步骤。'}
                        </span>
                    ) : (
                        <div className="timeline" data-testid="timeline">
                            {items.map((it) => (
                                <div key={it.key} className={`tl-step is-${it.status}`}>
                                    <span className="tl-dot" aria-hidden />
                                    <div className="tl-label">
                                        <div className="tl-title">
                                            <span>{it.title}</span>
                                            <span className="tl-dur">{duration(it.durationMs)}</span>
                                        </div>
                                        {it.detail !== undefined && <div className="tl-log">{it.detail}</div>}
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </div>
                <div className="obs-section">
                    <span className="sec-title caps">
                        <Pin size={12} /> 观察流
                    </span>
                    <ObservationStream live={live} />
                </div>
                <ContextMeter />
            </div>
        </aside>
    );
}
