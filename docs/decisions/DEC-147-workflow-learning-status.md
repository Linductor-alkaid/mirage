# DEC-147：Workflow 学习存储只读状态

> 状态：Accepted
> 日期：2026-11-16；工作项：M15-18（M15-06 的可观测增量）

## 决策

提供 workflow.learning.status IPC 与 mirage workflow learning CLI，查询产品默认学习
是否装配，以及 SQLite 文件长度总量、硬限额、维护预留和当前进程已接纳峰值。所有数值
来自既有 WorkflowLearningStore::capacity_snapshot / Mira StoreChannel；不创建计数表、
后台任务或另一套状态。请求通过既有服务串行上下文运行，底层操作仍有界且受 Executor
管理；Store 不可用返回脱敏 unavailable，不能把读取失败报成空库。

未装配学习时返回 enabled=false 与全部零值，不打开数据库。响应为 learning 对象，
只含 enabled、file_bytes、limit_bytes、reserve_bytes、peak_admitted_file_bytes；不包含
路径、主体、Episode/Lesson 正文或凭据。未知旧服务明确失败。CLI 无操作数或修改选项，
仅输出完整 JSON 响应。启用时要求非负整数字节、1MiB～1TiB 限额、预留小于限额、当前
和峰值不超硬限额；关闭时非零预算响应拒绝。峰值为本进程已接纳长度，不能当物理磁盘
高水位或历史全生命周期峰值。

## 验收与边界

验证请求/响应回环、缺字段/错类型/负数/越界/关闭却非零拒绝，真实服务默认启用与显式
关闭，以及 CLI 在私有进程中的只读结果。查询不得派发桌面动作、不改变配置或记忆。
不增加模型调用。清理、跨 store 总量、视觉采集和原生监督仍按 M15 后续门禁推进。
