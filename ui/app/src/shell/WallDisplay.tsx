/// 壁挂屏（wall display）：任务剖面的公开真相。
/// 左：大字状态动词（状态换幕）；中：蓝线任务剖面（步骤节点推进）；
/// 右：主机灯阵（4 联告警瓦片）。一切从契约事实派生，不做装饰性演出。

import { useEffect, useMemo, useRef, useState } from 'react';

import { progressLabel } from '../lib/labels.js';
import { useHarness } from '../hooks.js';

interface WallState {
    verb: string;
    goal?: string;
    takeover: boolean;
}

function useWallState(): WallState {
    const { state } = useHarness();
    if (state.takeover) {
        return { verb: '紧急停止', takeover: true };
    }
    if (state.connection === 'error') {
        return { verb: '连接中断', takeover: false, goal: state.connectionError };
    }
    if (state.connection === 'connecting') {
        return { verb: '连接中', takeover: false };
    }
    if (state.hostStatus === 'failed') {
        return { verb: '主机故障', takeover: false };
    }
    const activeId = state.activeTaskIds[0];
    if (activeId !== undefined) {
        const detail = state.taskDetails.get(activeId);
        const session = state.sessions.find((s) => s.taskId === activeId);
        return {
            verb: detail?.progress === 'Cancelling' ? '取消中' : '执行中',
            goal: session?.title ?? `任务 ${activeId}`,
            takeover: false,
        };
    }
    if (state.hostStatus === 'stopped') {
        return { verb: '待命', takeover: false };
    }
    return { verb: '就绪', takeover: false, goal: '提交一个目标，或继续对话' };
}

/** 蓝线任务剖面：优先直连活动任务快照；否则取会话内最近步骤。 */
function useProfileNodes(): { label: string; status: 'done' | 'active' | 'todo' | 'failed' }[] {
    const { state } = useHarness();
    return useMemo(() => {
        const activeId = state.activeTaskIds[0];
        const detail = activeId !== undefined ? state.taskDetails.get(activeId) : undefined;
        if (detail !== undefined && detail.steps.length > 0) {
            return detail.steps.slice(-8).map((s) => ({
                label: `${s.index + 1}`,
                status:
                    s.status === 'ok'
                        ? ('done' as const)
                        : s.status === 'failed'
                          ? ('failed' as const)
                          : s.status === 'running'
                            ? ('active' as const)
                            : ('todo' as const),
            }));
        }
        if (state.route.view === 'chat') {
            const sid = state.route.sessionId;
            const list = sid !== undefined ? state.messages.get(sid) ?? [] : [];
            const steps = list
                .filter((m): m is Extract<(typeof list)[number], { kind: 'step' }> => m.kind === 'step')
                .slice(-8);
            if (steps.length > 0) {
                return steps.map((m) => ({
                    label: `${m.step.index + 1}`,
                    status:
                        m.step.status === 'ok'
                            ? ('done' as const)
                            : m.step.status === 'failed'
                              ? ('failed' as const)
                              : m.step.status === 'running'
                                ? ('active' as const)
                                : ('todo' as const),
                }));
            }
        }
        return [];
    }, [state]);
}

function MissionProfile(): React.ReactElement {
    const nodes = useProfileNodes();
    const width = 560;
    const height = 56;
    if (nodes.length === 0) {
        return (
            <div className="wall-profile" aria-hidden>
                <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
                    <line className="edge" x1="8" y1={height / 2} x2={width - 8} y2={height / 2} strokeDasharray="3 6" />
                </svg>
            </div>
        );
    }
    const pad = 26;
    const step = (width - pad * 2) / Math.max(1, nodes.length - 1);
    const y = height / 2;
    return (
        <div className="wall-profile" aria-hidden>
            <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
                <line className="edge" x1={pad} y1={y} x2={width - pad} y2={y} />
                {nodes.map((n, i) => {
                    const cx = pad + step * i;
                    const cls =
                        n.status === 'done'
                            ? 'node node-done'
                            : n.status === 'active'
                              ? 'node node-active'
                              : n.status === 'failed'
                                ? 'node'
                                : 'node';
                    return (
                        <g key={i}>
                            {i < nodes.length - 1 && (
                                <line
                                    className={n.status === 'done' ? 'edge-done' : 'edge'}
                                    x1={cx}
                                    y1={y}
                                    x2={pad + step * (i + 1)}
                                    y2={y}
                                />
                            )}
                            <circle
                                className={cls}
                                cx={cx}
                                cy={y}
                                r={n.status === 'active' ? 6 : 4.5}
                            />
                            <text className="node-label" x={cx} y={y - 11} textAnchor="middle">
                                {n.label}
                            </text>
                        </g>
                    );
                })}
            </svg>
        </div>
    );
}

