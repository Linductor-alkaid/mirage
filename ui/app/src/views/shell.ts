/// Application shell: header (brand, transport badge, host status), the nav
/// tabs for the three delivered views, the resync toast, and routing into
/// the active view.

import type { AppActions, AppState } from '../store.js';
import { hostBadge } from '../components/status-badge.js';
import { h, render } from '../dom.js';
import { renderWorkspace } from './workspace.js';
import { renderTasks } from './tasks.js';
import { renderExecution } from './execution.js';
import { renderAppearance } from './appearance.js';

export function renderShell(state: AppState, actions: AppActions): HTMLElement {
    const page = h('div', { class: 'shell' }, renderHeader(state), renderNav(state), renderMain(state, actions));
    if (state.resyncNote !== undefined) {
        page.append(h(
            'div',
            { class: 'toast', role: 'status', 'data-testid': 'resync-toast' },
            h('span', {}, `事件流已重新同步：${state.resyncNote.reason}`),
        ));
    }
    return page;
}

function renderHeader(state: AppState): HTMLElement {
    const identity = state.identity;
    const hostStatus = identity?.host_status ?? 'stopped';
    return h(
        'header',
        { class: 'topbar' },
        h('div', { class: 'brand' }, h('span', { class: 'brand-mark' }, 'M'), h('span', { class: 'brand-name' }, 'Mirage 控制台')),
        h('div', { class: 'topbar-right' },
            hostBadge(hostStatus),
            h('a', { class: 'pill pill-transport', href: '#/settings/appearance', 'data-testid': 'host-detail-link' }, `${state.transportLabel} 数据 · 外观设置`),
        ),
    );
}

function renderNav(state: AppState): HTMLElement {
    const active = state.route.view === 'execution' ? 'tasks' : state.route.view;
    return h(
        'nav',
        { class: 'tabs', 'data-testid': 'nav' },
        navTab('#/workspace', '工作台', active === 'workspace'),
        navTab('#/tasks', '任务', active === 'tasks'),
        navTab('#/settings/appearance', '设置 · 外观', active === 'appearance'),
    );
}

function navTab(hash: string, label: string, isActive: boolean): HTMLElement {
    return h('a', { class: isActive ? 'tab tab-active' : 'tab', href: hash }, label);
}

function renderMain(state: AppState, actions: AppActions): HTMLElement {
    const main = h('main', { class: 'content' });
    if (state.connection === 'error') {
        render(
            main,
            h('section', { class: 'card error-card' },
                h('h2', {}, '无法连接运行时服务'),
                h('p', { class: 'muted' }, state.connectionError ?? '未知错误'),
            ),
        );
        return main;
    }
    if (state.connection === 'connecting') {
        render(main, h('p', { class: 'muted loading' }, '正在连接运行时服务…'));
        return main;
    }
    switch (state.route.view) {
        case 'workspace':
            render(main, renderWorkspace(state, actions));
            break;
        case 'tasks':
            render(main, renderTasks(state, actions));
            break;
        case 'execution':
            render(main, renderExecution(state, actions));
            break;
        case 'appearance':
            render(main, renderAppearance());
            break;
    }
    return main;
}
