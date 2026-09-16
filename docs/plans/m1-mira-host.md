# M1：Mira Host 与基础 Runtime

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：无（项目初始化已随本里程碑工作项完成）
> 建议发布点：`release-alpha`
> 更新日期：2026-09-16

## 目标

建立 Mirage 的基础产品骨架和 Mira Host：在 Linux 开发环境中能够启动 Mira Agent、提交
任务，并允许 Agent 使用 Filesystem、Process/Shell 等基础 PC 能力。同时建立 Runtime
Service 与 Local IPC，使 Agent 可以脱离 GUI 生命周期运行（设计文档第 18 节第一阶段）。

## 范围与非目标

范围：

- 项目骨架、pinned 依赖接线（mira / mirador）与构建验证基线。
- Mira Host：初始化 Mira、绑定最小 Desktop Environment、转发任务状态与执行事件。
- Runtime Service + Local IPC + CLI（`mirage task ...` 最小命令面）。
- Filesystem / Process Provider 的第一版实现与权限判定框架。

非目标：

- Window / Accessibility / Screen / Input Provider（M2）。
- Mirador 视觉集成（M3）。
- Windows Backend（M4）。
- 桌面 GUI 产品界面（M5）。

## 设计与决策依据

- [设计文档](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md) 第 5、11、12、
  17、18 节。
- [DEC-001](../decisions/DEC-001-dependency-pinning.md)：依赖以 submodule + 锁文件 pin。
- [DEC-002](../decisions/DEC-002-build-test-baseline.md)：构建、预设与测试基线。
- [DEC-003](../decisions/DEC-003-repository-layout.md)：仓库布局与分层依赖方向。
- [DEC-004](../decisions/DEC-004-mira-host-status-set.md)：Mira Host 状态集（M1 冻结）。
- [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)：GUI / Tray / CLI 仅经
  Local IPC 与 Runtime Service 交互。
- [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)：Local IPC 机制与
  Service 进程形态（M1 冻结传输与帧格式）。
- [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)：
  M1 环境绑定与参考 Provider 边界（`M1-04` 附带
  `DesktopEnvironmentBinding::bound_environment()` 访问器）。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)：M1 Provider
  路径范围、执行预算与取消路径（Permission 判定与其叠加而非互替）。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)：M1 Desktop Permission
  框架雏形（Capability 词表、策略判定、用户确认挂点与 Trace 关联形态）。

## 工作项

- [x] `M1-01` 项目初始化：pinned `mira` / `mirador` 子模块与
      `dependencies.lock.json`、CMake 骨架（`CMakePresets.json` 五预设）、设计文档
      第 17 节目录骨架、依赖接线冒烟测试（mira / mirador 真实链接验证）。
- [x] `M1-02` Mira Host 实现可运行的宿主生命周期：初始化 / 关闭顺序、HostStatus 状态
      机（冻结状态集并写入设计文档）、桌面环境绑定接口，覆盖正常完成、取消与 shutdown
      测试。
- [x] `M1-03` Desktop Environment 绑定适配器：经 `integration/mira` 把 Filesystem /
      Process Provider 暴露给 Mira，Agent 可提交一个读取文件并执行 Shell 命令的任务并
      观察到结构化结果。
- [x] `M1-04` Runtime Service 与 Local IPC：Service 独立于 GUI 生命周期运行，CLI 经
      IPC 完成 `task list` / `task submit` / `task inspect`；IPC 机制定案（DEC-007）。
- [x] `M1-05` Filesystem / Process Provider：路径范围约束、命令执行预算与取消路径，
      负向用例覆盖越界访问与拒绝执行（范围/预算/取消语义见
      [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)）。
- [x] `M1-06` Desktop Permission 框架雏形：`filesystem.read` / `filesystem.write` /
      `process.execute` Capability 判定与用户确认挂点（确认 UI 可延后到 M5）。
- [ ] `M1-07` 持久化骨架：Mirage 本地配置与 Runtime Recovery State 的存取（设计文档
      第 16 节中 M1 相关子集）。

## 风险与阻塞

- Mira Host 对 Mira 公开 API 的宿主形态（实例生命周期、事件订阅）依赖 mira 0.1.x 契约
  稳定性；若上游契约在 M1 期间变化，按依赖升级流程处理并同步锁文件。
- ~~Local IPC 机制未定案~~：已定案为 [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)
  （2026-09-16，Unix domain socket + 长度前缀 JSON 帧，`apps/service` 进程形态）。
