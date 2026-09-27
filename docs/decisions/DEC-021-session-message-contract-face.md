# DEC-021：M5 会话与消息契约面（协议 v1 扩展）与 Runtime Service 会话 / 任务模型迁移

> 状态：Accepted
> 日期：2026-09-26
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-04` 落地）
> 替代/被替代：无；本记录是 [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的**附加扩展**（传输载体、帧格式、载荷上限、单连接单未决请求纪律与一致性模型全部
> 不变），并兑现 [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
> 预告的迁移路径第一步——Runtime Service 由 M1 过渡驱动形态向 pinned mira 会话 /
> 任务模型演进

## 背景与问题

`M5-04` 要求交付会话与消息契约面：产品 UI 的会话页（`M5-06` 产品化）需要会话
列表 / 打开 / 历史摘要的请求面，以及消息 / 轮次 / 增量输出的事件流。现状的四个
结构性缺口：

1. **会话不是一等公民**：`MiraHost::start()` 在 pinned 运行时上打开唯一主会话并
   隐藏其身份；`task.submit` 只落主会话，wire 上没有会话概念，UI 无法枚举或新建
   会话。
2. **无对话历史承载**：pinned mira 已交付 `build_conversation_view` 投影（从事件
   存储的 `UserMessageInjected` / `LoopSettled` 事件重建会话对话，RULE-07 投影纪
   律），但 Mirage 服务侧没有事件存储，任务目标与结算结果不入存储，历史无处可读。
3. **会话 / 轮次 / 输出无事件流**：`task.updated` 是任务级快照通知（progress 为
   产品投影串），会话维度的新消息、逐步轮次结算与步结果输出在 wire 上不可见，
   `M5-06` 的消息流 / 运行时间线 / 增量输出渲染没有输入。
4. **迁移路径未动**：DEC-008 记录的 M1 过渡形态（宿主侧驱动循环 + 脚本化 steps）
   至今是唯一任务形态；模型循环（pinned `AgentLoop`）落地前，会话 / 消息契约面需要
   一个不依赖模型循环的承载边界，避免契约先等实现。

实现前需定案：`session.*` 请求面形状、事件集语义与预算、任务与会话的绑定方式、
历史投影的承载位置、以及过渡驱动形态下哪些语义现在为真、哪些留给模型循环。

## 决策

1. **协议 v1 附加扩展（DEC-012 流程，传输 / 帧格式 / 版本号不变）**：
   - 新请求 op：`session.list`（无参数）、`session.open`（无参数）、
     `session.history`（`session_id` 非空 string，可选正整数 `limit`）。
   - 新成功载荷：`SessionList`（`{"sessions":[{"id","state","created_at_ms"}]}`）、
     `SessionOpened`（`{"session_id"}`）、`SessionHistory`（`{"session_id",
     "entries":[{"kind","text","sequence","recorded_at_ms"}],"truncated"}`）。
   - `task.submit` 新增可选成员 `session_id`（string，非空）：缺席落主会话，M1
     行为不变（旧客户端零影响）；`task.submit` 成功载荷 `TaskSubmitted` 新增可选
     成员 `session_id`（encode-when-set，服务端恒写出）。不改动 `task.list` /
     `task.inspect` / `task.updated` 载荷——任务与会话的关联经提交回执与本决策
     事件集呈现，快照载荷的关联成员留待真实需要时以附加扩展进入。
   - hello 响应（`ServiceIdentity`）新增可选成员 `sessions`（boolean）能力通告：
     会话请求面被服务时恒写出 true；解码端缺省视为 false，沿用 `events` /
     `permissions` 纪律。
2. **会话语义**：
   - 会话经 `session.open` 在 hosted pinned 运行时上打开（pinned `open_session`，
     绑定同一桌面环境），服务侧注册表记录 id、创建时刻并投影状态；主会话在
     `session.list` 中与后续会话同等可见。M5-04 不提供 `session.close`——会话随
     服务停机的 pinned 有序关闭收敛，显式关闭面留待真实需要时附加。
   - `state` 为 pinned `SessionState` 的稳定小写蛇形名（封闭集合：`opening` /
     `autonomous` / `takeover_pending` / `human_controlled` / `resuming` /
     `closing` / `closed` / `failed`），由服务从 pinned `session_snapshot` 实时
     投影；单项快照失败的条目保守呈现 `failed`，不虚构状态。
   - 容量边界：服务侧会话数上限（`max_sessions`，默认 16）饱和时 `session.open`
     以稳定错误 `unavailable`（"session capacity exhausted"）显式拒绝，不静默
     排队、不挤占既有会话。未知会话 id（`session.history`、`task.submit` 显式
     绑定）为稳定错误 `not_found`；pinned 拒绝按既有 passthrough 形态呈现。
   - `session.list` / `session.history` 是快照事实源；事件是通知，一致性模型
     （DEC-012 决策 4）不变：客户端经 `session.list` / `session.history` 重同步，
     不把事件流当完整状态。订阅 seed 语义不变（仍只 seed host 状态）。
3. **事件集（封闭集合，附加进入，不改既有事件字段语义）**：
   - `session.updated`：`{"session_id","state"}`，会话进入注册表（open）时发布；
     会话内状态的逐步变化不逐条广播（列表快照为事实源）。
   - `session.message`：`{"session_id","task_id","kind","text","sequence"}`，会话
     对话投影新增一条时发布；`kind` 为封闭词表 `"user"`（任务目标入会话）/
     `"outcome"`（任务结算摘要），`sequence` 为该条目在会话事件序列中的序号
     （pinned `session_sequence`），`text` 为消息文本。历史快照的条目另含
     `recorded_at_ms`（完整投影语义）；事件不带时间戳（投递时点语义由客户端
     标注）。
   - `session.turn`：`{"session_id","task_id","step","kind","status"}`，一个有界
     工作单元（M1 驱动 = 一个脚本步；模型循环落地后 = 一次 Observe→…→Verify
     迭代）结算时发布；`kind` 沿用步词表（`filesystem.read` / `process.execute`，
     模型循环落地后按其动作词表附加扩展），`status` 沿用步状态词表（`ok` /
     `failed` / `cancelled` / `skipped`）。轮次开始不发布事件（`task.updated`
     已覆盖进行中语义），只发布结算。
   - `session.output`：`{"session_id","task_id","step","chunk","truncated"}`，步
     的结构化结果作为输出增量发布；`chunk` 受与服务步结果相同的字节预算（
     `max_result_bytes`）约束，`truncated` 标记截断。M1 驱动每步产出一份完整结
     果（单 chunk）；流式输出出现后同一形状承载多个增量 chunk，wire 不变。
   - 承载与背压：全部经既有 EventHub（`executor::comm::Topic`）广播，会话级发布
     与任务事件同路径同纪律（serial 域发布、驱动 best-effort 投递、每连接有界
     drop-oldest 队列、溢出 `events.overflow` 显式呈现），不新增通信设施。
4. **历史投影承载（pinned 投影，不自建第二事实源）**：
   - 服务进程持有 pinned `MemoryEventStore`（有界，默认 10000 条）作为会话事件
     存储，经 integration/mira 新适配器 `SessionJournal` 呈现为 pinned-free 面：
     `append_user_message`（写 `UserMessageInjected` 事件，载荷为 pinned 循环同
     款信封 `{"task_id","task_epoch","detail":{"text","bytes","digest"}}`）、
     `append_outcome`（写 `LoopSettled` 事件，detail 为
     `{"outcome","steps","recoveries"}`，recoveries 恒 0）、`history`（经 pinned
     `build_conversation_view` 重建后取最新 `limit` 条，`truncated` 标记更早条目
     被预算截断）。历史读取是每次重建的投影，存储是唯一事实源（RULE-07）。
   - 任务与会话的消息流接线：`task.submit` 受理即向目标会话追加一条 user 消息
     （文本 = goal）；驱动结算即追加一条 outcome 消息（pinned `loop_outcome_name`
     词表 + 实际步数）。模型循环（pinned `AgentLoop`）落地后改为循环自身经
     `set_event_store` 写同一存储，同一投影、同一契约面，驱动追加速率归零——这
     是过渡形态与目标形态的接缝，wire 无感。
   - 存储为进程内存易失形态：服务重启后会话历史从空开始（投影可重建性不受影
     响）。会话 / 历史的跨重启持久化不在本决策范围，留待设置持久化条目定案
     （DEC-011 修订或后续决策）时一并处理；在此之前不得宣称会话持久化能力。
5. **Runtime Service 会话 / 任务模型迁移（DEC-008 路径第一步）**：
   - `MiraHost` 新增 pinned-free 会话面：`open_session()`（hosted 运行时上新开
     会话）、`session_view()`（pinned 快照投影：state 名 + environment epoch）、
     `submit_task(session, goal)` 重载、`primary_session()`（主会话身份）。pinned
     实例仍由 host 独占持有，单 owner 纪律不变；会话命令全部经既有 serial 域。
   - 服务侧会话注册表（id → 创建时刻）为服务内存态；注册表容量与 `max_sessions`
     同源。任务记录新增所属会话 id（持久化恢复的旧记录缺省为空，wire
     encode-when-set）。
   - 本步不引入模型循环、不动 steps 语义：`task.submit` 的脚本化 steps 驱动保持
     DEC-007 / DEC-008 形态。模型循环进入属后续里程碑工作项，其接缝由决策 4 的
     存储共享点固定。

## 备选方案

- **`task.list` / `task.inspect` / `task.updated` 全部加 `session_id` 成员**：推
  迟。关联面（提交回执 + 会话事件集）已满足会话页 `M5-06` 的第一轮需求；给全部
  任务快照载荷加成员会放大 golden 面变更而无对应消费方，待会话页真实接线暴露缺
  口后以附加扩展进入。
- **服务自建会话 / 消息模型（不动 pinned 运行时）**：否决。pinned `open_session`
  / `session_snapshot` / `build_conversation_view` 已交付对应能力，自建即重复实
  现依赖已有能力（AGENTS.md 路由纪律），且丢失 pinned 控制面对会话的可见性。
- **`FileEventStore` 持久会话历史**：推迟。跨重启会话持久化的条目归属（设置八类
  中的哪一类、目录布局、清理策略）未定案，先落地内存形态并把持久化显式挂账，不
  让存储选型阻塞契约面。
- **`session.close` 请求面**：推迟。无 UI 消费方，pinned 停机路径已收敛会话生
  命周期；显式关闭进入时按附加扩展处理（`SessionClosed` 载荷 + `session.updated`
  复用）。
- **轮次开始事件（`session.turn` phase=started）**：否决。`task.updated` 已在任
  务推进时发布快照，双事件流会让时间线渲染面对双重事实源；只发布结算保持"事件
  =已完成事实"的纪律。
- **把输出增量并进 `session.turn` 载荷**：否决。轮次（工作单元结算）与输出（结
  果流）的发布节奏与消费方不同（时间线 vs 输出流），合并后流式化时必须拆分，直
  接分开发布避免二次 wire 变更。

## 影响与风险

- `runtime/ipc` 协议面新增三个请求、三个成功载荷与四个事件；既有解码路径不改，
  M1 / M1.5 / M5-03 测试零回归；golden vectors `meta.version` 3 → 4，双端门禁同
  步。
- `integration/mira` 新增 `SessionJournal` 适配器：runtime 对 pinned 事件存储的
  唯一接触面，公共头 pinned-free（边界门禁覆盖）。
- `runtime/mira_host` 公共头新增会话面类型与方法；pinned 类型仍不外溢。
- `runtime/service` 新增会话注册表、三个请求处理器与事件发布点；`ServiceConfig`
  新增 `max_sessions`（容量显式）。任务记录新增会话 id 成员（持久化 schema 向后
  兼容：缺省为空）。
- 事件负载构造成本随订阅数线性增长（DEC-012 既有注记适用）；`session.output`
  使每步多一条广播事件，会话页接入后按多连接产品化复核条目复核。
- 内存事件存储容量（默认 10000 条）饱和行为遵循 pinned `MemoryEventStore` 语义；
  超预算的会话历史以 `truncated` 呈现，不静默丢条目。

## 验证方式

- 协议测试（`tests/runtime`）：`session.*` 请求 / 响应 / 事件编解码 round-trip、
  未知会话错误路径、`task.submit` 带 / 不带 `session_id` 两形态、hello `sessions`
  能力位（有 / 无两形态解码）、新事件词表校验失败路径。
- Golden vectors 双端门禁：v4 向量（requests +4、responses +5、events +4 及失败
  向量）C++ 与 TypeScript 消费同一文件，逐字节一致。
- 服务测试：hello 通告 `sessions`；`session.open` → `session.list` 含主会话与新
  会话；`task.submit`（显式会话）→ `session.message`（user）→ 步结算
  `session.turn` / `session.output` → 终态 `session.message`（outcome）→
  `session.history` 呈现 user + outcome 投影；未知会话 `not_found`；容量饱和
  `unavailable`；`truncated` 截断路径；断连清理与既有事件纪律零回归。
- 集成 / 主机测试：`MiraHost` 会话面（新开会话视图、会话内提交、视图状态投影、
  停机收敛）；`SessionJournal` 追加 / 投影 round-trip 与 pinned 投影 fail-closed
  行为。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan`（会话事件跨上下文发
  布路径必须 tsan）构建与 `ctest` 全绿；`mirage-format-check` 与
  `mirage-boundary-check` 通过；前端 `npm run check` / `npm test` / `npm run
  lint` / `npm run build` 全绿。

