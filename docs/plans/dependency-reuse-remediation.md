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

- [x] REUSE-01：`runtime/update` 更新下载器迁移到 pinned mira 的
  `mira::SocketHttpTransport`，删除 `update_fetcher_posix.cpp` /
  `update_fetcher_windows.cpp` 手写 HTTP/1.1 客户端；保持 DEC-032 fail-closed 语义
  （明文 HTTP 允许、不跟随 3xx、ed25519 信任锚、有界超时）不变。
- [x] REUSE-02：`runtime/update` 手写 SHA-256（`sha256.cpp` /
  `sha256_internal.hpp`）替换为 `mira::digest_bytes`，理由"MSVC 无 OpenSSL"不成立的
  注释一并移除。
- [x] REUSE-03：`platform/linux/src/linux_file_actions.cpp` 内 Sha256 按 runtime ->
  desktop -> platform 层界处置：platform 层不可依赖 pinned mira，评估保留单份本地实现
  并注释层界原因，或将摘要职责上移至调用方所在层；消除与 REUSE-02 之后的重复。
- [ ] REUSE-04：`integration/mira/src/desktop_png.cpp` 手写 PNG/DEFLATE 编码器按 9.4
  登记依赖缺口台账（RGBA→PNG 转码缝，`mira/observation.hpp` 明示留给 host），代码中
  引用编号；不改动 pinned 代码。
- [x] REUSE-05：UTF-8 校验 4 份（`desktop/environment`、`platform/linux` ×2、
  `apps/native`）收敛为单一内部工具头（置于 desktop 层，platform 经合法向下依赖复用），
  行为逐份比对后统一。

### B. 分层与边界整改（P1/P2）

- [x] REUSE-06：runtime 公共头 pinned 泄漏整改：按 DEC-096 边界策略收口——
  直接 include 维持禁止；经 integration 适配层的传递到达改为可测（BoundaryCheck
  allowlist 显式登记 `runtime_service.hpp` / `mira_host.hpp` 两条缝）；
  ServiceConfig 配置面 pinned-free 化（去 `mira::ModelUsage`/`IModelProvider` 内嵌）
  登记为后续迁移项，不在本计划内实施。
- [x] REUSE-07：`cmake/BoundaryCheck.cmake` 升级为传递闭包检查（沿 `mirage/...`
  include 追踪进 desktop/runtime/platform/integration 树），登记缝之外的新到达
  直接失败，堵住字面 grep 盲区。
- [x] REUSE-08：apps 直连 pinned 收口：DEC-096 明示组合根形态——每进程唯一
  Executor owner（CLI 更新命令/native/pet/tray-service 各自成立）；kairo 的
  include/link 一律经 `Mirage::kairo_consumer` facade（apps/native、apps/pet 全部
  硬编码嵌套 pin 路径已迁移）；mira 核心 API 只经正规 target 链接获取。
- [ ] REUSE-09：`runtime/ipc` session_client 的 mutex+deque outbox 与 promise 应答
  迁移到 `kairo::comm::MpscChannel`（逐条 FIFO 语义），或在核实 kairo 公开接口确不
  承载后以 DEC 登记豁免；四方消费方（native/pet/cli/service）行为不变。
- [ ] REUSE-10：并发轮询灰区逐点收口：`confirmation_hub`（promise+切片轮询）、
  `task_driver` 暂停驻留轮询（DEC-030）、`screen_capture_backend` 15ms 帧轮询、
  `application_backend` terminate 轮询、`apps/cli` 无 Executor 进程轮询——能以
  Executor timer/唤醒表达的改写，确属平台限制的补齐 DEC/注释引用与豁免边界。

### C. 内部重复收敛（P2）

- [x] REUSE-11：`apps/service/host.cpp` 两段 ~80 行 settings→ServiceConfig 映射
  合并为单一映射函数，消除 :547 注释自认的漂移点；E2E 走 flags 的路径行为不变。
- [x] REUSE-12：Windows env→UTF-8 助手 4 份收敛：`runtime/persistence/paths_windows.cpp`
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

- [x] REUSE-16：台账状态收口：`ledger.md` Mirador 索引行与 MIRADOR-20261009-001 锚点
  改为 Resolved（上游 PR#35 已合并、pin 已消费）；`mira.md` MIRA-20261014-005 尾部
  孤立单行表修正、MIRA-20261004-001 详情段状态收口、MIRA-20261005-001 状态收口；
  `ledger.md` 历史锚点中 MIRA-20261006-001/002（PR#80/81 已合并）、
  MIRA-20261008-001（PR#84 已合并）的过期上游状态同步。
- [x] REUSE-17：MIRA-20261013-001 补齐状态/负责人/上游链接字段，入 mira.md 汇总表与
  ledger.md 索引，测试规范侧补引用。
- [x] REUSE-18：补登记被消费/延期但未入台账的上游能力：PR#91/92/93（DEC-085 系列）、
  PR#89 kairo 0.7.0 延期的 QoS/AdaptiveScheduler 触发条件；孤儿编号处置：
  MIRA-20261014-003（Superseded 计划文档草稿与悬空链接清理）、MIRA-20261016-001
  （m11 预备清单前瞻引用，按清单语义正式登记或标注待决策）。
