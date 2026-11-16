# 任务经验、视觉记忆与 Agent 监督的 RPA

> 状态：Proposed（完整产品闭环）；已交付限定增量由 DEC-138～148 冻结
> 日期：2026-11-11；更新：2026-11-16
> 负责人：Mirage 维护者
> 计划：[M15](../plans/m15-supervised-rpa-experience.md)
> 依据：维护者要求重新推进早期 Workflow 目标，从已执行任务沉淀经验并记录视觉记忆

## 目标与使用过程

用户完成一次桌面任务后，系统能回答做了什么、成功依据是什么、哪些步骤可以复用。
经审阅与验证的稳定步骤形成参数化 Workflow；再次执行时由本地动作完成稳定部分，
Agent 负责选择、检查、解释、处理变化和修复。用户沿同一会话入口监督、接管或停止。
本轮在独立 Git worktree 开发；Git worktree 是开发隔离方式，不是 Mirage 新产品模块。

目标闭环：

```text
执行事实 + 任务终态 + 动作前后证据
  -> 有来源的经验候选 -> 审阅/定位知识提炼/参数化
  -> DryRun 结构门禁 -> 当前桌面受控实跑与新证据核验
  -> 可复用 Workflow / Procedure / Skill
  -> 再次匹配 -> 新观察/新引用 -> 执行稳定步骤
  -> 检查点或失败交给 Agent -> 校验 patch -> 续跑/停止
  -> Episode；成功恢复后才生成 RecoveryLesson
```

DryRun 只证明结构与规划可接受，不能代替真实桌面验证。一次成功也不能证明跨页面、
跨版本或任意环境可重复。候选草稿、可运行版本、已实测资产必须在产品中分别呈现。

## 早期目标与当前证据

以下结论来自工作树基线 `1f9fba1`，Mira pin `be44e36044eaf8e5ab09bc0a22f134a3c8545111`、
Mirador pin `2a9f4830f4727498ed4cbad568a1d3ec357ece7e` 的本地公开契约与文档核对。
它们不代表本轮重新执行上游测试或已验收完整产品。

2026-11-14～16：维护者授权补齐 Mira 后，当前 pin 已升级为 2c42627。Mira DEC-111/112
与 Mirage DEC-146 交付有界 SQLite 元数据、显式恢复协议及产品默认学习；下表接线状态
随 M15-17 更新，早期基线与历史记录保留。

