/// World Renderer shared types.
///
/// 渲染层面对外暴露的最小接口集合。World Model 与 Projector 仅依赖此处的
/// 类型抽象，不会直接访问 Three.js object，避免与具体渲染实现耦合。

import type { AgentId, WorldId } from '../model/types.js';

/** 一个 3D 向量（右手坐标系；y 轴朝上）。 */
export interface Vec3 {
    readonly x: number;
    readonly y: number;
    readonly z: number;
}

/** 二维向量，主要用于平面布局计算。 */
export interface Vec2 {
    readonly x: number;
    readonly z: number;
}

/** RGBA 颜色（0–1 范围）。 */
export interface Color4 {
    readonly r: number;
    readonly g: number;
    readonly b: number;
    readonly a: number;
}

/** World Renderer 在 React 层对外的最小接口；不允许直接暴露 Three.js 对象。 */
export interface WorldRenderApi {
    readonly worldId: WorldId;
    /** 帧率（FPS）；用于状态栏 / HUD 显示。 */
    readonly fps: number;
    /** 已加载的 Agent 数量。 */
    readonly agentCount: number;
    /** 是否正在动画中（用于 React 决定是否订阅。 */
    readonly running: boolean;
}

/** WorldRenderer 监听的事件类型：选择 / hover / camera focus。 */
export type WorldInteractionKind = 'agent-selected' | 'agent-hover' | 'focus-agent' | 'focus-overview';

/** UI → Renderer 的输入命令；与 Three.js 内部状态解耦。 */
export interface WorldInputCommand {
    readonly kind: 'select-agent' | 'hover-agent' | 'focus-agent' | 'focus-overview' | 'dispose';
    readonly agentId?: AgentId;
}

/** Renderer 订阅的渲染 tick 回调（仅返回 elapsed 时间，不要求 dt）。 */
export type RenderTick = (elapsedMs: number) => void;

/** 主题适配器契约（首期只交付默认主题）；返回当前主题的色板与材质预设。 */
export interface ThemeAdapter {
    readonly id: string;
    readonly label: string;
    readonly background: Color4;
    readonly ambient: Color4;
    readonly directional: Color4;
    readonly ground: Color4;
    readonly agentBody: Color4;
    readonly agentAccent: Color4;
    readonly workstation: Color4;
    readonly meeting: Color4;
    readonly corridor: Color4;
    /** 主题对阴影 / 雾效等的选择。 */
    readonly fogEnabled: boolean;
}

/** 默认主题：现代办公室（首期主题）。 */
export const DEFAULT_THEME: ThemeAdapter = {
    id: 'modern-office',
    label: 'Modern Office',
    background: { r: 0.078, g: 0.098, b: 0.125, a: 1 },
    ambient: { r: 0.42, g: 0.46, b: 0.52, a: 1 },
    directional: { r: 1.0, g: 0.96, b: 0.84, a: 1 },
    ground: { r: 0.16, g: 0.18, b: 0.22, a: 1 },
    agentBody: { r: 0.88, g: 0.86, b: 0.78, a: 1 },
    agentAccent: { r: 0.91, g: 0.64, b: 0.24, a: 1 },
    workstation: { r: 0.21, g: 0.26, b: 0.31, a: 1 },
    meeting: { r: 0.27, g: 0.34, b: 0.41, a: 1 },
    corridor: { r: 0.18, g: 0.21, b: 0.26, a: 1 },
    fogEnabled: false,
};