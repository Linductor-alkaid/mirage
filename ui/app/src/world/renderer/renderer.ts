/// WorldRenderer — Three.js 渲染层。
///
/// 责任：
/// - 拥有 Scene / Camera / WebGLRenderer / OrbitCamera / Lighting；
/// - 订阅 World Model 增量，重建 Three.js Object3D 树（diff 策略）；
/// - 维护 AgentEntity → mesh 的映射（新增 / 删除 / 状态切换）；
/// - 提供 select / hover / focus 命令；
/// - 暴露 `dispose()` 严格释放资源。
///
/// 不引用 React / DOM 业务逻辑；canvas 容器由调用方提供。

import * as THREE from 'three';

import type { World } from '../model/world.js';
import type { AgentEntity, MeetingSpace, MeetingSpaceId } from '../model/index.js';
import type { ThemeAdapter } from './types.js';
import { DEFAULT_THEME } from './types.js';
import { OrbitCamera } from './orbit.js';
import { pickAgent } from './picking.js';
import { createBadgeBundle, isBadgeNotable, type BadgeBundle } from './badges.js';
import { CollaborationLayer } from './connections.js';

export interface WorldRendererOptions {
    readonly canvas: HTMLCanvasElement;
    readonly theme?: ThemeAdapter;
    readonly onSelect?: (agentId: string | null) => void;
    readonly onHover?: (agentId: string | null) => void;
    /** 视口像素大小；缺省时按 canvas 客户端尺寸。 */
    readonly size?: { width: number; height: number };
}

interface AgentMeshBundle {
    readonly root: THREE.Object3D;
    readonly body: THREE.Mesh;
    readonly accent: THREE.Mesh;
    readonly badge: BadgeBundle;
    /** 视觉状态对应的 body 颜色。 */
    setVisualState(state: AgentEntity['visualState']): void;
    setAccent(rgb: { r: number; g: number; b: number }): void;
    setPosition(p: { x: number; y: number; z: number }): void;
    dispose(): void;
}

export class WorldRenderer {
    private readonly opts: WorldRendererOptions;
    private readonly scene: THREE.Scene;
    private readonly camera: THREE.PerspectiveCamera;
    private readonly orbit: OrbitCamera;
    private readonly renderer: THREE.WebGLRenderer;
    private readonly theme: ThemeAdapter;
    private readonly agentBundles = new Map<string, AgentMeshBundle>();
    private readonly root: THREE.Group;
    private buildingRoot: THREE.Group;
    private readonly collaborationLayer: CollaborationLayer;
    private readonly collabTickAccumulator = { value: 0 };
    private rafId: number | null = null;
    private lastFrameMs = 0;
    private running = false;
    private disposed = false;
    private fps = 0;
    private frameAccumulator = 0;
    private frameCount = 0;
    private selectedAgentId: string | null = null;
    private hoveredAgentId: string | null = null;
    private currentWorld: World | null = null;
    private resizeObserver: ResizeObserver | null = null;
    private readonly selectListeners = new Set<(id: string | null) => void>();
    private readonly hoverListeners = new Set<(id: string | null) => void>();

    constructor(opts: WorldRendererOptions) {
        this.opts = opts;
        this.theme = opts.theme ?? DEFAULT_THEME;
        this.scene = new THREE.Scene();
        this.scene.background = colorToThree(this.theme.background);
        this.camera = new THREE.PerspectiveCamera(50, 1, 0.1, 500);
        this.orbit = new OrbitCamera(this.camera, opts.canvas, {
            target: new THREE.Vector3(0, 0, 0),
            initialDistance: 35,
        });
        this.renderer = new THREE.WebGLRenderer({
            canvas: opts.canvas,
            antialias: true,
            alpha: false,
            powerPreference: 'high-performance',
        });
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        this.renderer.outputColorSpace = THREE.SRGBColorSpace;
        this.root = new THREE.Group();
        this.scene.add(this.root);
        this.buildingRoot = new THREE.Group();
        this.root.add(this.buildingRoot);
        this.collaborationLayer = new CollaborationLayer();
        this.root.add(this.collaborationLayer.getGroup());
        this.installLighting();
        this.installResize();
        this.installPointer();
    }

