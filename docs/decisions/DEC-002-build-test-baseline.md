# DEC-002：构建、预设与测试基线

> 状态：Accepted（"Mbed TLS 适配器默认关闭"为暂定默认值，冻结于 M2）
> 日期：2026-09-15
> 负责人：Mirage 维护者
> 冻结里程碑：M1（构建基线）/ M2（TLS 默认值复核）
> 替代/被替代：无

## 背景与问题

Mirage 需要确定 C++ 标准、CMake 版本、构建预设矩阵、测试框架选型与依赖构建开关，作为
所有里程碑的验证基线（工程规范第 11 节）。

## 决策

- 语言标准 C++20，CMake ≥ 3.25，生成器 Ninja；`CMakePresets.json` 提供 `debug`、
  `release`、`asan`、`ubsan`、`tsan` 五个 configure/build/test 预设，sanitizer 以
  `CMAKE_CXX_FLAGS_INIT` 全树生效（覆盖 Mirage 与源码树内构建的依赖目标）。
- 测试采用与 pinned mira 一致的轻量 harness（`tests/support/test.hpp`，`MIRAGE_CHECK`
  + 裸 `main` + `add_test`），不引入外部测试框架；ctest 预设 `noTestsAction=error`，
  测试打 `unit` / `integration` / `smoke` 等标签驱动 CI 选择。测试规模超出该 harness
  的表达力时，先经决策记录再引入框架。
- Mirage 自身目标统一经 `cmake/MirageWarnings.cmake` 启用告警集，
  `MIRAGE_WARNINGS_AS_ERRORS=ON`（默认）下告警即错误；`.clang-format` / `.clang-tidy`
  与 pinned mira 对齐，`mirage-format-check` 目标复用其检查脚本。
- 依赖构建开关（在 Mirage 消费面强制）：
  - `MIRA_BUILD_TESTS=OFF`、`MIRADOR_BUILD_TESTS/BENCHMARKS/EXAMPLES=OFF`；
  - `MIRA_WITH_OPENSSL` 保持 mira 默认（系统存在即构建 TLS 适配器，不存在则 https
    在传输边界 fail closed，不阻塞配置）；
  - **暂定默认值**：`MIRA_WITH_MBEDTLS=OFF`（经 `MIRAGE_WITH_MIRA_MBEDTLS` 透传），
    避免无谓的 mbedtls 全量构建；接入真实模型网关需要 Mbed TLS 通道时在 M2 复核并
    记录证据。负责人：Mirage 维护者；最迟冻结：M2。

## 备选方案

- Google Test / Catch2：能力更强，但引入新供应链依赖与 pinned 目标的隔离问题；mira /
  mirador 自身均已收敛于自研 harness（mirador 用 googletest，mira 用自研），Mirage 处
  于消费侧，测试密度在 M1 阶段不足以支付框架成本。
- 每项目 sanitizer 选项透传（如 `MIRA_ENABLE_ASAN`）：需在三层依赖中逐一映射，预设级
  全局 flags 更简单且语义一致。

## 影响与风险

- 全局 sanitizer flags 会让 sqlite / mbedtls 等第三方编译单元也参与插桩，构建时间与
  运行开销略增；换取"预设即完整验证环境"的一致性。
- 自研 harness 无 fixture / 参数化能力；复杂场景测试的组织成本上升，必要时经决策引入
  框架。

## 验证方式

- 五预设构建 + ctest 通过记录进入 M1 验证记录（暂缺预设补跑时在验证记录注明）。
- `mirage-format-check` 目标可在本地复现格式门禁。

## 关联文档和工作项

- 工程规范：[project-standards.md](../project/project-standards.md) 第 11 节。
- 总计划：`DOD-03`、`M1-01`。
- 关联：[DEC-001](DEC-001-dependency-pinning.md)（依赖消费开关）。
