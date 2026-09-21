/// World Model — AgentEntity 与空间行为状态机。
///
/// 视觉状态机与 Mira Runtime 语义解耦：
/// - Organization Event 描述 Agent 在做什么（Activity 语义）；
/// - World Model 决定 Agent 在空间里如何呈现（Visual State）。
///
/// 这两层通过 `mapActivityToVisual` 投影（M5 落地），不在此处耦合。

import type {
    AgentEntityId,
    AgentId,
    LogicalTime,
    TeamId,
    Vec2,
    Vec3,
    WorkstationId,
    MeetingSpaceId,
} from './types.js';

/** 视觉状态机。有限集合，便于动画 blend 与渲染。 */
export type VisualState =
    | 'idle'
    | 'walking'
    | 'working'
    | 'collaborating'
    | 'waiting'
    | 'blocked'
    | 'reviewing'
    | 'testing'
    | 'reporting'
    | 'completed'
    | 'hibernated';

export interface NavState {
    /** 当前是否正在移动。 */
    readonly moving: boolean;
    /** 路径上的下一个 waypoint。 */
    readonly nextWaypoint: Vec2 | null;
    /** 目标位置（工作区坐标，y 始终为 0）。 */
    readonly destination: Vec2 | null;
    /** 朝向（弧度）。 */
    readonly facing: number;
    /** 上次前进时间（ms）。 */
    readonly lastUpdateMs: LogicalTime;
}

export interface AgentEntity {
    readonly id: AgentEntityId;
    /** 来自 Organization 的稳定 Agent ID（与 Mira Agent 一一对应）。 */
    readonly agentId: AgentId;
    readonly displayName: string;
    /** 视觉呈现：颜色 / avatar 描述（首期使用调色板）。 */
    readonly accent: { readonly r: number; readonly g: number; readonly b: number };
    /** 归属 Team（用于视觉分组）。 */
    readonly teamId: TeamId | null;
    /** 当前空间中的坐标（米，y 朝上）。 */
    readonly position: Vec3;
    /** 所属 Workstation（working 时一般为非空）。 */
    readonly atWorkstation: WorkstationId | null;
    /** 所属 MeetingSpace（collaborating 时一般为非空）。 */
    readonly atMeeting: MeetingSpaceId | null;
    readonly visualState: VisualState;
    readonly nav: NavState;
    /** 上次状态变更时间。 */
    readonly updatedAt: LogicalTime;
}

/** 不变量校验（M1 之后由 Projector 维护）。 */
export function isValidVisualState(state: string): state is VisualState {
    switch (state) {
        case 'idle':
        case 'walking':
        case 'working':
        case 'collaborating':
        case 'waiting':
        case 'blocked':
        case 'reviewing':
        case 'testing':
        case 'reporting':
        case 'completed':
        case 'hibernated':
            return true;
        default:
            return false;
    }
}