## 关联文档和工作项

- [DEC-007](DEC-007-local-ipc-and-runtime-service.md)（协议 v1 与服务形态）、
  [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)（迁移路径
  第一目兑现）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
  （扩展流程与一致性模型）、[DEC-004](DEC-004-mira-host-status-set.md)（终态幂
  等语义沿用）。
- Wire 契约：
  [《Mirage Local IPC 协议 v1 Wire Schema》](../design/mirage-ipc-protocol-v1.md)
  §4 / §6 / §7.2 / §10。
- pinned 依据：`third_party/mira/include/mira/runtime.hpp`（`open_session` /
  `session_snapshot`）、`conversation_log.hpp`（`build_conversation_view`）、
  `event_store.hpp`（`MemoryEventStore`）、`agent_loop.hpp`（事件信封与类型词
  表）、`third_party/mira/docs/api/core-runtime.md` 与 `model-agent-loop.md`。
- 工作项：`M5-04`（本决策）；消费方 `M5-06`（会话页真实化，已于 2026-09-27
  第一轮兑现——UI 侧 `MirageTransport` 会话三方法 + `sessions` 能力位消费，
  会话页全部数据面接契约路径、模拟域退役，消费语义见
  [DEC-025](DEC-025-session-page-productization.md)；wire 契约零变更）、
  `M5-07`（批准中心复用事件纪律）。