- [x] REUSE-19：`dependencies.lock.json` notes 回写编号：PR#80/#81→MIRA-20261006-001/002、
  PR#94→MIRA-20261014-004、PR#98→MIRA-20261015-001、PR#99→MIRA-20261015-002。
- [x] REUSE-20：`AGENTS.md` 措辞对齐：规则 7 Executor owner 改为与 DEC-030/037/045
  一致的"每进程唯一外部 owner"表述；为测试脚手架（fake 平台事件循环、竞态注入）的
  受控 `std::thread` 增加明示豁免条款。
- [x] REUSE-21：runtime 内平台源文件对（ipc/persistence/update 的
  `*_posix/_windows`、`stream.hpp` 条件编译）与"平台细节只在 platform/"的字面冲突，
  核对既有 DEC-007/M4-06 覆盖度，缺口处以决策/注释收口，不做大规模搬迁。

## 非目标

- 不修改 `third_party/` 任何 pinned 代码；不动 `eui-neo` 边界（DEC-033）。
- 不改变 IPC wire 协议、错误码、更新签名信任锚等对外契约。
- 不在本计划内将 runtime 平台传输实现整体搬迁至 platform/（仅登记与收口）。

### 2026-10-16：REUSE-01/02/03/05/11/12（代码批次，独立验证通过）

- 范围：runtime/update 摘要与下载器迁移 mira、platform/linux SHA-256 层界处置、UTF-8
  收敛、Windows env 助手收敛、apps/service/host.cpp 双份映射合并。
- 关键落地：
  - REUSE-02：`sha256_hex` 改 `mira::digest_bytes`；删除 `sha256.cpp`/`sha256_internal.hpp`；
    独立验证新增 8 条 FIPS 180-4 已知向量钉死与原实现逐字节一致（含空串/百万 'a'/内嵌 NUL）。
  - REUSE-01：`http_get` 自由函数改为 `FetchSession`（PImpl，构造收 `kairo::Executor&`），
    内部经 `mira::adapters::net::SocketHttpTransport`；DEC-032 语义保持——`max_redirects=0`
    （3xx fail-closed，诊断为 transport 的 redirect-limit 文案）、`allow_private_endpoints=true`
    （LAN/localhost 镜像源合法）、2GiB 硬预算、分阶段 deadline（DNS/connect 5s、首字节/空闲
    30s、总 120s，较旧的 30s 切片更细）。`update_fetcher_posix/_windows.cpp` 删除，平台边界
    归 pinned adapter。apps/cli 更新命令补进程唯一 Executor owner（DEC-030 EXEC-01），
    经新建 `cmake/MirageKairoConsumer.cmake` 的 `Mirage::kairo_consumer` 消费面（SYSTEM
    include + 文件路径链接，仿 runtime/service 纪律）。
    与旧实现的记录差异：3xx 无 Location 时诊断为 "redirect carried no location header"；
    超预算诊断为 transport cap 文案；慢服务器超时路径无自动化钉测（未做慢服务器 harness）。
  - REUSE-03：platform/linux 的 Sha256 为树内唯一保留的本地实现（platform 层仅依赖
    Mirage::desktop，pinned mira 不可达），注释写明层界原因与 REUSE-02 的去向。
  - REUSE-05：`linux_file_actions` 的 `is_valid_utf8_text` 收敛为「NUL 策略 + desktop 层
    `mirage::desktop::is_valid_utf8`」；`apps/native/attachment.cpp` 保留本地副本并注明
    薄客户端不链 desktop 层的原因；x11 `decode_codepoint` 为解码器（已注明严格性镜像）。
    实际收敛 4 份 → desktop 单一实现 + 2 个有记录的边界特例。
  - REUSE-12：`runtime/internal/win32_environment.hpp` 承载 runtime 内共享实现（ipc 版死
    分支消除，行为等价）；integration/mira 与 apps/common 两份因层界不可合并，注明归属。
    Windows/MinGW 编译面本机无工具链，待 CI 兜底（NOT TESTED）。
  - REUSE-11：`apply_settings` lambda 统一两条入口；行为修正一处——默认配置路径补齐
    model/runtime 的 `*_from_flags` 守卫（此前注释自认的漂移点，现与 DEC-011 flags 优先
    策略一致）；冗余 settings_directory 赋值删除（其后无条件重设，等价）。
- 验证：Independent-Verification-Agent 两轮。第一轮发现产品缺陷一处（`mirage_update`
  缺 kairo SYSTEM include 链接——net_transport 对 kairo 私有链接不传递 include），主循环
  修复后第二轮官方树复验：`cmake --preset debug` + `cmake --build build/debug` 退出码 0、
  warning/error 0；`ctest -R 'update|file_actions|persistence|ipc|desktop_environment|
  linux_file'` 10/10 通过；CLI `mirage update check/apply` 冒烟（连接拒绝/坏公钥/DNS
  失败三路径 fail-closed、退出码正确、无挂死）。全量 ctest 88/89，唯一失败
  `session_client_test:594` 经 HEAD 基线复现确认为 pre-existing（与本批无关，归入
  REUSE-09 工作项排查）。
