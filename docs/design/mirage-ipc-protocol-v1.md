# Mirage Local IPC 协议 v1 Wire Schema

> 状态：Accepted（事实源自 `M1.5-01` 起生效；事件帧格式与订阅语义已随 `M1.5-02`
> 落地冻结，2026-09-17）
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 依据：[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)（协议 v1 与传输冻结）、
> [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)（事件订阅扩展，已
> Accepted）、[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)（UI 唯一耦合面）
> 一致性门禁：`tests/runtime/data/ipc_protocol_golden.json`（golden vectors，C++ 与 TypeScript
> 双端测试消费同一文件）

本文档是 Mirage Local IPC 协议 v1 的**权威 wire 契约**（DEC-012 决策 1）。两端实现——
`runtime/ipc` 的 C++ 编解码（`protocol.hpp` / `protocol.cpp` / `framing.hpp`）与 `ui/contracts`
的 TypeScript 镜像——都必须与本文档一致；一致性由共享 golden vectors 的双端测试锁定，
漂移即测试失败。变更流程：先改本文档（注明版本与兼容性影响），同一变更内同步 golden
vectors 与两端实现及测试（工程规范第 8 节）。

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
| `hello` | 无 | `ServiceIdentity`（§6.1） | — |
| `task.submit` | `goal`（string，非空），`steps`（array，可省略 = 空任务），`step_timeout_ms`（可选正整数，毫秒），`session_id`（可选 string，非空；会话绑定，M5-04 落地，缺席落主会话） | `{"task_id"}`（非空），`session_id`（可选 string，非空；任务会话归属的回执，M5-04 落地，服务端恒写出） | `invalid_argument`（空 goal、goal 超长、步数超上限、单步 argument 超长）、`invalid_state`（注册表容量满，提交回滚）、`not_found`（显式 `session_id` 未知）、`pinned_runtime`（pinned 控制面拒绝，透传） |
| `task.list` | 无 | `{"tasks":[{"id","goal","progress"}...]}` | — |
| `task.inspect` | `task_id`（string，非空） | `{"task": InspectTask}`（§6.2） | `not_found`（未知任务） |
| `task.cancel` | `task_id`（string，非空） | `{"task_cancelled":{"task_id","progress"}}`（progress 为取消请求时点的快照，典型为 `Cancelling` 或终态） | `not_found`（未知任务）、`invalid_state`（任务属既往服务轮次且已终态）、`pinned_runtime`（已终态任务，message 前缀 `invalid_state:`） |
| `service.shutdown` | 无 | `{}`（确认形状，无附加成员） | — |
| `events.subscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 旧服务端按未知 op 拒绝：`protocol_error`（`"unknown op 'events.subscribe'"`），新客户端据此降级轮询 |
| `events.unsubscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 同上 |
| `permission.respond` | `request_id`（string，非空），`approved`（boolean）（M5-03 落地） | `{"request_id"}`（回显，§6.3） | `not_found`（未知、已决或已过期的请求 id）、`unavailable`（确认面未启用，§6.3） |
| `permission.list` | 无（M5-03 落地） | `{"pending":[PendingPermission...]}`（§6.3，可为空数组） | `unavailable`（确认面未启用） |
| `session.list` | 无（M5-04 落地） | `{"sessions":[SessionSummary...]}`（§6.4，含主会话，可为空数组） | — |
| `session.open` | 无（M5-04 落地） | `{"session_id"}`（§6.4） | `unavailable`（会话容量饱和，§6.4）、`pinned_runtime`（透传） |
| `session.history` | `session_id`（string，非空），`limit`（可选正整数，M5-04 落地） | `{"session_id","entries":[...],"truncated"}`（§6.4） | `not_found`（未知会话） |
| `workflow.list` | 无（M5-05 落地） | `{"workflows":[WorkflowSummary...]}`（§6.5，可为空数组） | — |
| `workflow.save` | `definition`（object，IR v1 JSON，M5-05 落地） | `{"workflow_id","digest"}`（§6.5） | `unavailable`（注册表容量饱和，§6.5）、`pinned_runtime`（解码 / 追加拒绝，透传） |
| `workflow.publish` | `definition`（object，IR v1 JSON，M5-05 落地） | `{"workflow_id","digest","dry_run_id","idempotent"}`（§6.5） | `pinned_runtime`（门禁拒绝，透传） |
| `workflow.delete` | `workflow_id`（string，非空，M5-05 落地） | `{"workflow_id"}`（§6.5） | `not_found`（未知工作流）、`invalid_state`（存在非终态 Run） |
| `workflow.atom.catalog` | 无（M5-05 落地） | `{"tools":[ExposedTool...]}`（§6.5，可为空数组） | — |
| `workflow.runs` | 无（M5-05 落地） | `{"runs":[WorkflowRunSummary...]}`（§6.5，可为空数组） | — |
| `workflow.run` | `workflow_id`（string，非空），`digest`（可选 string，非空；缺省取 head），`parameters`（可选 object），`policy`（可选封闭策略名，§6.5；M5-05 落地） | `{"run_id"}`（§6.5） | `not_found`（未知工作流）、`unavailable`（运行注册表饱和，§6.5）、`pinned_runtime`（准入 / 提交拒绝，透传） |
| `workflow.cancel` | `run_id`（string，非空，M5-05 落地） | `{"run_id","state"}`（§6.5） | `pinned_runtime`（透传；幂等取消含终态重申） |

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
| `session.updated` | `session_id`（string，非空）、`state`（string，§6.4 状态集合） | 会话进入服务注册表（open）时发布（M5-04，DEC-021）；会话内状态不逐条广播，`session.list` 是快照事实源 |
| `session.message` | `session_id`（string，非空）、`task_id`（string，非空）、`kind`（string，§6.4 词表）、`text`（string，非空）、`sequence`（正整数） | 会话对话投影新增一条时发布（M5-04）：`user` 为任务目标入会话，`outcome` 为任务结算；`sequence` 为条目在会话事件序列中的序号；`session.history` 是含时间戳的完整投影事实源 |
| `session.turn` | `session_id`（string，非空）、`task_id`（string，非空）、`step`（正整数）、`kind`（string，§6.2 步词表）、`status`（string，结算态词表 `ok` / `failed` / `cancelled` / `skipped`） | 一个有界会话工作单元结算时发布（M5-04）：M1 驱动形态为一个脚本步，模型循环落地后为一次循环迭代；轮次开始不发布（`task.updated` 覆盖进行中语义） |
| `session.output` | `session_id`（string，非空）、`task_id`（string，非空）、`step`（正整数）、`chunk`（string，可为空）、`truncated`（boolean） | 步结构化结果的输出增量发布（M5-04）：`chunk` 受 `task.inspect` 结果同源字节预算，M1 驱动每步一份完整结果，流式生产者同形状多 chunk |
| `workflow.run_updated` | `run_id`（string，非空）、`workflow_id`（string，非空）、`state`（string，§6.5 运行状态集合）、`run_epoch`（非负整数）、`summary`（string，可选，encode-when-set） | 工作流运行状态发布（M5-05，DEC-023）：由服务从 pinned 工作流事件转译——`WorkflowRunStarted` 发布 `running`，`WorkflowRunSettled` 发布终态并携带 `summary`（pinned `safe_summary`，2 KiB 上限）；不从服务侧推测状态。`workflow.runs` 是快照事实源 |

### 7.3 一致性模型与背压（DEC-012 决策 4、5）

- 事件是通知，不是可靠投递：`task.list` / `task.inspect` 快照始终是事实源。客户端检测
  到 `seq` 跳跃、`events.overflow` 或重连时**必须 resync**（重新 list/inspect），不得把
  事件流当作完整状态。
- 服务端承载（Executor 路由，`EXEC-02`）：`executor::comm::Topic<TaskEvent>` 多订阅广播
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
