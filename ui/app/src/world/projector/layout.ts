/// Procedural layout：把组织结构映射为 World Model 的空间结构。
///
/// 规则（首期默认主题：现代办公室）：
/// - 1 个 Building，每 Building 单 Floor；
/// - 每个 Team 占用一个矩形 Zone，Zone 内布置 Workstation；
/// - Team Zone 围绕 Building 中央走廊环形排列；
/// - 公共 Zone（meeting + corridor）由 layout 注入；
/// - 协作 Meeting Space 数量 = max(2, ceil(teamCount / 2))。
///
/// 算法：纯函数；输入 Org 摘要（team 数 / agent 数），输出完整 Building。
/// 不依赖 Three.js。Three.js Renderer 拿到 Building 后构造 mesh。

import type {
    Aabb,
    AgentEntity,
    AgentEntityId,
    Building,
    BuildingId,
    Floor,
    FloorId,
    MeetingSpace,
    MeetingSpaceId,
    TeamId,
    Workstation,
    WorkstationId,
    Zone,
} from '../model/index.js';
import {
    newBuildingId,
    newFloorId,
    newMeetingSpaceId,
    newWorkstationId,
    newZoneId,
} from '../model/identity.js';
import { hexToRgb } from './colors.js';

export interface OrganizationLayoutInput {
    readonly teams: ReadonlyArray<{ id: TeamId; name: string }>;
    /** 站点布局参数。 */
    readonly options?: LayoutOptions;
}

export interface LayoutOptions {
    /** 每 Team Zone 的理想宽度（米）。 */
    readonly teamWidth?: number;
    /** 每 Team Zone 的理想深度（米）。 */
    readonly teamDepth?: number;
    /** 工作站间距（米）。 */
    readonly workstationSpacing?: number;
    /** 中央走廊宽度（米）。 */
    readonly corridorWidth?: number;
    /** 边缘 padding（米）。 */
    readonly padding?: number;
    /** Meeting Space 半径（米）。 */
    readonly meetingRadius?: number;
}

const DEFAULTS: Required<LayoutOptions> = {
    teamWidth: 8,
    teamDepth: 5,
    workstationSpacing: 1.6,
    corridorWidth: 2.5,
    padding: 1.5,
    meetingRadius: 0.9,
};

/** 计算 Building 占地。 */
export function computeBuildingSize(
    teamCount: number,
    options: LayoutOptions = {},
): { width: number; depth: number } {
    const opt = { ...DEFAULTS, ...options };
    if (teamCount <= 0) {
        return { width: 0, depth: 0 };
    }
    // 围绕走廊环形排布；按列分组，每列最多 4 个 Team。
    const teamsPerRow = Math.max(1, Math.ceil(Math.sqrt(teamCount * 2)));
    const cols = Math.ceil(teamCount / teamsPerRow);
    const rows = Math.ceil(teamCount / cols);
    const widthWithoutPadding = cols * opt.teamWidth + (cols + 1) * opt.corridorWidth;
    const depthWithoutPadding = rows * opt.teamDepth + (rows + 1) * opt.corridorWidth;
    return {
        width: widthWithoutPadding + 2 * opt.padding,
        depth: depthWithoutPadding + 2 * opt.padding,
    };
}

/** 生成 Floor + Zone + Workstation + MeetingSpace。 */
export function layoutFloor(
    floorId: FloorId,
    index: number,
    height: number,
    teams: ReadonlyArray<{ id: TeamId; name: string }>,
    options: LayoutOptions = {},
): Floor {
    const opt = { ...DEFAULTS, ...options };
    const teamsPerRow = Math.max(1, Math.ceil(Math.sqrt(teamCount(teams) * 2)));
    const cols = Math.max(1, Math.ceil(teamCount(teams) / teamsPerRow));
    const rows = Math.ceil(teamCount(teams) / cols);

    const zones: Zone[] = [];
    let teamIdx = 0;
    for (let r = 0; r < rows; r += 1) {
        for (let c = 0; c < cols; c += 1) {
            if (teamIdx >= teams.length) {
                break;
            }
            const team = teams[teamIdx]!;
            const x0 = opt.padding + opt.corridorWidth + c * (opt.teamWidth + opt.corridorWidth);
            const z0 = opt.padding + opt.corridorWidth + r * (opt.teamDepth + opt.corridorWidth);
            const zone: Zone = {
                id: newZoneId(),
                kind: 'team',
                label: team.name,
                bounds: aabb(x0, 0, z0, x0 + opt.teamWidth, 0, z0 + opt.teamDepth),
                workstations: layoutWorkstations(x0, z0, opt.teamWidth, opt.teamDepth, opt.workstationSpacing),
                meetingSpaces: [],
                teamId: team.id,
            };
            zones.push(zone);
            teamIdx += 1;
        }
    }

    // 中央 meeting zone
    const cx = opt.padding + opt.corridorWidth + (cols * (opt.teamWidth + opt.corridorWidth)) / 2 - opt.corridorWidth / 2;
    const cz = opt.padding + opt.corridorWidth + (rows * (opt.teamDepth + opt.corridorWidth)) / 2 - opt.corridorWidth / 2;
    const meetingCount = Math.max(2, Math.ceil(teams.length / 2));
    const meetingBounds = aabb(
        cx - opt.teamWidth * 0.6,
        0,
        cz - opt.teamDepth * 0.6,
        cx + opt.teamWidth * 0.6,
        0,
        cz + opt.teamDepth * 0.6,
    );
    const meetingZones: MeetingSpace[] = [];
    for (let i = 0; i < meetingCount; i += 1) {
        const angle = (i / meetingCount) * Math.PI * 2;
        const r = (opt.teamWidth + opt.teamDepth) * 0.18;
        const sx = cx + Math.cos(angle) * r;
        const sz = cz + Math.sin(angle) * r;
        const id = newMeetingSpaceId();
        meetingZones.push({
            id,
            label: `Meeting ${i + 1}`,
            anchor: { x: sx, z: sz },
            radius: opt.meetingRadius,
            capacity: 5,
            currentMembers: new Set(),
        });
    }
    zones.push({
        id: newZoneId(),
        kind: 'meeting',
        label: 'Meeting Hub',
        bounds: meetingBounds,
        workstations: [],
        meetingSpaces: meetingZones,
    });

    return {
        id: floorId,
        index,
        height,
        zones,
    };
}

