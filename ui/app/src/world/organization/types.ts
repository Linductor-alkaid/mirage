/// Organization Layer — 类型定义。
///
/// 这里是 Mira 多 Agent 运行时 / 前端模拟器输出的语义快照，由 World Projector
/// 投影到 World Model。本模块不引用 Three.js。

import type {
    ActivityId,
    AgentId,
    CollaborationId,
    EventSource,
    LogicalTime,
    MeetingSpaceId,
    OrganizationId,
    RoleId,
    TaskId,
    TeamId,
} from '../model/types.js';

/** Agent 当前生命周期状态。 */
export type AgentLifecycleState = 'active' | 'idle' | 'offline' | 'hibernated';

/** Agent 当前 Activity 的语义类型（与 World Model 的 VisualState 解耦）。 */
export type ActivityKind =
    | 'idle'
    | 'working'
    | 'collaborating'
    | 'reviewing'
    | 'testing'
    | 'reporting'
    | 'waiting'
    | 'blocked';

export interface Agent {
    readonly id: AgentId;
    readonly displayName: string;
    readonly roleId: RoleId;
    /** 顶层 Team；允许嵌套但通过 parentTeamId 表达层级。 */
    readonly teamId: TeamId | null;
    readonly lifecycle: AgentLifecycleState;
    /** 当前 Activity（指向 ActivityStore 中的活动记录）。 */
    readonly currentActivityId: ActivityId | null;
    /** 最近一次任务 ID（便于 Projector 直接映射为 working 工作站）。 */
    readonly currentTaskId: TaskId | null;
    /** 视觉强调色（hex，例如 "#E8A33D"），由角色或 Team 解析得到。 */
    readonly accent: string;
    readonly joinedAt: LogicalTime;
}

export interface Role {
    readonly id: RoleId;
    readonly name: string;
    readonly description: string;
}

export interface Team {
    readonly id: TeamId;
    readonly name: string;
    readonly parentTeamId: TeamId | null;
    readonly productLine: string | null;
    readonly memberIds: ReadonlyArray<AgentId>;
    readonly createdAt: LogicalTime;
}

export type TaskStatus = 'pending' | 'active' | 'blocked' | 'completed' | 'cancelled' | 'failed';

export interface Task {
    readonly id: TaskId;
    readonly title: string;
    readonly goal: string;
    readonly status: TaskStatus;
    readonly assigneeAgentIds: ReadonlyArray<AgentId>;
    readonly parentTaskId: TaskId | null;
    readonly createdAt: LogicalTime;
    readonly startedAt: LogicalTime | null;
    readonly completedAt: LogicalTime | null;
}

export interface Activity {
    readonly id: ActivityId;
    readonly agentId: AgentId;
    readonly kind: ActivityKind;
    readonly taskId: TaskId | null;
    readonly collaborationId: CollaborationId | null;
    readonly startedAt: LogicalTime;
    readonly endedAt: LogicalTime | null;
}

export interface Collaboration {
    readonly id: CollaborationId;
    readonly participants: ReadonlyArray<AgentId>;
    readonly taskId: TaskId | null;
    readonly meetingSpaceId: MeetingSpaceId | null;
    readonly startedAt: LogicalTime;
    readonly endedAt: LogicalTime | null;
}

/** 顶层 Organization 快照（不可变）。 */
export interface OrganizationState {
    readonly id: OrganizationId;
    readonly name: string;
    readonly agents: Readonly<Record<string, Agent>>;
    readonly teams: Readonly<Record<string, Team>>;
    readonly roles: Readonly<Record<string, Role>>;
    readonly tasks: Readonly<Record<string, Task>>;
    readonly activities: Readonly<Record<string, Activity>>;
    readonly collaborations: Readonly<Record<string, Collaboration>>;
    readonly logicalTime: LogicalTime;
    readonly source: EventSource;
}