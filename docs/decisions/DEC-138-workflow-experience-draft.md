# DEC-138：成功 Workflow Run 的只读经验草稿

> 状态：Accepted（首个 Host 接口；完整产品方案仍为 Proposed）
> 日期：2026-11-11
> 负责人：Mirage 维护者
> 冻结里程碑：M15-02
> 替代/被替代：无；扩展 DEC-023，不改变既有发布/执行契约

> 2026-11-11 后续：[DEC-139](DEC-139-workflow-experience-export.md) 将本接口接入
> 只读 IPC/CLI 导出；本决策中的“无 IPC/UI 改动”指首个 Host 增量，接口语义保持有效。

## 背景与问题

维护者要求恢复早期“成功任务沉淀经验、视觉记忆、Agent 监督 RPA”的方向。
Mira 已交付 `WorkflowRuntime::capture_trajectory` 与 `compile_workflow`，Mirage Host
尚无对应入口。普通 Agent 事件到轨迹的自动提取仍是上游 DEC-038 的未实现方向，不能
声称所有历史任务已可生成 Workflow。先提供可验证的 Run -> 草稿增量，保留后续入口。

## 决策

1. 在 `MiraHost` 增加 pinned-free `WorkflowExperienceDraftResult` 和 const
   `compile_workflow_experience(source_run_id, target_workflow_id, name, summary)`。
   要求 Host 现有 owner-thread 调用纪律及活动 workflow surface。
2. 非 nil 来源 Run/目标 Workflow ID、目标不同于源 Workflow，name/summary 预检使用
   pinned `WorkflowLimits::max_string_bytes`；其余定义/编译校验委托 Mira。
3. 来源资格全部委托 `capture_trajectory`；编译委托 `compile_workflow`，不重写参数
   绑定、谓词、跳过步、生效态或 digest 逻辑。只做字面量编译，参数归纳另列工作项。
4. 成功返回 canonical definition JSON、候选内容 digest 与来源 Run/Workflow/digest。
   失败沿 `HostError` 透出，产物字段为空。相同输入内容确定，失败不影响源终态。
5. 不保存、不发布、不执行、不写 Memory 或事件；草稿不因编译而成为库中可运行版本。
   之后由调用方审阅并经既有发布门禁；DryRun 仍不证明目标桌面可复用。
6. 生效实参可能包含敏感数据、临时引用或一次性值。返回值仅供可信审阅，不直接写日志、
   不作为长期资产自动保存。坐标/临时引用提升与视觉证据绑定由 M15-03～05 设计，未通过
   时不开放自动重放。接口本身不承诺目标 ID 在库中未使用，也不修改已有版本。
7. 本接口无新增异步工作；不修改 IPC、UI 或 pinned 依赖。普通 Task ID、聊天历史和
   文本成功宣告不属于输入，能力缺口登记 [MIRA-20261111-001](../dependency_feedback/mira.md#mira-20261111-001)。

## 备选方案

- 自研成功轨迹编译器：与已交付 Mira 能力重复，且易丢失 patch 生效态及验证语义。
- 编译后直接发布并运行：跳过敏感实参/持久目标审阅，一次成功被错误提升为复用承诺。
- 先完成全部长期记忆与 native 页面：无法独立验证资产生产端，扩大本轮改动范围。

## 影响与风险

新增 Host 公开 std 类型和函数，不改变 wire 或持久格式。草稿保留原谓词/策略，包括来源
可能较弱的验证；本轮只是来源与转换正确性，不为原验证背书。服务重启后的 Run 不在
当前内存运行表中时明确拒绝；跨重启检索/候选来源持久化由 M15-03/05 承担。

## 验证方式

在现有 Host/桌面 atom 集成场景中，实际完成一次受控 Run 后编译，核对源 digest、有效
参数默认值、IR digest/确定性，并确认候选未自动入库、未多次派发动作。拒绝 DryRun、
失败 Run、未知/错误 ID、同源目标、超限/空元数据、普通 Task 和 surface 关闭调用。
运行 Debug/ASAN/UBSAN、公开头边界及格式检查；其他门禁如实记于计划。

## 关联文档和工作项

- [设计与早期目标核对](../design/supervised-rpa-experience-and-visual-memory.md)
- [M15-01/02 与后续工作](../plans/m15-supervised-rpa-experience.md)
- Mira [Workflow API](../../third_party/mira/docs/api/workflow-contracts.md)
- Mirage [DEC-023](DEC-023-workflow-contract-face.md)、[DEC-067](DEC-067-local-semantic-workflow-grounding.md)、[DEC-069](DEC-069-local-visual-workflow-grounding.md)、[DEC-119](DEC-119-bounded-memory-and-session-retention.md)
