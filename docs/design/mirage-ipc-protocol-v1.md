# Mirage Local IPC 协议 v1 Wire Schema

> 状态：Accepted（事实源自 `M1.5-01` 起生效；事件帧格式与订阅语义已随 `M1.5-02`
> 落地冻结，2026-09-17）
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 依据：[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)（协议 v1 与传输冻结）、
> [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)（事件订阅扩展，已
> Accepted）、[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)（UI 唯一耦合面）
> 一致性门禁：`tests/runtime/data/ipc_protocol_golden.json`（C++ golden；TS 镜像已按 DEC-037 退役）

本文档是 Mirage Local IPC v1 的权威 wire 契约。`runtime/ipc` C++ 编解码与
客户端均以此为准，golden vectors 锁定规范字节与错误语义；原 TS 镜像按 DEC-037
退役。变更时同步本文档、golden、实现与测试（工程规范第 8 节）。

## 1. 传输与帧格式（DEC-007 冻结，本文档仅转录）

- 载体：Unix domain socket（`LOCAL IPC` 流式连接）；Windows 传输属 M4，wire 契约不变。
- 帧：4 字节小端无符号载荷长度前缀 + 载荷字节。头部固定 4 字节（`kFrameHeaderBytes`）。
- 载荷上限：1 MiB（`kMaxFrameBytes` = 1048576）。超限是协议错误：服务端回
  `protocol_error`（`"inbound exceeds the frame cap"`）并关闭连接，无静默截断。
- 帧解析（`try_extract_frame`）：缓冲区不足一个完整帧时等待更多数据；头部或声明长度
  违反规则返回协议错误，连接必须关闭。
- 每帧载荷恰为一个 JSON 对象（第 2 节信封）；一帧多对象、尾部冗余数据均属解码失败。
- 开发期 WebSocket 映射（非规范，`M1.5-03` devbridge，DEC-012 决策 6）：开发工具
  `mirage-devbridge` 在浏览器 WebSocket 与 Unix socket 之间做双向帧透传，**一条
  WebSocket binary 消息恰承载一个完整帧（含 4 字节长度前缀）**，载荷逐字节不改写；
  浏览器侧复用同一 framing 规则（`ui/contracts` 的 `makeFrame`/`tryExtractFrame`
  语义），载荷超 1 MiB 或帧头与消息尺寸不符均按协议错误关闭该连接。此映射仅是
  开发期工具约定，不构成第二传输载体，不改变 DEC-007 传输冻结。

## 2. 信封与帧判别

每个载荷是一个 JSON 对象，按**判别成员**分三类（DEC-012 决策 2）：

| 判别成员存在 | 帧类型 | 方向 |
| --- | --- | --- |
| `op` | 请求 | 客户端 → 服务端 |
| `ok` | 响应 | 服务端 → 客户端 |
| `event` | 事件 | 服务端 → 客户端（仅订阅建立后） |

既有 `decode_request` / `decode_response` 路径不因事件扩展改变；客户端帧读取循环按上表
分发。三类判别成员互斥：同时含 `op` 与 `ok` 的载荷按请求解码失败处理（服务端）；客户端
按 `op` → `ok` → `event` 顺序判别。

### 单连接单未决请求纪律（DEC-007，不变）

- 客户端在收到上一请求的响应前不得发送下一请求（事件帧不是请求，不占用未决位）。
- 服务端检测到流水线请求即回 `protocol_error`
  （`"pipelined request before the previous response"`）并关闭连接。
- 关联：响应以请求信封中的 `id` 回显关联；客户端必须校验 `id` 与未决请求一致。

## 3. 序列化规范（canonical form）

- 紧凑 JSON：无空白分隔符（`,` / `:` 直连），UTF-8，标准转义（`\"` `\\` `\b` `\f` `\n`
  `\r` `\t`，控制字符按 JSON 规则）。服务端编码用 `mira::to_json_string`；TypeScript 用
  `JSON.stringify`；两者对 ASCII 内容字节一致。
- **成员顺序即编码序**：本文档各消息表中的成员排列顺序就是编码端的写出顺序
  （mira JSON 对象为插入序）。golden vectors 的 `canonical` 字符串按此顺序锁定；两端
  编码输出必须与之逐字节一致。解码端对成员顺序不敏感，但**未知成员必须容忍并忽略**
  （附加扩展向后兼容纪律，DEC-010 先例）；未知 op / 未知事件名拒绝。
- 整数一律 JSON 整数（无小数点、无指数）；布尔小写；`null` 不出现在任何合法消息中。
- 字符串成员除注明外均为非空约束见消息表；空串是否合法以消息表"约束"列为准。

## 4. 请求消息表（客户端 → 服务端）

信封：`{"v":1,"id":<非负整数>,"op":"<名称>",...参数}`。`v` 必须等于 1，否则
`protocol_error`（`"unsupported protocol version"`）。参数成员紧随 `op` 之后，按表内
顺序写出。

| op | 参数（按 wire 顺序） | 成功载荷 | 主要错误 |
| --- | --- | --- | --- |
| `product.control` | `action`, `exit_epoch`；ready 附 `frontend_pid` | `ProductState`（DEC-045 增量） | — |
| `hello` | 无 | `ServiceIdentity`（§6.1） | — |
| `task.submit` | `goal`（string，非空），`steps`（array，可省略 = 空任务），`step_timeout_ms`（可选正整数，毫秒），`session_id`（可选 string，非空；会话绑定，M5-04 落地，缺席落主会话） | `{"task_id"}`（非空），`session_id`（可选 string，非空；任务会话归属的回执，M5-04 落地，服务端恒写出） | `invalid_argument`（空 goal、goal 超长、步数超上限、单步 argument 超长）、`invalid_state`（注册表容量满，提交回滚）、`not_found`（显式 `session_id` 未知）、`pinned_runtime`（pinned 控制面拒绝，透传） |
| `task.list` | 无 | `{"tasks":[{"id","goal","progress"}...]}` | — |
| `task.inspect` | `task_id`（string，非空） | `{"task": InspectTask}`（§6.2） | `not_found`（未知任务） |
| `task.cancel` | `task_id`（string，非空） | `{"task_cancelled":{"task_id","progress"}}`（progress 为取消请求时点的快照，典型为 `Cancelling` 或终态） | `not_found`（未知任务）、`invalid_state`（任务属既往服务轮次且已终态）、`pinned_runtime`（已终态任务，message 前缀 `invalid_state:`） |
| `task.pause` | `task_id`（string，非空；M5-10 落地，DEC-030） | `{"task_paused":{"task_id","progress"}}`（§6.9，progress 为命令受理时点的快照，典型为 `Paused`） | `not_found`（未知任务）、`invalid_state`（任务属既往服务轮次且已终态）、`pinned_runtime`（非法迁移如已暂停/终态，逐字透传） |
| `task.resume` | `task_id`（string，非空；M5-10 落地，DEC-030） | `{"task_resumed":{"task_id","progress"}}`（§6.9，progress 为命令受理时点的快照，典型为 `Active`） | `not_found`（未知任务）、`invalid_state`（任务属既往服务轮次且已终态；任务非暂停态——pinned 迁移表允许 Idle→Observing，产品边界先行拒绝）、`pinned_runtime`（其余非法迁移逐字透传） |
| `service.shutdown` | 无 | `{}`（确认形状，无附加成员） | — |
| `events.subscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 旧服务端按未知 op 拒绝：`protocol_error`（`"unknown op 'events.subscribe'"`），新客户端据此降级轮询 |
| `events.unsubscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 同上 |
| `permission.respond` | `request_id`（string，非空），`approved`（boolean）（M5-03 落地） | `{"request_id"}`（回显，§6.3） | `not_found`（未知、已决或已过期的请求 id）、`unavailable`（确认面未启用，§6.3） |
| `permission.list` | 无（M5-03 落地） | `{"pending":[PendingPermission...]}`（§6.3，可为空数组） | `unavailable`（确认面未启用） |
| `session.list` | 无（M5-04 落地） | `{"sessions":[SessionSummary...]}`（§6.4，含主会话，可为空数组） | — |
| `session.open` | 无（M5-04 落地） | `{"session_id"}`（§6.4） | `unavailable`（会话容量饱和，§6.4）、`pinned_runtime`（透传） |
| `session.close` | `session_id`（string，非空，M5-06 落地，DEC-026 挂账②） | `{"session_id","state"}`（§6.4，state 为关闭后会话状态名） | `invalid_state`（主会话不可关闭）、`not_found`（未知会话）、`pinned_runtime`（透传） |
| `session.chat` | `session_id`（string，非空）、`text`（string，非空；M5-06 落地，DEC-027） | `{"turn_id"}`（§6.7） | `unavailable`（模型层未配置）、`not_found`（未知会话）、`invalid_state`（该会话已有在途对话轮）、`invalid_argument`（文本超预算） |
| `session.chat.history` | `session_id`（string，非空）、`limit`（可选正整数；M5-06 落地，DEC-027） | `{"session_id","turns":[...],"truncated"}`（§6.7） | `not_found`（未知会话） |
| `policy.get` | 无（M5-07 落地，DEC-028） | `{"rules":{...},"read_roots":[...]}`（§6.8） | — |
| `policy.set` | `rules`（object：全 DEC-010 能力 → allow/confirm/deny）、`read_roots`（可选 string 数组 ≤64 项；M5-07 落地，DEC-028） | `{"rules":{...},"read_roots":[...]}`（§6.8） | `invalid_argument`（能力缺失 / 能力或规则越词表 / 越界） |
| `session.history` | `session_id`（string，非空），`limit`（可选正整数，M5-04 落地） | `{"session_id","entries":[...],"truncated"}`（§6.4） | `not_found`（未知会话） |
| `workflow.list` | 无（M5-05 落地） | `{"workflows":[WorkflowSummary...]}`（§6.5，可为空数组） | — |
| `workflow.save` | `definition`（object，IR v1 JSON，M5-05 落地） | `{"workflow_id","digest"}`（§6.5） | `unavailable`（注册表容量饱和，§6.5）、`pinned_runtime`（解码 / 追加拒绝，透传） |
| `workflow.publish` | `definition`（object，IR v1 JSON，M5-05 落地） | `{"workflow_id","digest","dry_run_id","idempotent"}`（§6.5） | `pinned_runtime`（门禁拒绝，透传） |
| `workflow.delete` | `workflow_id`（string，非空，M5-05 落地） | `{"workflow_id"}`（§6.5） | `not_found`（未知工作流）、`invalid_state`（存在非终态 Run） |
| `workflow.atom.catalog` | 无（M5-05 落地） | `{"tools":[ExposedTool...]}`（§6.5，可为空数组） | — |
| `workflow.runs` | 无（M5-05 落地） | `{"runs":[WorkflowRunSummary...]}`（§6.5，可为空数组） | — |
| `workflow.run` | `workflow_id`（string，非空），`digest`（可选 string，非空；缺省取 head），`parameters`（可选 object），`policy`（可选封闭策略名，§6.5；M5-05 落地） | `{"run_id"}`（§6.5） | `not_found`（未知工作流）、`unavailable`（运行注册表饱和，§6.5）、`pinned_runtime`（准入 / 提交拒绝，透传） |
| `workflow.cancel` | `run_id`（string，非空，M5-05 落地） | `{"run_id","state"}`（§6.5） | `pinned_runtime`（透传；幂等取消含终态重申） |
| `workflow.get` | `workflow_id`（string，非空，M5-06 落地，DEC-026） | `{"workflow_id","digest","definition"}`（§6.6） | `not_found`（未知工作流） |
| `desktop.observe` | `semantic`（可选 boolean，缺省 true）、`visual`（可选 boolean，缺省 false；M5-06 落地，DEC-026；编码端恒写出两成员） | `ObservationView`（§6.6） | `unavailable`（环境未绑定、组件不可交付或桌宠排除/恢复失败，fail closed）、`permission_denied`（视觉捕获权限拒绝） |