    start(): void {
        if (this.running || this.disposed) {
            return;
        }
        this.running = true;
        this.lastFrameMs = performance.now();
        const loop = (now: number): void => {
            if (!this.running || this.disposed) {
                return;
            }
            const dt = now - this.lastFrameMs;
            this.lastFrameMs = now;
            this.frameAccumulator += dt;
            this.frameCount += 1;
            if (this.frameAccumulator > 500) {
                this.fps = (this.frameCount * 1000) / this.frameAccumulator;
                this.frameAccumulator = 0;
                this.frameCount = 0;
            }
            this.orbit.update();
            this.tickAgents(now, dt);
            this.renderer.render(this.scene, this.camera);
            this.rafId = requestAnimationFrame(loop);
        };
        this.rafId = requestAnimationFrame(loop);
    }

    dispose(): void {
        if (this.disposed) {
            return;
        }
        this.disposed = true;
        this.running = false;
        if (this.rafId !== null) {
            cancelAnimationFrame(this.rafId);
            this.rafId = null;
        }
        this.orbit.dispose();
        this.resizeObserver?.disconnect();
        this.resizeObserver = null;
        for (const bundle of this.agentBundles.values()) {
            bundle.dispose();
        }
        this.agentBundles.clear();
        this.collaborationLayer.dispose();
        this.buildingRoot.clear();
        this.root.clear();
        this.scene.clear();
        this.renderer.dispose();
    }

    getFps(): number {
        return Math.round(this.fps);
    }

    getAgentCount(): number {
        return this.agentBundles.size;
    }

    selectAgent(agentId: string | null): void {
        if (this.selectedAgentId === agentId) {
            return;
        }
        this.selectedAgentId = agentId;
        this.refreshSelectionVisuals();
        for (const l of this.selectListeners) {
            l(agentId);
        }
        this.opts.onSelect?.(agentId);
    }

    hoverAgent(agentId: string | null): void {
        if (this.hoveredAgentId === agentId) {
            return;
        }
        this.hoveredAgentId = agentId;
        this.refreshHoverVisuals();
        for (const l of this.hoverListeners) {
            l(agentId);
        }
        this.opts.onHover?.(agentId);
    }

    focusAgent(agentId: string): void {
        const bundle = this.agentBundles.get(agentId);
        if (!bundle) {
            return;
        }
        const p = bundle.root.position;
        this.orbit.focusAgent({ x: p.x, y: p.y, z: p.z });
    }

    focusOverview(): void {
        const center = this.currentWorld
            ? { x: this.currentWorld.building.size.width / 2, z: this.currentWorld.building.size.depth / 2 }
            : { x: 0, z: 0 };
        const radius = Math.max(this.currentWorld?.building.size.width ?? 30, this.currentWorld?.building.size.depth ?? 30);
        this.orbit.focusOverview(center, radius * 1.1);
    }

    /** 用一个完整的 World 快照做 diff 应用。 */
    applyWorld(world: World): void {
        this.currentWorld = world;
        this.rebuildBuilding(world);
        this.reconcileAgents(world);
        this.applyTransforms();
    }

    // ----- internal --------------------------------------------------------

    private installLighting(): void {
        const ambient = new THREE.AmbientLight(colorToThree(this.theme.ambient), 0.8);
        const directional = new THREE.DirectionalLight(colorToThree(this.theme.directional), 0.7);
        directional.position.set(20, 30, 15);
        this.scene.add(ambient, directional);
    }

    private installResize(): void {
        const apply = (): void => {
            const parent = this.opts.canvas.parentElement;
            const width = parent?.clientWidth ?? this.opts.canvas.clientWidth;
            const height = parent?.clientHeight ?? this.opts.canvas.clientHeight;
            if (width > 0 && height > 0) {
                this.renderer.setSize(width, height, false);
                this.camera.aspect = width / height;
                this.camera.updateProjectionMatrix();
            }
        };
        apply();
        this.resizeObserver = new ResizeObserver(apply);
        const parent = this.opts.canvas.parentElement;
        if (parent) {
            this.resizeObserver.observe(parent);
        }
    }

    private installPointer(): void {
        const canvas = this.opts.canvas;
        canvas.addEventListener('click', this.onCanvasClick);
        canvas.addEventListener('pointermove', this.onCanvasPointerMove);
    }

    private onCanvasClick = (e: MouseEvent): void => {
        const hit = this.raycast(e.clientX, e.clientY);
        this.selectAgent(hit);
    };

    private onCanvasPointerMove = (e: PointerEvent): void => {
        const hit = this.raycast(e.clientX, e.clientY);
        this.hoverAgent(hit);
    };

    /** 让 WorldCoordinator 暴露给 React：renderer 选中某个 AgentEntity 时回调。 */
    onSelect(listener: (agentEntityId: string | null) => void): () => void {
        this.selectListeners.add(listener);
        return () => {
            this.selectListeners.delete(listener);
        };
    }

