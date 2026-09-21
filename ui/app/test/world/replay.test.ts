/// M11 历史回放基础。

import { describe, expect, it } from 'vitest';

import {
    ManualWorldClock,
    WorldReplayEngine,
    replayEventsToState,
} from '../../src/world/replay.js';
import { createOrganizationStore } from '../../src/world/organization/source.js';
import { emptyOrganizationState, reduceOrganization } from '../../src/world/organization/reducer.js';
import { newOrganizationId, resetIdCounterForTests } from '../../src/world/model/identity.js';
import type { OrganizationEvent } from '../../src/world/organization/events.js';

describe('M11 replay foundation', () => {
    it('ManualWorldClock advances monotonically and notifies', () => {
        const clock = new ManualWorldClock();
        let last = -1;
        const unsub = clock.subscribe((at) => {
            last = at;
        });
        clock.advance(100);
        expect(clock.now()).toBe(100);
        clock.advance(50); // no-op (倒退)
        expect(clock.now()).toBe(100);
        clock.advance(150);
        expect(clock.now()).toBe(150);
        expect(last).toBe(150);
        unsub();
    });

    it('WorldReplayEngine wires source events to store + clock', () => {
        resetIdCounterForTests();
        const store = createOrganizationStore(newOrganizationId(), null, 0, 'simulator');
        const clock = new ManualWorldClock();
        const engine = new WorldReplayEngine(store, clock);
        // 模拟 source
        const events: OrganizationEvent[] = [
            {
                type: 'agent.created',
                at: 10,
                source: 'simulator',
                agent: {
                    id: 'ag-1' as never,
                    displayName: 'A1',
                    roleId: 'role-engineer' as never,
                    teamId: null,
                    lifecycle: 'active',
                    currentActivityId: null,
                    currentTaskId: null,
                    accent: '#E8A33D',
                    joinedAt: 10,
                },
            },
            {
                type: 'agent.removed',
                at: 20,
                source: 'simulator',
                agentId: 'ag-1' as never,
            },
        ];
        const unsub = engine.bindSource({
            subscribe: (listener) => {
                for (const ev of events) {
                    listener(ev);
                }
                return () => undefined;
            },
        });
        unsub();
        engine.dispose();
        expect(clock.now()).toBe(20);
        expect(store.getState().agents['ag-1' as never]).toBeUndefined();
    });

    it('replayEventsToState replays events up to a target time', () => {
        const initial = emptyOrganizationState(newOrganizationId(), 'simulator', 0);
        const events: OrganizationEvent[] = [
            {
                type: 'agent.created',
                at: 100,
                source: 'simulator',
                agent: {
                    id: 'ag-1' as never,
                    displayName: 'A1',
                    roleId: 'role-engineer' as never,
                    teamId: null,
                    lifecycle: 'active',
                    currentActivityId: null,
                    currentTaskId: null,
                    accent: '#000000',
                    joinedAt: 100,
                },
            },
            {
                type: 'agent.removed',
                at: 200,
                source: 'simulator',
                agentId: 'ag-1' as never,
            },
        ];
        const at100 = replayEventsToState(initial, events, 100);
        expect(at100.agents['ag-1' as never]).toBeDefined();
        const at200 = replayEventsToState(initial, events, 200);
        expect(at200.agents['ag-1' as never]).toBeUndefined();
    });

    it('replayEventsToState respects event order; out-of-order tolerated by reducer', () => {
        const initial = emptyOrganizationState(newOrganizationId(), 'simulator', 0);
        // 两个 agent.created 事件，时间顺序无关（按 events 数组顺序）
        const events: OrganizationEvent[] = [
            {
                type: 'agent.created',
                at: 10,
                source: 'simulator',
                agent: {
                    id: 'ag-A' as never,
                    displayName: 'A',
                    roleId: 'role-engineer' as never,
                    teamId: null,
                    lifecycle: 'active',
                    currentActivityId: null,
                    currentTaskId: null,
                    accent: '#000000',
                    joinedAt: 10,
                },
            },
            {
                type: 'agent.created',
                at: 5,
                source: 'simulator',
                agent: {
                    id: 'ag-B' as never,
                    displayName: 'B',
                    roleId: 'role-engineer' as never,
                    teamId: null,
                    lifecycle: 'active',
                    currentActivityId: null,
                    currentTaskId: null,
                    accent: '#000000',
                    joinedAt: 5,
                },
            },
        ];
        const after = replayEventsToState(initial, events, 10);
        expect(Object.keys(after.agents).sort()).toEqual(['ag-A', 'ag-B']);
    });

    it('OrganizationStore event log is a circular buffer (M11 foundation)', () => {
        const store = createOrganizationStore(newOrganizationId(), null, 0, 'simulator');
        for (let i = 0; i < 100; i += 1) {
            store.apply({
                type: 'agent.lifecycle_changed',
                at: i,
                source: 'simulator',
                agentId: 'ag-1' as never,
                lifecycle: 'active',
            });
        }
        const log = store.eventLogSnapshot();
        // MAX_EVENT_LOG = 4096，100 个事件全部保留
        expect(log.length).toBe(101); // 1 snapshot_sync + 100 events
    });
});