协议层（`decode_request`）只约束参数的存在与类型（如 `task.submit` 缺 `goal` 即
`protocol_error`）；空值等语义校验发生在服务层，产出表中 `invalid_argument` 等稳定错误。
解码失败（非法 JSON、版本不符、未知 op、缺判别成员、字段形状错误）一律
`protocol_error` 并关闭连接。

## 5. 响应信封与错误码

成功：`{"v":1,"id":<回显>,"ok":true,...载荷成员}`——载荷成员直接平铺进信封顶层（不嵌套
包装），按 §6 各形状的成员顺序写出。失败：`{"v":1,"id":<回显>,"ok":false,"error":{"code","message"}}`。

`code` 取值（`mirage.ipc` 域，封闭集合）：

| code | 含义 | 连接后果 |
| --- | --- | --- |
| `protocol_error` | 帧格式、JSON、版本、判别成员、未知 op 或字段形状违反契约 | 服务端关闭连接 |
| `unsupported` | 解码成功但服务端无处理路径（防御性分支；现行 op 表下不可达） | 保持连接 |
| `invalid_argument` | 请求通过协议层但违反服务层语义校验 | 保持连接 |
| `not_found` | 引用了不存在的任务或会话 id | 保持连接 |
| `invalid_state` | 任务或服务当前状态不允许该操作 | 保持连接 |
| `unavailable` | 服务暂不能提供该能力（`permission.*` 确认面未启用，M5-03；`session.open` 会话容量饱和，M5-04；其余防御性保留） | 保持连接 |
| `internal` | 服务端内部失败（如任务未被 service runtime 接纳） | 保持连接 |
| `pinned_runtime` | pinned Mira 控制面拒绝的逐字透传；`message` 以 `invalid_state:` 等稳定前缀开头，安全用于日志与 UI | 保持连接 |

`message` 面向日志与 UI，不含敏感信息；两端实现必须保持上表的错误字符串稳定
（golden vectors 锁定代表性样本）。

## 6. 载荷形状（成员按 wire 顺序）

### 6.1 `ServiceIdentity`（hello 响应）

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `service` | string | 服务名（`mirage-service`） |
| `mirage_version` | string | Mirage 版本 |
| `mira_core_version` | string | pinned Mira core 版本 |
| `host_status` | string | 主机五态：`stopped` / `starting` / `running` / `stopping` / `failed`（DEC-004，小写稳定形式） |
| `protocol` | integer | 协议版本（1） |
| `events` | boolean（可选） | DEC-012 能力通告：新服务端编码时总是写出；解码端缺省视为 `false`。置于 `protocol` 之后 |
| `permissions` | boolean（可选） | DEC-020 异步确认面能力通告（`permission.*` 请求面可用）：语义与 `events` 相同（编码端总是写出、解码端缺省 `false`）。置于 `events` 之后 |
| `sessions` | boolean（可选） | DEC-021 会话面能力通告（`session.*` 请求面可用）：语义与 `events` 相同。置于 `permissions` 之后 |
| `workflows` | boolean（可选） | DEC-023 工作流面能力通告（`workflow.*` 请求面可用）：语义与 `events` 相同。置于 `sessions` 之后 |
| `observation` | boolean（可选） | DEC-026 观察面能力通告（`desktop.observe` 请求面可用）：语义与 `events` 相同。置于 `workflows` 之后 |
| `chat` | boolean（可选） | DEC-027 对话面能力通告（模型层已配置，`session.chat` 请求面可用）：语义与 `events` 相同。置于 `observation` 之后 |
| `policy` | boolean（可选） | DEC-028 策略面能力通告（`policy.get` / `policy.set` 请求面可用）：语义与 `events` 相同。置于 `chat` 之后 |
| `tray` | boolean（可选） | DEC-045 产品准入；缺失为 headless/旧服务，true 为已注册在线托盘；wire 置于 protocol 后、events 前 |

### 6.2 `InspectTask`（task.inspect 响应载荷，嵌于 `task` 成员）

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `id` | string | 任务 id |
| `goal` | string | 提交目标 |
| `progress` | string | 产品层进度投影：`Idle` / `Active` / `Paused` / `Cancelling` / `Completed` / `Failed` / `Cancelled` / `Unknown`（pinned TaskState 的稳定名，首字母大写） |
| `success` | boolean（可选） | 仅任务到达终态后写出；存在即 `has_success` 语义 |
| `steps` | array | 步骤视图，见下表 |

`steps[i]`（`StepView`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `index` | integer | 步骤序号，自 0 起 |
| `kind` | string | `filesystem.read` / `process.execute` |
| `status` | string | `pending` / `running` / `ok` / `failed` / `skipped` / `cancelled` |
| `operation_id` | string | pinned OperationRecord 身份；未 admitted 为空 |
| `permission` | string | DEC-010 判定：`allowed` / `confirmed` / `denied` / `confirmation_rejected`，未判定为空 |
| `ok` | boolean | 步骤是否成功 |
| `exit_code` | integer | 仅 `process.execute`；否则 -1 |
| `result` | string | 文件内容或捕获输出，服务端设上限；`result_truncated` 标记截断 |
| `result_truncated` | boolean | — |
| `error` | string | 稳定失败摘要，安全用于 UI |

`task.list` 条目为 `{id, goal, progress}`（成员同序）；`task.submit` 成功载荷为
`{task_id}`，携带 `session_id` 可选成员（M5-04，任务会话归属回执，服务端恒写出；
§4）；`task.cancel` 成功载荷为 `{task_cancelled: {task_id, progress}}`；
`permission.respond` 成功载荷为 `{request_id}`（§6.3）。

### 6.3 `PendingPermission`（permission.* 载荷，M5-03，DEC-020）

`permission.list` 成功载荷：`{"pending":[PendingPermission...]}`（`pending` 可为空
数组）；`permission.respond` 成功载荷：`{"request_id": <回显>}`。

`pending[i]`（`PendingPermission`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `request_id` | string | 非空；服务端生成的待确认请求身份（`permission.respond` 以其回指） |
| `capability` | string | 封闭 Capability 词表（`filesystem.read` / `filesystem.write` / `process.execute` / `window.activate` / `screen.capture` / `input.inject` / `clipboard.read` / `clipboard.write` / `application.launch` / `application.terminate` / `notification.post`，DEC-010 / DEC-020） |
| `resource` | string | 动作资源（路径 / 命令行），可为空；披露边界见 DEC-020 决策 8 |
| `task_id` | string | 非空；发起确认的任务（RULE-05 trace 关联） |
| `timeout_ms` | integer | 正整数；快照时点的剩余等待预算 |

`permission.respond` 的 `request_id` 未知、已被应答或已超时清理时，服务回
`not_found`（`"unknown or already decided permission request id"`）；确认面未启用
（hello `permissions` 为 `false` 或缺省）时，`permission.respond` /
`permission.list` 回 `unavailable`。多连接竞争语义（first-response-wins）见
[DEC-020](../decisions/DEC-020-permission-async-confirmation.md) 决策 4。

### 6.4 会话面载荷（session.* 载荷，M5-04，DEC-021）

`session.list` 成功载荷：`{"sessions":[SessionSummary...]}`（`sessions` 含主会话，
可为空数组）；`session.open` 成功载荷：`{"session_id"}`；`session.history` 成功载荷：
`{"session_id","entries":[...],"truncated"}`。

`sessions[i]`（`SessionSummary`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `id` | string | 非空；会话身份（pinned 会话 id 的 32 位小写十六进制形式） |
| `state` | string | 封闭会话状态投影（pinned SessionState 的稳定小写形式）：`opening` / `autonomous` / `takeover_pending` / `human_controlled` / `resuming` / `closing` / `closed` / `failed`；单项快照失败保守呈现 `failed` |
| `created_at_ms` | integer | 非负整数；服务侧注册墙钟时刻（epoch 毫秒） |

`entries[i]`（`SessionHistoryEntry`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `kind` | string | 封闭词表：`user`（任务目标入会话）/ `outcome`（任务结算摘要） |
| `text` | string | 非空；消息文本（user 为提交 goal 原文；outcome 为 pinned 会话投影的结算句式 `loop settled: <outcome> (steps N)`） |
| `sequence` | integer | 正整数；该条目在会话事件序列中的序号 |
| `recorded_at_ms` | integer | 非负整数；底层事件入存储的墙钟时刻 |

`session.history` 的 `limit` 缺省为 50，服务端钳制到内部上限；`entries` 为最新
窗口（会话语序），更早条目存在时 `truncated` 为 `true`。未知会话 id（`session.history`
与 `task.submit` 显式绑定）回 `not_found`（`"unknown session id"`）；会话容量饱和时
`session.open` 回 `unavailable`（`"session capacity exhausted (N)"`）。`session.list` /
`session.history` 是快照事实源，事件是通知（DEC-012 一致性模型沿用）。语义
（状态投影、容量边界、投影承载与持久化挂账）见
[DEC-021](../decisions/DEC-021-session-message-contract-face.md)。

`session.close` 成功载荷（M5-06，DEC-026 挂账②）：`{"session_id","state"}`——
`session_id` 为被关闭会话 id，`state` 为关闭后的会话状态名（§6.4 封闭状态词
表；`closed` 为关闭命令自身的收敛后条件，服务端投影视图可读时以实时视图为
准）。成员形状沿用 `workflow.cancel` 回执先例（id + 状态名），并使信封与
`session.open` 回执在 wire 上可判别。语义：关闭由 pinned `close_session` 承
载——该会话的非终态任务被 pinned 取消、会话收敛 Closed（幂等：已 Closed 的
会话重放为 NoOp）；成功后服务侧注册表条目移除，`session.list` 不再呈现该会
话，释放的容量可复用。主会话是 start() 打开的产品设备并锚定 `task.submit`
的缺席默认绑定，关闭请求回 `invalid_state`
（`"the primary session cannot be closed"`）；未知 id 回 `not_found`
（`"unknown session id"`）。

