/// Organization Event 总集（additive，never closed）。
///
/// 每条事件都带：
/// - `at`：逻辑时间戳（ms，自世界起点）；
/// - `source`：来源（simulator / mirage / mock / replay）；
/// - 数据 payload。
///
/// Mira multi-agent Runtime 真实接入时，只需把上游事件转为本类型即可
/// 喂入 `OrganizationStore.apply`；Projector 与 Renderer 不感知来源。

import type {
    Activity,
    Agent,
    Collaboration,
    Role,
    Task,
    Team,
} from './types.js';
import type {
    ActivityId,
    AgentId,
    CollaborationId,
    EventSource,
    LogicalTime,
    OrganizationId,
    TaskId,
    TeamId,
} from '../model/types.js';

interface BaseEvent {
    readonly at: LogicalTime;
    readonly source: EventSource;
}

export interface OrganizationEventAgentCreated extends BaseEvent {
    readonly type: 'agent.created';
    readonly agent: Agent;
}

export interface OrganizationEventAgentRemoved extends BaseEvent {
    readonly type: 'agent.removed';
    readonly agentId: AgentId;
}

export interface OrganizationEventAgentLifecycleChanged extends BaseEvent {
    readonly type: 'agent.lifecycle_changed';
    readonly agentId: AgentId;
    readonly lifecycle: Agent['lifecycle'];
}

export interface OrganizationEventAgentActivityStarted extends BaseEvent {
    readonly type: 'agent.activity_started';
    readonly activity: Activity;
}

export interface OrganizationEventAgentActivityEnded extends BaseEvent {
    readonly type: 'agent.activity_ended';
    readonly agentId: AgentId;
    readonly activityId: ActivityId;
}

export interface OrganizationEventAgentAssigned extends BaseEvent {
    readonly type: 'agent.assigned';
    readonly agentId: AgentId;
    readonly taskId: TaskId;
}

export interface OrganizationEventTeamCreated extends BaseEvent {
    readonly type: 'organization.team_created';
    readonly team: Team;
}

export interface OrganizationEventTeamRemoved extends BaseEvent {
    readonly type: 'organization.team_removed';
    readonly teamId: TeamId;
}

export interface OrganizationEventMembershipChanged extends BaseEvent {
    readonly type: 'organization.membership_changed';
    readonly agentId: AgentId;
    readonly teamId: TeamId;
    readonly added: boolean;
}

export interface OrganizationEventTaskCreated extends BaseEvent {
    readonly type: 'task.created';
    readonly task: Task;
}

export interface OrganizationEventTaskAssigned extends BaseEvent {
    readonly type: 'task.assigned';
    readonly taskId: TaskId;
    readonly assigneeAgentIds: ReadonlyArray<AgentId>;
}

export interface OrganizationEventTaskStatusChanged extends BaseEvent {
    readonly type: 'task.status_changed';
    readonly taskId: TaskId;
    readonly status: Task['status'];
    readonly reason?: string;
}

export interface OrganizationEventCollaborationStarted extends BaseEvent {
    readonly type: 'collaboration.started';
    readonly collaboration: Collaboration;
}

export interface OrganizationEventCollaborationEnded extends BaseEvent {
    readonly type: 'collaboration.ended';
    readonly collaborationId: CollaborationId;
}

export interface OrganizationEventRoleCreated extends BaseEvent {
    readonly type: 'organization.role_created';
    readonly role: Role;
}

export interface OrganizationEventSnapshotSync extends BaseEvent {
    readonly type: 'organization.snapshot_sync';
    readonly organizationId: OrganizationId;
    readonly agents: Readonly<Record<string, Agent>>;
    readonly teams: Readonly<Record<string, Team>>;
    readonly roles: Readonly<Record<string, Role>>;
    readonly tasks: Readonly<Record<string, Task>>;
    readonly activities: Readonly<Record<string, Activity>>;
    readonly collaborations: Readonly<Record<string, Collaboration>>;
}

export type OrganizationEvent =
    | OrganizationEventAgentCreated
    | OrganizationEventAgentRemoved
    | OrganizationEventAgentLifecycleChanged
    | OrganizationEventAgentActivityStarted
    | OrganizationEventAgentActivityEnded
    | OrganizationEventAgentAssigned
    | OrganizationEventTeamCreated
    | OrganizationEventTeamRemoved
    | OrganizationEventMembershipChanged
    | OrganizationEventTaskCreated
    | OrganizationEventTaskAssigned
    | OrganizationEventTaskStatusChanged
    | OrganizationEventCollaborationStarted
    | OrganizationEventCollaborationEnded
    | OrganizationEventRoleCreated
    | OrganizationEventSnapshotSync;

/** 事件名常量（用于订阅 / 日志过滤；与 type 字符串一致）。 */
export const ORGANIZATION_EVENT_TYPES: readonly OrganizationEvent['type'][] = [
    'agent.created',
    'agent.removed',
    'agent.lifecycle_changed',
    'agent.activity_started',
    'agent.activity_ended',
    'agent.assigned',
    'organization.team_created',
    'organization.team_removed',
    'organization.membership_changed',
    'organization.role_created',
    'organization.snapshot_sync',
    'task.created',
    'task.assigned',
    'task.status_changed',
    'collaboration.started',
    'collaboration.ended',
] as const;

export type OrganizationEventType = OrganizationEvent['type'];