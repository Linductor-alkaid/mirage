/// WorldView entry — 该文件由 `App.tsx` 通过 React.lazy 异步加载。
///
/// 目的：把 World Renderer（包含 three.js 全量）拆为独立 chunk；只有当用户
/// 真正进入 \\#/world 路由时才下载，避免污染首屏 / 其它视图的 bundle。
///
/// 该文件内部负责：
/// - 动态 import `../../world/hooks.js` 与 `WorldPage` 内部依赖；
/// - 暴露 `<WorldPage/>` React 组件；
/// - 显示「加载中」占位。

import { useEffect, useRef, useState } from 'react';
import { useWorldPanel } from '../../world/hooks.js';
import { AgentDetailPanel } from './AgentDetailPanel.js';
import type { WorldCoordinator } from '../../world/coordinator.js';
import type { AgentEntity } from '../../world/model/agentEntity.js';
import type { OrganizationState } from '../../world/organization/types.js';

export interface WorldPageProps {
    readonly onAgentSelect?: (agentId: string | null) => void;
}

/** 三维世界主页面（懒加载入口）。 */
export function WorldPageLazy({ onAgentSelect }: WorldPageProps): React.ReactElement {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const { coordinator, agentCount, logicalTime, fps, loading } = useWorldPanel({
        canvasRef: canvasRef as React.RefObject<HTMLCanvasElement>,
    });
    const [selectedEntityId, setSelectedEntityId] = useState<string | null>(null);
    const [hoveredEntityId, setHoveredEntityId] = useState<string | null>(null);
    const [organization, setOrganization] = useState<OrganizationState | null>(null);

    useEffect(() => {
        if (!coordinator) {
            return;
        }
        const unsubOrg = coordinator.subscribeOrganization((state) => setOrganization(state));
        return () => {
            unsubOrg();
        };
    }, [coordinator]);

    useEffect(() => {
        if (!coordinator) {
            return;
        }
        const unsubSelect = coordinator.onRendererSelect((id) => {
            setSelectedEntityId(id);
            onAgentSelect?.(id);
        });
        const unsubHover = coordinator.onRendererHover((id) => setHoveredEntityId(id));
        return () => {
            unsubSelect();
            unsubHover();
        };
    }, [coordinator, onAgentSelect]);

    const focusedEntity =
        selectedEntityId && coordinator
            ? findEntity(coordinator, selectedEntityId)
            : undefined;

    return (
        <div className="world-page">
            <div className="world-page__toolbar">
                <div className="world-page__hud">
                    <span className="world-page__hud-item" data-testid="world-fps">{fps} FPS</span>
                    <span className="world-page__hud-item" data-testid="world-agents">{agentCount} Agents</span>
                    <span className="world-page__hud-item" data-testid="world-time">t = {Math.floor(logicalTime / 1000)}s</span>
                    <span className="world-page__hud-item world-page__hud-note">模拟（seeded simulator）</span>
                    {hoveredEntityId && (
                        <span className="world-page__hud-item world-page__hud-note">
                            hover: {hoveredEntityId.slice(0, 8)}
                        </span>
                    )}
                </div>
                <div className="world-page__actions">
                    <button
                        type="button"
                        onClick={() => coordinator && coordinator.focusOverview()}
                        disabled={!coordinator}
                    >
                        俯览
                    </button>
                </div>
            </div>
            <div className="world-page__canvas">
                <canvas
                    ref={canvasRef}
                    data-testid="world-canvas"
                    style={{ cursor: hoveredEntityId ? 'pointer' : 'grab' }}
                />
                {loading && (
                    <div className="world-page__loading" data-testid="world-loading">
                        正在加载 Three.js 渲染层…
                    </div>
                )}
                {focusedEntity && organization && (
                    <AgentDetailPanel
                        entity={focusedEntity}
                        agent={organization.agents[focusedEntity.agentId]}
                        organization={organization}
                        onFocus={() => coordinator?.focusAgent(focusedEntity.id as unknown as import('../../world/model/types.js').AgentId)}
                        onClose={() => setSelectedEntityId(null)}
                    />
                )}
            </div>
            <div className="world-page__footer">
                <span>左键 = 旋转 / 右键 = 平移 / 滚轮 = 缩放 / 点击 Agent = 详情</span>
            </div>
        </div>
    );
}

function findEntity(coord: WorldCoordinator, entityId: string): AgentEntity | undefined {
    const snap = coord.getSnapshot();
    return snap.world.entities[entityId];
}