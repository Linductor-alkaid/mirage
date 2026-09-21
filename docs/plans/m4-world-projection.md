# M4：Organization Layer 与 World Projection

> 状态：Planned
> 负责人：Mirage 维护者（自主开发轨道）
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M1 / M1.5 / M2 / M3（已完成）
> 范围增量建议发布点：随 master 开发线交付（M5 产品化窗口内联）
> 更新日期：2026-09-21

## 目标

把"自治软件公司"的旧概念重新拆成两层：

1. **Organization Layer**：Mira Multi-Agent Runtime 当前状态的语义层表达
   （Agent / Team / Role / Task / Activity / Collaboration / Event）。
   它**不是** Three.js 的附属品；它独立存在，可被任意 UI 消费。
2. **World Projection**：Mirage 中一个新增的「三维组织世界」视图，以 Three.js
   作为渲染后端。World Model 是空间语义层，与 Three.js 解耦，可换主题。

Mira 不应感知任何视觉概念。Organization Layer 只描述"谁在做什么、与谁发生
什么关系"。World Projection 自行把语义状态解释成空间行为
（走到工作站、坐下、协作聚集、阻塞警示）。Runtime 不会因为视觉逻辑而改变行为。

完整说明与原则见 [Genesis Organization Layer × Three.js World Projection 长线开发任务]
（位于本计划第 0 章"任务声明"）。

## 交付边界

- [ ] `SCOPE-M4-01` **Organization Layer**：组织 / 团队 / Agent / 角色 / 任务 /
      活动 / 协作 / 事件的纯 TypeScript 数据模型与事件流契约；
      携带 OrganisationEvent 总集（`agent.created`、`agent.state_changed`、
      `agent.activity_changed`、`task.*`、`collaboration.*`、`organization.*`）。
- [ ] `SCOPE-M4-02` **Organization 模拟器**：Mira 当前尚未暴露 multi-agent
      Runtime，Organization Layer 在前端以**确定性 seed 模拟器**先行（不冒充
      契约层事实，UI 标注「模拟」），按状态机推进活动；为后续接入真实 multi-agent
      Runtime 保留 `OrganizationEventSource` 接口。
- [ ] `SCOPE-M4-03` **World Model**：与渲染无关的空间语义数据（Building /
      Floor / Zone / Workstation / MeetingSpace / AgentEntity /
      InteractiveObject / SpatialRelations）。可被替换主题而不影响 Organization。
- [ ] `SCOPE-M4-04` **World Projector**：将 Organization 事件序列投影为 World Model
      状态变更（增量 reducer + 审计日志）。World Model 与 Organization 之间保持
      单向依赖。
- [ ] `SCOPE-M4-05` **Three.js 渲染层**：在 `ui/app/` 内新增 Three.js 渲染器，
      订阅 World Model 状态变化，独立 RAF；camera / lighting / resize / 生命周期
      全部封装；为后续更换主题预留 ThemeAdapter 抽象（首期仅落地现代办公室主题）。
- [ ] `SCOPE-M4-06` **Avatar 抽象**：AvatarLoader 抽象支持未来接入 VRM / GLTF，
      首期以可替换的 placeholder 几何（capsule / cone / sprite）演示；
      AnimationRetargeter 接口预留，AnimationStateMachine 覆盖 idle / working /
      walking / collaborating / blocked / waiting / hibernated 等基础状态。
- [ ] `SCOPE-M4-07` **空间行为**：基于路点的导航（不实现 NavMesh），简单碰撞避让，
      位置 / 朝向插值，状态过渡 blend；动画切换与语义状态解耦。
- [ ] `SCOPE-M4-08` **活动可视化**：status icon、task badge、speech bubble、
      connection line、warning effect；避免 3D 场景成为堆叠 HUD，详细信息仍
      由 Mirage 传统 UI Panel 承载。
- [ ] `SCOPE-M4-09` **3D ↔ UI 联动**：Agent picking、hover、selection、focus
      camera、task highlight、collaboration highlight；点击 Agent → Mirage Agent
      Detail（与现有 `routes` 一致），`Task Panel` 选中任务 → 3D 高亮关联 Agent。
- [ ] `SCOPE-M4-10` **动态空间生成**：procedural / rule-based layout，组织规模
      变化时世界随之生长（3 / 10 / 30 Agents / 多 Team / 多 Product Line
      分区）。
- [ ] `SCOPE-M4-11` **历史回放基础**：World Model 与 Projector 都收敛到
      `state + events` 描述；保留 `WorldClock` 与 `EventLog` 接口。本阶段不
      实现 timeline UI，但架构不阻断未来接入。
