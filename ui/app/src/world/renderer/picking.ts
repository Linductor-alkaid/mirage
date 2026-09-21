/// Raycasting — 把鼠标位置解析为 AgentEntityId。

import * as THREE from 'three';

export interface PickInput {
    readonly camera: THREE.PerspectiveCamera;
    readonly raycaster: THREE.Raycaster;
    readonly bundles: Map<string, { root: THREE.Object3D; body: THREE.Mesh; accent: THREE.Mesh }>;
    readonly ndcX: number;
    readonly ndcY: number;
}

/** 返回命中 Agent 的 AgentEntityId（与 bundles 的 key 对应），未命中返回 null。 */
export function pickAgent(input: PickInput): string | null {
    const { camera, raycaster, bundles, ndcX, ndcY } = input;
    raycaster.setFromCamera(new THREE.Vector2(ndcX, ndcY), camera);
    const candidates: THREE.Object3D[] = [];
    for (const bundle of bundles.values()) {
        candidates.push(bundle.body, bundle.accent);
    }
    const hits = raycaster.intersectObjects(candidates, false);
    if (hits.length === 0) {
        return null;
    }
    const hit = hits[0]!.object;
    const data = hit.userData as { agentEntityId?: string };
    return data.agentEntityId ?? null;
}