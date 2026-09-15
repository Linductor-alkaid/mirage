# DEC-001：依赖以 Git submodule + dependencies.lock.json 锁定

> 状态：Accepted
> 日期：2026-09-15
> 负责人：Mirage 维护者
> 冻结里程碑：M1（已完成）
> 替代/被替代：无

## 背景与问题

Mirage 直接依赖 pinned `mira`（Agent Harness Core，内嵌 Executor 并发基础设施）与
pinned `mirador`（视觉基础设施）。工程规范第 9.1 节要求依赖锁定可离线校验、configure
阶段验证，且未经授权不得修改上游。需要选定一种锁定机制并落实校验工具。

## 决策

- 采用 **Git submodule + 锁文件**：`.gitmodules` 声明 `third_party/mira` 与
  `third_party/mirador`；`dependencies.lock.json`（schema 1，格式沿用 mira 仓库惯例）
  登记每个顶层依赖的 source、精确 commit、版本、许可证与消费方式，并登记需审计的嵌套
  pin（mira -> executor、mbedtls；mirador -> googletest）与 vendored 项（sqlite）。
- `cmake/MirageDependencies.cmake` 在 configure 时：
  - `MIRAGE_FETCH_DEPENDENCIES=ON`（默认）：缺失子模块自动 `git submodule update
    --init --recursive` 同步后校验；
  - `MIRAGE_FETCH_DEPENDENCIES=OFF`：只校验路径，缺失或漂移即 configure 失败
    （离线/CI 用途）；
  - 嵌套 pin 与父仓库 HEAD 的 gitlink 比对，防止子模块内部静默漂移。
- 消费方式为源码树内 `add_subdirectory(... EXCLUDE_FROM_ALL)`，经上游别名目标链接
  （`Mira::core`、`mirador::core`）；依赖自身的 tests / benchmarks / examples 一律
  关闭，由 Mirage 的 `tests/` 覆盖集成契约。

## 备选方案

- lock 清单 + CMake 拉取（`third_party/dependencies.lock` 逐行登记、configure 同步）：
  与 mira 仓库现行 submodule 惯例不一致，且本仓库依赖数量少，submodule 的离线可见性
  和 git diff 可审计性更好，故不采用。
- 系统包 / FetchContent 按版本号拉取：无法满足"精确 commit + configure 校验 + 离线可
  验证"的供应链要求，否决。
- 安装后 `find_package(Mira CONFIG)`：隔离性好，但失去源码级 sanitizer 联动与单仓
  冒烟验证的便利；留作 M2+ 的兼容性证据项再评估。

## 影响与风险

- 依赖升级 = submodule 指针 + `dependencies.lock.json` 同步变更，走独立 MR（工程规范
  10.7）；锁文件与指针不一致时 configure 失败（`RULE-06`）。
- mira 上游未随附 LICENSE 文件，锁文件如实登记为 `UNLICENSED` 并要求升级审计时向上游
  确认；mirador 为 MIT，executor 为 MIT（随 mira 登记）。
- `EXCLUDE_FROM_ALL` 下依赖目标仅在链接需要时构建，configure 校验与构建顺序解耦。

## 验证方式

- configure 日志输出每个顶层依赖与嵌套 pin 的 verified 行。
- 负向验证：篡改锁文件任一 commit 后 configure 失败（M1 退出条件之一）。
- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` 的
  通过记录进入 M1 验证记录。

## 关联文档和工作项

- 工程规范：[project-standards.md](../project/project-standards.md) 第 9.1、10.7 节。
- 总计划：`RULE-06`、`M1-01`（[M1](../plans/m1-mira-host.md)）。
