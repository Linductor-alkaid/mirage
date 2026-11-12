# DEC-144：Host 的显式 Workflow Agent 恢复入口

> 状态：Accepted（Host 接线增量，产品自动触发尚未启用）
> 日期：2026-11-12；负责人：Mirage 维护者
> 工作项：M15-15，M15-07 首增量

## 决策

1. MiraHost 复用 pinned WorkflowRecoveryOrchestrator，提供 attach/start/poll/cancel/
   shutdown 的 std-only 结果面。只有当前 WaitingAgent Run 可显式开始恢复；启动成功
   只是 admission。poll 最多等待 1ms，返回最近已结算结果；after_ordinal 可排除已读
   结果，未返回新结果不代表某个尝试仍在运行。模型决策结算
   不是 Run 已完成，最终状态仍读 WorkflowRuntime。
2. integration 的 WorkflowRecoveryModel 持有外部注入的 shared ModelGateway、同一
   profile 的 provider 和 ID。Host 对 binding 作强引用快照，不组装第二模型栈，也不
   访问 ModelLayer 私有 gateway；外部 owner
   须使该 Gateway 使用同一 Executor 并保证 provider/profile/传输与权限预算有效。
   当前 Runtime Service 的 ModelLayer 共享、串行推理与热更新生命周期仍需后续接线；
   不绕开 infer_mutex 接到当前产品，不创建 IPC/native/自动重试入口。
3. 每 Run 只开放一次显式尝试，已结算后拒绝再次启动，不通过新建 Run/重绑规避。
   单次模型请求期限可配 1ms～30s、尝试内决策修复 0～1 次、跟踪记录 1～32 条；同时
   最多 1 次恢复。默认 1 次/5s/0 修复，token/费用沿注入 Gateway 的
   profile 预算，不新增私有配额。一次 Host 生命周期只可 attach 一次，关闭不重置预算。
4. 参数投影、模型 schema、ToolChoice::None、四类决策、补丁校验、权限/副作用/epoch
   检查、续跑和尝试审计全部由 Mira 承载。与 Workflow 共用同一事件桥；Host 仅透出
   outcome/reason/ordinal 与关联 digest/ID，不复制 rationale、实参或图片到诊断。
   这里不装配长期 Memory/Lesson，不因一次恢复即宣称跨环境可复用。
5. 取消恢复只停止模型尝试，Run 保留 WaitingAgent；取消 Run 同时请求取消恢复，
   两条结果均消费，终态不能被迟到模型回答复活。关闭顺序为恢复生产者 shutdown/排空
   -> WorkflowRuntime shutdown -> MiraRuntime stop -> 外部 owner 关闭 Gateway/Executor。
   Gateway 的强引用活过恢复对象；不新增线程、队列或平行状态机。
6. 故障注入确认上游 provider 异常可能穿透恢复 future 并在 shutdown/析构时终止进程
   （MIRA-20261112-002）。在单一 integration Adapter 安装 provider 异常边界，保留
   错误域/域码/错误码/operation/诊断身份及重试属性、固定 safe_message，异常转 Internal；
   诊断 getter 异常降为空。
   外部 owner 在装配期串行注册该边界，Gateway 仅路由绑定 profile，不能并发重配/共享
   原生会话。此临时边界不承载调度或补丁逻辑；上游异常生命周期修复并授权升级后移除。
7. 上游 wait_recovery 优先返回 last_result，多轮/次数耗尽时新回执可能不可读
   （MIRA-20261112-003）；游标本身不能修复该问题。故本增量拒绝同 Run 再次恢复，不
   建第二份 tracking。上游提供可靠尝试身份/新回执后再冻结多轮语义；本轮单次决策
   内的修复预算仍由 Mira 承载。两个缺口均本地登记，未修改 pinned 或发送上游消息。

## 验证

受控 Host、真实 DesktopAtomToolset 与脚本 Gateway 验证失败 Run 等待、补丁修复后实际
atom 续跑与新经验候选，以及 resume/cancel/need_user、坏决策/再次尝试拒绝、重复/满容量、执行中取消、
超时、Run 取消后的迟到回答和关停排空。模型请求无工具授权、参数投影无值，审计与
Host 结果一致。Debug/ASAN/UBSAN/TSAN、格式与公共头检查记录于 M15；不以脚本模型
代替在线质量、真实桌面变化/接管、费用收益或 Windows 验收。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、
[专项设计](../design/supervised-rpa-experience-and-visual-memory.md)、
[Mira recovery API](../../third_party/mira/include/mira/workflow_recovery.hpp)。
