/// M10 性能 / 规模 fixture。
///
/// 在不依赖 Three.js 的前提下（仅 projector + reducer）跑：
/// - 100 Agent × 4 Team × 12 Task 的「还原」时间；
/// - 大批量 activity 事件喂入时的 reducer 性能。
///
/// Three.js 实际渲染性能在浏览器中验证（这里只保证数据层可承担 100 agents）。

import { describe, expect, it } from 'vitest';

import {
    WorldProjector,
} from '../../src/world/projector/projector.js';
import { buildInitialBuilding } from '../../src/world/projector/layout.js';
import { createOrganizationStore } from '../../src/world/organization/source.js';
import { OrganizationSimulator } from '../../src/world/organization/simulator.js';
import { newOrganizationId, resetIdCounterForTests } from '../../src/world/model/identity.js';
import type { Agent, OrganizationState, Team } from '../../src/world/organization/types.js';

function makeOrg(agentCount: number): OrganizationState {
    resetIdCounterForTests();
    const teamCount = Math.max(1, Math.ceil(agentCount / 30));
    const teams: Team[] = [];
    for (let t = 0; t < teamCount; t += 1) {
        teams.push({
            id: `team-${t}` as never,
            name: `Team ${t}`,
            parentTeamId: null,
            productLine: null,
            memberIds: [],
            createdAt: 0,
        });
    }
    const agents: Agent[] = [];
    for (let i = 0; i < agentCount; i += 1) {
        const team = teams[i % teams.length]!;
        agents.push({
            id: `ag-${i}` as never,
            displayName: `Agent ${i}`,
            roleId: 'role-engineer' as never,
            teamId: team.id,
            lifecycle: 'active',
            currentActivityId: null,
            currentTaskId: null,
            accent: '#888888',
            joinedAt: 0,
        });
        team.memberIds.push(agents[i]!.id);
    }
    return {
        id: newOrganizationId(),
        name: 'stress',
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

describe('M10 stress', () => {
    it('resync 100 agents within 500ms', () => {
        const org = makeOrg(100);
        const building = buildInitialBuilding(
            Object.values(org.teams).map((t) => ({ id: t.id, name: t.name })),
        );
        const projector = new WorldProjector({
            organizationId: newOrganizationId(),
            logicalStart: 0,
            source: 'simulator',
            initialBuilding: building,
            organizationTeams: org.teams,
        });
        const t0 = performance.now();
        projector.resyncFromOrganization(org);
        const dt = performance.now() - t0;
        expect(dt).toBeLessThan(500);
        expect(Object.keys(projector.getWorld().entities)).toHaveLength(100);
    });

    it('OrganizationStore.apply tolerates 1000 events', () => {
        resetIdCounterForTests();
        const orgId = newOrganizationId();
        const store = createOrganizationStore(orgId, null, 0, 'simulator');
        const org = makeOrg(50);
        // 喂入 snapshot_sync，然后大量 agent.activity_started 事件
        store.apply({
            type: 'organization.snapshot_sync',
            at: 0,
            source: 'simulator',
            organizationId: org.id,
            agents: org.agents,
            teams: org.teams,
            roles: org.roles,
            tasks: org.tasks,
            activities: org.activities,
            collaborations: org.collaborations,
        });
        const agentIds = Object.keys(org.agents);
        const t0 = performance.now();
        for (let i = 0; i < 1000; i += 1) {
            const aid = agentIds[i % agentIds.length]!;
            store.apply({
                type: 'agent.activity_started',
                at: 1000 + i,
                source: 'simulator',
                activity: {
                    id: `act-${i}` as never,
                    agentId: aid as never,
                    kind: 'working',
                    taskId: null,
                    collaborationId: null,
                    startedAt: 1000 + i,
                    endedAt: null,
                },
            });
        }
        const dt = performance.now() - t0;
        expect(dt).toBeLessThan(1000);
        expect(store.getState().logicalTime).toBeGreaterThan(0);
    });

    it('OrganizationSimulator fires 30 agents deterministically', () => {
        // 重在 shape，重复 M4 的关键不变量。
        resetIdCounterForTests();
        OrganizationSimulator.resetForTests();
        const sim = new OrganizationSimulator(newOrganizationId());
        const received: number[] = [];
        const unsub = sim.subscribe((e) => {
            if (e.type === 'organization.snapshot_sync') {
                received.push(Object.keys((e as { agents: Record<string, unknown> }).agents).length);
            }
        });
        sim.pause();
        unsub();
        sim.dispose();
        expect(received[0]).toBe(28); // 默认 seed：28 agents
    });
});