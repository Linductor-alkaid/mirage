/// 工作台（Workspace）：服务身份与主机状态卡片 + Composer（执行模式承载
/// M1.5-04 的任务提交表单：目标 + 步骤编辑器 + 超时）。提交成功后跳转到
/// 该任务的执行详情视图。

import type { AppActions, AppState } from '../store.js';
import { hostStatusLabel } from '../store.js';
import { h } from '../dom.js';
import { composer } from '../components/composer.js';

export function renderWorkspace(state: AppState, actions: AppActions): HTMLElement {
    const wrap = h('div', { class: 'stack' });
    wrap.append(renderIdentityCard(state));

    let mode: 'chat' | 'exec' = 'exec';
    const composerHost = h('div', {});
    const redraw = (): void => {
        composerHost.replaceChildren(composer({
            mode,
            onModeChange: (next) => {
                mode = next;
                redraw();
            },
            onSubmitExec: (input) => actions.submit(input),
        }));
    };
    redraw();
    wrap.append(h('section', { class: 'card', 'data-testid': 'workspace-composer' }, composerHost));
    return wrap;
}

function renderIdentityCard(state: AppState): HTMLElement {
    const identity = state.identity;
    const grid = h('div', { class: 'identity-grid', 'data-testid': 'identity' });
    if (identity === undefined) {
        grid.append(h('p', { class: 'muted' }, '暂无服务身份信息'));
    } else {
        grid.append(
            identityItem('服务名称', identity.service),
            identityItem('Mirage 版本', identity.mirage_version),
            identityItem('Mira Core 版本', identity.mira_core_version),
            identityItem('协议版本', String(identity.protocol)),
            identityItem('事件订阅', identity.events === true ? '支持' : '不支持（将使用轮询）'),
        );
    }
    return h(
        'section',
        { class: 'card' },
        h('h2', {}, '运行时服务'),
        grid,
        h('p', { class: 'muted small', 'data-testid': 'host-detail' },
            `Mira Host 状态：${hostStatusLabel(identity?.host_status ?? 'stopped')}（五态：stopped / starting / running / stopping / failed）`),
    );
}

function identityItem(label: string, value: string): HTMLElement {
    return h('div', { class: 'identity-item' },
        h('span', { class: 'identity-label' }, label),
        h('span', { class: 'identity-value' }, value),
    );
}
