/// 签派栏（SessionsSidebar）：新建、搜索、分组列表（今天 / 近 7 天 / 更早）。
/// 数据全部来自会话面契约路径（session.list 快照 + 派生标题，DEC-025）；
/// wire 没有的管理面（重命名 / 置顶 / 删除 / 导出 / fork）不呈现。

import { useMemo, useState } from 'react';
import { MessageSquarePlus, Search } from 'lucide-react';

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
    const { state, newSession, selectSession } = useHarness();
    const [query, setQuery] = useState('');
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
                                    <button
                                        key={s.id}
                                        type="button"
                                        className={`sess-item ${isActive ? 'is-active' : ''}`}
                                        onClick={() => selectSession(s.id)}
                                        title={`${s.id} · ${relativeTime(s.lastActivityAt, now)}`}
                                    >
                                        <span className="s-title">{s.title}</span>
                                        <SessionBadge session={s} />
                                    </button>
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
