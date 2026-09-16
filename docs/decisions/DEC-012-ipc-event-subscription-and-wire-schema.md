# DEC-012：IPC 事件订阅契约与 wire schema 事实源

> 状态：Accepted（2026-09-16 评审通过，记录见文末变更记录；事件帧格式与订阅语义已随
> `M1.5-02` 落地冻结，2026-09-17）
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1.5（wire schema 文档自 `M1.5-01` 起作为契约事实源；事件帧格式与订阅语义已随 `M1.5-02` 落地冻结）
> 替代/被替代：无；本记录是 [DEC-007](DEC-007-local-ipc-and-runtime-service.md) 的**附加扩展**——传输载体、帧格式、载荷上限与单连接单未决请求纪律全部不变

## 背景与问题

[DEC-006](DEC-006-ui-web-frontend-packaging.md) 确定 UI（Web 前端、独立进程）与 Runtime
Service 的唯一耦合面是 Local IPC 契约，UI 与核心因此可以并行开发。落到当前状态有两个缺口：

1. **契约事实源缺位**：协议 v1 的消息定义只存在于 C++ 结构
   （`runtime/ipc/include/mirage/runtime/ipc/protocol.hpp`），前端（TypeScript）无法直接
   消费，也没有一份人可读的 wire 契约文档作为两端演进的共同基准。
2. **事件推送面缺位**：协议 v1 是纯请求-响应（CLI 靠轮询 `task.inspect` 观察进度），
   总计划 `EXEC-02` 已定方向"事件到 UI 的分发映射为 `Topic` / `LatestMailbox`"，但 wire
   上没有订阅与推送语义。产品 UI 的实时任务视图需要该面，且应尽早冻结以免前端返工。

两者都要在不破坏 DEC-007 冻结承诺（严格解码、未知 op 拒绝、单连接单未决请求）的前提
下落地。

## 决策

1. **wire schema 事实源**：新增 `docs/design/mirage-ipc-protocol-v1.md` 作为协议 v1 的
   权威契约文档（帧格式、信封、请求/响应/事件消息表、错误码、golden vectors 说明）。
   `runtime/ipc` 的 C++ 结构与 `ui/contracts` 的 TypeScript 镜像都必须与之一致；一致性由
   共享 golden vectors（`tests/runtime/data/ipc_protocol_golden.json`，手写测试数据、
   属源码树）的双端测试锁定，漂移即测试失败。契约变更流程：先改 schema 文档（注明版本
   与兼容性影响），同一变更内同步两端实现与测试（工程规范第 8 节）。
2. **事件订阅是协议 v1 的附加扩展，版本号不递增**：
   - 新请求 op：`events.subscribe` / `events.unsubscribe`（无参数，订阅粒度为连接）。
     旧服务端按既有严格解码拒绝未知 op（稳定错误 `unsupported` 或 unknown-op
     `protocol_error`），新客户端据此降级为轮询——这正是 DEC-007 错误面的设计用途；旧
     客户端不发送新 op、也不会收到事件帧，零影响。
   - 新服务端帧种类：`event` 信封 `{"v":1,"seq":N,"event":"<名称>",...载荷}`，`seq` 为
     每连接自 1 起单调递增的序号。帧分类按成员判别：含 `op` 为请求（客户端 → 服务端），
     含 `ok` 为响应，含 `event` 为事件（仅订阅建立后，服务端 → 客户端）。既有
     `decode_request` / `decode_response` 路径不改。
   - `hello` 响应（`ServiceIdentity`）新增可选成员 `events`（boolean）能力通告：新
     服务端编码时总是写出，解码端缺省 `false`——沿用 M1-06（DEC-010）附加字段向后
     兼容纪律。客户端经 hello 探测能力，失败路径仅作兜底。
   - 单连接单未决请求纪律不变：事件帧不是请求，订阅建立后可与响应帧交错下发；客户端
     帧读取循环按上述成员判别分发。订阅是连接级状态，断连即失效，不跨连接保持。
3. **M1.5 事件集**（封闭集合，M2+ 新事件以附加方式进入，不改既有事件字段语义）：
   - `task.updated`：载荷为 `{"task_id","goal","progress","has_success","success"}` 快照
     语义（progress 含义与 `task.inspect` 一致）；任务创建、进度推进与终态（含取消、
     失败）都发布。
   - `host.status`：载荷 `{"status"}`，Mira Host 五态（DEC-004）变化即发布。
   - `events.overflow`：合成标记事件，载荷 `{"dropped":N}`，连接级事件队列溢出时发布。
4. **一致性模型**：事件是通知，不是可靠投递。`task.list` / `task.inspect` 快照始终是
   事实源；客户端检测到 `seq` 跳跃、`events.overflow` 或重连时必须 resync（重新
   list/inspect），不得把事件流当作完整状态。