    onHover(listener: (agentEntityId: string | null) => void): () => void {
        this.hoverListeners.add(listener);
        return () => {
            this.hoverListeners.delete(listener);
        };
    }

    private raycast(clientX: number, clientY: number): string | null {
        if (!this.currentWorld) {
            return null;
        }
        const rect = this.opts.canvas.getBoundingClientRect();
        const ndcX = ((clientX - rect.left) / rect.width) * 2 - 1;
        const ndcY = -((clientY - rect.top) / rect.height) * 2 + 1;
        return pickAgent({
            camera: this.camera,
            raycaster: new THREE.Raycaster(),
            bundles: this.agentBundles,
            ndcX,
            ndcY,
        });
    }

    private rebuildBuilding(world: World): void {
        // 简化策略：building 数量不大时全量重建；M11 再优化成 diff。
        this.buildingRoot.clear();
        const ground = new THREE.Mesh(
            new THREE.PlaneGeometry(world.building.size.width, world.building.size.depth),
            new THREE.MeshStandardMaterial({ color: colorToThree(this.theme.ground), roughness: 0.9 }),
        );
        ground.rotation.x = -Math.PI / 2;
        ground.position.set(world.building.size.width / 2, 0, world.building.size.depth / 2);
        this.buildingRoot.add(ground);

        for (const floor of world.building.floors) {
            for (const zone of floor.zones) {
                const w = zone.bounds.max.x - zone.bounds.min.x;
                const d = zone.bounds.max.z - zone.bounds.min.z;
                const centerX = (zone.bounds.max.x + zone.bounds.min.x) / 2;
                const centerZ = (zone.bounds.max.z + zone.bounds.min.z) / 2;
                let color: THREE.Color;
                if (zone.kind === 'team') {
                    color = colorToThree(this.theme.workstation);
                } else if (zone.kind === 'meeting') {
                    color = colorToThree(this.theme.meeting);
                } else {
                    color = colorToThree(this.theme.corridor);
                }
                const pad = new THREE.Mesh(
                    new THREE.PlaneGeometry(w, d),
                    new THREE.MeshStandardMaterial({ color, roughness: 0.85 }),
                );
                pad.rotation.x = -Math.PI / 2;
                pad.position.set(centerX, 0.01, centerZ);
                this.buildingRoot.add(pad);

                for (const ws of zone.workstations) {
                    const desk = new THREE.Mesh(
                        new THREE.BoxGeometry(1.0, 0.05, 0.6),
                        new THREE.MeshStandardMaterial({ color: 0x4a5560, roughness: 0.7 }),
                    );
                    desk.position.set(ws.anchor.x, 0.55, ws.anchor.z);
                    this.buildingRoot.add(desk);
                    const stand = new THREE.Mesh(
                        new THREE.CylinderGeometry(0.2, 0.2, 0.5),
                        new THREE.MeshStandardMaterial({ color: 0x3a4756, roughness: 0.5 }),
                    );
                    stand.position.set(ws.anchor.x, 0.275, ws.anchor.z);
                    this.buildingRoot.add(stand);
                }
                for (const ms of zone.meetingSpaces) {
                    const table = new THREE.Mesh(
                        new THREE.CylinderGeometry(ms.radius, ms.radius, 0.05, 24),
                        new THREE.MeshStandardMaterial({ color: 0x3a4756, roughness: 0.6 }),
                    );
                    table.position.set(ms.anchor.x, 0.5, ms.anchor.z);
                    this.buildingRoot.add(table);
                }
            }
        }
    }

    private reconcileAgents(world: World): void {
        const incoming = new Set(Object.keys(world.entities));
        // 删除：renderer 中有但 world 中没有
        for (const id of [...this.agentBundles.keys()]) {
            if (!incoming.has(id)) {
                this.agentBundles.get(id)?.dispose();
                this.agentBundles.delete(id);
            }
        }
        // 新增 / 更新
        for (const entity of Object.values(world.entities)) {
            let bundle = this.agentBundles.get(entity.id);
            if (!bundle) {
                bundle = this.createAgentBundle(entity);
                this.agentBundles.set(entity.id, bundle);
            }
            bundle.setAccent(entity.accent);
            bundle.setVisualState(entity.visualState);
            bundle.setPosition(entity.position);
        }
    }

