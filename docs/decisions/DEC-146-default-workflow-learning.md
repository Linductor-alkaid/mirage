# DEC-146：默认 Workflow 学习、长期元数据与在线验收

> 状态：Accepted（2026-11-14 维护者明确要求默认开启，并授权补齐 Mira/升级 pin）
> 工作项：M15-17；完整监督产品仍按 M15 退出条件验收

## 决策

Runtime Service 默认开启 Workflow 学习；明确 false 可关闭。由服务唯一 Executor 上的
共享 owner 打开产品 state 目录的 workflow-learning/memory.sqlite，Mira 独占 SQLite
文件总长度硬限额 64 MiB，其中 4 MiB 留作维护，不将正文条数上限当总量。后端单次操作
2s、在途 64；达到容量返回明确失败，不删除旧记录、不自动扩容或悄悄退到内存库。
目录只给当前 OS 用户访问，不接受模型路径。打开失败返回服务启动失败；失败信息脱敏。

首增量仅持久化 Mira 规范化 Episode/Lesson 元数据。产品为同一 OS 用户的该专用私有库
使用固定 Agent scope mirage.workflow.local-user.v1，授权同主体跨 Session 召回；不混入
User 偏好域，不跨 OS 用户/其他目录/多租户检索。直接 Host 的可选 binding 仍默认固定
primary Session，只有产品 owner 显式选择固定 Agent scope。现有动作权限独立判定。

先停止生产者/恢复、排空 Workflow/Host，关闭并释放实际 SQLite Store，再关闭 Executor。
所有启动失败路径同样闭合；不依赖 Core 字段析构在 Executor 关闭后补数据库写入。
采用 Mira 的 mutation-id/WAL 重开语义及新容量边界，不自建 Mirage 配额数据库或清理器。
恢复入口无论是否启用学习，均以 Mira wait_run 做至多 1ms 的当前驱动结算检查；
WaitingAgent 视图可能先于 carrier epoch 与检索结算。尚未就绪明确拒绝且不消耗模型尝试。

文件总长度不等于文件系统块占用、目录元数据/峰值或物理磁盘配额；快照明确区分。
Mira M31 的 Artifact/EventStore/价值淘汰/跨层恢复仍未交付，视觉截图长期保存继续经过
单独的授权、来源和工件预算；默认学习不等同于后台录屏。

在线验收使用真实 ModelLayer 的专用 Workflow 模式，复用同一已装配 Gateway/provider/
传输并禁止普通聊天/原生会话并用，不暴露私有访问或创建额外模型栈。该隔离模式只用于
有限 Host 验收；Runtime Service 原生模型共享/热更/自动触发仍需独立接线。最终链路
最多两次真实调用；诊断与最终验收累计最多六次（已保留四次失败），每次输出上限
2048 token。显式选择 Mira DEC-112 的输出模式：Messages 用 Text，仍执行本地同一
JSON/补丁校验，不在一次尝试中降级协议。仅投影合成、非敏感的任务约束白名单，默认
参数值仍不暴露。使用已配置模型/钥匙环，受控文件路径、隔离状态，不将凭据或原始模型输出
写入报告。报告只含模型标识、结果、用量、耗时和预算/存储观测。

## 验收

默认开/显式关、私有路径/坏配置/容量满拒绝、正常完成、写失败、关闭顺序、数据库重开
和旧数据不丢失；新 pin 的存储契约测试以及 Debug/ASAN/UBSAN/TSAN 回归。在线用真实
模型验证失败修复、实际续跑、Lesson 与同签名召回；目标桌面布局变化、接管、真实断电、
Windows 和收益不由本增量替代。实际结果、未通过轮次和补跑条件写入 M15。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、
[最小依赖方案](../dependency_feedback/workflow-memory-default-on-proposal.md)、
[Mira DEC-111](../../third_party/mira/docs/decisions/DEC-111-bounded-sqlite-memory-files.md)。
