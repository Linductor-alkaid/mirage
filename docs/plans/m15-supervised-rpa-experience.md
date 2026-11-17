# M15：任务经验、视觉记忆与 Agent 监督的 RPA

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M5 Workflow 服务面、M7 本地知识 atom；Mira 已交付编译/学习/恢复公开能力
> 建议发布点：经验候选/显式恢复 CLI 预览；native 入口与发布标签另验
> 更新日期：2026-11-16

## 目标

按维护者本轮要求重新推进早期双路径目标：从已执行任务沉淀可解释的经验，以视觉证据
帮助再次定位，稳定步骤由 Workflow 执行，Agent 负责监督、介入和恢复。先核对历史目标
与真实可用 API，再逐段交付，不把已有上游模块当成 Mirage 产品已接通。

M15 是 Mirage 新分配的具体里程碑；与 Mira 的同号 M15 评估 Harness 无关。
持续个体路线图的 M11～M14 保留原编号；其中 M15+ 原为未分配的长期范围，本项将
经验/RPA 部分独立验收，不要求注意力/身份/多任务提案全部完成后才开始。

## 范围与非目标

- 已完成执行的事实与验证来源 -> 经验候选 -> 审阅/参数化 -> 发布/实跑门禁。
- 任务相关视觉前后证据与有界持久化、再次定位，不复用过期坐标或执行引用。
- Episode/Lesson、已验证 Workflow/Skill 和 Agent 恢复的公开能力装配。
- 现有单入口中的候选审阅、执行状态、等待/接管/取消反馈。

首轮完成能力核对与 Host 只读 Run 草稿接口，后续 M15-10 接入 IPC/CLI 导出；
M15-11 提供同版成功多 Run 的参数归纳及审阅；M15-12 提供用户候选文件跨重启导入草稿。
最初增量不启用历史批量导入、录屏、自动发布、无人监督重放或 native 新页面。
本工作树最初基于 `1f9fba1`（现有 Mira 上游同步开发线），不合并该开发线。M15-17
按维护者明确授权补齐 Mira 并升级 pin，新增受控 Host 在线恢复验收；产品原生自动触发仍后续。

## 设计与决策依据

- [专项设计与证据表](../design/supervised-rpa-experience-and-visual-memory.md)
- [DEC-138](../decisions/DEC-138-workflow-experience-draft.md)：首个 Host 契约已冻结。
- [DEC-023](../decisions/DEC-023-workflow-contract-face.md)、[DEC-067](../decisions/DEC-067-local-semantic-workflow-grounding.md)、[DEC-069](../decisions/DEC-069-local-visual-workflow-grounding.md)、[DEC-119](../decisions/DEC-119-bounded-memory-and-session-retention.md)。
- Mira DEC-014/M11/M13/M14：见专项设计的本地来源链接；Mira DEC-038 自动轨迹仍未交付。
- pinned `kairo-integration` 的 by-api router / tasks-and-lifecycle card：有限任务的
  future 消费与外部 owner 关闭要求；首接口同步执行，无新并发路径。

## 工作项

- [x] `M15-01` 早期目标与能力/接线矩阵（Completed）：定位原始方向、已交付 API、
  Mirage 空缺与上游未实现边界；设计/总计划/反馈互链并通过链接核查。
- [x] `M15-02` 成功 Workflow Run -> 只读经验草稿（Completed）：Host 只透出 std
  结果，委托 Mira 捕获/编译，保留来源、有效参数和确定性 digest；拒绝不合格 Run；
  不入库、不新增动作；Debug/ASAN/UBSAN 与边界/格式检查，状态与证据同步。
