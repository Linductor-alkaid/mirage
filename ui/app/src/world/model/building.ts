/// World Model — 空间结构（Building / Floor / Zone / Workstation / MeetingSpace）。
///
/// 所有几何与拓扑都是纯数据；不引用 Three.js。

import type {
    Aabb,
    AgentEntityId,
    BuildingId,
    FloorId,
    InteractiveObjectId,
    MeetingSpaceId,
    Vec2,
    WorkstationId,
    ZoneId,
} from './types.js';

export type ZoneKind = 'team' | 'corridor' | 'meeting' | 'common';

export interface Workstation {
    readonly id: WorkstationId;
    readonly label: string;
    /** 工位在工作区坐标系内的中心点（米）。 */
    readonly anchor: Vec2;
    /** 朝向（弧度，0 = +x 方向）。 */
    readonly facing: number;
    /** 当前占用者；空表示空闲。 */
    readonly occupant: AgentEntityId | null;
}

export interface MeetingSpace {
    readonly id: MeetingSpaceId;
    readonly label: string;
    readonly anchor: Vec2;
    readonly radius: number;
    readonly capacity: number;
    readonly currentMembers: ReadonlySet<AgentEntityId>;
}

export interface Zone {
    readonly id: ZoneId;
    readonly kind: ZoneKind;
    readonly label: string;
    /** Zone 在 floor 坐标系内的 AABB。 */
    readonly bounds: Aabb;
    readonly workstations: ReadonlyArray<Workstation>;
    readonly meetingSpaces: ReadonlyArray<MeetingSpace>;
    /** 关联的 Team ID（仅 kind === 'team' 时存在）。 */
    readonly teamId?: string;
}

export interface Floor {
    readonly id: FloorId;
    readonly index: number;
    /** 楼层相对地面的高度（米，0 = 地面层）。 */
    readonly height: number;
    readonly zones: ReadonlyArray<Zone>;
}

export interface Building {
    readonly id: BuildingId;
    /** 楼体占地（米），用于 ground 与可见范围。 */
    readonly size: { readonly width: number; readonly depth: number };
    readonly floors: ReadonlyArray<Floor>;
}

/** 物体：可交互对象（打印机、咖啡机、门等）。 */
export interface InteractiveObject {
    readonly id: InteractiveObjectId;
    readonly label: string;
    readonly kind: 'printer' | 'coffee' | 'door' | 'whiteboard' | 'terminal';
    readonly anchor: { readonly x: number; readonly y: number; readonly z: number };
}