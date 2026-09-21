/// 活动栏（rail）+ cue 状态栏（statusbar）+ 紧急停止。
/// 会话 · 工作流 · 资源(M2+) · — · 设置；底部：transport / host 五态 /
/// seq / 版本 / 紧急停止警戒钮。

import { Bell, Boxes, Globe, MessageSquare, PanelLeftClose, PanelLeftOpen, Settings, Workflow } from 'lucide-react';

import { useHarness } from '../hooks.js';
import { hostStatusLabel, hostStatusDot } from '../lib/labels.js';

export function ActivityBar({
    sidebarCollapsed,
    onToggleSidebar,
    onOpenApprovals,
    approvalsOpen,
}: {
    sidebarCollapsed: boolean;
    onToggleSidebar(): void;
    onOpenApprovals(): void;
    approvalsOpen: boolean;
}): React.ReactElement {
    const { state, navigate } = useHarness();
    const active =
        state.route.view === 'workflow-editor' || state.route.view === 'workflow-run'
            ? 'workflows'
            : state.route.view;
    const pendingApprovals = state.pendingApprovals.length;
    const goChat = (): void => {
        navigate({ view: 'chat', sessionId: state.route.view === 'chat' ? state.route.sessionId : undefined });
    };
    return (
        <nav className="rail" aria-label="活动栏">
            <div className="rail-brand" title="Mirage 控制台">
                M
            </div>
            <button
                type="button"
                className={`rail-btn ${active === 'chat' ? 'is-active' : ''}`}
                title="会话"
                aria-label="会话"
                aria-current={active === 'chat' || undefined}
                onClick={goChat}
            >
                <MessageSquare size={19} strokeWidth={1.6} />
            </button>
            <button
                type="button"
                className={`rail-btn ${active === 'workflows' ? 'is-active' : ''}`}
                title="工作流"
                aria-label="工作流"
                aria-current={active === 'workflows' || undefined}
                onClick={() => navigate({ view: 'workflows' })}
            >
                <Workflow size={19} strokeWidth={1.6} />
            </button>
            <button
                type="button"
                className={`rail-btn ${active === 'world' ? 'is-active' : ''}`}
                title="组织世界（World Projection）"
                aria-label="组织世界（World Projection）"
                aria-current={active === 'world' || undefined}
                onClick={() => navigate({ view: 'world' })}
                data-testid="rail-world"
            >
                <Globe size={19} strokeWidth={1.6} />
            </button>
            <button
                type="button"
                className="rail-btn"
                title="资源（M2+ 占位）"
                aria-label="资源（M2+ 占位）"
                disabled
            >
                <Boxes size={19} strokeWidth={1.6} />
            </button>
            <div className="rail-spacer" />
            <button
                type="button"
                className={`rail-btn ${approvalsOpen ? 'is-active' : ''}`}
                title={pendingApprovals > 0 ? `批准中心（${pendingApprovals} 待决）` : '批准中心'}
                aria-label="批准中心"
                onClick={onOpenApprovals}
            >
                <Bell size={19} strokeWidth={1.6} />
                {pendingApprovals > 0 && <span className="dot" aria-hidden />}
            </button>
            <button
                type="button"
                className="rail-btn"
                title={sidebarCollapsed ? '展开签派栏' : '收起签派栏'}
                aria-label={sidebarCollapsed ? '展开签派栏' : '收起签派栏'}
                onClick={onToggleSidebar}
            >
                {sidebarCollapsed ? <PanelLeftOpen size={19} strokeWidth={1.6} /> : <PanelLeftClose size={19} strokeWidth={1.6} />}
            </button>
            <button
                type="button"
                className={`rail-btn ${active === 'settings' ? 'is-active' : ''}`}
                title="设置"
                aria-label="设置"
                aria-current={active === 'settings' || undefined}
                onClick={() => navigate({ view: 'settings', category: 'appearance' })}
            >
                <Settings size={19} strokeWidth={1.6} />
            </button>
        </nav>
    );
}

export function StatusBar(): React.ReactElement {
    const { state, engageEstop, releaseEstop } = useHarness();
    const dot =
        state.connection === 'ready'
            ? hostStatusDot(state.hostStatus)
            : state.connection === 'connecting'
              ? 'run'
              : 'bad';
    const connLabel =
        state.connection === 'ready'
            ? hostStatusLabel(state.hostStatus)
            : state.connection === 'connecting'
              ? '连接中'
              : '连接错误';
    return (
        <footer className="statusbar">
            <span className="seg caps hide-sm" title="Mirage 控制台" style={{ fontSize: 10 }}>
                Mirage · Mission Console
            </span>
            <span className="seg">
                <span className={`status-dot is-${dot}`} aria-hidden />
                <span className="k">Host</span>
                {connLabel}
            </span>
            <span className="seg hide-sm">
                <span className="k">Transport</span>
                <span className="mono">{state.transportLabel}</span>
            </span>
            <span className="seg hide-sm">
                <span className="k">Events</span>
                {state.eventsSupported ? (
                    <span className="mono">订阅 · seq {String(state.eventSeq).padStart(3, '0')}</span>
                ) : (
                    <span className="mono">降级轮询</span>
                )}
            </span>
            {state.identity !== undefined && (
                <span className="seg">
                    <span className="k">Protocol</span>
                    <span className="mono">v{state.identity.protocol}</span>
                </span>
            )}
            <span className="spacer" />
            {state.resyncNote !== undefined && (
                <span className="seg muted hide-sm">已重新同步：{state.resyncNote.reason}</span>
            )}
            {state.identity !== undefined && (
                <span className="seg mono hide-sm">Mirage {state.identity.mirage_version}</span>
            )}
            <button
                type="button"
                className={`estop ${state.takeover ? 'is-latched' : ''}`}
                data-testid="estop"
                onClick={() => (state.takeover ? releaseEstop() : engageEstop())}
                title={state.takeover ? '解除 Takeover（恢复前建议重新观察环境）' : '紧急停止：取消运行中任务并暂停自动动作'}
            >
                ⏻ {state.takeover ? '解除接管' : '紧急停止'}
            </button>
        </footer>
    );
}
