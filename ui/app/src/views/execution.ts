/// 执行详情（Execution）：单个任务的 step 级实时视图 —— 进度、每个步骤的
/// 状态/权限/退出码/结果/错误，以及取消操作与终态横幅。

import type { StepView } from '@mirage/contracts';

import type { AppActions, AppState } from '../store.js';
import { isTerminalProgress, kindLabel, stepStatusLabel } from '../store.js';
import { h } from '../dom.js';
import { progressBadge } from './tasks.js';

export function renderExecution(state: AppState, actions: AppActions): HTMLElement {
    if (state.route.view !== 'execution') {
        return h('section', { class: 'card' });
    }
    const taskId = state.route.taskId;
    const detail = state.details.get(taskId);
    const summary = state.summaries.get(taskId);
    const container = h('div', { class: 'stack', 'data-testid': 'execution-view' });

    container.append(h(
        'div',
        { class: 'card-title-row' },
        h('button', {
            class: 'btn btn-ghost',
            type: 'button',
            onclick: () => actions.navigate({ view: 'tasks' }),
        }, '← 返回任务列表'),
    ));

    if (detail === undefined) {
        container.append(h(
            'section',
            { class: 'card' },
            h('h2', {}, `任务 ${taskId}`),
            h('p', { class: 'muted' }, summary !== undefined
                ? '正在载入执行详情…'
                : '该任务不存在或已被清理。'),
        ));
        return container;
    }

    container.append(renderHeaderCard(detail, actions));
    const stepsCard = h('section', { class: 'card' }, h('h2', {}, `步骤（${detail.steps.length}）`));
    if (detail.steps.length === 0) {
        stepsCard.append(h('p', { class: 'muted' }, '该任务没有步骤。'));
    } else {
        const list = h('ol', { class: 'step-list', 'data-testid': 'step-list' });
        for (const step of detail.steps) {
            list.append(renderStepItem(step));
        }
        stepsCard.append(list);
    }
    container.append(stepsCard);
    return container;
}

function renderHeaderCard(detail: { id: string; goal: string; progress: string; has_success: boolean; success?: boolean }, actions: AppActions): HTMLElement {
    const progress = detail.progress;
    const card = h('section', { class: 'card', 'data-testid': 'execution-header' });
    card.append(
        h('div', { class: 'card-title-row' },
            h('h2', {}, '执行详情'),
            h('div', { class: 'row-gap' },
                progressBadge(progress),
                cancelSlot(detail.id, progress, actions),
            ),
        ),
        h('p', { class: 'task-goal-line' }, detail.goal),
        h('p', { class: 'muted small mono' }, detail.id),
    );
    if (detail.has_success && detail.success === true) {
        card.append(h('div', { class: 'banner banner-success', 'data-testid': 'terminal-banner' }, '任务成功完成。'));
    } else if (progress === 'Failed') {
        card.append(h('div', { class: 'banner banner-failed', 'data-testid': 'terminal-banner' }, '任务失败：首个失败步骤导致任务终止（fail-fast）。'));
    } else if (progress === 'Cancelled') {
        card.append(h('div', { class: 'banner banner-cancelled', 'data-testid': 'terminal-banner' }, '任务已取消：运行中的步骤被中断，未开始的步骤被跳过。'));
    } else if (progress === 'Cancelling') {
        card.append(h('div', { class: 'banner banner-cancelled', 'data-testid': 'terminal-banner' }, '正在取消…'));
    }
    return card;
}

function cancelSlot(taskId: string, progress: string, actions: AppActions): HTMLElement {
    if (isTerminalProgress(progress) || progress === 'Idle' || progress === 'Paused') {
        return h('span', {});
    }
    const cancelling = progress === 'Cancelling';
    const button = h('button', {
        class: 'btn btn-danger',
        type: 'button',
        'data-testid': 'cancel-button',
        onclick: () => {
            button.disabled = true;
            button.textContent = '取消中…';
            actions.cancel(taskId).catch(() => {
                button.disabled = false;
                button.textContent = '取消任务';
            });
        },
    }, cancelling ? '取消中…' : '取消任务');
    if (cancelling) {
        button.disabled = true;
    }
    return button;
}

function renderStepItem(step: StepView): HTMLElement {
    const item = h('li', { class: 'step-item', 'data-status': step.status, 'data-testid': 'step-item' });
    item.append(h(
        'div',
        { class: 'step-head' },
        h('span', { class: 'step-index mono' }, `#${step.index + 1}`),
        h('span', { class: 'chip' }, kindLabel(step.kind)),
        h('span', { class: `badge badge-${step.status}` }, stepStatusLabel(step.status)),
        step.permission.length > 0
            ? h('span', { class: 'chip chip-muted' }, `权限：${step.permission}`)
            : h('span', {}),
    ));
    const meta: string[] = [];
    if (step.operation_id.length > 0) {
        meta.push(`operation: ${step.operation_id}`);
    }
    if (step.exit_code >= 0) {
        meta.push(`exit code: ${step.exit_code}`);
    }
    if (meta.length > 0) {
        item.append(h('p', { class: 'muted small mono' }, meta.join(' · ')));
    }
    if (step.error.length > 0) {
        item.append(h('p', { class: 'step-error', 'data-testid': 'step-error' }, step.error));
    }
    if (step.result.length > 0) {
        item.append(h(
            'details',
            { class: 'step-result' },
            h('summary', {}, `结果${step.result_truncated ? '（已截断）' : ''}`),
            h('pre', { class: 'mono' }, step.result),
        ));
    }
    return item;
}
