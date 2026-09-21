/// Status / activity badges — 在 Agent 头顶上方的小型 3D 标记。
///
/// 设计目标：
/// - 不引入复杂 Shader 或 font 资源；
/// - 使用纯几何 + 颜色表达状态；
/// - 跟随 Agent 位置（不存储在 AgentEntity 上，由 Renderer 维护）；
/// - 让用户「看一眼就知道 Agent 在做什么」。

import * as THREE from 'three';

import type { AgentEntity, VisualState } from '../model/index.js';

export interface BadgeBundle {
    readonly group: THREE.Group;
    setVisible(visible: boolean): void;
    setPosition(p: { x: number; y: number; z: number }): void;
    setVisualState(state: VisualState): void;
    setBlocked(severity: number): void;
    dispose(): void;
}

const BADGE_GEOMETRY = new THREE.RingGeometry(0.36, 0.46, 24);

/** 构造 / 复用 status badge。 */
export function createBadgeBundle(entity: AgentEntity): BadgeBundle {
    const group = new THREE.Group();
    const ring = new THREE.Mesh(
        BADGE_GEOMETRY,
        new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.9, side: THREE.DoubleSide }),
    );
    const inner = new THREE.Mesh(
        new THREE.CircleGeometry(0.32, 24),
        new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.55 }),
    );
    // Ring 默认在 xy 平面；旋转使其水平面对相机（billboard 由 Renderer 每帧处理）。
    ring.rotation.x = -Math.PI / 2;
    inner.rotation.x = -Math.PI / 2;
    group.add(ring, inner);
    group.userData = { agentEntityId: entity.id };
    group.visible = false; // 默认隐藏；只有当 visualState 不是 idle/working 时显著

    return {
        group,
        setVisible(visible) {
            group.visible = visible;
        },
        setPosition(p) {
            group.position.set(p.x, p.y, p.z);
        },
        setVisualState(state) {
            const ringMat = ring.material as THREE.MeshBasicMaterial;
            const innerMat = inner.material as THREE.MeshBasicMaterial;
            const color = statusColor(state);
            ringMat.color.set(color);
            innerMat.color.set(color);
        },
        setBlocked(severity) {
            const mat = ring.material as THREE.MeshBasicMaterial;
            mat.opacity = 0.5 + 0.4 * Math.min(1, severity);
        },
        dispose() {
            ring.geometry.dispose();
            inner.geometry.dispose();
            (ring.material as THREE.Material).dispose();
            (inner.material as THREE.Material).dispose();
        },
    };
}

export function statusColor(state: VisualState): number {
    switch (state) {
        case 'blocked':
            return 0xf87171;
        case 'waiting':
            return 0xfbbf24;
        case 'collaborating':
            return 0x60a5fa;
        case 'reviewing':
            return 0x7fb3d5;
        case 'testing':
            return 0x34d399;
        case 'reporting':
            return 0xa78bfa;
        case 'completed':
            return 0x86efac;
        case 'hibernated':
            return 0x6b7280;
        case 'working':
        case 'walking':
        case 'idle':
        default:
            return 0xeae5d8;
    }
}

/** 判断 badge 是否显著（默认 idle 时不显眼，blocked 强调可见）。 */
export function isBadgeNotable(state: VisualState): boolean {
    return state === 'blocked' || state === 'waiting' || state === 'collaborating' || state === 'reporting';
}