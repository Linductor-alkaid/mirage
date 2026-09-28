# DEC-025：M5 会话页产品化（会话面消费、模拟域退役与对话模式承载）

> 状态：Accepted
> 日期：2026-09-27
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-06` 第一轮：会话页接真实面）
> 替代/被替代：无；本记录消费 [DEC-021](DEC-021-session-message-contract-face.md)
> 交付的 `session.*` 契约面（UI 侧接线在该决策留为 `M5-06`），wire 契约不变
> （`meta.version` 仍为 5，无新请求 / 事件 / 成员）

## 背景与问题

`M5-04` 交付了 `session.*` 请求面（list / open / history）、会话事件集
（updated / message / turn / output）与 `task.submit` 会话绑定，但 UI 侧
`MirageTransport` 未暴露会话方法；会话页（签派栏 / 线程流 / Composer）仍由
M1.5 模拟域承载（预置会话种子、本地回复模拟器、演示权限卡）。`M5-06` 动工前需定案：

1. **会话列表 / 管理的消费语义**：wire `SessionSummary` 只有
   `{id, state, created_at_ms}`——无标题、无重命名 / 置顶 / 删除 / 导出 /
   fork 面；设计规范 §3.3 的会话"四件套"不能全部上线。
2. **消息流承载**：`session.history` 只投影 `user` / `outcome` 两类条目；
   `session.turn` / `session.output` 是通知（无快照回补面）。线程流的渲染
   分工与重同步策略需要明确。
3. **对话模式承载**：M1 驱动形态无模型循环（DEC-008 变更记录：宿主侧驱动
   循环不变），对话输入没有任何真实服务面可以承载；M1.5 模拟回复语料按
   M5-06 目标必须退出会话页。
4. **观察台承载**：wire 无任何观察载荷（DesktopObservation / 视觉快照不在
   协议 v1 上）；"观察流直连任务快照与事件"的现状形态与"视觉参考进入 UI
   观察面"（M3 非目标）的差距需要如实划界。

## 决策

1. **会话列表 / 管理消费**：`session.list` 是快照事实源（含主会话），
   `session.updated` 仅作通知触发刷新（DEC-012 一致性模型）；新建会话 =
   `session.open`，容量饱和以 `unavailable` 显式呈现（设计规范 §4 既定）。
   会话标题为**展示层派生**（首条 user 消息文本，缺省 `会话 <id 前 8 位>`），
   不冒充服务端权威标题；重命名 / 置顶 / 删除 / 导出 / fork 无 wire 面，
   UI 一律不呈现对应操作（不以本地状态伪造管理能力），挂账协议扩展评估
   （见挂账②）。
2. **消息流承载与重同步**：选中会话时以 `session.history`（limit 缺省窗口）
   重建 user / outcome 基线；`session.message` 事件增量入线程；`task.inspect`
   + `task.updated` 继续驱动步骤卡与活动行（步骤证据的快照事实源）；
   `task.submit` 携带 `session_id`（M5-04 既有可选参数）使任务落当前会话。
   `session.turn` / `session.output` 不直接入线程——M1 驱动形态一轮 = 一个
   脚本步，与步骤卡同源，重复呈现只制造噪音；二者作为观察流的事件源
   （决策 5）。事件溢出 / seq 跳跃后按既有 resync 纪律以
   `session.history` + `task.inspect` 重建（turn / output 细节不回补，见
   决策 5 的通知面语义）。
3. **对话模式如实降级**：模型循环未引入，pinned mira 模型层 /
   `AgentLoop` 的接入是 DEC-008 迁移路径第二步（独立工作项）。Composer
   保留对话 / 执行双模式信息架构（前端规范 §3.3），**对话模式呈现为不可用**
   （禁用 + 标注"模型循环接入后开放"），模拟回复引擎（`ChatSimulator` 与
   回复语料）删除；不以模拟对话冒充真实会话面。挂账：模型循环引入后对话
   模式接 pinned `AgentLoop` 纯对话形态（见挂账③）。
4. **模拟域退役边界**：会话页全部数据面接契约路径——`seedSessions` 预置
   会话、`ChatSimulator`、演示权限卡叠加（`approve:` 步骤约定）、上下文
   占用 meter（`seedContext`，无契约承载）全部移除；模拟域不再有任何进入
   会话页的路径。批准中心接异步确认面（`permission.*`，DEC-020）属
   `M5-07`：会话页在本轮移除演示批准卡与 Composer dock，M5-07 以真实
   批准面回归该入口。
5. **观察台真实化（现状形态）**：运行时间线 = 任务快照投影（`task.inspect`，
   既有事实源）；观察流 = 真实事件尾随——`session.turn` / `session.output` /
   `session.message` / `task.updated` 进有界环形缓冲（每会话 40 帧），
   **通知面语义**：丢帧有界、不回补，快照事实源仍是任务快照与
   `session.history`；上下文占用 meter 无契约承载，撤除。**视觉状态呈现
   挂账**：`visual_snapshot_ref` / 观察载荷不在协议 v1 上，"视觉参考进入
   UI 观察面"（M3 非目标兑现）需协议 v1 附加扩展（观察事件 / 请求面，
   golden v5 → v6）与驱动器观察数据投影评估，另立增量（挂账①）。
6. **mock 服务会话面**：与工作流面同一纪律（M5-05 第二轮先例）——wire
   忠实镜像可观察行为：主会话随构造入册、`session.open` 容量 16 饱和
   `unavailable`、`session.history` 默认 50 窗口 + `truncated`、user /
   outcome 句式与 `sequence`（`loop settled: <progress> (steps N)`）、
   turn / output 每步结算发布（无结果步骤不发布 output）、`session.updated`
   随 open、hello `sessions` 能力位与 `?sessions=off` 降级构造。mock 不
   重实现 pinned 投影内核，只保证线上形状与真实服务一致。

## 备选方案

- **保留对话模式模拟、界面标注"模拟"**：否决。M5-06 的验收即"模拟域演示
  语义退出会话页"；标注残留是 M1.5 过渡形态的延续，不是产品化。
- **turn / output 直接入线程渲染**：否决。与步骤卡双份呈现同一事实，且
  通知面丢帧会造成线程缺口而无回补面；步骤证据的权威来源是任务快照。
- **会话四件套做本地状态（不入库）**：否决。本地重命名 / 置顶在多连接与
  重连后即失真，违反"不伪造契约层事实"的 M1.5 既有纪律（harness-mock
  头注释先例）。
- **观察流回补（以 task.inspect 重建全部历史帧）**：推迟。时间线已承担
  快照回放职责；流的价值在实时尾随，回补的帧序与事件序不可对齐，留待
  观察面协议扩展（挂账①）统一定案。

## 影响与风险

- 会话页失去全部演示叙事：空状态（无会话 / 无消息）成为常态形态，视图
  与测试按真实数据形态重写。
- 会话标题为派生展示层事实：跨连接不一致可接受（不进入任何契约面）。
- 观察流为通知面：溢出场景下流有缺口；时间线（快照）不受影响。
- `MirageTransport` 接口扩展（三个会话方法 + `sessionsSupported`）为编译期
  破坏性变更：三个传输（mock / ws / desktop）同变更实现。

## 验证方式

- contracts：mock 会话面测试（list 含主会话、open 容量 `unavailable`、
  history 窗口 / truncated / sequence、user-outcome 句式、turn / output
  每步发布与无结果不发布、`?sessions=off` 降级）；ws / desktop 传输会话
  请求编码与载荷路由用例；golden vectors 消费面不变（仍 `meta.version` 5）。
- app：store 会话契约路径测试（hello 能力位 → list 基线、open、提交绑定
  session、message 增量、事件 resync）；视图行为测试（签派栏派生标题与
  徽标、Composer 对话模式禁用、观察台事件流）；模拟域测试文件退役。
- `react-hooks` 两规则豁免（M5-01 挂账）随视图重做清零并移除配置豁免；
  `npm run check` / `npm test` / `npm run lint` / `npm run build` 全绿。

## 挂账

1. **观察面协议附加扩展**（golden v5 → v6）：**已于 2026-09-28 随
   [DEC-026](DEC-026-observation-face-and-definition-read.md) 兑现**——
   `desktop.observe` 按需请求 + `ObservationView` 载荷（语义快照投影 / 视觉
   状态 / `visual_snapshot_ref` 呈现承载），"视觉参考进入 UI 观察面"（M3 非
   目标）上线；驱动器观察数据投影评估结论（M1 驱动形态无观察生产者，wire
   面为按需请求而非事件流）见该决策 1/2。
2. **会话管理面评估**：上游能力核对已完成（pinned `runtime.hpp` 会话面 =
   open / submit / takeover / release / close / snapshot），结论随
   [DEC-026](DEC-026-observation-face-and-definition-read.md) 决策 5：删除
   由 pinned `close_session`（既有能力）承载，wire 面 `session.close` ——
   **已于 2026-09-28 随 `M5-06` 第三增量兑现**（DEC-026 挂账②，golden
   v6 → v7；主会话 `invalid_state` 守卫、注册表条目移除、会话页删除入口）；
   重命名 = 产品展示层别名（DEC-011 产品状态条目，M5-08）；导出 = 客户端以
   `session.history` 投影；fork 无 pinned 操作面、产品语义未定案前不立项。
3. **对话模式真实化**：pinned `AgentLoop` 纯对话形态接入（模型层装配、
   ModelProfile 配置面、SecretRef 凭据解析），属 DEC-008 迁移路径第二步，
   与设置-模型类目（M5-08）联动评估。
4. **会话历史持久化**：维持 DEC-021 挂账（`M5-08` 设置条目定案）。

## 关联文档和工作项

- [DEC-021](DEC-021-session-message-contract-face.md)（被消费的契约面）、
  [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（一致性模型
  与附加扩展流程）、[DEC-020](DEC-020-permission-async-confirmation.md)
  （M5-07 批准中心输入）、[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
  （对话模式挂账的迁移路径定位）、[DEC-023](DEC-023-workflow-contract-face.md)
  （挂账批次协同、mock 面纪律先例）、
  [DEC-013](DEC-013-frontend-ia-harness-first.md) / 前端规范 §3.3 / §4
  （会话页信息架构与契约映射基线）。
- wire 契约：
  [《Mirage Local IPC 协议 v1 Wire Schema》](../design/mirage-ipc-protocol-v1.md)
  §4 / §5 / §6.4 / §7.2（本记录零变更）。
- 工作项：`M5-06` 第一轮（本决策）；观察台视觉呈现与会话管理面挂账见上。