5. **Service 侧承载（Executor 路由，落实 `EXEC-02`）**：RuntimeService 持有进程内
   `executor::comm::Topic<TaskEvent>`（容量与 DropPolicy 显式配置）作为多订阅广播点；
   host 状态变化以 `LatestMailbox` 语义（仅最新状态有效）进入同一发布路径。任务驱动
   循环与 MiraHost 状态变更在既有 SerialExecutionContext 域内 publish（publish 与向
   每连接有界 `MpscChannel` 的 `try_send` 均为有界操作）；各 IPC 会话订阅 Topic，经
   每连接有界队列（drop-oldest）投递到连接 blocking I/O worker 写出。写出口径：响应帧
   优先于事件帧，事件溢出以 `events.overflow` 显式呈现（总计划 `RULE-07` 与 AGENTS.md
   Executor 规则 10：背压转化为明确结果与事件，不静默丢弃）。断连时取消订阅并排空
   队列。`Topic` 非实时（mutex + 动态分配）——本地 IPC 的连接量级（个位数）下这是正确
   选型，不冒充实时通道，产品化规模（M5）复核。
6. **开发期桥接与前端形态**：新增 `apps/devbridge` 开发期工具（不进入产品安装包与发布
   目标）：Unix domain socket ↔ WebSocket 帧转发（长度前缀帧透传，不改写载荷），使
   前端能在浏览器中对真实 Service 联调。devbridge 属 Mirage 自研代码，完整适用
   Executor 纪律（本进程唯一 owner、blocking I/O worker 承载双端 I/O、可关闭、句柄
   可见）。前端无 Service 时使用 `ui/contracts` 提供的 mock transport（内存任务状态机
   + 事件模拟）开发界面；mock 与真实实现共用同一 golden vectors。CEF 壳与生产形态
   不受影响：壳选型 PoC 仍属 M3，前端工具链定案仍属 DEC-006 决策 6（M5）。

## 备选方案

- **独立第二条事件连接**：否决。订阅状态与连接生命周期的耦合、双通道清理顺序都会
  复杂化 DEC-007 的会话模型；单连接 + 帧判别成员以最小 wire 变化满足需求。
- **协议版本递增到 v2**：否决。附加扩展满足向后兼容（旧客户端零影响、新客户端可探测
  降级），不满足递增条件；出现真正不兼容变更（改变既有字段语义）时才递增，并按
  DEC-006 双版本并存。
- **事件订阅带 topic 过滤参数**：推迟。M1.5 事件集小且都面向 UI，过滤是伪需求；M2
  observation 事件进入时若确需过滤，以附加可选参数扩展。
- **服务端每事件直接写 socket**：否决。绕开连接 worker 串行化会造成响应/事件写竞争，
  且无法实施显式背压策略。
- **前端只轮询不订阅**：保留为降级路径（决策 2），不作主路径——实时性与轮询开销在
  产品 UI 上不可兼得。
- **gRPC / 裸 WebSocket 直连 Service**：否决。违反 DEC-007 传输冻结与依赖锁定基线
  （工程规范 9.1）；devbridge 仅存在于开发期、进程外。

## 影响与风险

- `runtime/ipc` 公共头新增事件信封结构与订阅请求；`apps/service` 新增订阅会话状态与
  Topic 发布点。M1 既有协议/服务测试必须零回归。
- 契约三处同步成本（schema 文档 / C++ / TS）由 golden vectors 双端测试强制锁定，漂移
  在 CI 可见；文档更新与实现同变更（工程规范第 8 节）。
- 事件负载构造成本随订阅数线性增长；M1.5 规模可忽略，多连接产品化（M5）复核。
- devbridge 引入一个仅开发期使用的可执行目标：不进安装包、不进 SBOM 发布清单、不产
  生新的进程形态承诺；其源码仍受全部仓库纪律约束。
- 前端工具链（Vite、包管理器、框架）为开发期暂定值，在 M5 定案前不得写入兼容性声明
  或发布承诺（DEC-006 决策 6）。

## 验证方式

- 协议测试（`tests/runtime`）：事件帧编解码 round-trip、`seq` 单调性、订阅/退订
  round-trip、未知 op 稳定错误、hello `events` 能力成员（有/无两形态解码）。
- 服务测试：订阅后 `task.submit` → 逐步 `task.updated` → 终态事件的完整序列；
  `host.status` 变化事件；断连清理订阅；多订阅者扇出；响应帧不被事件饥饿；未订阅
  连接零事件帧；溢出路径注入（小容量队列）产生 `events.overflow`。