- [ ] `M15-03` 普通 Agent 历史 -> 轨迹候选（Proposed）：先推动
  [MIRA-20261111-001](../dependency_feedback/mira.md#mira-20261111-001)，冻结可重建的
  事件投影、原生任务关联、成功/缺失/截断判定和有界提取；同事实前缀同候选，未知
  工具或缺验证明确拒绝。就绪后接 Mira 公开提取接口，不私建通用 Trace。
- [ ] `M15-04` 视觉记忆契约与预算（In Progress，首增量 M15-13）：定义动作前后图/ROI/文字/应用页面/
  事件来源、工件与记忆关联、敏感内容、授权、总/主体/任务配额和物理遗忘；先在
  单一参考任务验证跨会话新观察/唯一目标重定位，再启用任何长期保存。
- [ ] `M15-05` 审阅、参数化和资产保存（In Progress，增量 M15-11/12）：复用 `induce_parameters`、Mira
  版本与 Skill 生命周期，区分常量/参数/秘密/临时目标；草稿不覆盖已验证资产。
  冻结跨重启资产/来源恢复、删除与兼容策略，DryRun 和实际验证分别呈现。
- [ ] `M15-06` 学习存储装配（In Progress，受控写入/Host 学习/默认长期元数据增量 M15-14/16/17）：共享既有 scope-bound Memory/ArtifactStore，
  在 Workflow surface 设置 learning context，验证终态 Episode、成功恢复 Lesson、
  失败检索和写失败可观测；不得另建 MemoryStore、JSON 经验库或图片数据库。
- [ ] `M15-07` Agent 监督与恢复（In Progress，Host/受控在线/显式产品增量 M15-15/17/19/20）：装配 Mira RecoveryOrchestrator，冻结
  检查点/失败触发、次数/期限/成本预算、权限及副作用不确定行为；patch 校验后续跑，
  失败明确等用户/停止，取消/接管与 shutdown 期间零新增自主动作。
- [ ] `M15-08` native 单入口接线（In Progress，图编辑/独立调用与运行/恢复增量 M15-20/21）：呈现候选来源与验证等级、审阅/发布、
  运行状态/等待原因、接管/取消；复用现有 IPC 并按需求加法扩展，不做第二套状态机。
- [ ] `M15-09` 参考任务与收益验收（Proposed）：跑前冻结应用/版本/输入及任务集，
  比较首次探索和复用的成功率、模型调用、耗时/成本、错误副作用与恢复率；包括窗口
  移动、布局/内容变化和错误页面。受控测试、真实 Linux 与 Windows 分别记录。
- [x] `M15-10` 经验候选 IPC/CLI 导出（Completed，Linux 本机增量）：按
  [DEC-139](../decisions/DEC-139-workflow-experience-export.md) 接通只读 Host 接口；
  用户能从当前服务的成功 Run 导出 IR，来源与候选状态明确，错误、超限和旧服务
  明确拒绝；验证未自动入库/执行与私有 CLI 导出/发布门禁闭环。
- [x] `M15-11` 多次成功 Run 的参数归纳（Completed，Linux 本机增量）：按
  [DEC-140](../decisions/DEC-140-workflow-experience-parameter-induction.md) 复用 Mira
  归纳/参数化编译，导出有来源和观测值的候选；CLI 显式多样本与 review；验证
  单 Run 兼容、默认/显式绑定、预算和失败零变更。资产持久化/native 审阅仍另验。
- [x] `M15-12` 自持候选文件跨重启导入（Completed，Linux 本机增量）：按
  [DEC-141](../decisions/DEC-141-workflow-experience-archive-import.md) 导入已有 review
  文件，经 Mira 校验 IR 身份后只创建 NotValidated 草稿；验证独立服务重启、正文
  改写/错身份/超限/冲突/容量拒绝、再发布运行与旧 Run 不复活。自动资产库仍后续。

- [x] `M15-13` 视觉 Episode 候选适配器（Completed，Linux 本机适配器增量）：按
  [DEC-142](../decisions/DEC-142-visual-action-evidence-candidate.md) 编码有来源的动作前后图，
  单工件 + Task-scoped MemoryRecord；实际隔离 Mira 存储验证重启、查询、失效与擦除。
  不启用自动捕获或第二存储，产品总量与生命周期仍由 M15-04/06 冻结。

- [x] `M15-14` Supervisor 管理的视觉记忆写入（Completed，Linux 本机有限操作增量）：按
  [DEC-143](../decisions/DEC-143-supervised-visual-memory-write.md) 接真实事件回执/回读与
  Memory apply，验证在途容量、阶段失败/部分写入、取消及关闭；长期总量 owner 仍依赖
  上游容量/回收契约，不以进程内计数替代持久配额。

- [x] `M15-15` Host 显式 Agent 恢复（Completed，Linux 本机受控 Host 增量）：按
  [DEC-144](../decisions/DEC-144-host-workflow-recovery.md) 接 attach/start/poll/cancel/shutdown，
  每 Run 一次，复用 Mira 的有界编排与审计，验证受控失败/修复/续跑及取消竞态。产品模型共享/重载、
  IPC/native/自动触发和在线模型另验。

- [x] `M15-16` Host 会话内学习接线（Completed，Linux 本机受控增量）：按
  [DEC-145](../decisions/DEC-145-host-workflow-learning.md) 注入既有 IMemory，固定 primary
  Session scope，复用 Mira 的 Episode/失败检索/显式恢复 Lesson；真实隔离存储验证
  幂等、失败降级、关闭/重开与恢复模型上下文。产品长期 owner/跨会话召回仍另验。

- [x] `M15-17` 默认学习、长期 owner 与在线验收（Completed，Linux 元数据/受控在线增量）：
  [DEC-146](../decisions/DEC-146-default-workflow-learning.md) 与
  [最小依赖补齐方案](../dependency_feedback/workflow-memory-default-on-proposal.md)；维护者
  已授权 Mira 增量/pin。默认 true/明确关闭、长期元数据 owner、私有跨会话召回与两轮
  隔离在线验收已实施；最终回归及 CI 结果见下方记录，完整视觉/原生产品闭环仍另验。

- [x] `M15-18` 学习存储只读状态（Completed，Linux IPC/CLI 增量）：按 [DEC-147](../decisions/DEC-147-workflow-learning-status.md)
  接 IPC/CLI，使用 Mira 真实容量快照；验证默认开/显式关、只读与坏响应拒绝。

- [x] `M15-19` 产品服务显式恢复接线（Completed，Linux 受控 IPC/CLI 增量）：按
  [DEC-148](../decisions/DEC-148-service-workflow-recovery.md) 复用当前模型栈，增加
  IPC/CLI start/status/cancel/lesson，冻结推理互斥与模型配置生命周期；验证实际 atom
  恢复/学习、异常/拒绝/取消/超时/shutdown。native/自动触发及真实桌面收益仍另验。

- [x] `M15-20` native 工作流运行与显式恢复（Completed，Linux 受控原生增量）：按
  [DEC-149](../decisions/DEC-149-native-workflow-controls.md) 接现有窗口/RuntimeBridge，
  查看目录与 Run、默认参数启动、取消、显式恢复/结果查询与成功经验保存；验证有界投影、
  错/迟到响应、事件缺口、真实服务接线和明暗/最小窗口。资产编辑/自动恢复另验。

- [x] `M15-21` 工作流图编辑与独立工作流调用（Completed，Linux 限定增量）：按
  [DEC-150](../decisions/DEC-150-workflow-graph-composition.md) 迁移侧栏切换，提供 IR 图与
  条件/原子任务编辑、任务段提取、动态调用与目标跳转；拒绝直接/间接调用环路，验证
  目标更新、并发快照、取消、CAS 与旧服务只读。证据见
  [图与调用验收](../compatibility/workflow-graph-calls-20261116.md)。

## 风险与阻塞

| 风险/依赖 | 负责人 | 解除条件/处置 |
| --- | --- | --- |
| 普通 Agent 自动轨迹提取尚无公开 API | Mira/Mirage 维护者 | 本地反馈 MIRA-20261111-001；上游契约交付并授权升级后接线；M15-02 可独立推进 |
| 旧历史可能只有文本/有界摘要，无完整动作证据 | Mirage 维护者 | 审计真实样本；缺证据返回不支持，只从具备证据的新任务开始，不补造历史 |
| 长期视觉容量、删除与多存储一致性未冻结 | Mira/Mirage 维护者 | [MIRA-20261112-001](../dependency_feedback/mira.md#mira-20261112-001) 与 M15-04/06：公开容量/恢复契约及 DEC-119 预算/故障/擦除测试；未通过前不启用产品长期保存 |
| 恢复 future 异常与多轮旧回执 | Mira/Mirage 维护者 | [MIRA-20261112-002](../dependency_feedback/mira.md#mira-20261112-002)/[003](../dependency_feedback/mira.md#mira-20261112-003)：单 Adapter 异常边界、单 Run 一次；上游结算/尝试身份契约修正并授权升级后移除限制；无缝模型重绑/native 仍另验，显式服务面见 M15-19 |
| 一次成功或 DryRun 被误标为可重复技能 | Mirage 维护者 | 审阅等级、实跑门禁、新引用与后条件；错误/不确定副作用禁止盲目重放 |
| 基于尚未合并的 Mira 同步开发线 | Mirage 维护者 | 合并/重放时确认同步线进入目标分支并补回归，不能丢失所核对的 pin 基线 |

尚未冻结的后续设计保持 Proposed，不用已完成增量的状态关闭产品里程碑。

## 测试与退出条件

| 范围 | 验证方式 | 当前边界 |
| --- | --- | --- |
| Run 草稿 | 实际 Host + 受控 desktop atom 执行，核对源/候选 digest、有效参数、确定性、未入库与零额外动作 | M15-02；不证明桌面资产可复用 |
| 负向/生命周期 | DryRun、失败、未知/错误 ID、普通 Task、同源目标、元数据超限、surface 关闭拒绝；WaitingAgent/取消 Run 的受控测试 | 不从不合格状态推断成功；不据此宣称执行中取消全覆盖 |
| 原生历史 | 配对工具结果、缺失/截断/迟到、混合任务、失败/取消、同输入确定性 | M15-03；API 与样本就绪后执行 |
| 视觉/持久化 | 新 scope/移动/重名/错页/旧图，重启、磁盘满、共享引用删除、索引重建、总容量收敛 | M15-04～06；真实目标环境必验 |
| 监督 | 正常、异常、接纳拒绝、执行中取消、超时、接管、shutdown，恢复预算与不确定副作用 | M15-07；新并发路径六类门禁 + TSAN/故障注入 |
| 产品收益 | 已冻结任务集多次探索/复用对照，真实 Linux、Windows、在线模型分开 | M15-08/09；无数据不写改善指标 |

- [ ] M15-01～20 均完成，所需后续决策已冻结。
- [ ] 至少一个真实桌面任务达到执行 -> 候选 -> 审阅/发布 -> 重启后再用 -> 变化交给
  Agent -> 经验证恢复 -> Lesson 的闭环。
- [ ] 视觉长期预算/遗忘与隐私删除实际通过；缺失证据不继续执行。
- [ ] 取消/接管/故障/shutdown 与目标平台及收益矩阵通过并保留准确证据。

## 验证记录

### 2026-11-11：M15-01/02 首增量

- 基线：`1f9fba1`；独立 worktree `supervised-rpa-experience`，分支
  `feat/supervised-rpa-experience`。Mira `be44e36` / kairo `10e870b`（v0.7.4），
  Mirador `2a9f483`；只初始化锁定子模块，没有修改指针或第三方内容。
- 核对：Mira DEC-014、DEC-038、M11 计划、Workflow API、compiler/learning/runtime/
  artifact 公共头与 m11_compile_test；Mirage 原设计、DEC-023/067/069/109/119/132、
  Host 装配、native Workflow 提示与 SessionJournal 投影。
- 实现：Host 只读经验草稿入口及对应现有 atom 场景的契约测试；完整方案另列后续项。
- 初次配置：`cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` 因未初始化 eui-neo 拒绝；按锁定指针初始化
  后重跑，不关闭依赖校验。后续实测结果完成后追加。
- 初次构建因新增测试误将 `WorkflowParameterBindings` 当 JSON 编译失败，改为读取
  `.values` 后构建通过。初次 Debug 矩阵 5/6 通过，新权限回调取消夹具未获得预期
  cancelled 终态（2 个断言失败）；改用 WaitingAgent 稳定边界验证取消后的捕获拒绝，
  不将该用例作为执行中取消保证。最终专项命令与结果在完成验证后追加。
- 未执行产品范围：普通历史提取、视觉记忆持久化、native 入口、在线监督恢复、真实桌面
  复用收益、Windows；负责人 Mirage 维护者，按 M15-03～09 依赖/契约就绪后补跑。

最终验证（同日，`1f9fba1` 上的本工作树改动）：

- 环境：Ubuntu 24.04.5 LTS / x86_64，Linux `7.0.0-42-generic`，GCC 13.3.0，
  CMake 3.28.3 / Ninja，clang-format 18.1.8。默认非 native/OCR 配置，依赖校验开启。
- Debug 配置最终用
  `cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=`；
  本机无 ccache，清空初次指定的 launcher。构建命令：
  `cmake --build --preset debug --target mira_host_test local_semantic_workflow_test local_visual_workflow_test workflow_event_bridge_test -j 6`。
  `ctest --preset debug -R '^(mira_host_test|local_semantic_workflow_test|local_visual_workflow_test|workflow_event_bridge_test)$' --output-on-failure`
  **6/6 通过，0 skip**（4 个业务 binary + setup/cleanup）；Host 195、事件桥 44、
  视觉 Workflow 177、语义 Workflow 125 个检查，0 失败。
- 分别执行 `cmake --preset asan -DMIRAGE_FETCH_DEPENDENCIES=OFF`、
  `cmake --preset ubsan -DMIRAGE_FETCH_DEPENDENCIES=OFF`、
  `cmake --preset tsan -DMIRAGE_FETCH_DEPENDENCIES=OFF`，并以
  `cmake --build --preset <preset> --target mira_host_test -j 4` 构建。
  `ctest --preset asan -R '^mira_host_test$' --output-on-failure` 与
  `ctest --preset ubsan -R '^mira_host_test$' --output-on-failure` 各 **3/3 通过，0 skip**；
  `setarch x86_64 -R ctest --preset tsan -R '^mira_host_test$' --output-on-failure`
  **3/3 通过，0 skip**。各含 Host 的 195 检查和两个隔离 fixture；无 sanitizer 报告。
- `cmake --build --preset debug --target mirage-boundary-check mirage-format-check`
  通过：67 个头，0 新违规；DEC-096 的两条既有 seam 如实保留。
  变更的 9 份 Markdown 去除代码围栏后核对 605 个本地链接，0 缺失路径；
  `git diff --check` 通过。
- 契约证据：实际 atom Run 生效值/来源/digest 一致，重复编译相同；权限调用计数及
  Run 事件计数不增加，目标未自动入库；DryRun/Failed/WaitingAgent/Cancelled、普通
  Task、未知/错误 ID、同源目标、超限/空元数据、surface 关闭均拒绝。
- M15-01/02 完成，仅限本机 Host 契约增量。M15 保持 In Progress，M15-03～09 和
  退出条件保持未完成；没有启动/重启用户产品、导入真实历史、保存截图或发送上游消息。

### 2026-11-11：M15-10 只读候选导出

- 范围：在原工作树与相同 pin 上，新增 DEC-139、协议 v1 的
  `workflow.experience.compile`、Runtime Service 串行处理和 `workflow experience`
  CLI；Host 捕获/编译接口复用不变。返回专用 candidate 信封和来源，stdout 仅为
  用户显式导出的 IR，诊断只有 ID/digest，输出失败非零；不新建任务、版本、候选库、
  Memory 或事件，不自动执行。参数化、长期保存与 native 仍未实现。
- 测试增量：共享 golden v24 的两个请求、一种响应及九个负向样本；固定元数据/IR
  预算边界、候选不能降格为 workflow.get；RuntimeService 实际 fake atom 成功后
  导出，满产品目录仍只读、来源/digest/确定性、目录和 Run 不增加、未知/DryRun/同源
  目标拒绝。私有无显示真实服务/CLI 完成文件读取 -> 导出 -> 显式发布门禁，验证失败
  Run、未知来源、用法拒绝、stdout/stderr 写入失败和无诊断正文泄漏。
- 首次 Debug 矩阵 **4/5 通过**：golden 的数组 definition 拒绝样本失败。发现
  `object_member_text` 仅序列化、调用方必须校验对象形状；本分支补上 `is_object()`，
  不修改既有响应解码。修复后的矩阵和新 CLI 门禁见下方，保留此次失败记录。
- 环境：沿首增量 Ubuntu 24.04.5 / x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8，debug/asan/ubsan/tsan 预设，native/OCR 关闭，pin 校验开启。
- 构建：分别执行
  `cmake --build --preset <preset> --target mirage mirage-service ipc_protocol_test ipc_protocol_golden_test runtime_service_test -j 4`
  （Debug 构建使用 `-j 5`）。stdout/stderr 错误收口后四配置追加
  `cmake --build --preset <preset> --target mirage -j 4`，确保最终 CLI 已重建。
- Debug：
  `ctest --preset debug -R '^(ipc_protocol_golden_test|ipc_protocol_test|runtime_service_test|workflow_experience_cli_test)$' --output-on-failure`
  **6/6 通过，0 skip**（四个业务门禁 + 两个状态隔离 fixture）；最终 CLI 输出错误
  收口后 `ctest --preset debug -R '^workflow_experience_cli_test$' --output-on-failure`
  **1/1 通过**。
- ASAN/UBSAN：对相同正则分别执行 `ctest --preset asan` 和 `ctest --preset ubsan`
  （均带 `--output-on-failure`），各 **6/6 通过，0 skip**；TSAN 使用
  `setarch x86_64 -R ctest --preset tsan` 携带相同正则与输出参数，
  **6/6 通过，0 skip**，无 sanitizer 报告。各配置 codec/golden/service 分别
  516/1703/710 检查、0 失败；Python 门禁使用独立目录与受控子进程，均成功回收。
- 兼容与质量：297 个既有 golden 样本逐个与 HEAD 基线比较完全一致；新增负向
  向量作为协议回归输入语料。格式/公共头检查通过，67 头、0 新违规，DEC-096 两个
  既有 seam 保留；12 份变更 Markdown 的 696 个本地链接及标题/围栏检查通过；
  `git diff --check` 通过。最终检查若有新增计数仅反映文档同步，不改变功能验收。
- 同步：总计划、主设计/专项设计、DEC-138 后续说明、协议、CLI 示例及 README。
  M15-10 完成；整体 M15 仍 In Progress。Windows 运行/native UI、真实视觉任务复用、
  旧服务进程互操作尚未实测，负责人 Mirage 维护者；在对应构建/设备与服务版本就绪后
  补验 M15-08/09。旧服务器未知 op 的现有错误契约已保持，不据此宣称旧二进制实测。

### 2026-11-11：M15-11 多次成功执行的参数归纳

- 范围：在原 `1f9fba1` 工作树及相同 pin 上，按 DEC-140 扩展 Host/IPC/CLI，2～16 个
  同版本/策略的不同成功 Run -> Mira induce_parameters -> 参数化 compile_workflow。
  anchor 默认值、全部来源与非 null 标量观测值可 review；64 KiB 审阅预算超限整次
  拒绝。新 CLI --also-run/--review；单 Run 默认字面量输出保持，不自动发布、执行、
  保存或写 Memory。M15-05 只完成参数化首增量，资产持久化/native 审阅未交付。
- 开发期失败：首次新增 Host 测试触发 `-Werror=shadow`，已更名修复；该构建失败后
  曾误跑旧测试二进制，golden 对新增向量有两处不匹配，该轮不作为完成证据。之后
  所有验证先检查构建退出码，再运行测试；以下结果来自最终重建的可执行文件。
- 测试：Host 直接校验空/单个/重复/超量来源、9 个变化参数造成的真实审阅预算拒绝、
  失败零产物/零额外权限调用、关闭拒绝。Codec 包含 64 KiB 精确边界/越界和不完整
  producer 信封拒绝；golden 新增多样本正向及重复/坏来源、混合元数据、无效候选/
  null 或非标量观测、缺成对成员等输入。v24 的 309 个样本逐条比较保持完全一致。
- 私有实际服务/CLI：两个只读文件的真实成功 Run -> 完整来源/观测 review -> 参数化
  IR -> 独立 publish -> 默认/显式 file_path 运行完成，错误类型绑定拒绝。无差异返回
  空候选、重复请求确定、导出不改变目录/Run；重复/DryRun/失败/未知、混合版本和
  生效策略拒绝。无显示、在线模型、用户设置或用户桌面参与。
- 环境沿首增量 Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8；默认非 native/OCR，依赖校验开启，无第三方或 pin 修改。
- 最终对 debug/asan/ubsan/tsan 各执行
  `cmake --build --preset <preset> --target mirage mirage-service mira_host_test ipc_protocol_test ipc_protocol_golden_test runtime_service_test -j 4`，
  构建退出码均为 0。对前三个配置执行
  `ctest --preset <preset> -R '^(mira_host_test|ipc_protocol_test|ipc_protocol_golden_test|runtime_service_test|workflow_experience_cli_test)$' --output-on-failure`；
  TSAN 在该命令前加 `setarch x86_64 -R`。四配置均 **7/7 通过、0 skip**
  （5 个业务门禁 + 2 个隔离 fixture），无 sanitizer 报告。各配置 Host/codec/golden/
  service 为 **208/520/1775/710** 个检查、0 失败；独立 CLI 子进程均正常回收。
- `cmake --build --preset debug --target mirage-boundary-check mirage-format-check` 通过，
  67 头、0 新违规，两个 DEC-096 seam 保留；13 份变更 Markdown 的 705 个本地链接、
  标题与围栏检查通过，`git diff --check` 通过。同步 DEC-140、原决策后续说明、
  协议/专项设计、总计划、示例和 README。
- M15-11 完成，M15-05 与整体 M15 保持 In Progress；Windows/真实视觉任务上的
  泛化、持久资产、native 审阅和普通 Agent 自动历史提取未验收。负责人 Mirage
  维护者，按 M15-03～09 的上游接口、设计及目标设备条件补跑，不以本机文件任务代替。

### 2026-11-11：M15-12 候选文件跨重启导入

- 范围：同一工作树与 pin，按 DEC-141 提供 `workflow.experience.import` 和 CLI
  `workflow import --file`。完整 review 响应/用户文件限 384 KiB，复用既有 reader；
  Host 用 Mira 解析 IR 与规范化 digest 核对内容身份，服务预检目录容量与目标冲突后
  复用原 save 路径追加 NotValidated 草稿。来源声明留在文件中，不复活旧 Run、原验证
  等级或版本历史，不新增并发、受管文件库或 MemoryStore。CLI 文件 helper 有界读取并
  拒绝读取长度不一致，已有定义/参数文件默认受 IPC 帧预算约束。
- 开发期 Debug 首轮 **6/7 通过**：两个新 golden 请求遗漏向量顶层 correlation id，
  导致生成 id=0，出现 6 个检查失败；已补齐样本元数据，未改 codec，单项含 fixture
  **3/3 通过**。以下最终矩阵来自修复后重新构建的二进制和最终测试文件。
- Host：坏正文、错 ID/digest 拒绝且返回空身份，失败后目标仍保持原不存在的拒绝；
  正常导入只创建不可运行草稿，权限计数/Run 事件不增加；surface 关闭后拒绝。
  Codec：archive 精确 384 KiB 与越界；golden v26 新增 2 个导入请求与 11 个拒绝
  样本（缺失、错类型、未知字段、其他/失败响应、错版本/状态/摘要、半份审阅）。逐项
  对照增量前快照，v25 的 **332 个既有样本保持完全一致**。
- 实际私有服务/CLI：第一进程导出两个成功文件读取 Run 的参数化 review 并正常退出；
  第二独立进程初始目录/Run 为空。正文改写、错身份/摘要、坏 JSON/IR、空/超限文件、
  错用法拒绝；有效导入后 head 为 not_validated、不可运行，重复导入不改变目录。
  显式发布后默认/显式参数均执行完成，重新导入不会降级已发布 head，旧来源 Run
  仍不可捕获。服务契约测试另覆盖满目录拒绝且无产品目录/Run 变化。
- 环境沿前增量 Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8；非 native/OCR，无显示、在线模型、用户配置或桌面参与。
  四配置均执行
  `cmake --build --preset <preset> --target mirage mirage-service mira_host_test ipc_protocol_test ipc_protocol_golden_test runtime_service_test -j 4`，
  构建退出码 0；随后执行
  `ctest --preset <preset> -R '^(mira_host_test|ipc_protocol_test|ipc_protocol_golden_test|runtime_service_test|workflow_experience_cli_test)$' --output-on-failure`。
  TSAN 前缀 `setarch x86_64 -R`。Debug/ASAN/UBSAN/TSAN 均 **7/7 通过，0 skip**，
  无 sanitizer 报告；各配置 Host/codec/golden/service 为 **224/523/1805/712**
  检查、0 失败，两次服务生命周期及 CLI 子进程正常回收。
- `mirage-boundary-check` / `mirage-format-check` 通过，67 个头、0 新违规，两个
  DEC-096 seam 保留；14 份 Markdown 的 718 个本地链接、标题/围栏检查通过，
  `git diff --check` 通过。DEC-141、专项/主设计、协议、总计划、示例和 README 已同步；
  无第三方内容或依赖 pin 变更，未提交/推送。
- M15-12 完成；M15-05、整体 M15 与其他退出条件保持 In Progress/未完成。自动受管
  资产恢复、断电/磁盘故障、长期视觉保存/删除、native 审阅、普通 Agent 历史提取、在线
  监督恢复及 Windows 未验收；负责人 Mirage 维护者，待相应 M15-03～09 设计、公开
  接口和目标设备就绪后补跑。本机文件任务不能代替真实桌面经验复用验收。

### 2026-11-12：M15-13 动作前后视觉证据候选

- 基线：本分支 `e00debf`；沿既有 Mira/Mirador/kairo pin，没有依赖指针或第三方源码
  变更。冻结 DEC-142，M15-04 进入 In Progress。核对 Mira artifact_store、MemoryRecord/
  IMemory、SqliteMemoryStore 公共头、durable-memory-host 指南、M4 secret/作用域测试；
  SQLite 生命周期沿 pinned kairo-integration 的 by-api / blocking-io 卡片。
- 实现：新增 integration 的 prepare_visual_action_evidence / make_visual_memory_record /
  review_visual_memory。宿主提供的前后 BGRA8、应用/页面描述和来源编为单工件，丢弃
  padding；manifest/每帧/整体均核对摘要，Task Episode 的 statement 只有摘要。
  敏感度与内容绑定，Secret 遵循 Mira 禁止进入 Memory 的规则，拒绝而不降级。取消、
  超时、过期、错 scope/来源/类型/等级、缺失与篡改拒绝；底层读失败保留错误码但不
  复制错误正文。历史图不带 source_sequence、执行引用、授权或坐标。
- 操作预算由 caller 显式提供，并约束于 8 MiB/帧、16 MiB/工件、8 KiB manifest、
  120 秒观察窗口的上界；不设置产品总量或默认保留期。适配器不创建 Store、不 apply、
  不捕获/输入/写磁盘或自建任务。外部 owner 仍负责授权、来源真实性、总量接纳、
  孤儿/共享引用处理和完整关闭，不把生成候选标为已经入库。
- 开发期：三次构建分别暴露测试临时目录 helper 带入 IPC 头、JsonValue::as_object
  只读、遗漏 Mira::state_store 链接；已改用独立可移植临时目录、重建 JSON 对象和
  显式测试链接。首次运行有 2 个失败，原因是测试误以为重复内容会改变原工件标签；
  独立 Store 后验证拒绝。随后 Secret 正例与 Mira MemoryRecord 禁止 Secret 的契约
  冲突，包含加诊断定位的一次重复失败；已把准备/绑定/读取全部收敛为拒绝 Secret。
  最终以下结果来自最后构建，不使用失败构建后的旧二进制作为完成证据。
- 新测试 229 检查：确定性、padding 剔除、完整预算边界与溢出、源/时间/上下文/取消/
  期限、闭集/非整数/版本/偏移/正文/敏感度/来源篡改、错误码和诊断隔离；真实
  FileArtifactStore/SqliteMemoryStore 的写入、幂等重放、总工件容量拒绝、Task scope
  get/query 与隔离、关闭重开后图像恢复、显式擦除后再次重开记录和工件仍不可读。
  存储在独占私有临时目录；SQLite worker 由唯一测试 Executor 管理，close 后再
  shutdown，无私有线程/队列。这里是进程内重建存储对象，不是产品服务跨进程恢复。
- 环境沿前轮 Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8，非 native/OCR，无用户桌面/配置和在线模型。
  四配置各执行
  `cmake --build --preset <preset> --target visual_memory_test local_visual_workflow_test mira_host_test mirage mirage-service -j 4`，
  构建均退出 0；Debug 另含 mirage-format-check / mirage-boundary-check。
  随后执行
  `ctest --preset <preset> -R '^(visual_memory_test|local_visual_workflow_test|mira_host_test|workflow_experience_cli_test)$' --output-on-failure`，
  TSAN 前缀 `setarch x86_64 -R`。Debug/ASAN/UBSAN/TSAN 均 **6/6 通过、0 skip**
  （4 个业务测试 + 2 个隔离 fixture），无 sanitizer 报告。各配置视觉记忆/本地视觉
  Workflow/Host 为 **229/177/224** 检查、0 失败；实际 CLI 的经验文件闭环仍通过。
- 格式与公共头检查通过：67 头、0 新违规、DEC-096 两个既有 seam；7 份 Markdown
  的 540 个本地路径及标题/围栏检查通过，git diff --check 通过。同步主/专项设计、
  DEC-142、总计划、专项计划、调用示例和 README；本增量未提交/推送。
- M15-13 完成，M15-04 与总体 M15 保持 In Progress；M15-06 仍待产品共享 owner/
  全局预算方案。未接 Runtime Service/CLI/native 的自动视觉记忆，未从原生 Agent
  历史生成图像，未验收真实桌面重定位、断电/磁盘故障、共享工件隐私删除、全局物理
  占用/峰值、Agent 在线恢复或 Windows。负责人 Mirage 维护者，按 M15-03～09 的
  上游接口、owner/预算设计和目标设备条件补跑；本机受控图不代表产品闭环完成。

### 2026-11-12：M15-14 Supervisor 写入与持久容量缺口

- 基线仍为 e00debf，加同工作树未提交的 M15-13；无第三方/依赖 pin 变更。
  DEC-143 冻结有限写入协议，M15-06 进入 In Progress。核对 pinned Mira 的
  ContextMemorySupervisor、EventStore、Memory/ArtifactStore 公共头及 M4 监督/存储
  测试；kairo-integration 的 by-api / tasks-and-lifecycle 约束 future 消费和外部 owner
  排空。Mira DEC-069/M31 和记忆维护计划明确尚无长期总量/恢复 facade。
- 接线：schedule_visual_memory_write 借用唯一外部 Supervisor 与共享 Store，Critical
  有限操作持有不可变输入，顺序执行准备、工件提交/核验、来源事件提交/按序号限一条
  回读、Memory apply。ProcessCrash/PowerLoss 回执、真实 runtime/session/task/event/
  sequence/payload 必须匹配，才可写 Memory；事件只有来源与工件身份，不含像素/页面。
  不自建线程、队列、Store、任务监控或持久配额。
- 失败：保留错误码、业务阶段及已知工件 ID，异常正文不进入诊断；不自动重试或 erase。
  commit/apply 错误可能已经写入，回执异常也不能伪装回滚。apply 期间取消但已成功时，
  返回真实回执和 completed_after_cancel，不改变上层 Task 状态。关闭先停止生产、
  Supervisor 关键排空/消费 future、关闭/析构 Store，最后外部非 worker owner 关闭
  Executor；测试 owner 在末轮复核后改为由 RAII 保证 Executor 最后析构。
- 开发期首轮 Debug 新测试 **306 检查、1 失败**：关闭后的测试误期望 InvalidState；
  pinned Supervisor 的文档/测试约定实际是 Unavailable，修正测试后 **3/3**（含 fixture）
  通过，没有改依赖或改变原错误码。末轮 owner 析构调整后重建并重跑完整专项矩阵。
- 新测试最终 306 检查：16 类阶段故障，包括真实提交后报错、异常、Buffered/坏序号、
  无/错来源回读、Memory apply 拒绝和提交后坏回执；检查后续层零新增写入、部分结果
  标识、无自动删除、Supervisor failed 统计与诊断隔离。另验空输入、排队/提交后取消、
  排队/受控 I/O 超时、apply 后取消的实际成功、满容量拒绝、Critical 排空与关闭拒绝。
  真实 FileEventStore/FileArtifactStore/SQLite 关闭重开后来源与图像仍匹配。所有 Store
  与取消 probe 活过 future；对象重建是进程内测试，不宣称产品服务恢复。
- 容量证据：公开 FileArtifactStore(root, 1) 接口做 16 次不同单字节提交/erase，旧工件
  open 均拒绝。目录普通文件长度合计在 8 轮后为 **1760 字节**、16 轮后 **3520 字节**，
  四配置一致。它证明正文限额不能当作整目录总量；不是文件系统块分配、可回用空间、
  SQLite/WAL 规模或全局物理占用/峰值基准。登记 MIRA-20261112-001（Open，未发送），
  延伸既有 M31/容量反馈，不用 Mirage 私有计数/数据库绕过。
- 环境沿前增量 Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8，非 native/OCR，无用户桌面/配置或在线模型。四配置各先构建
  `cmake --build --preset <preset> --target visual_memory_write_test visual_memory_test local_visual_workflow_test mira_host_test mirage mirage-service -j 4`，
  owner 收口后各追加构建 visual_memory_write_test，退出码均 0。最终执行
  `ctest --preset <preset> -R '^(visual_memory_write_test|visual_memory_test|local_visual_workflow_test|mira_host_test|workflow_experience_cli_test)$' --output-on-failure`，
  TSAN 前缀 setarch x86_64 -R。Debug/ASAN/UBSAN/TSAN 均 **7/7 通过，0 skip**
  （5 个业务测试 + 2 个隔离 fixture），无 sanitizer 报告；写入/视觉记忆/本地视觉
  Workflow/Host 分别 **306/229/177/224** 检查、0 失败，真实 CLI 经验文件闭环仍通过。
- 格式/公共头检查通过，67 头、0 新违规、DEC-096 两条既有 seam；10 份 Markdown
  的 681 个本地路径、标题/围栏检查与 git diff --check 通过。同步 DEC-143、专项设计、
  计划、示例、README 和依赖反馈；代码和文档未提交/推送。
- M15-14 完成，M15-04/06 与总体 M15 保持 In Progress。产品长期存储 owner 的总量/
  原子预留/孤儿恢复仍依赖上游 M31；真实桌面采集、服务跨进程恢复、磁盘/断电故障、
  共享证据隐私删除、Agent 监督 RPA、native/Windows 和收益未验收。负责人 Mira/Mirage
  维护者，待公开契约/授权升级和 M15-03～09 的目标环境就绪后补跑。

### 2026-11-12：M15-15 Host 显式 Agent 恢复

- 基线 e00debf，加本工作树未提交的 M15-13/14；冻结 DEC-144，M15-07 进入
  In Progress。核对 Mira workflow_recovery、ModelGateway/IModelProvider、Workflow
  公共头、API 与 M14 测试，以及 kairo-integration by-api / tasks-and-lifecycle 卡片。
  不读取 ModelLayer 私有 gateway，不新建模型栈/线程/队列/任务状态机，不改依赖。
- Host 提供 attach/start/poll/cancel/shutdown，外部注入同一 Executor 上的模型对象；
  强引用快照活过恢复对象。每 Run 一次、同时一次；请求期限 1ms～30s、尝试内修复
  0～1 次、跟踪 1～32 条，费用/token 沿外部 profile。Mira 承载决策/schema/补丁/
  权限/epoch/续跑与同桥审计；Host 只投影关联 ID、outcome/reason 和 ordinal。
  启动只是接纳，回执结算不是 Run 完成；poll 至多等待 1ms，游标只排除已读回执。
- 正向路径：真实 DesktopAtomToolset 的受控文件读取失败 -> WaitingAgent -> 脚本
  模型修改失败步骤实参 -> 实际 atom 成功续跑 -> Completed Run 编译新候选，候选
  包含修正路径、不含旧失败路径。模型请求 ToolChoice::None、无参数秘密值，审计
  可关联且无 rationale/参数/异常正文；本机 fixture 不访问用户文件或网络。
- 取消尝试保留等待；取消 Run 同时取消恢复生产者，迟到回答不能复活终态。关闭先
  恢复排空，再 WorkflowRuntime/MiraRuntime，最后外部 Gateway/Executor；Host 不再
  丢弃 workflow 关闭失败。已关 surface 不可查询 Run，以真实终态事件核验关闭取消。
- 开发期：首轮遗漏事件解析头导致编译失败，补头后重建；测试将 outcome 误写为
  下划线已改为公开名称的连字符。故障注入复现 provider 异常穿透 future/析构而
  Subprocess aborted，登记 MIRA-20261112-002，在单一 integration Adapter 增加
  provider 异常边界，保留错误身份/重试属性并固定正文；不宣称任意内部异常已修复。
  多轮 need_user/次数耗尽又复现旧回执，即使游标过滤仍在 10 秒内无新回执，登记
  MIRA-20261112-003，将范围收敛为单次；没有自建 tracking 或更换 Run 绕过预算。
  后续取消测试的返回体不符 pinned 模型取消契约、关闭后仍查询 Run 的错误预期均已
  修正。最终以下矩阵均来自修正后重新构建的二进制，不使用失败构建后的旧结果。
- 最终恢复测试 **544 检查、0 失败**：修复/续跑/新候选、四类决策、坏 JSON/补丁、
  provider 错误/异常/诊断异常、绑定/期限/修复预算参数拒绝、重复/并发容量拒绝、
  执行中取消、模型期限、取消 Run 后迟到返回、权限拒绝与关闭排空。权限拒绝场景
  即使得到 patched-and-resumed，Run 仍等待且无成功读，验证回执不冒充实际成功。
- 环境沿前增量 Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、
  clang-format 18.1.8，非 native/OCR。四配置各先执行
  `cmake --build --preset <preset> --target workflow_recovery_host_test mira_host_test runtime_service_test visual_memory_test visual_memory_write_test local_visual_workflow_test mirage mirage-service -j 4`，
  退出 0；再执行
  `ctest --preset <preset> -R '^(workflow_recovery_host_test|mira_host_test|runtime_service_test|visual_memory_test|visual_memory_write_test|local_visual_workflow_test|workflow_experience_cli_test)$' --output-on-failure`，
  TSAN 前缀 setarch x86_64 -R。Debug/ASAN/UBSAN/TSAN 均 **9/9 通过，0 skip**
  （7 个业务测试 + 2 个隔离 fixture），无 sanitizer 报告；恢复/Host/服务/视觉记忆/
  监督写入/本地视觉 Workflow 分别 **544/224/712/229/306/177** 检查、0 失败。
  原始日志在本机 build/m15-recovery-<preset>-final-build.log / final-test.log。
- mirage-format-check / mirage-boundary-check 通过，67 头、0 新违规，DEC-096 两条
  既有 seam；12 份 Markdown 的 704 个本地路径、标题/围栏与 git diff --check 通过。已同步 DEC-144、
  主/专项设计、总/专项计划、反馈、README 和接线示例；本工作树增量未提交/推送。
- M15-15 完成，M15-07 与整体 M15 保持 In Progress。后续负责人 Mirage/Mira 维护者：
  上游异常/尝试身份契约交付并授权升级后解除临时限制；模型共享/串行推理/热重载边界
  冻结后接 Runtime Service/IPC/native 与显式或自动触发；M15-06～09 的长期 Lesson、
  在线模型、真实桌面变化/接管、磁盘/断电、Linux/Windows 和收益矩阵仍未验收，待相应
  owner/公开接口与目标设备就绪后补跑。本增量不代表产品监督 RPA 闭环完成。

### 2026-11-14：M15-16 Host 会话内学习

- 基线 8f05a5b，工作树起始干净；Mira be44e36 / kairo 10e870b、Mirador 2a9f483、
  eui-neo 19acab6 均不变。按 DEC-145 核对 Mira workflow_runtime/learning/events、
  IMemory/SqliteMemoryStore 公共头、Workflow API、DEC-030 与 M13 测试；并发遵循
  pinned kairo-integration 的 by-api / tasks-and-lifecycle 卡片。不修改第三方源码。
- attach_workflow_surface 可选注入 WorkflowLearningMemory，空 memory 拒绝且不占用
  surface，省略保持关闭。Mira 安装前共享句柄按值传递，修改 caller binding 不会丢失
  Runtime 的强引用。scope 固定 primary Session 真实 ID，无外部 scope/热替换入口。
  默认单次检索 8 条/1024 token/250ms 为 Mira 暂定值，非产品容量/延迟校准。
- Episode、失败检索和显式恢复 Lesson 均由 Mira 承载；新 Host 接口只返回 run/lesson
  ID/digest，错误码加固定说明。自动 Episode 写失败不改写已结算终态；检索失败降级
  为空但继续 WaitingAgent；Lesson 错误可能已真实写入，沿同 Run mutation ID 幂等，
  不创建私有数据库、线程、队列、后台重试或自动擦除。
- WaitingAgent 快照可以先于检索结束；启用学习时，恢复准入调用 Mira wait_run 最多
  1ms 确认当前驱动完成，尚未就绪返回 invalid_state 且不启动模型。真实 SQLite 的
  受控查询阻塞验证这个边界，解除后显式下一次驱动成功，未用 sleep 猜测检索已完成。
- 正向：失败 atom -> Agent patch -> 实际成功 -> 终态 Episode -> 显式 Lesson ->
  下一 Run 同签名失败 -> Lesson 进入模型请求且 lessons_kept > 0 -> 实际恢复完成。
  第三 Run 撤销权限后即使得到 patched-and-resumed 也继续等待、无新增成功动作，
  Lesson 不提升权限。无参数值/路径/rationale/后端错误正文进入记忆或审计。
- 受控存储为唯一外部 Executor 的真实 SqliteMemoryStore；故障 probe 只转发公开接口，
  没有第二记录表。验证 Completed/Failed/Cancelled Episode、DryRun/缺 context 不写、
  Lesson 负向准入、幂等 ID/版本、空 binding、写失败、真实提交后报错、查询降级、
  会话 scope 隔离。关闭 Host 后关闭/重开 SQLite，实际记录仍可读；显式擦除 Session
  后再重开仍不可读。全部 Store 在 Executor 之前关闭/析构，无用户文件或网络参与。
- 开发期首次构建因新测试遗漏 DesktopAtomToolset::build 的权限 gate 参数失败，补齐
  明确拒绝 gate 后重建；Debug 定向含 fixture **5/5 通过**。旧恢复 fixture 提取为
  共用测试头以复用同一模型/真实 atom，旧恢复仍 **544 检查、0 失败**。下述完整矩阵
  来自修正后重新构建的所有目标，不使用失败构建后的旧二进制。
- 环境：Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、clang-format
  18.1.8；fetch OFF、无 ccache、沿非 native/OCR 配置。四配置各执行
  `cmake --build --preset <preset> --target workflow_learning_host_test workflow_recovery_host_test mira_host_test runtime_service_test visual_memory_test visual_memory_write_test local_visual_workflow_test mirage mirage-service -j 4`，
  退出码 0；随后执行
  `ctest --preset <preset> -R '^(workflow_learning_host_test|workflow_recovery_host_test|mira_host_test|runtime_service_test|visual_memory_test|visual_memory_write_test|local_visual_workflow_test|workflow_experience_cli_test)$' --output-on-failure`，
  TSAN 前缀 setarch x86_64 -R。Debug/ASAN/UBSAN/TSAN 均 **10/10 通过，0 skip**
  （8 个业务测试 + 2 个隔离 fixture），无 sanitizer 报告。新测试逐配置检查数见下项，
  恢复/Host/服务/视觉记忆/监督写入/本地视觉均 **544/224/712/229/306/177**，0 失败。
  日志在本机 build/m15-learning-<preset>-final-build.log / final-test.log。
- 新测试 Debug/ASAN/UBSAN 为 **283**、TSAN 为 **282** 检查，均 0 失败；事件审计遍历
  的检查数受读取时点影响，核心结果断言一致。格式/公共头检查通过，67 头、0 新违规、
  DEC-096 两条既有 seam；7 份 Markdown 的 563 个本地路径、标题/围栏检查与
  git diff --check 通过。同步 DEC-145、主/专项设计、总/专项计划、示例和 README。
- M15-16 完成，M15-06 与整体 M15 保持 In Progress；代码/文档尚未提交或推送。
  产品长期 owner/总量/恢复、跨会话授权、普通 Agent 轨迹、模型共享/热更、IPC/native
  自动触发、在线模型、真实桌面/接管、磁盘/断电、Windows 与收益仍未验收。负责人
  Mirage/Mira 维护者，待 M15-03～09 的公开接口、owner/容量方案和目标设备就绪补跑。
  本机对象重开不能当作产品跨进程或断电恢复保证。


### 2026-11-14～16：M15-17 默认学习、长期元数据与受控在线验收

- 维护者明确要求默认学习开启、确认“在线验收”，并授权补齐 Mira 及升级 pin。
  DEC-146 冻结产品默认、私有作用域、预算与验收边界；复用 pinned public
  SqliteMemoryStore/WorkflowRuntime/RecoveryOrchestrator、ModelGateway 与 kairo
  StoreChannel，不引入第二存储、任务队列、线程或调度器。
- Mira be44e36 -> 2c42627a23da83c6d04825c28163836f9f747ec3：88c6836 为 DEC-111
  文件长度接纳/预留/snapshot，e1eab33 为 DEC-112 显式恢复输出协议与生成预算；
  232c9d1 修正 Windows 原生 VFS 文件名匹配，2c42627 修正嵌入式架构检查。
  四个范围化提交已普通推送 feat/bounded-memory-storage，未合并 master、发 tag
  或修改 kairo/Mirador/eui-neo。gitlink/lock 同步，供应链见
  [审计](../supply-chain/mira-workflow-learning-20261114.md)。
- Runtime Service 默认打开产品 state/workflow-learning/memory.sqlite；缺省 true，
  service.json runtime.workflow_learning_enabled=false 明确关闭。共享外部 owner
  使用 OS 用户私有目录及固定 Agent scope mirage.workflow.local-user.v1，允许本
  主体跨 Session 召回，权限检查保持独立；直接 Host binding 默认仍 primary Session。
  文件长度硬限额 64 MiB，普通写入留 4 MiB 维护预留，在途 64、单操作 2s、busy 250ms。
  超限/坏配置/目录不可用明确拒绝，不删除旧库、不换内存库、不无限重试。
- 实际存储 owner 在 Workflow 排空后、Executor 关闭前释放；遗留共享 facade 返回
  Unavailable。验证默认服务 Episode 落盘、服务关闭后另一 Executor 重开读取、显式
  关闭不创建库、错误预算/阻挡文件启动拒绝、设置 false 编解码及其他设置更新不丢失。
  容量测试独立测量目录四文件总长度，验证满额拒绝、保留旧记录/幂等、独占冲突、
  重开、擦除可能失败、超限旧库拒绝且不截断；四配置每项 58 检查。
- 专用 Workflow ModelLayer 模式复用生产 Gateway/provider/传输，拒绝普通聊天并用；
  binding 由公开 integration 接口给 Host，不访问私有对象。Text/JsonObject/
  StrictJsonSchema 显式选择，Text 回复仍经 Mira 同源本地 schema/补丁/权限/epoch
  校验；默认输出 2048 token，Host 允许 128～8192，显式思考模式，受信任白名单参数
  投影。空投影继续隐藏值。产品原生模型共享/热更和自动触发仍后续。
- 真实配置模型 deepseek-flash / anthropic.messages.v1，Text、关闭思考、30s 期限、
  输出 2048 token、单 Run 单次恢复。两轮均 patched-and-resumed，实际文件 atom
  完成；首轮写 Lesson，第二轮上下文带 1 条历史 Lesson 并再写成功。输入/输出
  合计 1818/363 token，3319ms。另一独立进程打开同库，按确定性 ID 核对 2 条
  Episode、2 条 Lesson；文件长度运行快照 337848、重开 110592 字节，均低于预算。
  使用隔离合成文件/状态、标准钥匙环解析，无凭据、用户文件正文或原始模型输出进入报告。
- 开发与最终共 6 次恢复尝试，4 次失败、最终 2 次成功，未改写失败。首轮 Host 5s
  默认期限错误、gpt InvalidModelRequest 具体远端字段未确认；两轮 Messages 的
  decision-invalid 分别暴露本地 parser 接线缺失及无法逐字归因的决策失败。补同源
  parser 和明确 step ID/value 契约后最终通过。只有 5 次 provider 调用有仪表，首轮
  未仪表化，缺失 usage 不填零；详见[脱敏证据](../compatibility/workflow-learning-20261114.md)。
- 在线请求后模型协议代码未再变化。最终 pin 的 VFS/工具修正及 Host 恢复准入结算
  边界由离线矩阵验证；重建最终验收入口后再次以 --inspect 独立进程核对原库通过，
  未追加付费请求。该入口只经 MIRAGE_BUILD_MANUAL_ACCEPTANCE 构建，不进 CTest/CI。
- 回归期间一次 UBSAN 的 cancel 决策停留 WaitingAgent；检查确认公开 WaitingAgent
  先于 carrier epoch/当前驱动结算，原 1ms wait_run 门禁只对学习启用。现所有恢复
  准入均先有界结算，未就绪返回 invalid_state 且不消耗尝试；fixture 只对该明确
  暂态继续驱动，其他错误立即失败。恢复 Debug 连续 20 次通过，取消状态/零新增动作
  断言保留。另有端点探针在 sanitizer 回归中未匹配预期网络阶段错误；词表未覆盖明确 DNS 超时，补精确 DNS
  阶段超时结果，继续拒绝 EndpointPolicyDenied；不把旧失败或失败构建后的二进制作为验收。
- CI 首轮 Windows open 失败修正后两 Windows 配置通过；第二轮 quality 显示既有
  sqlite_memory_store 规模违规。本地检查器按绝对路径过滤 third_party，实际漏扫，
  原“架构通过/陈旧基线删除”撤销作为证据。2c42627 改为相对仓库根过滤，补普通/
  third_party/build 三种父路径测试并恢复 be44e36 既有规模债务；真实扫描 16 项
  既有债务、0 stale、0 新违规，没有新增豁免或提高预算。
- 最终 Mira [CI 43784628663](https://github.com/Linductor-alkaid/mira/actions/runs/43784628663)
  **12/12 job 通过**：Linux GCC/Clang Debug/Release、Windows Debug/Release、Android
  两 ABI、ASAN/UBSAN/TSAN 与 quality。该 CI 不替代 Mirage Windows 产品设备验收。
- Mirage 最终四配置构建/测试与文档检查结果见下方收口记录。
- 完成范围仅 Linux 默认 Workflow 元数据、受控 Host/生产 ModelLayer/真实模型与跨
  进程重开；M15-04～09 其余、完整 M15 保持 In Progress。普通 Agent 自动轨迹提取、
  视觉工件长期总量/跨 store 回收、native 模型共享/自动恢复、真实桌面布局变化/接管、
  真断电/磁盘故障、Windows 设备及收益矩阵尚未验收。负责人 Mira/Mirage 维护者，
  待公开接口、owner/容量方案和相应设备就绪按原门禁补跑，不用元数据增量关闭完整 M31。


#### 最终收口（2026-11-16）

- 四配置分别配置 `cmake --preset <preset> -DMIRAGE_FETCH_DEPENDENCIES=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=`，
  并执行 `cmake --build --preset <preset> --target workflow_learning_store_test mira_memory_capacity_contract_test mira_recovery_contract_test workflow_learning_host_test workflow_recovery_host_test mira_host_test runtime_service_test visual_memory_test visual_memory_write_test local_visual_workflow_test persistence_test model_profile_endpoint_test mirage mirage-service -j 2`。
  各目标构建退出 0；最终端点断言调整后四配置重新构建 model_profile_endpoint_test，
  退出 0。Debug 另重建 workflow_learning_online_acceptance，仅离线 --inspect 读取通过。
- 最终执行 `ctest --preset <preset> -R '^(workflow_learning_store_test|mira_memory_capacity_contract_test|mira_recovery_contract_test|workflow_learning_host_test|workflow_recovery_host_test|mira_host_test|runtime_service_test|visual_memory_test|visual_memory_write_test|local_visual_workflow_test|persistence_test|model_profile_endpoint_test|workflow_experience_cli_test)$' --output-on-failure`；
  TSAN 前缀 `setarch x86_64 -R`，四配置顺序运行，避免同时压入真实 DNS 探针。
  Debug/ASAN/UBSAN/TSAN 各 **15/15 通过，0 skip**（13 业务测试 + 2 隔离 fixture），
  无 sanitizer 报告。Host/恢复/学习/容量/owner/端点/本地视觉/服务/持久化/视觉记忆/
  写入各 **224/600/282/58/38/43/177/727/584/229/306** 检查，四配置均 0 失败。
  原恢复断言保留，fixture 对明确的 drive-is-settling 暂态驱动替代一次 ok 检查；
  历史轮次事件遍历数也可随读取时点不同，不用旧计数推断覆盖变化。
- 本地日志 build/m15-final-<preset>-configure.log、-build.log、
  m15-final-<preset>-endpoint-build.log（UBSAN 为 m15-endpoint-ubsan-build.log）及
  build/m15-accepted-<preset>-test.log。旧失败日志保留，不用其结果覆盖最终矩阵。
  恢复额外 `--repeat until-fail:20` Debug 通过，日志 m15-final-recovery-repeat-debug.log。
- mirage-format-check / mirage-boundary-check / format-check / architecture-check /
  platform-boundary-check / docs-check / sbom-check 全部退出 0。Mirage 67 公共头、0
  新违规、DEC-096 两条既有 seam；Mira 379 文件格式、真实架构扫描与检查器回归通过。
  锁文件比对仅 Mira commit/说明与日期变化，嵌套 pins/artifacts 和其他依赖不变。
  本轮 Markdown 本地路径/标题/围栏及 git diff --check 通过。
- M15-17 限定增量完成，M15-06/07 和整体 M15 仍 In Progress。Mira 功能分支已推送，
  Mirage 主工作树代码/文档/pin 保留未提交；没有合并、发布或修改仓库设置。


### 2026-11-16：M15-18 学习存储只读状态

- 以当前未提交 M15-16/17 增量为基线，按 DEC-147 核对 Mira 的公开
  SqliteMemoryStore::capacity_snapshot 与既有 WorkflowLearningStore、服务串行请求
  路径。没有修改任何 pinned 源码/gitlink/lock，没有新增异步任务、线程、队列或缓存。
- 增加 v1 加法操作 workflow.learning.status 和 mirage workflow learning。直接投影
  Mira 的文件长度、限额、维护预留和进程已接纳峰值；读取失败返回固定 unavailable。
  关闭时返回 false/四个零且不打开库；不返回路径、主体、记忆正文，不修改配置或记录。
  CLI 无修改/清理选项，未知操作数拒绝，旧服务/错误 payload 明确失败。
- 协议验证回环、缺字段、布尔错类型、负数、小数、越限、预留越界、峰值越界和关闭
  却非零响应；保留既有 golden 契约。实际服务验证默认启用、64MiB/4MiB 预算、
  显式关闭无数据库。独立服务进程的 CLI 验证精确数值字段集合、查询前后容量一致、
  无效操作数拒绝，继续原经验导出/归纳/发布/重启导入流程，零在线模型请求。
- Ubuntu 24.04.5/GCC 13.3/CMake 3.28.3，沿既有非 native/OCR 与 fetch OFF 配置。
  四配置各构建 ipc_protocol_test ipc_protocol_golden_test runtime_service_test mirage
  mirage-service（cmake --build --preset <preset> --target <以上目标> -j 2；Debug -j 4），
  退出 0；随后执行
  `ctest --preset <preset> -R '^(ipc_protocol_test|ipc_protocol_golden_test|runtime_service_test|workflow_experience_cli_test)$' --output-on-failure`，
  TSAN 前缀 setarch x86_64 -R。Debug/ASAN/UBSAN/TSAN **各 6/6 通过，0 skip**
  （4 业务测试 + 2 隔离 fixture），无 sanitizer 报告。协议/golden/服务各
  **537/1805/731 检查、0 失败**。日志本机 build/m15-status-<preset>-build.log / -test.log。
- mirage-format-check / mirage-boundary-check 退出 0，67 公共头、0 新违规、DEC-096
  两条既有 seam；git diff --check 与 Markdown 本地路径/标题/围栏检查通过。
  DEC-147、主/专项设计、IPC v1 契约、总/专项计划、接线示例与 README 已同步。
- M15-18 限定增量完成；整体 M15 与 M15-04～09 保持原开放状态。Linux 以外产品
  设备验收、跨 store 回收/视觉长期总量、原生共享模型和自动恢复仍未验收，负责人
  Mira/Mirage 维护者，待相应公开接口/owner/设备就绪补跑。工作树继续保留未提交。


### 2026-11-16：提交收口

维护者要求提交。默认学习、长期元数据 owner、受控在线验收及配套只读容量诊断作为
同一完整产品增量提交；Mira 指针/lock 与供应链审计同步，Mira 功能分支此前已推送。
Mirage 未推送。提交前复核状态/差异/暂存区与本人 Git 身份，无无关文件、编译产物或
凭据。沿用已记录四配置学习链路各 15/15、诊断专项各 6/6 与 Mira CI 12/12 结果，
本次仅同步提交记录，不重复运行模型请求或无变更测试。此前“未提交”描述保留为历史状态。

### 2026-11-16：状态统一与 M15-19 产品恢复接线

- 总计划 Planned、M10 索引 Proposed 及 DEC-095 待冻结的滞后说明已按专项事实更新。
  总计划、M10、M15 的状态和已完成增量范围统一；历史失败与未验门禁保留。
- 基线 7b5bc28 上的本轮未提交工作树；新增 DEC-148，复用产品 ModelLayer 的独占预留，
  接显式 IPC/CLI start/status/cancel/lesson。首次绑定后模型切换需重启；不重建编排器
  重置预算，不创建第二模型栈或后台轮询，不修改第三方依赖。
- 实际 RuntimeService/脚本模型验证缺失文件 -> patch -> atom 续跑 -> completed ->
  显式 Lesson；验证聊天互斥、模型配置拒绝、重入/容量、异常、取消、实际 30s 超时和关闭。
  Debug/ASAN/UBSAN/TSAN **各 12/12，0 skip**，格式/67 公开头边界检查通过；
  11 份 Markdown/629 个本地路径链接与围栏、345 个既有 golden 不变核查通过。
  准确命令、检查计数、开发期失败及限制见
  [验收记录](../compatibility/workflow-recovery-service-20261116.md)。
- M15-19 限定增量完成；M15-03～09 与完整退出门禁继续开放。native/自动触发、
  真实桌面变化/收益、长期视觉配额与 Windows 实跑未验，不由受控 Linux 测试替代。

#### M15-19 提交收口（2026-11-16）

维护者要求提交并继续推进；本次将状态同步、DEC-148、产品恢复接线、测试和验收记录
作为同一产品增量本地提交，不推送。提交前复核本人身份、工作树/差异/暂存区和既有
四配置各 12/12 结果；仅补提交记录，不重复运行未改变的模型请求或测试。以上“未提交”
为验证时状态，后续 native 接线以本次提交为新基线。

### 2026-11-16：M15-20 原生运行与显式恢复入口

- 基线：M15-19 已按维护者要求本地提交为 `89fc251`，未推送；在相同工作树继续。
  DEC-149 冻结现有窗口标题栏入口、目录/Run/学习快照、默认参数运行、取消、显式
  恢复/查询/经验保存；用户草稿/会话保留。M15-08 进入 In Progress，完整审阅/资产入口未关闭。
- UI 只存有界投影和请求修订号，复用 RuntimeBridge 的 Executor/串行请求/事件通道。
  失败或事件缺口保留旧目录/Run 并标为过期，禁用依赖状态的动作；无自动恢复、重试或新轮询。
- Debug/ASAN/UBSAN/TSAN 的投影/真实原生 IPC 链路/既有服务和 Agent 回归各 **6/6**；
  评审修正后受影响两门禁各 **4/4**，0 skip。native-debug、ASAN、UBSAN 产品及 renderer
  构建通过，最终 renderer 各 **3/3，34215 检查**、0 失败；TSAN GPU renderer 未运行，
  跨上下文路径使用功能 TSAN。格式/公开头边界检查通过。
- 14 份真实产品渲染的合成场景覆盖明暗、常规/最小、收起侧栏、长名称/等待/失败/终态/
  实际滚动。独立首评 fix 的两项（选中行对比度、刷新优先提示）经一次修正后均 resolved；
  复核的 ship 只覆盖这两项。开发期失败、准确命令、记录位置与限制见
  [验收记录](../compatibility/native-workflow-controls-20261116.md)。
- M15-20 限定增量完成；M15-03～09 与完整产品退出门禁继续开放。资产审阅/参数编辑/
  导入发布、自动恢复、视觉长期配额/回收、真实桌面收益和 Windows 实跑仍后续。

最终文档收口：产品说明、原生使用/设计和专项设计同步；14 份 Markdown/572 个本地
路径链接与围栏、14 份截图尺寸/hash 及 git diff --check 通过。既有 DESIGN/token
保持不变。89fc251 是已提交服务增量；本轮新增 native 增量仍在工作树，尚未提交。

### 2026-11-16：M15-21 工作流图编辑与独立调用

- 状态：Completed（Linux 限定增量）；负责人：Mirage 维护者；依据：[DEC-150](../decisions/DEC-150-workflow-graph-composition.md)。
- 将入口迁移为侧栏左上角会话/工作流切换；工作流资产选择、真实 IR 图与节点检查器。
- 复用 Mira 公共 IR 与 Skill API，实现显式保存/发布、条件与原子任务编辑、最新已发布版本调用、循环检验及跳转。
- 验收：Debug 功能 8/8；ASAN/UBSAN 含 renderer 各 9/9；TSAN 功能 8/8；native renderer
  最终 40657 检查、0 失败，26 图尺寸/SHA 验证。独立首评 fix 的五项同批修正，首次评分
  四项 resolved / F1 hover partial；单色补正后 F1 resolved，最终 ship 只评分这五项。
- UI、服务、CLI 产品构建、格式/公开头边界与文档同步完成；未执行 Windows、真实桌面/
  在线模型质量和目录跨重启恢复。当前目录只在服务内，长期学习存储另行持久化。
  完整 M15-08/09 保持开放。准确命令、历史失败与补跑责任见
  [图与调用验收](../compatibility/workflow-graph-calls-20261116.md)。
- 沿用当前工作树，保留 M15-20 增量；本轮尚未提交或推送，不升级依赖。

M15-21 最终收口：产品 UI/服务/CLI 构建通过，21 份 Markdown/628 本地链接与26图尺寸/
SHA、git diff --check通过，无新增超过1MiB文件；依赖 pin 和既有 DESIGN/token未变。
保留当前工作树全部 M15-20/21 增量，尚未提交或推送。

### 2026-11-17：M15-20/21 提交收口

维护者要求提交并继续。本次提交已验收的原生运行/恢复、工作流图编辑、独立动态调用、
循环检验、相关测试与文档证据，不推送。上文“尚未提交”为各次验收时状态；后续以本次
提交为基线推进工作流目录跨服务重启持久化，完整 M15 仍保持 In Progress。