### 6.5 工作流面载荷（workflow.* 载荷，M5-05，DEC-023）

`workflow.list` 成功载荷：`{"workflows":[WorkflowSummary...]}`；`workflow.save` 成功
载荷：`{"workflow_id","digest"}`；`workflow.publish` 成功载荷：
`{"workflow_id","digest","dry_run_id","idempotent"}`；`workflow.delete` 成功载荷：
`{"workflow_id"}`；`workflow.atom.catalog` 成功载荷：`{"tools":[ExposedTool...]}`；
`workflow.runs` 成功载荷：`{"runs":[WorkflowRunSummary...]}`；`workflow.run` 成功
载荷：`{"run_id"}`；`workflow.cancel` 成功载荷：`{"run_id","state"}`。

`workflows[i]`（`WorkflowSummary`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `workflow_id` | string | 非空；pinned 工作流 id 的 32 位小写十六进制形式 |
| `name` | string | 非空；定义内声明的名称 |
| `head_digest` | string | 非空；head 版本内容 digest 的 64 位小写十六进制形式 |
| `validation` | string | 封闭词表（pinned WorkflowValidationResult 稳定名）：`not_validated` / `dry_run_passed` / `validated` / `rejected` |
| `runnable` | boolean | head 记录是否可运行（`dry_run_passed` / `validated` 为 true，W-04） |
| `updated_at_ms` | integer | 非负整数；服务侧最近一次 save / publish 的墙钟时刻 |

`tools[i]`（`ExposedTool`，成员按 wire 顺序；宿主 BuiltIn 注册表 exposed view，
DEC-022 决策 1：目录经 pinned 投影承载，不自建第二套目录模型）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `wire_name` | string | 非空；工具 wire 名 |
| `version` | string | 非空；语义版本 `major.minor.patch` |
| `description` | string | 非空 |
| `has_side_effects` | boolean | — |
| `parameters_schema` | object | 工具参数 JSON Schema（pinned `JsonSchema` 根对象原样嵌入） |

`runs[i]`（`WorkflowRunSummary`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `run_id` | string | 非空；pinned 运行 id 的 32 位小写十六进制形式 |
| `workflow_id` | string | 非空；所属工作流 |
| `state` | string | 封闭运行状态投影（pinned WorkflowRunState 的稳定小写形式）：`created` / `running` / `paused` / `waiting_user` / `waiting_agent` / `completed` / `failed` / `cancelled`；单项快照失败保守呈现 `failed` |
| `run_epoch` | integer | 非负整数；pinned 运行纪元（每次状态转换 +1，RULE-03） |
| `created_at_ms` | integer | 非负整数；服务侧注册墙钟时刻 |

`workflow.save` / `workflow.publish` 的 `definition` 为 Workflow IR v1 JSON 对象
（DEC-013 对齐的编辑器文档形态），由 pinned `parse_workflow_definition` 严格解码
（未知字段 fail closed）；字节预算 256 KiB，超限回 `invalid_argument`
（`"workflow definition exceeds the 256 KiB budget"`）。`workflow.save` 追加
`not_validated` 草稿版本（可解析不可运行，W-04）；`workflow.publish` 经 pinned
DryRun 门禁追加 `dry_run_passed` 版本，head 同容同证幂等（`idempotent` 为 true）。
`workflow.run` 缺省 `digest` 取注册表 head（未知工作流 `not_found`
（`"unknown workflow id"`）；草稿 head 直接触发 pinned W-04 拒绝透传）；`policy`
未知名回 `invalid_argument`（`"workflow.run 'policy' is not a known policy name"`）。
注册表 / 运行注册表容量饱和回 `unavailable`（`"workflow registry capacity
exhausted (N)"` / `"workflow run registry capacity exhausted (N)"`）。删除是产品
目录条目移除，不动 pinned 追加式版本历史；存在非终态 Run 时回 `invalid_state`
（`"workflow has non-terminal runs"`）。工作流库与运行注册表为进程内存易失形态，
重启即空，在此之上不宣称持久化（DEC-023）。`workflow.runs` 是运行状态快照事实源，
事件是通知。

M7-11 / DEC-067 在此目录条件注册 `desktop.knowledge.activate`（1.0.0，副作用工具）：
需要 scoped Accessibility、Window 和共享 ActionCoordinator。实参为 knowledge_schema=1
及 application/window_title/role/name/expected_role/expected_name 六个精确 UTF-8 文本；
每项 1..1024 字节、不含 C0/DEL 控制字符，未知实参拒绝。工具执行时重新观察和唯一解析，再经
input.inject 权限、单写者及取消门禁语义激活一次；成功只在新后条件成立时返回。
成功 ToolExecutionRecord.result 是 canonical JSON **字符串**，内含 schema=2、verified、
action_id、verification 与 knowledge_digest。该形式兼容 Mira 的标量 step_result Contains
谓词；不扩展 IPC op 或 IR 语法，不将运行参数当作业务证据。DryRun 仅发布计划。
歧义、页面失效、已存在后条件或拒绝不会产生动作；不确定效果失败，Recoverable 可停在
waiting_agent。示例不声明 Retry/循环，当前不自动选择资产或恢复 Agent；版本目录重启
仍为空。参见[示例与操作](../examples/local-semantic-workflow.md)。

### 6.6 观察面与定义读取面载荷（M5-06，DEC-026）

`workflow.get` 成功载荷：`{"workflow_id","digest","definition"}`。`definition` 为
该工作流 head 版本的 Workflow IR v1 JSON 对象（服务侧产品目录保留的最近一次
save / publish 内容；pinned 库仅存版本记录、无定义正文读取 API，W-03），`digest`
为其内容寻址摘要。未知 id 回 `not_found`（`"unknown workflow id"`）。该请求面
兑现 DEC-023 挂账①（编辑器跨会话读取与编辑）。

`desktop.observe` 成功载荷（`ObservationView`，成员按 wire 顺序）——按需捕获的
桌面观察投影，快照语义、无事件形态（M1 驱动形态无观察生产者，DEC-026）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `active_application` | string | 可为空（未知应用） |
| `active_window` | string | 可为空（无前台窗口） |
| `window_geometry` | object | `{"x","y","width","height"}`，整数（全局桌面坐标） |
| `window_focused` | boolean | — |
| `focused_element` | string | 可为空 |
| `pointer_x` / `pointer_y` | integer | 全局桌面坐标 |
| `environment_state` | string | 后端环境摘要，可为空 |
| `semantic` | object（可选） | encode-when-set：请求且捕获成功时写出 |
| `visual_snapshot_ref` | string（可选） | encode-when-set：与 `visual_regions` 同现同缺 |
| `visual_regions` | array（可选） | encode-when-set：与 `visual_snapshot_ref` 同现同缺 |

`semantic`（语义快照投影，成员按 wire 顺序）：`application` / `window_title`
（均可为空）、`nodes`、`truncated`（boolean）。`nodes[i]`：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `ref` | string | 非空；"@eN"，本快照内稳定 |
| `role` / `name` / `description` | string | role 为平台稳定小写词表；name / description 可为空 |
| `parent` | integer | 节点在 `nodes` 内的下标；根为 -1 |
| `geometry` | object | 同 `window_geometry` |
| `focused` / `enabled` | boolean | — |

服务侧投影预算 1024 节点（`kObservationNodeWireBudget`）：超出以 `truncated`
显式标记（服务端快照本身完整，截断只发生在 wire 视图，RULE-07）。生产方置信度
等无 UI 消费方的字段不进入 wire（按需附加纪律）。

`visual_regions[i]`：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `ref` | string | 非空；"@vN"，可执行视觉引用 |
| `source` | string | 封闭词表（pinned VisualRegionSource 稳定名）：`ocr` / `detector` / `template` / `geometry` |
| `geometry` | object | 同 `window_geometry`（全局桌面坐标） |
| `text` / `template_id` | string | OCR 文本 / 缓存模板 id，可为空 |

`desktop.observe` 语义：`semantic` 缺省 true、`visual` 缺省 false（DEC-016：不
请求不点亮视觉面）；请求即必须——被请求组件不可交付（环境未绑定、视觉注册表
未接线或无已发布代次、捕获失败）时整个请求以 `unavailable` 失败并命名组件原因，
不返回静默残缺的观察。响应是时点捕获；快照是唯一事实源，无事件流、无回补。

按 [DEC-103](../decisions/DEC-103-pet-desktop-exclusion.md)，配置受管桌宠 owner 时，
帧/语义/视觉观察共用整窗排除租约，结束核验并恢复后才回复。视觉请求必须在排除后
经已运行的视觉管线新采集；仅有历史视觉注册表时返回 `unavailable`
（`pet_observation_requires_fresh_visual`），包括桌宠当前未启动的情况。没有配置 pet
owner 的嵌入仍兼容注册表投影。五秒预算覆盖权限、采集与组装；取消/超时和 provider
异常仍执行恢复，恢复失败不得返回成功。响应绑定本次采集的确切视觉代次；组装/恢复
期间桌面 epoch 或视觉代次变化时拒绝返回。wire 字段、版本和既有错误词表保持不变。

### 6.7 对话面载荷（session.chat.* 载荷，M5-06，DEC-027）

`session.chat` 成功载荷：`{"turn_id"}`——受理回执，对话轮异步执行；settled
回复/错误经 `session.chat_updated` 事件与 `session.chat.history` 快照投递。
`session.chat.history` 成功载荷：`{"session_id","turns":[...],"truncated"}`。

`turns[i]`（`DialogTurnEntry`，成员按 wire 顺序）：

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| `turn_id` | string | 非空；对话轮身份 |
| `status` | string | 封闭词表：`pending` / `ok` / `failed` |
| `user_text` | string | 非空；用户输入原文 |
| `reply_text` | string（可选） | encode-when-set：恰在 `ok` 时携带 |
| `error` | string（可选） | encode-when-set：恰在 `failed` 时携带（稳定失败原因，安全用于 UI） |
| `sequence` | integer | 正整数；该轮在会话对话序列中的序号 |
| `recorded_at_ms` | integer | 非负整数；规范记录写入时刻，受理初始化、终态结算更新（DEC-079） |

`session.chat` 语义：对话轮由模型层异步执行（有界长任务，Executor 可取消任
务承载），受理即回执；每会话同时至多一个在途轮（`invalid_state`），文本预
算 16 KiB（`invalid_argument`）。turn 日志是服务内存易失状态（DEC-011 持
久化挂账），会话关闭时随之移除。`session.chat.history` 是快照事实源，
`session.chat_updated` 是通知（DEC-012 一致性模型沿用）。语义（模型层装
配、凭据边界、测试承载）见 [DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)。

失败轮次的正文保留（[DEC-104](../decisions/DEC-104-failed-turn-output-retention.md)）：
`failed` 仍仅以 `error` 表达轮次失败，`reply_text` 的既有 ok-only 约束不变。
若模型已生成最终答复、随后宿主核验失败，正文作为现有 `parts` 的 text 项传输，
保留 id 为 `terminal.reply`；complete 表示文本生成完成，不表示任务成功。沿用
正文分类 16KiB、总量和 UTF-8 截断约束，`truncated` 显式标记。新 UI 将该项作为
正文，并将错误独立呈现；旧 UI 仍可按普通 text Part 显示。无新字段或版本变化。

