/// World Model — 完整空间语义快照。
///
/// 设计原则：
/// - 纯数据，无副作用，不引用 Three.js；
/// - 通过 `cloneWorld` / `applyWorldDelta` 维持 immutable 语义（React 友好）；
/// - 所有 ID 都是 brand string，方便日志辨识。
///
/// 物理结构：
///
/// ```text
/// World
/// ├── building: Building
/// │     ├── id
/// │     ├── size: { width, depth }
/// │     └── floors: Floor[]
/// │           └── zones: Zone[]
/// │                 ├── kind: 'team' | 'corridor' | 'meeting' | 'common'
/// │                 ├── bounds: Aabb
/// │                 ├── workstations: Workstation[]
/// │                 └── meetingSpaces: MeetingSpace[]
/// ├── entities: Record<AgentEntityId, AgentEntity>
/// └── objects: Record<InteractiveObjectId, InteractiveObject>
/// ```

import type {
    Aabb,
    AgentEntity,
    AgentEntityId,
    Building,
    BuildingId,
    EventSource,
    InteractiveObject,
    InteractiveObjectId,
    LogicalTime,
    WorldId,
} from './index.js';
import type { OrganizationState } from '../organization/types.js';

export interface World {
    readonly id: WorldId;
    readonly createdAt: LogicalTime;
    /** 当前 latest known logical time（事件源推进）。 */
    readonly logicalTime: LogicalTime;
    /** 关联的 Organization（用于链路审计 / 日志，不参与渲染逻辑）。 */
    readonly organizationId: OrganizationState['id'];
    readonly building: Building;
    readonly entities: Readonly<Record<string, AgentEntity>>;
    readonly objects: Readonly<Record<string, InteractiveObject>>;
    /** 最近一次投影的来源（用于审计）。 */
    readonly lastSource: EventSource;
}

export interface WorldDelta {
    readonly at: LogicalTime;
    readonly source: EventSource;
    /** 任何字段保持 `undefined` 表示不变。 */
    readonly entities?: Readonly<Record<string, AgentEntity>>;
    readonly objects?: Readonly<Record<string, InteractiveObject>>;
    readonly building?: Building;
    /** 是否新增 / 替换 entity；Projector 用于幂等回放。 */
    readonly removedEntityIds?: readonly AgentEntityId[];
    readonly removedObjectIds?: readonly InteractiveObjectId[];
}

/** 空 World 工厂（M1 渲染骨架先用，后续 M2 接入完整字段）。 */
export function emptyWorld(now: LogicalTime, source: EventSource): World {
    return {
        id: 'w-root' as WorldId,
        createdAt: now,
        logicalTime: now,
        organizationId: 'org-placeholder' as World['organizationId'],
        building: {
            id: 'b-root' as BuildingId,
            size: { width: 40, depth: 30 },
            floors: [
                {
                    id: 'f-0' as Building['floors'][number]['id'],
                    index: 0,
                    height: 0,
                    zones: [],
                },
            ],
        },
        entities: {},
        objects: {},
        lastSource: source,
    };
}

/** 不可变更新：返回新 World，原对象不修改。 */
export function applyWorldDelta(prev: World, delta: WorldDelta): World {
    let entities = prev.entities;
    if (delta.entities !== undefined || delta.removedEntityIds !== undefined) {
        const next = { ...entities };
        if (delta.entities) {
            for (const [id, entity] of Object.entries(delta.entities)) {
                next[id] = entity;
            }
        }
        if (delta.removedEntityIds) {
            for (const id of delta.removedEntityIds) {
                delete next[id];
            }
        }
        entities = next;
    }
    let objects = prev.objects;
    if (delta.objects !== undefined || delta.removedObjectIds !== undefined) {
        const next = { ...objects };
        if (delta.objects) {
            for (const [id, obj] of Object.entries(delta.objects)) {
                next[id] = obj;
            }
        }
        if (delta.removedObjectIds) {
            for (const id of delta.removedObjectIds) {
                delete next[id];
            }
        }
        objects = next;
    }
    return {
        ...prev,
        logicalTime: delta.at,
        lastSource: delta.source,
        building: delta.building ?? prev.building,
        entities,
        objects,
    };
}

/** 用于 React：把任意 plain 值标记为「World 引用变更」。 */
export function isWorldShallowEqual(a: World, c: World): boolean {
    return a === c;
}

/** 调试用：Aabb→string。 */
export function aabbToString(box: Aabb): string {
    return `[${box.min.x.toFixed(2)},${box.min.y.toFixed(2)},${box.min.z.toFixed(2)}]→[${box.max.x.toFixed(2)},${box.max.y.toFixed(2)},${box.max.z.toFixed(2)}]`;
}