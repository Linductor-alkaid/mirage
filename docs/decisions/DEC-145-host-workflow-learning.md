# DEC-145：Host 的会话内 Workflow 学习接线

> 状态：Accepted（受控宿主增量；产品默认及 Agent scope 由 DEC-146 后续扩展）
> 日期：2026-11-14；负责人：Mirage 维护者
> 工作项：M15-16，M15-06 增量

## 决策

1. attach_workflow_surface 增加默认关闭的可选 integration binding，借用外部 owner
   提供的共享 IMemory。构造 WorkflowRuntime 时、任何发布/Run 开始之前安装 pinned
   set_learning_context；不增加运行期替换、自动 Store、Supervisor 或第二份经验库。
   省略 binding 保持原行为；传入空 memory 拒绝且不占用 surface。
2. scope 由 Host 固定为当前 primary Session 的真实 ID，不接受调用方/模型指定 scope，
   不提升为 User/Agent/Application。Mira 默认学习限额约束单次 statement、检索条数/
   token/deadline（8/1024/250ms 为上游暂定值，非产品校准结果）。Memory 后端的 I/O
   期限、容量、授权、保留和删除仍属外部 owner；MIRA-20261112-001 的全局持久配额与
   恢复缺口未全部解除。后续 DEC-146 以授权后的 Mira 元数据容量增量接入产品，视觉
   工件的跨层总量/恢复仍保持独立门禁；直接 Host 的 Session 默认不变。
3. Episode 的终态记录、DryRun 跳过、失败检索、规范化 statement、确定性记忆/变更 ID、
   失败降级与审计全由 Mira 承载。Host 新增 record_workflow_recovery_lesson，仅显式
   委托 Mira 为实际恢复后 Completed 的 Run 记录 Lesson；返回 lesson/run ID 和 digest，
   不复制正文/模型 rationale/实参，错误仅暴露码与固定说明。没有模型工具或自动发布。
   记录失败不等于没有写入；宿主重试只能复用同 Run 的 Mira 幂等语义。
4. 学习操作沿 Mira 的同步 IMemory 门面，存储通过同一外部 Executor 的 store worker
   管理 I/O；不新增线程、队列、定时或平行监控。调用沿 Host 的 owner-thread 纪律，
   不在 UI 或存储 worker 上自等待。binding 只在装配期读取，Runtime 保持 memory 强引用。
5. 关闭顺序为停止生产者、恢复排空、WorkflowRuntime shutdown（含最后的取消 Episode）、
   MiraRuntime stop、外部 owner 关闭/析构 Memory，最后非 worker owner 关闭 Executor。
   学习错误不能复活或改写已结算 Run；可选检索失败不能阻断 WaitingAgent。
6. 会话 scope 内检索只供模型参考，不带动作授权。验证新的同签名 Run 得到此前 Lesson
   后，仍走 DEC-144 的单次恢复与实际权限/epoch 门禁。启用学习时，恢复准入先调用
   Mira wait_run（至多 1ms）确认当前驱动已结算；只有 WaitingAgent 快照但检索仍在途
   时返回 invalid_state，宿主下次驱动再显式尝试，不自动重试/排队。跨会话召回、长期视觉存储、普通
   Agent 自动轨迹、产品共享模型/热更新、在线模型和真实桌面验收继续保持开放。

## 验证

使用真实 Host、DesktopAtomToolset、受控模型和隔离 SqliteMemoryStore：验证终态三态
Episode、DryRun/未绑定无写入、失败恢复后 Lesson/幂等/负向准入、同签名检索进入模型、
会话隔离、写/查询失败及权限不提升，关闭/重开后实际记录可读。所有受控故障通过公开
IMemory 转发到实际后端，不实现第二个存储。结果与失败轮次记入 M15；不把进程内重开
当作产品跨进程恢复、全局配额或断电保证。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、
[专项设计](../design/supervised-rpa-experience-and-visual-memory.md)、
[Mira 学习 API](../../third_party/mira/include/mira/workflow_runtime.hpp)、
[Mira DEC-030](../../third_party/mira/docs/decisions/DEC-030-learning-loop-runtime-semantics.md)。
