# 从成功 Run 生成经验候选（M15-02/10/11/12）

现有 CLI/IPC 已接通 [DEC-139](../decisions/DEC-139-workflow-experience-export.md)
导出；native 页面尚未接入。适用输入是当前 Host
运行表中真实执行并已 Completed 的 Workflow Run，不是普通 Agent Task ID 或聊天记录。

## CLI 导出与审阅

先用 `workflow runs` 确认来源 Run 已完成，为候选选择不同的 32 位小写十六进制
Workflow ID。下面占位 ID 需要替换为实际值：

```bash
build/debug/apps/mirage workflow runs --socket /path/to/private-service.sock
build/debug/apps/mirage workflow experience <completed-run-id> \
  --workflow-id <new-workflow-id> --name "保存表单候选" \
  --summary "从一次完成执行提取" --socket /path/to/private-service.sock \
  > candidate.workflow.json
```

stdout 只有 IR JSON；stderr 显示 candidate digest 和来源 Run/Workflow/digest。
这是用户主动导出的审阅正文，可能含敏感实参。命令不会创建库版本或运行任务。参数/
服务拒绝时返回非零且不输出候选；I/O 失败可能留下部分正文。shell 重定向仍可能
创建空文件，发布前先确认导出退出码。

审阅适用应用/页面、参数/秘密、坐标和临时引用、后条件、一次性副作用与原验证强度，
完成后单独调用既有发布门禁：

```bash
build/debug/apps/mirage workflow publish --file candidate.workflow.json \
  --socket /path/to/private-service.sock
```

发布是 DryRun，不表示当前桌面实跑验证。不要把一次成功、弱谓词或模型回答当作
再次执行保证；不确定副作用不得盲目重发。长期资产保存与 native 审阅由后续项推进。

## 多样本参数归纳

同一工作流版本、同一策略下 2～16 个不同的成功 Run 可归纳实参差异。首个 Run 是
anchor，参数默认值取它的观测值；每个候选保留全部样本的观测值与来源，不把结构差异
视为业务语义或安全证明。示例中 RUN_A/RUN_B/NEW_ID 都需替换为实际 ID：

```bash
build/debug/apps/mirage workflow experience RUN_A --also-run RUN_B \
  --workflow-id NEW_ID --name "参数化候选" --review \
  --socket /path/to/private-service.sock > candidate.review.json
build/debug/apps/mirage workflow experience RUN_A --also-run RUN_B \
  --workflow-id NEW_ID --name "参数化候选" \
  --socket /path/to/private-service.sock > candidate.workflow.json
```

review 文件是完整响应，其中 experience.parameter_candidates 给出名称、步骤位置、
JSON pointer、来源类型及 observed_values；experience.sources 按输入顺序列出来源。
默认导出的 workflow 文件含 `$param` 与 anchor 默认值，才可交给 publish。命令不自动
发布；候选可能包含秘密、坐标/临时引用或一次性值，需要审阅并编辑 IR 后再决定使用。
`--also-run` 可重复，最多 15 次；重复来源、失败/DryRun、混合版本或策略拒绝。完整
审阅数据超过 64 KiB 时整次拒绝，不静默丢弃样本或观测值。

## 服务重启后导入候选文件

[DEC-141](../decisions/DEC-141-workflow-experience-archive-import.md) 提供显式导入。
单 Run 也可加 `--review` 保存完整响应；文件上限 384 KiB，由用户保管。新服务不要求
旧 Run 仍存在，但只把其来源/观测视为文件中的声明，不恢复历史 Run、验证等级或记忆。
内容摘要用于核对 IR 身份，不是签名或来源真实性证明。

```bash
# 先审阅 candidate.review.json；可在另一个服务生命周期执行。
build/debug/apps/mirage workflow import --file candidate.review.json \
  --socket /path/to/private-service.sock
build/debug/apps/mirage workflow list --socket /path/to/private-service.sock
# 导入结果是 not_validated / draft；单独发布已审阅的 IR 后才可执行。
build/debug/apps/mirage workflow publish --file candidate.workflow.json \
  --socket /path/to/private-service.sock
```

导入核对 IR 的 workflow_id 与规范化 digest；正文改写、坏文件、超限、目录已满或目标
ID 已存在均拒绝，不覆盖当前资产。若需编辑，使用 IR 的 save/publish 编辑路径；不要
修改 review 文件并保留旧 digest。输入应为完整 `--review` 响应，原始 IR 文件不能直接
交给 import。CLI 文件读取有预算且拒绝读取长度不一致，不输出敏感正文到诊断。
若服务成功导入后 CLI 输出失败，草稿仍在目录中；用 list/get 核实，再次导入会报目标冲突。
这是用户文件的可携带性；自动保存、受管目录恢复、保留/删除和视觉工件仍由 M15 后续
工作项实现。

## Host 调用

在已启动并 attach workflow surface 的 Host owner 上，以调用方选择的不同目标
Workflow ID 请求候选：

```cpp
const auto candidate = host.compile_workflow_experience(
    completed_run_id, target_workflow_id, "保存表单候选", "从一次已完成执行中提取");
if (!candidate.ok) {
    // 按 HostError 处理拒绝；没有候选可供发布。
    return;
}
// candidate.definition_json：待审阅的 canonical IR，可能包含敏感实参。
// candidate.digest：候选内容身份。
// candidate.source_run_id / source_workflow_id / source_digest：来源。
// 将候选交给可信审阅方；不要记录完整 JSON，不要在这里直接发布或执行。
```

候选未进入版本库。名称/摘要、目标 ID 和来源相同的重复调用内容确定。DryRun、失败、
WaitingAgent、取消或不存在的 Run 明确拒绝；surface 关闭后也拒绝。候选沿用来源
生效参数和验证谓词，不自动补足原验证、移除秘密或将临时引用转换成持久目标。

后续审阅需识别一次性参数、秘密、坐标/临时引用及适用的应用/页面；发布仍经原有
`publish_workflow_definition` / `workflow.publish` 门禁。DryRun 只规划，不表示新桌面
已验证。自动提取普通 Agent 经验、持久视觉证据及 native 入口由 M15 后续项接线。

无需用户桌面或在线模型的受控运行示例见
[Host 测试](../../tests/runtime/mira_host_test.cpp) 的
`scenario_tool_call_steps_dispatch_desktop_atoms`：完成实际 atom Run -> 编译候选 ->
核对生效参数与来源 -> 确认未入库和零额外派发；同时覆盖不合格状态及关闭拒绝。

范围和验收见 [DEC-138](../decisions/DEC-138-workflow-experience-draft.md)及
[M15 计划](../plans/m15-supervised-rpa-experience.md)。

真实 CLI 与私有无显示服务的可重复门禁：构建 `mirage`、`mirage-service` 后执行
`ctest --preset debug -R '^workflow_experience_cli_test$' --output-on-failure`。
它仅使用隔离目录中的只读文件，无在线模型或用户桌面；不替代真实视觉 Workflow 验收。
