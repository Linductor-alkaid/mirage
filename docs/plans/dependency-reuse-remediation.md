# 依赖复用与边界整改计划（2026-10-16 全仓审查）

> 状态：In Progress
> 日期：2026-10-16
> 负责人：Mirage 维护者 / ZCode
> 依据：2026-10-16 全仓复用审查（并发原语、能力路由、分层边界、内部重复、反馈台账五路审计）；
> [AGENTS.md](../../AGENTS.md)、[工程规范](../project/project-standards.md) 第 3.3/9.4 节
> 范围：仅整改审查确认的偏离；不改变任何产品行为契约，不修改 `third_party/` pinned 代码。

## 背景

审查结论：复用纪律整体良好（生产代码零裸线程、JSON 全走 mira/json、视觉感知全委托
mirador、状态机为 Mira::core 薄适配），但存在三类系统性缺口：`runtime/update` 重复实现
依赖已有的 HTTP 与摘要能力；runtime 公共头经 integration 头把 pinned 类型泄漏进公共 API
传递闭包；若干已文档化但与 AGENTS.md 字面冲突的并发模式缺统一收口。另有台账状态陈旧、
内部代码重复与测试基建样板问题。

编号说明：本计划使用领域前缀 `REUSE-NN`，仅在本文档内使用，不与既有系列冲突。

## 工作项

### A. 依赖能力重复整改（P1）

- [ ] REUSE-01：`runtime/update` 更新下载器迁移到 pinned mira 的
  `mira::SocketHttpTransport`，删除 `update_fetcher_posix.cpp` /
  `update_fetcher_windows.cpp` 手写 HTTP/1.1 客户端；保持 DEC-032 fail-closed 语义
  （明文 HTTP 允许、不跟随 3xx、ed25519 信任锚、有界超时）不变。
- [ ] REUSE-02：`runtime/update` 手写 SHA-256（`sha256.cpp` /
  `sha256_internal.hpp`）替换为 `mira::digest_bytes`，理由"MSVC 无 OpenSSL"不成立的
  注释一并移除。
- [ ] REUSE-03：`platform/linux/src/linux_file_actions.cpp` 内 Sha256 按 runtime ->
  desktop -> platform 层界处置：platform 层不可依赖 pinned mira，评估保留单份本地实现
  并注释层界原因，或将摘要职责上移至调用方所在层；消除与 REUSE-02 之后的重复。
- [ ] REUSE-04：`integration/mira/src/desktop_png.cpp` 手写 PNG/DEFLATE 编码器按 9.4
  登记依赖缺口台账（RGBA→PNG 转码缝，`mira/observation.hpp` 明示留给 host），代码中
  引用编号；不改动 pinned 代码。
- [ ] REUSE-05：UTF-8 校验 4 份（`desktop/environment`、`platform/linux` ×2、
  `apps/native`）收敛为单一内部工具头（置于 desktop 层，platform 经合法向下依赖复用），
  行为逐份比对后统一。

### B. 分层与边界整改（P1/P2）

- [ ] REUSE-06：runtime 公共头 pinned 泄漏整改：`runtime_service.hpp` /
  `mira_host.hpp` 不再经 integration 公共头传递包含 `mira/*` 头；
  `ServiceConfig` 中的 integration 适配层类型改为不暴露 pinned 传递闭包的形态。
- [ ] REUSE-07：`cmake/BoundaryCheck.cmake` 增加传递闭包检查，能发现"公共头 include
  integration 公共头 → 传递包含 pinned 头"的路径，堵住字面 grep 盲区。
- [ ] REUSE-08：apps 直连 pinned 收口：`apps/native`、`apps/pet` 直接链接/包含
  kairo、`mira/event_store.hpp` 的消费点，按"每进程唯一 Executor owner（DEC-037/045）"
  形态收口到明确边界或以 DEC 明示组合根豁免，消除未记录的直接依赖。
- [ ] REUSE-09：`runtime/ipc` session_client 的 mutex+deque outbox 与 promise 应答
  迁移到 `kairo::comm::MpscChannel`（逐条 FIFO 语义），或在核实 kairo 公开接口确不
  承载后以 DEC 登记豁免；四方消费方（native/pet/cli/service）行为不变。
- [ ] REUSE-10：并发轮询灰区逐点收口：`confirmation_hub`（promise+切片轮询）、
  `task_driver` 暂停驻留轮询（DEC-030）、`screen_capture_backend` 15ms 帧轮询、
  `application_backend` terminate 轮询、`apps/cli` 无 Executor 进程轮询——能以
  Executor timer/唤醒表达的改写，确属平台限制的补齐 DEC/注释引用与豁免边界。

