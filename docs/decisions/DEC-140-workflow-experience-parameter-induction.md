# DEC-140：多次成功执行的参数归纳候选

> 状态：Accepted
> 日期：2026-11-11
> 负责人：Mirage 维护者
> 冻结工作项：M15-11（M15-05 的首增量）
> 替代/被替代：无；扩展 DEC-138/139，单 Run 行为与 wire 字节不变

## 背景与问题

现有 experience 导出只固定一次执行的实参。维护者要求继续推进，下一步复用 Mira 已有
`induce_parameters` 与参数化 `compile_workflow`，将多次成功执行的标量差异作为参数
提议，并保留观测值供审阅。参数差异不等于业务可泛化或安全重放证明。

## 决策

1. Host 新增 `induce_workflow_experience(source_run_ids, target_workflow_id, name, summary)`，
   接受按序 2～16 个不同 Run，首个为 anchor。每个 Run 由 pinned capture 校验真实执行
   且 Completed；要求来源 Workflow、创建时 digest 和生效执行策略一致，避免混合版本/
   策略。目标不同于来源；元数据沿 DEC-138。单 Run 方法保持原语义。
2. 步骤同骨架、标量类型、候选命名/路径、参数默认值与 IR 重写全部交给 Mira 公共
   `induce_parameters` 和参数化 `compile_workflow`。不自建 diff、绑定或编译算法。
   同值不产生候选；无差异的合格样本可返回空候选列表。任何来源或归纳失败均零产物。
3. 多 Run 返回 anchor 溯源和有序完整来源列表，并以 JSON 数组给出候选的 name、
   step_index、pointer、provenance、observed_values。候选最多 64 个位置；同名参数
   可有多个位置。观测值按来源顺序，默认值取 anchor；不把其他观测写入 IR 或日志。
   候选审阅数组 canonical JSON 总量上限 64 KiB，超限整个请求拒绝，不静默截断。
4. `workflow.experience.compile` 增加可选 `additional_run_ids`（1～15 个，不能与 anchor
   或其他来源重复）；缺席保持字面量编译。响应 experience 仅在归纳模式同时追加
   sources 和 parameter_candidates；来源 2～16 个，首项与原 anchor 字段一致，每项
   source_run_id/source_workflow_id/source_digest。候选数组满足上项形状、标量观测值
   和每项观测数=来源数、64 KiB 限额；缺一成员或坏形状拒绝。IR 仍限 256 KiB。
5. CLI `workflow experience RUN --also-run RUN ...` 显式选择归纳样本。
   `--review` 输出完整候选响应 JSON（含来源和观测），缺席仍只输出 IR；stderr 仅输出
   ID/digest 和归纳提示，不打印观测值、JSON 路径或正文。保持 stdout/stderr 失败非零。
   两种输出均为用户显式请求的审阅产物；只读、不自动发布/保存/执行或写 Memory。
6. 参数名、秘密、坐标/临时引用与一次性副作用仍需人工或后续产品审阅。算法只给结构
   候选，不推断参数业务含义、移除秘密或提高来源验证等级。发布仍是独立门禁。
7. 工作为有界同步快照/纯计算，沿现有 Host owner 和 Executor 串行请求上下文，无新
   线程、队列、timer 或生命周期。最多 16 个受 IR 限额约束的终态快照；终态来源不被
   迟到结果或 patch 改写。没有跨重启学习、长期视觉存储或普通 Agent 历史提取承诺。
8. Golden v24 -> v25；现有 309 个样本保持原字节。旧服务按 DEC-139 的请求字段闭集
   拒绝新参数，CLI 不回退成单 Run 编译。尚未实测的旧二进制互操作不宣称通过。

## 备选方案

- 在 Mirage 扫描实参做差异归纳：重复 Mira 已交付能力，容易漂移参数和默认值语义。
- 静默抽样/截断观测值：审阅者无法评估候选，拒绝改为明确预算错误。
- 直接发布参数化结果：把少量样本差异提升为可执行事实，跳过审阅及真实桌面验证。

## 验证方式

Host、codec、实际私有服务与 CLI 验证：两个成功样本 -> 提议/观测 -> 参数化 IR ->
默认及显式绑定；原单 Run 字节、无差异、确定性、不入库/不执行；计数、重复/坏 ID、
DryRun/失败、混合版本/策略、审阅预算、响应形状、输出错误和关闭拒绝。
Debug/ASAN/UBSAN/TSAN 适用专项、格式/头边界与文档同步，记录于 M15。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、[设计](../design/supervised-rpa-experience-and-visual-memory.md)、
[DEC-139](DEC-139-workflow-experience-export.md)、[Mira 编译公开契约](../../third_party/mira/include/mira/workflow_compiler.hpp)。
