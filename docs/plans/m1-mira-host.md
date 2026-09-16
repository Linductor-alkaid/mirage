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
- [ ] `M1-04` Runtime Service 与 Local IPC：Service 独立于 GUI 生命周期运行，CLI 经
      IPC 完成 `task list` / `task submit` / `task inspect`；IPC 机制定案（DEC-007）。
- [ ] `M1-05` Filesystem / Process Provider：路径范围约束、命令执行预算与取消路径，
      负向用例覆盖越界访问与拒绝执行。
- [ ] `M1-06` Desktop Permission 框架雏形：`filesystem.read` / `filesystem.write` /
      `process.execute` Capability 判定与用户确认挂点（确认 UI 可延后到 M5）。
- [ ] `M1-07` 持久化骨架：Mirage 本地配置与 Runtime Recovery State 的存取（设计文档
      第 16 节中 M1 相关子集）。

## 风险与阻塞

- Mira Host 对 Mira 公开 API 的宿主形态（实例生命周期、事件订阅）依赖 mira 0.1.x 契约
  稳定性；若上游契约在 M1 期间变化，按依赖升级流程处理并同步锁文件。
- Local IPC 机制未定案（DEC-007 暂定默认值）；在定案前 `M1-04` 不进入实现。
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
