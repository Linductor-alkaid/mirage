/// World Projector reducer 集成测试。

import { describe, expect, it } from 'vitest';

import {
    WorldProjector,
    mapActivityKindToVisual,
} from '../../src/world/projector/projector.js';
import { buildInitialBuilding } from '../../src/world/projector/layout.js';
import { newOrganizationId, resetIdCounterForTests } from '../../src/world/model/identity.js';
import type { Agent, OrganizationState } from '../../src/world/organization/types.js';

function seedAgent(id: string, teamId: string | null): Agent {
    return {
        id: id as never,
        displayName: id,
        roleId: 'role-engineer' as never,
        teamId: teamId as never,
        lifecycle: 'active',
        currentActivityId: null,
        currentTaskId: null,
        accent: '#E8A33D',
        joinedAt: 0,
    };
}

describe('WorldProjector', () => {
    it('mapActivityKindToVisual covers all kinds', () => {
        expect(mapActivityKindToVisual('working')).toBe('working');
        expect(mapActivityKindToVisual('collaborating')).toBe('collaborating');
        expect(mapActivityKindToVisual('idle')).toBe('idle');
        expect(mapActivityKindToVisual('blocked')).toBe('blocked');
        expect(mapActivityKindToVisual('reviewing')).toBe('reviewing');
        expect(mapActivityKindToVisual('testing')).toBe('testing');
        expect(mapActivityKindToVisual('reporting')).toBe('reporting');
        expect(mapActivityKindToVisual('waiting')).toBe('waiting');
    });

    it('resyncFromOrganization places agents in team workstations', () => {
        resetIdCounterForTests();
        const teamId = 'team-A' as never;
        const building = buildInitialBuilding([{ id: teamId, name: 'A' }]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
        });
        const org: OrganizationState = {
            id: newOrganizationId(),
            name: 'X',
            agents: { 'ag-1': seedAgent('ag-1', teamId) },
            teams: {},
            roles: {},
            tasks: {},
            activities: {},
            collaborations: {},
            logicalTime: 0,
            source: 'simulator',
        };
        projector.resyncFromOrganization(org);
        const w = projector.getWorld();
        const entities = Object.values(w.entities);
        expect(entities).toHaveLength(1);
        expect(entities[0]?.atWorkstation).not.toBeNull();
        expect(entities[0]?.teamId).toBe(teamId);
        const zone = w.building.floors[0]?.zones.find((z) => z.teamId === teamId);
        expect(zone?.workstations.some((ws) => ws.occupant === entities[0]?.id)).toBe(true);
    });

    it('project(agent.activity_started working) moves the entity', () => {
        resetIdCounterForTests();
        const teamId = 'team-A' as never;
        const building = buildInitialBuilding([{ id: teamId, name: 'A' }]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
        });
        const org: OrganizationState = {
            id: newOrganizationId(),
            name: 'X',
            agents: { 'ag-1': seedAgent('ag-1', teamId) },
            teams: {},
            roles: {},
            tasks: {},
            activities: {},
            collaborations: {},
            logicalTime: 0,
            source: 'simulator',
        };
        projector.resyncFromOrganization(org);
        const before = projector.getWorld();
        const entityId = Object.keys(before.entities)[0]!;
        const changed = projector.project({
            type: 'agent.activity_started',
            at: 100,
            source: 'simulator',
            activity: {
                id: 'act-1' as never,
                agentId: 'ag-1' as never,
                kind: 'working',
                taskId: 'task-1' as never,
                collaborationId: null,
                startedAt: 100,
                endedAt: null,
            },
        });
        expect(changed).toBe(true);
        const after = projector.getWorld();
        const ent2 = after.entities[entityId]!;
        expect(ent2.visualState).toBe('working');
        expect(ent2.nav.moving).toBe(true);
        expect(ent2.nav.destination).not.toBeNull();
    });

    it('project(agent.activity_ended) restores visual state to idle', () => {
        resetIdCounterForTests();
        const teamId = 'team-A' as never;
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: buildInitialBuilding([{ id: teamId, name: 'A' }]),
        });
        projector.resyncFromOrganization({
            id: newOrganizationId(),
            name: 'X',
            agents: { 'ag-1': seedAgent('ag-1', teamId) },
            teams: {},
            roles: {},
            tasks: {},
            activities: {},
            collaborations: {},
            logicalTime: 0,
            source: 'simulator',
        });
        projector.project({
            type: 'agent.activity_started',
            at: 100,
            source: 'simulator',
            activity: {
                id: 'act-1' as never,
                agentId: 'ag-1' as never,
                kind: 'working',
                taskId: 'task-1' as never,
                collaborationId: null,
                startedAt: 100,
                endedAt: null,
            },
        });
        projector.project({
            type: 'agent.activity_ended',
            at: 200,
            source: 'simulator',
            agentId: 'ag-1' as never,
            activityId: 'act-1' as never,
        });
        const entity = Object.values(projector.getWorld().entities)[0]!;
        expect(entity.visualState).toBe('idle');
        expect(entity.nav.moving).toBe(false);
    });

    it('project(agent.lifecycle_changed hibernated) toggles visual to hibernated', () => {
        resetIdCounterForTests();
        const teamId = 'team-A' as never;
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: buildInitialBuilding([{ id: teamId, name: 'A' }]),
        });
        projector.resyncFromOrganization({
            id: newOrganizationId(),
            name: 'X',
            agents: { 'ag-1': seedAgent('ag-1', teamId) },
            teams: {},
            roles: {},
            tasks: {},
            activities: {},
            collaborations: {},
            logicalTime: 0,
            source: 'simulator',
        });
        projector.project({
            type: 'agent.lifecycle_changed',
            at: 100,
            source: 'simulator',
            agentId: 'ag-1' as never,
            lifecycle: 'hibernated',
        });
        const entity = Object.values(projector.getWorld().entities)[0]!;
        expect(entity.visualState).toBe('hibernated');
    });
});