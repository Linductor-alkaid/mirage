/// React hooks for World Projection。
///
/// - `useWorldPanel` 创建一个完整的 Coordinator + Renderer（生命周期跟随挂载）。
/// - 卸载时严格 dispose Three.js 资源。
/// - 内部维护独立的 OrganizationStore 与 Simulator，UI 不感知。

import { useEffect, useRef, useState } from 'react';

import { newOrganizationId } from './model/identity.js';
import { OrganizationSimulator } from './organization/simulator.js';
import { createOrganizationStore, OrganizationStore } from './organization/source.js';
import { WorldCoordinator } from './coordinator.js';
import { WorldRenderer } from './renderer/renderer.js';

export interface UseWorldPanelOptions {
    /** canvas DOM ref 必填；size 由 renderer 内部 ResizeObserver 处理。 */
    readonly canvasRef: React.RefObject<HTMLCanvasElement>;
}

export interface UseWorldPanelResult {
    readonly coordinator: WorldCoordinator | null;
    readonly agentCount: number;
    readonly logicalTime: number;
    readonly fps: number;
}

export function useWorldPanel(opts: UseWorldPanelOptions): UseWorldPanelResult {
    const [coordinator, setCoordinator] = useState<WorldCoordinator | null>(null);
    const [agentCount, setAgentCount] = useState(0);
    const [logicalTime, setLogicalTime] = useState(0);
    const [fps, setFps] = useState(0);

    // 在 useEffect 中持有可释放对象；refs 避免 React 重渲染导致 dispose 重入。
    const disposeBagRef = useRef<{
        coordinator: WorldCoordinator;
        renderer: WorldRenderer;
        store: OrganizationStore;
        simulator: OrganizationSimulator;
    } | null>(null);

    useEffect(() => {
        const canvas = opts.canvasRef.current;
        if (!canvas) {
            return;
        }
        const organizationId = newOrganizationId();
        const store = createOrganizationStore(organizationId, null, 0, 'simulator');
        const simulator = new OrganizationSimulator(organizationId);
        // 把 simulator 的事件流持续转发到 store。
        // simulator.subscribe() 会立即发一份 snapshot_sync，确保 store 与 sim 同步。
        const unsubSim = simulator.subscribe((event) => {
            store.apply(event);
        });
        const renderer = new WorldRenderer({ canvas });
        const coord = new WorldCoordinator({
            organizationId,
            organizationStore: store,
            renderer,
        });
        setCoordinator(coord);
        coord.start();
        const unsubCoord = coord.subscribe((snap) => {
            setAgentCount(snap.agentCount);
            setLogicalTime(snap.logicalTime);
        });
        const fpsTimer = window.setInterval(() => {
            setFps(renderer.getFps());
        }, 1000);
        simulator.start();
        disposeBagRef.current = { coordinator: coord, renderer, store, simulator };

        return () => {
            window.clearInterval(fpsTimer);
            unsubCoord();
            unsubSim();
            const bag = disposeBagRef.current;
            if (bag) {
                bag.coordinator.dispose();
                bag.simulator.dispose();
                disposeBagRef.current = null;
            }
            setCoordinator(null);
            setAgentCount(0);
            setLogicalTime(0);
            setFps(0);
        };
        // canvasRef 是稳定 ref，不入依赖。
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    return { coordinator, agentCount, logicalTime, fps };
}