### C. 内部重复收敛（P2）

- [ ] REUSE-11：`apps/service/host.cpp` 两段 ~80 行 settings→ServiceConfig 映射
  合并为单一映射函数，消除 :547 注释自认的漂移点；E2E 走 flags 的路径行为不变。
- [ ] REUSE-12：Windows env→UTF-8 助手 4 份收敛：`runtime/persistence/paths_windows.cpp`
  与 `runtime/ipc/endpoint_windows.cpp` 合并为 runtime 内部共享头（消除已存在的空值
  分支语义漂移）；`integration/mira`、`apps/common` 两份因层界不可跨层共享，逐一注明
  归属与不可合并原因。
- [ ] REUSE-13：`tests/CMakeLists.txt` 97 个同构目标块收敛为 `mirage_add_test()` 函数；
  kairo include 路径全仓 40+ 处硬编码收敛为 CMake 接口目标；行为等价（全部测试目标
  仍可构建、注册）。
- [ ] REUSE-14：测试假体上移：`runtime_service_test.cpp` 与
  `event_subscription_test.cpp` 互抄的 4 个 `mira::IModelProvider` 假体收敛到
  `tests/support`。
- [ ] REUSE-15：`apps/native/runtime_bridge.cpp` 与 `apps/pet/bridge.cpp` 同构的
  kairo IPC worker 胶水（Executor + IBlockingIoWorker 包装 SessionClient + 关闭序列）
  下沉到 apps 共享；每进程 owner 结构不变。

### D. 台账与文档同步（P2/P3）

- [ ] REUSE-16：台账状态收口：`ledger.md` Mirador 索引行与 MIRADOR-20261009-001 锚点
  改为 Resolved（上游 PR#35 已合并、pin 已消费）；`mira.md` MIRA-20261014-005 尾部
  孤立单行表修正、MIRA-20261004-001 详情段状态收口、MIRA-20261005-001 状态收口；
  `ledger.md` 历史锚点中 MIRA-20261006-001/002（PR#80/81 已合并）、
  MIRA-20261008-001（PR#84 已合并）的过期上游状态同步。
- [ ] REUSE-17：MIRA-20261013-001 补齐状态/负责人/上游链接字段，入 mira.md 汇总表与
  ledger.md 索引，测试规范侧补引用。
- [ ] REUSE-18：补登记被消费/延期但未入台账的上游能力：PR#91/92/93（DEC-085 系列）、
  PR#89 kairo 0.7.0 延期的 QoS/AdaptiveScheduler 触发条件；孤儿编号处置：
  MIRA-20261014-003（Superseded 计划文档草稿与悬空链接清理）、MIRA-20261016-001
  （m11 预备清单前瞻引用，按清单语义正式登记或标注待决策）。
- [ ] REUSE-19：`dependencies.lock.json` notes 回写编号：PR#80/#81→MIRA-20261006-001/002、
  PR#94→MIRA-20261014-004、PR#98→MIRA-20261015-001、PR#99→MIRA-20261015-002。
- [ ] REUSE-20：`AGENTS.md` 措辞对齐：规则 7 Executor owner 改为与 DEC-030/037/045
  一致的"每进程唯一外部 owner"表述；为测试脚手架（fake 平台事件循环、竞态注入）的
  受控 `std::thread` 增加明示豁免条款。
- [ ] REUSE-21：runtime 内平台源文件对（ipc/persistence/update 的
  `*_posix/_windows`、`stream.hpp` 条件编译）与"平台细节只在 platform/"的字面冲突，
  核对既有 DEC-007/M4-06 覆盖度，缺口处以决策/注释收口，不做大规模搬迁。

## 非目标

- 不修改 `third_party/` 任何 pinned 代码；不动 `eui-neo` 边界（DEC-033）。
- 不改变 IPC wire 协议、错误码、更新签名信任锚等对外契约。
- 不在本计划内将 runtime 平台传输实现整体搬迁至 platform/（仅登记与收口）。

## 退出条件

1. 全部 REUSE 项勾选完成或记录 Blocked 原因与补跑条件。
2. 代码项经 Independent-Verification-Agent 独立验证：受影响模块构建通过、相关既有
   测试通过、新增行为等价证据留档。
3. 台账/文档项交叉引用一致：无孤儿编号、无过期状态、lock notes 可反查。
4. BoundaryCheck 新检查在当前树通过（整改后无新增泄漏）。

## 验证记录

（随各工作项完成追加；证据等级区分见工程规范第 7 节。）