- 端到端：devbridge + 浏览器 WebSocket 完成 hello / submit / 事件接收；mock transport
  与真实 service 对同一 golden vectors 一致。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan`（跨上下文事件路径必须
  tsan）构建与 `ctest` 全绿，M1 测试零回归。

## 变更记录

- 2026-09-17：`M1.5-02` 落地，事件帧格式与订阅语义按本决策冻结。实现范围：决策 2
  （`events.subscribe` / `events.unsubscribe`、`event` 信封、hello `events` 能力通告，
  `runtime/ipc` 编解码 + TypeScript 镜像同步）、决策 3（`task.updated` /
  `host.status` / `events.overflow` 事件集；订阅建立时先下发当前 host 状态作 seed，
  与前端 mock 的 Enqueue-after-subscribe 行为一致）、决策 4（一致性模型不变，测试
  锁定 seq 连续性与 overflow 后 resync 依据）、决策 5（`executor::comm::Topic` +
  `LatestMailbox` 承载、每连接 drop-oldest 有界队列、响应优先写出、断连清理，
  见设计文档 §12.2 补记）。Golden vectors `meta.version` 1 → 2（新增订阅 round-trip
  向量；原 `unknown-op-events-*` 失败向量为旧服务端行为、已过时，替换为
  `unknown-op-registry-edit`；`hello-capability` `encode_pending` 解除）。
  顺带修复实现中发现的两个非契约缺陷：驱动 post 到 serial 上下文的 lambda 按值
  捕获栈上字符串（原按引用捕获在 serial 积压超时后悬垂），`IpcStream::write_some`
  改用 `send(MSG_NOSIGNAL)`（嵌入方未忽略 SIGPIPE 时断连写出不再杀进程）。

- 2026-09-16：评审通过，状态 Proposed → Accepted（维护者授权评审）。按工程规范第 14
  节六项重点逐项核查：
  1. 与产品目标及既有决策一致：扩展 [DEC-007](DEC-007-local-ipc-and-runtime-service.md)
     冻结的协议 v1 而不破坏其承诺（传输、帧格式、载荷上限、单连接单未决请求全部不变），
     落实总计划 `EXEC-02` 与 `RULE-07`，耦合面仍为 [DEC-006](DEC-006-ui-web-frontend-packaging.md)
     的 Local IPC。
  2. 分层与 Executor 生命周期正确：决策 5 的承载组件（`executor::comm::Topic` /
     `LatestMailbox` / `MpscChannel` / `DropPolicy::DropOldest`）已在 pinned executor
     公开头逐一核实存在且语义相符；发布点 owner 为 RuntimeService，publish 位于既有
     SerialExecutionContext 域，每连接有界队列汇入连接 blocking I/O worker 写出，无
     自建线程或队列。
  3. 失败/取消/背压/关闭闭合：溢出以 `events.overflow` 显式呈现、响应帧优先写出、
     断连取消订阅并排空队列、事件定位为通知（快照为事实源）。
  4. 兼容与迁移明确：旧客户端零影响，新客户端经 hello `events` 能力探测或稳定错误
     降级轮询（现服务端对未知 op 以 `protocol_error` 拒绝，见
     `runtime/service/src/runtime_service.cpp` 的解码失败路径，与决策 2 一致）。
  5. 验证方式可执行：协议/服务/端到端/预设矩阵用例清单完整；实现证据由 `M1.5-01`
     （golden vectors 双端一致性）与 `M1.5-02`（事件订阅服务测试）交付时产生，决策
     状态不表示实施进度。
  6. 文档同步：本次状态转换同步 [M1.5 计划](../plans/m1.5-ui-parallel-track.md)的
     风险与阻塞节；`M1.5-01` 的 schema 事实源文档为本决策决策 1 的首个落地物。
  现状对照：`runtime/ipc` 协议面（严格解码、op 表、`ServiceIdentity`、progress 语义、
  StepView）与 `ui/contracts` TypeScript 镜像（含事件信封草案实现）均与本决策一致。

## 关联文档和工作项

- [DEC-006](DEC-006-ui-web-frontend-packaging.md)（UI 唯一耦合面、独立发版前提）、
  [DEC-007](DEC-007-local-ipc-and-runtime-service.md)（本决策扩展其协议 v1，传输与
  帧格式不变）。
- 总计划：`EXEC-02`（事件分发映射 `Topic` / `LatestMailbox`，本决策落实）、`RULE-07`
  （背压显式化）。
- 工作项：`M1.5-01`（schema 事实源与 TS 镜像）、`M1.5-02`（service 事件订阅）、
  `M1.5-03`（devbridge）、`M1.5-05`（前端真实联调），见
  [M1.5 里程碑计划](../plans/m1.5-ui-parallel-track.md)。
- DEC-005（待建，DesktopObservation 契约）：其 M2 事件面将沿用本决策的事件帧机制与
  一致性模型。
