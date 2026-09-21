/// WorldCoordinator — 把 OrganizationStore + Projector + WorldRenderer
/// 串成一个一致的更新链。
///
/// 数据流：
///
/// ```text
/// OrganizationEventSource
///        │
///        ▼
/// OrganizationStore (apply)
///        │
///        ▼
/// Projector (project) ── 在 prepare() 中：resyncFromOrganization(snapshot)
///        │
///        ▼
/// World (immutable)
///        │
///        ▼
/// WorldRenderer.applyWorld()
///        │
///        ▼
/// Three.js Canvas
/// ```
///
/// 这是世界呈现的「主编排」类；React 层只负责 mount/unmount + 暴露
/// subscribe 给 HUD overlay（详情面板等）。

import type { OrganizationStore } from './organization/source.js';
import { WorldProjector } from './projector/projector.js';
import { buildInitialBuilding } from './projector/layout.js';
import type { World } from './model/world.js';
import type { AgentId, OrganizationId } from './model/types.js';
import type { WorldRenderer } from './renderer/renderer.js';

export interface WorldCoordinatorOptions {
    readonly organizationId: OrganizationId;
    readonly organizationStore: OrganizationStore;
    readonly renderer: WorldRenderer;
    readonly logicalStart?: number;
}

export interface CoordinatorSnapshot {
    readonly world: World;
    readonly agentCount: number;
    readonly logicalTime: number;
}

type Listener = (snap: CoordinatorSnapshot) => void;

export class WorldCoordinator {
    private readonly opts: WorldCoordinatorOptions;
    private readonly projector: WorldProjector;
    private snapshot: CoordinatorSnapshot;
    private readonly listeners = new Set<Listener>();
    private unsubscribeOrg: (() => void) | null = null;
    private disposed = false;

    constructor(opts: WorldCoordinatorOptions) {
        this.opts = opts;
        const orgState = opts.organizationStore.getState();
        const teams = Object.values(orgState.teams).map((t) => ({ id: t.id, name: t.name }));
        const initialBuilding = buildInitialBuilding(teams);
        this.projector = new WorldProjector({
            organizationId: opts.organizationId,
            logicalStart: opts.logicalStart ?? orgState.logicalTime,
            source: orgState.source,
            initialBuilding,
        });
        this.projector.resyncFromOrganization(orgState);
        const w = this.projector.getWorld();
        this.snapshot = {
            world: w,
            agentCount: Object.keys(w.entities).length,
            logicalTime: w.logicalTime,
        };
        this.apply();
    }

    start(): void {
        if (this.disposed) {
            return;
        }
        this.opts.renderer.start();
        this.unsubscribeOrg = this.opts.organizationStore.subscribe((state) => {
            // 简单策略：状态变化时整体 resync（M11 替换为 incremental）。
            this.projector.resyncFromOrganization(state);
            this.snapshot = {
                world: this.projector.getWorld(),
                agentCount: Object.keys(this.projector.getWorld().entities).length,
                logicalTime: this.projector.getWorld().logicalTime,
            };
            this.apply();
        });
    }

    dispose(): void {
        if (this.disposed) {
            return;
        }
        this.disposed = true;
        this.unsubscribeOrg?.();
        this.unsubscribeOrg = null;
        this.listeners.clear();
        this.opts.renderer.dispose();
    }

    subscribe(listener: Listener): () => void {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    }

    getSnapshot(): CoordinatorSnapshot {
        return this.snapshot;
    }

    selectAgent(agentId: string | null): void {
        this.opts.renderer.selectAgent(agentId);
    }

    focusAgent(agentId: AgentId): void {
        // agentId 在此语境下对应 AgentEntityId（renderer 用 entityId 作为 key）。
        this.opts.renderer.focusAgent(agentId as unknown as string);
    }

    focusOverview(): void {
        this.opts.renderer.focusOverview();
    }

    private apply(): void {
        this.opts.renderer.applyWorld(this.snapshot.world);
        for (const listener of this.listeners) {
            listener(this.snapshot);
        }
    }
}