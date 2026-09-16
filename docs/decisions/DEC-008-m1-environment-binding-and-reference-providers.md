# DEC-008：M1 环境绑定适配器与 Filesystem / Process 参考实现边界

> 状态：Accepted
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1（M1-05 / M1-06 在此边界内收紧，不推翻形态）
> 替代/被替代：无

## 背景与问题

`M1-03` 要求经 `integration/mira` 把 Filesystem / Process Provider 暴露给 Mira，使
Agent 可提交一个读取文件并执行 Shell 命令的任务并观察到结构化结果。约束来自三方：

1. pinned mira 0.1.x 的 `IEnvironment` 是感知 + 输入契约（`capabilities` / `observe` /
   `execute` / `interrupt`），没有宿主环境可以声明 Filesystem / Process 能力的表面；
   `MiraRuntime` 是串行控制面，任务由外部驱动循环推进（`begin_operation` /
   `admit_operation_completion` 是协调者接入点），M1 阶段不存在模型驱动的 Agent 循环。
2. 设计文档第 5 节已把 FilesystemProvider / ProcessProvider 划为 M1 的 Desktop
   Environment Provider，第 11.1 节已固定绑定接口
   （`DesktopEnvironmentBinding` + 运行时 cross-cast 恢复 pinned 契约）形态。
3. `RULE-05`（副作用动作必须经 Permission 判定）的框架在 `M1-06` 落地，`M1-05` 落地
   路径范围约束与命令执行预算硬化；`M1-03` 需要明确过渡期边界。

## 决策

1. **绑定适配器形态**：`integration/mira::MiraEnvironmentBinding` 具体绑定，最派生
   类型同时实现 `mirage::integration::DesktopEnvironmentBinding` 与
   `mira::IEnvironment`，包装 `std::shared_ptr<mirage::desktop::DesktopEnvironment>`。
   pinned 面向 M1 环境能力集如实适配：`capabilities()` 报告空能力集；
   `observe()` 对超出能力集的 required 组件 fail closed（`UnsupportedCapability`），
   否则返回无组件的最小 Observation；`execute()` 在任何副作用前拒绝（`Rejected`
   回执）；`interrupt()` 为幂等成功。不虚报能力、不静默返回不完整观察。
2. **Filesystem / Process 的暴露路径**：以 mirage 自有 Provider 接口
   （`desktop::FilesystemProvider` 只读文本读取；`desktop::ProcessProvider` 有界
   shell 执行）挂到 `DesktopEnvironment::filesystem()` / `process()` 访问器。M1 的
   消费方是宿主侧驱动循环：每个桌面动作以宿主操作面
   （`MiraHost::begin_operation` / `admit_operation_completion`）括起，使动作以
   pinned OperationRecord 进入控制面视图；结构化结果即 Provider 返回值。pinned
   上游未来提供宿主环境工具表面（Tool/MCP 类）时，适配器迁移过去；这不是 Executor
   能力缺口，不登记反馈台账。
3. **M1 参考后端位置**：`platform/linux::LinuxDesktopEnvironment` 实现 desktop
   Provider 接口（std::filesystem 读取 + POSIX fork/exec shell 执行：独立进程组、
   poll 读取、超时 kill 进程组、输出按字节预算截断）。构建图上 platform 以 desktop
   抽象接口的实现身份依赖 desktop（Adapter 依赖 Core 接口）；产品调用方向仍是
   runtime -> desktop 接口，desktop 不反向包含 platform。完整 Linux Backend（窗口 /
   Accessibility / 采集 / 输入）与 Windows Backend 仍属 M2 / M4。
4. **过渡期安全边界**：`M1-03` 的 Provider 为首版——文件只读且无路径范围约束、命令
   执行有超时与输出预算但无完整取消路径（M1-05 收紧）、Permission 判定未接入
   （M1-06 落地 `RULE-05`）。在 M1-05 / M1-06 完成前，该绑定只允许出现在开发与测试
   拓扑，不进入产品默认配置。

## 备选方案

