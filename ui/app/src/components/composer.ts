/// Composer（设计规范 §2.5-10）：底部固定输入区 —— 顶行模式切换（对话 /
/// 执行 segmented）+ 模型选择器；主体多行自适应（3–8 行）；右下发送；
/// 运行中变为停止。M1.5-04 的任务提交表单（目标 + 步骤编辑器 + 超时）作为
/// Composer 的「执行」模式主体接入，功能等价（会话化迁移在 M1.5-07）。

import type { StepKind, SubmitTaskInput, TaskStep } from '@mirage/contracts';

import { h, render } from '../dom.js';

export type ComposerMode = 'chat' | 'exec';

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

export interface ComposerProps {
    mode: ComposerMode;
    onModeChange?: (mode: ComposerMode) => void;
    onSubmitExec: (input: SubmitTaskInput) => Promise<void>;
}

export function composer(props: ComposerProps): HTMLElement {
    let stepCounter = 0;
    const drafts: StepDraft[] = [{ op: 'filesystem.read', arg: '' }];

    const execBody = h('div', { class: 'composer-exec' });
    const textArea = h('textarea', {
        class: 'composer-input mono',
        name: 'goal',
        rows: '3',
        placeholder: props.mode === 'exec' ? '描述这个任务要完成什么…' : '与 Mirage 对话（M1.5-07 会话化迁移）…',
        oninput: (event: Event) => {
            const el = event.target as HTMLTextAreaElement;
            // 多行自适应（3–8 行）
            el.style.height = 'auto';
            el.style.height = `${Math.min(Math.max(el.scrollHeight, 66), 176)}px`;
        },
    }) as HTMLTextAreaElement;

    const errorBox = h('div', { class: 'form-error', hidden: true, 'data-testid': 'submit-error' });
    const sendButton = h(
        'button',
        { class: 'btn btn-primary', type: 'submit', 'data-testid': 'composer-send' },
        props.mode === 'exec' ? '提交任务' : '发送',
    );

    const redrawExecBody = (): void => {
        if (props.mode !== 'exec') {
            render(execBody);
            return;
        }
        const stepsList = h('div', { class: 'steps-editor', 'data-testid': 'steps-editor' });
        const timeoutInput = h('input', {
            class: 'input',
            name: 'step_timeout_ms',
            type: 'number',
            min: '1',
            placeholder: '留空使用默认',
        }) as HTMLInputElement;
        render(
            execBody,
            h('label', { class: 'field-label' }, '步骤'),
            h('div', { class: 'field-label-row' },
                h('span', { class: 'field-label' }, '单步超时（毫秒，可选）'),
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
            timeoutInput,
            errorBox,
            h('p', { class: 'muted small' }, FAIL_HINT),
        );
        timeoutRef = timeoutInput;

        const redrawSteps = (): void => {
            render(stepsList, ...drafts.map((draft, index) => h(
                'div',
                { class: 'step-editor-row' },
                h('span', { class: 'step-editor-index' }, String(index + 1)),
                stepKindSelect(draft),
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
    };
    let timeoutRef: HTMLInputElement | null = null;

    const form = h(
        'form',
        {
            class: 'composer',
            'data-testid': 'composer',
            onsubmit: (event: Event) => {
                event.preventDefault();
                if (props.mode !== 'exec') {
                    errorBox.textContent = '对话模式随 M1.5-07 会话化接线。';
                    errorBox.hidden = false;
                    return;
                }
                errorBox.hidden = true;
                const goal = textArea.value.trim();
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
                const timeout = timeoutRef !== null ? Number(timeoutRef.value) : NaN;
                if (timeoutRef !== null && timeoutRef.value !== '' && Number.isFinite(timeout) && timeout > 0) {
                    input.step_timeout_ms = timeout;
                }
                sendButton.disabled = true;
                props.onSubmitExec(input)
                    .catch((error: unknown) => {
                        errorBox.textContent = error instanceof Error ? error.message : String(error);
                        errorBox.hidden = false;
                    })
                    .finally(() => {
                        sendButton.disabled = false;
                    });
            },
        },
        h(
            'div',
            { class: 'composer-top' },
            segmented(props.mode, (mode) => {
                if (props.onModeChange !== undefined) {
                    props.onModeChange(mode);
                }
            }),
            h('span', { class: 'chip chip-muted', 'data-testid': 'composer-model' }, '模型：默认（占位）'),
        ),
        textArea,
        execBody,
        h('div', { class: 'composer-actions' }, sendButton),
    );
    redrawExecBody();
    return form;
}

function segmented(mode: ComposerMode, onChange: (mode: ComposerMode) => void): HTMLElement {
    const segButton = (value: ComposerMode, label: string): HTMLElement => {
        const button = h(
            'button',
            {
                class: `segmented-item${mode === value ? ' segmented-active' : ''}`,
                type: 'button',
                'aria-pressed': String(mode === value),
                'data-testid': `composer-mode-${value}`,
            },
            label,
        );
        button.addEventListener('click', () => onChange(value));
        return button;
    };
    return h('div', { class: 'segmented', role: 'group', 'aria-label': '输入模式' },
        segButton('chat', '对话'),
        segButton('exec', '执行'),
    );
}

function stepKindSelect(draft: StepDraft): HTMLElement {
    const select = h('select', {
        class: 'input step-kind',
        onchange: (event: Event) => {
            draft.op = (event.target as HTMLSelectElement).value as StepKind;
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
