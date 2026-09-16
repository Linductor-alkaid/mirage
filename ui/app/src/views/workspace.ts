/// 工作台（Workspace）：服务身份与主机状态卡片 + 任务提交表单。提交成功后
/// 跳转到该任务的执行详情视图。

import type { StepKind, SubmitTaskInput, TaskStep } from '@mirage/contracts';

import type { AppActions, AppState } from '../store.js';
import { hostStatusLabel } from '../store.js';
import { h, render } from '../dom.js';

interface StepDraft {
    op: StepKind;
    arg: string;
}

const STEP_KIND_OPTIONS: readonly { value: StepKind; label: string }[] = [
    { value: 'filesystem.read', label: '文件读取 (filesystem.read)' },
    { value: 'process.execute', label: '命令执行 (process.execute)' },
];

/** Mock-only convention (see ui/README.md): a process.execute argument
 * starting with `fail:` makes that step fail — the hint surfaces it for
 * demonstrating the error view. */
const FAIL_HINT = '提示：命令执行步骤的参数以 fail: 开头时，mock 将模拟该步骤失败（仅 mock 行为）。';

export function renderWorkspace(state: AppState, actions: AppActions): HTMLElement {
    const wrap = h('div', { class: 'stack' });
    wrap.append(renderIdentityCard(state));
    wrap.append(renderSubmitCard(actions));
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

function renderSubmitCard(actions: AppActions): HTMLElement {
    let stepCounter = 0;
    const drafts: StepDraft[] = [{ op: 'filesystem.read', arg: '' }];

    const stepsList = h('div', { class: 'steps-editor', 'data-testid': 'steps-editor' });
    const goalInput = h('textarea', {
        class: 'input goal-input',
        name: 'goal',
        rows: '3',
        placeholder: '描述这个任务要完成什么…',
    }) as HTMLTextAreaElement;
    const timeoutInput = h('input', {
        class: 'input',
        name: 'step_timeout_ms',
        type: 'number',
        min: '1',
        placeholder: '留空使用默认',
    }) as HTMLInputElement;
    const errorBox = h('div', { class: 'form-error', hidden: true, 'data-testid': 'submit-error' });
    const submitButton = h('button', { class: 'btn btn-primary', type: 'submit' }, '提交任务');

    const redrawSteps = (): void => {
        render(stepsList, ...drafts.map((draft, index) => h(
            'div',
            { class: 'step-editor-row' },
            h('span', { class: 'step-editor-index' }, String(index + 1)),
            stepKindSelect(draft, redrawSteps),
            stepArgInput(draft),
            h('button', {
                class: 'btn btn-ghost btn-icon',
                type: 'button',
                title: '移除步骤',
                onclick: () => {
                    drafts.splice(index, 1);
                    if (drafts.length === 0) {
                        drafts.push({ op: 'filesystem.read', arg: '' });
                    }
                    redrawSteps();
                },
            }, '✕'),
        )));
    };
    redrawSteps();

    const form = h(
        'form',
        {
            class: 'submit-form',
            onsubmit: (event: Event) => {
                event.preventDefault();
                errorBox.hidden = true;
                const goal = goalInput.value.trim();
                if (goal.length === 0) {
                    errorBox.textContent = '请填写任务目标。';
                    errorBox.hidden = false;
                    return;
                }
                const steps: TaskStep[] = drafts
                    .map((draft) => ({ op: draft.op, arg: draft.arg.trim() }))
                    .filter((step) => step.arg.length > 0);
                if (steps.length === 0) {
                    errorBox.textContent = '请至少添加一个步骤（并填写参数）。';
                    errorBox.hidden = false;
                    return;
                }
                const input: SubmitTaskInput = { goal, steps };
                const timeout = Number(timeoutInput.value);
                if (timeoutInput.value !== '' && Number.isFinite(timeout) && timeout > 0) {
                    input.step_timeout_ms = timeout;
                }
                submitButton.disabled = true;
                actions.submit(input)
                    .catch((error: unknown) => {
                        errorBox.textContent = error instanceof Error ? error.message : String(error);
                        errorBox.hidden = false;
                    })
                    .finally(() => {
                        submitButton.disabled = false;
                    });
            },
        },
        h('label', { class: 'field-label' }, '任务目标'),
        goalInput,
        h('div', { class: 'field-label-row' },
            h('span', { class: 'field-label' }, '步骤'),
            h('button', {
                class: 'btn btn-ghost',
                type: 'button',
                onclick: () => {
                    stepCounter += 1;
                    drafts.push({ op: stepCounter % 2 === 0 ? 'process.execute' : 'filesystem.read', arg: '' });
                    redrawSteps();
                },
            }, '+ 添加步骤'),
        ),
        stepsList,
        h('label', { class: 'field-label' }, '单步超时（毫秒，可选）'),
        timeoutInput,
        errorBox,
        h('div', { class: 'form-actions' }, submitButton),
        h('p', { class: 'muted small' }, FAIL_HINT),
    );

    return h('section', { class: 'card' }, h('h2', {}, '提交任务'), form);
}

function stepKindSelect(draft: StepDraft, redraw: () => void): HTMLElement {
    const select = h('select', {
        class: 'input step-kind',
        onchange: (event: Event) => {
            draft.op = (event.target as HTMLSelectElement).value as StepKind;
            redraw();
        },
    }) as HTMLSelectElement;
    for (const option of STEP_KIND_OPTIONS) {
        const opt = h('option', { value: option.value }, option.label);
        if (option.value === draft.op) {
            opt.selected = true;
        }
        select.append(opt);
    }
    return select;
}

function stepArgInput(draft: StepDraft): HTMLElement {
    return h('input', {
        class: 'input step-arg',
        value: draft.arg,
        placeholder: draft.op === 'filesystem.read' ? '/path/to/file' : 'shell 命令行',
        oninput: (event: Event) => {
            draft.arg = (event.target as HTMLInputElement).value;
        },
    }) as HTMLInputElement;
}