    private createAgentBundle(entity: AgentEntity): AgentMeshBundle {
        const group = new THREE.Group();
        const body = new THREE.Mesh(
            new THREE.CapsuleGeometry(0.28, 0.5, 4, 8),
            new THREE.MeshStandardMaterial({ color: colorToThree(this.theme.agentBody), roughness: 0.6 }),
        );
        body.position.y = 0.65;
        const accent = new THREE.Mesh(
            new THREE.SphereGeometry(0.18, 16, 12),
            new THREE.MeshStandardMaterial({ color: 0xe8a33d, roughness: 0.4, emissive: 0x4a3a16, emissiveIntensity: 0.7 }),
        );
        accent.position.y = 1.25;
        group.add(body);
        group.add(accent);
        // 用户数据用于 picking
        body.userData = { agentEntityId: entity.id, agentId: entity.agentId };
        accent.userData = { agentEntityId: entity.id, agentId: entity.agentId };
        group.userData = { agentEntityId: entity.id, agentId: entity.agentId };
        // 头顶 status badge（M7）
        const badge = createBadgeBundle(entity);
        group.add(badge.group);
        this.root.add(group);

        return {
            root: group,
            body,
            accent,
            badge,
            setVisualState: (state) => {
                const color = visualStateColor(state, this.theme);
                const mat = body.material as THREE.MeshStandardMaterial;
                mat.color.copy(color);
                mat.emissive.copy(visualStateEmissive(state, this.theme));
                mat.emissiveIntensity = 0.35;
                badge.setVisualState(state);
                badge.setVisible(isBadgeNotable(state));
            },
            setAccent: (rgb) => {
                const mat = accent.material as THREE.MeshStandardMaterial;
                mat.color.setRGB(rgb.r, rgb.g, rgb.b);
                mat.emissive.setRGB(Math.min(1, rgb.r * 0.6), Math.min(1, rgb.g * 0.6), Math.min(1, rgb.b * 0.6));
            },
            setPosition: (p) => {
                group.position.set(p.x, p.y, p.z);
                badge.setPosition({ x: p.x, y: 1.6, z: p.z });
            },
            dispose: () => {
                this.root.remove(group);
                body.geometry.dispose();
                (body.material as THREE.Material).dispose();
                accent.geometry.dispose();
                (accent.material as THREE.Material).dispose();
                badge.dispose();
            },
        };
    }

    private applyTransforms(): void {
        for (const bundle of this.agentBundles.values()) {
            bundle.root.updateMatrixWorld();
        }
    }

    private tickAgents(now: number, dt: number): void {
        if (!this.currentWorld) {
            return;
        }
        // 位置插值（k 与 dt 相关；避免高速帧率时穿插）。
        const k = Math.min(1, dt / 220);
        const arrivalEpsilon = 0.08;
        for (const entity of Object.values(this.currentWorld.entities)) {
            const bundle = this.agentBundles.get(entity.id);
            if (!bundle) {
                continue;
            }
            const target = entity.position;
            const cur = bundle.root.position;
            cur.x += (target.x - cur.x) * k;
            cur.y += (target.y - cur.y) * k;
            cur.z += (target.z - cur.z) * k;
            // 简单的「主动走」状态机（M6）：当 Agent 在 nav.moving 但 target 与当前差异
            // 小于 epsilon 时，认为到达；视觉上拉高体位（轻微坐入 workstation）。
            const dx = target.x - cur.x;
            const dz = target.z - cur.z;
            const distance = Math.hypot(dx, dz);
            if (entity.nav.moving && distance < arrivalEpsilon) {
                // 到达
                cur.x = target.x;
                cur.z = target.z;
            }
            // 朝向：依据 movement direction；朝向用 lerp 接近目标。
            if (entity.nav.moving && entity.nav.destination) {
                const tdx = entity.nav.destination.x - cur.x;
                const tdz = entity.nav.destination.z - cur.z;
                if (Math.hypot(tdx, tdz) > 0.05) {
                    const targetYaw = Math.atan2(tdx, tdz);
                    bundle.root.rotation.y = lerpAngle(bundle.root.rotation.y, targetYaw, 0.18);
                }
            } else {
                // 不在移动时，让 Agent 缓慢回到 0 朝向（默认面朝 +z）。
                bundle.root.rotation.y = lerpAngle(bundle.root.rotation.y, 0, 0.05);
            }
            // body 高度根据状态调整：working/collaborating 时略微下沉（坐姿）；
            // walking 时略高（站立位移）。
            let bodyY = 0.65;
            if (entity.visualState === 'walking') {
                bodyY = 0.7 + 0.04 * Math.sin(now * 0.012);
            } else if (
                entity.visualState === 'working' ||
                entity.visualState === 'reviewing' ||
                entity.visualState === 'testing' ||
                entity.visualState === 'reporting' ||
                entity.visualState === 'collaborating'
            ) {
                bodyY = 0.55;
            } else if (entity.visualState === 'blocked') {
                bodyY = 0.65 + 0.06 * Math.sin(now * 0.015);
            }
            bundle.body.position.y = bodyY;
            bundle.accent.position.y = bodyY + 0.6;
            // accent 微脉冲（提示 working / blocked）
            const pulse = 1 + 0.08 * Math.sin(now * 0.005);
            const accentMesh = bundle.accent;
            const mat = accentMesh.material as THREE.MeshStandardMaterial;
            if (entity.visualState === 'blocked') {
                mat.emissiveIntensity = 0.9 + 0.4 * Math.sin(now * 0.012);
            } else if (entity.visualState === 'working' || entity.visualState === 'collaborating') {
                mat.emissiveIntensity = 0.7 * pulse;
            } else {
                mat.emissiveIntensity = 0.35;
            }
        }
        // M7：协作连线（在每 ~200ms 重算一次，避免每帧重建 BufferGeometry）
        this.collabTickAccumulator.value += dt;
        if (this.collabTickAccumulator.value > 200) {
            this.collabTickAccumulator.value = 0;
            this.recomputeCollaborationConnections();
        }
    }

