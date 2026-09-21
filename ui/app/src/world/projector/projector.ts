/// World Projector — 把 Organization 事件序列投影为 World Model 增量。
///
/// 单向 reducer：`project(World, OrganizationEvent[]) → WorldDelta`。
/// 投影规则（首期）：
///
/// - `agent.created`              → 构造 AgentEntity，初始 idle，分配 workstation
/// - `agent.removed`              → 移除 entity，释放 workstation
/// - `agent.lifecycle_changed`    → 视觉状态切换（hibernated / offline / active）
/// - `agent.activity_started`     → 根据 activity kind 选择路径：
///      - working + task → 走到 task 对应的 workstation 区域；
///      - collaborating + collaboration → 走到 meeting space；
///      - idle / waiting → 回到原位置；
///      - blocked → 显示 warning，停留。
/// - `agent.activity_ended`       → 视觉状态回到 idle，释放路径点。
/// - `agent.assigned`             → 不直接驱动视觉（M5 之后 task 进度才投影）。
/// - `task.created` / `task.assigned` / `task.status_changed` → 不直接改 entity；
/// - 仅供 Renderer 调取 metadata（badge / indicator）。
/// - `collaboration.started`      → 在 meeting space 形成 group（视觉：连线 + glow）。
/// - `collaboration.ended`        → group 解散。
/// - `organization.team_created`  → 重建 Building（简单粗暴；M9 引入 incremental
///   layout 时替换为增量 zone 插入）。
/// - `organization.team_removed`  → 重建 Building（释放 entity workstation）。
/// - `organization.membership_changed` → 把 entity 重定位到新 team zone。
/// - `organization.snapshot_sync` → 整张 World 重建（首期仅作为 fallback）。
///
/// 该 reducer 不引用 Three.js；只输出 WorldDelta。

import type {
    Aabb,
    AgentEntity,
    AgentEntityId,
    AgentId,
    Building,
    LogicalTime,
    MeetingSpace,
    MeetingSpaceId,
    Workstation,
    WorkstationId,
    Zone,
} from '../model/index.js';
import { newAgentEntityId } from '../model/identity.js';
import { hexToRgb } from './colors.js';
import {
    buildInitialBuilding,
    ensureAgentAssigned,
    findZoneByTeam,
    initialAgentAnchor,
} from './layout.js';
import type {
    Activity,
    Agent,
    OrganizationState,
    Team,
} from '../organization/types.js';
import type { OrganizationEvent } from '../organization/events.js';
import { applyWorldDelta, emptyWorld } from '../model/world.js';
import type { World, WorldDelta } from '../model/world.js';
import type { ActivityKind } from '../organization/types.js';
import type { VisualState } from '../model/index.js';

/** Projector 配置。 */
export interface ProjectorOptions {
    readonly organizationId: import('../model/types.js').OrganizationId;
    readonly logicalStart: LogicalTime;
    readonly source: import('../model/types.js').EventSource;
    readonly initialBuilding: Building;
}

export class WorldProjector {
    private world: World;
    private readonly opts: ProjectorOptions;

    constructor(opts: ProjectorOptions) {
        this.opts = opts;
        this.world = emptyWorld(opts.logicalStart, opts.source);
        this.world = {
            ...this.world,
            organizationId: opts.organizationId,
            building: opts.initialBuilding,
            logicalTime: opts.logicalStart,
        };
    }

    getWorld(): World {
        return this.world;
    }

    /** 把单条组织事件投影为 World 增量并应用；返回是否实际改动。 */
    project(event: OrganizationEvent): boolean {
        const delta = this.diff(event);
        if (delta === null) {
            return false;
        }
        this.world = applyWorldDelta(this.world, delta);
        return true;
    }

    /** 给定 organization 完整快照（含已应用的 events），重建 World。 */
    resyncFromOrganization(org: OrganizationState): void {
        // 始终从构造时的 initialBuilding 出发，避免 org 状态缺字段时把建筑清空；
        // 真实增量布局（M9 引入）会替换此处的简单策略。
        let building = this.opts.initialBuilding;
        const orgTeams = Object.values(org.teams);
        if (orgTeams.length > 0) {
            const teams = orgTeams.map((t) => ({ id: t.id, name: t.name }));
            // 仅在 initialBuilding 还没成型（空 zone）时用 org 团队重生成；
            // 否则保留初始建筑的几何，让 entity 自行匹配 zone。
            const hasZones = building.floors.some((f) => f.zones.length > 0);
            if (!hasZones) {
                building = buildInitialBuilding(teams);
            }
        }
        const entities: Record<string, AgentEntity> = {};
        // 为每个 Agent 分配 workstation 与位置
        for (const a of Object.values(org.agents)) {
            const entity = makeAgentEntity(a, org);
            const { building: b, workstationId } = ensureAgentAssigned(building, entity);
            building = b;
            const fallbackAnchor = initialAgentAnchor(building, a.teamId).position;
            const foundAnchor = workstationId ? findWorkstationAnchor(building, workstationId) : null;
            const anchor = foundAnchor ?? fallbackAnchor;
            const placed: AgentEntity = {
                ...entity,
                atWorkstation: workstationId,
                position: anchor,
                nav: {
                    moving: false,
                    nextWaypoint: null,
                    destination: null,
                    facing: 0,
                    lastUpdateMs: org.logicalTime,
                },
            };
            entities[entity.id] = placed;
        }
        this.world = {
            ...this.world,
            logicalTime: org.logicalTime,
            lastSource: org.source,
            building,
            entities,
            objects: {},
        };
    }

