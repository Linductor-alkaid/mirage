# DEC-053：M7-02 会话桌面工具首增量

> 状态：Accepted
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 冻结里程碑：M7-02 首增量
> 替代/被替代：扩展 DEC-024/034；后由 [DEC-054](DEC-054-scoped-complete-desktop-actions.md) 增补作用域/完整输入

本文保留六工具首增量的历史边界；当前工具清单、版本和释放语义以 DEC-054 为准。

## 背景与问题

维护者要求推进 M7-02。工具注册可复用既有 Provider 原子；M7-01 的完整引用/释放
契约及 M7-03 的点击/按键动作仍未实施，不应据此开放裸坐标或持有输入。

## 决策

1. 首增量按现有 Provider 暴露六个桌面原子：`desktop.window.list`、
   `desktop.window.front`、`desktop.application.list`、
   `desktop.accessibility.semantic_snapshot`，及经权限判定的
   `desktop.window.activate`、`desktop.input.type_text`。缺 Provider 不注册；
   type_text 还要求 WindowProvider。零 Provider 的原有通用会话仍只有 wait。
2. 使用 DesktopAtomToolset 的原有 spec/handler 构造函数，将注册逻辑复用到
   ConversationLoop；不复用工作流运行实例或长期注册表。每轮使用新 registry，
   固定清单最多六个桌面工具加 wait；Mira 管理的同一 provider_call_id 的
   至多一次派发记录限定于该轮，不跨轮复用。
3. 服务通过 pinned-free DesktopToolContext 注入共享 DesktopEnvironment 与
   PermissionController 门禁；模型切换和初始装配使用同一路径。该 context 持有
   环境共享所有权，生命周期覆盖模型任务与 teardown。没有门禁时不注册修改工具；
   Allow/Confirm/Deny 在每次动作时动态判定，不从目录存在推导授权；请求
   task_id / operation_id 来自当前 Mira 工具上下文，resource 为目标窗口 ID。
4. `read_only` 轮次保留四个观察原子，移除窗口激活、文本输入及原有 wait；工作流
   原子的公开目录/schema 保持原形，服务协议零变更。
5. 会话 type_text schema 明确要求 `window_id` 与 `text`，门禁 resource 为窗口 ID，
   不为输入文字。权限等待结束后重新检查取消/deadline 与前台窗口 ID；焦点不符则
   拒绝并要求重新观察。限制为既有 64 KiB UTF-8 与 Provider timeout；Unicode 可
   用性仍以平台实现为准。不自动激活、不自动剪贴板回退、不自动重试失败输入。
6. Runtime 的 Executor StopToken 使用有界 stop callback 将停止通知传到共享
   Provider CancelToken；无新线程/worker/队列。工具进入和权限等待后检查 deadline，
   type_text 的 InputLimits timeout 不超过剩余预算；不得声称 Xlib 阻塞被强制终止。
7. handler 用现有 ConversationCapture 投影 pending/running/complete/failed/cancelled。
   Provider/handler 异常转换成安全失败并交 Mira 规范 ToolResultPart 回填；输入文字
   在过程事件/产品历史仅投影窗口与字节数，规范模型往返保持原载荷。
8. 文本输入返回 `status=dispatched`、`verified=false`，仅证明 Provider 接受输入。
   会话提示要求副作用后重新观察，但本轮不强制业务验证；后观察门禁归 M7-08。

## 备选方案

全量复用 13 个工作流原子目录会默认开放进程执行、应用终止与剪贴板修改，超出本轮
桌面交互范围，因此不采用。提前开放 click/key/ref 动作会绕过未闭合的 M7-01/03
契约，因此本轮不注册。图片工具等待 MIRA-20261009-001，不建立私有模型循环。

## 影响与风险

ModelLayer 构造参数加法扩展，默认空 context 保持现有独立消费者；complete_harness_turn
增加默认 Provider cancel 参数。共享门禁和模型的既有串行调用纪律不变，无新增异步路径。
焦点核对与 OS 输入仍不能形成跨应用原子事务；用户切焦点/接管、输入部分失败与按键
释放策略需要 M7-03 进一步闭合。本轮不宣称完整桌面操作或真实业务完成。

## 验证方式

独立 fixture 覆盖窗口观察→激活→目标文本输入→语义/前台再观察、只读目录、门禁拒绝、
焦点不符、取消、deadline、异常及现有工具过程；RuntimeService IPC 验证真实装配。
增加缺门禁/缺 Provider/跨轮 registry 与输入摘要取证；真实 Runtime 运行中
Provider CancelToken 的 IPC 取消和 shutdown 收敛；既有原子/原生 Agent 回归。
私有 Xvfb 目标窗口核对工具发现/激活、收到的文本与逐键释放事件。
真实平台与 sanitisers 结果记录在 [M7](../plans/m7-visual-tools-and-input.md)。

## 关联文档和工作项

[M7-02](../plans/m7-visual-tools-and-input.md)、[DEC-024](DEC-024-desktop-atom-toolset.md)、
[DEC-034](DEC-034-native-model-agent-loop.md)、[DEC-052](DEC-052-visual-tools-and-input.md)。
