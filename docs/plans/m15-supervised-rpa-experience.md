# M15：任务经验、视觉记忆与 Agent 监督的 RPA

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M5 Workflow 服务面、M7 本地知识 atom；Mira 已交付编译/学习/恢复公开能力
> 建议发布点：经验候选契约预览；产品入口与发布标签另验
> 更新日期：2026-11-12

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
不启用历史批量导入、录屏、自动发布、无人监督重放、native 新页面或在线恢复模型。
本工作树基于 `1f9fba1`（现有 Mira 上游
同步开发线），不合并该开发线、不修改 pin 或第三方代码。

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
- [ ] `M15-06` 学习存储装配（In Progress，有限写入增量 M15-14）：共享既有 scope-bound Memory/ArtifactStore，
  在 Workflow surface 设置 learning context，验证终态 Episode、成功恢复 Lesson、
  失败检索和写失败可观测；不得另建 MemoryStore、JSON 经验库或图片数据库。
- [ ] `M15-07` Agent 监督与恢复（In Progress，Host 增量 M15-15）：装配 Mira RecoveryOrchestrator，冻结
  检查点/失败触发、次数/期限/成本预算、权限及副作用不确定行为；patch 校验后续跑，
  失败明确等用户/停止，取消/接管与 shutdown 期间零新增自主动作。
- [ ] `M15-08` native 单入口接线（Proposed）：呈现候选来源与验证等级、审阅/发布、
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

## 风险与阻塞

| 风险/依赖 | 负责人 | 解除条件/处置 |
| --- | --- | --- |
| 普通 Agent 自动轨迹提取尚无公开 API | Mira/Mirage 维护者 | 本地反馈 MIRA-20261111-001；上游契约交付并授权升级后接线；M15-02 可独立推进 |
| 旧历史可能只有文本/有界摘要，无完整动作证据 | Mirage 维护者 | 审计真实样本；缺证据返回不支持，只从具备证据的新任务开始，不补造历史 |
| 长期视觉容量、删除与多存储一致性未冻结 | Mira/Mirage 维护者 | [MIRA-20261112-001](../dependency_feedback/mira.md#mira-20261112-001) 与 M15-04/06：公开容量/恢复契约及 DEC-119 预算/故障/擦除测试；未通过前不启用产品长期保存 |
| 恢复 future 异常与多轮旧回执 | Mira/Mirage 维护者 | [MIRA-20261112-002](../dependency_feedback/mira.md#mira-20261112-002)/[003](../dependency_feedback/mira.md#mira-20261112-003)：单 Adapter 异常边界、单 Run 一次；上游结算/尝试身份契约修正并授权升级后移除限制；产品模型共享另验 |
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

- [ ] M15-01～15 均完成，所需后续决策已冻结。
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
