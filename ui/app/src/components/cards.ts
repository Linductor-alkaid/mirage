/// ActivityCard / ApprovalCard(占位) / SnapshotFrame(占位) / StepCard
/// （设计规范 §2.5-5/6/8/9）。全部只消费 L2 语义 token；ApprovalCard 与
/// SnapshotFrame 是 M1.5-08 的占位规格实现（交互面在后续里程碑接线）。

import { h } from '../dom.js';
import { kindLabel } from '../store.js';
import { stepBadge } from './status-badge.js';

/** ActivityCard：工具调用 / 桌面动作 / 计划 / 工作流引用统一形态。
 * 标题行 = 类型标记 + 名称 + 状态徽标 + 耗时；正文默认折叠为一行摘要，
 * 点击展开；桌面动作卡展开后首屏为 SnapshotFrame。 */
export function activityCard(input: {
    kindLabel: string;
    name: string;
    badge: HTMLElement;
    duration?: string;
    summary: string;
    detail?: HTMLElement;
}): HTMLElement {
    const head = h(
        'div',
        { class: 'activity-card-head' },
        h('span', { class: 'activity-card-kind mono' }, input.kindLabel),
        h('span', { class: 'activity-card-name' }, input.name),
        input.badge,
        input.duration !== undefined
            ? h('span', { class: 'activity-card-duration muted small mono' }, input.duration)
            : h('span', {}),
    );
    const body = h(
        'div',
        { class: 'activity-card-body' },
        h('p', { class: 'activity-card-summary' }, input.summary),
    );
    if (input.detail !== undefined) {
        body.append(h('details', { class: 'activity-card-detail' }, h('summary', {}, '细节'), input.detail));
    }
    return h(
        'section',
        { class: 'activity-card', 'data-testid': 'activity-card' },
        head,
        body,
    );
}

/** ApprovalCard（占位）：打断式等宽横幅；边框按风险分级（warning /
 * destructive）；按钮组 = 允许一次 / 本会话始终允许 / 拒绝；附范围说明。
 * M1.5-08 仅交付视觉规格与无占位数据态，审批交互随权限线（DEC-010）接线。 */
export function approvalCardPlaceholder(): HTMLElement {
    return h(
        'section',
        { class: 'approval-card', 'data-testid': 'approval-card', 'data-placeholder': 'true' },
        h(
            'div',
            { class: 'approval-head' },
            h('span', { class: 'approval-title' }, '需要批准'),
            h('span', { class: 'badge is-warning' }, '占位规格'),
        ),
        h('p', { class: 'approval-scope muted small' }, '范围：Provider × 范围（DEC-010 语义，随权限线接线）'),
        h(
            'div',
            { class: 'approval-actions' },
            h('button', { class: 'btn btn-primary', type: 'button', disabled: true }, '允许一次'),
            h('button', { class: 'btn', type: 'button', disabled: true }, '本会话始终允许'),
            h('button', { class: 'btn btn-danger', type: 'button', disabled: true }, '拒绝'),
        ),
    );
}

/** SnapshotFrame（占位）：16:9 容器、圆角 8px、1px 边界；真实截图底与
 * `--evidence-highlight` 证据高亮随观察线（M2）接线。 */
export function snapshotFramePlaceholder(label = '观察快照'): HTMLElement {
    return h(
        'figure',
        { class: 'snapshot-frame', 'data-testid': 'snapshot-frame', 'data-placeholder': 'true' },
        h('span', { class: 'snapshot-frame-label chip' }, label),
    );
}

/** StepCard：垂直序列步骤卡 —— 左侧序号轨道 + 类型/参数摘要 + 状态徽标；
 * 运行详情（operation / exit code / 错误 / 结果）折叠展示。 */
export interface StepCardData {
    index: number;
    kind: string;
    status: string;
    permission: string;
    operationId: string;
    exitCode: number;
    error: string;
    result: string;
    resultTruncated: boolean;
}

export function stepCard(step: StepCardData): HTMLElement {
    const card = h(
        'li',
        { class: 'step-card', 'data-status': step.status, 'data-testid': 'step-card' },
        h(
            'div',
            { class: 'step-card-head' },
            h('span', { class: 'step-card-index mono' }, String(step.index + 1)),
            h('span', { class: 'chip' }, kindLabel(step.kind)),
            stepBadge(step.status),
            step.permission.length > 0
                ? h('span', { class: 'chip chip-muted' }, `权限：${step.permission}`)
                : h('span', {}),
        ),
    );
    const meta: string[] = [];
    if (step.operationId.length > 0) {
        meta.push(`operation: ${step.operationId}`);
    }
    if (step.exitCode >= 0) {
        meta.push(`exit code: ${step.exitCode}`);
    }
    if (meta.length > 0) {
        card.append(h('p', { class: 'muted small mono step-card-meta' }, meta.join(' · ')));
    }
    if (step.error.length > 0) {
        card.append(h('p', { class: 'step-error', 'data-testid': 'step-error' }, step.error));
    }
    if (step.result.length > 0) {
        card.append(h(
            'details',
            { class: 'step-result' },
            h('summary', {}, `结果${step.resultTruncated ? '（已截断）' : ''}`),
            h('pre', { class: 'mono' }, step.result),
        ));
    }
    return card;
}
