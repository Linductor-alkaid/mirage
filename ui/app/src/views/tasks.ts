/// 任务列表（Tasks）：来自 task.list 快照 + task.updated 事件的摘要表，
/// 点击行进入执行详情。

import type { AppActions, AppState } from '../store.js';
import { taskBadge } from '../components/status-badge.js';
import { h } from '../dom.js';

export function renderTasks(state: AppState, actions: AppActions): HTMLElement {
    const summaries = [...state.summaries.values()].sort((a, b) => a.id.localeCompare(b.id));
    const table = h(
        'table',
        { class: 'task-table', 'data-testid': 'task-table' },
        h('thead', {}, h('tr', {},
            h('th', {}, '任务 ID'),
            h('th', {}, '目标'),
            h('th', {}, '进度'),
        )),
    );
    const body = h('tbody', {});
    if (summaries.length === 0) {
        body.append(h('tr', {}, h('td', { colspan: '3', class: 'muted empty-row' }, '暂无任务，去工作台提交一个吧。')));
    } else {
        for (const task of summaries) {
            body.append(h('tr', {
                class: 'task-row',
                'data-testid': 'task-row',
                onclick: () => actions.navigate({ view: 'execution', taskId: task.id }),
            },
            h('td', { class: 'mono' }, task.id),
            h('td', { class: 'task-goal' }, task.goal),
            h('td', {}, taskBadge(task.progress)),
            ));
        }
    }
    table.append(body);
    return h(
        'section',
        { class: 'card' },
        h('div', { class: 'card-title-row' },
            h('h2', {}, '任务'),
            h('button', {
                class: 'btn btn-ghost',
                type: 'button',
                onclick: () => {
                    actions.refresh().catch(() => undefined);
                },
            }, '刷新'),
        ),
        table,
    );
}
