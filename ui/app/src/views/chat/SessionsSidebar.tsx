/// 签派栏（SessionsSidebar）：新建、搜索、分组列表（置顶 / 今天 / 近 7 天 /
/// 更早），会话四件套（重命名 / 置顶 / 删除确认 / 导出 Markdown）。

import { useMemo, useRef, useState } from 'react';
import { MessageSquarePlus, Pin, Search, SquarePlus, Trash2 } from 'lucide-react';

import { useHarness } from '../../hooks.js';
import { progressTone } from '../../lib/labels.js';
import type { SessionMeta } from '../../state/model.js';
import { isTerminalProgress } from '../../state/store.js';

type GroupKey = 'pinned' | 'today' | 'week' | 'older';

const GROUP_LABELS: Record<GroupKey, string> = {
    pinned: '置顶',
    today: '今天',
    week: '近 7 天',
    older: '更早',
};

function groupOf(s: SessionMeta, now: number): GroupKey {
    if (s.pinned) {
        return 'pinned';
    }
    const age = now - s.updatedAt;
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
    const { state, newSession, selectSession, pinSession, renameSession, deleteSession, exportSession } = useHarness();
    const [query, setQuery] = useState('');
    const [menuFor, setMenuFor] = useState<string | null>(null);
    const [renaming, setRenaming] = useState<string | null>(null);
    const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
    const renameRef = useRef<HTMLInputElement>(null);

    const activeId = state.route.view === 'chat' ? state.route.sessionId : undefined;
    const now = Date.now();

    const groups = useMemo(() => {
        const q = query.trim().toLowerCase();
        const filtered = state.sessions.filter((s) => q.length === 0 || s.title.toLowerCase().includes(q));
        const sorted = [...filtered].sort((a, b) => b.updatedAt - a.updatedAt);
        const out = new Map<GroupKey, SessionMeta[]>();
        for (const s of sorted) {
            const g = groupOf(s, now);
            const list = out.get(g) ?? [];
            list.push(s);
            out.set(g, list);
        }
        return out;
    }, [state.sessions, query, now]);

    const commitRename = (id: string): void => {
        const value = renameRef.current?.value ?? '';
        if (value.trim().length > 0) {
            renameSession(id, value);
        }
        setRenaming(null);
    };

    return (
        <aside className="sidebar" aria-label="会话签派栏">
            <div className="sidebar-head">
                <button type="button" className="btn btn-primary" style={{ flex: 1 }} onClick={() => newSession('exec')}>
                    <SquarePlus size={15} /> 新建执行
                </button>
                <button type="button" className="btn" title="新建对话会话" aria-label="新建对话会话" onClick={() => newSession('chat')}>
                    <MessageSquarePlus size={15} />
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
                {(['pinned', 'today', 'week', 'older'] as const).map((g) => {
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
                                        {renaming === s.id ? (
                                            <div className="sess-item is-active" onClick={(e) => e.stopPropagation()}>
                                                <input
                                                    ref={renameRef}
                                                    className="input"
                                                    style={{ height: 22 }}
                                                    defaultValue={s.title}
                                                    onKeyDown={(e) => {
                                                        if (e.key === 'Enter') {
                                                            commitRename(s.id);
                                                        }
                                                        if (e.key === 'Escape') {
                                                            setRenaming(null);
                                                        }
                                                    }}
                                                    onBlur={() => commitRename(s.id)}
                                                    autoFocus
                                                />
                                            </div>
                                        ) : (
                                            <button
                                                type="button"
                                                className={`sess-item ${isActive ? 'is-active' : ''}`}
                                                onClick={() => selectSession(s.id)}
                                            >
                                                {s.pinned && <Pin size={12} className="s-pin" aria-hidden />}
                                                <span className="s-title">{s.title}</span>
                                                <SessionBadge session={s} />
                                                <span
                                                    className="sess-menu-btn"
                                                    role="button"
                                                    tabIndex={0}
                                                    aria-label={`会话操作：${s.title}`}
                                                    onClick={(e) => {
                                                        e.stopPropagation();
                                                        setMenuFor(menuFor === s.id ? null : s.id);
                                                        setConfirmDelete(null);
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
                                            </button>
                                        )}
                                        {menuFor === s.id && (
                                            <div
                                                className="menu-panel"
                                                style={{ position: 'absolute', top: 28, right: 6, zIndex: 30 }}
                                                onMouseLeave={() => setMenuFor(null)}
                                            >
                                                <button type="button" className="menu-item" onClick={() => { pinSession(s.id, !s.pinned); setMenuFor(null); }}>
                                                    <Pin size={13} /> {s.pinned ? '取消置顶' : '置顶'}
                                                </button>
                                                <button type="button" className="menu-item" onClick={() => { setRenaming(s.id); setMenuFor(null); }}>
                                                    重命名
                                                </button>
                                                <button type="button" className="menu-item" onClick={() => { exportSession(s.id); setMenuFor(null); }}>
                                                    导出 Markdown
                                                </button>
                                                <button
                                                    type="button"
                                                    className="menu-item is-danger"
                                                    onClick={() => {
                                                        if (confirmDelete === s.id) {
                                                            deleteSession(s.id);
                                                            setMenuFor(null);
                                                        } else {
                                                            setConfirmDelete(s.id);
                                                        }
                                                    }}
                                                >
                                                    <Trash2 size={13} /> {confirmDelete === s.id ? '确认删除？' : '删除会话'}
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
                    <div className="palette-empty">没有会话。用上方按钮新建一个。</div>
                )}
            </div>
        </aside>
    );
}
