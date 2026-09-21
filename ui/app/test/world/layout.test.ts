/// Procedural layout：Building / Floor / Zone / Workstation 形状断言。

import { describe, expect, it } from 'vitest';

import {
    computeBuildingSize,
    layoutFloor,
    buildInitialBuilding,
    findZoneByTeam,
    assignWorkstation,
    initialAgentAnchor,
    ensureAgentAssigned,
} from '../../src/world/projector/layout.js';
import { newFloorId, newTeamId, resetIdCounterForTests } from '../../src/world/model/identity.js';
import { newAgentEntityId } from '../../src/world/model/identity.js';
import type { AgentEntity } from '../../src/world/model/agentEntity.js';

describe('procedural layout', () => {
    it('computeBuildingSize grows with team count', () => {
        resetIdCounterForTests();
        const a = computeBuildingSize(1);
        const b = computeBuildingSize(4);
        const c = computeBuildingSize(16);
        expect(b.width).toBeGreaterThan(a.width);
        expect(c.width).toBeGreaterThan(b.width);
    });

    it('computeBuildingSize returns zero for 0 teams', () => {
        expect(computeBuildingSize(0)).toEqual({ width: 0, depth: 0 });
    });

    it('layoutFloor emits one zone per team + meeting hub', () => {
        resetIdCounterForTests();
        const teams = [
            { id: newTeamId(), name: 'A' },
            { id: newTeamId(), name: 'B' },
            { id: newTeamId(), name: 'C' },
        ];
        const floor = layoutFloor(newFloorId(), 0, 0, teams);
        const teamZones = floor.zones.filter((z) => z.kind === 'team');
        expect(teamZones).toHaveLength(teams.length);
        const meetingZones = floor.zones.filter((z) => z.kind === 'meeting');
        expect(meetingZones).toHaveLength(1);
        const meetingSpaceCount = meetingZones[0]!.meetingSpaces.length;
        expect(meetingSpaceCount).toBeGreaterThanOrEqual(2);
    });

    it('every team zone carries a non-empty workstation array', () => {
        resetIdCounterForTests();
        const teams = [
            { id: newTeamId(), name: 'A' },
            { id: newTeamId(), name: 'B' },
        ];
        const floor = layoutFloor(newFloorId(), 0, 0, teams);
        for (const z of floor.zones.filter((z) => z.kind === 'team')) {
            expect(z.workstations.length).toBeGreaterThan(0);
        }
    });

    it('buildInitialBuilding floors[0].zones includes a meeting hub', () => {
        resetIdCounterForTests();
        const building = buildInitialBuilding([
            { id: newTeamId(), name: 'A' },
            { id: newTeamId(), name: 'B' },
        ]);
        expect(building.floors).toHaveLength(1);
        expect(building.floors[0]!.zones.some((z) => z.kind === 'meeting')).toBe(true);
    });

    it('findZoneByTeam returns the matching team zone', () => {
        resetIdCounterForTests();
        const t1 = newTeamId();
        const t2 = newTeamId();
        const building = buildInitialBuilding([
            { id: t1, name: 'A' },
            { id: t2, name: 'B' },
        ]);
        const zone = findZoneByTeam(building, t1);
        expect(zone).toBeDefined();
        expect(zone?.teamId).toBe(t1);
    });

    it('assignWorkstation finds the first empty slot and stamps occupant', () => {
        resetIdCounterForTests();
        const workstations = [
            { id: 'ws-1' as never, label: '1', anchor: { x: 0, z: 0 }, facing: 0, occupant: null },
            { id: 'ws-2' as never, label: '2', anchor: { x: 1, z: 0 }, facing: 0, occupant: null },
        ];
        const { next, assigned } = assignWorkstation(workstations, 'a' as never);
        expect(assigned?.id).toBe('ws-1');
        expect(next[0]?.occupant).toBe('a');
        expect(next[1]?.occupant).toBeNull();
    });

    it('assignWorkstation returns no assigned when all occupied', () => {
        const occupied = [
            { id: 'ws-1' as never, label: '1', anchor: { x: 0, z: 0 }, facing: 0, occupant: 'a' as never },
            { id: 'ws-2' as never, label: '2', anchor: { x: 1, z: 0 }, facing: 0, occupant: 'b' as never },
        ];
        const { assigned } = assignWorkstation(occupied, 'c' as never);
        expect(assigned).toBeUndefined();
    });

    it('initialAgentAnchor places agent at first workstation of their team zone', () => {
        resetIdCounterForTests();
        const team = newTeamId();
        const building = buildInitialBuilding([{ id: team, name: 'X' }]);
        const anchor = initialAgentAnchor(building, team);
        const zone = findZoneByTeam(building, team);
        expect(zone).toBeDefined();
        const firstWs = zone!.workstations[0]!;
        expect(anchor.workstationId).toBe(firstWs.id);
        expect(anchor.position.x).toBeCloseTo(firstWs.anchor.x);
    });

    it('ensureAgentAssigned mutates a copy of building and assigns id', () => {
        resetIdCounterForTests();
        const team = newTeamId();
        let building = buildInitialBuilding([{ id: team, name: 'X' }]);
        const agent: AgentEntity = {
            id: newAgentEntityId(),
            agentId: 'ag-1' as never,
            displayName: 'A',
            accent: { r: 1, g: 1, b: 1 },
            teamId: team,
            position: { x: 0, y: 0, z: 0 },
            atWorkstation: null,
            atMeeting: null,
            visualState: 'idle',
            nav: { moving: false, nextWaypoint: null, destination: null, facing: 0, lastUpdateMs: 0 },
            updatedAt: 0,
        };
        const { building: next, workstationId } = ensureAgentAssigned(building, agent);
        expect(workstationId).not.toBeNull();
        expect(next).not.toBe(building);
        building = next;
        const zone = findZoneByTeam(building, team)!;
        expect(zone.workstations.some((ws) => ws.occupant === agent.id)).toBe(true);
    });
});