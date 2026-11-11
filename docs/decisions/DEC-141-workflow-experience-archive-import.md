# DEC-141：候选文件跨重启导入为草稿

> 状态：Accepted
> 日期：2026-11-11
> 负责人：Mirage 维护者
> 冻结工作项：M15-12（M15-05 第二增量）
> 替代/被替代：无；扩展 DEC-139/140，不恢复历史运行或验证等级

## 背景与问题

已有 CLI 能导出 IR 和带来源/观测的 review 响应，文件由用户自行保存。服务重启后
Workflow 库与 Run 表为空，尚无导入完整候选文件的入口。核对 pinned Mira 的
workflow_runtime/versioning 公共接口、Workflow API 与 offline-composition-hosts
示例后确认：库是进程内投影，没有完整版本历史/Run 的快照恢复入口。Mira 的
FileArtifactStore/SqliteMemoryStore 可承载持久正文/记忆，但不等于恢复 Workflow 库。

本增量采用用户自持候选文件，不新建 JSON 经验库、版本库或受管资产目录；完整资产
持久化与生命周期仍由 M15-05/06 后续接线。这里验证可携带文件的完整性与重新准入，
不能把历史 source ID 或观测当作新服务中的已核验事实。

## 决策

1. `workflow experience ... --review` 的完整成功响应 JSON 作为候选文件输入，包含
   experience 的 candidate 状态、IR、digest 及原有来源/可选参数审阅。协议版本与
   DEC-139/140 codec 校验保持；文件/归一化 archive 对象上限 384 KiB。
2. 新增 `workflow.experience.import`，必填 archive 对象（上述完整响应）；请求字段
   闭集 v/id/op/archive。先按既有候选 reader 校验形状、身份、预算和成对审阅数据，
   拒绝失败响应、其他响应或坏归档。旧服务会拒绝未知 op，禁止回退成普通 save。
3. Host 再以 Mira parse_workflow_definition 与 workflow_definition_digest 核对 IR
   内容身份：IR workflow_id 必须等于候选 workflow_id，规范化 digest 必须等于候选
   digest；格式正确但正文被改写也拒绝。验证不执行工具、不查询或恢复历史 Run。
4. 服务在现有串行上下文先检查目录容量和目标 ID 未被使用，再经原 save 路径追加
   NotValidated 草稿；成功返回既有 WorkflowSaved。已有目录目标明确拒绝，不静默
   覆盖已发布资产。准入/校验失败不写目录；成功后 CLI 输出失败不回滚已导入草稿，
   可用 list/get 核实。来源只保留在用户文件中，不提升为服务事实或 Memory。
5. 新增 `mirage workflow import --file candidate.review.json [--socket ...]`；读取有界
   完整文件，不接受超限或读取中变化造成的截断正文。导入只产生草稿；再次执行须用户
   单独 publish，重新经过当前宿主的结构/DryRun 门禁和权限/新观察规则。
6. 用户文件是显式导出/导入产物，可能含敏感实参；不进入诊断正文。无后台自动保存、
   扫描、批量导入或新受管存储，不从 user 文件目录建立第二个事实源。Mira 仍管理
   Workflow 定义、版本化和运行，导入不声称保留原 version history 或验证等级。
7. 无新异步路径；串行请求已由 Runtime Service 唯一 Executor 管理。CLI 文件读取
   沿既有 helper 增加预算/完整性校验，默认上限为 IPC 帧预算，archive 使用更小上限。
   新增协议 golden v26；既有 v25 样本字节保持。

## 备选方案

- 将旧 runtime 内存表序列化后恢复：没有对应公开恢复接口，容易重建第二运行引擎或
  冒充历史验证；不采用。
- Mirage 自建磁盘 JSON Workflow Library：与 Mira 数据/版本边界冲突；不采用。
- 导入即发布/运行：会携带旧环境假设和弱验证，不经过新的显式准入；不采用。
- 只使用原始 IR 文件重新 publish：已有基础路径保留，但无法导入并校验完整候选文件。

## 验证方式

独立服务进程之间：导出并保存 review -> 正常关闭 -> 新服务目录/Run 为空 -> 导入 ->
NotValidated/不可运行 -> 单独发布 -> 默认/显式参数运行。旧 Run 不复活。覆盖改写正文/
错 ID/digest、坏归档/超限、已有目标和满目录拒绝；Host digest 校验失败不产生版本。
Debug/ASAN/UBSAN/TSAN、golden、边界和文档证据写入 M15；不替代断电恢复、自动
资产库、Windows 或真实视觉复用验收。

## 关联

[M15](../plans/m15-supervised-rpa-experience.md)、[DEC-140](DEC-140-workflow-experience-parameter-induction.md)、
[原库边界 DEC-023](DEC-023-workflow-contract-face.md)、[导入示例](../examples/workflow-experience-draft.md)。
