/// World Model — 共享标识符与基础代数类型。
///
/// 这些类型在 Organization / World / Renderer 之间流通，必须保持 0 依赖
/// （不允许 import Three.js 或 React）。

/** 稳定的字符串 ID；以字面量前缀表达类型，便于日志辨识。 */
export type Brand<K, T extends string> = K & { readonly __brand: T };

export type WorldId = Brand<string, 'WorldId'>;
export type BuildingId = Brand<string, 'BuildingId'>;
export type FloorId = Brand<string, 'FloorId'>;
export type ZoneId = Brand<string, 'ZoneId'>;
export type WorkstationId = Brand<string, 'WorkstationId'>;
export type MeetingSpaceId = Brand<string, 'MeetingSpaceId'>;
export type InteractiveObjectId = Brand<string, 'InteractiveObjectId'>;
export type AgentEntityId = Brand<string, 'AgentEntityId'>;

export type OrganizationId = Brand<string, 'OrganizationId'>;
export type TeamId = Brand<string, 'TeamId'>;
export type RoleId = Brand<string, 'RoleId'>;
export type AgentId = Brand<string, 'AgentId'>;
export type TaskId = Brand<string, 'TaskId'>;
export type ActivityId = Brand<string, 'ActivityId'>;
export type CollaborationId = Brand<string, 'CollaborationId'>;
export type SessionId = Brand<string, 'SessionId'>;

/** 三维向量（右手坐标系，y 朝上）。 */
export interface Vec3 {
    readonly x: number;
    readonly y: number;
    readonly z: number;
}

/** 二维向量（xz 平面布局用）。 */
export interface Vec2 {
    readonly x: number;
    readonly z: number;
}

/** 轴对齐包围盒。 */
export interface Aabb {
    readonly min: Vec3;
    readonly max: Vec3;
}

/** 一个逻辑时间戳；可以是毫秒时间，也可以是模拟步序号。 */
export type LogicalTime = number;

/** 事件来源标识：`simulator` 表示前端确定性模拟器，`mirage` 表示真实 multi-agent Runtime
 *  （未来接入），`mock` 保留以兼容 harness-mock 标记。 */
export type EventSource = 'simulator' | 'mirage' | 'mock' | 'replay';