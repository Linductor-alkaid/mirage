/// Organization Store — 单向 reducer。
///
/// OrganizationStore 是 Organization Layer 唯一状态容器：
/// - 不可变（immutable）状态 + 事件 reducer；
/// - 提供快照订阅 / 事件订阅 / 状态查询；
/// - 不感知 World Model / Three.js。
///
/// 模拟器、真实 Mira 事件源、未来 timeline 回放都通过 `apply(event)` 推入。

import type {
    Agent,
    Collaboration,
    OrganizationState,
    Task,
    Team,
} from './types.js';
import type {
    EventSource,
    LogicalTime,
    OrganizationId,
} from '../model/types.js';
import type { OrganizationEvent } from './events.js';

/** 初始空状态（仅占位，M4 模拟器覆写）。 */
export function emptyOrganizationState(
    id: OrganizationId,
    source: EventSource,
    now: LogicalTime,
): OrganizationState {
    return {
        id,
        name: 'uninitialized',
        agents: {},
        teams: {},
        roles: {},
        tasks: {},
        activities: {},
        collaborations: {},
        logicalTime: now,
        source,
    };
}

/** 单事件 reducer。 */
export function reduceOrganization(prev: OrganizationState, event: OrganizationEvent): OrganizationState {
    const at = event.at;
    const source = event.source;
    switch (event.type) {
        case 'agent.created': {
            if (prev.agents[event.agent.id] !== undefined) {
                return withTime(prev, at, source); // 幂等：仍推进 logicalTime
            }
            return withTime({ ...prev, agents: { ...prev.agents, [event.agent.id]: event.agent } }, at, source);
        }
        case 'agent.removed': {
            if (prev.agents[event.agentId] === undefined) {
                return withTime(prev, at, source);
            }
            const agents = { ...prev.agents };
            delete agents[event.agentId];
            return withTime({ ...prev, agents }, at, source);
        }
        case 'agent.lifecycle_changed': {
            const agent = prev.agents[event.agentId];
            if (agent === undefined) {
                return prev;
            }
            const next: Agent = { ...agent, lifecycle: event.lifecycle };
            return withTime(
                { ...prev, agents: { ...prev.agents, [agent.id]: next } },
                at,
                source,
            );
        }
        case 'agent.activity_started': {
            if (prev.activities[event.activity.id] !== undefined) {
                return withTime(prev, at, source);
            }
            const agent = prev.agents[event.activity.agentId];
            if (agent === undefined) {
                return withTime(prev, at, source);
            }
            const nextAgent: Agent = {
                ...agent,
                currentActivityId: event.activity.id,
                currentTaskId: event.activity.taskId ?? agent.currentTaskId,
            };
            return withTime(
                {
                    ...prev,
                    activities: { ...prev.activities, [event.activity.id]: event.activity },
                    agents: { ...prev.agents, [agent.id]: nextAgent },
                },
                at,
                source,
            );
        }
        case 'agent.activity_ended': {
            const activity = prev.activities[event.activityId];
            if (activity === undefined) {
                return withTime(prev, at, source);
            }
            const agent = prev.agents[event.agentId];
            const activities = { ...prev.activities };
            activities[event.activityId] = { ...activity, endedAt: at };
            if (agent !== undefined && agent.currentActivityId === event.activityId) {
                const nextAgent: Agent = { ...agent, currentActivityId: null };
                return withTime(
                    { ...prev, activities, agents: { ...prev.agents, [agent.id]: nextAgent } },
                    at,
                    source,
                );
            }
            return withTime({ ...prev, activities }, at, source);
        }
        case 'agent.assigned': {
            const task = prev.tasks[event.taskId];
            if (task === undefined) {
                return withTime(prev, at, source);
            }
            const next: Task = {
                ...task,
                assigneeAgentIds: uniquePush(task.assigneeAgentIds, event.agentId),
            };
            return withTime(
                { ...prev, tasks: { ...prev.tasks, [task.id]: next } },
                at,
                source,
            );
        }
        case 'organization.team_created': {
            if (prev.teams[event.team.id] !== undefined) {
                return withTime(prev, at, source);
            }
            return withTime(
                { ...prev, teams: { ...prev.teams, [event.team.id]: event.team } },
                at,
                source,
            );
        }
        case 'organization.team_removed': {
            if (prev.teams[event.teamId] === undefined) {
                return withTime(prev, at, source);
            }
            const teams = { ...prev.teams };
            delete teams[event.teamId];
            return withTime({ ...prev, teams }, at, source);
        }
        case 'organization.membership_changed': {
            const team = prev.teams[event.teamId];
            if (team === undefined) {
                return withTime(prev, at, source);
            }
            const next: Team = {
                ...team,
                memberIds: event.added
                    ? uniquePush(team.memberIds, event.agentId)
                    : removeOnce(team.memberIds, event.agentId),
            };
            return withTime(
                { ...prev, teams: { ...prev.teams, [team.id]: next } },
                at,
                source,
            );
        }
        case 'organization.role_created': {
            if (prev.roles[event.role.id] !== undefined) {
                return withTime(prev, at, source);
            }
            return withTime(
                { ...prev, roles: { ...prev.roles, [event.role.id]: event.role } },
                at,
                source,
            );
        }
        case 'organization.snapshot_sync': {
            return {
                ...prev,
                id: event.organizationId,
                agents: event.agents,
                teams: event.teams,
                roles: event.roles,
                tasks: event.tasks,
                activities: event.activities,
                collaborations: event.collaborations,
                logicalTime: at,
                source,
            };
        }
        case 'task.created': {
            if (prev.tasks[event.task.id] !== undefined) {
                return withTime(prev, at, source);
            }
            return withTime(
                { ...prev, tasks: { ...prev.tasks, [event.task.id]: event.task } },
                at,
                source,
            );
        }
        case 'task.assigned': {
            const task = prev.tasks[event.taskId];
            if (task === undefined) {
                return withTime(prev, at, source);
            }
            const merged: Task = {
                ...task,
                assigneeAgentIds: uniqueConcat(task.assigneeAgentIds, event.assigneeAgentIds),
            };
            return withTime(
                { ...prev, tasks: { ...prev.tasks, [task.id]: merged } },
                at,
                source,
            );
        }
        case 'task.status_changed': {
            const task = prev.tasks[event.taskId];
            if (task === undefined) {
                return withTime(prev, at, source);
            }
            const next: Task = {
                ...task,
                status: event.status,
                startedAt: event.status === 'active' && task.startedAt === null ? at : task.startedAt,
                completedAt:
                    (event.status === 'completed' || event.status === 'failed' || event.status === 'cancelled') &&
                    task.completedAt === null
                        ? at
                        : task.completedAt,
            };
            return withTime(
                { ...prev, tasks: { ...prev.tasks, [task.id]: next } },
                at,
                source,
            );
        }
        case 'collaboration.started': {
            if (prev.collaborations[event.collaboration.id] !== undefined) {
                return withTime(prev, at, source);
            }
            return withTime(
                {
                    ...prev,
                    collaborations: {
                        ...prev.collaborations,
                        [event.collaboration.id]: event.collaboration,
                    },
                },
                at,
                source,
            );
        }
        case 'collaboration.ended': {
            const col = prev.collaborations[event.collaborationId];
            if (col === undefined || col.endedAt !== null) {
                return withTime(prev, at, source);
            }
            const next: Collaboration = { ...col, endedAt: at };
            return withTime(
                {
                    ...prev,
                    collaborations: { ...prev.collaborations, [col.id]: next },
                },
                at,
                source,
            );
        }
    }
}

