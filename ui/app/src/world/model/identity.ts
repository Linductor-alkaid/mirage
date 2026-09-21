/// ID 生成（确定性 + 唯一）。
///
/// 在 Organization 模拟器、World Projector 与 Renderer 之间共用。
/// 设计要点：
/// - 全部 ID 都是字符串（brand 类型以保证字面量形态不变）；
/// - 不引入外部随机数源；确定性模拟器使用 `seededCounter`；
/// - 真实 multi-agent Runtime 接入后保留 `asWorldId` / `asAgentId` 等
///   适配函数，把上游 ID 安全地映射为 World Model 内部 brand。

import type {
    ActivityId,
    AgentEntityId,
    AgentId,
    BuildingId,
    CollaborationId,
    EventSource,
    FloorId,
    InteractiveObjectId,
    MeetingSpaceId,
    OrganizationId,
    RoleId,
    SessionId,
    TaskId,
    TeamId,
    WorkstationId,
    WorldId,
    ZoneId,
} from './types.js';

let counter = 0;

/** 仅测试用：重置 counter（vitest fixture）。 */
export function resetIdCounterForTests(): void {
    counter = 0;
}

/** 通用 newId（不限定 brand，仅内部生成）。 */
function newId(prefix: string): string {
    counter += 1;
    return `${prefix}-${counter.toString(36).padStart(4, '0')}`;
}

export const newWorldId = (): WorldId => newId('w') as WorldId;
export const newBuildingId = (): BuildingId => newId('b') as BuildingId;
export const newFloorId = (): FloorId => newId('f') as FloorId;
export const newZoneId = (): ZoneId => newId('z') as ZoneId;
export const newWorkstationId = (): WorkstationId => newId('ws') as WorkstationId;
export const newMeetingSpaceId = (): MeetingSpaceId => newId('mt') as MeetingSpaceId;
export const newInteractiveObjectId = (): InteractiveObjectId => newId('o') as InteractiveObjectId;
export const newAgentEntityId = (): AgentEntityId => newId('ae') as AgentEntityId;

export const newOrganizationId = (): OrganizationId => newId('org') as OrganizationId;
export const newTeamId = (): TeamId => newId('team') as TeamId;
export const newRoleId = (): RoleId => newId('role') as RoleId;
export const newAgentId = (): AgentId => newId('ag') as AgentId;
export const newTaskId = (): TaskId => newId('task') as TaskId;
export const newActivityId = (): ActivityId => newId('act') as ActivityId;
export const newCollaborationId = (): CollaborationId => newId('col') as CollaborationId;
export const newSessionId = (): SessionId => newId('s') as SessionId;

/** 把任意上游字符串适配为 brand ID（保持原字符串，不二次哈希以保证日志可读）。 */
export const asWorldId = (raw: string): WorldId => raw as WorldId;
export const asBuildingId = (raw: string): BuildingId => raw as BuildingId;
export const asFloorId = (raw: string): FloorId => raw as FloorId;
export const asZoneId = (raw: string): ZoneId => raw as ZoneId;
export const asWorkstationId = (raw: string): WorkstationId => raw as WorkstationId;
export const asMeetingSpaceId = (raw: string): MeetingSpaceId => raw as MeetingSpaceId;
export const asInteractiveObjectId = (raw: string): InteractiveObjectId => raw as InteractiveObjectId;
export const asAgentEntityId = (raw: string): AgentEntityId => raw as AgentEntityId;
export const asOrganizationId = (raw: string): OrganizationId => raw as OrganizationId;
export const asTeamId = (raw: string): TeamId => raw as TeamId;
export const asRoleId = (raw: string): RoleId => raw as RoleId;
export const asAgentId = (raw: string): AgentId => raw as AgentId;
export const asTaskId = (raw: string): TaskId => raw as TaskId;
export const asActivityId = (raw: string): ActivityId => raw as ActivityId;
export const asCollaborationId = (raw: string): CollaborationId => raw as CollaborationId;
export const asSessionId = (raw: string): SessionId => raw as SessionId;

export const EVENT_SOURCES: readonly EventSource[] = ['simulator', 'mirage', 'mock', 'replay'];