# DEC-143：经 Mira Supervisor 管理的视觉记忆写入

> 状态：Accepted（受控接线增量，产品自动采集仍关闭）
> 日期：2026-11-12；负责人：Mirage 维护者
> 工作项：M15-14；扩展 DEC-142，不关闭 M15-04/06

## 背景与边界

M15-13 只准备字节与候选，缺少真实事件回执和写入编排。核对 pinned Mira 的
ContextMemorySupervisor、IMemory、IArtifactStore、SqliteMemoryStore 与上游 DEC-069 /
M31：Supervisor 可管理有限操作、拒绝和关闭，但不提供持久总量准入；文件工件限额也
不覆盖 manifest/墓碑，SQLite 没有全库配额。相关缺口见
[MIRA-20261112-001](../dependency_feedback/mira.md#mira-20261112-001)。不在 Mirage
重建通用容量/回收引擎，本增量不启用产品持久视觉 owner。

## 决策

1. 新增 integration 的 schedule_visual_memory_write，接收外部已有 Supervisor、
   EventStore、ArtifactStore、IMemory 和稳定 runtime/memory/mutation ID。返回唯一
   future，调用方必须消费；不可变图像输入以 shared_ptr 保持有限操作生命周期。
   不创建 Store、Executor、线程或队列，所有引用须活到已接纳 future 结算。
2. 通过 ContextMemorySupervisor 的 Critical 类提交，复用其 max_in_flight 和失败/
   关闭统计。实际串行阶段为准备字节 -> 工件 commit -> 核对实际字节并构建候选 ->
   事实事件 append -> 按回执序号回读核验 -> IMemory::apply。每阶段检查任务取消/
   期限；步骤停止后不继续写后续层，取消不尝试强杀已有 I/O。
3. 使用 observed.provenance 作为宿主预分配的事件 ID。产品事件
   mirage.visual_evidence_recorded.v1 只记录 schema、操作/任务代次、工件 ID/digest；
   通过 EventStore 的真实 receipt 与限一条回读核对 runtime/session/task/event/sequence/
   payload。必须达到 ProcessCrash 或 PowerLoss；不能凭构造的 envelope 或 Buffered
   回执让 Memory 接纳。该事件只证明工件已记录，不证明动作成功，不是通用 BehaviorTrace。
4. 失败保留底层错误码，使用固定 reason；错误域 mirage.visual_memory.write 的
   domain_code 表示失败发生的阶段，已知已提交的工件 ID 放在 diagnostic_artifact。
   不复制异常/存储错误正文。任何跨存储失败都不是原子回滚保证；commit/apply 返回错误
   或异常可能已经写入，owner 须复核，禁止自动重试或删除可能被共享的工件。
5. Memory 成功回执必须匹配 memory ID、正版本和 Add/幂等 Noop。apply 期间发生取消/
   超时，实际成功写入仍返回成功回执，并标记 completed_after_cancel，不能伪装成未写入
   或回滚，也不能因此复活上层 Task。错误/坏回执明确为 MemoryApply 阶段不确定结果。
6. 停止生产后调用 Supervisor.begin_shutdown、消费全部 futures，确认关键排空完成，
   再关闭 SQLite/其他 Store，最后由外部非 worker owner 关闭 Executor。未排空不得
   销毁被借用对象。Supervisor 的在途上限不等于持久容量上限；全局预算/孤儿恢复/
   共享引用删除仍依赖 M15-04/06 与上游 M31，不增加私有监控事实源。

## 验证

真实 FileEventStore + FileArtifactStore + SQLite Memory 经 Supervisor 写入、关闭重开，
核对原始事件/记忆/图像和失败统计；故障注入覆盖准备、commit、事件回执/回读、apply、
异常、未知部分写入、取消/期限、接纳满/关闭与关键排空。公开 API 的微配额循环测量
正文擦除后目录仍增长，作为总量缺口证据，不把逻辑删除写成磁盘回收。
Debug/ASAN/UBSAN/TSAN 及边界/格式/文档检查记入 M15。真实桌面自动采集、长期 owner、
断电恢复和 Agent 监督执行本身均不因这个存储 Supervisor 接线而完成。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、
[DEC-142](DEC-142-visual-action-evidence-candidate.md)、
[调用说明](../examples/visual-memory-candidate.md)。
