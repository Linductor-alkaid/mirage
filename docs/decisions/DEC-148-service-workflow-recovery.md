# DEC-148：产品服务的显式 Workflow 恢复预览

> 状态：Accepted（IPC/CLI 限定增量；native/自动触发另验）
> 日期：2026-11-16
> 负责人：Mirage 维护者
> 冻结里程碑：M15-19；关联 M15-06/07/08
> 替代/被替代：扩展 DEC-144/146；不替代单次恢复限制

## 背景与问题

Host 已能有限恢复并记录 Lesson，产品服务尚无入口。现有 ModelLayer 的 infer_mutex
覆盖完整聊天/Agent turn；直接把 Gateway 交给恢复会绕开互斥及热更新生命周期。
Mira 公开编排器无模型重绑接口，重建会丢失单 Run 尝试预算（MIRA-20261112-003）。

## 决策

- 加法提供 workflow.recovery.start/status/cancel/lesson 四个 IPC v1 操作与 CLI。
  只接受当前服务登记的 Run，启动是 admission，状态结算不代表 Run 完成；Run 状态
  继续来自 workflow.runs。lesson 是单独显式写入，复用 Host 成功/幂等门禁。
- 仅复用当前 ModelLayer 的 Gateway、ObservedProvider 和 profile。首次 attach 在模型
  独占预留内进行；预留通过 infer_mutex 的 try_lock 获取，不排队等待聊天。
  服务先拒绝未结算聊天，再预留；完整 turn 与恢复互斥，预留期间直接调用模型层也拒绝。
- 同时只保留一个未消费的恢复 Run ID；其结算事实、future、取消和尝试预算全部属于
  Mira。start/status/cancel、聊天请求前可用 1ms poll 消费已完成结果后释放预留；
  未 poll 的已完成尝试最多保留一个预留，不启动轮询 worker 或私有调度器。
- 首次绑定后模型配置在本服务生命周期内固定，model.set 返回 invalid_state 并说明
  重启条件。绑定前正常热更新不变；拒绝发生在凭据写入/配置保存/新模型装配之前。
  取消不立即释放预留，必须等模型 future 结算。shutdown 先排空 Host 再关闭模型层。
  后续在保留预算/回执身份的模型重绑契约冻结后解除此预览限制。
- 固定有界恢复配置：Text 输出并由 Mira 同一严格本地 schema 解析，thinking=none，
  单次 30s、2048 输出 tokens、0 决策修复、最多 32 跟踪 Run、每 Run 一次；
  不新增 provider/传输栈，不自动方言降级，不向请求方开放秘密或实参投影。
- 响应只包含 Run/Workflow/请求/补丁 ID、digest、ordinal、outcome/reason；不包含
  模型 rationale、正文、参数值或凭据。lesson 返回 ID/digest，失败明确返回错误，
  不以 Run 成功掩盖学习写入失败。不新增恢复事件 schema，沿用 Mira 的审计与 Run 事件。

## 备选方案

第二模型栈会复制路由/凭据/传输，直接共享 Gateway 会破坏完整 turn 互斥；均不采用。
无限等待模型锁会阻塞串行 IPC 控制面；采用即时拒绝，用户可在聊天结算后重试。

## 影响与风险

本增量固定模型直至服务重启，可能影响模型设置操作；CLI 文档明确说明。
没有 native 按钮、自动触发、真实桌面收益或 Windows 实跑声明。现有 provider 异常
边界和单 Run 一次限制继续引用 MIRA-20261112-002/003，不修改 pinned 依赖。

## 验证方式

实际 RuntimeService/本地 IPC 与脚本 provider 验证失败 atom -> 修复 -> 实际续跑 ->
Lesson，以及模型共享、聊天互斥、未绑定热更新、已绑定配置零副作用拒绝、坏决策/异常、
提交拒绝、取消、超时、shutdown 排空。协议负向向量与 CLI 用法/错误响应；
Debug/ASAN/UBSAN/TSAN 及公共头/格式检查，证据写入 M15。

## 关联文档和工作项

[M15](../plans/m15-supervised-rpa-experience.md)、
[专项设计](../design/supervised-rpa-experience-and-visual-memory.md)、
[DEC-144](DEC-144-host-workflow-recovery.md)、
[DEC-146](DEC-146-default-workflow-learning.md)。