- `BUG-20260916-001`：`mirage service start` fork 出的服务子进程继承 CLI 的 stdout，
  当调用方把输出接入管道时（如 `| grep`），管道写端被长生命周期服务持有，读端等不到
  EOF（M1-06 冒烟中发现，M1-04 遗留，非权限框架引入）。正常 CLI 使用（终端/文件重
  定向）不受影响；修复方向为 exec 前按守护进程纪律重定向子进程 stdio，随 M5 产品化
  或独立 fix 工作项处理。
- Wayland 环境下截图与输入能力受限不阻塞 M1（M1 不依赖屏幕能力）。

## 测试与退出条件

- [ ] `debug`、`release`、`asan`、`ubsan` 预设构建通过，`ctest` 全绿且无 skip。
- [ ] 依赖锁定校验生效：篡改 `dependencies.lock.json` 中任一 commit 后 configure 必须
      失败（负向验证，验证后还原）。
- [ ] Mira Host 生命周期测试：正常启动-提交-完成、提交时拒绝、运行中取消、shutdown
      排空，全部经由可观察结果验证。
- [ ] 端到端冒烟：CLI 提交一个使用 Filesystem + Shell 的任务，任务在 Service 内完成
      并可 `task inspect` 观察到结构化结果与 Trace 关联。
- [ ] 公开头文件边界检查：`desktop`、`runtime` 目标的公共头不含 mira / mirador /
      executor include（编译测试或脚本断言）。

## 验证记录

2026-09-15：`M1-01` 项目初始化完成。

- 范围：建立 pinned 依赖（mira `255e4cf7f5ef1dd96aa20eab5d7532b830e8f70b`、mirador
  `fbcf6c44072694d79918e1de3552c497d7e6907d`）、`dependencies.lock.json`（含 executor /
  mbedtls / googletest 嵌套 pin 登记）、`cmake/MirageDependencies.cmake` configure 校验、
  CMake 预设与骨架目标（`mirage_desktop`、`mirage_mira_host`、`mirage_runtime_service`、
  `mirage_integration_mira`、`mirage_integration_mirador`、`mirage_platform`、`mirage`
  CLI）、依赖接线冒烟测试与 Desktop Observation 默认值测试。
- 依据：设计文档第 17、18 节；`DEC-001`、`DEC-002`、`DEC-003`。
- 验证：Linux x64（Ubuntu 24.04，GCC 13.3.0，CMake 3.28.3，Ninja），
  `cmake --preset debug` configure 通过（依赖 commit 校验通过，OpenSSL 3.0.13 适配器
  可用），`cmake --build --preset debug` 通过，`ctest --preset debug` 2/2 通过
  （`dependency_wiring_test`、`desktop_observation_test`），
  `./build/debug/apps/mirage --version` 输出 mirage 0.1.0 / mira core 0.1.0 / platform
  linux。完整预设矩阵与负向校验由后续验证任务覆盖。
- 限制：asan / ubsan / tsan 预设、Windows 交叉检查、格式检查目标未在本条验证；`M1-01`
  勾选以主循环构建与测试通过为据，独立复验记录随后续验证记录补充。
- 同步：总计划状态、本里程碑工作项、`DEC-001..003`、根 `AGENTS.md`、
  `docs/project/project-standards.md`。

2026-09-15：`M1-01` 独立复验（Independent-Verification-Agent）。

