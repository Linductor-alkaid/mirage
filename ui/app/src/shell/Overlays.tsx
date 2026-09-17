/// 全局覆盖层：命令面板（Ctrl+K）、批准/通知中心、Toast 层。
/// 面板为自绘 listbox（role=listbox/option + 键盘导航 + aria）。

import { useEffect, useMemo, useRef, useState } from 'react';
import { Check, X } from 'lucide-react';

import { useHarness } from '../hooks.js';
import { approvalKindLabel, relativeTime } from '../lib/labels.js';

interface PaletteItem {
    id: string;
    label: string;
    hint?: string;
    run(): void;
}

export function CommandPalette({ onClose }: { onClose(): void }): React.ReactElement {
    const { state, ...actions } = useHarness();
    const [query, setQuery] = useState('');
    const [highlight, setHighlight] = useState(0);
    const inputRef = useRef<HTMLInputElement>(null);

    const items = useMemo<PaletteItem[]>(() => {
        const nav: PaletteItem[] = [
            { id: 'nav-chat', label: '前往：会话', hint: '页面', run: () => actions.navigate({ view: 'chat', sessionId: state.route.view === 'chat' ? state.route.sessionId : undefined }) },
            { id: 'nav-wf', label: '前往：工作流', hint: '页面', run: () => actions.navigate({ view: 'workflows' }) },
            { id: 'nav-res', label: '前往：资源（M2+）', hint: '页面', run: () => actions.navigate({ view: 'resources' }) },
            { id: 'nav-set', label: '前往：设置 · 外观', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'appearance' }) },
            { id: 'nav-set-perm', label: '前往：设置 · 权限', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'permissions' }) },
            { id: 'nav-set-runtime', label: '前往：设置 · 运行时', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'runtime' }) },
            { id: 'new-chat', label: '新建对话会话', hint: '动作', run: () => actions.newSession('chat') },
            { id: 'new-exec', label: '新建执行会话', hint: '动作', run: () => actions.newSession('exec') },
            { id: 'resync', label: '重新同步事件流', hint: '动作', run: () => void actions.resync() },
            { id: 'export', label: '导出当前会话 Markdown', hint: '动作', run: () => {
                if (state.route.view === 'chat' && state.route.sessionId !== undefined) {
                    actions.exportSession(state.route.sessionId);
                }
            } },
            { id: 'estop', label: state.takeover ? '解除紧急停止' : '紧急停止（Takeover）', hint: '控制', run: () => (state.takeover ? actions.releaseEstop() : actions.engageEstop()) },
        ];
        const sessions: PaletteItem[] = state.sessions.map((s) => ({
            id: `sess-${s.id}`,
            label: `会话：${s.title}`,
            hint: relativeTime(s.updatedAt, Date.now()),
            run: () => actions.selectSession(s.id),
        }));
        const workflows: PaletteItem[] = state.workflows.map((w) => ({
            id: `wf-${w.id}`,
            label: `工作流：${w.name}`,
            hint: w.version,
            run: () => actions.navigate({ view: 'workflow-editor', workflowId: w.id }),
        }));
        return [...nav, ...sessions, ...workflows];
    }, [state, actions]);

    const filtered = useMemo(() => {
        const q = query.trim().toLowerCase();
        if (q.length === 0) {
            return items;
        }
        return items.filter((i) => i.label.toLowerCase().includes(q));
    }, [items, query]);

    useEffect(() => {
        inputRef.current?.focus();
    }, []);
    useEffect(() => {
        setHighlight(0);
    }, [query]);

    const onKeyDown = (e: React.KeyboardEvent): void => {
        if (e.key === 'Escape') {
            e.preventDefault();
            onClose();
            return;
        }
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            setHighlight((h) => Math.min(h + 1, filtered.length - 1));
            return;
        }
        if (e.key === 'ArrowUp') {
            e.preventDefault();
            setHighlight((h) => Math.max(h - 1, 0));
            return;
        }
        if (e.key === 'Enter') {
            e.preventDefault();
            const item = filtered[highlight];
            if (item !== undefined) {
                item.run();
                onClose();
            }
        }
    };

    return (
        <div className="overlay-scrim" onClick={onClose}>
            <div
                className="palette"
                role="dialog"
                aria-label="命令面板"
                onClick={(e) => e.stopPropagation()}
                onKeyDown={onKeyDown}
            >
                <input
                    ref={inputRef}
                    value={query}
                    onChange={(e) => setQuery(e.target.value)}
                    placeholder="输入命令或搜索…（↑↓ 选择，Enter 执行，Esc 关闭）"
                    aria-label="命令搜索"
                />
                <div className="palette-list" role="listbox" aria-label="命令列表">
                    {filtered.length === 0 && <div className="palette-empty">没有匹配的命令</div>}
                    {filtered.map((item, i) => (
                        <div
                            key={item.id}
                            role="option"
                            aria-selected={i === highlight}
                            data-highlighted={i === highlight || undefined}
                            className="palette-item"
                            onMouseEnter={() => setHighlight(i)}
                            onClick={() => {
                                item.run();
                                onClose();
                            }}
                        >
                            <span>{item.label}</span>
                            {item.hint !== undefined && <span className="pi-hint">{item.hint}</span>}
                        </div>
                    ))}
                </div>
            </div>
        </div>
    );
}


export function ApprovalsCenter({ onClose }: { onClose(): void }): React.ReactElement {
    const { state, decideApproval } = useHarness();
    const pending = state.pendingApprovals;
    return (
        <div className="overlay-scrim" onClick={onClose}>
            <div
                className="popover-panel"
                role="dialog"
                aria-label="批准中心"
                style={{ top: 'calc(var(--mir-size-wall) + 8px)', left: 'calc(var(--mir-size-rail) + 8px)' }}
                onClick={(e) => e.stopPropagation()}
            >
                {pending.length === 0 && (
                    <div className="palette-empty">
                        没有待决事项。后台批准会出现在这里，不打断当前视图。
                    </div>
                )}
                {pending.map((a) => (
                    <div key={a.id} className="notif-item">
                        <span className="n-title">
                            <strong>{a.summary}</strong>
                            <span className="badge is-warning is-blink">{approvalKindLabel(a.kind)}</span>
                        </span>
                        {a.detail !== undefined && <span className="n-sub mono">{a.detail}</span>}
                        <span className="n-sub">请求于 {relativeTime(a.requestedAt ?? Date.now(), Date.now())}</span>
                        <span style={{ display: 'flex', gap: 8, marginTop: 4 }}>
                            <button type="button" className="btn btn-primary" onClick={() => decideApproval(a.id, true)}>
                                <Check size={14} /> 放行
                            </button>
                            <button type="button" className="btn btn-danger" onClick={() => decideApproval(a.id, false)}>
                                <X size={14} /> 拒止
                            </button>
                        </span>
                    </div>
                ))}
            </div>
        </div>
    );
}

export function ToastLayer(): React.ReactElement {
    const { state, dismissToast } = useHarness();
    return (
        <div className="toast-layer" role="status" aria-live="polite">
            {state.toasts.map((t) => (
                <div key={t.id} className={`toast is-${t.tone}`}>
                    <span>{t.text}</span>
                    <button type="button" className="btn-ghost btn" style={{ height: 20, padding: '0 4px' }} onClick={() => dismissToast(t.id)} aria-label="关闭">
                        <X size={13} />
                    </button>
                </div>
            ))}
        </div>
    );
}
