# World Projection (ui/app/src/world)

Three.js World Projection 是 Mirage 中「三维组织世界」视图的承载模块。它把
Mira / Organization Layer 的语义状态投影成一个可观察的 3D 空间，使多 Agent
运行状态不再仅以日志 / 任务图 / 对话形式存在，而成为可被直觉感知的空间行为。

## 依赖方向

```text
Organization Layer (Mira world)  /  Simulator (seed)
        │
        ▼
World Projector (reducer)
        │
        ▼
World Model (spatial semantics, rendering-agnostic)
        │
        ▼
World Renderer (Three.js; theme-pluggable)
        │
        ▼
React View (<WorldPage/>)
```

不允许反向依赖：

- Organization / Mira 不得 import Three.js 或本模块的 renderer 子树。
- World Model 不得 import Three.js。
- Renderer 不得订阅 Organization 原始事件，只通过 World Model + Projector 接口。

UI 操作（picking / hover / select）通过 `interaction/` 反向回到 Mirage store
或 organization command channel，与正向数据流分离。

## 目录划分

- `model/` — World Model（纯数据结构 + 不变量）。Building / Floor / Zone /
  Workstation / AgentEntity / InteractiveObject / SpatialRelations。
- `organization/` — Organization Layer。Organization / Team / Agent / Role /
  Task / Activity / Collaboration / OrganizationEvent / OrganizationEventSource
  接口 + 确定性 seed 模拟器（占位用，UI 标注「模拟」）。
- `projector/` — World Projector。`reducer(OrgState, OrgEvent) → WorldDelta`，
  维护审计日志。
- `renderer/` — Three.js 渲染层。`WorldRenderer`（场景 / 相机 / RAF / dispose /
  resize）、`OrbitCamera`、`GroundPlane`、`ThemeAdapter`、`PickingController`、
  `AvatarLoader`、`AnimationStateMachine`。
- `interaction/` — World ↔ Mirage UI 双向桥。

## 当前阶段

- [M0] 仓库调查 + 计划文档（`docs/plans/m4-world-projection.md`）
- [M1] Three.js 渲染骨架（`renderer/` 最小生命周期 + OrbitCamera + 地面）
- [M2] World Model 数据结构
- [M3] Procedural layout
- [M4] Organization Layer + 模拟器
- [M5] Agent placeholder entity + Projector reducer
- [M6] Movement + 状态过渡
- [M7] Activity 可视化
- [M8] Mirage UI 联动
- [M9] 动态组织变化
- [M10] 性能
- [M11] 历史回放基础

详见 [../../../docs/plans/m4-world-projection.md](../../../../docs/plans/m4-world-projection.md)。