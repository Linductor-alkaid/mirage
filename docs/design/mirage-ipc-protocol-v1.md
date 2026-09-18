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
| `task.submit` | `goal`（string，非空），`steps`（array，可省略 = 空任务），`step_timeout_ms`（可选正整数，毫秒） | `{"task_id"}`（非空） | `invalid_argument`（空 goal、goal 超长、步数超上限、单步 argument 超长）、`invalid_state`（注册表容量满，提交回滚）、`pinned_runtime`（pinned 控制面拒绝，透传） |
| `task.list` | 无 | `{"tasks":[{"id","goal","progress"}...]}` | — |
| `task.inspect` | `task_id`（string，非空） | `{"task": InspectTask}`（§6.2） | `not_found`（未知任务） |
| `task.cancel` | `task_id`（string，非空） | `{"task_cancelled":{"task_id","progress"}}`（progress 为取消请求时点的快照，典型为 `Cancelling` 或终态） | `not_found`（未知任务）、`invalid_state`（任务属既往服务轮次且已终态）、`pinned_runtime`（已终态任务，message 前缀 `invalid_state:`） |
| `service.shutdown` | 无 | `{}`（确认形状，无附加成员） | — |
| `events.subscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 旧服务端按未知 op 拒绝：`protocol_error`（`"unknown op 'events.subscribe'"`），新客户端据此降级轮询 |
| `events.unsubscribe` | 无（M1.5-02 落地） | `{}`（确认形状） | 同上 |

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
| `not_found` | 引用了不存在的任务 id | 保持连接 |
| `invalid_state` | 任务或服务当前状态不允许该操作 | 保持连接 |
| `unavailable` | 服务暂不能提供该能力（保留码，M1 服务未使用） | 保持连接 |
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
`{task_id}`；`task.cancel` 成功载荷为 `{task_cancelled: {task_id, progress}}`。

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
