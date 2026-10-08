# 任务终态通知顺序验收

- 日期：2026-10-08；工作项 M6-36；负责人 Mirage 维护者；依据 DEC-021。
- 来源：授权合并 Mirage 窗口修复时，PR #80 最终提交的 TSan CI 两次在
  event_subscription_test.cpp:978 失败，后续任务期望 Completed、实际 Cancelled/skipped；
  无 ThreadSanitizer 数据竞争报告。失败 run 37795347345，jobs 113373237786、113381352578。
- 与窗口修复无源码关联；依赖 PR #79 已合入，原始窗口功能提交的八项 CI 曾全部成功。

## 原因与最小修复

mark_driver_done 先发布终态 task.updated，再写入 outcome 并通过同一 serial context
通知订阅者。收到终态的客户端立即提交新任务时，旧驱动可能仍等待 outcome 投递，
新驱动也等待 serial context 上的 begin_operation。两普通 worker 同时被等待占据，
串行工作无法推进；有界等待失败被既有拒绝分支投影为 Cancelled，尽管没有用户取消。

将终态 task.updated 移到 outcome 写入、持久化及通知投递之后，使终态通知后旧驱动
不再发起另一轮串行等待。复用现有 Executor、future、EventHub；无新线程、调度器、
通信设施或依赖，无超时放宽或断言删除。此修复关闭该终态通知引起的重叠窗口，
不宣称消除任意数量并行阻塞驱动的所有普通池饥饿问题。

## 回归与证据

保留两 worker、断开一个订阅者、剩余订阅者继续接收、立即提交后续任务且必须
Completed 的原断言，新增终态通知前已经接收相同任务 outcome 的事件流断言。
仅加入新断言、使用未修复实现：433 检查中 1 失败，8.31s，
日志 /tmp/mirage-terminal-baseline-{build,test}.log；该顺序回归可确定复现，无睡眠猜测。

修复后 native-release 完整构建与全量 ctest 52/52，50.55s；事件订阅用例
连续五次全部通过（40.88s）。新实现的事件顺序与原有 Completed 断言均保留。
Debug ASAN+UBSAN（-fsanitize=address,undefined）及 Debug TSan 目标配置/构建，
事件订阅真实 IPC 集成用例各 1/1 通过，未关闭 leak 检测；TSan 按 CI 方法 setarch -R。
日志 /tmp/mirage-terminal-{fixed,sanitizers,tsan}-*.log。

Linux 本地验收完成；独立 PR 交付后同步应用窗口 PR，全部最终 CI 通过后合入。
Windows 完整编译/自动测试通过 CI 门禁执行，物理缩放仍由窗口工作项另行补验。
Journal/通知失败和 events.overflow 继续遵循既有 best-effort / drop 语义，
不承诺通知必达或任意数量并行阻塞驱动的全局进展保证。
