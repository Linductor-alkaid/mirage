# DEC-022：上游能力消费路由——mira 工作流 / 工具层与 mirador 目标跟踪

> 状态：Accepted
> 日期：2026-09-27
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-05` 绑定面兑现；mirador 侧消费按 POST-04 触发另行立项）
> 替代/被替代：无；本记录落实
> [2026-09-26 依赖升级审计](../supply-chain/dependency-upgrade-audit.md)
> （mira `cf0af75` → `1348515`、mirador `fff7f15` → `fb0dc3f`）交付的上游能力
> 在 Mirage 侧的消费路径与节奏

## 背景与问题

2026-09-26 双依赖升级把上游一批新能力带入 pin：mira 交付 DEC-039（MCP 工具
模块接纳）、DEC-040 TR0/TR1/TR2（工具稳定引用、Skill 发布生命周期、运行时
接线）、M26 DEC-037 Stage T1（时间策略契约）、M23/M24（记忆晋升、上下文
fork/merge）与 DEC-043（上游架构治理）；mirador 交付 M7 跨帧目标跟踪
Experimental 轨道（`object_tracker.hpp` / `shift_estimation.hpp`、
`stable_id_tracker::advance` 门控直通参数）。

这批能力触及 Mirage 两条进行中的线：

1. **M5-05（工作流契约面与编辑器真实化）尚未动工**，其 `workflow.*` 请求面、
   `atom.catalog` 能力上报与运行监控事件化必须选择绑定哪个版本的 pinned 契约
   面——升级前冻结的 `WorkflowBackend` 接口缝认知（M1.5 mock 形态）与升级后
   的 pinned `WorkflowRuntime`（新增工具引用挂载、Skill 执行注册、Degraded
   准入事件）已经不同。
2. **设计文档给 Mirage 的产品面包含 Tools / MCP 资源区与 Workflow 视图**，
   上游 Skill 发布 / 工具兼容投影交付后，"这些产品面何时以什么方式接入"需要
   一个显式路由，避免各工作项各自临时决断。

同时 mirador M7 处于 Experimental（go/no-go 即上游 M7-10 未判定、未计入兼
容性承诺），需要明确 Mirage 在转正前后的姿态。

## 决策

1. **M5-05 绑定升级后的 pinned 工作流 / 工具契约面（现状 pin，不再对齐旧认
   知）**：
   - `workflow.*` 请求面与运行监控对齐 pinned `WorkflowRuntime` 现状 API：
     `attach_workflow_tool_refs` 挂载表、Skill 执行注册（Published skill 每
     个一个 BuiltIn 注册）、`WorkflowToolCompatDegradedEvent` 等新增事件词表。
     实现前以 pinned 公开头与 `docs/api/` 复核接口缝，M1.5 mock 的
     `WorkflowBackend` 形态只作 UI 侧接口缝，不作为 pinned 面的事实源。
   - `atom.catalog` 能力上报经 pinned 工具暴露投影（exposure view）承载
     Platform Backend 能力，不自建第二套目录模型。
   - 若把工具兼容 Degraded 状态呈现在运行监控 wire 上，走
     [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md) 附加扩展流
     程（新事件词表，传输 / 帧格式 / 版本号不变）；是否呈现由 M5-05 实现轮按
     产品需求定，不提前承诺。
   - DEC-039 MCP 接纳契约（`tool_module_mcp.hpp`，Core 侧纯契约 + 宿主适配
     器边界）登记为后续 MCP 服务器连接配置（设置八类中的 MCP / Tool
     Connection Configuration）的既定承载边界：届时 Mirage 宿主适配器持有
     MCP 传输、Core 经接纳契约准入，Core 不说 MCP 协议。
2. **Skill / 工具管理产品面延后立项（POST-05）**：设计文档 Tools / MCP 资源
   区的真实化（Skill 列表 / 发布 / 吊销 / 版本轨迹、工具兼容状态呈现）在
   M5-05 契约面落地且出现产品需求后立项；Mirage 经 IPC 镜像 pinned 的发布生
   命周期（host-explicit 发布、版本严格递增、吊销仅降级），**不自建 skill 存
   储或注册表**（AGENTS.md 路由纪律）。
3. **mira 记忆晋升 / 上下文 fork-merge（M23/M24）与时间策略（M26 T1）：无
   Mirage 直接接触面，经 `Mira::core` 随 pin 隐式消费**。二者是 Mira Agent
   内核能力，Mirage 设计文档口径为"经 Mira 的接口访问"；不为其建产品视图或
   平行实现。时间策略进入 Mirage 运行监控呈现的前提是其经 workflow 事件面对
   Mirage 可见，届时按决策 1 的附加扩展通道评估。
4. **mirador M7 目标跟踪：转正前观察、不建产品契约依赖（POST-04）**：
   - `object_tracker.hpp` 为 Experimental（上游 M7-10 go/no-go 未判定，未计
     入兼容性承诺），M5 及既有多数工作项不得引用该头，不把产品或 wire 契约
     建立在其上（与上游 `DEC-018` 几何提议"阶段 1 转正后再消费"同一模式，
     参照 [DEC-016](DEC-016-mirador-visual-integration-contract.md) 先例）。
   - 转正（GO + 计入兼容性承诺）后作为**视觉观察增强**工作项立项：消费点在
     `integration/mirador` 的 PerceptionSession 管线（RULE-01 边界内）——跨
     帧稳定身份改善 Visual Reference 的时序一致性、变化门控短路降低逐帧
     分析成本；Mirage 既有 `detect_change` 产物（ChangeReport）正是上游
     `evaluate_change_gate` 的设计输入，接缝现成。wire 演进走 DEC-016 的
     visual snapshot schema 加法通道（track 身份为候选新字段）。
   - `stable_id_tracker::advance` 的新增关联参数为源码兼容增量（空列表行为
     逐位不变），现阶段无需任何代码适配；消费随 POST-04 一并评估。
5. **上游 DEC-043 架构治理：无 Mirage 行动**。上游内部纪律与门禁不构成
   Mirage 约束变化；Mirage 自身架构门禁（`mirage-format-check` /
   `mirage-boundary-check`）维持现状。

## 备选方案

- **M5-05 立即消费 `object_tracker`（跨帧跟踪直接进观察管线）**：否决。
  Experimental 契约在 M7 内可变，产品契约建立其上意味着上游 NO-GO 或签名变
  更时 Mirage 连带返工；且 M5 是产品化里程碑，视觉增强不在其验收基线内。
- **Mirage 自建工具 / Skill 注册表与发布流程**：否决。pinned DEC-040 已交付
  描述符、版本轨迹与发布生命周期，自建即重复实现依赖已有能力，且丢失 pinned
  控制面对工具接纳（DEC-015 / TM0 门禁）的统一可见性。
- **M5 内接入时间策略 UI / 控制**：推迟。Stage T1 是纯 Core 确定性闭环
  （调用方供时时钟、有界同步步），尚无经运行时暴露给 Mirage 的事件面，也无
  产品需求；先行接线只能建立平行面。
- **为记忆晋升 / fork-merge 建记忆查看产品面**：否决（现阶段）。无 UI 需求
  输入，Mira 侧能力仍在快速演进（W4/W5 刚落地）；待会话 / 工作流产品面稳定
  后按需求立项。
- **等上游 M7 转正后再升级 mirador pin**：否决。pin 前滚与能力消费是两个决
  策：升级审计证明接触面零适配、升级成本近零；为等待单一 Experimental 能力
  而 hold 整树 pin 会让其余增量（mira 工具层等 M5-05 直接所需）一起延期。

## 影响与风险

- `M5-05` 的接口缝复核基线由 M1.5 mock 形态改为 pinned 现状头文件（
  `workflow_runtime.hpp` / `workflow_events.hpp` / `tool_reference.hpp` /
  `tool_skill.hpp` / `tool_module_mcp.hpp`），工作量边界变化记入该工作项；
  wire 若扩展（工具兼容状态呈现）按 DEC-012 附加扩展执行，golden vectors 双
  端同步。
- POST-04 立项时的迁移路径已固定：`object_tracker` 仅经 `integration/mirador`
  进入（公共头边界门禁覆盖），visual snapshot schema 加法演进（DEC-016 通
  道），不触碰既有 `@vN` 生命周期语义。
- 本决策无代码变更；各消费点的验证随所属工作项执行（`M5-05` 测试面、
  POST-05 / POST-04 立项时另定验证计划）。
- 风险：上游 M7 go/no-go 时间表不受 Mirage 控制，POST-04 的触发时点不确定——
  已按"观察态"显式挂账，不阻塞任何在途工作项。

## 验证方式

- 本决策验收：实施总计划（当前状态、决策表、延后项）与 M5 计划（`M5-05` 工
  作项、设计依据）同步更新；升级审计条目与本记录互相引用。
- 决策 1 的后续验证随 `M5-05`：pinned 接口缝复核记录、`workflow.*` 面
  round-trip 测试、（若呈现）工具兼容事件 golden vectors。
- 决策 4 的后续验证随 POST-04 立项：以 DEC-016 修订或新决策记录承载 schema
  加法演进与 PerceptionSession 接线契约。

## 关联文档和工作项

- [2026-09-26 依赖升级审计](../supply-chain/dependency-upgrade-audit.md)
  （本决策的能力清单事实源）。
- [DEC-016](DEC-016-mirador-visual-integration-contract.md)（mirador 消费契
  约与 schema 加法通道）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
  （协议附加扩展流程）、[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
  （Runtime Service 向 mira 模型迁移路径）。
- 设计文档第 1、7、8、14 节（Mira 能力直用口径、观察台 / Tools-MCP 产品面、
  Workflow 视图）。
- pinned 依据：`third_party/mira/include/mira/tool_reference.hpp`（TR0）、
  `tool_skill.hpp`（TR1）、`tool_module_mcp.hpp`（DEC-039）、
  `workflow_runtime.hpp` / `workflow_events.hpp`（TR2 接线与事件）、
  `temporal_policy.hpp`（M26 T1）、`context_working_context_{fork,promotion}.hpp`
  （M24/M23）；`third_party/mirador/include/mirador/object_tracker.hpp`
  （Experimental）、`stable_id_tracker.hpp`（`ConfirmedAssociation`）。
- 工作项：`M5-05`（决策 1 绑定面）；延后项 `POST-04`（决策 4）、`POST-05`
  （决策 2）。
