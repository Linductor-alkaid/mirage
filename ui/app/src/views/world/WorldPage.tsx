/// WorldView — Mirage 中的「三维组织世界」主页面。
///
/// 阶段：M1 — Three.js 渲染骨架。
/// - canvas + 顶部 HUD（FPS / agent 数量 / 当前时间）；
/// - 模拟器由 hook 创建并启动；
/// - 视图层不直接持有 WorldRenderer（由 hook 管控）；仅做事件透传。

import { useRef } from 'react';
import { useWorldPanel } from '../../world/hooks.js';
import type { WorldCoordinator } from '../../world/coordinator.js';

export interface WorldPageProps {
    readonly onAgentSelect?: (agentId: string | null) => void;
}

export function WorldPage({ onAgentSelect }: WorldPageProps): React.ReactElement {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const { coordinator, agentCount, logicalTime, fps } = useWorldPanel({
        canvasRef: canvasRef as React.RefObject<HTMLCanvasElement>,
    });

    return (
        <div className="world-page">
            <div className="world-page__toolbar">
                <div className="world-page__hud">
                    <span className="world-page__hud-item" data-testid="world-fps">{fps} FPS</span>
                    <span className="world-page__hud-item" data-testid="world-agents">{agentCount} Agents</span>
                    <span className="world-page__hud-item" data-testid="world-time">t = {Math.floor(logicalTime / 1000)}s</span>
                    <span className="world-page__hud-item world-page__hud-note">模拟（seeded simulator）</span>
                </div>
                <div className="world-page__actions">
                    <button
                        type="button"
                        onClick={() => coordinator && handleOverview(coordinator)}
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
                    onClick={() => {
                        if (coordinator && onAgentSelect) {
                            // 通知上层：当前选中的 Agent（null 表示取消）
                            onAgentSelect(null);
                        }
                    }}
                />
            </div>
            <div className="world-page__footer">
                <span>左键 = 旋转 / 右键 = 平移 / 滚轮 = 缩放</span>
            </div>
        </div>
    );
}

function handleOverview(coord: WorldCoordinator): void {
    coord.focusOverview();
}