- [ ] `SCOPE-M4-12` **性能与可观察性**：在 10 / 30 / 100 Agent 三档规模上
      建立 fixture 与最小性能指标；instancing、dispose、resize 资源回收、
      事件节流、React re-render 隔离全部覆盖。

明确不属于本阶段（详见"非目标"）：

- 重新实现 Mira Agent Runtime / Memory / Workflow / Computer Use。
- 自研 humanoid animation engine / 自研 3D engine / 复杂物理模拟。
- 商业美术资产采购（占位几何不阻塞架构）。
- 历史回放 UI 与 timeline 控件（M11 之前不落地）。
- 完整主题系统（默认主题落地即可，其他主题仅预留接口）。

## 架构约束（强制）

依赖方向固定为：

```text
Organization State   ←── 来自 OrganizationEventSource（Mira multi-agent 或模拟器）
        │
        ▼
Organization Layer（纯语义层）
        │
        ▼
World Projector（reducer：org events → world model delta + 审计日志）
        │
        ▼
World Model（空间语义，与渲染无关）
        │
        ▼
World Renderer（Three.js；可换 ThemeAdapter）
        │
        ▼
React View（`<WorldView/>`，挂载 canvas）
```

禁止：

- Mira / Organization 直接构造 Three.js 对象。
- Three.js 反向写入 Organization 或 Mira 状态。
- 把角色（CEO / Dev / QA）写死为特定视觉位置或动画 class（角色是数据而非视觉）。
- 让 Three.js 直接订阅原始 Mira Runtime 事件；必须经 Organization Layer 与
  World Projector 两层投影。

UI 交互方向（与以上反向，需独立命令通道）：

```text
World Renderer（picking）
        │
        ▼
World Interaction Adapter
        │
        ▼
Mirage Action（store / organization command）
```

## 边界与已有架构的衔接

- **新增仓库位置**：`ui/app/src/world/`，作为与 `state/`、`views/`、`theme/` 平级的
  一级模块；`ui/contracts/` 同步扩展（或新建 `ui/contracts/src/organization.ts`）。
- **依赖注入**：Three.js / 项目内 npm 依赖，使用现有 npm workspace；新增
  `three` 与 `@types/three`（peer dep of `three`）。
- **设计系统**：遵循「任务控制台」主题（`ui/app/src/theme/`）；3D canvas 容器
  使用已有 `surface-raised`、`overlay-scrim`、`border`、`evidence-highlight`
  等语义 token，不引入私有颜色。
- **测试**：vitest；纯逻辑（Organization / World / Projector）任 100% Node 环境
  可跑；Three.js 相关渲染仅在需要 react 测试时使用 happy-dom / jsdom 兜底，
  真实渲染依赖 `npm run build` 验证。
- **构建**：`npm run check`、`npm test`、`npm run build` 必须保持现有 PASS；
  TypeScript 严格模式 (`noUncheckedIndexedAccess`、`verbatimModuleSyntax`) 全程
  适用。

## 里程碑拆分

每个 Milestone 都必须**可运行、可验证**（增量跑 `npm run check`、`npm test`、
`npm run build`）。下列顺序是建议值；M0 / M1 / M2 / M4 是关键路径，其余可根据
需要调整或并行。

```text
M0  仓库调查 + 里程碑计划（本文件）+ 文件结构与边界定稿
M1  Three.js 渲染骨架：scene / lighting / camera / resize / lifecycle / 第一个 view 路由
M2  World Model（纯数据，无渲染）：Building / Floor / Zone / Workstation / Entity / Spatial relations
M3  Procedural office layout：Organization tree → floor plan（规则驱动 + 单测）
M4  Organization Layer + 模拟器 + 事件契约（OrganizationEvent / OrganizationEventSource）
M5  WorldProjector（org events → world model delta reducer + 审计日志 + 状态机）
M6  Agent placeholder entity + 视觉状态机（idle / working / walking / collaborating / blocked / waiting / hibernated）
M7  Movement + 路径 / 朝向插值 + 动画 blend + arrival detection
M8  Activity 可视化（badge / connection line / speech bubble / warning）
M9  Mirage UI 联动（route、picking、hover、select、focus camera、Task highlight、collaboration highlight）
M10 动态组织变化（agent / team 增删后世界形状变化）
M11 性能与稳定性（10 / 30 / 100 fixture + 资源回收 + 事件节流 + React 渲染隔离）
M12 历史回放基础（WorldClock + EventLog；不实现 timeline UI，但不可阻断未来接入）
```

每个 Milestone 退出条件（最小集）：

