# DEC-096：runtime 公共面的 pinned 类型边界策略与 apps 组合根消费形态

> 状态：Accepted
> 日期：2026-10-16
> 关联工作项：[依赖复用整改](../plans/dependency-reuse-remediation.md) REUSE-06/07/08
> 关联：AGENTS.md 工程约束（"公开 API 避免暴露平台与第三方类型"）、DEC-007、DEC-021、
> DEC-030（EXEC-01 每进程 owner 唯一）、DEC-037、DEC-045

## 背景与问题

2026-10-16 复用审查确认：`cmake/BoundaryCheck.cmake` 原实现只做字面 grep，
只覆盖"公共头直接 include pinned 头"，检测不到经 integration 公共头的传递路径：

- `runtime/service/include/mirage/runtime/runtime_service.hpp` 经
  `mirage/integration/desktop_tool_context.hpp`（→ `mira/tool_executor.hpp`）与
  `mirage/integration/model_layer.hpp`（→ `mira/model_contracts.hpp`）传递包含
  pinned 头；`ServiceConfig` 按值持有 `mirage::integration::BasicAgentToolsOptions`、
  `ModelLayerConfig` 与 `shared_ptr<ModelProviderOverride>`。
- `runtime/mira_host/include/mirage/runtime/mira_host.hpp` 经
  `workflow_event_bridge.hpp`（→ `mira/event_store.hpp`，且公开继承
  `mira::IEventStore`）、`desktop_atom_toolset.hpp`、`mira_environment_binding.hpp`
  （公开实现 `mira::IEnvironment`）传递包含 pinned 头。

而 runtime/service 的 CMake 注释宣称 "Public surface is pinned-free"——仅在
命名空间意义上成立，与事实有落差。apps 侧另有 kairo 消费路径各自硬编码嵌套 pin
的 include 目录（apps/native、apps/pet），新近的 CLI 更新命令也需要明确的消费形态。

## 决策

1. **直接 include 维持禁止**：desktop/runtime/platform 公共头直接 include
   `mira/`、`mirador/`、`kairo/` 头仍然违规，BoundaryCheck 原检查不变。
2. **经 integration 适配层的类型引用是合法边界**：runtime 公共面允许命名
   `mirage::integration` 适配层类型；integration 是仓库指定的 pinned 边界层，
   适配类型本身就是边界的产物。pinned 类型由此"经边界"到达消费方，不构成
   绕过 integration 的新路径。
3. **传递闭包可测**：BoundaryCheck 升级为传递解析——从每个公共头出发，沿
   `mirage/...` include 追踪进 desktop/runtime/platform/integration 的 include
   树，任何叶子到达 pinned include 即报告。已登记的传递缝
   （`runtime_service.hpp`、`mira_host.hpp`）以脚本内显式 allowlist 记录；
   **新增**的传递到达直接失败。
4. **apps 组合根消费形态**：每个需要并发的进程在组合根持有唯一的
   `kairo::Executor` owner（DEC-030 EXEC-01、DEC-037、DEC-045——CLI 更新命令、
   native、pet、tray/service 各自成立）。apps 对 kairo 的 include/link 一律经
   `Mirage::kairo_consumer`（`cmake/MirageKairoConsumer.cmake`：SYSTEM include +
   `$<TARGET_FILE:kairo::kairo>` 文件路径链接，即 runtime/service 的既有纪律，
   防 kairo 非 SYSTEM interface include 泄漏）；对 mira 核心 API（如
   `mira/event_store.hpp`）只经正规 target 链接（`Mira::core` 等）获取，
   不得自拼 include 目录。
5. **迁移项**：ServiceConfig 配置面 pinned-free 化（前向声明/pimpl/自有值类型
   替代 `ModelLayerConfig` 内嵌的 `mira::ModelUsage` 观察者与
   `mira::IModelProvider` 工厂）作为后续工作项，由维护者评估排期；完成前以
   本决策与 allowlist 承载现状。

## 备选方案

- **全量前向声明/pimpl 去 pinned 化**：拒绝——`ModelLayerConfig`、
  `WorkflowEventBridge`（继承 `mira::IEventStore`）、`MiraEnvironmentBinding`
  （实现 `mira::IEnvironment`）的公开面结构性使用 pinned 类型，去化波及全部
  apps 消费方与 integration 接口，风险与收益不成比例；作为迁移项另议。
- **维持字面 grep 不登记**：拒绝——边界缺口不可见、不可测，违反"边界必须
  可测试"的完成定义。

## 影响与风险

- 新增传递检查后，任何公共头新增"经 mirage/ 到 pinned"的路径会被
  `mirage-boundary-check` 拦截；已登记缝是显式债务清单，不会静默增长。
- kairo 消费面统一后，apps 不再复制嵌套 pin 的路径常量；REUSE-13 将把
  tests/ 的存量硬编码路径迁到同一 facade。
- 风险：allowlist 可能被滥用为默认出口——缓解：allowlist 位于构建脚本内、
  每个 add 都出现在 diff 与 `mirage-boundary-check` 输出中，评审可见。

## 验证方式

- `cmake -DROOT_DIR=<src> -P cmake/BoundaryCheck.cmake`：当前树通过
  （60 头 0 违规、2 登记缝到达）；人为向任一非登记公共头注入
  `#include <mirage/integration/...>`（其传递包含 pinned 头）时检查失败。
- apps 全目标在 facade 迁移后构建通过（受影响测试组全绿）。

## 关联文档和工作项

- [依赖复用整改计划](../plans/dependency-reuse-remediation.md)（REUSE-06/07/08）
- `cmake/BoundaryCheck.cmake`（传递解析与 allowlist）
- `cmake/MirageKairoConsumer.cmake`（kairo 消费面）
