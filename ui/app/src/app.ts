/// Application controller: owns the transport, mirrors snapshot facts into
/// the store, consumes the DEC-012 event stream (with resync + polling
/// fallback), and exposes the actions the views call.

import {
    EventSequencer,
    IpcRequestError,
    TransportClosedError,
} from '@mirage/contracts';
import type { MirageTransport, ServerEvent } from '@mirage/contracts';

import { parseRoute, routeToHash, Store } from './store.js';
import type { AppActions, AppState } from './store.js';
import { render } from './dom.js';
import { renderShell } from './views/shell.js';

const POLL_INTERVAL_MS = 1500;

export class App {
    private readonly sequencer = new EventSequencer();
    private readonly refreshingDetails = new Set<string>();
    private pollTimer: ReturnType<typeof setInterval> | null = null;
    private listener: ((event: ServerEvent) => void) | null = null;

    constructor(
        private readonly transport: MirageTransport,
        private readonly store: Store,
    ) {}

    readonly actions: AppActions = {
        submit: async (input) => {
            const { task_id } = await this.transport.submitTask(input);
            window.location.hash = routeToHash({ view: 'execution', taskId: task_id });
            await this.fetchInspect(task_id);
            await this.refreshSummaries();
        },
        cancel: async (taskId) => {
            await this.transport.cancelTask(taskId);
            await this.fetchInspect(taskId);
        },
        refresh: () => this.refreshAll(),
        navigate: (route) => {
            window.location.hash = routeToHash(route);
        },
    };

    async start(root: HTMLElement): Promise<void> {
        window.addEventListener('pagehide', () => this.stop());
        window.addEventListener('hashchange', () => {
            this.store.update({ route: parseRoute(window.location.hash) });
            void this.ensureRouteDetail();
        });
        this.store.subscribe(() => this.renderApp(root));
        try {
            const identity = await this.transport.hello();
            this.store.update({ identity, connection: 'ready' });
        } catch (error) {
            this.store.update({
                connection: 'error',
                connectionError: error instanceof Error ? error.message : String(error),
            });
            return;
        }
        await this.refreshSummaries();
        await this.ensureRouteDetail();
        await this.setupEvents();
        this.renderApp(root);
    }

    /** Releases the transport-facing resources (poll timer, subscription);
     * used on page hide. */
    stop(): void {
        if (this.pollTimer !== null) {
            clearInterval(this.pollTimer);
            this.pollTimer = null;
        }
        if (this.listener !== null) {
            void this.transport.unsubscribe().catch(() => undefined);
            this.listener = null;
        }
    }

    private async setupEvents(): Promise<void> {
        if (!this.transport.eventsSupported) {
            // Degradation path (DEC-012 decision 2): no event surface —
            // poll snapshots instead.
            this.pollTimer = setInterval(() => {
                void this.refreshAll();
            }, POLL_INTERVAL_MS);
            return;
        }
        this.listener = (event) => this.onEvent(event);
        try {
            await this.transport.subscribe(this.listener);
        } catch (error) {
            if (error instanceof IpcRequestError && error.code === 'unsupported') {
                this.pollTimer = setInterval(() => {
                    void this.refreshAll();
                }, POLL_INTERVAL_MS);
            }
        }
    }

    private onEvent(event: ServerEvent): void {
        const result = this.sequencer.push(event);
        if (result.kind === 'resync') {
            this.store.update({
                resyncNote: {
                    reason:
                        result.reason === 'overflow'
                            ? `事件队列溢出（丢弃 ${result.dropped ?? '?'} 条）`
                            : '事件序号跳跃',
                    at: Date.now(),
                },
            });
            window.setTimeout(() => {
                if (this.store.get().resyncNote !== undefined) {
                    this.store.update({ resyncNote: undefined });
                }
            }, 6000);
            void this.refreshAll();
            return;
        }
        if (event.event === 'task.updated') {
            const state = this.store.get();
            const summaries = new Map(state.summaries);
            summaries.set(event.task_id, {
                id: event.task_id,
                goal: event.goal,
                progress: event.progress,
            });
            this.store.update({ summaries });
            const route = state.route;
            if (
                state.details.has(event.task_id) ||
                (route.view === 'execution' && route.taskId === event.task_id)
            ) {
                void this.fetchInspect(event.task_id);
            }
            return;
        }
        if (event.event === 'host.status') {
            const identity = this.store.get().identity;
            if (identity !== undefined) {
                this.store.update({ identity: { ...identity, host_status: event.status } });
            }
        }
    }

    private async fetchInspect(taskId: string): Promise<void> {
        if (this.refreshingDetails.has(taskId)) {
            return;
        }
        this.refreshingDetails.add(taskId);
        try {
            const detail = await this.transport.inspectTask(taskId);
            const state = this.store.get();
            const details = new Map(state.details);
            details.set(taskId, detail);
            this.store.update({ details });
        } catch (error) {
            if (error instanceof IpcRequestError && error.code === 'not_found') {
                const state = this.store.get();
                const details = new Map(state.details);
                const summaries = new Map(state.summaries);
                details.delete(taskId);
                summaries.delete(taskId);
                this.store.update({ details, summaries });
            }
        } finally {
            this.refreshingDetails.delete(taskId);
        }
    }

    private async refreshSummaries(): Promise<void> {
        try {
            const tasks = await this.transport.listTasks();
            const summaries = new Map<string, (typeof tasks)[number]>();
            for (const task of tasks) {
                summaries.set(task.id, task);
            }
            this.store.update({ summaries });
        } catch (error) {
            if (error instanceof TransportClosedError) {
                this.store.update({
                    connection: 'error',
                    connectionError: error.message,
                });
            }
        }
    }

    private async refreshAll(): Promise<void> {
        const state = this.store.get();
        await this.refreshSummaries();
        for (const taskId of state.details.keys()) {
            await this.fetchInspect(taskId);
        }
        await this.ensureRouteDetail();
    }

    private async ensureRouteDetail(): Promise<void> {
        const route = this.store.get().route;
        if (route.view === 'execution') {
            await this.fetchInspect(route.taskId);
        }
    }

    private renderApp(root: HTMLElement): void {
        const state: AppState = this.store.get();
        render(root, renderShell(state, this.actions));
    }
}