| 原目标/能力 | 证据 | 当前 Mirage 接线 |
| --- | --- | --- |
| Agent 控制平面 + Workflow 执行平面，成功经验固化 | Mira [DEC-014](../../third_party/mira/docs/decisions/DEC-014-agent-harness-workflow-dual-plane.md)，2026-09-07 | 架构方向已有；原生 UI 后来优先 harness |
| 成功 Run 捕获、编译、参数归纳、发布门禁 | Mira [M11](../../third_party/mira/docs/plans/m11-trajectory-compilation-and-task-induction.md)、[workflow_compiler.hpp](../../third_party/mira/include/mira/workflow_compiler.hpp) | Host 已发布/运行 Workflow；本轮新增只读编译候选 |
| Episode、成功恢复 Lesson、失败检索 | Mira [M13](../../third_party/mira/docs/plans/m13-memory-and-learning-loop.md)、[workflow_learning.hpp](../../third_party/mira/include/mira/workflow_learning.hpp) | M15-16 提供默认 Session 的可选 Host context；M15-17 产品默认注入私有 Agent scope 元数据 owner |
| WaitingAgent 到模型恢复与续跑 | Mira [恢复 API](../../third_party/mira/docs/api/workflow-contracts.md#workflow-恢复编排workflow_recoveryhppm14dec-031) | M15-15 已接 Host 单次显式恢复；产品共享模型与自动触发待接 |
| 普通 Agent 历史转成功轨迹 | Mira [DEC-038](../../third_party/mira/docs/decisions/DEC-038-unified-behavior-trace.md)，方向冻结、未实现 | 不能把 Task ID/聊天文本作为 Run 轨迹；[反馈](../dependency_feedback/mira.md#mira-20261111-001) |
| 统一 Desktop Environment、产品 Workflow 面 | Mirage [原设计](Mirage：Linux%20-%20Windows%20桌面端设计方案.md) §2/12/14、[DEC-023](../decisions/DEC-023-workflow-contract-face.md) | CLI/IPC 已有发布、运行、取消；native 仍提示未接入 RPA |
| 新语义/OCR/模板目标与动作后验证 | [DEC-067](../decisions/DEC-067-local-semantic-workflow-grounding.md)、[DEC-069](../decisions/DEC-069-local-visual-workflow-grounding.md) | 已有本地复合 atom；模板资源不会自动学习 |
| 短期视觉状态与历史供图 | [DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)、[DEC-132](../decisions/DEC-132-delivered-visual-evidence-ledger.md) | 当前是有界短期状态/供图账，不是持久视觉记忆 |
| 有界长期存储与遗忘 | [DEC-119](../decisions/DEC-119-bounded-memory-and-session-retention.md)、Mira Memory/ArtifactStore | 已有通用设施；产品预算、联合删除与迁移仍需设计验证 |

特别注意：`SessionJournal` 的 user/outcome 投影和 `ConversationLoopResult` 的有界 turn
摘要都不是完整动作档案。已有历史若缺实参、配对结果、当前任务关联或验证证据，应返回
“证据不足，不能生成可复用流程”，保留可解释摘要；不能从成功回答补造成功动作。

## 职责与资产

| 责任 | 承载方 | Mirage 工作 |
| --- | --- | --- |
| 事实、轨迹编译/参数归纳、版本/运行状态、学习/恢复 | Mira | 在 integration/Host 装配公开接口；不新建执行引擎或记忆库 |
| OCR、检测、变化、跟踪、候选证据 | Mirador | 选择证据并绑定桌面任务；不把 stable ID 当跨会话执行引用 |
| 应用/窗口/Provider、权限、接管、动作新鲜度 | Mirage Desktop / Platform Backend | 每次重新定位、一次动作、动作后新观察 |
| 候选审阅、部署预算、产品目录、原生 UI/IPC | Mirage Runtime / apps | 只持久化产品状态；Mira 资产经其接口访问 |

经验至少区分：Episode（发生过的事实）、RecoveryLesson（观察到成功恢复）、Procedure /
Workflow（经门禁的步骤）、视觉证据（辅助定位/解释）。模型总结是带来源的候选，不能
替代执行事实，也不构成输入授权、成功证明或用户偏好批准。

## 首个实现：成功 Workflow Run 到草稿

[DEC-138](../decisions/DEC-138-workflow-experience-draft.md) 定义 Host 的
`compile_workflow_experience(source_run_id, target_workflow_id, name, summary)`：

1. 要求活动 workflow surface，校验非 nil ID 与有界元数据；目标 ID 不得等于来源。
2. 调用 Mira `capture_trajectory`，只接受真实执行且 Completed 的 Run。
3. 调用 Mira 字面量 `compile_workflow`，保留生效实参、观测默认值、谓词与原策略。
4. 返回 canonical IR、候选 digest、来源 Run/Workflow/digest；不写库或记忆、不派发动作。

接口不创建新 Run 或事件，不新增后台任务。相同来源、目标和元数据应生成同一内容。
返回值可能包含敏感实参，只能用于可信审阅，禁止直接写诊断日志。这里只复制来源契约，
不自动删除秘密、消解坐标/临时引用、不提升验证等级；发布之前还需要审阅和重定位门禁。
[DEC-139](../decisions/DEC-139-workflow-experience-export.md) / M15-10 将此能力接到
现有 IPC/CLI，只把候选 IR 返回给显式请求方，独立审阅后沿既有发布门禁使用。
参数化/持久保存的完整产品流程仍由 M15-05 承担；native 会话入口不变。

[DEC-140](../decisions/DEC-140-workflow-experience-parameter-induction.md) / M15-11 已进入
多样本参数化增量：同一版本/策略下 2～16 次成功 Run，复用 Mira 归纳和参数化编译，
anchor 默认值、有序来源和非 null 标量观测供显式 review。完整审阅数据限 64 KiB，
超限整次拒绝；单 Run 仍字面量导出。`--also-run` 和 `--review` 只选择样本及输出，
不自动发布、执行、写记忆或识别秘密/持久目标。持久资产、native 审阅仍未实施。

[DEC-141](../decisions/DEC-141-workflow-experience-archive-import.md) / M15-12 支持用户
自持 review 文件跨服务重启导入。`workflow.experience.import` 接受完整候选响应（384
KiB 上限），Mira 校验 IR workflow_id/规范化 digest，服务预检目标冲突/容量后追加
NotValidated 草稿；重新发布后才可运行。不会恢复旧 Run、版本历史或验证等级；文件
来源/观测是导入声明，不是当前服务的已核验事实，digest 也不是来源签名。公开 Mira
库没有完整重启恢复接口，本增量不自建库或序列化 runtime，不自动扫描、保存到 Memory
或恢复目录；受管资产生命周期和视觉长期存储仍归 M15-05/06。

## 后续设计与接线约束

### 从普通 Agent 任务产生经验

通用 L0 -> L1 事实投影与轨迹提取优先由 Mira DEC-038 承载。Mirage 负责原生任务关联、
桌面前/后证据供应及产品反馈。范围内只提取有配对执行结果的已完成任务；失败/取消、
中间状态、迟到结果、缺失前缀和被截断历史不能生成可发布步骤。完整事实不足时允许
拒绝，不使用 UI 工具行或自然语言回答补齐轨迹。通用能力缺口按反馈处理，不能私建
一套 BehaviorTrace 或在 pinned 依赖中直接补丁。

### 长期视觉记忆

首个适配器增量 [DEC-142](../decisions/DEC-142-visual-action-evidence-candidate.md) /
M15-13 将宿主提供的动作前后图与来源编为单个有界工件，去除 padding；通过同一
ArtifactStore 核对字节后生成 Task-scoped Observed Episode 候选。Memory 索引仅含
摘要，敏感度纳入正文身份，Secret 沿 Mira 策略拒绝。读取重新核对来源/作用域/有效期/
摘要，缺图不能降格成可用证据；历史图无执行引用。存储关闭重开、查询和擦除使用真实
Mira 后端在隔离测试中验证，[调用示例](../examples/visual-memory-candidate.md)说明
外部 owner 的接纳/清理责任。尚未接入产品采集或全局存储生命周期，不代表已启用长期记忆。

[DEC-143](../decisions/DEC-143-supervised-visual-memory-write.md) / M15-14 增加经现有
ContextMemorySupervisor 排定的有限写入：工件核验后提交并回读真实来源事件，再
IMemory::apply；future、阶段错误、已知部分写入和取消后的实际完成均明确。所有
Store/取消 owner 外部注入并按关键排空后关闭。该存储 Supervisor 不是 Agent 监督
执行；产品长期 owner 还依赖
[MIRA-20261112-001](../dependency_feedback/mira.md#mira-20261112-001) 的总量/恢复能力。

记录与任务/动作相关的关键前后帧、ROI、OCR/目标描述、时间、应用/页面身份、来源事件与
验证结果；截图经 Mira ArtifactStore 存储与去重，索引/可回忆事实走 scope-bound Memory。
原图和派生解释分开标识。这里不新增 MemoryKind 或平行图片数据库。

旧图、坐标、`@e`/`@v`、scope、跨帧 stable ID 不能作为持久执行目标。再次执行先匹配
应用/页面，再新观察并唯一定位，取得临时引用后沿权限、ActionCoordinator、epoch 和
新鲜度门禁执行。页面变化、歧义、遮挡或证据到期触发重新观察/交给 Agent。

M15-04 进入实施前须冻结：证据 schema、授权与脱敏、每任务/主体/全局字节和条数、
保留期、软/硬水位及回收预留、引用/删除一致性、索引重建与崩溃恢复。预算值须用目标
设备数据确定；本轮不启用持续录屏或后台长期截图保存。用户删除需覆盖原图、派生物、
索引与共享引用；缺失/擦除证据的记忆明确失效，不能降格成当前事实继续执行。

### Agent 监督执行

使用 Mira `WorkflowRecoveryOrchestrator` 与既有 ModelGateway。稳定步骤本地运行，
显式检查点/失败经 `agent_continuation` 交给 Agent；Agent 提议 patch，经现有校验、权限
及副作用判定后 resume，无法确认则等待用户或停止。普通步骤不必每步请求模型，监督
频率、恢复次数/费用/期限与用户呈现须在 M15-07 冻结。

[DEC-144](../decisions/DEC-144-host-workflow-recovery.md) / M15-15 已接 Host 的显式
attach/start/poll/cancel/shutdown；外部 owner 注入同一 Executor 上的 Gateway/profile/
provider，Host 保留强引用，不访问 ModelLayer 私有对象。受控修复后真实 atom 续跑，
完成 Run 可重新编译候选。当前每 Run 一次、并发一次、请求期限 1ms～30s、尝试内
决策修复 0～1 次、跟踪 1～32 条；token/费用预算沿外部 profile。回执结算与 Run
完成分别观察，审计沿同一事件桥。provider 异常与旧回执缺口已登记
[MIRA-20261112-002](../dependency_feedback/mira.md#mira-20261112-002)/
[003](../dependency_feedback/mira.md#mira-20261112-003)，采用单 Adapter 异常边界与单次
限制；M15-15 首增量未修改 pin。产品模型共享/重载、IPC/native/自动触发、真实接管
在 M15-15 时仍待 M15-06～09，后续长期元数据/受控在线见 M15-17。
M15-19 / [DEC-148](../decisions/DEC-148-service-workflow-recovery.md) 进一步将显式恢复和
Lesson 接到产品 IPC/CLI，复用当前 ModelLayer 的独占预留，不绕开完整 turn 互斥。
future 结算后释放预留；首次绑定后的模型配置固定至服务重启，不重建编排器重置预算。
native、自动触发及真实桌面收益仍开放，见[接线示例](../examples/workflow-agent-recovery.md)。

不确定副作用不能盲目重发；取消/接管阻止新动作，恢复前重新观察，终态不可被迟到结果
复活。补丁只有在恢复成功后才可沉淀为 Lesson；旧错误版本保留来源和失效原因。

[DEC-145](../decisions/DEC-145-host-workflow-learning.md) / M15-16 在 surface 装配期
可选注入共享 IMemory，scope 固定为 primary Session，省略时行为不变。Mira 负责终态
Episode 与失败检索；Host 显式 record_workflow_recovery_lesson 委托 Mira 写已成功恢复
Run 的 Lesson，只返回 ID/digest 与固定错误说明。检索在途时恢复入口先以 Mira wait_run
做至多 1ms 的驱动结算检查，尚未就绪返回 invalid_state，不消耗模型尝试。写入错误
可能跟在真实提交之后，同 Run 的确定性 mutation ID 提供幂等；不伪造回滚或重复资产。
受控 SQLite 验证后续同签名失败将 Lesson 带入模型，权限依然独立检查。M15-16 首增量仅外部
注入存储；下述 M15-17 补产品长期元数据 owner、跨会话召回与受控在线验收。

后续 [DEC-146](../decisions/DEC-146-default-workflow-learning.md) / M15-17 默认装配
Runtime Service 的私有长期元数据 owner，使用 Mira 的文件总量接纳/预留/snapshot，
64 MiB 含数据库/WAL/SHM/journal，4 MiB 维护预留。产品为同一 OS 用户的专用目录
显式选择固定 Agent scope；直接 Host 的 Session 默认仍保留。普通新写入达限拒绝，
错误不伪造回滚；先排空 Workflow，释放实际 Store 后再关 Executor。设置中明确 false
关闭，缺省为 true。视觉工件、自动归档与物理块/跨层总量仍另验。

专用 Workflow ModelLayer 模式只服务受控恢复，不与普通聊天并用；显式选择远端 Text/
JsonObject/StrictJsonSchema，Text 回复仍走同源本地 schema 和补丁校验，无自动降级。
默认生成 2048 token，思考模式可显式配置，期限与尝试预算独立。真实 deepseek-flash
的两轮修复/续跑、Lesson 召回与独立进程重开已取证，见
[在线验收](../compatibility/workflow-learning-20261114.md)。M15-19 后续补产品显式共享模型/IPC/CLI，
配置固定至重启；native、无缝模型重绑/自动触发、真实桌面及通用 Agent 历史提取仍待验收。

### Executor 与关闭

首接口是有界快照加同步纯编译，无新任务/线程/队列。后续有限提取、审阅准备和工件
工作由现有 Runtime Service Executor 通过 `submit_auto` 或可取消路径管理，owner
持有并消费 future；长阻塞 I/O 复用 blocking worker；事件复用既有 Topic。创建任何
新并发路径前须在对应工作项写明容量、句柄、取消、失败与关闭测试。
关闭依次停止提取/监督生产者，取消并排空恢复和工件任务，关闭 WorkflowRuntime，
停止 MiraRuntime，最后非 worker owner 关闭 Executor；不隐藏额外生命周期。

## 验证边界

Host 调用示例见[经验草稿](../examples/workflow-experience-draft.md)。
首接口验证来源/digest、有效参数、确定性、无发布/动作、错误 ID/元数据、DryRun/失败
Run/普通 Task 拒绝和 surface 关闭拒绝。后续测试按 [M15 矩阵](../plans/m15-supervised-rpa-experience.md)
覆盖原生历史、视觉重定位、持久删除、监督修复、取消和长期容量。Fake/契约测试不作为
真实桌面、在线模型、Windows 或经验复用收益证明。


## 默认学习存储的只读诊断

[DEC-147](../decisions/DEC-147-workflow-learning-status.md) / M15-18 为产品 IPC/CLI
提供启用状态与真实 SQLite 文件长度快照，直接委托 Mira 容量接口，不增加缓存、后台
任务或数据库。现有服务串行请求路径调用有界 StoreChannel；未装配时不打开，错误明确
返回 unavailable。查询不包含路径/主体/记忆正文，也不承担物理磁盘、清理或完整 M31 的保证。