    private recomputeCollaborationConnections(): void {
        if (!this.currentWorld) {
            return;
        }
        const entities = this.currentWorld.entities;
        // 按 atMeeting 分桶
        const meetingMembers = new Map<MeetingSpaceId, string[]>();
        for (const e of Object.values(entities)) {
            if (e.atMeeting) {
                const arr = meetingMembers.get(e.atMeeting) ?? [];
                arr.push(e.id);
                meetingMembers.set(e.atMeeting, arr);
            }
        }
        const meetingSpaces: { meeting: MeetingSpace; entityIds: ReadonlyArray<string> }[] = [];
        for (const floor of this.currentWorld.building.floors) {
            for (const zone of floor.zones) {
                for (const ms of zone.meetingSpaces) {
                    const ids = meetingMembers.get(ms.id);
                    if (ids && ids.length > 0) {
                        meetingSpaces.push({ meeting: ms, entityIds: ids });
                    }
                }
            }
        }
        this.collaborationLayer.update(entities, meetingSpaces);
    }

    private refreshSelectionVisuals(): void {
        for (const [id, bundle] of this.agentBundles) {
            const selected = id === this.selectedAgentId;
            const mat = bundle.body.material as THREE.MeshStandardMaterial;
            mat.emissiveIntensity = selected ? 0.9 : 0.35;
        }
    }

    private refreshHoverVisuals(): void {
        for (const [id, bundle] of this.agentBundles) {
            if (id === this.selectedAgentId) {
                continue;
            }
            const hovered = id === this.hoveredAgentId;
            const mat = bundle.body.material as THREE.MeshStandardMaterial;
            mat.emissiveIntensity = hovered ? 0.7 : 0.35;
        }
    }
}

function colorToThree(c: { r: number; g: number; b: number }): THREE.Color {
    return new THREE.Color(c.r, c.g, c.b);
}

function visualStateColor(state: AgentEntity['visualState'], theme: ThemeAdapter): THREE.Color {
    switch (state) {
        case 'blocked':
            return new THREE.Color(0xf87171);
        case 'waiting':
            return new THREE.Color(0xfbbf24);
        case 'collaborating':
            return new THREE.Color(0x60a5fa);
        case 'reporting':
            return new THREE.Color(0xa78bfa);
        case 'testing':
            return new THREE.Color(0x34d399);
        case 'reviewing':
            return new THREE.Color(0x7fb3d5);
        case 'working':
            return new THREE.Color(0xeae5d8);
        case 'completed':
            return new THREE.Color(0x86efac);
        case 'hibernated':
            return new THREE.Color(0x6b7280);
        case 'walking':
            return new THREE.Color(0xc7d2dd);
        case 'idle':
        default:
            return colorToThree(theme.agentBody);
    }
}

function visualStateEmissive(state: AgentEntity['visualState'], theme: ThemeAdapter): THREE.Color {
    switch (state) {
        case 'blocked':
            return new THREE.Color(0.6, 0.2, 0.2);
        case 'collaborating':
            return new THREE.Color(0.2, 0.4, 0.7);
        case 'waiting':
            return new THREE.Color(0.5, 0.4, 0.1);
        default:
            return new THREE.Color(theme.ambient.r * 0.2, theme.ambient.g * 0.2, theme.ambient.b * 0.2);
    }
}

function lerpAngle(a: number, b: number, t: number): number {
    const diff = ((b - a + Math.PI) % (Math.PI * 2)) - Math.PI;
    return a + diff * t;
}