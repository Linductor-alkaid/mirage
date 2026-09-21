/// World Model + applyWorldDelta 不变量测试。

import { describe, expect, it } from 'vitest';

import {
    applyWorldDelta,
    emptyWorld,
} from '../../src/world/model/world.js';
import type { AgentEntity } from '../../src/world/model/agentEntity.js';
import type { WorldDelta } from '../../src/world/model/world.js';
import { newAgentEntityId, newBuildingId } from '../../src/world/model/identity.js';

function entity(id: string): AgentEntity {
    return {
        id: id as never,
        agentId: `ag-${id}` as never,
        displayName: id,
        accent: { r: 1, g: 0.7, b: 0.3 },
        teamId: null,
        position: { x: 0, y: 0, z: 0 },
        atWorkstation: null,
        atMeeting: null,
        visualState: 'idle',
        nav: { moving: false, nextWaypoint: null, destination: null, facing: 0, lastUpdateMs: 0 },
        updatedAt: 0,
    };
}

describe('World Model', () => {
    it('emptyWorld carries the source and now', () => {
        const w = emptyWorld(1_000, 'simulator');
        expect(w.logicalTime).toBe(1_000);
        expect(w.lastSource).toBe('simulator');
        expect(w.entities).toEqual({});
        expect(w.objects).toEqual({});
    });

    it('applyWorldDelta merges and removes entities immutably', () => {
        const w0 = emptyWorld(0, 'simulator');
        const a = entity('ae-a');
        const c = entity('ae-c');
        const delta1: WorldDelta = {
            at: 10,
            source: 'simulator',
            entities: { [a.id]: a, [c.id]: c },
        };
        const w1 = applyWorldDelta(w0, delta1);
        expect(Object.keys(w1.entities)).toHaveLength(2);
        expect(w0.entities).toEqual({});
        expect(w1).not.toBe(w0);

        const delta2: WorldDelta = {
            at: 20,
            source: 'simulator',
            removedEntityIds: [a.id],
        };
        const w2 = applyWorldDelta(w1, delta2);
        expect(Object.keys(w2.entities)).toHaveLength(1);
        expect(w2.entities[a.id]).toBeUndefined();
        expect(w2.entities[c.id]).toEqual(c);
    });

    it('applyWorldDelta replaces building wholesale when provided', () => {
        const w0 = emptyWorld(0, 'simulator');
        const newBuilding = {
            id: newBuildingId(),
            size: { width: 50, depth: 30 },
            floors: [],
        };
        const w1 = applyWorldDelta(w0, {
            at: 10,
            source: 'simulator',
            building: newBuilding,
        });
        expect(w1.building.id).toBe(newBuilding.id);
    });

    it('applyWorldDelta handles objects the same way', () => {
        const w0 = emptyWorld(0, 'simulator');
        const obj = {
            id: 'o-1' as never,
            label: 'printer',
            kind: 'printer' as const,
            anchor: { x: 1, y: 0, z: 1 },
        };
        const w1 = applyWorldDelta(w0, {
            at: 10,
            source: 'simulator',
            objects: { [obj.id]: obj },
        });
        expect(w1.objects[obj.id]).toEqual(obj);
    });

    it('applyWorldDelta increments logical time and updates source', () => {
        const w0 = emptyWorld(0, 'simulator');
        const w1 = applyWorldDelta(w0, {
            at: 5_000,
            source: 'mirage',
        });
        expect(w1.logicalTime).toBe(5_000);
        expect(w1.lastSource).toBe('mirage');
    });

    it('newAgentEntityId returns a brand string', () => {
        const id = newAgentEntityId();
        expect(typeof id).toBe('string');
        expect(id.startsWith('ae-')).toBe(true);
    });
});