### 6.8 权限策略面载荷（policy.* 载荷，M5-07，DEC-028）

`policy.get` / `policy.set` 成功载荷同形：`{"rules":{...},"read_roots":[...]}`。
`rules` 为全 DEC-010 能力 → 规则（"allow" / "confirm" / "deny"）映射（键即
capability 稳定名）；`read_roots` 为 filesystem.read 的资源范围（PathScope
根目录，绑定 provider 时应用——即重启后生效）。

`policy.set` 语义：`rules` 必须覆盖全部 DEC-010 能力（缺失或越词表
`invalid_argument`）；规则**立即生效**（controller 线程安全快照，与任务驱
动、atom toolset 门共享），read_roots 持久化为资源范围状态（重启生效）。
合并文档持久化到 settings store（DEC-011 Desktop Permissions 条目；已存
文档不可信即拒绝写入）。策略面为核心装备恒可用。语义与持久化边界见
[DEC-028](../decisions/DEC-028-permission-policy-face.md)。

### 6.9 任务暂停/恢复载荷（M5-10，DEC-030）

`task.pause` / `task.resume` 投影 pinned pause 家族（`pause_task` /
`resume_task`，pinned `runtime.hpp`）：epoch 递增、在途操作迟到完成按
stale 结算、驱动的 `begin_operation` 在暂停期被拒——任务驱动在下一个操作
边界驻留（取消可中断），`task.resume` 后以新 step id 重新受理并续驱
（DEC-030 决策 4）。应答载荷与 `task_cancelled` 同形：

```text
TaskPaused  = {"task_id","progress"}   # progress ∈ task.updated 词表，典型 "Paused"
TaskResumed = {"task_id","progress"}   # 典型 "Active"
```

`progress` 为命令受理时点的快照；快照事实源仍是 `task.list` /
`task.inspect`。受理是进度推进（DEC-012 决策 3）：应答离站前先发布
`task.updated`。恢复语义为 pinned 既定"重新驱动"（pinned
core-runtime.md：resume 回 Observing 再进 epoch，不支持执行级续跑）——
M1 过渡驱动以内存延续驻留/续驱，属宿主侧驱动形态，见 DEC-030 决策 4 的
边界声明。

## 7. 事件扩展（DEC-012，wire 语义自 `M1.5-02` 落地起冻结）

### 7.1 订阅

- `events.subscribe` / `events.unsubscribe`：无参数请求，订阅粒度为连接；订阅是连接级
  状态，断连即失效，不跨连接保持。响应均为通用确认形状 `{}`。
- 订阅建立后，服务端先向该连接下发当前 host 状态作为首个事件（seed，占用
  `seq=1`），随后事件按发布顺序推送；`events.unsubscribe` 幂等（退订已退连接仍
  确认）。退订前已写入连接缓冲的事件仍可能到达。
- hello `events` 能力通告（§6.1）是主探测路径；对新服务端发送订阅前无需重复探测，对
  旧服务端（无 `events` 成员）发送订阅将收到 `protocol_error`，客户端据此降级为
  `task.inspect` 轮询（能力探测失败仅作兜底）。

### 7.2 事件信封

`{"v":1,"seq":<N>,"event":"<名称>",...载荷成员}`。`v` 恒为 1；`seq` 为每连接自 1 起
单调递增的整数（事件帧不回显请求 `id`）。事件只含附加成员，不改既有请求/响应解码路径。

M1.5 事件集（封闭集合，M2+ 新事件以附加方式进入，不改既有事件字段语义）：

| event | 载荷成员（按 wire 顺序） | 语义 |
| --- | --- | --- |
| `task.updated` | `task_id`（string）、`goal`（string）、`progress`（string，§6.2 集合）、`has_success`（boolean）、`success`（boolean） | 任务创建、进度推进与终态（含取消、失败）发布；快照语义，`progress` 含义与 `task.inspect` 一致 |
| `host.status` | `status`（string，§6.1 五态） | Mira Host 状态变化即发布 |
| `events.overflow` | `dropped`（integer，非负） | 连接级事件队列溢出时发布的合成标记事件 |
| `permission.request` | `request_id`（string，非空）、`capability`（string，§6.3 词表）、`resource`（string）、`task_id`（string，非空）、`timeout_ms`（正整数） | `confirm` 规则命中且异步确认面启用时发布（DEC-020）：广播给全部订阅连接，等待 `timeout_ms` 预算内任一连接的 `permission.respond`；无应答即超时 fail closed。判定结果以 `task.updated` 终态与 `task.inspect` 步 trace 呈现，`permission.list` 是待确认快照事实源 |
| `session.updated` | `session_id`（string，非空）、`state`（string，§6.4 状态集合） | 会话进入服务注册表（open）时发布（M5-04，DEC-021），会话被关闭并移出注册表时以关闭后状态名（典型 `closed`）发布（M5-06，DEC-026 挂账②）；会话内状态不逐条广播，`session.list` 是快照事实源 |
| `session.message` | `session_id`（string，非空）、`task_id`（string，非空）、`kind`（string，§6.4 词表）、`text`（string，非空）、`sequence`（正整数） | 会话对话投影新增一条时发布（M5-04）：`user` 为任务目标入会话，`outcome` 为任务结算；`sequence` 为条目在会话事件序列中的序号；`session.history` 是含时间戳的完整投影事实源 |
| `session.turn` | `session_id`（string，非空）、`task_id`（string，非空）、`step`（正整数）、`kind`（string，§6.2 步词表）、`status`（string，结算态词表 `ok` / `failed` / `cancelled` / `skipped`） | 一个有界会话工作单元结算时发布（M5-04）：M1 驱动形态为一个脚本步，模型循环落地后为一次循环迭代；轮次开始不发布（`task.updated` 覆盖进行中语义） |
| `session.output` | `session_id`（string，非空）、`task_id`（string，非空）、`step`（正整数）、`chunk`（string，可为空）、`truncated`（boolean） | 步结构化结果的输出增量发布（M5-04）：`chunk` 受 `task.inspect` 结果同源字节预算，M1 驱动每步一份完整结果，流式生产者同形状多 chunk |
| `workflow.run_updated` | `run_id`（string，非空）、`workflow_id`（string，非空）、`state`（string，§6.5 运行状态集合）、`run_epoch`（非负整数）、`summary`（string，可选，encode-when-set） | 工作流运行状态发布（M5-05，DEC-023）：由服务从 pinned 工作流事件转译——`WorkflowRunStarted` 发布 `running`，`WorkflowRunSettled` 发布终态并携带 `summary`（pinned `safe_summary`，2 KiB 上限）；不从服务侧推测状态。`workflow.runs` 是快照事实源 |
| `session.chat_updated` | `session_id`（string，非空）、`turn_id`（string，非空）、`status`（string，§6.7 轮状态集合）、`user_text`（string，非空）、`reply_text`（string，可选，encode-when-set 恰在 ok）、`error`（string，可选，encode-when-set 恰在 failed）、`sequence`（正整数） | 对话轮生命周期发布（M5-06，DEC-027）：受理 `pending`、收敛 `ok` / `failed`；`session.chat.history` 是快照事实源 |

### 7.3 一致性模型与背压（DEC-012 决策 4、5）

- 事件是通知，不是可靠投递：`task.list` / `task.inspect` 快照始终是事实源。客户端检测
  到 `seq` 跳跃、`events.overflow` 或重连时**必须 resync**（重新 list/inspect），不得把
  事件流当作完整状态。
- 服务端承载（Executor 路由，`EXEC-02`）：`kairo::comm::Topic<TaskEvent>` 多订阅广播
  （容量与 DropPolicy 显式配置）；host 状态以 `LatestMailbox` 语义进入同一发布路径；每
  连接有界 `MpscChannel`（drop-oldest）投递到连接 blocking I/O worker。写出口径：响应帧
  优先于事件帧；溢出以 `events.overflow` 显式呈现，不静默丢弃（`RULE-07`）。
- 未订阅连接不产生任何事件帧。

## 8. Golden Vectors 一致性门禁

事实源文件：[`tests/runtime/data/ipc_protocol_golden.json`](../../tests/runtime/data/ipc_protocol_golden.json)
（手写测试数据，属源码树）。C++ 消费者 `tests/runtime/ipc_protocol_golden_test.cpp` 与
TypeScript 消费者 `ui/contracts/test/golden-vectors.test.ts` 读取**同一文件**，不得各自
复制向量。

文件结构（`meta.schema` = `"mirage-ipc-protocol-golden-vectors"`，`meta.version` 随契约
演进递增）：

- `requests[]`：`{name, id, body, canonical}`——按 `body` 编码必须逐字节等于 `canonical`；
  `canonical` 解码必须还原为 `body`（含 `id`）。
- `request_failures[]`：`{name, payload, error | error_prefix}`——非法请求的稳定错误
  字符串断言；`error` 为精确相等，`error_prefix` 用于解析失败类（C++ 解析器在
  `"payload is not valid JSON"` 后追加自身诊断，两端按前缀匹配）。`responses` /
  `events` 的失败向量同构。
- `responses[]`：`{name, response, canonical}`——`response` 为结构化描述
  （`id` / `ok` / `payload` 判别 `kind` / `error`），编码必须逐字节等于 `canonical`，
  解码必须还原。
- `events[]`：`{name, event, canonical}`——事件信封同上（TypeScript 自 `M1.5-01`
  消费；C++ 事件编解码自 `M1.5-02` 起消费 `events` / `event_failures`，
  `hello-capability` 向量的 `encode_pending` 标记已随事件编解码落地解除，hello
  `events` 成员双向断言）。
- `event_failures[]`：`{name, payload, error}`——非法事件的稳定错误字符串断言。
- `framing[]`：`{name, payload, frame_hex}`——帧编码逐字节断言（小端长度前缀）；
  `framing_failures[]` 锁定超限/坏头拒绝。

向量与本文档同变更维护：新增消息成员 → 本文档表格 → 同一变更内补 vectors 与两端实现、
测试。在门禁接入 CI 前（`M1.5-06`），提交前本地运行双端测试是强制纪律。

## 9. 版本与兼容

- 协议版本号 `v` = 1。事件订阅是 v1 的附加扩展，版本号不递增（DEC-012 决策 2）：旧
  客户端零影响（不发新 op、不收事件帧），新客户端经能力探测/稳定错误降级。
- 满足以下条件才递增 v2 并按 DEC-006 双版本并存：改变既有成员语义、删除或改型既有
  成员、改变帧格式或判别规则。
- 附加扩展纪律：新成员只增不改，编码端写出、解码端缺省容忍；本文档成员表同步扩充并
  标注引入里程碑。

## 10. 变更记录

