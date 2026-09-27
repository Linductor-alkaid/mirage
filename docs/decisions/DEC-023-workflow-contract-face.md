# DEC-023：M5 工作流契约面（协议 v1 扩展）与 WorkflowRuntime 服务承载

> 状态：Accepted
> 日期：2026-09-27
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-05` 第一轮落地；编辑器接真实面为该工作项第二轮）
> 替代/被替代：无；本记录是 [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的**附加扩展**（传输载体、帧格式、载荷上限、单连接单未决请求纪律与一致性模型全部
> 不变），并兑现 [DEC-022](DEC-022-upstream-capability-adoption.md) 决策 1 的绑定承诺
> ——`workflow.*` 请求面与运行监控对齐 2026-09-26 升级后 pinned `WorkflowRuntime`
> 现状 API，以 pinned 公开头（`workflow_runtime.hpp` / `workflow_events.hpp` /
> `workflow_ir.hpp` / `workflow_run.hpp` / `tool_executor.hpp`）与
> `docs/api/workflow-contracts.md` 复核接口缝，不按 M1.5 mock 旧认知实现

## 背景与问题

`M5-05` 要求交付工作流契约面与编辑器真实化。实现前接口缝复核确认的结构性事实：

1. **pinned 工作流承载已交付但未被 Mirage 接触**：pinned `WorkflowRuntime`
   （executor 路由、Run 表、库、patch/决策面、TR2 工具引用挂载与 Skill 执行注册）
   已随 pin 就绪；它需要一个宿主 `executor::Executor`、一个 `MiraRuntime` 会话与
   绑定环境——Mirage 服务进程恰好持有全部三者，但宿主至今未构造它。
2. **库是内容寻址、追加式、进程内投影**：`WorkflowDefinition` 以 IR v1 JSON 严格
   解码（未知字段 fail closed），版本记录以内容 digest 链表达；`NotValidated`
   版本可解析不可运行（W-04），`publish_validated` 以 DryRun 门禁产出
   `DryRunPassed` 版本。pinned 无库枚举 API，也无删除 API（追加式设计）。
3. **事件有词表、无 Mirage 出口**：pinned 工作流事件（`WorkflowRunStarted` /
   `WorkflowRunSettled` 等）经 `set_event_store` 落 `IEventStore`；EventStore 是
   权威记录（RULE-07），但 store 无推送钩子，wire 运行监控需要宿主侧转译。
4. **UI 编辑器接口缝（M1.5 mock `WorkflowBackend`）已预留**：`listDefs /
   saveDraft / publish / remove / atomCatalog / listRuns / run / cancelRun` 八个
   操作的 IPC 映射早已登记为协议演进输入；编辑器完整版接真实面属 `M5-05` 第二轮，
   本决策先落契约面。
5. **atom 目录的真实承载与工具注册**：DEC-022 决策 1 已定 `atom.catalog` 经
   pinned 工具暴露投影承载（`BuiltinToolRegistry` 的 exposed view），不自建第二套
   目录模型；Mirage 尚无桌面能力 → BuiltIn 工具的注册路径，目录人口是独立工作量。

实现前需定案：wire 请求面形状、库操作语义（保存 / 发布 / 删除）与 pinned 追加式
库的对齐方式、运行监控事件集、运行承载与服务侧注册表、以及 TR2 面（工具引用挂载、
Skill 执行注册、Degraded 呈现）在本轮的取舍。

## 决策

1. **协议 v1 附加扩展（DEC-012 流程，传输 / 帧格式 / 版本号不变）**——新请求 op
   八个，与 M1.5 `WorkflowBackend` 接口缝逐一对应：
   - `workflow.list`（无参数）→ `WorkflowList`：
     `{"workflows":[{"workflow_id","name","head_digest","validation","runnable",
     "updated_at_ms"}]}`。`validation` 为 pinned `WorkflowValidationResult` 稳定名
     （封闭集合 `not_validated` / `dry_run_passed` / `validated` / `rejected`）；
     `runnable` 由服务按 head 记录的 validation 投影（`dry_run_passed` /
     `validated` 为 true）。
   - `workflow.save`（`definition` 为 IR v1 JSON 对象）→ `WorkflowSaved`：
     `{"workflow_id","digest"}`。服务以 pinned `parse_workflow_definition` 严格解码
     后经 `publish_workflow(..., NotValidated)` 追加草稿版本——草稿可解析、不可运行
     （W-04），即 M1.5 `saveDraft` 的真实语义。
   - `workflow.publish`（`definition` 为 IR v1 JSON 对象）→ `WorkflowPublished`：
     `{"workflow_id","digest","dry_run_id","idempotent"}`。服务经 pinned
     `publish_validated`（结构校验 → 空参 DryRun 门禁 → 内容派生证据 → 追加
     `DryRunPassed`）；head 同容同证幂等返回。发布按**内容**寻址而非按 id：调用方
     总是提交当前定义全文（W-03 内容寻址纪律；M1.5 mock 的 `publish(id)` 进位语义
     由适配层映射到"提交当前草稿内容"）。
   - `workflow.delete`（`workflow_id` 非空 string）→ `WorkflowDeleted`：
     `{"workflow_id"}`。**删除的是产品目录条目，不是 pinned 版本历史**——pinned 库
     追加式、版本记录不可变是上游设计（W-03/W-04 链完整性），不是能力缺口；删除后
     `workflow.run` / `workflow.list` 不再呈现该工作流，已建 Run 不受影响。该工作流
     存在非终态 Run 时以稳定错误 `invalid_state` 拒绝；未知 id 为 `not_found`。
   - `workflow.atom.catalog`（无参数）→ `WorkflowAtomCatalog`：
     `{"tools":[{"wire_name","version","description","has_side_effects",
     "parameters_schema"}]}`。返回宿主 BuiltIn 工具注册表的 exposed view（pinned
     `exposed_tools()` 投影，DEC-022 决策 1：不自建第二套目录模型）。**第一轮为空
     目录**：Mirage 桌面能力 → BuiltIn 工具注册是 `M5-05` 第二轮工作（含目录人口
     与 ToolCall 步执行路径），wire 面先行定形，UI 侧按空目录呈现"无可用原子"，
     不回退到 M1.5 mock 静态目录宣称真实能力。
   - `workflow.runs`（无参数）→ `WorkflowRunList`：
     `{"runs":[{"run_id","workflow_id","state","run_epoch","created_at_ms"}]}`。
     `state` 为 pinned `WorkflowRunState` 稳定名（封闭集合 `created` / `running` /
     `paused` / `waiting_user` / `waiting_agent` / `completed` / `failed` /
     `cancelled`），由服务对每个运行时过 pinned `run_snapshot` 实时投影；单项快照
     失败的条目保守呈现 `failed`，不虚构状态。
   - `workflow.run`（`workflow_id` 非空 string；可选 `digest`（string）、
     `parameters`（JSON 对象）、`policy`（封闭策略名 `strict` / `recoverable` /
     `agent_assisted` / `interactive` / `dry_run`））→ `WorkflowRunStarted`：
     `{"run_id"}`。`digest` 缺省取服务注册表记录的 head digest（未知工作流为
     `not_found`）；经 pinned 库路径 `create_run(workflow_id, digest, ...)` 准入
     （W-03/W-04：仅可运行版本），`start_run` 异步驱动。`start_run` 拒绝时服务对该
     run 补发 `cancel_run`（best-effort）后以 pinned 透传错误呈现，不留 Created
     孤儿。并发异步驱动受 pinned `max_concurrent_async_drives`（默认 1）约束，饱和
     以 pinned 透传错误显式呈现。
   - `workflow.cancel`（`run_id` 非空 string）→ `WorkflowRunCancelled`：
     `{"run_id","state"}`。pinned `cancel_run` 幂等；`state` 为回执视图的运行状态名。
   - hello 响应（`ServiceIdentity`）新增可选成员 `workflows`（boolean）能力通告：
     工作流请求面被服务时恒写出 true；解码端缺省视为 false，沿用 `events` /
     `permissions` / `sessions` 纪律。
   - `definition` 字节预算为 256 KiB（与 pinned `WorkflowLimits::max_document_bytes`
     同源），服务侧先行字节检查，pinned 严格解码承担全部结构校验。
2. **运行监控事件化（封闭集合，附加进入）**：新事件 `workflow.run_updated`：
   `{"run_id","workflow_id","state","run_epoch","summary"?}`（`summary` 为
   encode-when-set，仅 RunSettled 携带 pinned `safe_summary`，受 pinned 2 KiB 上
   限）。事件由服务经事件桥从 **pinned 工作流事件**转译发布（`WorkflowRunStarted`
   → `running`、`WorkflowRunSettled` → 终态名），不从服务侧推测状态——事件 = 已发
   生事实。快照事实源为 `workflow.runs`（实时逐 run 快照投影），一致性模型沿用
   DEC-012 决策 4。步级 / patch / 决策 / 导航等更细事件词表按真实 UI 消费需求
   （`M5-06` 观察台、编辑器运行视图）以附加扩展进入，本轮不预铺。
3. **WorkflowRuntime 服务承载（宿主化，pinned-free 公共面）**：
   - `MiraHost` 新增工作流承载面：`attach_workflow_surface(executor,
     event_bridge)` 在 Running 宿主上构造 pinned `WorkflowRuntime`（绑定宿主
     executor、pinned 运行时、主会话与已绑定环境）；`shutdown_workflow_surface()`
     按 pinned 关闭顺序（停止生产者 → 控制面取消活动 Run → 有界清算驱动）收敛，
     且必须在服务 teardown 的 `executor.shutdown(true)` **之前**调用（pinned 关闭
     纪律：WorkflowRuntime shutdown → MiraRuntime 停止 → Executor shutdown；
     现有服务 teardown 顺序不动，在 executor 关停前插入本步）。`MiraHost::shutdown`
     兜底幂等调用。executor 引用经方法参数显式传递（公共头仅前向声明，不含
     executor 头），事件桥为 integration 层类型（`DesktopEnvironmentBinding`
     先例）。pinned 类型不外溢。
   - 事件桥（integration/mira 新适配器 `WorkflowEventBridge`）实现 pinned
     `IEventStore`：append 透传内部有界 `MemoryEventStore`（默认 10000 条，
     EventStore 权威记录纪律），同路解码 `mira.workflow.*` 词表，把 Run 级事件经
     pinned-free 回调交付服务广播。桥是宿主工作流事件到 wire 的唯一转译点，线程
     安全（驱动线程并发 append），回调异常隔离计数不外溢。
   - 服务侧两个内存注册表：工作流注册表（workflow_id → name / head digest /
     validation / updated_at，容量 `max_workflow_definitions` 默认 128，饱和
     `unavailable`）——pinned 无库枚举 API，注册表是服务对"它写入过什么"的产品
     索引，库仍是执行侧事实源；运行注册表（run_id → workflow_id / created_at，
     容量 `max_workflow_runs` 默认 256，溢出先淘汰终态条目、无终态可淘汰时
     `workflow.run` 以 `unavailable` 显式拒绝）。两者均为进程内存易失形态：与
     pinned 进程内库/Run 表同生命周期（上游 SQLite 库存储未交付，`RISK-2026-038`），
     跨重启持久化随上游库持久化与 DEC-011 设置条目另行定案，在此之前不宣称工作流
     持久化。
4. **TR2 与工具兼容呈现的取舍（DEC-022 决策 1 的实现轮定案项）**：
   - 本轮**不在 wire 呈现工具兼容 Degraded 状态**：编辑器 IR 不产生 v1.1 工具引用
     形态、无每版本 manifest 挂载入口，pinned 兼容门禁不会进入 Degraded 路径；
     呈现属后续工具引用挂载轮的附加扩展（新事件成员或新事件，golden 同步）。
   - `attach_workflow_tool_refs` / `skill_tool_registrations` 接口缝已复核，本轮
     不消费：前者需要每版本 manifest 的生产面（编辑器第二轮评估），后者依赖
     `SkillPublicationRegistry` 的发布产品面（POST-05）。宿主 verbatim 承载升级后
     `WorkflowRuntime`，后续轮按其公开 API 接线，无迁移成本。
   - 本轮不向 pinned `WorkflowRuntime` 注册 BuiltIn 工具集（`set_tool_registry`
     缺席）：含 ToolCall 步骤的 Strict 定义在 `create_run` 准入即被 pinned fail
     closed 拒绝（显式错误，非运行期失败）；工具注册与原子执行路径属第二轮。

## 备选方案

- **`workflow.save` 服务自建草稿存储、`publish` 才进 pinned 库**：否决。pinned
  `NotValidated` 版本记录就是"可解析不可运行"的草稿语义（W-04），自建草稿存储是
  第二事实源，且丢失版本链与幂等语义（AGENTS.md 路由纪律）。
- **`workflow.publish` 按 id 发布 head**：否决。pinned `publish_validated` 以定义
  内容为输入、以内容派生证据入库（DEC-025 §3）；按 id 发布需要宿主不可得的库读取
  API，且让内容寻址退化为指针语义。调用方提交全文，幂等由 pinned 承担。
- **`workflow.delete` 映射为 pinned 库删除**：否决（无此 API 且违背追加式设计），
  亦不登记依赖缺口——删除版本历史不是合理需求（Run 按 digest 回放依赖历史完整）；
  产品面删除 = 目录条目移除，属 Mirage 产品状态（AGENTS.md：产品状态持久化由
  Mirage 自研）。
- **服务侧轮询 `run_snapshot` 生成运行事件**：否决。定时轮询要么丢状态沿、要么放
  大无谓请求；事件桥从 pinned 事件词表转译，单一事实源、零轮询。
- **运行事件直接透传 pinned 事件 JSON**：否决。wire 载荷只含 id、状态名、epoch 与
  受限摘要（与 DEC-021 `session.*` 同纪律），digest / policy 等字段无 UI 消费方，
  进入即成 golden 双端承诺；按需附加。
- **`workflow.run` 同步驱动（`execute_run`）**：否决。阻塞 IPC 请求线程直至 Run 终
  态违背有界工作单元纪律；`start_run` 异步驱动 + 事件化监控才是产品形态。
- **本轮同时落地桌面原子工具注册**：推迟。目录人口 = 桌面 Provider → BuiltIn 工具
  规格（参数 schema、副作用分级）+ 执行路径（权限判定、操作边界、取消），是独立的
  实现单元；并入本轮会把契约面变更与执行面风险耦合，违背"先契约后实现"拆分纪律。

## 影响与风险

- `runtime/ipc` 协议面新增八个请求、八个成功载荷与一个事件；既有解码路径不改，
  M1 / M1.5 / M5-03 / M5-04 测试零回归；golden vectors `meta.version` 4 → 5，双端
  门禁同步。
- `integration/mira` 新增 `WorkflowEventBridge`：宿主工作流事件的唯一转译点，公共
  头除 pinned `IEventStore` 基类外 pinned-free（integration 层不受公共头边界门禁
  约束，`DesktopEnvironmentBinding` 同位先例）。
- `runtime/mira_host` 公共头新增工作流承载面类型与方法（executor 仅前向声明）；
  pinned 类型不外溢。宿主 start()/shutdown() 语义不变；工作流面未 attach 时各方法
  以 `invalid_state` 显式拒绝。
- `runtime/service` 新增两个注册表、八个请求处理器与事件发布点；`ServiceConfig`
  新增 `max_workflow_definitions` / `max_workflow_runs`（容量显式）。服务 teardown
  在 executor 关停前新增 workflow surface 收敛步（pinned 关闭顺序兑现）。
- `workflow.run` 的 Run 由 pinned `WorkflowRuntime` 经控制面创建载体任务并占用其
  Run 表（默认容量 32），饱和以 pinned 透传错误呈现；异步驱动并发（默认 1）饱和
  同。运行监控事件经既有 EventHub（`executor::comm::Topic`）广播，会话事件同纪律，
  不新增通信设施。
- 工作流库 / 注册表 / 运行注册表进程内易失：服务重启即空。在此之上不得宣称工作流
  持久化或跨重启续跑（上游 RISK-2026-038 未解除）。

## 验证方式

- 协议测试（`tests/runtime`）：`workflow.*` 八请求 / 八载荷 / 一事件编解码
  round-trip、definition 字节预算与形状失败路径、`policy` 未知名拒绝、可选成员
  （digest / parameters / policy / summary）encode-when-set 两形态、hello
  `workflows` 能力位（有 / 无两形态解码）。
- Golden vectors 双端门禁：v5 向量（requests +8、responses +9、events +1 及失败向
  量）C++ 与 TypeScript 消费同一文件，逐字节一致。
- 服务测试：hello 通告 `workflows`；save → list（not_validated、不可运行）→
  publish（dry_run_passed、可运行）→ run → runs（running / 终态投影）→
  `workflow.run_updated`（started / settled）→ cancel 路径；未知工作流 / 运行
  `not_found`；草稿 head 直接 run 被 pinned 拒绝（W-04 透传）；活动 Run 存在时
  delete 拒绝；注册表容量饱和拒绝。
- 集成 / 主机测试：`WorkflowEventBridge` append 透传 + 事件转译 + 回调异常隔离；
  `MiraHost` 工作流面（attach 状态门禁、save/publish 入库、run 生命周期、事件桥
  接线、surface 收敛后拒绝）。
- 预设矩阵按本地与 CI 能力执行（`debug` / `release` 必跑；跨上下文路径随 CI 矩阵
  扩展 sanitizer 取证）；`mirage-format-check` 与 `mirage-boundary-check` 通过；
  前端 `npm run check` / `npm test` / `npm run lint` / `npm run build` 全绿。

## 关联文档和工作项

- [DEC-022](DEC-022-upstream-capability-adoption.md)（绑定承诺与 TR2 取舍输入）、
  [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（扩展流程与一致性
  模型）、[DEC-021](DEC-021-session-message-contract-face.md)（会话面先例：能力位
  / 快照 vs 通知 / passthrough 形态）、[DEC-007](DEC-007-local-ipc-and-runtime-service.md)
  （协议 v1 与服务形态）。
- Wire 契约：
  [《Mirage Local IPC 协议 v1 Wire Schema》](../design/mirage-ipc-protocol-v1.md)
  §4 / §6.5 / §7.2 / §10。
- pinned 依据：`third_party/mira/include/mira/workflow_runtime.hpp` /
  `workflow_events.hpp` / `workflow_ir.hpp` / `workflow_run.hpp` /
  `workflow_versioning.hpp` / `tool_executor.hpp`；
  `third_party/mira/docs/api/workflow-contracts.md` 与
  `docs/design/workflow_runtime_design.md`（§7/§8 Executor 路由与关闭顺序）。
- 工作项：`M5-05` 第一轮（本决策）；第二轮挂账：编辑器完整版接真实面（DEC-013 IR
  对齐验收）、桌面原子工具注册与 `atom.catalog` 人口、每版本工具引用挂载入口评估；
  消费方 `M5-06`（观察台 / 会话页运行视图）。