    /** 单事件 → 增量（不应用）。返回 null 表示无可投影结果。 */
    private diff(event: OrganizationEvent): WorldDelta | null {
        switch (event.type) {
            case 'agent.created':
                return this.diffAgentCreated(event.agent, event.at);
            case 'agent.removed':
                return this.diffAgentRemoved(event.agentId, event.at);
            case 'agent.lifecycle_changed':
                return this.diffAgentLifecycle(event.agentId, event.lifecycle, event.at);
            case 'agent.activity_started':
                return this.diffActivityStarted(event.activity, event.at);
            case 'agent.activity_ended':
                return this.diffActivityEnded(event.agentId, event.activityId, event.at);
            case 'organization.team_created':
            case 'organization.team_removed':
            case 'organization.membership_changed':
                return null; // 当前由 resyncFromOrganization 统一处理；M9 引入增量。
            case 'organization.role_created':
            case 'task.created':
            case 'task.assigned':
            case 'task.status_changed':
            case 'collaboration.started':
            case 'collaboration.ended':
                return null; // 由 renderer 通过 organization metadata 自行消费。
            case 'organization.snapshot_sync':
                return null;
            default:
                return null;
        }
    }

    private diffAgentCreated(agent: Agent, at: LogicalTime): WorldDelta {
        const entityId = newAgentEntityId();
        const entity: AgentEntity = {
            id: entityId,
            agentId: agent.id,
            displayName: agent.displayName,
            accent: hexToRgb(agent.accent),
            teamId: agent.teamId,
            position: initialAgentAnchor(this.world.building, agent.teamId).position,
            atWorkstation: null,
            atMeeting: null,
            visualState: 'idle',
            nav: {
                moving: false,
                nextWaypoint: null,
                destination: null,
                facing: 0,
                lastUpdateMs: at,
            },
            updatedAt: at,
        };
        const { building, workstationId } = ensureAgentAssigned(this.world.building, entity);
        const placed: AgentEntity = workstationId
            ? {
                ...entity,
                atWorkstation: workstationId,
                position: findWorkstationAnchor(building, workstationId) ?? entity.position,
            }
            : entity;
        this.world = { ...this.world, building };
        return {
            at,
            source: this.opts.source,
            entities: { [placed.id]: placed },
        };
    }

    private diffAgentRemoved(agentId: AgentId, at: LogicalTime): WorldDelta | null {
        const target = Object.values(this.world.entities).find((e) => e.agentId === agentId);
        if (!target) {
            return null;
        }
        return {
            at,
            source: this.opts.source,
            removedEntityIds: [target.id],
        };
    }

    private diffAgentLifecycle(
        agentId: AgentId,
        lifecycle: Agent['lifecycle'],
        at: LogicalTime,
    ): WorldDelta | null {
        const target = Object.values(this.world.entities).find((e) => e.agentId === agentId);
        if (!target) {
            return null;
        }
        const visual: VisualState = lifecycle === 'hibernated' || lifecycle === 'offline' ? 'hibernated' : target.visualState;
        const next: AgentEntity = { ...target, visualState: visual, updatedAt: at };
        return {
            at,
            source: this.opts.source,
            entities: { [next.id]: next },
        };
    }

