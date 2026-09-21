/// WorldInteractionAdapter — 把 Three.js Canvas 上的用户操作（点击 Agent），
/// 转译为 Mirage UI Action。

import type { AgentId } from '../model/types.js';
import type { WorldCoordinator } from '../coordinator.js';

export interface AgentDetailRequest {
    readonly kind: 'open-agent-detail';
    readonly agentId: AgentId;
}

export interface AgentHoverRequest {
    readonly kind: 'agent-hover';
    readonly agentId: AgentId | null;
}

export type WorldInteractionEvent = AgentDetailRequest | AgentHoverRequest;

export type WorldInteractionListener = (event: WorldInteractionEvent) => void;

/** 把 Coordinator 与上层 UI 命令连接起来的桥。 */
export class WorldInteractionAdapter {
    private readonly coordinator: WorldCoordinator;
    private readonly listeners = new Set<WorldInteractionListener>();
    private unsubscribe: (() => void) | null = null;

    constructor(coordinator: WorldCoordinator) {
        this.coordinator = coordinator;
        // 订阅 renderer 的 hover/select 由 coordinator 层处理；
        // 这里只暴露高层语义事件（agentId 一致）。
    }

    bind(rendererHover: (id: string | null) => void, rendererSelect: (id: string | null) => void): void {
        // 上层 React 调用 renderer.selectAgent 等价于此处的 emit。
        // 这里只是占位：保留未来 hook 入口。
        void rendererHover;
        void rendererSelect;
    }

    dispose(): void {
        this.unsubscribe?.();
        this.unsubscribe = null;
        this.listeners.clear();
    }

    on(listener: WorldInteractionListener): () => void {
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    }

    emit(event: WorldInteractionEvent): void {
        for (const listener of this.listeners) {
            listener(event);
        }
    }

    selectAgent(agentEntityId: string | null): void {
        this.coordinator.selectAgent(agentEntityId);
        if (agentEntityId) {
            this.emit({ kind: 'open-agent-detail', agentId: agentEntityId as unknown as AgentId });
        }
    }

    hoverAgent(agentEntityId: string | null): void {
        this.emit({ kind: 'agent-hover', agentId: (agentEntityId as unknown as AgentId) ?? null });
    }
}