- 2026-09-30（`M5-10`）：任务暂停/恢复面附加扩展（DEC-030，协议版本不递
  增）。§4 新增 `task.pause` / `task.resume`；§6.9（新）新增
  `TaskPaused` / `TaskResumed` 载荷形状（pinned pause 家族投影，与
  `task_cancelled` 同形）。golden vectors：requests +2、request_failures
  +2、responses +2，失败向量锁定新稳定错误串（`meta.version` 9 → 10）。
- 2026-09-28（`M5-07`）：权限策略面附加扩展（DEC-028，协议版本不递增）。
  §4 新增 `policy.get` / `policy.set`；§6.1 新增 `policy` 能力通告成员；
  §6.8（新）新增 PolicyView 载荷形状（全 DEC-010 规则集 + read_roots 资源
  范围）。golden vectors：requests +2、request_failures +4、responses +2
  含 hello 能力位、response_failures +3，失败向量锁定新稳定错误串
  （`meta.version` 8 → 9）。
- 2026-09-28（`M5-06` 第四增量）：对话面附加扩展（DEC-027，DEC-025 挂账③
  兑现，协议版本不递增）。§4 新增 `session.chat` / `session.chat.history`；
  §6.1 新增 `chat` 能力通告成员；§6.7（新）新增对话轮载荷形状（pending/
  ok/failed 封闭词表、reply_text/error 成对可选校验）；§7.2 新增
  `session.chat_updated` 事件。golden vectors：requests +2、
  request_failures +4、responses +3 含 hello 能力位、response_failures +4、
  events +2、event_failures +2，失败向量锁定新稳定错误串（`meta.version`
  7 → 8）。
- 2026-09-28（`M5-06` 第三增量）：会话管理面 `session.close`（DEC-026 挂账②
  兑现，协议版本不递增）。§4 新增 `session.close`；§6.4 新增关闭语义（pinned
  `close_session` 承载 + 注册表条目移除 + 主会话 `invalid_state` 守卫）；§7.2
  `session.updated` 增补关闭发布点。golden vectors：requests +1、
  request_failures +2、responses +1、response_failures +2，失败向量锁定新稳
  定错误串（`meta.version` 6 → 7）。
- 2026-09-28（`M5-06` 第二轮）：观察面与工作流定义读取面附加扩展（DEC-026，
  协议版本不递增）。§4 新增 `workflow.get` / `desktop.observe`；§6.1 新增
  `observation` 能力通告成员；§6.6（新）新增 `WorkflowDefinitionView` 载荷
  （head 定义内容自服务侧产品目录，pinned 库无定义正文读取 API）与
  `ObservationView` 载荷（帧成员 + 语义快照投影（1024 节点 wire 预算 +
  `truncated` 显式标记）+ `visual_snapshot_ref` / `visual_regions` 同现同缺
  的视觉承载——M3 非目标"视觉参考进入 UI 观察面"兑现）。golden vectors：
  requests +3、request_failures +4、responses +4、response_failures +5，
  失败向量锁定新稳定错误串（`meta.version` 5 → 6）。
- 2026-09-27（`M5-05`）：工作流契约面附加扩展（DEC-023，协议版本不递增）。
  §4 新增 `workflow.list` / `workflow.save` / `workflow.publish` /
  `workflow.delete` / `workflow.atom.catalog` / `workflow.runs` / `workflow.run` /
  `workflow.cancel`；§6.1 新增 `workflows` 能力通告成员；§6.5 新增
  `WorkflowSummary` / `ExposedTool` / `WorkflowRunSummary` 载荷形状；§7.2 新增
  `workflow.run_updated` 事件。golden vectors：requests +8、responses +9、
  events +1，失败向量锁定新稳定错误串（`meta.version` 4 → 5）。
- 2026-09-26（`M5-04`）：会话与消息契约面附加扩展（DEC-021，协议版本不递增）。
  §4 新增 `session.list` / `session.open` / `session.history`，`task.submit`
  增补可选 `session_id` 参数与 `TaskSubmitted.session_id` 回执；§6.1 新增
  `sessions` 能力通告成员；§6.4 新增 `SessionSummary` / `SessionHistoryEntry`
  载荷形状；§7.2 新增 `session.updated` / `session.message` / `session.turn` /
  `session.output` 事件。顺带修正 §4 权限行的两处 §6.4 引用为 §6.3（M5-03
  时的编号错位）。golden vectors：requests +4（含 `task.submit` 会话绑定形
  态）、responses +5、events +4，失败向量锁定新稳定错误串（`meta.version`
  3 → 4）。
- 2026-09-26（`M5-03`）：权限异步确认面附加扩展（DEC-020，协议版本不递增）。
  §4 新增 `permission.respond` / `permission.list`；§6.1 新增 `permissions`
  能力通告成员；§6.3 新增 `PendingPermission` 载荷形状；§7.2 新增
  `permission.request` 事件。golden vectors：requests 增补两条 permission
  向量、responses 增补 `permission-responded` / `permission-list`、events
  增补 `permission-request`，失败向量锁定新稳定错误串（`meta.version`
  2 → 3）。
- 2026-09-17（`M1.5-02`）：事件订阅落地，§7 事件帧格式与订阅语义冻结。§6.1
  `events` 能力成员与 §7 的 `events.subscribe` / `events.unsubscribe` 进入两端实现
  （golden vectors：requests 增补两条订阅向量；原 `unknown-op-events-*` 失败向量
  作为旧服务端行为已过时，替换为对新旧编解码器恒真的 `unknown-op-registry-edit`；
  `hello-capability` 的 `encode_pending` 解除；vectors `meta.version` 1 → 2）。
  §7.1 补记订阅 seed 语义（首个事件为当前 host 状态，`seq=1`），与前端 mock 的
  Enqueue-after-subscribe 行为一致。
- 2026-09-16（`M1.5-01`）：初版。按 `runtime/ipc` 现状（`protocol.hpp` / `protocol.cpp` /
  `framing.hpp`、`runtime_service.cpp` 错误面）与 DEC-012 事件扩展（Accepted）整理；
  golden vectors 门禁随本变更落地（C++ + TypeScript 双端测试）。


## DEC-034 原生模型与通用 harness 附加面

v1加法扩展，旧session.chat缺省仍是单次纯文字推理，旧编码不增加agent:false。
新客户端发送`agent:true`启用通用harness；此值必须是bool。接纳仍返回turn_id，
终态通过session.chat_updated及session.chat.history呈现，接纳不代表执行成功。

| 请求op | 参数 | 成功payload与约束 |
| --- | --- | --- |
| model.get | 无 | model_settings：schema=1 LocalSettings JSON字符串，仅model块 |
| model.set | settings：上述JSON字符串，最多65536字节 | 同model.get；只允许model块，有活动轮次时invalid_state |
| session.chat.cancel | session_id | turn_id；协作取消，终态通过原有轮次事件/历史返回 |

服务校验origin、prefix、model ID、dialect、凭据引用/旧环境变量名及字段预算；
DEC-038 直接密钥只通过本地 model.set 的 write-only api_key 参数进入，不进入响应或普通设置。
保存保留既有配置其他块，写入失败不替换当前模型。agent轮次以MiraRuntime任务身份执行，
全服务同时最多一个agent轮次；有活动agent时旧dialog也忙拒绝。当前wait工具的受限
文本反馈契约及依赖缺口见DEC-034 / MIRA-20261004-001。M7 追加按需结构化桌面工具，
DEC-055 另接授权截图与规范图片输入；不能据此
宣称 task.submit 设备闭环或业务验证已迁入原生 UI。旧golden仍兼容，新面由native_agent_integration_test覆盖。


### DEC-036：可选上下文用量投影（M6-06）

`session.chat.history`各turn及`session.chat_updated`可选携带`context_usage`：
`{"input_tokens":391,"window_tokens":128000,"model":"Qwen/Qwen3.5-4B"}`。
仅成功`ok`轮次携带；input_tokens为最后一次模型请求的Exact/ProviderReported输入用量，
范围0–2000000000；window_tokens为显式配置预算（0表示未知，非零2048–2000000），
model为非空≤1024bytes模型ID。未知用量整对象省略，旧帧不变；缺字段可解析。
有对象时错误类型/负值/越界/错误状态拒绝。不得从轮次累计或bytes推算Token。
用量随现有事件推送，可经history补读。

M6-43（2026-10-18）新增会话级快照：`session.chat.history` 顶层在 `session_id` 后依次
编码可选 `context_usage_sequence`、`context_usage`，再编码 `turns`、`truncated`。
快照不受返回轮数与帧预算截断影响。`context_usage_sequence` 为非负整数，0 表示尚无
成功请求；正数表示最近成功请求或编辑替换的轮次序列。仅在正序列下允许 `context_usage`，
其对象边界与轮次内相同。缺少用量且有序列表示明确未知；两字段都缺少时兼容旧服务。
成功缺少用量清空旧值，失败/取消和普通 pending 保留上次值，编辑替换清空并推进序列。
UI 同步历史时保留较新事件的快照，防止同一 pending 轮次结算后被迟到的历史响应回退。
`sessions/<id>.json`（schema 1，DEC-101）在对应会话内保存同名字段，结算/替换/有序退出沿既有原子保存
路径落盘，启动恢复到会话，再经上述顶层字段回填 UI。旧文件缺字段仍可读取，显示未知；
旧轮次没有供应商用量时不能追溯重建。该增量未承诺旧版本严格解码器可读取新文件。
Golden meta.version=22 增加已知/未知/初始快照与非法序列、缺失序列、负输入向量，
既有帧的规范字符串保持不变。

`model.set`请求的settings及`model.get/set`响应的model_settings（均为JSON字符串）
中model块新增可选
`context_window_tokens`整数；缺失/0表示未知，其余2048–2000000。非零预算应用至Mira
ProfileLimits。未配置保留原运行默认，前端仍报告分母未知，不把默认当供应商容量。
保存/停用沿既有模型设置与Executor路径；活动会话运行中不允许修改模型配置。


### DEC-102：缺用量时的请求输入估算（M6-45）

`context_usage` 在 `model` 后新增可选布尔 `estimated`。true 表示本地保守输入估算，
false/缺失表示供应商 Exact / ProviderReported。编码 false 时省略，原帧的规范字符串
不变；存在但非布尔时拒绝，错误为 `invalid context_usage estimated flag`。
该字段适用于成功轮次事件、轮次历史、会话顶层快照和 schema 1 的逐会话文件。
有效报告优先，包括真实0；报告缺失或质量不足时，复用 pinned Mira 的
`estimate_input_tokens(ModelRequest)`，输入是实际模型请求，工具循环取最终回答调用。
不改变 ModelResponse.usage、预算对账、供应商报告锚点或调度/取消行为。
UI 对估算值显示“约”和“本地估算”，窗口预算未知时仍显示输入 Token；已有会话无
完整请求及计数数据时保持未知，不能从裁剪后的显示消息重建精确值。
Golden meta.version=23 增加估算快照与非法标记类型，旧向量保持原字节。