    private diffActivityStarted(activity: Activity, at: LogicalTime): WorldDelta | null {
        const target = Object.values(this.world.entities).find((e) => e.agentId === activity.agentId);
        if (!target) {
            return null;
        }
        const visual = mapActivityKindToVisual(activity.kind);
        let position = target.position;
        let atWorkstation: WorkstationId | null = target.atWorkstation;
        let atMeeting: MeetingSpaceId | null = target.atMeeting;
        let destination: { x: number; z: number } | null = null;

        if (visual === 'working' && activity.taskId) {
            // 走到所属 team zone 的第一个空 workstation
            const teamZone = target.teamId ? findZoneByTeam(this.world.building, target.teamId) : undefined;
            const candidate = teamZone?.workstations.find((ws) => ws.occupant === null) ?? teamZone?.workstations[0];
            if (candidate) {
                destination = { x: candidate.anchor.x, z: candidate.anchor.z };
                atWorkstation = candidate.id;
                position = { x: candidate.anchor.x, y: 0, z: candidate.anchor.z };
            }
        } else if (visual === 'collaborating') {
            // 走到最近的 meeting space
            const meetingSpace = findNearestMeetingSpace(this.world.building, target.position);
            if (meetingSpace) {
                destination = { x: meetingSpace.anchor.x, z: meetingSpace.anchor.z };
                atMeeting = meetingSpace.id;
                position = { x: meetingSpace.anchor.x, y: 0, z: meetingSpace.anchor.z };
            }
        } else {
            // idle / waiting / blocked / reviewing / testing / reporting：停留
            destination = null;
        }

        const next: AgentEntity = {
            ...target,
            visualState: visual,
            position,
            atWorkstation,
            atMeeting,
            nav: {
                moving: destination !== null,
                nextWaypoint: destination,
                destination,
                facing: target.nav.facing,
                lastUpdateMs: at,
            },
            updatedAt: at,
        };
        return {
            at,
            source: this.opts.source,
            entities: { [next.id]: next },
        };
    }

    private diffActivityEnded(agentId: AgentId, _activityId: import('../model/types.js').ActivityId, at: LogicalTime): WorldDelta | null {
        const target = Object.values(this.world.entities).find((e) => e.agentId === agentId);
        if (!target) {
            return null;
        }
        const next: AgentEntity = {
            ...target,
            visualState: 'idle',
            nav: {
                moving: false,
                nextWaypoint: null,
                destination: null,
                facing: target.nav.facing,
                lastUpdateMs: at,
            },
            updatedAt: at,
        };
        return {
            at,
            source: this.opts.source,
            entities: { [next.id]: next },
        };
    }
}

function makeAgentEntity(agent: Agent, _org: OrganizationState): AgentEntity {
    return {
        id: newAgentEntityId(),
        agentId: agent.id,
        displayName: agent.displayName,
        accent: hexToRgb(agent.accent),
        teamId: agent.teamId,
        position: { x: 0, y: 0, z: 0 },
        atWorkstation: null,
        atMeeting: null,
        visualState: 'idle',
        nav: {
            moving: false,
            nextWaypoint: null,
            destination: null,
            facing: 0,
            lastUpdateMs: _org.logicalTime,
        },
        updatedAt: _org.logicalTime,
    };
}

function findWorkstationAnchor(building: Building, workstationId: WorkstationId): { x: number; y: number; z: number } | null {
    for (const floor of building.floors) {
        for (const zone of floor.zones) {
            for (const ws of zone.workstations) {
                if (ws.id === workstationId) {
                    return { x: ws.anchor.x, y: 0, z: ws.anchor.z };
                }
            }
        }
    }
    return null;
}

function findNearestMeetingSpace(building: Building, position: { x: number; z: number }): MeetingSpace | undefined {
    let best: MeetingSpace | undefined;
    let bestDist = Number.POSITIVE_INFINITY;
    for (const floor of building.floors) {
        for (const zone of floor.zones) {
            for (const ms of zone.meetingSpaces) {
                const dx = ms.anchor.x - position.x;
                const dz = ms.anchor.z - position.z;
                const d2 = dx * dx + dz * dz;
                if (d2 < bestDist) {
                    best = ms;
                    bestDist = d2;
                }
            }
        }
    }
    return best;
}

/** Activity 语义 → 视觉状态机的映射。 */
export function mapActivityKindToVisual(kind: ActivityKind): VisualState {
    switch (kind) {
        case 'working':
            return 'working';
        case 'collaborating':
            return 'collaborating';
        case 'reviewing':
            return 'reviewing';
        case 'testing':
            return 'testing';
        case 'reporting':
            return 'reporting';
        case 'waiting':
            return 'waiting';
        case 'blocked':
            return 'blocked';
        case 'idle':
            return 'idle';
        default:
            return 'idle';
    }
}

/** 计算两点距离。 */
export function distance(a: { x: number; z: number }, b: { x: number; z: number }): number {
    const dx = a.x - b.x;
    const dz = a.z - b.z;
    return Math.hypot(dx, dz);
}

export type { Building, Zone, MeetingSpace, Workstation, AgentEntity, Aabb, World, WorldDelta };
export type { AgentEntityId, OrganizationState, OrganizationEvent, Team };
export type { Activity };