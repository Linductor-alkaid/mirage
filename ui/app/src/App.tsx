/// 应用壳：壁挂屏 + 活动栏 + 主区路由 + cue 状态栏 + 全局层
/// （命令面板 Ctrl+K、批准中心、Toast）。签派栏由会话页承载。

import { useEffect, useState } from 'react';

import { WallDisplay } from './shell/WallDisplay.js';
import { ActivityBar, StatusBar } from './shell/Chrome.js';
import { ApprovalsCenter, CommandPalette, ToastLayer } from './shell/Overlays.js';
import { ChatPage } from './views/chat/ChatPage.js';
import { ResourcesPage, SettingsPage } from './views/SettingsAndResources.js';
import { WorkflowsPage, WorkflowRunPage } from './views/WorkflowsPages.js';
import { useHarness } from './hooks.js';

export function App(): React.ReactElement {
    const { state } = useHarness();
    const [paletteOpen, setPaletteOpen] = useState(false);
    const [approvalsOpen, setApprovalsOpen] = useState(false);
    const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

    useEffect(() => {
        const onKey = (e: KeyboardEvent): void => {
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
                e.preventDefault();
                setPaletteOpen((v) => !v);
                setApprovalsOpen(false);
                return;
            }
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'b') {
                e.preventDefault();
                setSidebarCollapsed((v) => !v);
                return;
            }
            if (e.key === 'Escape') {
                setPaletteOpen(false);
                setApprovalsOpen(false);
            }
        };
        window.addEventListener('keydown', onKey);
        return () => window.removeEventListener('keydown', onKey);
    }, []);

    const route = state.route;
    return (
        <div className="console">
            <WallDisplay />
            <ActivityBar
                sidebarCollapsed={sidebarCollapsed}
                onToggleSidebar={() => setSidebarCollapsed((v) => !v)}
                onOpenApprovals={() => {
                    setApprovalsOpen((v) => !v);
                    setPaletteOpen(false);
                }}
                approvalsOpen={approvalsOpen}
            />
            <main className="main">
                {route.view === 'chat' && <ChatPage sidebarCollapsed={sidebarCollapsed} />}
                {(route.view === 'workflows' || route.view === 'workflow-editor') && <WorkflowsPage />}
                {route.view === 'workflow-run' && (
                    <WorkflowRunPage workflowId={route.workflowId} runId={route.runId} />
                )}
                {route.view === 'resources' && <ResourcesPage />}
                {route.view === 'settings' && <SettingsPage category={route.category} />}
            </main>
            <StatusBar />
            {paletteOpen && <CommandPalette onClose={() => setPaletteOpen(false)} />}
            {approvalsOpen && <ApprovalsCenter onClose={() => setApprovalsOpen(false)} />}
            <ToastLayer />
        </div>
    );
}
