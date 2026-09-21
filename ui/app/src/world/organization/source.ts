/// OrganizationEventSource 接口 + OrganizationStore。
///
/// OrganizationStore 是 Organization Layer 的状态容器；OrganizationEventSource
/// 是事件生产者的抽象（模拟器 / 真实 Mira multi-agent / 历史回放）。

import type {
    OrganizationState,
} from './types.js';
import type {
    EventSource,
    LogicalTime,
    OrganizationId,
} from '../model/types.js';
import type { OrganizationEvent } from './events.js';
import { emptyOrganizationState, reduceOrganization } from './reducer.js';

export type { OrganizationEvent } from './events.js';
export type { OrganizationState } from './types.js';

/** 事件源通用接口。 */
export interface OrganizationEventSource {
    /** 事件源标识。 */
    readonly id: string;
    readonly source: EventSource;
    /** 订阅事件流；返回 unsubscribe。 */
    subscribe(listener: (event: OrganizationEvent) => void): () => void;
    /** 拉取当前快照（M4 模拟器无接入时直接 yield 当前状态；真实运行时可能 throw）。 */
    snapshot(): OrganizationState;
    /** 启动 / 停止。 */
    start?(): void;
    stop?(): void;
}

type Listener = (state: OrganizationState, event: OrganizationEvent | null) => void;

/** 不可变状态 + 事件订阅；接受任意 OrganizationEventSource。 */
export class OrganizationStore {
    private state: OrganizationState;
    private readonly listeners = new Set<Listener>();
    private readonly unsubscribeSource: (() => void) | null;
    private disposed = false;
    private readonly eventLog: OrganizationEvent[] = [];
    /** 历史回放辅助：固定上限避免无界增长。 */
    static readonly MAX_EVENT_LOG = 4096;

    constructor(
        initial: OrganizationState,
        source: OrganizationEventSource | null = null,
    ) {
        this.state = initial;
        this.eventLog.push({
            type: 'organization.snapshot_sync',
            at: initial.logicalTime,
            source: initial.source,
            organizationId: initial.id,
            agents: initial.agents,
            teams: initial.teams,
            roles: initial.roles,
            tasks: initial.tasks,
            activities: initial.activities,
            collaborations: initial.collaborations,
        });
        this.unsubscribeSource = source ? source.subscribe((event) => this.apply(event)) : null;
    }

    getState(): OrganizationState {
        return this.state;
    }

    /** 当前事件日志（M11 历史回放基础设施）。 */
    eventLogSnapshot(): readonly OrganizationEvent[] {
        return [...this.eventLog];
    }

    apply(event: OrganizationEvent): void {
        if (this.disposed) {
            return;
        }
        this.state = reduceOrganization(this.state, event);
        this.eventLog.push(event);
        if (this.eventLog.length > OrganizationStore.MAX_EVENT_LOG) {
            this.eventLog.splice(0, this.eventLog.length - OrganizationStore.MAX_EVENT_LOG);
        }
        for (const listener of this.listeners) {
            listener(this.state, event);
        }
    }

    subscribe(listener: Listener): () => void {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    }

    dispose(): void {
        if (this.disposed) {
            return;
        }
        this.disposed = true;
        this.listeners.clear();
        this.unsubscribeSource?.();
    }
}

/** 工厂函数：方便测试与 React 使用。 */
export function createOrganizationStore(
    id: OrganizationId,
    source: OrganizationEventSource | null = null,
    now: LogicalTime = 0,
    sourceLabel: EventSource = 'simulator',
): OrganizationStore {
    return new OrganizationStore(emptyOrganizationState(id, sourceLabel, now), source);
}