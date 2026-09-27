/// 观察台（RunDrawer）：当前运行时间线 + 观察流。
/// - 运行时间线：任务快照投影（task.inspect，快照事实源）。
/// - 观察流：真实事件尾随（session.turn / session.output / session.message，
///   有界环形缓冲）——通知面语义，丢帧不回补（DEC-025 决策 5）。
///   trigger-lock：有活动任务时自动跟随最新帧；用户上滚即锁定，出现
///   「已锁定 · 回到实时」cue；点击 cue 或回到底部解除锁定。

import { useEffect, useMemo, useRef, useState } from 'react';
import { Activity, Pin, PinOff } from 'lucide-react';

import { useHarness } from '../../hooks.js';
import { duration, kindLabel } from '../../lib/labels.js';
import { displayStepsOf, isTerminalProgress } from '../../state/store.js';

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

/** 观察流：真实事件帧（观察台 store 缓冲）+ 线程活动投影，配合 trigger-lock。 */
function ObservationStream({ live, sessionId }: { live: boolean; sessionId: string | undefined }): React.ReactElement {
    const { state } = useHarness();
    const frames = useMemo(() => {
        if (sessionId === undefined) {
            return [];
        }
        return (state.obsFeed.get(sessionId) ?? []).map((f) => ({
            key: f.id,
            at: f.at,
            text: f.text,
        }));
    }, [state, sessionId]);
    // trigger-lock：following 是可写状态（滚动/点击改写）；live 代次翻转时
    // 重置为跟随。prop→state 调整用 React 官方的渲染期调整模式（不进 effect）。
    const [followGen, setFollowGen] = useState(live);
    const [following, setFollowing] = useState(true);
    if (followGen !== live) {
        setFollowGen(live);
        setFollowing(true);
    }
    const locked = !following && live;
    const boxRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        if (!following) {
            return;
        }
        const box = boxRef.current;
        if (box !== null) {
            box.scrollTop = box.scrollHeight;
        }
    }, [frames.length, following]);

    const onScroll = (): void => {
        const box = boxRef.current;
        if (box === null) {
            return;
        }
        setFollowing(box.scrollHeight - box.scrollTop - box.clientHeight < 24);
    };

    return (
        <div style={{ position: 'relative' }}>
            {locked && (
                <button
                    type="button"
                    className="obs-lock-cue"
                    onClick={() => setFollowing(true)}
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
                {frames.length === 0 && (
                    <span className="muted">
                        暂无观察帧。观察流跟随会话事件（轮次结算 / 输出 / 消息）实时追加。
                    </span>
                )}
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
                    <ObservationStream live={live} sessionId={sid} />
                </div>
            </div>
        </aside>
    );
}
