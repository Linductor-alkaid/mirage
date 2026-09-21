/// M11 历史回放基础（架构接口层，不含 UI）。
///
/// 设计：
/// - WorldClock：可由 simulator / replay 源驱动，统一向前推进或向后跳；
/// - EventLog：OrganizationStore 已经维护了环形 eventLog（上限 4096）；
/// - WorldReplayEngine：接收一个 EventSource（如 OrganizationSimulator）的
///   EventSequence，按 LogicalTime 顺序回放到 OrganizationStore；
/// - 这层不引入 React / Three.js，仅保证接口存在；
/// - M12 之后再做时间轴 UI 与播放 / 暂停 / 跳进。

import type { OrganizationEvent } from './organization/events.js';
import type { OrganizationStore } from './organization/source.js';
import type { OrganizationState } from './organization/types.js';
import { reduceOrganization } from './organization/reducer.js';
import type { LogicalTime } from './model/types.js';

export interface WorldClock {
    /** 当前逻辑时间。 */
    now(): LogicalTime;
    /** 订阅 tick；外部通过 advance() 推进。 */
    subscribe(listener: (at: LogicalTime) => void): () => void;
    /** 推进时间（不允许倒退）。 */
    advance(at: LogicalTime): void;
}

export class ManualWorldClock implements WorldClock {
    private _now: LogicalTime = 0;
    private readonly listeners = new Set<(at: LogicalTime) => void>();
    now(): LogicalTime {
        return this._now;
    }
    subscribe(listener: (at: LogicalTime) => void): () => void {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    }
    advance(at: LogicalTime): void {
        if (at < this._now) {
            return;
        }
        if (at === this._now) {
            return;
        }
        this._now = at;
        for (const l of this.listeners) {
            l(at);
        }
    }
}

/** 事件日志抽象：用于历史回放与 timeline UI。 */
export interface EventLogReader {
    /** 按时间排序返回事件序列（含 snapshot_sync）。 */
    snapshot(): readonly OrganizationEvent[];
    /** 时间窗内的子集；首尾闭区间。 */
    range(from: LogicalTime, to: LogicalTime): readonly OrganizationEvent[];
}

/** 把 EventSource 与 store 串起来：把 source 推来的事件转发给 store。
 *  回放场景下：source 是 EventLogReader 而非实时 source。 */
export class WorldReplayEngine {
    private readonly store: OrganizationStore;
    private readonly clock: WorldClock;
    private unsubscribeSource: (() => void) | null = null;
    private disposed = false;

    constructor(store: OrganizationStore, clock: WorldClock) {
        this.store = store;
        this.clock = clock;
    }

    /** 绑定一个事件源；事件来时自动 apply 到 store 并推进 clock。 */
    bindSource(source: { subscribe: (l: (e: OrganizationEvent) => void) => () => void }): () => void {
        this.unsubscribeSource = source.subscribe((event) => {
            this.store.apply(event);
            this.clock.advance(event.at);
        });
        return () => {
            this.unsubscribeSource?.();
            this.unsubscribeSource = null;
        };
    }

    dispose(): void {
        if (this.disposed) {
            return;
        }
        this.disposed = true;
        this.unsubscribeSource?.();
        this.unsubscribeSource = null;
    }
}

/** 从快照与事件集合「重建」到指定 LogicalTime 的 OrganizationState。
 *  主要给未来 timeline UI 用：用户拖动滑杆时重放；M12 之前由调用方
 *  自行保证事件序列与 state 的合法性。 */
export function replayEventsToState(
    initialState: OrganizationState,
    events: readonly OrganizationEvent[],
    target: LogicalTime,
): OrganizationState {
    // 简化策略：从 initialState 出发按顺序 reduce 到 target 时刻；
    // 大于 target 的事件被丢弃。
    let state = initialState;
    for (const ev of events) {
        if (ev.at > target) {
            break;
        }
        state = reduceOrganization(state, ev);
    }
    return state;
}