# M1：Mira Host 与基础 Runtime

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：无（项目初始化已随本里程碑工作项完成）
> 建议发布点：`release-alpha`
> 更新日期：2026-09-15

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

## 工作项

- [x] `M1-01` 项目初始化：pinned `mira` / `mirador` 子模块与
      `dependencies.lock.json`、CMake 骨架（`CMakePresets.json` 五预设）、设计文档
      第 17 节目录骨架、依赖接线冒烟测试（mira / mirador 真实链接验证）。
- [ ] `M1-02` Mira Host 实现可运行的宿主生命周期：初始化 / 关闭顺序、HostStatus 状态
      机（冻结状态集并写入设计文档）、桌面环境绑定接口，覆盖正常完成、取消与 shutdown
      测试。
- [ ] `M1-03` Desktop Environment 绑定适配器：经 `integration/mira` 把 Filesystem /
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