- 范围：独立复验 `M1-01` 的构建、测试与供应链校验，未修改任何源码与文档。
- 依据：同上；复验范围覆盖 M1 退出条件中与初始化相关的项。
- 验证：Linux x64（Ubuntu 24.04，GCC 13.3.0，CMake 3.28.3，Ninja 1.13.2，
  clang-format 18.1.3）：
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` configure + build + ctest 全部
    通过，2/2 测试、0 skip；`tsan` 默认执行 0/2（`ThreadSanitizer: unexpected memory
    mapping`），对照实验 `cd build/tsan && setarch $(uname -m) -R ctest` 为 2/2 通过，
    证明失败源于本机内核 ASLR 熵与 GCC TSan 的已知不兼容，非代码缺陷。
  - CLI 冒烟：`--version` 输出逐行匹配预期；`--help` 退出 0；未知命令退出 2。
  - 依赖锁负向验证：篡改 `dependencies.lock.json` 中 mira commit 为 40 个 `a` 后
    configure 以 commit mismatch FATAL_ERROR 失败；sha256 校验确认还原后字节一致。
    `-DMIRAGE_FETCH_DEPENDENCIES=OFF` 只校验模式 configure 通过，验证后缓存恢复默认
    `ON`。
  - 格式：`mirage-format-check` 目标通过。
  - 边界：`desktop` / `runtime/*/include` / `platform/include` 公共头对第三方 include
    的严格正则零命中（`runtime/mira_host.hpp` 命中处为文档注释；`src/mira_host.cpp`
    的 `<mira/version.hpp>` 位于允许的集成层）。
  - 子模块：mira `255e4cf7…`、mirador `fbcf6c4…` 与锁文件一致，嵌套 executor /
    mbedtls / googletest 逐一匹配，两个子模块工作树干净。
  - 文档链接：README / AGENTS / docs 共 10 个 markdown、27 条相对链接，0 断链。
- 限制：`tsan` 预设在本机需 `setarch $(uname -m) -R ctest` 运行（或调低
  `vm.mmap_rnd_bits`），已记为环境注意事项；`MIRAGE_FETCH_DEPENDENCIES=ON` 的"缺失
  子模块自动拉取"分支未测（所有子模块已检出，触发需删除后重建，留待 CI 覆盖）；
  asan/ubsan/tsan 目前仅覆盖两个轻量测试，净化器结论覆盖面随 M1 后续工作项扩展。
- 同步：本验证记录、README 构建（tsan 注意事项）。

2026-09-16：`M1-02` Mira Host 生命周期完成。

- 范围：`runtime/mira_host` 从占位骨架替换为真实宿主——`MiraHost` 以 pimpl 持有 pinned
  `MiraRuntime`，实现 `start()`（pinned 初始化 → 绑定环境打开主会话）、`submit_task` /
  `cancel_task` / `complete_task` / `task_view` 与有序 `shutdown()`
  （`request_shutdown` → 排空等待 → `finish_shutdown`），析构与失败路径复用同一释放
  顺序；`HostStatus` 五态状态集冻结（`Stopped/Starting/Running/Stopping/Failed`，终态
  幂等不可复活）并写入设计文档第 11.1 节，登记
  [DEC-004](../decisions/DEC-004-mira-host-status-set.md)；环境绑定接口
  `integration/mira::DesktopEnvironmentBinding` 固化为 pinned-free 契约桥（具体适配器
  经运行时 cross-cast 恢复 pinned 环境契约，未携带契约的绑定 fail closed）。
  `Mira::core` 降为 `mirage_mira_host` 的 PRIVATE 依赖，公共头保持 pinned-free。
- 依据：设计文档第 11、12、17 节；`DEC-001`..`004`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  CMake 3.28.3，Ninja，clang-format 18.1.3）：
  - 新增 `tests/runtime/mira_host_test.cpp`（12 场景 95 断言）：null binding 拒绝、
    无 pinned 契约 binding fail closed、正常启动-提交-完成-关闭、启动前提交拒绝
    （invalid_state）、运行中取消（Idle → Cancelled）、终态不复活（complete 后 cancel
    实测被 pinned 以 InvalidState 拒绝，宿主呈现 `pinned_runtime` 错误）、双 start /
    空 goal / 非法任务 id / 未知任务 id 负向用例、shutdown 排空（在途任务被 pinned
    取消后 clean 关闭、关闭后提交拒绝）、`host_status_name` 稳定串；另含 restart
    对抗场景（干净关闭后原实例不可再宿主，进 `Failed` 且不可复活）。
  - 冒烟 `dependency_wiring_test` 迁移到真实 `MiraHost`（skeleton 已删除），版本断言
    保留；因 `Mira::core` 转 PRIVATE，测试目标显式补链接。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` / `tsan` configure+build+ctest
    3/3 通过、0 skip；`tsan` 按[本机注意事项](../../README.md)以
    `setarch $(uname -m) -R ctest` 运行（未加 setarch 复现 `unexpected memory
    mapping`，属已知 ASLR 环境怪癖）。
  - `mirage-format-check` 通过；公共头边界：`runtime/*/include`、`desktop/*/include`、
    `platform/include` 对 `mira/`、`mirador/`、`executor/` include 及标识符零命中。
- 限制：宿主 `Failed` 路径仅能由 pinned 初始化/会话失败触发，真实依赖下无法不改实现
  地注入任意运行中故障，故障注入用例留待具备注入钩子后补；绑定适配器的真实实现与
  Agent 可观察的结构化结果属 `M1-03`。
- 同步：设计文档第 11.1 节、`DEC-004`、总计划里程碑状态、本验证记录。

2026-09-16：`M1-03` Desktop Environment 绑定适配器完成。

- 范围：`integration/mira` 从纯契约桥扩展为真实绑定——具体适配器
  `MiraEnvironmentBinding` 最派生类型同时实现 `DesktopEnvironmentBinding` 与 pinned
  `mira::IEnvironment`，包装 `mirage::desktop::DesktopEnvironment`；pinned 感知/输入
  面按 M1 能力集如实适配（capabilities 全空集、observe 对 required 超集 fail closed
  返回 `UnsupportedCapability`、空 required 返回最小无组件 Observation、execute 在
  副作用前拒绝、interrupt 幂等）。desktop 层新增 `FilesystemProvider`（只读
  `read_text_file`）、`ProcessProvider`（有界 shell `execute`，超时 + 逐流输出预算）
  与 `DesktopEnvironment::filesystem()`/`process()` 访问器（缺位返回 null，消费方
  fail closed）。M1 参考后端 `platform/linux::LinuxDesktopEnvironment` 以 std
  filesystem 读文件、POSIX fork/exec `/bin/sh -c` 执行命令：独立进程组（父子双侧
  setpgid）、poll 读取、超时与返回前整组 SIGKILL 收尾、阻塞 waitpid 回收不留僵尸。
  `MiraHost` 新增 pinned-free 操作面 `begin_operation` / `admit_operation_completion`
  （`OperationTicket`），宿主侧驱动循环以此括起桌面动作，使其以 pinned
  OperationRecord 进入控制面；stale/duplicate 完成按 pinned NoOp 幂等呈现，任务终态
  不复活。绑定形态与过渡期边界登记
  [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)。
- 依据：设计文档第 5、9、11、17 节；`DEC-001`..`004`、`DEC-008`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  CMake 3.28.3，Ninja，clang-format 18.1.3，两轮）：
  - 第一轮发现参考后端进程收尾路径两个缺陷：直接子进程先退出、孙进程仍持有捕获
    管道时整组 kill 不触发，孙进程以孤儿存活满 30 秒；deadline kill 后未确认回收
    即返回，向调用方泄漏僵尸。修复为无条件整组收尾 + 阻塞回收后复验通过。
  - 新增 `tests/integration/mira_binding_test.cpp`（9 场景 91 断言）：绑定身份与能力
    如实性、observe fail closed / 最小观察、execute 拒绝与 interrupt 幂等、文件读取
    正负（缺失/目录/空路径）、进程执行（exit code、stdout/stderr 捕获）、超时预算
    （sleep 30 用 200ms 预算 5 秒内返回）、非法参数、操作面非法身份拒绝、端到端任务
    （start → submit → begin_operation → 读文件 → admit → begin_operation → 执行
    shell → admit → complete_task → `Completed`，结构化结果逐一断言）、取消后迟到
    完成不复活任务。Independent-Verification-Agent 另补充
    `tests/integration/desktop_provider_boundary_test.cpp`（6 场景 55 断言）：超时整组
    清理（含直接子进程先退出的孙进程场景）、重复/迟到操作完成幂等且终态不复活、
    操作面宿主状态门（Stopped/关闭后 invalid_state）、截断与预算边界（16/64 截断、
    恰好 64 不截断、零预算拒绝）、文件系统边界（空文件、permission_denied）。
  - 端到端断言经测试源码与 `mira_host.cpp` 双向核实真实经过 pinned 运行时
    （submit_task / begin_operation / admit_operation_completion / complete_task）。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` / `tsan` configure + build +
    ctest 5/5 通过、0 skip；`tsan` 直跑复现 `unexpected memory mapping`（[本机注意
    事项](../../README.md)），`setarch $(uname -m) -R ctest` 5/5 通过。
  - 收尾路径重点复跑：boundary 测试 debug 连续 10 次、asan/ubsan/tsan 各 3 次均
    55/0，每轮系统级抽查 `sleep 30` 残留 0、全系统僵尸 0；对抗探针（重定向输出的
    游离孙进程在预算内正常完成）验证返回后被整组收尾。
  - `mirage-format-check` 通过；公共头边界：`desktop/*/include`、`runtime/*/include`、
    `platform/include`、`platform/linux/include` 对 `mira/`、`mirador/`、`executor/`
    include 零命中；`OperationTicket` 等新增宿主公共类型 pinned-free。
- 限制：M1 无模型驱动的 Agent 循环，端到端任务由宿主侧驱动循环推进（`DEC-008`
  第 2 条，pinned 上游提供宿主环境工具表面后迁移）；Provider 尚无路径范围约束与
  Permission 判定（`M1-05` / `M1-06` 收紧，`RULE-05` 于 M1-06 落地），该绑定在收紧前
  仅限开发与测试拓扑；预算内正常完成时游离后台后代（`cmd & disown` 类）会被整组
  终止，语义演进随 M1-05 取消路径加固复核；Windows 分支本机不可编译验证（M4）；
  `dependency_wiring_test` 的依赖供应链负向校验已在 `M1-01` 覆盖，本轮未重复。
- 同步：设计文档第 5、11.1 节、`DEC-008`、总计划里程碑状态、本验证记录、
  `desktop/process` 契约注释。

2026-09-16：`M1-04` Runtime Service 与 Local IPC 完成。

- 范围：[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md) 定案并落地——
  新增 `runtime/ipc`（`endpoint` 默认路径解析、4 字节小端长度前缀帧（1 MiB 上限）、
  协议 v1 消息面（pinned-free C++ 结构 + `mira::JsonValue` JSON 实现收在实现文件）、
  POSIX Unix domain socket 传输（非阻塞流、0700 目录、陈旧 socket 探测接管）、阻塞
  一次性 `IpcClient`）；`runtime/service` 从占位骨架替换为真实 `RuntimeService`——
  进程内唯一 `executor::Executor` 实例（`EXEC-01`），IPC 事件循环为专属 blocking
  I/O worker（poll + 双自检管道唤醒），全部 `MiraHost` 操作经一个
  `SerialExecutionContext` 串行化（宿主单线程所有权纪律），M1 任务驱动器以
  `submit_cancellable` 承载（step 间检查停止令牌，`begin_operation` /
  `admit_operation_completion` 括起桌面动作，结构化结果与 operation id 入注册表，
  fail-fast 结算），响应回投走 `executor::comm::MpscChannel`，停机序列 = 停止
  accept → 取消驱动 → 排空 executor → `host.shutdown()`（非 worker 线程收尾）；
  新增 `apps/service`（`mirage-service`，前台运行，SIGINT/SIGTERM 经自管道接入停机），
  CLI 新增 `service start|status|shutdown` 与 `task submit|list|inspect`
  （fork + exec 兄弟二进制并等待就绪，退出码 0/1/2/3）；
  `DesktopEnvironmentBinding` 新增 `bound_environment()` 访问器（默认 null 向后兼容，
  [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  变更记录）；任务 steps 为 DEC-007 第 5 条的过渡输入形态。
- 依据：设计文档第 12（新增 12.1 落地形态）、17、18 节；`DEC-001`..`004`、`DEC-006`、
  `DEC-007`、`DEC-008`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  CMake 3.28.3，Ninja，clang-format 18.1.3，两轮）：
  - 新增 `tests/runtime/ipc_protocol_test.cpp`（25 场景 267 断言）：帧 round-trip /
    半包逐字节 / 粘包两帧 / 超长声明长度 ProtocolError / 恰好 1 MiB 上限；五类请求
    与各响应形状 round-trip（含 correlation id 保留、`InspectTask` 有无 success
    两态）、错误响应、23 种非法请求 + 17 种非法响应的稳定拒绝；真实 UDS 传输双向
    （300 KiB 分片）、重复 bind 拒绝、关闭删 socket、普通文件与孤儿 socket 接管、
    自动建 0700 目录、`endpoint_has_listener` 语义、`IpcClient` unavailable 与成功
    路径。
  - 新增 `tests/runtime/runtime_service_test.cpp`（17 场景 172 断言）：hello 身份、
    start/run 前置拒绝、null binding 与无环境 fail closed、终态不可重启、steps 任务
    Completed + operation id（32 位小写十六进制）+ 结构化结果（文件内容、exit code、
    stdout）、fail-fast（失败步 failed + 后续步 skipped + 金丝雀步未执行）、结果
    截断、`not_found` / 空 goal `invalid_argument` / 超长 goal / 超步数 / 超长参数、
    注册表容量 `invalid_state`、同连接顺序复用、3 连接交错、垃圾 JSON →
    `protocol_error` 后关闭、pipeline 帧违规与超限帧均"先错误帧后关闭"、IPC
    shutdown（空闲与在途 `sleep 2` 两种拓扑下 clean 且停机有界）、自管道
    `register_shutdown_fd`、默认 socket 路径解析。测试全程单进程单线程，未用
    `std::thread`/`std::async`。
  - 第一轮发现三个实现缺陷并修复后第二轮复验通过：协议违规错误帧被立即关闭吞掉
    （service_loop 引入独立 violation 结果，先投递错误帧再关闭）；空 goal 错误码
    归类（解码层仅要求存在与类型，语义 `invalid_argument` 归服务层）；release 预设
    `-Wunused-result` 构建失败（三处 `::write` 改为消费返回值）。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` / `tsan` configure + build +
    ctest 均 7/7 通过、0 skip；`tsan` 按[本机注意事项](../../README.md)以
    `setarch $(uname -m) -R ctest` 运行，无 race 报告。
  - 端到端冒烟（真实进程）：`mirage service start`（fork + exec + 就绪探测）→
    `task submit --read/--exec` → `task list` → `task inspect`（Completed、结构化
    结果与 operation id 可观察）→ `service shutdown`（进程退出、socket 文件删除、
    同路径可重启）；独立进程 SIGTERM → `stopped cleanly`；CLI 误用与不可达路径
    退出码（0/1/2/3）符合设计。
  - `mirage-format-check` 通过；公共头边界：`runtime/*/include`、`desktop/*/include`、
    `platform/include`、`platform/linux/include` 对 `mira/`、`mirador/`、`executor/`
    include 零命中（例外为允许项 `mira_environment_binding.hpp` 的
    `<mira/environment.hpp>`）。
- 限制：`max_connections` 容量拒绝与 SIGINT（与 SIGTERM 共用处理路径）未单独构造
  场景（验收未要求，M5 产品化时补充）；任务 steps 为 DEC-007 过渡形态，`M1-05` /
  `M1-06` 在此边界内收紧（step 预算与取消路径硬化、Permission 判定接入），pinned
  上游提供宿主环境工具表面后随 DEC-008 迁移；socket 访问控制仅依赖 0700 目录权限，
  peer credentials 校验留 M5 复核；Windows 命名管道传输属 M4。
- 同步：设计文档第 12.1、17 节、`DEC-007`（新）、`DEC-008`（变更记录）、总计划
  里程碑状态、本验证记录、`README` 运行说明。

2026-09-16：`M1-05` Filesystem / Process Provider 收紧完成。

- 范围：desktop 层新增 pinned-free 原语 `desktop::CancelToken`（拷贝共享状态的
  协作取消标志）与 `desktop::PathScope`（canonical 逐组件读范围包含，空范围
  fail closed）；`FilesystemProvider` 纯虚签名追加 `FileReadLimits`（默认 1 MiB，
  超限 `file_too_large` 不静默截断）与取消参数，`ProcessLimits` 新增
  `max_command_bytes`（默认 64 KiB，fork 前拒绝），`ProcessOutcome` 新增
  `cancelled`。`platform/linux::LinuxDesktopEnvironment` 构造声明读范围（默认空 =
  拒绝一切读取；`mirage-service` 以可重复 `--read-root` 声明）：范围检查先于存在
  检查，越界一律 `permission_denied`，symlink / `..` / 兄弟目录逃逸均被 canonical
  包含判定拒绝；读取为 64 KiB 分块、逐块检查预算与取消；执行 poll 以 25 ms 切片
  观察 token，取消复用超时路径的整组 SIGKILL + 阻塞回收。Runtime Service 每任务
  持有 CancelToken，`task.cancel` IPC（协议 v1 扩展，DEC-007 变更记录）按
  token → driver 停止令牌 → `MiraHost::cancel_task` 顺序传播；被中断步标记新
  step 状态 `cancelled`、后续 `skipped`、终态由 pinned 结算不复活（`RULE-04`）；
  有序停机同样先请求任务 token 使排空有界。CLI 新增 `task cancel <id>`。
  范围、预算与取消语义登记 [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)。
- 依据：设计文档第 5、12.1、15 节；`DEC-001`..`004`、`DEC-007`、`DEC-008`、
  `DEC-009`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  CMake 3.28.3，Ninja，clang-format 18.1.3）：
  - 新增 `tests/desktop/provider_hardening_test.cpp`（10 场景 96 断言）：CancelToken
    语义（默认不可取消/拷贝共享/幂等）、PathScope 单元语义（空 scope、root "/"、
    非绝对 root、"/a/b" 拒 "/a/bc"、不存在叶子）、空范围全拒、越界三形态
    （兄弟目录 / `..` 逃逸 / symlink 逃逸且消息不泄露解析目标）、root 即文件可读、
    读预算边界（恰好 max_bytes / +1 fail 且无内容 / 0 invalid_argument）、读取消、
    命令预算 fork 前拒绝（恰边界成功、+1 拒绝且 canary 未产生、0 拒绝）、执行中
    取消（<3s 返回、cancelled、已捕获输出保留、含后台子进程整组无残留）。
  - 新增 `tests/runtime/task_cancel_test.cpp`（4 场景 48 断言）：运行中任务取消
    （step_timeout 60s 排除超时掩盖，poll 至 step running 后取消，2s 内收敛
    `Cancelled` / step0 `cancelled` / 后续 `skipped`、canary 未执行、无残留）、
    未知 id `not_found`、终态任务取消被拒（`pinned_runtime` + `invalid_state:`
    message）且终态不变、取消后停机有界且 clean。
  - 更新既有测试以适配空范围默认拒绝的新契约：`mira_binding_test`（95 断言，
    读场景 scoped 化 + 默认构造拒绝用例）、`desktop_provider_boundary_test`
    （57 断言，文件边界场景 scoped 化 + `..` 逃逸负向）、
    `runtime_service_test`（172 断言，binding 辅助统一带 TempDir scope）、
    `ipc_protocol_test`（299 断言，新增 task.cancel / TaskCancelled round-trip
    与 11 条非法变体拒绝）。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` / `tsan` configure + build +
    ctest 均 **9/9 通过、0 skip**；`tsan` 按[本机注意事项](../../README.md)以
    `setarch $(uname -m) -R ctest` 运行。
  - `mirage-format-check` 通过；公共头边界：`desktop/*/include`、
    `runtime/*/include`、`platform/include`、`platform/linux/include` 对
    `mira/`、`mirador/`、`executor/` include 零命中（新增
    `cancellation.hpp` / `path_scope.hpp` 为纯 std 类型）。
  - 端到端冒烟（真实进程）：`mirage-service --read-root <ws>` 启动（输出
    `filesystem read scope: 1 root(s)`）→ 范围内 `task submit --read/--exec`
    Completed 且结构化结果可观察 → 范围外读取 `permission_denied` →
    `sleep 30` 任务 `task cancel`（输出 `cancel requested (Cancelled)`、inspect
    step0 `cancelled`、无进程残留）→ CLI 误用/不可达/未知 id 退出码 2/3/1 →
    `service shutdown` 干净退出、socket 删除。
  - 主循环预检：debug 构建、format-check 与同拓扑手动冒烟先行通过后委派独立
    验证；实现缺陷未发现，仅修正一处协议注释（终态取消 wire code 实为
    `pinned_runtime` 透传，与 `task.submit` 形态一致）。
- 限制：读取中文件增长的运行期预算检查路径无法在单线程 harness 确定性构造，
  仅由实现覆盖（预检 + 逐块检查双保险），待后续注入测试；CLI 取消路径以手工
  端到端冒烟验证，未固化为 ctest；范围检查与 open 之间的 TOCTOU 窗口由参考
  后端接受（完整 Linux Backend 于 M2 以 `openat2(RESOLVE_BENEATH)` 类机制收敛，
  DEC-009）；取消延迟以 25 ms poll 切片为上界，非即时中断；读范围不约束
  `/bin/sh -c` 的内部文件访问，命令级约束属 `M1-06` Permission 判定与后续
  沙箱化；Windows 命名管道与 Backend 属 M4。
- 同步：设计文档第 5、12.1 节、`DEC-007`（变更记录）、`DEC-008`（变更记录）、
  `DEC-009`（新）、总计划里程碑状态、本验证记录、`README` 运行说明。

2026-09-16：`M1-06` Desktop Permission 框架雏形完成。

- 范围：新增 `runtime/permission` 目标（pinned-free 纯 std，设计文档第 17 节
  `runtime/permission/` 落位）——`Capability` 词表（`filesystem.read` /
  `filesystem.write` / `process.execute`，稳定字符串名与解析）、每能力
  `Rule`（`allow`/`confirm`/`deny`）、`PermissionController::authorize()`
  四态决策（`allowed`/`confirmed`/`denied`/`confirmation_rejected`）与同步
  用户确认挂点 `ConfirmationHandler`（默认 `DenyAllConfirmation` fail
  closed，`AllowAllConfirmation` 供开发/测试显式选择）。Runtime Service 每
  任务驱动步在 pinned 操作准入与任何副作用之前判定（controller 缺位同样
  fail closed）；决策以稳定字符串记入 StepRecord 并经协议 v1 附加字段
  `StepView.permission`（DEC-007 变更记录）回传 `task.inspect`；被拒步以
  `permission_denied` 结算（无 operation id，不进 pinned 控制面）、后续步
  skipped、任务 fail-fast 结算 Failed。`mirage-service` 新增 `--perm
  CAPABILITY=allow|confirm|deny`（可重复）与 `--confirm allow|deny`（默认
  deny），启动时打印生效策略；`mirage service start` 本地预校验并转发两旗
  标，`task inspect` 显示 `perm=` 决策。默认策略 read=allow / write=deny /
  execute=allow（保持 M1-05 拓扑行为，收紧经显式配置）。判定语义登记
  [DEC-010](../decisions/DEC-010-m1-permission-framework.md)；desktop
  Provider 保持 permission-agnostic，PathScope/预算/取消硬边界不受判定结果
  影响（与 DEC-009 叠加）。
- 依据：设计文档第 5、12.1、15、17 节；`DEC-001`..`004`、`DEC-007`..`010`；
  `RULE-05`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  CMake 3.28.3，Ninja，clang-format 18.1.3）：
  - 新增 `tests/runtime/permission_test.cpp`（10 场景 59 断言）：三类稳定名
    与解析负向（空串、残缺、大小写、前后缀污染名均 nullopt）、默认策略恰为
    Allow/Deny/Allow、Allow/Deny 不触碰挂点（fake handler 计数 0）、Deny
    reason 稳定含能力名、Confirm 恰好调用一次且两向决策/reason 逐字断言、
    PermissionRequest 四个 trace 字段原样到达挂点、DenyAll/AllowAll 返回值、
    策略单槽改写不扰动其他槽。
  - 新增 `tests/runtime/task_permission_test.cpp`（5 场景 69 断言，真实
    RuntimeService + IPC）：默认策略 read+exec Completed 且步 `allowed` +
    32 位 hex operation id + canary 文件实际产生（放行路径真触达桌面）、
    exec=Deny 步 failed + `permission_denied:` 前缀 + 无 operation id +
    后续 skipped + 任务 Failed + canary 不存在（fork 前拒绝）+ 停机 clean、
    read=Confirm 默认拒绝（`confirmation_rejected` + 稳定 reason）、
    read=Confirm + AllowAll 注入后 `confirmed` + Completed + 内容可观察、
    write 规则改写不扰动其他规则。
  - 扩展 `tests/runtime/ipc_protocol_test.cpp`（313 断言，+7）：StepView
    `permission` 字段 round-trip（`allowed`/`confirmed`/缺省为空）与 legacy
    无该字段 step 的向后兼容解码。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` configure + build +
    ctest 均 **11/11 通过、0 skip**；`tsan` 直跑 11/11 复现
    `unexpected memory mapping`（[本机注意事项](../../README.md)，含纯 std
    的 permission_test，属 ASLR 环境怪癖），`setarch $(uname -m) -R ctest`
    11/11 通过、无 race 报告。
  - `mirage-format-check` 通过；公共头边界：`runtime/permission/include`、
    `runtime/service/include`、`runtime/ipc/include`、`desktop/*/include`、
    `platform/include`、`platform/linux/include` 对 `mira/`、`mirador/`、
    `executor/` include 零命中（唯一例外仍为允许项
    `mira_environment_binding.hpp`）。
  - 端到端冒烟（真实进程，经 CLI 转发拉起）：`--perm process.execute=deny`
    策略行与 `perm=denied` / `error: permission_denied: process.execute
    denied by policy` 原文符合、canary 未产生；范围内 read `perm=allowed`
    Completed；`--perm filesystem.read=confirm`（默认拒绝挂点）
    `perm=confirmation_rejected`；`--confirm allow` 后 `perm=confirmed`
    Completed；`--perm bogus=allow` 退出码 2 且 fork 前拦截；收尾 0 残留
    进程、0 残留 socket。
  - 实现缺陷：未发现（独立验证零实现修改）。
- 限制：`filesystem.write` 在 M1 无 Provider 可触达，集成层仅验证"策略可配
  置且不影响其他规则"，write 动作判定链路随 M2 写 Provider 落地时补；确认
  挂点为同步进程内契约（必须立即返回），产品确认流（持久化待确认请求、UI
  交互、超时收敛）待 M5 以 Local IPC 异步面落地时另立决策；拒绝步的 trace
  落在 Mirage 侧步记录，不产生 pinned OperationRecord（DEC-010 备选方案
  3）；冒烟中发现 `BUG-20260916-001`（`service start` 子进程继承 stdout 致
  管道读端无 EOF，M1-04 遗留），见"风险与阻塞"；Windows 平台路径属 M4。
- 同步：设计文档第 5、12.1、15 节、`DEC-010`（新）、`DEC-007`（变更记录）、
  总计划里程碑状态、本验证记录、`README` 运行说明。
