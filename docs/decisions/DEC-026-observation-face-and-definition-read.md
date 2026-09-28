# DEC-026：M5 观察面协议附加扩展与工作流定义读取面（golden v6 批次）

> 状态：Accepted
> 日期：2026-09-28
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-06` 第二轮：观察面协议扩展批次）
> 替代/被替代：无；本记录是 [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的**附加扩展**（传输载体、帧格式、载荷上限、单连接单未决请求纪律与一致性模型
> 全部不变），并兑现 [DEC-025](DEC-025-session-page-productization.md) 挂账①
> （观察面协议附加扩展，M3 非目标"视觉参考进入 UI 观察面"）与
> [DEC-023](DEC-023-workflow-contract-face.md) 挂账①（工作流定义读取面）。
> wire 契约 `meta.version` 5 → 6（golden vectors 双端门禁同一文件）

## 背景与问题

`M5-06` 第一轮（DEC-025）把会话页接上 `session.*` 契约面后，剩余范围在
DEC-025 挂账①②③与 DEC-023 挂账①中锚定。第二轮动工前需对 golden v6 批次
构成逐项定案：

1. **观察面无 wire 承载**：wire 上没有任何观察载荷（DesktopObservation /
   `visual_snapshot_ref` 不在协议 v1 上），UI 观察台只能尾随会话事件，"视觉
   参考进入 UI 观察面"（M3 非目标）无兑现路径。
2. **驱动器观察数据投影待评估**：M1 驱动形态（脚本步 read/execute）不产生
   观察数据；观察捕获点是 design doc §6 的按需 ObservationAssembler（pinned
   绑定的 observe() 同源）。观察面以什么形态上线（事件流 vs 按需请求）必须
   如实评估，不能为"订阅演进"虚构一个没有生产者的事件流。
3. **工作流定义读取面缺位（DEC-023 挂账①）**：`workflow.list` 只投影摘要，
   编辑器只能编辑本会话持有内容副本的定义；pinned 库追加式版本记录只有
   digest 链、无定义正文读取 API（W-03 内容寻址），读取面的承载方需要定案。
4. **步级运行事件待评估（DEC-023 决策 2 预留）**：pinned 事件词表有
   `WorkflowStepStarted` / `WorkflowStepSettled`，是否随本批次进入 wire 取决
   于真实 UI 消费需求。
5. **会话管理面待评估（DEC-025 挂账②）**：重命名 / 删除 / 导出 / fork 的
   协议承载需上游能力核对后决定走扩展或登记台账。

## 决策

1. **观察面 = 按需请求，无事件形态**。新请求 `desktop.observe`（可选
   `semantic` 缺省 true、`visual` 缺省 false；编码端恒写出两成员）+ 新载荷
   `ObservationView`：帧成员（active_application / active_window /
   window_geometry / window_focused / focused_element / pointer_x /
   pointer_y / environment_state）恒在，`semantic` 与视觉对
   （`visual_snapshot_ref` + `visual_regions`，同现同缺）encode-when-set。
   语义快照投影 1024 节点 wire 预算（`kObservationNodeWireBudget`），超出以
   `truncated` 显式标记（服务端快照完整，截断只发生在 wire 视图）；生产方
   置信度等无 UI 消费方的字段不进入 wire。请求即必须（assembler 的
   requested-means-mandatory 纪律原样映射）：被请求组件不可交付时整个请求
   以 `unavailable` 失败并命名组件原因，不返回静默残缺的观察。**驱动器观
   察数据投影评估结论**：M1 驱动形态无观察生产者（脚本步 read/execute 不产
   生 DesktopObservation），wire 面因此是按需捕获请求而非订阅流；驱动器步
   级观察事件（若模型循环引入后需要）随 DEC-008 迁移路径第二步以附加扩展
   再评估，本轮不预铺没有生产者的事件。视觉组件从 `ServiceConfig`
   显式接线的 VisualReferenceRegistry 投影（非属主指针，须 outlive 服务运
   行期）；未接线即 `unavailable`，如实呈现产品现状（视觉管线进产品服务属
   后续产品化增量）。
2. **工作流定义读取面 = 服务侧产品目录保留 head 内容**。新请求
   `workflow.get`（`workflow_id` 非空）+ 新载荷 `WorkflowDefinitionView`
   （`workflow_id` / `digest` / `definition`，IR v1 JSON 对象）。承载方是
   服务侧产品目录（DEC-023 决策 3 的注册表扩展为同内容保留 head 定义文
   本）——pinned 库追加式版本记录无正文读取 API 是上游设计而非能力缺口
   （W-03/W-04 链完整性），目录保留"服务经手过的 head 内容"属 Mirage 产品
   状态（AGENTS.md：产品状态持久化由 Mirage 自研）；跨重启持久化仍随
   DEC-011 条目定案挂账。未知 id 沿用稳定 `not_found`。
3. **hello 新增可选成员 `observation`**（boolean 能力通告）：观察请求面被
   服务时恒写出 true，解码端缺省视为 false，沿用 `events` / `permissions` /
   `sessions` / `workflows` 纪律。
4. **步级运行事件：本批次不进入**。评估结论：pinned 词表确有
   `WorkflowStepStarted` / `WorkflowStepSettled`，桥可转译；但当前无任何已
   承诺的 UI 视图消费步级 wire 事件（运行视图以 `workflow.runs` 快照 +
   `workflow.run_updated` 呈现状态），DEC-023 的"按真实 UI 消费需求进入"前
   提未成立。挂账运行视图增量（与编辑器运行视图步级呈现联动）再以附加扩
   展进入，避免无消费方的 golden 双端承诺。
5. **会话管理面：上游能力核对完成，本批次不进入 wire**。核对结论（pinned
   `runtime.hpp` 会话面 = open / submit / takeover / release / close /
   snapshot）：**删除**可由 pinned `close_session`（既有能力）+ 服务注册表
   移除承载，wire 面 `session.close` 挂账下一增量（非依赖缺口，不登记台
   账）；**重命名**为产品展示层别名（DEC-025 决策 1 已定标题派生语义），
   属 Mirage 产品状态持久化（DEC-011 条目，`M5-08`）；**导出**由客户端以
   既有 `session.history` 快照投影完成，无需 wire 面；**fork**在 pinned 会
   话模型无对应操作面，产品侧"复制会话"语义未定案前不立项，若后续产品需
   要服务端 fork 再按工程规范 9.4 评估登记。

## 备选方案

- **观察面做周期广播事件（`observation.updated`）**：否决。M1 驱动形态无观
  察生产者，定时轮询捕获要么空转（无 GUI 感知需求的会话）要么放大无谓开
  销，且 Wayland 截图授权等平台约束下"按需"是设计文档 §6 的既定形态；事
  件面留给有真实生产者的模型循环。
- **观察载荷带 per-component 错误位（部分成功也返回 200 形态）**：否决。
  assembler 的 requested→mandatory 契约是 fail-closed（M2-06 先例），wire
  上复刻一套部分成功语义等于第二套观察契约；客户端显式少请求一个组件即
  得到确定性行为。
- **`workflow.get` 按 digest 读任意版本**：否决。服务只经手过 save/publish
  时的 head 内容，保留全历史正文是未被需求支撑的第二事实源；head 读取满
  足编辑器跨会话编辑（DEC-023 挂账①原文）。
- **定义正文存 pinned 侧（登记库读取缺口）**：否决。pinned 追加式版本记录
  无正文是 W-03 链完整性设计，不是缺口；正文随 save/publish 请求经过服务，
  产品目录保留即得。
- **本轮同时落地 `session.close` / 步级运行事件**：推迟。会话管理与运行视
  图步级呈现各有独立消费面（会话页管理入口、编辑器运行视图），按"先契约
  后实现、按消费分批"纪律各自成增量，避免一轮 golden 批次耦合三个消费面。

## 影响与风险

- `runtime/ipc` 协议面新增两个请求、两个成功载荷与 hello 一个能力成员；既
  有解码路径不改，M1 / M1.5 / M5-03..05 测试零回归；golden vectors
  `meta.version` 5 → 6，双端门禁同步。
- `runtime/service` 新增两个处理器与 `ServiceConfig.visual_registry`（非属
  主观察投影源，须 outlive 服务运行期）；工作流目录条目增加 head 定义正
  文（容量仍由 `max_workflow_definitions` 界定，256 KiB 字节预算沿用）。
- 观察捕获在服务 serial 域执行，耗时由 Provider 预算约束（accessibility
  快照有界、视觉组件只读注册表不触发分析——DEC-016 决策 4 既有纪律）；
  产品服务未接线视觉管线前 `visual=true` 恒 `unavailable`，UI 如实呈现。
- TS 镜像（types / codec / 三传输 / mock）与 golden 消费映射同步；mock 观
  察面为确定性模拟桌面数据（wire 形状忠实），并以 `observationCapability`
  支持旧服务降级构造。

## 验证方式

- 协议测试（`tests/runtime`）：两请求 / 两载荷编解码 round-trip、可选成员
  encode-when-set 两形态、视觉对同现同缺失败路径、区域词表拒绝、hello
  `observation` 能力位（有 / 无两形态解码）。
- Golden vectors 双端门禁：v6 向量（requests +3、request_failures +4、
  responses +4、response_failures +5）C++ 与 TypeScript 消费同一文件，逐字
  节一致。
- 服务测试：`observation_face_test`（Xvfb + AT-SPI 活拓扑）——hello 能力
  位、真实语义树投影（fixture 四节点 / focused 元素 / 派生标题）、
  semantic=false 跳过、visual 无注册表 fail closed、注册表接线后
  `@vs1` / `@v1` 视觉承载；`runtime_service_test` 无头拓扑 fail closed 与
  `workflow.get` 未知 id `not_found`。
- 前端：mock 观察面（默认 / 降级构造）、ws / desktop 传输帧形状、store 观
  察状态机（ready / error）、编辑器经 `workflow.get` 跨会话回读水合、
  `irToWorkflowDef` 往返；`npm run check` / `npm test` / `npm run lint` /
  `npm run build` 全绿。

## 挂账

1. **产品服务视觉管线接线**：产品 `mirage-service` 构造
   EnvironmentVisualPipeline + artifact store 并把注册表接入
   `ServiceConfig.visual_registry`，`desktop.observe` 视觉组件在真实桌面端
   到端点亮（与 Overlay / 采集产品化联动评估）。
2. **`session.close` wire 面**（DEC-025 挂账②结论的扩展部分）：pinned
   `close_session` 承载 + 服务注册表移除 + 会话页删除入口。——**已于
   2026-09-28 随 `M5-06` 第三增量兑现**（协议 v1 附加扩展，golden
   v6 → v7）：pinned close 取消该会话非终态任务并收敛 Closed（幂等
   NoOp），服务注册表条目移除、容量可复用；主会话为产品设备并锚定
   `task.submit` 缺席默认绑定，关闭以 `invalid_state` 拒绝；`session.updated`
   增补关闭发布点；`session.close` 回执 `{session_id, state}` 沿用
   `workflow.cancel` 形状先例。
3. **步级运行事件**（DEC-023 挂账②联动）：编辑器运行视图需要步级呈现时，
   桥转译 `WorkflowStepStarted` / `WorkflowStepSettled` + 新事件附加扩展。
4. **驱动器步级观察事件**：模型循环（DEC-008 第二步）引入后按真实消费再
   评估（DEC-025 挂账③联动）。
5. **会话重命名持久化 / fork**：重命名别名随 DEC-011 产品状态条目（M5-08）
   定案；fork 维持决策 5 结论。

## 关联文档和工作项

- [DEC-025](DEC-025-session-page-productization.md)（挂账①兑现、挂账②③④
  评估结论）、[DEC-023](DEC-023-workflow-contract-face.md)（挂账①兑现、挂
  账②联动）、[DEC-021](DEC-021-session-message-contract-face.md)（能力位 /
  快照 vs 通知先例）、[DEC-016](DEC-016-mirador-visual-integration-contract.md)
  （视觉组件投影与注册表承载）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
  （扩展流程与一致性模型）、[DEC-013](DEC-013-frontend-ia-harness-first.md)
  （编辑器 IR 对齐）。
- wire 契约：
  [《Mirage Local IPC 协议 v1 Wire Schema》](../design/mirage-ipc-protocol-v1.md)
  §4 / §6.1 / §6.6 / §10。
- pinned 依据：`third_party/mira/include/mira/runtime.hpp`（会话面核对）、
  `workflow_versioning.hpp`（版本记录无正文核实）、`workflow_events.hpp`
  （步级事件词表核实）。
- 工作项：`M5-06` 第二轮（本决策）；消费方：UI 观察台桌面状态面板、工作流
  编辑器跨会话编辑、`M5-07` 批准中心与权限页共用观察台面板形态。
