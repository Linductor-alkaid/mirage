# DEC-003：仓库布局与分层依赖方向

> 状态：Accepted
> 日期：2026-09-15
> 负责人：Mirage 维护者
> 冻结里程碑：M1（目录骨架已建立；目标随里程碑增量落地）
> 替代/被替代：无

## 背景与问题

设计文档第 17 节给出推荐代码结构（`apps/`、`runtime/`、`desktop/`、`platform/`、
`integration/`、`ui/`、`tests/`）。初始化需要把该结构落为 CMake 目标图，固定依赖方向，
并明确"公共 API 不暴露第三方类型"边界的执行方式。

## 决策

- 目录按设计文档第 17 节建立；首版只创建有内容的目标，其余目录以 `.gitkeep` 占位并
  由里程碑工作项填充（占位目录在总计划工作项中一一对应）。
- CMake 目标与依赖方向（箭头指向被依赖方）：

  ```text
  apps/mirage (CLI)
      -> runtime: mirage_runtime_service -> mirage_mira_host -> Mira::core (pinned)
                                        \\-> mirage_desktop
      -> integration: mirage_integration_mira (INTERFACE, 契约适配)
                      mirage_integration_mirador -> mirador::core (pinned, PRIVATE)
      -> platform: mirage_platform (Linux / Windows 源二选一)
  ```

- 边界执行方式：
  - `integration/*` 是 pinned 依赖头文件的唯一包含层；`mirage_integration_mirador`
    以 PRIVATE 链接 mirador 并在源内完成类型翻译（如 `VisualBackendIdentity`），
    公共头只出现 Mirage 自有类型。
  - `mirage_mira_host` 是 Mira 身份与实例生命周期的唯一宿主，链接 `Mira::core`；
    其公共头仅暴露 Mirage 自有类型（版本以值结构体返回）。
  - `desktop` 与 `platform` 不得包含任何第三方头文件；平台相关源文件由
    `platform/CMakeLists.txt` 按 `CMAKE_SYSTEM_NAME` 选择，其余平台 configure 即
    失败（设计文档范围仅 Linux / Windows）。
- 编译期边界由构建图与测试锁定；后续以脚本/编译测试固化"公共头不得 include 第三方
  路径"断言（M1 退出条件之一）。

## 备选方案

- 扁平 `src/` 布局 + 命名空间区分：设计文档已给出目录契约，且分层目录使违规依赖在
  review 与构建图中可见，不采用。
- mirador 头文件经 integration 层透传（PUBLIC 链接）：会让 mirador 类型进入 Mirage
  公共接口，违反工程规范第 11 节，否决（初始化过程中曾出现该形态，已在本决策下改为
  源内翻译）。

## 影响与风险

- 每增加一个 Provider / Backend 都需要维护翻译边界，集成层代码量略增；换取 pinned
  依赖可升级性与跨平台接口稳定。
- `apps/desktop` / `apps/tray` / `ui/*` 在 M5 落地，占位目录在此之前为空目录，属于
  计划内状态。

## 验证方式

- 构建图检查：`cmake --graphviz` 或对 `target_link_libraries` 的评审。
- 边界断言进入 M1 退出条件（公共头 include 检查）。
- `tests/smoke/dependency_wiring_test.cpp` 验证 pinned 依赖真实链接而非仅头文件可见。

## 关联文档和工作项

- 设计文档第 17 节（推荐代码结构）。
- 工程规范：[project-standards.md](../project/project-standards.md) 第 11 节。
- 总计划：`RULE-01`、`RULE-02`、`M1-01`。
- 关联：[DEC-001](DEC-001-dependency-pinning.md)、
  [DEC-002](DEC-002-build-test-baseline.md)。