### DEC-037 增量：原生配置目录与提交选项

`model.get/set` 的schema=1 JSON字符串新增可选 `models` 数组（至多12个ModelSettings）。
每项display_name非空、至多128bytes且唯一，字段各至多2048bytes；整文档至多64KiB。
`model`仍表示当前应用配置；`supports_reasoning`为可选布尔，默认false。
目录保存和活动配置同一model.set完成，保留其他配置块。DEC-042更新：省略models保留已有目录，显式空数组清空目录，
支持旧客户端；每次切换拒绝活动模型任务。重启从service.json恢复目录。

`session.chat`新增可选`access`（default/read_only/chat_only，缺省default）、`reasoning`
（空字符串/minimal/low/medium/high，缺省空）。未知值/错误类型decode拒绝；
未声明supports_reasoning时带reasoning的请求在admission前回unavailable。
M7-01/02/03 / [DEC-054](../decisions/DEC-054-scoped-complete-desktop-actions.md) 更新：
read_only 仅提供实际可用的窗口/应用/语义观察；default 另有 wait、窗口激活、
语义 click 与完整 key/type_text。目录取决于 Provider、完整释放/scope 能力与门禁，
每次动作仍由共享 PermissionController 判定。
此增量不新增 IPC 字段；会话工具 schema v2 要求 window_id、observation_scope、
environment_epoch，click 使用当前语义 ref。输入 Part.input 不复制文本，仅含窗口、
text_bytes 与 scope/epoch；output 为 schema=2 的结构化回执：status 为 rejected、
dispatched 或 uncertain，verified=false、side_effect_may_have_occurred、input_released。
错误还含稳定 error_code 和安全 message，经失败工具结果/既有 Part 输出投影。
completed 工具过程状态表示 handler 结束，不能代替业务完成证据。原有 workflow schema
保持兼容；四个修改工具 required 字段由 action_contract_test 的 golden 文件锁定。
选择和文本随请求冻结，对已有运行轮次不生效。文本附件使用现有text载荷，受同一16KiB
限制，明确来源为用户主动选择的文本，并非文件上传协议或自动文件访问。


### DEC-038 增量：直接凭据与持久化删除

| 请求 op | 参数 | 成功载荷与失败语义 |
| --- | --- | --- |
| model.set | settings 同前；可选 api_key string | 同 model.get，可选 warning string 表示配置已应用但旧凭据清理失败 |
| session.delete | 非空 session_id | {session_id, deleted:true}；not_found、invalid_state（活动轮次/任务）、internal（持久化失败） |

api_key 为 write-only，最多2048个ASCII字节（33–126）；缺省保留凭据，空字符串显式移除。
settings/model_settings 的 model 与 models 项新增可选32位小写十六进制 credential_ref，
以及布尔 api_key_configured（服务端投影，不代表凭据已通过供应商验证）。无明文 key 成员。
旧 credential_env 继续兼容；界面改用直接 Key。密钥仅存系统 Secret Service / Credential Manager；
新引用写入后原子保存配置，保存失败清理新引用并保留原活动模型，清理失败对调用方可见。
成功后的旧引用清理失败通过 warning 提示，调用方必须呈现；不得把 warning 当作保存失败。

session.delete 对无活动任务/对话的普通会话复用 pinned Mira close 并清除服务注册、产品对话与
持久化快照。历史遗留主会话保留身份和任务审计，只清除产品对话。写盘失败返回错误并保留可见历史；
成功后普通会话不再出现在 session.list，主会话仍存在但对话为空。与 session.close 的原有语义分开。
UI 对空列表和空对话不建立历史行；新草稿首次发送才 session.open，已删除本地 ID 不接受迟到更新。
新增 wire golden 覆盖写 Key、移除 Key、删除请求/响应和带 warning 的配置响应。

### DEC-039 增量：最后一轮替换

| 面 | 新增可选字段 | 语义 |
| --- | --- | --- |
| session.chat 请求 | replace_turn_id:string | 非空且≤128字节；只能精确匹配当前会话最后已终结轮次 |
| session.chat 成功ACK | replaces_turn_id:string | 被替换旧轮次ID；turn_id为新轮次 |
| session.chat_updated 事件 | replaces_turn_id:string | pending和终态均携带，订阅者据此移除旧消息对 |

字段缺省时保持旧v1 canonical编码。非法类型、空值、超长ID在解码层拒绝；目标过期、
非末轮或正在运行返回invalid_state。先完成Mira Task接纳、Executor提交与替换持久化，
才发布pending和ACK；失败恢复旧记录并返回unavailable，绝不提前删除调用方历史。

新轮次sequence继续单调增长。模型transcript排除旧轮次用户文字/回复，更早历史保留；
这不是外部工具事务回滚，旧Task审计保留。新轮次推理失败仍是被接纳轮次的失败。
UI用替换ID和序列抵御迟到旧回复，session.chat.history为事实快照，旧ID不再存在。
持久化不需要新字段：保存的是已替换后的有界轮次，重启后用稳定产品ID继续历史harness。
运行Mira会话由串行服务延迟重新打开并映射，Task/OperationContext使用当前运行ID；
session.close/delete关闭映射目标，旧桌面task.submit面不在该恢复范围内。

Wire golden覆盖缺省兼容、替换请求/ACK/事件及非法目标，服务集成测试检查Mira真实请求
和写盘拒绝、并发拒绝、取消、重启编辑/续聊/删除。见
[DEC-039](../decisions/DEC-039-conversation-selection-and-revision.md)与
[Linux验收](../compatibility/native-conversation-revision-20261005.md)。


## DEC-041：可选会话流式预览

`events.subscribe`新增可选boolean `chat_preview`（缺省false，旧编码不增加字段）。
仅声明true的连接收到`session.chat_preview`；未知类型/非法bool拒绝。旧服务忽略此可选
字段，客户端仍可依赖最终会话事件工作；当前服务对旧客户端过滤预览且不增加其事件seq。

事件字段为session_id、turn_id、request_id（非空，各≤128bytes）、sequence（正整数，
轮内递增）、text（≤16384bytes完整快照）、truncated（bool）。预览来自Mira的非权威
公开sink，经既有有界Topic/服务循环传递。队列溢出沿用events.overflow与历史恢复。
每次推理重试先发空快照；客户端仅替换匹配pending轮，拒绝旧序列/终态/取消后的内容。
预览不存盘、不推动工具、不修改context_usage；最终成功ChatTurnUpdated才携带规范reply_text。
协议版本仍为1，其他请求/终态契约不变。测试覆盖新旧订阅同场及字段拒绝。


### DEC-042增量：服务商元数据与目录存在性

model/models条目增加可选provider_id/provider_name（字符串）；无字段的旧目录视为单模型服务。model.get回送所选模型的服务元数据和显式models数组。同provider_id共享Base URL（endpoint/api_prefix）、dialect和credential_ref/credential_env；model.set以当前model的连接字段为准统一更新同服务条目。模型目录仍限制12条，display_name为稳定且独立的模型配置身份。空模型服务以enabled=false保存，选择真实模型后才可运行。缺失models保持旧目录；空数组表示清空，旧客户端无字段行为保留。持久化成功后才切换应用状态并清理不再引用的Key，清理失败以warning回送。

## DEC-045 增量：托盘产品控制（M6-25 / golden v13）

协议版本仍为 1，已有 canonical 不变。`hello` 增加可选布尔 `tray`，wire 顺序位于
`protocol` 后、`events` 前：缺失表示 headless/旧宿主，false 表示产品托盘尚未注册，
true 仅在真实注册且存活时返回。原生前端只接受 true，并验证自己为该托盘持有的 PID。

请求顺序为 `v,id,op,action,exit_epoch`，`op` 为 `product.control`；`frontend_ready`
另在末尾携带正整数 `frontend_pid`，其 `exit_epoch` 必须为 0。action 闭集：

| action | 语义 |
| --- | --- |
| status | 获取事实快照 |
| open | 启动或复用单个前端，递增 window_epoch 并激活 |
| frontend_ready | 仅当前所属前端 PID 可报告 UI/IPC 就绪 |
| quit | 无活动工作时退出；有活动时产生待确认 exit_epoch 并打开前端 |
| confirm_quit | 仅当前待确认 epoch 有效，取消工作并关闭应用 |
| cancel_quit | 仅当前待确认 epoch 有效，清除确认并保留工作 |

成功响应字段按序为 `v,id,ok,product,frontend_pid,window_epoch,exit_epoch,active_work,
exit_pending,frontend_ready`。product 必须为 true；三个整数状态及计数非负，两个布尔
字段必需；PID=0 表示无前端。active_work 包括未终态任务、在途 Agent 轮次与非终态
Workflow（无法查询的工作保守计为活动）。状态由 Runtime 串行上下文生成，客户端
通过既有 HostStatus Topic 通知/重同步取快照，不根据事件自行推断退出资格。

未知 action/负 epoch 解码错误为 `invalid product action or exit epoch`；ready 缺失或
非法 PID、非零 epoch 为 `frontend_ready requires a positive frontend_pid and zero
exit_epoch`；非法快照为 `invalid product state`；hello 非布尔 tray 为 `hello tray must
be boolean`。运行中无托盘、错误前端 PID、过期确认均按既有失败响应返回
invalid_state 与诊断。退出和通知区故障不绕过 Runtime 有序取消/shutdown。

### M6-26 思考选项投影（DEC-046）

模型配置 settings_json 的 model 与 models 条目可含 reasoning_options 字符串数组，
最多8项、每项最多16字节。空字符串表示提供商默认，其他值由服务推导。
这是只读投影，model.set 忽略客户端列表；session.chat.reasoning 经实际模型能力校验。
当前新增 none/adaptive/xhigh/max；是否提供由具体模型决定。新增字段要求同步升级原生前端的设置解码器；
新 UI 缺失列表时只显示默认，不猜测未知厂商能力。wire version 保持 v1。

### M7-04/05 识图声明与截图（DEC-055）

schema=1 的 `model` 与 `models` 条目新增可选布尔 `supports_images`。缺失/false 为
关闭；编码时 false 省略，保持旧 golden。错误类型严格拒绝；旧版严格解码器不能
读取 true 字段，服务与前端须同步升级。model.get/set 持久化与投影包含该字段，
它是用户能力声明，不代替截图权限，也不表示所有供应商已经在线验证。

