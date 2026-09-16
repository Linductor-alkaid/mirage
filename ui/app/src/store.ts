/// UI-side application state: the observable snapshot store the views render
/// from. Facts come from `task.list` / `task.inspect` snapshots; events only
/// trigger refetches (DEC-012 decision 4 — events are notifications, never
/// treated as complete state).

import type {
    HostStatus,
    InspectTask,
    ServiceIdentity,
    SubmitTaskInput,
    TaskProgress,
    TaskSummary,
} from '@mirage/contracts';

export type Route =
    | { view: 'workspace' }
    | { view: 'tasks' }
    | { view: 'execution'; taskId: string }
    | { view: 'appearance' };

/** The operations views can invoke; the controller (app.ts) implements. */
export interface AppActions {
    submit(input: SubmitTaskInput): Promise<void>;
    cancel(taskId: string): Promise<void>;
    refresh(): Promise<void>;
    navigate(route: Route): void;
}

export type ConnectionStatus = 'connecting' | 'ready' | 'error';

export interface AppState {
    connection: ConnectionStatus;
    connectionError?: string;
    transportLabel: string;
    identity?: ServiceIdentity;
    summaries: Map<string, TaskSummary>;
    details: Map<string, InspectTask>;
    route: Route;
    /** Last event-stream resync, shown as a transient note. */
    resyncNote?: { reason: string; at: number };
}

export function parseRoute(hash: string): Route {
    const path = hash.replace(/^#/, '');
    const execution = /^\/task\/(.+)$/.exec(path);
    if (execution !== null && execution[1] !== undefined && execution[1].length > 0) {
        return { view: 'execution', taskId: decodeURIComponent(execution[1]) };
    }
    if (path === '/tasks') {
        return { view: 'tasks' };
    }
    if (path === '/settings/appearance') {
        return { view: 'appearance' };
    }
    return { view: 'workspace' };
}

export function routeToHash(route: Route): string {
    switch (route.view) {
        case 'tasks':
            return '#/tasks';
        case 'execution':
            return `#/task/${encodeURIComponent(route.taskId)}`;
        case 'appearance':
            return '#/settings/appearance';
        default:
            return '#/workspace';
    }
}

type Listener = () => void;

export class Store {
    private readonly listeners = new Set<Listener>();
    private state: AppState = {
        connection: 'connecting',
        transportLabel: 'Mock',
        summaries: new Map(),
        details: new Map(),
        route: parseRoute(window.location.hash),
    };

    get(): AppState {
        return this.state;
    }

    subscribe(listener: Listener): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    update(patch: Partial<AppState>): void {
        this.state = { ...this.state, ...patch };
        for (const listener of this.listeners) {
            listener();
        }
    }
}

// ---------------------------------------------------------------------------
// Presentation helpers shared by views
// ---------------------------------------------------------------------------

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
            return status || '未知';
    }
}

export function kindLabel(kind: string): string {
    switch (kind) {
        case 'filesystem.read':
            return '文件读取';
        case 'process.execute':
            return '命令执行';
        default:
            return kind || '未知';
    }
}

export function isTerminalProgress(progress: TaskProgress | string): boolean {
    return progress === 'Completed' || progress === 'Failed' || progress === 'Cancelled';
}
