# DEC-004：Mira Host 状态集

> 状态：Accepted（M1 冻结，实现见 `runtime/mira_host`，规范见设计文档第 11.1 节）
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1（`M1-02` 完成）
> 替代/被替代：无

## 背景与问题

Mira Host 是 pinned `MiraRuntime` 的唯一 owner，其生命周期状态会被 Runtime Service、
IPC 契约与未来的 UI 消费。状态集必须在 M1 冻结，避免产品层对宿主状态的解释随实现
漂移；同时任务进度的产品可见投影需要固定语义，保证终态幂等（`RULE-04`）。

## 决策

- 宿主状态集冻结为五态：`Stopped`、`Starting`、`Running`、`Stopping`、`Failed`。
  合法转移：`Stopped → Starting → Running → Stopping → Stopped`，
  `Starting/Running/Stopping → Failed`。`Stopped`（正常终态）与 `Failed` 幂等且不可
  复活；宿主不支持原地重启，重新宿主要求新的 `MiraHost` 实例（pinned 运行时拒绝
  二次 `initialize()`，已由 `mira_host_test` 对抗场景验证）。
- 关闭顺序闭合：任何成功初始化的 pinned 运行时，离开宿主时都经过
  `request_shutdown → 等待排空 → finish_shutdown`，无论该离开来自 `shutdown()`、
  失败路径还是析构。
- 环境绑定接口为 `integration/mira` 的 `DesktopEnvironmentBinding`（pinned-free）；
  具体适配器的最终派生类型必须同时实现 pinned 环境契约，宿主在 `start()` 时经运行时
  cross-cast 恢复，未携带契约的绑定以 `invalid_argument` 失败关闭。
- 产品层任务进度是 pinned `TaskState` 的 M1 投影：`Idle`；`Active`（Observing/
  Reasoning/Planning/Acting/Verifying/Recovering）；`Paused`（Pausing/Paused/
  TakeoverSettling/SuspendedForTakeover）；`Cancelling`；终态 `Completed`/`Failed`/
  `Cancelled`；`Unknown`。pinned 转移表是终态幂等的事实源，迟到完成/取消以可观察
  拒绝呈现（同终态重述为 NoOp，跨终态复活为 `pinned_runtime` 错误）。
- 并发归属：单 owner 线程驱动宿主控制面；`status()` 任意线程可观察。pinned 运行时
  内部的 Executor 编队由 pinned 依赖自管，Mirage 仅以 `HostConfig` 容量约束其准入
  与排队，不另建并发设施（`EXEC-01` 的 host 侧对应物）。

## 备选方案

- 增加 `Degraded` 态（早期骨架注释曾提及）：M1 无能力降级来源（无屏幕/无障碍能力
  协商），引入即死状态；M2 引入能力协商时再按反馈流程扩状态集。
- 宿主内自建 `Starting` 异步状态机 + 事件流：M1 控制面为单 owner 同步推进，异步化
  属于过度设计；M1-04 IPC 化后再评估。
- 宿主自持 Executor 并注入 pinned 运行时：pinned `MiraRuntime` 契约不接收外部
  Executor，强行注入需改 pinned 依赖，违反 DEC-001 边界。

## 影响

- `runtime/mira_host/include/mirage/runtime/mira_host.hpp` 是状态的唯一权威定义；
  设计文档第 11.1 节是规范文本。
- IPC 契约（M1-04）与 UI 状态展示直接消费 `host_status_name()` 的稳定小写串。
- 扩展状态集（如 `Degraded`）需要修订本决策与设计文档，并同步 IPC 契约版本。