- 限制：Windows/MinGW 编译面待 CI；transport 慢服务器 deadline 路径无钉测；负责人
  Mirage 维护者。
### 2026-10-16：REUSE-06/07/08（批次 3，独立验证通过）

- 范围：公共头 pinned 边界策略（DEC-096）、BoundaryCheck 传递闭包检查、apps kairo
  消费面统一到 `Mirage::kairo_consumer` facade。
- 关键落地：
  - DEC-096（Accepted）：直接 include 维持禁止；经 integration 适配层的类型引用是
    合法边界；两条传递缝（runtime_service.hpp、mira_host.hpp）显式登记于
    BoundaryCheck allowlist；apps 组合根每进程唯一 Executor owner + kairo 消费
    经 facade；ServiceConfig 面 pinned-free 化登记为后续迁移项（不在本计划实施）。
  - BoundaryCheck v2：沿 `mirage/...` include 递归解析进 desktop/runtime/platform/
    integration 树；60 个公共头 0 违规、2 登记缝到达；负向用例（临时头注入
    `mirage/integration/desktop_tool_context.hpp`）确认 FATAL 失败、移除后恢复；
    违规报文包含根公共头与到达边。
  - facade 迁移 6 处：apps/native、apps/pet（loader、mira_pet、两个 pet 测试）、
    runtime/service（验证观察的残留张力一并收口，facade 即其既有纪律的 INTERFACE
    化；该迁移属机械等价替换，随批次 4 全量构建复验）。
  - runtime/service CMake 的 "pinned-free" 注释更正为 DEC-096 实际策略。
- 验证：Independent-Verification-Agent——官方 debug 树配置+全量构建退出码 0、
  warning/error 0；preset 未覆盖的 mirage-native/mira_pet/pet_frames_test 以临时
  构建目录（NATIVE_FRONTEND+PET_PREVIEW 开关）补验构建 0 警告且 pet_frames_test
  实跑通过；`mirage-boundary-check` 正向通过、负向用例失败/恢复确认；回归
  `ctest -R 'pet|update|ipc|runtime_service|native_agent'` 17/17。
- 限制：ServiceConfig 面 pinned-free 化为后续项；负责人 Mirage 维护者。
## 退出条件

1. 全部 REUSE 项勾选完成或记录 Blocked 原因与补跑条件。
2. 代码项经 Independent-Verification-Agent 独立验证：受影响模块构建通过、相关既有
   测试通过、新增行为等价证据留档。
3. 台账/文档项交叉引用一致：无孤儿编号、无过期状态、lock notes 可反查。
4. BoundaryCheck 新检查在当前树通过（整改后无新增泄漏）。

## 验证记录

### 2026-10-16：REUSE-16..21（文档/台账批次）

- 范围：台账状态收口与补登记、lock notes 编号回写、AGENTS.md 措辞对齐、孤儿编号清理。
- 关键处置：
  - 新增 MIRA-20261016-001（Proposed，M10 预备清单前瞻占位）、-002（DEC-085 系列追记，
    Resolved）、-003（kairo 0.7.0 延期能力跟踪，Open）、-004（RGBA→PNG 编码缝，Open）；
    MIRA-20261014-003 确认未登记且其"本地改 pinned"路径从未实施，已在
    context-compaction-implementation.md §3.4 以作废草稿标注并修正悬空验收链接。
  - MIRA-20261013-001 补状态/负责人/上游字段并入汇总表；测试侧引用随 REUSE-14/17
    在 tests/support 落地（见代码批次验证记录）。
  - 依据核对：上游 PR#79/80/81 合并状态引自 2026-10-08 master d5c7d06 复核记录
    （mira.md 跟进表）；Mirador 三条收口依据 2026-10-09 授权合并回执（master ae8cf87）。
- REUSE-21 结论：DEC-007 §平台载体（"两平台共享 runtime/ipc 的帧格式、协议与消息
  语义，传输实现按平台分编译单元"）明文覆盖 runtime/ipc 平台源对与 stream.hpp 条件
  编译；persistence 平台差异由 DEC-011/DEC-017 注释背书（paths_windows.cpp、
  store_windows.cpp）；update fetcher 平台对随 REUSE-01 迁移 mira 传输后消除。既有
  决策覆盖充分，不需要新 DEC。
- 验证：JSON 经 `python3 -m json.tool` 等价校验通过；台账编号全仓 grep 无孤儿引用
  （20261014-003 仅存于作废标注、20261016-001..004 均已登记）。文档变更无运行时影响，
  不适用构建/测试证据；声明等级：源码与配置支持。
- 限制：Windows/MSVC 侧无影响面；负责人 Mirage 维护者，无补跑项。
