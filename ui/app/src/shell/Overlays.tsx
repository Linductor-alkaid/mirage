/// 全局覆盖层：命令面板（Ctrl+K）、批准/通知中心、Toast 层。
/// 面板为自绘 listbox（role=listbox/option + 键盘导航 + aria）。
/// 批准中心自 M5-07 起接真实异步确认面（permission.*，DEC-020）：快照 +
/// 请求事件 + permission.respond（DEC-027 增量记录的占位由此兑现）。

import { useEffect, useMemo, useRef, useState } from 'react';
import { X } from 'lucide-react';

import { useHarness, useNow } from '../hooks.js';
import { relativeTime } from '../lib/labels.js';

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
    // 查询词变化时高亮归零：渲染期调整模式（React 官方范式，不进 effect）。
    const [prevQuery, setPrevQuery] = useState(query);
    if (prevQuery !== query) {
        setPrevQuery(query);
        setHighlight(0);
    }
    const inputRef = useRef<HTMLInputElement>(null);
    const now = useNow();

    const items = useMemo<PaletteItem[]>(() => {
        const nav: PaletteItem[] = [
            { id: 'nav-chat', label: '前往：会话', hint: '页面', run: () => actions.navigate({ view: 'chat', sessionId: state.route.view === 'chat' ? state.route.sessionId : undefined }) },
            { id: 'nav-wf', label: '前往：工作流', hint: '页面', run: () => actions.navigate({ view: 'workflows' }) },
            { id: 'nav-res', label: '前往：资源（M2+）', hint: '页面', run: () => actions.navigate({ view: 'resources' }) },
            { id: 'nav-set', label: '前往：设置 · 外观', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'appearance' }) },
            { id: 'nav-set-perm', label: '前往：设置 · 权限', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'permissions' }) },
            { id: 'nav-set-runtime', label: '前往：设置 · 运行时', hint: '页面', run: () => actions.navigate({ view: 'settings', category: 'runtime' }) },
            { id: 'new-session', label: '新建会话', hint: '动作', run: () => actions.newSession() },
            { id: 'resync', label: '重新同步事件流', hint: '动作', run: () => void actions.resync() },
            { id: 'estop', label: state.takeover ? '解除紧急停止' : '紧急停止（Takeover）', hint: '控制', run: () => (state.takeover ? actions.releaseEstop() : actions.engageEstop()) },
        ];
        const sessions: PaletteItem[] = state.sessions.map((s) => ({
            id: `sess-${s.id}`,
            label: `会话：${s.title}`,
            hint: relativeTime(s.lastActivityAt, now),
            run: () => actions.selectSession(s.id),
        }));
        const workflows: PaletteItem[] = state.workflows.map((w) => ({
            id: `wf-${w.id}`,
            label: `工作流：${w.name}`,
            hint: w.version,
            run: () => actions.navigate({ view: 'workflow-editor', workflowId: w.id }),
        }));
        return [...nav, ...sessions, ...workflows];
    }, [state, actions, now]);

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


/// 批准中心（M5-07，DEC-020 异步确认面）：permission.list 为快照事实源，
/// 请求事件触发重取；每条挂起确认呈现能力 / 资源 / 任务 / 剩余预算，批准
/// 或拒绝经 permission.respond（先到先得，not_found 静默收敛）。
export function ApprovalsCenter({ onClose }: { onClose(): void }): React.ReactElement {
    const { state, respondApproval, loadApprovals } = useHarness();

    useEffect(() => {
        void loadApprovals();
    }, [loadApprovals]);

    const pending = state.pendingApprovals;
    const unavailable =
        !state.permissionsSupported && state.connection === 'ready';

    return (
        <div className="overlay-scrim" onClick={onClose}>
            <div
                className="popover-panel"
                role="dialog"
                aria-label="批准中心"
                style={{ top: 'calc(var(--mir-size-wall) + 8px)', left: 'calc(var(--mir-size-rail) + 8px)' }}
                onClick={(e) => e.stopPropagation()}
            >
                <div className="palette-empty" style={{ marginBottom: 8 }}>
                    <span>批准中心（DEC-020 异步确认面）。</span>
                    <button
                        type="button"
                        className="btn btn-ghost"
                        style={{ height: 20, marginLeft: 8 }}
                        onClick={() => void loadApprovals()}
                    >
                        刷新
                    </button>
                </div>
                {unavailable && (
                    <div className="palette-empty">
                        服务未提供异步确认面（hello 无 permissions 位，确认 hub 未配置）。
                    </div>
                )}
                {state.permissionsSupported && pending.length === 0 && (
                    <div className="palette-empty" data-testid="approvals-empty">
                        没有待批准请求。Confirm 规则命中时请求会出现在这里。
                    </div>
                )}
                {pending.map((item) => {
                    const remaining = Math.max(0, Math.round((item.timeout_ms) / 1000));
                    return (
                        <div
                            key={item.request_id}
                            className="card"
                            style={{ margin: '6px 0', padding: 8 }}
                            data-testid="approval-card"
                        >
                            <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8 }}>
                                <strong className="mono">{item.capability}</strong>
                                <span className="badge is-warn">剩余 {remaining}s</span>
                            </div>
                            {item.resource.length > 0 && (
                                <div className="mono" style={{ fontSize: 12, opacity: 0.8, marginTop: 4 }}>
                                    {item.resource}
                                </div>
                            )}
                            <div className="muted" style={{ fontSize: 12, marginTop: 4 }}>
                                任务 {item.task_id.slice(0, 8)} · 请求 {item.request_id.slice(0, 8)}
                            </div>
                            <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
                                <button
                                    type="button"
                                    className="btn btn-primary"
                                    style={{ height: 26 }}
                                    onClick={() => void respondApproval(item.request_id, true)}
                                >
                                    批准
                                </button>
                                <button
                                    type="button"
                                    className="btn btn-danger"
                                    style={{ height: 26 }}
                                    onClick={() => void respondApproval(item.request_id, false)}
                                >
                                    拒绝
                                </button>
                            </div>
                        </div>
                    );
                })}
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
