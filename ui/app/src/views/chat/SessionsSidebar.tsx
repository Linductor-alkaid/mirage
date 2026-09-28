/// 签派栏（SessionsSidebar）：新建、搜索、分组列表（今天 / 近 7 天 / 更早）、
/// 会话删除（session.close，DEC-026 挂账②兑现）。数据全部来自会话面契约路径
/// （session.list 快照 + 派生标题，DEC-025）；重命名 / 置顶 / 导出 / fork 仍
/// 无 wire 面，不呈现（DEC-026 决策 5）。

import { useMemo, useState } from 'react';
import { MessageSquarePlus, Search, Trash2 } from 'lucide-react';

import { useHarness, useNow } from '../../hooks.js';
import { progressTone, relativeTime } from '../../lib/labels.js';
import type { SessionMeta } from '../../state/model.js';
import { isTerminalProgress } from '../../state/store.js';

type GroupKey = 'today' | 'week' | 'older';

const GROUP_LABELS: Record<GroupKey, string> = {
    today: '今天',
    week: '近 7 天',
    older: '更早',
};

function groupOf(s: SessionMeta, now: number): GroupKey {
    const age = now - s.lastActivityAt;
    if (age < 86_400_000) {
        return 'today';
    }
    if (age < 7 * 86_400_000) {
        return 'week';
    }
    return 'older';
}

function SessionBadge({ session }: { session: SessionMeta }): React.ReactElement | null {
    const { state } = useHarness();
    if (session.taskId === undefined) {
        return null;
    }
    const detail = state.taskDetails.get(session.taskId);
    const progress = detail?.progress;
    if (progress === undefined) {
        return null;
    }
    const live = !isTerminalProgress(progress);
    return (
        <span
            className={`badge is-${progressTone(progress)} ${live ? 'is-blink' : ''}`}
            title={`任务 ${session.taskId} · ${progress}`}
        />
    );
}

export function SessionsSidebar(): React.ReactElement {
    const { state, newSession, selectSession, deleteSession } = useHarness();
    const [query, setQuery] = useState('');
    const [menuFor, setMenuFor] = useState<string | null>(null);
    const [confirmClose, setConfirmClose] = useState<string | null>(null);
    const now = useNow();

    const activeId = state.route.view === 'chat' ? state.route.sessionId : undefined;

    const groups = useMemo(() => {
        const q = query.trim().toLowerCase();
        const filtered = state.sessions.filter(
            (s) => q.length === 0 || s.title.toLowerCase().includes(q) || s.id.includes(q),
        );
        const sorted = [...filtered].sort((a, b) => b.lastActivityAt - a.lastActivityAt);
        const out = new Map<GroupKey, SessionMeta[]>();
        for (const s of sorted) {
            const g = groupOf(s, now);
            const list = out.get(g) ?? [];
            list.push(s);
            out.set(g, list);
        }
        return out;
    }, [state.sessions, query, now]);

    return (
        <aside className="sidebar" aria-label="会话签派栏">
            <div className="sidebar-head">
                <button
                    type="button"
                    className="btn btn-primary"
                    style={{ flex: 1 }}
                    onClick={() => newSession()}
                    disabled={!state.sessionsSupported}
                    title={state.sessionsSupported ? '通过 session.open 新建会话' : '服务未提供会话面'}
                >
                    <MessageSquarePlus size={15} /> 新建会话
                </button>
            </div>
            <div className="sess-search">
                <Search size={14} />
                <input
                    value={query}
                    onChange={(e) => setQuery(e.target.value)}
                    placeholder="搜索会话…"
                    aria-label="搜索会话"
                />
            </div>
            <div className="sess-groups">
                {(['today', 'week', 'older'] as const).map((g) => {
                    const list = groups.get(g) ?? [];
                    if (list.length === 0) {
                        return null;
                    }
                    return (
                        <div className="sess-group" key={g}>
                            <span className="g-label">{GROUP_LABELS[g]}</span>
                            {list.map((s) => {
                                const isActive = s.id === activeId;
                                return (
                                    <div key={s.id} style={{ position: 'relative' }}>
                                        <button
                                            type="button"
                                            className={`sess-item ${isActive ? 'is-active' : ''}`}
                                            onClick={() => selectSession(s.id)}
                                            title={`${s.id} · ${relativeTime(s.lastActivityAt, now)}`}
                                        >
                                            <span className="s-title">{s.title}</span>
                                            <SessionBadge session={s} />
                                            {state.sessionsSupported && (
                                                <span
                                                    className="sess-menu-btn"
                                                    role="button"
                                                    tabIndex={0}
                                                    aria-label={`会话操作：${s.title}`}
                                                    onClick={(e) => {
                                                        e.stopPropagation();
                                                        setMenuFor(menuFor === s.id ? null : s.id);
                                                        setConfirmClose(null);
                                                    }}
                                                    onKeyDown={(e) => {
                                                        if (e.key === 'Enter' || e.key === ' ') {
                                                            e.stopPropagation();
                                                            setMenuFor(menuFor === s.id ? null : s.id);
                                                        }
                                                    }}
                                                >
                                                    ⋯
                                                </span>
                                            )}
                                        </button>
                                        {menuFor === s.id && state.sessionsSupported && (
                                            <div
                                                className="menu-panel"
                                                style={{ position: 'absolute', top: 28, right: 6, zIndex: 30 }}
                                                onMouseLeave={() => setMenuFor(null)}
                                            >
                                                <button
                                                    type="button"
                                                    className="menu-item is-danger"
                                                    onClick={() => {
                                                        if (confirmClose === s.id) {
                                                            deleteSession(s.id);
                                                            setMenuFor(null);
                                                            setConfirmClose(null);
                                                        } else {
                                                            setConfirmClose(s.id);
                                                        }
                                                    }}
                                                >
                                                    <Trash2 size={13} />
                                                    {confirmClose === s.id ? '确认关闭？关联任务将取消' : '关闭会话'}
                                                </button>
                                            </div>
                                        )}
                                    </div>
                                );
                            })}
                        </div>
                    );
                })}
                {state.sessions.length === 0 && (
                    <div className="palette-empty">
                        {state.sessionsSupported
                            ? '没有会话。用上方按钮新建一个。'
                            : '服务未提供会话面（hello 无 sessions 位），会话页不可用。'}
                    </div>
                )}
            </div>
        </aside>
    );
}
