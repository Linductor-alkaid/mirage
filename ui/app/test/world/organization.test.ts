/// Organization reducer + simulator 形状断言。

import { describe, expect, it } from 'vitest';

import {
    emptyOrganizationState,
    reduceOrganization,
} from '../../src/world/organization/reducer.js';
import {
    OrganizationSimulator,
} from '../../src/world/organization/simulator.js';
import { newOrganizationId } from '../../src/world/model/identity.js';
import type {
    Agent,
    OrganizationState,
    Team,
    Task,
} from '../../src/world/organization/types.js';
import type { OrganizationEvent } from '../../src/world/organization/events.js';

const now = 1_000;

function baseState(): OrganizationState {
    return emptyOrganizationState(newOrganizationId(), 'simulator', now);
}

describe('Organization reducer', () => {
    it('agent.created inserts the agent (idempotent on duplicate)', () => {
        const state = baseState();
        const agent: Agent = {
            id: 'ag-1' as never,
            displayName: 'A1',
            roleId: 'role-engineer' as never,
            teamId: null,
            lifecycle: 'active',
            currentActivityId: null,
            currentTaskId: null,
            accent: '#E8A33D',
            joinedAt: now,
        };
        const next = reduceOrganization(state, {
            type: 'agent.created',
            at: now + 10,
            source: 'simulator',
            agent,
        });
        expect(next.agents[agent.id]).toEqual(agent);
        // 幂等：再次 created 不应抛错且状态保持
        const again = reduceOrganization(next, {
            type: 'agent.created',
            at: now + 20,
            source: 'simulator',
            agent,
        });
        expect(again.agents[agent.id]).toEqual(agent);
        expect(again.logicalTime).toBe(now + 20);
    });

    it('agent.removed deletes the agent', () => {
        const state = baseState();
        const agent: Agent = {
            id: 'ag-1' as never,
            displayName: 'A1',
            roleId: 'role-engineer' as never,
            teamId: null,
            lifecycle: 'active',
            currentActivityId: null,
            currentTaskId: null,
            accent: '#000000',
            joinedAt: now,
        };
        let s = reduceOrganization(state, {
            type: 'agent.created',
            at: now,
            source: 'simulator',
            agent,
        });
        s = reduceOrganization(s, {
            type: 'agent.removed',
            at: now + 5,
            source: 'simulator',
            agentId: agent.id,
        });
        expect(s.agents[agent.id]).toBeUndefined();
    });

    it('team_created + membership_changed accumulates memberIds without dup', () => {
        let s = baseState();
        const team: Team = {
            id: 'team-1' as never,
            name: 'A',
            parentTeamId: null,
            productLine: null,
            memberIds: [],
            createdAt: now,
        };
        s = reduceOrganization(s, {
            type: 'organization.team_created',
            at: now,
            source: 'simulator',
            team,
        });
        s = reduceOrganization(s, {
            type: 'organization.membership_changed',
            at: now + 1,
            source: 'simulator',
            agentId: 'ag-1' as never,
            teamId: team.id,
            added: true,
        });
        s = reduceOrganization(s, {
            type: 'organization.membership_changed',
            at: now + 2,
            source: 'simulator',
            agentId: 'ag-1' as never,
            teamId: team.id,
            added: true,
        });
        expect(s.teams[team.id]?.memberIds).toEqual(['ag-1']);
    });

    it('task.assigned merges assignees uniquely', () => {
        let s = baseState();
        const task: Task = {
            id: 'task-1' as never,
            title: 'X',
            goal: 'X',
            status: 'active',
            assigneeAgentIds: ['ag-1' as never],
            parentTaskId: null,
            createdAt: now,
            startedAt: now,
            completedAt: null,
        };
        s = reduceOrganization(s, {
            type: 'task.created',
            at: now,
            source: 'simulator',
            task,
        });
        s = reduceOrganization(s, {
            type: 'task.assigned',
            at: now + 1,
            source: 'simulator',
            taskId: task.id,
            assigneeAgentIds: ['ag-1' as never, 'ag-2' as never],
        });
        expect(s.tasks[task.id]?.assigneeAgentIds).toEqual(['ag-1', 'ag-2']);
    });

    it('advances logical time even when the event is idempotent (mirrors world clock)', () => {
        let s = baseState();
        s = reduceOrganization(s, {
            type: 'organization.team_created',
            at: now + 100,
            source: 'simulator',
            team: {
                id: 't' as never,
                name: 'X',
                parentTeamId: null,
                productLine: null,
                memberIds: [],
                createdAt: now + 100,
            },
        });
        s = reduceOrganization(s, {
            type: 'organization.team_created',
            at: now + 10,
            source: 'simulator',
            team: {
                id: 't' as never,
                name: 'X',
                parentTeamId: null,
                productLine: null,
                memberIds: [],
                createdAt: now + 10,
            },
        });
        // logicalTime 跟随最新事件，幂等事件不丢时间戳
        expect(s.logicalTime).toBe(now + 10);
    });
});

describe('OrganizationSimulator', () => {
    it('seeds deterministic agents and tasks', () => {
        OrganizationSimulator.resetForTests();
        const orgId = newOrganizationId();
        // 先订阅，再构造；这样 seed 阶段的事件也会被记录
        const collector: OrganizationEvent[] = [];
        const sim = new OrganizationSimulator(orgId, { tickMs: 50, now: 0 });
        const unsub = sim.subscribe((e) => collector.push(e));
        sim.pause();
        const snap = collector.find((e) => e.type === 'organization.snapshot_sync');
        expect(snap).toBeDefined();
        const teamsInSnap = Object.keys((snap as { teams: Record<string, unknown> }).teams);
        const agentsInSnap = Object.keys((snap as { agents: Record<string, unknown> }).agents);
        const tasksInSnap = Object.keys((snap as { tasks: Record<string, unknown> }).tasks);
        expect(teamsInSnap).toHaveLength(4);
        // 28 = 2+2+4+2 + 1+4+2 + 5+1+1 + 3+1
        expect(agentsInSnap).toHaveLength(28);
        expect(tasksInSnap).toHaveLength(6);
        unsub();
        sim.dispose();
    });

    it('subscribe yields a snapshot_sync before any tick', () => {
        OrganizationSimulator.resetForTests();
        const sim = new OrganizationSimulator(newOrganizationId(), { tickMs: 100, now: 0 });
        const events: OrganizationEvent[] = [];
        const unsub = sim.subscribe((e) => events.push(e));
        expect(events[0]?.type).toBe('organization.snapshot_sync');
        sim.pause();
        unsub();
        sim.dispose();
    });
});