/// StatusBadge（设计规范 §2.5-7）：封闭状态→色调映射，禁止新色。任务 /
/// 步骤 / 主机三个状态集分别映射到六个语义色调；组件只输出 `is-<tone>`
/// class，颜色全部经 L2 语义 token（`--success` / `--warning` / …）。

import { h } from '../dom.js';
import {
    hostStatusLabel,
    progressLabel,
    stepStatusLabel,
} from '../store.js';

export type BadgeTone = 'success' | 'destructive' | 'primary' | 'warning' | 'info' | 'muted';

/** 任务 progress 封闭映射：Idle 灰 / Active 蓝 / Paused 灰蓝 / Cancelling
 * 琥珀 / Completed 绿 / Failed 红 / Cancelled 灰。 */
export function taskBadgeTone(progress: string): BadgeTone {
    switch (progress) {
        case 'Active': return 'primary';
        case 'Paused': return 'info';
        case 'Cancelling': return 'warning';
        case 'Completed': return 'success';
        case 'Failed': return 'destructive';
        default: return 'muted'; // Idle / Cancelled / 未知
    }
}

/** 步骤状态封闭映射：pending 灰 / running 蓝 / ok 绿 / failed 红 /
 * skipped・cancelled 灰。 */
export function stepBadgeTone(status: string): BadgeTone {
    switch (status) {
        case 'running': return 'primary';
        case 'ok': return 'success';
        case 'failed': return 'destructive';
        default: return 'muted'; // pending / skipped / cancelled
    }
}

/** 主机五态封闭映射：running 绿 / starting・stopping 琥珀 / stopped 灰 /
 * failed 红。 */
export function hostBadgeTone(status: string): BadgeTone {
    switch (status) {
        case 'running': return 'success';
        case 'starting':
        case 'stopping': return 'warning';
        case 'failed': return 'destructive';
        default: return 'muted'; // stopped
    }
}

export function statusBadge(label: string, tone: BadgeTone, testid?: string): HTMLElement {
    const attrs: Record<string, string> = { class: `badge is-${tone}` };
    if (testid !== undefined) {
        attrs['data-testid'] = testid;
    }
    return h('span', attrs, h('span', { class: 'badge-dot' }), label);
}

export function taskBadge(progress: string): HTMLElement {
    return statusBadge(progressLabel(progress), taskBadgeTone(progress), 'task-badge');
}

export function stepBadge(status: string): HTMLElement {
    return statusBadge(stepStatusLabel(status), stepBadgeTone(status), 'step-badge');
}

export function hostBadge(status: string): HTMLElement {
    return statusBadge(hostStatusLabel(status), hostBadgeTone(status), 'host-badge');
}