- **把文件 / Shell 动作编码为 `InputEvent` kind 走 `execute()`**：被否决。
  `ExecutionReceipt` 只有状态与 safe_message（脱敏契约禁止携带内容），无法回传
  结构化结果；且 input 契约语义是平台输入派发，不是原生能力调用。
- **经 pinned `BuiltinToolRegistry` 以 BuiltIn Tool 暴露**：M1 不采用。注册表不经
  `MiraRuntime` 暴露，且 M1 无模型循环消费 `ModelRequest.tools`；待模型网关接入时
  重新评估（见第 2 条迁移路径）。
- **参考后端直接放 desktop 层**：否决。进程创建是 OS 平台能力，按 `RULE-02` 归
  platform 层；desktop 只定义抽象接口。

## 影响与风险

- `DesktopEnvironment` 公共接口新增两个访问器（设计文档第 5 节授权的里程碑式演进）；
  `MiraHost` 公共接口新增操作面（pinned-free，`OperationTicket` 全部为 Mirage 自有
  类型）。
- 首版 Provider 无范围约束是过渡性放宽，由第 4 条限制约束；若 M1-05 / M1-06 延期，
  不得把该绑定接入任何面向用户的默认路径。
- pinned `admit_operation_completion` 对陈旧票券结算为 NoOp：宿主报告幂等 ok，
  可观察不变量是任务终态不复活（与 DEC-004 的终态幂等语义一致）。

## 变更记录

- 2026-09-16（`M1-05`，[DEC-009](DEC-009-provider-scope-budget-cancellation.md)
  落地）：第 4 条过渡边界中的"范围约束与预算"部分解除——Filesystem 读取强制
  `PathScope` 范围与 `FileReadLimits` 预算（空范围 fail closed），Process 执行
  增加命令长度预算与 `CancelToken` 取消路径；两个 Provider 的纯虚签名追加
  取消参数（基类便捷重载保持源码兼容）。绑定仍仅限开发与测试拓扑：Permission
  判定（`RULE-05`）与用户确认挂点待 `M1-06` 收尾。
- 2026-09-16（`M1-04`，[DEC-007](DEC-007-local-ipc-and-runtime-service.md) 落地）：
  `DesktopEnvironmentBinding` 新增 `bound_environment()` 虚访问器（默认返回 null），
  具体绑定返回其包装的 `DesktopEnvironment`。Runtime Service 的任务驱动循环经它取得
  M1 桌面能力面（第 2 条"宿主侧驱动循环"的服务侧形态），不改变绑定/适配器形态与
  cross-cast 纪律；既有实现不受影响（默认实现向后兼容）。

## 验证方式

- `tests/integration/mira_binding_test.cpp`：绑定身份与能力如实性、observe fail
  closed / 最小观察、execute 拒绝、interrupt 幂等、Provider 正负用例（读文件、缺
  文件、目录、超时预算、非法参数、exit code 与 stdout/stderr 捕获）、宿主操作面
  非法身份拒绝、端到端任务（读文件 + 执行 Shell + 操作边界 + 终态观察）、取消后
  迟到完成不复活任务。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 构建与 `ctest` 全绿；
  `mirage-format-check` 通过；desktop / runtime 公共头 pinned-free 边界零命中。

## 关联文档和工作项

- 设计文档第 5、9、11 节；[DEC-003](DEC-003-repository-layout.md)（分层与边界）。
- 调研依据：[RPA / Agentic Automation 架构调研](../research/2026-09-16-rpa-agentic-automation-architecture-survey.md)
  ——M2 ElementReference 多提示 Target 与解析顺序契约（其第 5.1 节）的设计输入；
  本决策的 M1 Filesystem / Process Provider 边界不受影响。
- 工作项：`M1-03`（本决策）；后续 `M1-05`（范围约束与预算硬化）、`M1-06`
  （Permission 判定）在此边界内收紧。
- pinned 依据：`third_party/mira/docs/api/core-runtime.md`（外部驱动循环与操作接入
  点）、`environment-observation.md`（能力如实声明与 fail closed）。
