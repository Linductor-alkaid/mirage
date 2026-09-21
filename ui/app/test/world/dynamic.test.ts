/// M9 — 动态组织变化：projector 处理 team_created / team_removed / membership_changed。

import { describe, expect, it } from 'vitest';

import {
    WorldProjector,
    buildInitialBuilding,
} from '../../src/world/projector/projector.js';
import { buildInitialBuilding as buildBuilding } from '../../src/world/projector/layout.js';
import { newOrganizationId, resetIdCounterForTests } from '../../src/world/model/identity.js';
import type { Agent, OrganizationState, Team } from '../../src/world/organization/types.js';

function seedAgent(id: string, teamId: string): Agent {
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

function baseOrg(teams: ReadonlyArray<Team>, agents: ReadonlyArray<Agent>): OrganizationState {
    return {
        id: newOrganizationId(),
        name: 'X',
        agents: Object.fromEntries(agents.map((a) => [a.id, a])),
        teams: Object.fromEntries(teams.map((t) => [t.id, t])),
        roles: {},
        tasks: {},
        activities: {},
        collaborations: {},
        logicalTime: 0,
        source: 'simulator',
    };
}

describe('M9 dynamic organization', () => {
    it('project(team_created) rebuilds building and adds a zone', () => {
        resetIdCounterForTests();
        const t1 = 'team-A' as never;
        const building = buildBuilding([{ id: t1, name: 'A' }]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
            organizationTeams: {},
        });
        // 注入新 team
        const newTeam: Team = {
            id: 'team-B' as never,
            name: 'B',
            parentTeamId: null,
            productLine: 'product-b',
            memberIds: [],
            createdAt: 0,
        };
        (projector as unknown as { opts: { organizationTeams: Record<string, Team> } }).opts.organizationTeams = {
            [t1]: { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
            [newTeam.id]: newTeam,
        };
        const changed = projector.project({
            type: 'organization.team_created',
            at: 100,
            source: 'simulator',
            team: newTeam,
        });
        expect(changed).toBe(true);
        const w = projector.getWorld();
        const teamZones = w.building.floors[0]!.zones.filter((z) => z.kind === 'team');
        expect(teamZones.length).toBeGreaterThanOrEqual(2);
    });

    it('project(team_removed) drops the zone', () => {
        resetIdCounterForTests();
        const t1 = 'team-A' as never;
        const t2 = 'team-B' as never;
        const building = buildBuilding([
            { id: t1, name: 'A' },
            { id: t2, name: 'B' },
        ]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
            organizationTeams: {
                [t1]: { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
                [t2]: { id: t2, name: 'B', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
            },
        });
        const before = projector.getWorld().building.floors[0]!.zones.filter((z) => z.kind === 'team').length;
        // 移除 t2 时更新 opts
        (projector as unknown as { opts: { organizationTeams: Record<string, Team> } }).opts.organizationTeams = {
            [t1]: { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
        };
        projector.project({
            type: 'organization.team_removed',
            at: 100,
            source: 'simulator',
            teamId: t2,
        });
        const after = projector.getWorld().building.floors[0]!.zones.filter((z) => z.kind === 'team').length;
        expect(after).toBe(before - 1);
    });

    it('project(membership_changed added) re-binds agent to a new team zone', () => {
        resetIdCounterForTests();
        const t1 = 'team-A' as never;
        const t2 = 'team-B' as never;
        const building = buildBuilding([
            { id: t1, name: 'A' },
            { id: t2, name: 'B' },
        ]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
            organizationTeams: {
                [t1]: { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
                [t2]: { id: t2, name: 'B', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
            },
        });
        const org = baseOrg(
            [
                { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
                { id: t2, name: 'B', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
            ],
            [seedAgent('ag-1', t1)],
        );
        projector.resyncFromOrganization(org);
        const beforeEntity = Object.values(projector.getWorld().entities)[0]!;
        const changed = projector.project({
            type: 'organization.membership_changed',
            at: 100,
            source: 'simulator',
            agentId: 'ag-1' as never,
            teamId: t2,
            added: true,
        });
        expect(changed).toBe(true);
        const after = Object.values(projector.getWorld().entities)[0]!;
        expect(after.teamId).toBe(t2);
        expect(after.teamId).not.toBe(beforeEntity.teamId);
        expect(after.nav.moving).toBe(true);
    });

    it('project(membership_changed removed) clears team binding', () => {
        resetIdCounterForTests();
        const t1 = 'team-A' as never;
        const building = buildBuilding([{ id: t1, name: 'A' }]);
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
            organizationTeams: {
                [t1]: { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
            },
        });
        projector.resyncFromOrganization(
            baseOrg(
                [
                    { id: t1, name: 'A', parentTeamId: null, productLine: null, memberIds: [], createdAt: 0 },
                ],
                [seedAgent('ag-1', t1)],
            ),
        );
        const changed = projector.project({
            type: 'organization.membership_changed',
            at: 100,
            source: 'simulator',
            agentId: 'ag-1' as never,
            teamId: t1,
            added: false,
        });
        expect(changed).toBe(true);
        const after = Object.values(projector.getWorld().entities)[0]!;
        expect(after.teamId).toBeNull();
        expect(after.atWorkstation).toBeNull();
    });

    it('buildInitialBuilding produces valid building (smoke)', () => {
        // re-export sanity
        const b = buildBuilding([{ id: 'team-A' as never, name: 'A' }]);
        expect(b.floors.length).toBeGreaterThan(0);
    });
});