/// 工作流左栏：与 agent harness 的签派栏同壳布局 —— 只不过列的是已保存的
/// workflow（分组：全部 / 已发布 / 草稿；搜索；四件套对应物：运行 / 重命名 /
/// 删除确认 / 导出 IR JSON）。数据来自 WorkflowBackend（当前 mock）。

import { useMemo, useRef, useState } from 'react';
import { Play, Plus, Trash2 } from 'lucide-react';

import { useHarness } from '../../hooks.js';

type GroupKey = 'all' | 'published' | 'draft';

const GROUP_LABELS: Record<GroupKey, string> = {
    all: '全部',
    published: '已发布',
    draft: '草稿',
};

export function WorkflowSidebar(): React.ReactElement {
    const { state, navigate, runWorkflow, createWorkflow, renameWorkflow, deleteWorkflow, exportWorkflowJson } = useHarness();
    const [query, setQuery] = useState('');
    const [menuFor, setMenuFor] = useState<string | null>(null);
    const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
    const [renaming, setRenaming] = useState<string | null>(null);
    const renameRef = useRef<HTMLInputElement>(null);

    const selectedId = state.route.view === 'workflow-editor' ? state.route.workflowId : undefined;

    const groups = useMemo(() => {
        const q = query.trim().toLowerCase();
        const filtered = state.workflows.filter((w) => q.length === 0 || w.name.toLowerCase().includes(q));
        const sorted = [...filtered].sort((a, b) => b.updatedAt - a.updatedAt);
        const out = new Map<GroupKey, typeof filtered>([
            ['all', sorted],
            ['published', sorted.filter((w) => w.published)],
            ['draft', sorted.filter((w) => !w.published)],
        ]);
        return out;
    }, [state.workflows, query]);

    const commitRename = (id: string): void => {
        const value = renameRef.current?.value ?? '';
        if (value.trim().length > 0) {
            renameWorkflow(id, value);
        }
        setRenaming(null);
    };

    return (
        <aside className="sidebar" aria-label="工作流栏">
            <div className="sidebar-head">
                <button type="button" className="btn btn-primary" style={{ flex: 1 }} onClick={() => createWorkflow()}>
                    <Plus size={15} /> 新建工作流
                </button>
            </div>
            <div className="sess-search">
                <input
                    value={query}
                    onChange={(e) => setQuery(e.target.value)}
                    placeholder="搜索工作流…"
                    aria-label="搜索工作流"
                />
            </div>
            <div className="sess-groups">
                {(['all', 'published', 'draft'] as const).map((g) => {
                    const list = groups.get(g) ?? [];
                    if (g !== 'all' && list.length === 0) {
                        return null;
                    }
                    return (
                        <div className="sess-group" key={g}>
                            <span className="g-label">
                                {GROUP_LABELS[g]}
                                <span className="muted" style={{ letterSpacing: 0 }}>
                                    {list.length}
                                </span>
                            </span>
                            {list.map((w) => {
                                const isActive = w.id === selectedId;
                                return (
                                    <div key={w.id} style={{ position: 'relative' }}>
                                        {renaming === w.id ? (
                                            <div className="sess-item is-active" onClick={(e) => e.stopPropagation()}>
                                                <input
                                                    ref={renameRef}
                                                    className="input"
                                                    style={{ height: 22 }}
                                                    defaultValue={w.name}
                                                    onKeyDown={(e) => {
                                                        if (e.key === 'Enter') {
                                                            commitRename(w.id);
                                                        }
                                                        if (e.key === 'Escape') {
                                                            setRenaming(null);
                                                        }
                                                    }}
                                                    onBlur={() => commitRename(w.id)}
                                                    autoFocus
                                                />
                                            </div>
                                        ) : (
                                            <button
                                                type="button"
                                                className={`sess-item ${isActive ? 'is-active' : ''}`}
                                                onClick={() => navigate({ view: 'workflow-editor', workflowId: w.id })}
                                            >
                                                <span className="s-title">{w.name}</span>
                                                <span className={`badge ${w.published ? 'is-success' : 'is-warning'}`} style={{ height: 16, fontSize: 10 }}>
                                                    {w.version}
                                                </span>
                                                <span
                                                    className="sess-menu-btn"
                                                    role="button"
                                                    tabIndex={0}
                                                    aria-label={`工作流操作：${w.name}`}
                                                    onClick={(e) => {
                                                        e.stopPropagation();
                                                        setMenuFor(menuFor === w.id ? null : w.id);
                                                        setConfirmDelete(null);
                                                    }}
                                                    onKeyDown={(e) => {
                                                        if (e.key === 'Enter' || e.key === ' ') {
                                                            e.stopPropagation();
                                                            setMenuFor(menuFor === w.id ? null : w.id);
                                                        }
                                                    }}
                                                >
                                                    ⋯
                                                </span>
                                            </button>
                                        )}
                                        {menuFor === w.id && (
                                            <div
                                                className="menu-panel"
                                                style={{ position: 'absolute', top: 28, right: 6, zIndex: 30 }}
                                                onMouseLeave={() => setMenuFor(null)}
                                            >
                                                <button type="button" className="menu-item" onClick={() => { runWorkflow(w.id); setMenuFor(null); }}>
                                                    <Play size={13} /> 运行
                                                </button>
                                                <button type="button" className="menu-item" onClick={() => { setRenaming(w.id); setMenuFor(null); }}>
                                                    重命名
                                                </button>
                                                <button type="button" className="menu-item" onClick={() => { exportWorkflowJson(w.id); setMenuFor(null); }}>
                                                    导出 IR JSON
                                                </button>
                                                <button
                                                    type="button"
                                                    className="menu-item is-danger"
                                                    onClick={() => {
                                                        if (confirmDelete === w.id) {
                                                            deleteWorkflow(w.id);
                                                            setMenuFor(null);
                                                        } else {
                                                            setConfirmDelete(w.id);
                                                        }
                                                    }}
                                                >
                                                    <Trash2 size={13} /> {confirmDelete === w.id ? '确认删除？' : '删除工作流'}
                                                </button>
                                            </div>
                                        )}
                                    </div>
                                );
                            })}
                        </div>
                    );
                })}
                {state.workflows.length === 0 && (
                    <div className="palette-empty">没有工作流。用上方按钮新建一个。</div>
                )}
            </div>
        </aside>
    );
}

