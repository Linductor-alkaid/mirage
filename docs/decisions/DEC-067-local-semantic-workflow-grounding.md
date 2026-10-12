# DEC-067：本地语义目标与 Workflow 执行

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-11 首增量
> 依据：DEC-023/024、DEC-054/056；维护者明确要求推进 M7-11

## 决策与承载

首增量在现有 DesktopAtomToolset 的 workflow 注册表追加
`desktop.knowledge.activate`，仅在 Window、scoped Accessibility 和共享
ActionCoordinator 均存在时注册。它是 Mirage 的本地目标解析 Adapter，不是新的
workflow 引擎、平台实现或任务知识库。原生模型会话暂不暴露该原子。

知识以 Mira IR 实参保存：knowledge_schema=1、application、window_title、role、name、
expected_role、expected_name。文本为有效 UTF-8，1..1024 字节，不含 C0/DEL 控制字符；精确匹配，
不做模糊相似度、坐标回退或持久化 @e/@v/scope。资产定义/digest/可运行版本与 Run
继续由 Mira WorkflowRuntime 管理。现有库是进程内投影，用户可将 IR 文件保存在本地并
重新发布；首增量不宣称自动跨重启恢复，不建设平行版本库或 Memory 设施。

每步：取当前前台窗口→核对标题→新语义快照→核对应用/标题→唯一且启用的目标→
确认后条件尚不存在→新引用与 backend scope→既有权限、五秒观察期限、单写者 lease、
前台/几何/标题与 epoch 重验→一次语义激活→复用 DesktopActionVerification 新观察。
整个解析/执行有五秒上限且受 OperationContext 更短期限与取消约束；后条件沿用一秒
新语义验证预算。解析未达成时不产生动作；目标移动/引用换代必须重新解析。

成功只在实际新证据满足固定后条件时返回 verified=true 与 evidence/condition digest。
成功回执为 canonical JSON **字符串**，包含 schema=2、verified、action_id、verification
和 knowledge_digest。Pinned Mira 谓词只比较标量 step_result，不投影嵌套 verified 字段；
因此示例使用 Contains `"verified":true` 检查此宿主固定生成的回执。DryRun 中信号缺席，
Contains 为 NotEvaluable，发布披露为「计划、未验证」；Exists 在缺席时直接为 false，
不能用于本例的 DryRun 发布。真正证据来自复合原子的动作后新观察，而非运行参数承诺。
后条件未满足、取消后未取得证据或副作用不确定返回 ExecutionUncertain，不报告任务
完成。未知应用/页面、无目标、重名、禁用、旧版本、坏快照、焦点/标题/epoch 变化明确
失败。首增量示例不声明重试或循环；可用 Recoverable 交出 WaitingAgent，当前只承载
显式交出状态，不自动重新执行或发起模型恢复。
本地 Adapter 不提供重试。调用者不得给此副作用步骤配置 Retry 或循环；首增量未提供
对任意外部手写 IR/恢复 patch 的统一重放策略，原生会话与自动资产选择后续接入时另冻约束。

复用已交付 Mira 运行/版本/取消/事件、Mirage 新引用动作与核验，Mirador 的
PerceptionSession/VisualIndex 后续承载 OCR/模板候选；其 stable_id 不作为持久执行引用。
AppModel + ScreenStateProvider 的多页导航、语义资产选择、OCR/模板目标和原生会话入口
留在 M7-11 后续增量。首增量不在 ScreenStateProvider 回调做 OCR，不修改依赖。

## 验收

同一知识定义重复运行时刷新引用；目标移动/换代、歧义/禁用/页面变化、权限拒绝、运行中
取消、超时、接管、Provider 异常与动作后假成功均取证。实际 Mira WorkflowRuntime 的
DryRun 不派发；Strict 完成、Recoverable 交出、取消/shutdown 受 Executor 观察。
受控 GTK/AT-SPI2 任务另取平台证据，不以 fake 代替；未完成的宿主/其他平台验收保持开放。

## 第二增量

[DEC-069](DEC-069-local-visual-workflow-grounding.md) 冻结 OCR／模板本地复合原子；
本决定的语义首增量保持有效。原生会话选择、自动恢复、持久资产及多页导航仍为后续。
