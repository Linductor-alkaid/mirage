# DEC-151：工作流目录持久化与重启恢复

> 状态：Accepted
> 日期：2026-11-17
> 负责人：Mirage 维护者
> 工作项：M15-22（Completed，Linux 限定增量）
> 扩展：DEC-023、DEC-150；不恢复历史 Run

## 决策

工作流目录默认持久化，复用 Mirage LocalStateStore 的受限读、私有权限和原子替换，
JSON 复用 Mira；Mira WorkflowRuntime 无产品目录/定义正文导出恢复面，产品索引由
Mirage 承载。每个 ID 分别保留当前编辑 head 与最后成功发布的定义、摘要、是否暴露
调用入口和更新时间；保存草稿不撤销最后发布版本。服务草稿仅经过 Mira IR 校验后存入
产品目录，不以 NotValidated 记录覆盖同摘要的执行库发布记录；Host 的直接草稿 API 不变。
默认运行未发布 head 由产品层返回 invalid_state，不再依赖 pinned_runtime 的草稿拒绝。

发布先通过 Mira DryRun 和调用 DAG 检验，准备不可变 handler；原子存盘成功后才激活
调用入口及更新可见目录。写入失败保留原目录与调用目标，不确认成功；若持久提交后
激活异常，工作流编辑/读取/新运行失败关闭，保留文件并要求重启，不把旧缓存当当前状态。
执行入口只接纳已确认提交的发布摘要，服务期保留至多1024个已确认摘要，防止内部
预验证通过但写盘失败的版本被显式执行；重启只恢复最后发布摘要。已有 Run 的取消/状态查询继续可用。所有操作复用服务串行 Executor 路径，无新调度器。

启动在开放 IPC 前校验整个目录、摘要、调用依赖和环路，按被调用目标在先的顺序重新
DryRun/发布，最后恢复编辑 head。只重建定义与调用入口，不运行 Strict 任务、不恢复
Run 或重放桌面动作。任何损坏、超限、悬空已发布依赖或重新验证失败使启动失败，不截断、
不覆盖原文件。删除被其他已发布工作流调用的目标拒绝；删除和导入也必须先持久提交。

最多128个定义，每份 head/published 各256KiB，总文件上限72MiB，任一预算先到即拒绝。
head 保留原始 JSON 文本以维持 workflow.get 字节契约，published 以 JSON 对象存储；摘要
始终由 Mira canonical IR 计算，读取时重验。文件默认位于 session
state 根的 workflows/workflow-catalog.json，可由 workflow_state_directory 独立覆盖；
persist_workflow_state=false 为显式服务期目录模式。学习元数据仍使用独立 Mira 存储。
恢复只保留最新发布定义和编辑 head，不承诺跨重启的历史版本/Run/模型尝试恢复。

## 验证与边界

独立工作流调用最新发布版本的语义保持；测试重启、草稿/发布分离、依赖恢复顺序、更新、
直接/间接循环、删除、导入、容量/损坏/写失败和已开始调用不重放。用虚拟文件 Provider
计数证明启动零动作；Debug/ASAN/UBSAN/TSAN 分开记录。Windows 存储复用已有实现但本轮
不宣称 Windows 实跑。源文件不保存截图、Run 或凭据设施；IR 参数按用户定义私有保存。

实施、准确命令、失败窗口与未执行责任见[目录持久化验收](../compatibility/workflow-catalog-persistence-20261117.md)。