启用模型且实际 Screen Provider/门禁齐备时注册 `desktop.screen.list_displays` 和
`desktop.screen.capture`；read_only 可执行经授权截图，default 的修改工具保持
DEC-054。capture 参数为 source（display/window/roi）、source_id；roi 另有全局像素
x/y/width/height，必须落在指定显示器内。示例和预算见
[桌面安全契约](../security/desktop-actions.md#图片采集与模型输入)。
工具 output 仅含 PNG artifact 身份、敏感级别、来源、尺寸/原点、1:1 变换与
task/environment epoch、时间及 verified=false；Part 不携带图片/base64。真实图片
在 Adapter 的规范 User ImagePart 中供给当前请求，不存入普通会话历史。缺图、错误
身份、读取失败或 drop/degrade 不能产生识图成功；现有 failed 终态表达失败，无新增
IPC 事件。Answered 仍只表示终态回答，修改动作还须通过下述宿主核验门禁。

### M7-08 动作后条件与过程证据（DEC-056）

四个会话修改工具版本为 2.1.0，v2 required 字段不变。新增可选 verify_kind
（window_focused/window_title_equals/semantic_node_exists），后两者分别配
expected_title 或 expected_role/expected_name，各最多 1024 UTF-8 字节。
激活缺省检查前台窗口；其他动作无条件可派发，但不能以 Answered 结束为成功。
default 且窗口/动作上下文可用时另注册只读 desktop.action.verify(action_id)，仅重验
本轮最近动作的固定条件。未验证动作阻止下一修改，不确定副作用保持本轮拒绝。

回执新增 action_id 和 verification 对象：status 为 verified/unverified/uncertain/rejected，
含 reason、verify_kind、condition_digest、evidence_id、observed_digest、captured_at_ms
与 environment_epoch。观察摘要来自实际窗口/几何/匹配数与固定条件，时间戳用于展示，
新鲜度用单调时钟。文本/语义期望不复制到 Part.input；普通日志仅含摘要，规范模型
往返仍含声明条件。各 Part 16 KiB、总内容 64 KiB，截断不可变成已核验。

不增加 IPC 字段/事件或过程状态枚举；前端从既有 Part.output 解析证据状态与目标。
宿主以规范失败 ToolResultPart 和新观察为事实源，过程投影耗尽不能绕过失败门禁。
终态再次核验最近动作，拒绝/未验证/不确定/失败转现有 failed，Cancelled 优先，
迟到响应不能恢复成功。通过时回答明确只核对声明的局部条件。范围与实际测试见
[M7-08 验收](../compatibility/post-action-verification-20261009.md)。

## 会话过程快照（2026-10-07，DEC-049）

`session.chat.history` 的 turn 与 `session.chat_updated` 增加可选 `parts` 数组和
`process_sequence` 非负整数。缺省为空/0；原 `sequence` 仍表示轮在会话中的位置。
process_sequence 按同轮更新递增；pending可重复发布，ok/failed终态只收敛一次。
消费者用 process_sequence 丢弃过期快照，终态后拒绝 pending 更新；丢事件用历史恢复。

每段有 `id`（1..128字节）、`kind`（thinking/text/tool）、`status`
（pending/running/complete/failed/cancelled），可选 text/name/input/output 与 truncated 布尔。
非工具段只允许 complete 与 text；工具必须有 name，不携带 text。ID 在一轮内唯一。
最多96段；name最多128字节，text/input/output各16KiB、总内容64KiB。
超限显示投影截断且标记 truncated，wire超限/错误类型/重复ID拒绝。
思考签名和 redacted 数据不投影，不从一般正文识别工具或思考标签。

完整快照经既有 Executor Topic 推送，服务记录始终可从历史读取。参数只是工具请求，
只有 handler 开始才标 running，返回结果才标 complete；未执行/异常/取消明确收敛。
纯文本预览仍是瞬态 ChatPreviewEvent；工具响应完成后清除其过期正文预览。
过程与终态同存于 session-state 的每轮可选字段；旧记录不含字段仍可加载，旧版严格
持久化读取器遇到新增字段会拒绝，禁止以旧版覆盖新记录。
公开 ModelPreviewSink 仅有正文，思考在模型步骤规范响应到达后出现。

带过程的历史按实际编码后的1MiB帧预算保留最新轮、维持轮序，truncated同时表示因
条数或帧预算而省略旧轮；最新单轮超限明确unavailable。会话存储文件与JSON
读取同限8MiB，超过时沿用显式存储失败，不增大到无界容量。

### M6-33 模型目录发现（DEC-051）

`model.list` 请求：`provider_id`（至多256字节）、`endpoint_origin` 与 `api_prefix`
（各至多2048字节）、`dialect`（至多64字节），可带 write-only `api_key`
（至多2048个可打印ASCII字符）。成功响应为 `model_ids` 字符串数组，最多256项，
每项1–1024字节；不会返回 Key。错误沿用 `invalid_argument`、`unavailable`、`internal`。
未传 Key 时，服务仅从已保存目录中精确匹配 provider_id、origin、prefix 和方言后解析凭据；
地址编辑后不能借用旧服务 Key。请求显式触发、候选不自动落盘，旧客户端不受影响。
Mira 公共传输强制响应大小、期限和端点策略；Runtime Service 使用 Executor 任务执行，
关闭时协作取消与排空。`model.get/set` 的原语义保持。

## M7-06 显式 OCR 装配（DEC-057）

ServiceConfig.ocr_manifest / mirage-service --ocr-manifest 提供经过摘要与大小校验的
本地模型、字典及配置。构建 MIRAGE_WITH_PPOCR=OFF（默认）或资源不合法时启动失败，
不会默默把 OCR 标成可用。开启后 desktop.observe 的 visual 请求先走 screen.capture
权限并刷新 Runtime 所有的单显示器 PP-OCR session；失败返回 permission_denied 或
unavailable。取消/权限/分析共享 5 秒预算，显示器变化或 epoch 变化拒绝结果。
原 IPC 类型不变，未配置模型时沿用既有外部 registry 行为。

Agent 会话动态工具增加 desktop.vision.ocr（无参数、无输入副作用），返回 source=ocr、
scope/revision、全局框、非可信文字和 verified=false；工具过程摘要不包含 OCR 全文。
失败进入规范 ToolResult 和会话终态门禁。尚无视觉点击/SoM工具。资源契约、已执行验证
和真实质量限制见[DEC-057](../decisions/DEC-057-real-ocr-composition.md)及
[装配证据](../compatibility/real-ocr-composition-20261009.md)。

## M7-07/09 会话视觉动作补充

后续 DEC-058/064 增量已提供 `desktop.vision.mark` / `click` / `scroll` / `drag`。
它们属于 Mira 会话动态工具，不新增直接 IPC 注入端点；沿现有 session chat 过程
和动作 verification 回执传送状态。mark/click 及图片供给见设计文档 M7-07 小节。
scroll/drag 1.0.0 请求/回执和错误契约见
[桌面动作策略](../security/desktop-actions.md#有界滚动与完整拖拽)；权限、能力缺失
不注册、取消与终态门禁不变。后端支持声明仅限本轮验证的 Linux X11，见
[验收记录](../compatibility/bounded-scroll-and-drag-20261010.md)。


## M7-12 桌面视觉设置（DEC-066）

`vision.get` 无参数；`vision.set` 必须携带 `ocr_manifest` 字符串，最多 4096 字节，
不得含控制字符，服务要求绝对路径或空字符串。空字符串在下次启动关闭 OCR。
两者响应相同：`vision_manifest`（保存值，未保存时取启动值）、`active_manifest`
（本次启动值）、`vision_supported`（编译能力）、`vision_running`（实际管线）、
`restart_required`（保存路径与本次启动值不同）；这些字段始终写入，布尔不能用
缺失代替 false。IPC v1 追加操作，既有操作和帧格式不变；golden v15 锁定顺序。

```json
{"v":1,"id":113,"op":"vision.set","ocr_manifest":"/home/user/vision/manifest.json"}
```

设置经现有服务 IPC owner 合并写回，保留模型、目录、权限及 Runtime 字段，不切换
运行中管线。模型设置面拒绝夹带 vision 块；没有写回存储返回 unavailable，坏旧文件
返回 invalid_state，读写失败返回 internal；失败不报告保存成功。路径有效不代表资源
已审核/加载成功，权重身份仍在启动时严格校验。具体边界见
[DEC-066](../decisions/DEC-066-product-desktop-vision-settings.md)。

已启用 OCR 的 `desktop.observe(visual=true)` 保留五秒预算。失败仍返回 `unavailable`，
消息可附 `cancelled_or_expired`、`desktop_epoch_changed` 或有界 backend 原因，不能把
`vision_running=true` 解读为每次观察都成功。

DEC-070 不增加 IPC envelope 字段。桌面动作的失败 Part.output 可包含本轮宿主
投影的完整 verification；canonical 工具错误为最多 512 字节完整 JSON，含 action_id、
派发/副作用/释放状态和有界原因。Part.input 的条件正文继续隐藏，并增加 condition_fields
记录字段名及 string/empty_string/null/non_string 类型，不保存条件值。旧历史不迁移。
早先失败卡片保持失败；后续已纠正并验证的动作可使轮次成功，最终未知或不确定结果仍失败。

### DEC-071 增量：应用交付与凭据清理

model.set 的成功响应和 warning 语义保持：配置已应用后才清理旧凭据；删除不存在的
钥匙环条目返回成功。读取缺失/锁定/歧义与服务失败不同，不代表供应商 Key 失效。
Linux GNOME 默认 session 集合改用服务枚举出的 login 持久集合保存新 Key；失败不
降级到临时集合/文件。已有临时 Key 需新版重新保存，无用户配置 schema 迁移。

会话工具 application.list v2 返回 observation_scope / environment_epoch / captured_at_ms；
独立于窗口观察，绑定当前会话和五秒时效。application.launch v2 和可选 open_uri v2
要求 application_id、同列表 scope/epoch，open_uri 另需 HTTP(S) uri，最多 2048 字节。
两者固定 verify_kind=application_running，不接受窗口或页面后条件。派发/失败与
action.verify 使用既有 schema2 和 ≤512 字节失败回执。verification 新增可选
application_id，window_id 在应用动作中为空；原生卡片显示“应用进程”，局部 verified
不证明页面或业务成功。工具输入持久投影移除 uri，保留 uri_digest/uri_bytes。

### DEC-072：GUI 区域输入证据

通用 harness 内部增加 desktop.vision.click_region；conversation.parts 继续承载其
输入/回执，不增加 IPC 方法。verification 可带 display_id，显示上下文的 window_id
可空；原生只读投影识别该动作并显示显示器目标。新 verify_kind 为 foreground_changed、
foreground_title_equals、display_text_exists，均为局部条件，不能代表任意目标完成。
工件、scope、比例和区域坐标都是本轮输入，不作为持久知识或跨会话执行权。

## 记住桌面授权（DEC-078，v1 追加）

请求 `desktop.authorization.get` 无附加参数；`desktop.authorization.set` 必须包含
布尔 `remember`。true 保存偏好并请求系统授权，false 关闭复用、使旧会话输入失效并
删除本机恢复令牌。设置操作严格合并现有配置，损坏配置拒绝改写。
成功响应包含字符串 `desktop_authorization`、`supported`、`remember`、`remembered`
与最长 512 字节的 `notice`。desktop_authorization 为 unsupported / ask / authorizing / remembered /
needs_authorization / store_unavailable / cancelled。remembered 必须同时 supported、
remember 且 desktop_authorization=remembered；其含义为可尝试恢复，并非系统权限永不失效。
一个管理请求在途时返回 desktop_authorization_busy；存储、系统拒绝及取消返回明确
错误。等待由 Runtime Executor 有限可取消任务承载，future 进入统一关闭与失败视图。
UI 对此使用 40 秒 IPC 等待；Portal 自身 30 秒阶段总期限不改变。令牌不进入 IPC。

### 历史记录时间与列表排序（DEC-079）

DialogTurnEntry.recorded_at_ms 表示该轮规范记录写入时间：接纳时初始化，终态
结算时更新，之后读取历史不改写。原生列表使用既有字段恢复最近更新顺序；
较早版本持久化的值仍有效，无 schema 或 wire 字段变更。pending 不跨重启保存，
实时流式及过程活动在 UI 内排序，终态结算记录时间随现有历史持久化。

### DEC-080 / DEC-081：模式与原生源帧（2026-10-12）

`session.chat.access` 闭集为 default（完全访问）、read_only（视觉模式）、chat_only
（对话模式）。chat_only 在服务端强制走纯对话路径，即使 agent=true 也不创建桌面
harness 任务、工具或图片供给；read_only 只注册已可用观察，不允许修改。旧值兼容，
不认识新值的旧服务显式拒绝；golden v17 增加 chat_only，既有字段顺序不变。

原生 capture / OCR 模型工具回执在可交互时返回本次独立 image_scope 与
input_actionable。OCR 的 region actionable=false 仍只表示观察框不自带可执行引用。
source_sequence 是会话内源帧序号，不是跨任务标识或已知捕获时间；动作后验证要求
大于执行前序号，普通观察可读取当前流中最新自有画面。旧 image_scope 明确拒绝为
stale_image_scope，不能自动将旧目标重绑定到新图。没有新增控制面 operation。

## M8-01：mira_pet 只读状态（DEC-084，v1 加法扩展）

`hello` 响应可带布尔 `pet_status`。支持本扩展的 Runtime 写出 true，缺席的旧服务
按不支持处理；非布尔值解码失败。`pet.status` 请求无参数，响应在成功信封后写出
`"pet":true` 和完整 `PetStatus`。`events.subscribe` 可带布尔 `pet_status`，缺席或
false 不订阅桌宠状态；编码端仅在 true 时写出此成员。订阅者收到的事件名为
`pet.updated`，内容与 `pet.status` 响应中的状态字段相同。原有订阅和 golden
编码保持不变。
托盘配置了受管桌宠时，既有 `product.control(status)` 响应在原末字段之后追加非负
整数 `pet_pid`；没有桌宠载体时缺席，旧客户端忽略该可选字段。桌宠进入 UI 前须核对
`hello.tray`、`hello.pet_status` 和本机 PID 等于 `pet_pid`；这只是同一机器上的产品
子进程身份检查，不是认证外部不可信 IPC 服务的机制。

### M8-05：桌宠独立启停与 bootstrap（golden v20）

配置了桌宠载体的 `ProductState` 在 `pet_pid` 后加 `pet_state`、`pet_diagnostic`。
两字符串成对出现；state 闭集为 stopped/starting/running/stopping/failed，diagnostic
最多 512 字节，可为空。无载体时三字段缺席；只有旧版 `pet_pid` 的响应仍可解码，旧
客户端忽略新字段。`pet_state` 存在是独立控制能力标识，新客户端先检查再发操作。

`product.control` 增加 `pet_start`、`pet_stop`、`pet_ready`，epoch 必须为 0；
ready 在末字段携带正整数 `pet_pid`。start 复用 starting/running 的所属进程，stop
重复请求复用同一个在途关闭；停止期间 start/ready 拒绝为 invalid_state。停止请求
的成功响应只表示有限 Executor 任务已接纳，之后轮询 state；不表示进程已经退出。

启动后 state=starting；只有当前所属存活 PID 在窗口平台准备、IPC 订阅及健康/动画
timer 均建立后报告 ready，才进入 running。该状态不代表素材视觉验收或帧已解码。
10 秒未收到 ready 时有界关闭并报告 failed，不自动重启。退出/崩溃也报告 failed，
用户可显式 start 恢复。正常 stop 不取消 Agent、会话或主窗口；所有进程 handle 访问
串行，关闭任务持有 handle 期间快照只读缓存 PID。owner shutdown 先停止生产者，
经串行 fence 禁止新控制，再消费在途关闭 future，最后回收所属进程。

非法 ready 为 `pet_ready requires a positive pet_pid and zero exit_epoch`；start/stop
非零 epoch 为 `pet control requires zero exit_epoch`；非法/缺失诊断对或状态闭集为
`invalid product pet lifecycle`。载体缺席、错误 PID、stopping/closing 冲突返回
invalid_state 与明确诊断。服务端继续保留窗口/输入守卫，未将本控制面当作 M8-06
的目标避让协议。

`PetStatus` 字段按以下顺序写出，所有字段必有：`schema_version`（整数 1）、
`owner_epoch`（非空 ≤128 字节）、`revision`（正整数）、`host_state`（stopped /
starting / running / stopping / failed）、`display_state`（starting / idle / thinking /
looking / mouse / typing / task_success / task_failure / exiting / waiting / paused /
takeover / cancelled / disconnected / unknown）、`source_kind`（task / turn /
workflow 或空）、`source_id`（与 source_kind 同时为空或同时非空，≤128 字节）、
`session_id`（≤128 字节）、`active_count`（0～1024）、`summary`（≤256 字节）、
`reason_code`（≤64 字节）、`elapsed_ms`（非负整数）。整数不能超过有符号 64 位
上限；含状态的 JSON 对象编码后不得超过 4096 字节。字段类型、范围或 marker
无效均拒绝为 `invalid pet status`，不使用部分字段继续显示。

`owner_epoch` 标识本次 Runtime owner，`revision` 只在展示事实变化时增加；
`elapsed_ms` 是服务端同一显示阶段的持续时长，不是可跨进程比较的时钟时间戳。
`source_id` 用于结果归属，摘要只写阶段文字，不携带用户输入、思考正文、截图或凭据。
旧 epoch、倒退 revision、断连与事件 seq 缺口由客户端丢弃并重新获取快照；订阅建立
后再读快照，按 epoch/revision 合并。当前托盘 Runtime 的既有 500 ms 产品维护
周期会发布变化；headless 服务无此产品周期，客户端须读取 `pet.status` 才推动
快照与事件。此扩展不承诺实时性；looking/mouse/typing 只有真实子阶段证据进入后
才会出现，未知执行显示 `unknown`。


<a id="pet-bubbles"></a>

## M8-11：可选实时气泡（DEC-089）

`hello` 可选布尔 `pet_bubbles`，本实现写 true。无字段为旧服务；非布尔拒绝。
`PetStatus` 在 `elapsed_ms` 后可选对象 `bubble`，缺席表示清除气泡，旧向量编码字节不变。
字段顺序为 kind、content_origin、request_id、text、sequence、truncated、final、remaining_ms。
kind 为 thinking/dialog；origin 为 status_hint/model_preview/answer_summary（thinking 不允许 answer_summary）。
request_id ≤128 字节且无 NUL，未知请求（初始提示/未曾读取的终态）允许空串，不用 turn ID 伪造；
owner_epoch/source_kind/source_id 从外层状态继承。text 为非空有效 UTF-8 单行，≤2048 字节；
sequence 为 0～INT64_MAX，布尔字段严格类型；remaining_ms 为 0～5000，final 只能用于 dialog 且期限>0。
非法字段拒绝整个状态。完整 JSON 仍≤4096 字节，编码转义膨胀时 Runtime 减少模型思考文本并标截断。

当前请求首正文使 dialog 优先，迟到 thinking 不闪回；新请求/空快照重置。工具、取消、失败、
接管、暂停、等待确认、断连清除；正常完成以规范正文重算短句，5秒后过期。
remaining_ms 是服务计算的相对期限；客户端单调期限只允许缩短，同一终态旧快照不能延长显示。
展示事实（含气泡内容）变化使 revision 增加，经过时间/剩余期限不单独改变 revision。
源序号、外层身份和事件 seq 不可互相替代。预览未经验证，不写入历史或充当任务成功证据。

运输回调仅有界投递；Runtime 25ms 投影刷新与 Topic 消费，不再依赖动画首轮结算。
正文概括为保守抽取，无法生成完整短句则 status_hint，不按前N字符虚构完成。
Golden meta.version=21 增加思考/最终对话状态与 capability 三个向量；既有向量字节保持。

### 2026-10-17：过程省略原因与阶段耗时（DEC-098）

`parts[]` 可选 `omitted_for_display_budget: true` 明确展示裁剪；不表示模型输出被供应商
截断。省略后仍保留可读提示或首尾摘要，分类总量继续遵守 DEC-093。

`session.chat_updated` 和 `session.chat.history` 的 turn 增加可选 `timings` 对象；
同一 `process_sequence` 中的 parts 与 timings 是一致的完整快照。允许的键为
`inference`、`capture`、`ocr`、`settlement`，每项 `{calls: 正整数, duration_ms: 非负整数}`；
均受 JSON 有符号 64 位整数上限约束。缺项表示未计量，不推断为零；已执行但小于
毫秒分辨率的调用可报告 duration_ms=0。终态仍保持幂等并持久化这些指标；旧历史
缺失字段保持未知。发布载荷需要配套更新 native decoder；旧版严格 parts decoder
不认识新的省略原因字段，产品更新必须统一重启服务和前端。

inference 是公开 provider 调用的累计用时（含失败、取消和内部网络等待）；capture
是平台实际取帧调用（含授权后首帧等待、输入前检查和后条件取帧）；ocr 是既有 OCR
适配器调用；settlement 是最终只读核验。原生共享桌面 OCR 的取帧与识别分别计量；
传统整体 OCR 适配器的 OCR 计时包含其内部取帧，未单独暴露的平台阶段仍为未知。
收尾包含的取帧/OCR 会同时计入对应项，各项不可简单相加为总时长，不代表 CPU 时间。

动作 `verification` 增加 `evidence_scope` 与 `proves_arbitrary_goal:false`：

| scope | 证明范围 |
| --- | --- |
| fresh_frame | 已释放输入并取得新共享帧，不能据此认定焦点/导航成功 |
| focus_identity | 指定窗口确为当前焦点，不能证明页面加载/播放 |
| foreground_title | 前台窗口标题满足预先声明条件 |
| process_presence | 应用进程存在，不能证明页面已打开 |
| ui_progress | 前台发生变化，不能证明业务结果 |
| semantic_presence / text_presence | 预先声明的语义节点/文本存在 |
| unknown | 无可归类的证明，不提升为目标完成 |

这些字段说明原条件的含义，不授予额外输入权限，也不改变不确定副作用、取消或
最终收尾验证门禁。模型应按本次用户目标说明实际证据，已核验条件不要求机械重复。
