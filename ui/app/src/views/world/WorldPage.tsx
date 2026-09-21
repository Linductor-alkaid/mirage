/// WorldView — Mirage 中的「三维组织世界」主页面。
///
/// 阶段：M1-M8 — Three.js 渲染骨架 + Organization 投影 + 联动。
/// - canvas + 顶部 HUD（FPS / agent 数量 / 当前时间）；
/// - 模拟器由 hook 创建并启动；
/// - 视图层不直接持有 WorldRenderer（由 hook 管控）；仅做事件透传；
/// - 选中 Agent 时显示右侧详情面板。

import { useCallback, useEffect, useRef, useState } from 'react';
import { useWorldPanel } from '../../world/hooks.js';
import type { WorldCoordinator } from '../../world/coordinator.js';
import type { AgentEntity } from '../../world/model/agentEntity.js';
import { AgentDetailPanel } from './AgentDetailPanel.js';
import type { OrganizationState } from '../../world/organization/types.js';

export interface WorldPageProps {
    /** 当选中 Agent 时，上层可以监听（暂未实现 Dashboard 路由跳转；保留入口）。 */
    readonly onAgentSelect?: (agentId: string | null) => void;
}

export function WorldPage({ onAgentSelect }: WorldPageProps): React.ReactElement {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const { coordinator, agentCount, logicalTime, fps } = useWorldPanel({
        canvasRef: canvasRef as React.RefObject<HTMLCanvasElement>,
    });
    const [selectedEntityId, setSelectedEntityId] = useState<string | null>(null);
    const [hoveredEntityId, setHoveredEntityId] = useState<string | null>(null);
    const [organization, setOrganization] = useState<OrganizationState | null>(null);

    // 订阅 coordinator 的 OrganizationStore；渲染面板需要 org 状态。
    useEffect(() => {
        if (!coordinator) {
            return;
        }
        const unsubOrg = coordinator.subscribeOrganization((state) => setOrganization(state));
        return () => {
            unsubOrg();
        };
    }, [coordinator]);

    const handleSelect = useCallback(
        (entityId: string | null) => {
            setSelectedEntityId(entityId);
            onAgentSelect?.(entityId);
        },
        [onAgentSelect],
    );

    const handleHover = useCallback((entityId: string | null) => {
        setHoveredEntityId(entityId);
    }, []);

    // 把 React 层事件 push 给 coordinator（透传给 WorldRenderer 的 pick）。
    useEffect(() => {
        if (!coordinator) {
            return;
        }
        const unsubSelect = coordinator.onRendererSelect((id) => handleSelect(id));
        const unsubHover = coordinator.onRendererHover((id) => handleHover(id));
        return () => {
            unsubSelect();
            unsubHover();
        };
    }, [coordinator, handleHover, handleSelect]);

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
                {focusedEntity && organization && (
                    <AgentDetailPanel
                        entity={focusedEntity}
                        agent={organization.agents[focusedEntity.agentId]}
                        organization={organization}
                        onFocus={() => coordinator?.focusAgent(focusedEntity.id as unknown as import('../../world/model/types.js').AgentId)}
                        onClose={() => handleSelect(null)}
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