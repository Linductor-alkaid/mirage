/// Orbit Camera 控制器。
///
/// 仅依赖 Three.js（scene + camera + DOM canvas），不接触 React。
/// - pointer 拖拽 = orbit；wheel = zoom；右键 = pan；
/// - 包含 damping（lerp factor）；
/// - 暴露 focusAgent / focusOverview API。

import * as THREE from 'three';

export interface OrbitCameraOptions {
    readonly target?: THREE.Vector3;
    readonly initialDistance?: number;
    readonly minDistance?: number;
    readonly maxDistance?: number;
    readonly damping?: number;
    readonly minPolarAngle?: number;
    readonly maxPolarAngle?: number;
}

export class OrbitCamera {
    private readonly camera: THREE.PerspectiveCamera;
    private readonly target: THREE.Vector3;
    private readonly targetDesired: THREE.Vector3;
    private readonly spherical: THREE.Spherical;
    private readonly sphericalDesired: THREE.Spherical;
    private readonly damping: number;
    private readonly minDistance: number;
    private readonly maxDistance: number;
    private readonly minPolarAngle: number;
    private readonly maxPolarAngle: number;
    private readonly canvas: HTMLCanvasElement;

    private dragging = false;
    private lastX = 0;
    private lastY = 0;
    private button = 0;

    constructor(camera: THREE.PerspectiveCamera, canvas: HTMLCanvasElement, opts: OrbitCameraOptions = {}) {
        this.camera = camera;
        this.canvas = canvas;
        this.target = opts.target?.clone() ?? new THREE.Vector3(0, 0, 0);
        this.targetDesired = this.target.clone();
        const initialDistance = opts.initialDistance ?? 30;
        this.minDistance = opts.minDistance ?? 6;
        this.maxDistance = opts.maxDistance ?? 120;
        this.spherical = new THREE.Spherical(initialDistance, Math.PI * 0.35, Math.PI * 0.25);
        this.sphericalDesired = this.spherical.clone();
        this.damping = opts.damping ?? 0.15;
        this.minPolarAngle = opts.minPolarAngle ?? 0.08;
        this.maxPolarAngle = opts.maxPolarAngle ?? Math.PI * 0.48;
        this.attach();
    }

    dispose(): void {
        this.detach();
    }

    /** 每帧调用 lerp 平滑到 desired。 */
    update(): void {
        this.spherical.theta += (this.sphericalDesired.theta - this.spherical.theta) * this.damping;
        this.spherical.phi += (this.sphericalDesired.phi - this.spherical.phi) * this.damping;
        const radiusDelta = this.sphericalDesired.radius - this.spherical.radius;
        this.spherical.radius += radiusDelta * this.damping;
        this.spherical.radius = Math.max(this.minDistance, Math.min(this.maxDistance, this.spherical.radius));
        this.spherical.phi = Math.max(this.minPolarAngle, Math.min(this.maxPolarAngle, this.spherical.phi));

        this.target.lerp(this.targetDesired, this.damping);

        const v = new THREE.Vector3();
        v.setFromSpherical(this.spherical);
        this.camera.position.copy(this.target).add(v);
        this.camera.lookAt(this.target);
    }

    /** 立即把镜头对准某 Agent（围绕其中心 + 提升俯角）。 */
    focusAgent(position: { x: number; y: number; z: number }, distance = 12): void {
        this.targetDesired.set(position.x, position.y + 0.5, position.z);
        this.sphericalDesired.radius = distance;
        this.sphericalDesired.phi = Math.PI * 0.3;
    }

    /** 把镜头抬到俯瞰模式。 */
    focusOverview(center: { x: number; z: number }, radius = 35): void {
        this.targetDesired.set(center.x, 0, center.z);
        this.sphericalDesired.radius = radius;
        this.sphericalDesired.phi = Math.PI * 0.35;
    }

    private onPointerDown = (e: PointerEvent): void => {
        if (e.button !== 0 && e.button !== 2) {
            return;
        }
        this.dragging = true;
        this.lastX = e.clientX;
        this.lastY = e.clientY;
        this.button = e.button;
        this.canvas.setPointerCapture(e.pointerId);
    };

    private onPointerMove = (e: PointerEvent): void => {
        if (!this.dragging) {
            return;
        }
        const dx = e.clientX - this.lastX;
        const dy = e.clientY - this.lastY;
        this.lastX = e.clientX;
        this.lastY = e.clientY;
        const rotateSpeed = 0.005;
        if (this.button === 0) {
            // 左键：orbit
            this.sphericalDesired.theta -= dx * rotateSpeed;
            this.sphericalDesired.phi -= dy * rotateSpeed;
        } else if (this.button === 2) {
            // 右键：pan
            const panSpeed = this.spherical.radius * 0.0015;
            const right = new THREE.Vector3();
            const up = new THREE.Vector3();
            this.camera.getWorldDirection(right);
            right.cross(this.camera.up).normalize();
            up.copy(this.camera.up);
            this.targetDesired.addScaledVector(right, -dx * panSpeed);
            this.targetDesired.addScaledVector(up, dy * panSpeed);
        }
    };

    private onPointerUp = (e: PointerEvent): void => {
        if (!this.dragging) {
            return;
        }
        this.dragging = false;
        try {
            this.canvas.releasePointerCapture(e.pointerId);
        } catch {
            /* swallow */
        }
    };

    private onWheel = (e: WheelEvent): void => {
        e.preventDefault();
        const factor = Math.exp(e.deltaY * 0.0015);
        this.sphericalDesired.radius = Math.max(
            this.minDistance,
            Math.min(this.maxDistance, this.sphericalDesired.radius * factor),
        );
    };

    private onContextMenu = (e: Event): void => {
        e.preventDefault();
    };

    private attach(): void {
        this.canvas.addEventListener('pointerdown', this.onPointerDown);
        this.canvas.addEventListener('pointermove', this.onPointerMove);
        this.canvas.addEventListener('pointerup', this.onPointerUp);
        this.canvas.addEventListener('pointercancel', this.onPointerUp);
        this.canvas.addEventListener('wheel', this.onWheel, { passive: false });
        this.canvas.addEventListener('contextmenu', this.onContextMenu);
    }

    private detach(): void {
        this.canvas.removeEventListener('pointerdown', this.onPointerDown);
        this.canvas.removeEventListener('pointermove', this.onPointerMove);
        this.canvas.removeEventListener('pointerup', this.onPointerUp);
        this.canvas.removeEventListener('pointercancel', this.onPointerUp);
        this.canvas.removeEventListener('wheel', this.onWheel);
        this.canvas.removeEventListener('contextmenu', this.onContextMenu);
    }
}