function withTime(
    state: OrganizationState,
    at: LogicalTime,
    source: EventSource,
): OrganizationState {
    // 始终推进 logicalTime = at（at 是事件逻辑时间），即使事件被幂等忽略也
    // 表示世界认知推进到此时间。
    return { ...state, logicalTime: at, source };
}

function uniquePush<T>(arr: ReadonlyArray<T>, v: T): ReadonlyArray<T> {
    return arr.includes(v) ? arr : [...arr, v];
}

function uniqueConcat<T>(a: ReadonlyArray<T>, b: ReadonlyArray<T>): ReadonlyArray<T> {
    const seen = new Set<T>(a);
    const out: T[] = [...a];
    for (const v of b) {
        if (!seen.has(v)) {
            seen.add(v);
            out.push(v);
        }
    }
    return out;
}

function removeOnce<T>(arr: ReadonlyArray<T>, v: T): ReadonlyArray<T> {
    const idx = arr.indexOf(v);
    if (idx < 0) {
        return arr;
    }
    return [...arr.slice(0, idx), ...arr.slice(idx + 1)];
}

export type {
    Agent,
    Collaboration,
    OrganizationState,
    Role,
    Task,
    Team,
} from './types.js';
export type {
    OrganizationEvent,
    OrganizationEventType,
} from './events.js';
export type { AgentId, CollaborationId, RoleId, TaskId, TeamId } from '../model/types.js';