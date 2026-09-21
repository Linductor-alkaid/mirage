/// Collaboration connection lines — 协作关系的可视化。
///
/// 当 collaboration.started 事件进入 World Model 时（目前 M7 通过 collab 端点
/// 静态计算；M9 之后由 Projector 推导 collaborationEntity），Renderer 在
/// 三个参与 Agent 头顶画一个临时的「汇聚环」，表达「他们正在讨论」。
///
/// 首期实现：Renderer 维护一个 ConnectionLayer；每次 applyWorld 时根据 Agent
/// 之间的接近度（在同一 Meeting Space 内）动态生成连线。

import * as THREE from 'three';

import type { AgentEntity, MeetingSpace } from '../model/index.js';

interface ConnectionLine {
    readonly line: THREE.Line;
    readonly fromAgent: string;
    readonly toAgent: string;
}

const POINTS_PER_LINE = 2;

export class CollaborationLayer {
    private readonly group: THREE.Group;
    private readonly lines = new Map<string, ConnectionLine>();
    private disposed = false;

    constructor() {
        this.group = new THREE.Group();
        this.group.name = 'collaboration-layer';
    }

    getGroup(): THREE.Group {
        return this.group;
    }

    /** 重算：基于当前 Agent 位置 + MeetingSpace 归属，绘制在同一 meeting 内
     * 的 Agent 之间的连线。 */
    update(
        entities: Readonly<Record<string, AgentEntity>>,
        meetingSpaces: ReadonlyArray<{ meeting: MeetingSpace; entityIds: ReadonlyArray<string> }>,
    ): void {
        if (this.disposed) {
            return;
        }
        // 清空旧线
        for (const conn of this.lines.values()) {
            this.group.remove(conn.line);
            conn.line.geometry.dispose();
            (conn.line.material as THREE.Material).dispose();
        }
        this.lines.clear();

        for (const { meeting, entityIds } of meetingSpaces) {
            if (entityIds.length < 2) {
                continue;
            }
            // 同一 meeting space 内画一条 polyline（连接所有 members）
            const positions = new Float32Array(POINTS_PER_LINE * entityIds.length * 3);
            for (let i = 0; i < entityIds.length; i += 1) {
                const e = entities[entityIds[i]!];
                if (!e) {
                    continue;
                }
                const base = i * POINTS_PER_LINE * 3;
                // 起点：meeting space 中心稍下；终点：agent 头顶
                positions[base] = meeting.anchor.x;
                positions[base + 1] = 1.0;
                positions[base + 2] = meeting.anchor.z;
                positions[base + 3] = e.position.x;
                positions[base + 4] = 1.2;
                positions[base + 5] = e.position.z;
            }
            const geometry = new THREE.BufferGeometry();
            geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
            const material = new THREE.LineBasicMaterial({ color: 0x60a5fa, transparent: true, opacity: 0.6 });
            const line = new THREE.LineSegments(geometry, material);
            line.name = `collab-${meeting.id}`;
            this.group.add(line);
            this.lines.set(meeting.id as string, { line, fromAgent: entityIds[0]!, toAgent: entityIds[entityIds.length - 1]! });
        }
    }

    dispose(): void {
        if (this.disposed) {
            return;
        }
        this.disposed = true;
        for (const conn of this.lines.values()) {
            this.group.remove(conn.line);
            conn.line.geometry.dispose();
            (conn.line.material as THREE.Material).dispose();
        }
        this.lines.clear();
    }
}