function teamCount(teams: ReadonlyArray<unknown>): number {
    return teams.length;
}

function layoutWorkstations(
    x0: number,
    z0: number,
    width: number,
    depth: number,
    spacing: number,
): Workstation[] {
    const usable = width - spacing;
    const cols = Math.max(1, Math.floor(usable / spacing));
    const rows = Math.max(1, Math.floor((depth - spacing) / spacing));
    const items: Workstation[] = [];
    let i = 0;
    for (let r = 0; r < rows; r += 1) {
        for (let c = 0; c < cols; c += 1) {
            if (i >= cols * rows) {
                break;
            }
            const wx = x0 + spacing * (c + 0.5);
            const wz = z0 + spacing * (r + 0.5);
            const id = newWorkstationId();
            items.push({
                id,
                label: `WS-${(i + 1).toString().padStart(2, '0')}`,
                anchor: { x: wx, z: wz },
                facing: 0,
                occupant: null,
            });
            i += 1;
        }
    }
    return items;
}

function aabb(x0: number, y0: number, z0: number, x1: number, y1: number, z1: number): Aabb {
    return {
        min: { x: Math.min(x0, x1), y: Math.min(y0, y1), z: Math.min(z0, z1) },
        max: { x: Math.max(x0, x1), y: Math.max(y0, y1), z: Math.max(z0, z1) },
    };
}

/** 给定 Building 与 Org，构造初始 Building 数据。 */
export function buildInitialBuilding(
    teams: ReadonlyArray<{ id: TeamId; name: string }>,
    options: LayoutOptions = {},
): Building {
    const size = computeBuildingSize(teams.length, options);
    return {
        id: newBuildingId(),
        size,
        floors: [layoutFloor(newFloorId(), 0, 0, teams, options)],
    };
}

/** 找 Zone（按 TeamId）。 */
export function findZoneByTeam(building: Building, teamId: TeamId): Zone | undefined {
    for (const floor of building.floors) {
        for (const zone of floor.zones) {
            if (zone.kind === 'team' && zone.teamId === teamId) {
                return zone;
            }
        }
    }
    return undefined;
}

/** 在某 Zone 中找第一个空 workstation；返回新占用后的拷贝。 */
export function assignWorkstation(
    workstations: ReadonlyArray<Workstation>,
    agentId: AgentEntityId,
): { next: Workstation[]; assigned?: Workstation } {
    const next = workstations.map((ws) => ({ ...ws }));
    for (const slot of next) {
        if (slot.occupant === null) {
            slot.occupant = agentId;
            return { next, assigned: slot };
        }
    }
    return { next };
}

/** 给定 Agent 与 Building，返回初始锚点：所属 Team Zone 的第一个 workstation。 */
export function initialAgentAnchor(
    building: Building,
    teamId: TeamId | null,
): { position: { x: number; y: number; z: number }; workstationId: WorkstationId | null } {
    const zone = teamId ? findZoneByTeam(building, teamId) : undefined;
    if (zone && zone.workstations.length > 0) {
        const ws = zone.workstations[0]!;
        return {
            position: { x: ws.anchor.x, y: 0, z: ws.anchor.z },
            workstationId: ws.id,
        };
    }
    // 兜底：建筑中央
    return {
        position: { x: building.size.width / 2, y: 0, z: building.size.depth / 2 },
        workstationId: null,
    };
}

/** 把 Agent Entity 注册到空 workstation（已分配则忽略）。 */
export function ensureAgentAssigned(
    building: Building,
    agentEntity: AgentEntity,
): { building: Building; workstationId: WorkstationId | null } {
    if (agentEntity.atWorkstation) {
        return { building, workstationId: agentEntity.atWorkstation };
    }
    if (!agentEntity.teamId) {
        return { building, workstationId: null };
    }
    const teamZone = findZoneByTeam(building, agentEntity.teamId);
    if (!teamZone) {
        return { building, workstationId: null };
    }
    const { next, assigned } = assignWorkstation(teamZone.workstations, agentEntity.id);
    if (!assigned) {
        return { building, workstationId: null };
    }
    const firstFloor = building.floors[0];
    if (!firstFloor) {
        return { building, workstationId: null };
    }
    const newZones = firstFloor.zones.map((z) => (z.id === teamZone.id ? { ...z, workstations: next } : z));
    const newFloors = building.floors.map((f, i) => (i === 0 ? { ...f, zones: newZones } : f));
    return {
        building: { ...building, floors: newFloors },
        workstationId: assigned.id,
    };
}

export type { Workstation, WorkstationId, MeetingSpace, MeetingSpaceId, BuildingId };
export { hexToRgb };