# DEC-020：M5 权限异步确认面（Local IPC）

> 状态：Accepted
> 日期：2026-09-26
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-03` 落地）
> 替代/被替代：替换 [DEC-010](DEC-010-m1-permission-framework.md) 决策 4 冻结的
> M1 同步确认挂点契约（"同步、进程内、必须立即返回"），判定语义、Capability
> 词表与 Provider 硬边界（DEC-009）全部不变——正是 DEC-010"影响与风险"节预告的
> M5 演进决策

## 背景与问题

`M5-03` 要求把 M1 的同步确认挂点演进为 Local IPC 异步确认面：产品 UI（批准
中心，`M5-07` 产品化）与 tray 等 IPC 客户端在 `confirm` 规则命中时收到请求
事件并回送应答，任务驱动器在确认收敛前不执行任何桌面副作用。M1 形态的四个
结构性限制在产品化下不再成立（DEC-010 限制节预告）：

1. 同步进程内调用无法可靠超时（需要等待线程），而产品确认必须自带等待预算
   与超时收敛，否则一次无人应答的确认会让任务无限滞留；
2. 无取消路径：任务取消（`RULE-04`、Human Takeover）必须能中断进行中的
   确认等待，避免"已取消的任务还在等人批准"；
3. 无多客户端语义：产品形态下 UI / tray / CLI 可能同时连接，请求必须广播、
   应答必须收敛出唯一赢家；
4. 无恢复与重同步语义：断连、重连或事件丢失后客户端需要快照面重建待确认
   视图（DEC-012 一致性模型的要求）。

实现前需定案：挂点契约的替换形态、等待预算与超时收敛、取消与停机行为、
多连接竞争、恢复/重同步、协议扩展形状与信息披露边界。

## 决策

1. **挂点契约替换（判定语义不变）**：`ConfirmationHandler::confirm()` 由
   "同步返回 bool"演进为"有界确认返回 `ConfirmationResult`"
   （`Approved` / `Rejected` / `TimedOut` / `Unresolved` / `Cancelled`），并
   携带 `CancelProbe`（`std::function<bool()>`）供实现轮询调用方取消状态。
   `PermissionController::authorize()` 增加探测参数；`DenyAll` /
   `AllowAll` 内建行为不变（分别 `Rejected` / `Approved`）。四种判定
   （`allowed` / `confirmed` / `denied` / `confirmation_rejected`）、
   Capability 词表与 Provider 硬边界全部保持 DEC-010 冻结值；失败路径的
   差异只体现在步 trace 的稳定 reason 字符串（`confirmation rejected` /
   `timed out` / `unavailable` / `cancelled for <capability>`），wire 决策名
   不新增。探测先行：authorize 在 Confirm 分支先查探测，探测已触发即返回
   `Cancelled`，不触碰挂点。
2. **异步确认承载（`runtime/permission`，pinned-free）**：新增
   `AsyncConfirmationHub : ConfirmationHandler`（纯 std：mutex + promise +
   steady_clock，无线程、无调度、无 executor 类型）。`confirm()` 为每个请求
   生成 `perm-<N>` 稳定 id、经发布钩子广播后进入切片轮询等待（默认 25 ms
   切片，观察探测与截止时间）；等待预算耗尽返回 `TimedOut`（fail closed）。
   待确认集合容量显式封顶（构造参数，默认 64；`RULE-07`），饱和即返回
   `Unresolved`（fail closed），不排队、不静默丢弃。
3. **等待预算与超时收敛**：每请求一个等待预算，服务默认 120 s
   （`ServiceConfig` 经 hub 构造注入；CLI `--confirm-wait-ms` 覆盖）。预算
   耗尽收敛为 `confirmation_rejected` 判定 + `permission_denied: confirmation
   timed out for <capability>` 步错误——与 DEC-010 默认 `DenyAll` 同向，
   无人应答永远不等于批准。截止时间边界的竞态（应答恰在到期瞬间到达）以
   先完成状态迁移者为赢家，两种结果都被允许（确定性由互斥保证，不作时钟
   精度承诺）。
4. **多连接竞争：first-response-wins**：`permission.request` 事件经既有事件
   订阅面广播（仅订阅连接可见，DEC-012 纪律）；任何连接可发送
   `permission.respond`。第一个使状态迁移的应答是赢家（服务返回确认）；后续
   应答（含同 id 重复应答、超时/取消后的迟到应答）得到 `not_found`
   （`"unknown or already decided permission request id"`）。不绑定"请求 →
   提交连接"归属：本地 IPC 的信任模型下所有连接同权，端点对端凭据校验是
   `M5-08` 的产品化复核项（DEC-007 / DEC-012 既定）。
5. **取消与停机**：驱动器以 `{desktop cancel token, executor stop token}` 组
   成探测传入 authorize；确认等待期间任一触发，等待即返回 `Cancelled`，驱动
   器走既有取消路径（步 `cancelled`、后续 `skipped`、任务 `Cancelled`），
   **步 trace 的 permission 字段保持空**（未判定——未确认、未拒绝，schema
   "未判定为空"语义）。应答与取消的竞态同理取先到者：应答先到则确认结果
   有效，但驱动器在 authorize 返回后复查取消令牌，取消始终赢过后续动作
   （已确认但被取消的批准被丢弃，不产生副作用）。有序停机先请求取消再排空，
   等待中的确认以 `Cancelled` 收敛并进入恢复快照，无泄漏。
6. **恢复与重同步**：待确认请求纯内存态，不持久化（DEC-011 不新增条目）：
   服务重启即失，等待方以 fail closed 收敛，重启后不存在"复活中的确认"。
   事件是通知不是可靠投递（DEC-012 决策 4），客户端以 `permission.list`
   快照为重同步事实源（连接级、无参数、返回剩余预算 ≥ 1 ms 的待确认集）；
   `AsyncConfirmationHub::confirm` 的发布走既有 serial 域 best-effort 路径，
   与任务事件同一背压语义（丢弃由快照面兜底）。hello `ServiceIdentity` 新增
   可选 `permissions` 能力通告（编码端总是写出，解码端缺省 `false`，DEC-012
   `events` 先例）——新客户端据此启用批准面，避免对旧服务端发未知 op 触发
   `protocol_error` 断连的降级路径。
7. **协议扩展（DEC-012 附加扩展流程，v1 不变）**：
   - 事件 `permission.request`：`request_id`（非空）、`capability`（封闭
     Capability 词表）、`resource`（动作资源：路径 / 命令行）、`task_id`
     （非空）、`timeout_ms`（正整数，发布时点剩余预算）。事件不含
     `operation_id`——判定发生在 pinned 准入之前（DEC-010），此时不存在
     operation id。
   - 请求 `permission.respond`：`request_id`（非空）+ `approved`（boolean）；
     成功载荷 `{"request_id"}`（回显）。错误：未知 / 已决 / 已过期 id →
     `not_found`；确认面未启用 → `unavailable`（"服务暂不能提供该能力"，
     既有保留码的既定用途）。
   - 请求 `permission.list`：无参数；成功载荷 `{"pending":[...]}`，条目成员
     同事件（wire 顺序同表）。确认面未启用同样 `unavailable`。
   - 传输、帧格式、载荷上限、单连接单未决纪律、版本号全部不变；golden
     vectors 双端同步（meta.version 2 → 3）。
8. **信息披露边界**：`resource`（路径 / 命令行）上 wire 是本决策的显式选择：
   批准 UI 必须展示将要执行的动作内容，否则确认流形同虚设。披露范围由既有
   传输边界承接（本地 Unix socket / 命名管道，同用户；`task.inspect` 已披露
   文件内容与命令输出，resource 不扩大实际暴露面）；跨权限级别的对端凭据
   校验随 `M5-08` 复核收口。步 argument 本身仍不上 wire（`StepView` 不含），
   披露面仅限确认流。
9. **服务装配与 CLI**：`ServiceConfig` 新增 `confirmation_hub`
   （`shared_ptr<AsyncConfirmationHub>`）——设置即启用 IPC 异步面，与旧
   `confirmation` 同步钩子互斥（start() 校验，双设 fail closed）；判定器仍
   全服务唯一（DEC-010 决策 7 纪律）。`mirage-service --confirm ipc` 启用
   异步面（默认预算 120 s，`--confirm-wait-ms` 覆盖）；`--confirm allow|deny`
   同步钩子行为不变；默认（无旗标）仍是 headless fail closed。

## 备选方案

- **保留 bool 挂点、用异常/约定值表达超时**：否决。bool 无法承载收敛路径的
  差异，trace reason 将失去稳定来源；DEC-010 已预告挂点契约由 M5 决策替换，
  直接定义封闭结果集更诚实。
- **确认等待放 Executor timer / 延迟任务回调**：否决。驱动器已是有界任务
  上下文，切片轮询（`future::wait_for`）与既有 `ready_within` 等待模式同构，
  不引入新任务形态；确认不是定时器语义，是无界期（预算内）的外部应答等待。
- **许可请求按连接定向（谁提交任务谁收请求）**：否决。M1 任务模型没有连接
  归属（任务在服务侧存活，提交连接可断开），定向投递会在提交方断连后把
  确认送进黑洞；广播 + first-wins + 快照重同步是唯一与 DEC-012 一致性模型
  相容的形态。
- **应答加幂等 token / 第二次应答返回首个结果**：否决。first-wins + 后续
  `not_found` 已满足收敛；为迟到应答回放首个结果需要保留已决历史与保留
  时长两个新概念，M5 规模下是伪需求（`M5-08` 复核时可再议）。
- **待确认请求持久化、重启后恢复**：否决。任务本身不跨重启恢复执行
  （M1-07 只注水终态记录），恢复一个没有任务的待确认请求没有意义；fail
  closed（重启即失）与任务恢复语义一致。
- **timeout 也作为新 wire 决策名（如 `confirmation_timeout`）**：否决。
  wire 决策集合是 DEC-010 冻结契约，扩集会波及全部解码端与恢复注水；超时
  与拒绝对判定语义同向（fail closed），差异属于 reason 字符串（步 error
  已携带，安全用于 UI）。

## 影响与风险

- `ConfirmationHandler` 签名变更是源码级破坏：M1 内建实现与测试内
  RecordingConfirmation 随本变更更新；外部无消费者（runtime/permission 无
  下游嵌入方）。判定语义与默认策略零变化，`task_permission_test` 场景全部
  保留并通过。
- 确认等待占用驱动线程（executor async pool）：等待是切片轮询、可被取消
  及时打断，预算上限即占用上限；并发确认数受 hub 容量与并发任务数双重
  封顶。产品化规模（并发任务 × 确认数）随 `M5-08` 复核。
- `permission.request` 事件进 `EventPayload` 变体：旧客户端解码未知事件名
  得稳定错误（"unknown event"），与 DEC-012 事件封闭集演进纪律一致；
  前端 sequencer 按 seq 推进与事件名解耦，零回归。
- CLI 新旗标只增不改；`--confirm ipc` 在无人应答时表现为任务按预算失败
  （fail closed），使用方需知悉（usage 文本声明）。

## 验证方式

- `tests/runtime/permission_test.cpp`（单元，跨平台）：新结果集与探测先行
  语义（内建 handler 返回值、四种失败 reason 稳定串、探测触发即 Cancelled
  且不触碰挂点）；`AsyncConfirmationHub`：批准 / 拒绝 / 超时 / 取消 /
  `resolve` 未知 id / 容量饱和 `Unresolved` / `pending()` 快照剩余预算。
- `tests/runtime/permission_ipc_test.cpp`（集成，POSIX，真实
  RuntimeService + Local IPC）：订阅连接收到 `permission.request` 事件
  （成员与剩余预算）、独立连接 respond 批准 → 步 `confirmed` 且任务
  Completed；拒绝 → `confirmation_rejected` + 无副作用 + fail-fast；超时
  （小预算）→ `confirmation timed out` fail closed；`permission.list`
  待确认快照与应答后清空；first-response-wins（第二应答 `not_found`）；
  确认等待中 task.cancel → 任务 `Cancelled`、步 `cancelled`、permission
  字段为空；确认面未启用时 respond / list → `unavailable`；hello
  `permissions` 能力位两形态。
- golden vectors 双端门禁（C++ `ipc_protocol_golden_test` + TS
  `golden-vectors.test.ts`）：新请求 / 响应 / 事件与失败向量的逐字节锁定，
  `meta.version` 3。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 构建与 `ctest`
  全绿（确认等待跨上下文收敛路径必须 tsan）；`runtime/permission` 公共头
  pinned-free 边界零命中。

## 关联文档和工作项

- [DEC-010](DEC-010-m1-permission-framework.md)（被替换的挂点契约与不变的
  判定语义）、[DEC-009](DEC-009-provider-scope-budget-cancellation.md)
  （Provider 硬边界叠加）、[DEC-007](DEC-007-local-ipc-and-runtime-service.md) /
  [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（协议扩展流程
  与事件一致性模型）、[DEC-011](DEC-011-m1-local-state-persistence.md)
  （不新增持久化条目）。
- 设计文档第 12、15 节；[wire 契约](../design/mirage-ipc-protocol-v1.md)；
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4（批准中心契约映射）。
- 工作项：`M5-03`（本决策）；批准中心 UI 产品化在 `M5-07`，权限策略持久化
  与默认收紧在 `M5-07` / `M5-08`。