- `npm run check` 通过；
- `npm test` 通过并至少新增 1 个针对该 milestone 关键面的 vitest 用例；
- `npm run build` 通过；
- 受影响设计 / 决策文档同步（如新增 `world/projection` 模块就在本计划追加章节）。

## 当前状态

- M0 完成：仓库调查完毕；本计划已写。
- M1 完成：Three.js 渲染骨架（renderer lifecycle + OrbitCamera + 地面 + 自适应 ResizeObserver）。
- M2 完成：World Model 完整数据结构（World / Building / Floor / Zone / Workstation / MeetingSpace / AgentEntity / InteractiveObject）。
- M3 完成：Procedural office layout（规则驱动，4 Team × 28 Agent 测试 fixture）。
- M4 完成：Organization Layer（events / reducer / store / 确定性 seed simulator）。
- M5 完成：WorldProjector（org events → world deltas，agent visual state 映射）。

### 验证记录（M0-M5）

提交 hash：见 git log `feat/m4-world-projection` 分支。

- `npm run check`：通过（TypeScript 严格模式：noUncheckedIndexedAccess / verbatimModuleSyntax）。
- `npm test`：561 / 561 通过（含 31 个新增 world 测试：world-model、organization reducer、
  layout、projector、colors、simulator）。
- `npm run build`：通过（vite dist 生成；首屏 JS 1.07MB，gzip 319KB）。

受影响文件（按层）：

- World Model：`ui/app/src/world/model/*`（types / identity / building / agentEntity / world / index）。
- Organization：`ui/app/src/world/organization/*`（types / events / reducer / source / simulator / index）。
- Projector：`ui/app/src/world/projector/*`（colors / layout / projector / index）。
- Renderer：`ui/app/src/world/renderer/*`（types / orbit / picking / renderer / index）。
- Coordinator / Interaction / Hooks：`ui/app/src/world/{coordinator,interaction,hooks.tsx,index}`。
- View：`ui/app/src/views/world/WorldPage.tsx`，路由注册在 `state/store.ts`、`App.tsx`、
  `shell/Chrome.tsx`；样式追加在 `src/styles.css`（仅消费 `--mir-*` 语义 token）。

已知遗留与下一 Milestone 入口：

- WorldProjector 当前走 resync-from-org 全量重建；M9 引入 incremental layout 后替换。
- 视觉状态机仅切换 mesh 颜色 + 微脉冲；M6 引入路径插值与朝向 blend；M7 引入
  connection line / speech bubble / status icon 等可视化。
- 没有 Avatar / GLTF / 动画 mixer；M11 之后再做历史回放基础。

## 已接受 / 已登记的依赖反馈

- `mira` 不暴露 multi-agent Runtime 语义；Organization Layer 在前端以
  确定性 seed 模拟器先行（UI 标注「模拟」），不冒充契约层事实。
  → 反馈编号预留：`MIRA-20260921-001`（占位；详见
  `docs/dependency_feedback/ledger.md`）。

## 非目标（本计划主动不做）

- 真实 Mira multi-agent Runtime 接入（等待上游）；
- 商业 avatar / GLTF 资产；
- 任何复杂物理 / 流体 / 真实光照烘焙；
- 主题系统的完整工程化（仅留 ThemeAdapter 接口与一套默认主题）；
- 历史回放 UI（仅预留接口）；
- 复杂 NavMesh / 群体智能 / 真实游戏 AI。

## 验收与可视化

每个 Milestone 落地时：

1. 在 `docs/verification/` 增加或更新本计划的验收证据（可以是浏览器打开 dev server
   的截图、vitest 报告输出、`npm run build` 的 dist 体积记录）。
2. 在本计划追加 `## M<n>：<title> 验收记录` 段落，记录：
   - 提交 hash；
   - `npm run check` / `npm test` / `npm run build` 三项通过输出摘要；
   - 受影响文件清单；
   - 已知遗留与下一 Milestone 入口。
3. 任何对 Organization / World 契约的修订必须更新本文档，并引用相关测试。

## 待用户决策的事项（开发期间触发即暂停）

- 商业 avatar / GLTF 资产采购或定制。
- Organization Layer 与真实 Mira multi-agent Runtime 接入（必须等上游完成
  对应 contract 后再讨论 wire schema）。
- 发布策略变更（发布到 master 时机）。
- 默认主题的视觉风格定型（行业 office / 太空舱 / 学院研究所等）—— 暂定「现代
  办公室」主题；如需在 M5 之前切换须用户确认。

其余工程决策（文件结构、状态管理、camera、Three.js 选型、占位几何、测试结构、
内部命名）由开发 Agent 自主决定。