function Annunciators(): React.ReactElement {
    const { state } = useHarness();
    const [seqFlash, setSeqFlash] = useState(false);
    const prevSeq = useRef(state.eventSeq);
    useEffect(() => {
        if (state.eventSeq !== prevSeq.current) {
            prevSeq.current = state.eventSeq;
            setSeqFlash(true);
            const t = window.setTimeout(() => setSeqFlash(false), 900);
            return () => window.clearTimeout(t);
        }
    }, [state.eventSeq]);

    const hostOn = state.hostStatus === 'running';
    const hostCls = state.hostStatus === 'failed' ? 'is-on is-blink' : hostOn ? 'is-on' : '';
    const activeCount = state.activeTaskIds.length;
    const pendingCount = state.pendingApprovals.length;
    return (
        <div className="wall-tiles" role="status" aria-label="主机灯阵">
            <div className={`tile ${hostCls}`} style={{ '--tile-color': 'var(--success)' } as React.CSSProperties}>
                <span className="t-label">主机</span>
                <span className="t-value">{state.hostStatus === 'running' ? '运行' : '停止'}</span>
            </div>
            <div
                className={`tile ${activeCount > 0 ? 'is-on' : ''}`}
                style={{ '--tile-color': 'var(--info)' } as React.CSSProperties}
                title="协议层任务计数（工作流运行不计入）"
            >
                <span className="t-label">任务</span>
                <span className="t-value">{activeCount > 0 ? `${activeCount} 活跃` : '空'}</span>
            </div>
            <div
                className={`tile ${seqFlash ? 'is-on' : ''}`}
                style={{ '--tile-color': 'var(--evidence-highlight)' } as React.CSSProperties}
            >
                <span className="t-label">事件</span>
                <span className="t-value num">SEQ {String(state.eventSeq).padStart(3, '0')}</span>
            </div>
            <div
                className={`tile ${pendingCount > 0 ? 'is-on is-blink' : ''}`}
                style={{ '--tile-color': 'var(--primary)' } as React.CSSProperties}
            >
                <span className="t-label">批准</span>
                <span className="t-value">{pendingCount > 0 ? `${pendingCount} 待决` : '—'}</span>
            </div>
        </div>
    );
}

export function WallDisplay(): React.ReactElement {
    const { state } = useHarness();
    const wall = useWallState();
    const verbKey = `${wall.verb}-${state.activeTaskIds[0] ?? ''}`;
    const activeId = state.activeTaskIds[0];
    const detail = activeId !== undefined ? state.taskDetails.get(activeId) : undefined;
    const session = activeId !== undefined ? state.sessions.find((s) => s.taskId === activeId) : undefined;
    const sub =
        detail !== undefined
            ? `${progressLabel(detail.progress)} · ${session?.title ?? `任务 ${detail.id}`}`
            : wall.goal;
    return (
        <header className="wall">
            <div className={`wall-verb ${wall.takeover ? 'is-takeover' : ''}`}>
                <div className="v" key={verbKey}>
                    <span>{wall.verb}</span>
                </div>
                <span className="goal">{sub ?? ''}</span>
            </div>
            <MissionProfile />
            <Annunciators />
        </header>
    );
}
