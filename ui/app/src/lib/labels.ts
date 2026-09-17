/// 展示层标签与格式化（契约枚举的中文封闭映射 + 时间/相对时间）。

import type { HostStatus, TaskProgress } from '@mirage/contracts';

export function progressLabel(progress: TaskProgress | string): string {
    switch (progress) {
        case 'Idle':
            return '空闲';
        case 'Active':
            return '执行中';
        case 'Paused':
            return '已暂停';
        case 'Cancelling':
            return '取消中';
        case 'Completed':
            return '已完成';
        case 'Failed':
            return '已失败';
        case 'Cancelled':
            return '已取消';
        default:
            return '未知';
    }
}

/** 进度 → 徽标语义色（封闭映射）。 */
export function progressTone(progress: TaskProgress | string): 'success' | 'warning' | 'danger' | 'info' | 'muted' {
    switch (progress) {
        case 'Completed':
            return 'success';
        case 'Failed':
            return 'danger';
        case 'Cancelled':
            return 'muted';
        case 'Active':
        case 'Cancelling':
            return 'info';
        case 'Paused':
            return 'warning';
        default:
            return 'muted';
    }
}

export function hostStatusLabel(status: HostStatus | string): string {
    switch (status) {
        case 'stopped':
            return '已停止';
        case 'starting':
            return '启动中';
        case 'running':
            return '运行中';
        case 'stopping':
            return '停止中';
        case 'failed':
            return '故障';
        default:
            return '未知';
    }
}

export function hostStatusDot(status: HostStatus | string): 'ok' | 'warn' | 'bad' | 'run' {
    switch (status) {
        case 'running':
            return 'ok';
        case 'starting':
        case 'stopping':
            return 'run';
        case 'failed':
            return 'bad';
        default:
            return 'warn';
    }
}

export function stepStatusLabel(status: string): string {
    switch (status) {
        case 'pending':
            return '等待';
        case 'running':
            return '运行中';
        case 'ok':
            return '成功';
        case 'failed':
            return '失败';
        case 'skipped':
            return '跳过';
        case 'cancelled':
            return '已取消';
        default:
            return status === '' ? '未知' : status;
    }
}

export function kindLabel(kind: string): string {
    switch (kind) {
        case 'filesystem.read':
            return '文件读取';
        case 'process.execute':
            return '命令执行';
        case 'display.observe':
            return '屏幕观察';
        default:
            return kind === '' ? '未知' : kind;
    }
}

export function approvalKindLabel(kind: string): string {
    switch (kind) {
        case 'desktop.action':
            return '桌面动作';
        case 'process.execute':
            return '命令执行';
        case 'filesystem.write':
            return '文件写入';
        default:
            return kind;
    }
}

export function clock(t: number): string {
    return new Date(t).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
}

export function relativeTime(t: number, now: number): string {
    const diff = Math.max(0, now - t);
    if (diff < 60_000) {
        return '刚刚';
    }
    if (diff < 3_600_000) {
        return `${Math.floor(diff / 60_000)} 分钟前`;
    }
    if (diff < 86_400_000) {
        return `${Math.floor(diff / 3_600_000)} 小时前`;
    }
    return `${Math.floor(diff / 86_400_000)} 天前`;
}

export function duration(ms: number | undefined): string {
    if (ms === undefined) {
        return '—';
    }
    if (ms < 1000) {
        return `${ms}ms`;
    }
    return `${(ms / 1000).toFixed(1)}s`;
}

export function thinkingSeconds(elapsedMs: number): string {
    return `${Math.max(1, Math.round(elapsedMs / 1000))}`;
}
