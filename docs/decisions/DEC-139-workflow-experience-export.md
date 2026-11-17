# DEC-139：经验候选的 IPC 与 CLI 导出

> 状态：Accepted
> 日期：2026-11-11
> 负责人：Mirage 维护者
> 冻结工作项：M15-10
> 替代/被替代：无；扩展 DEC-138，不改变其 Host 契约

> 后续扩展：[DEC-140](DEC-140-workflow-experience-parameter-induction.md) 在保留单 Run
> 字节的前提下增加显式多来源归纳和完整 review 输出；原导出/发布边界保持。

## 背景与问题

M15-02 已提供成功 Run 到只读候选的 Host 接口，但用户只能从 C++ 调用，尚不能从
现有产品命令取得候选并审阅。维护者要求继续推进，先将这个已验证增量接入现有 IPC
和 CLI，使“真实执行 -> 导出 -> 审阅 -> 既有发布门禁”可以操作。

## 决策

1. 协议 v1 加法新增 `workflow.experience.compile`，请求必填 `source_run_id`、
   `target_workflow_id`、`name`，可选 `summary`（缺省空字符串）。ID 为非 nil 的
   32 位小写十六进制，name 非空，name/summary 各不超过 8192 字节。请求字段闭集
   为 v/id/op 和上述四项，未知字段拒绝。Host 继续承担成功来源和 IR 资格判定。
2. 成功载荷使用独立 `experience` 对象，包含固定 `validation:"candidate"`、
   source_run_id/source_workflow_id/source_digest、workflow_id/digest 和 definition。
   digest 为 64 位小写十六进制，definition 为不超过 256 KiB 的 JSON 对象。
   这是候选状态，不是 `workflow.get` 的已入库内容；专用信封防止老客户端误识别。
3. 服务现有 Executor 串行请求上下文调用 Host 的有界同步接口，失败透传原错误，
   不添加后台任务、任务表、候选存储或事件生产者；不改变 Run/catalog、发布或权限。
   来源范围是该服务进程仍持有的 Run，重启后未知来源明确拒绝。
4. 新增 `mirage workflow experience <run-id> --workflow-id <target-id> --name <name>
   [--summary <text>] [--socket <path>]`。stdout 仅输出候选 IR JSON，stderr 仅给出
   candidate digest 及来源 ID/digest，方便显式重定向后审阅。敏感实参只在用户主动
   请求的候选正文中出现，不进入事件或诊断摘要。输出失败返回非零。
5. CLI 不自动写文件、不发布、不执行、不改 Memory。参数/服务拒绝时 stdout 无候选
   正文；I/O 失败返回非零，可能留下部分输出。文件是否创建由调用者的 shell 重定向
   决定。导出的 IR 可能含秘密、坐标或临时引用，
   仍须按 DEC-138 审阅。参数归纳、持久保存、native 页面与自动历史学习另行推进。
6. 不新增 hello 能力位；旧服务对未知 op 返回既有 protocol_error，CLI 将其作为失败，
   不回退到其他动作。golden vectors v23 -> v24，已有 wire 字节不变。

## 备选方案

- 只提供 Host 示例：不能使已有 Workflow 用户实际取得候选。
- 导出时直接 save/publish：会把经验候选变成库内资产，跳过来源/实参审阅。
- 同时实现 GUI、参数归纳与完整持久化：超出这个可独立验收的接线增量。

## 影响与风险

增加 std-only IPC 请求/响应类型与一个 CLI 子命令。旧客户端/服务器兼容边界明确；
输出 IR 是用户主动导出的内容，必须避免把正文混入日志。无持久格式变更、依赖升级或
新并发路径。导出只证明转换正确，不能为原流程验证强度或再次执行安全性背书。

## 验证方式

- 请求/响应正向、缺字段、错类型、超限、未知字段及旧响应兼容；共享 golden 全量。
- 实际 RuntimeService 完成 atom Run 后导出，核对来源与 digest、重复确定性、未入库、
  无新 Run；DryRun/失败/未知来源拒绝，关闭沿既有生命周期。
- 私有测试服务上的 CLI 导出 -> JSON 解码/审阅 -> 既有发布门禁，错误出口不输出 IR。
- Debug、ASAN/UBSAN、TSAN 适用专项及格式/公开头边界检查，准确结果写入 M15。

## 关联

[M15-10](../plans/m15-supervised-rpa-experience.md)、[DEC-138](DEC-138-workflow-experience-draft.md)、
[协议](../design/mirage-ipc-protocol-v1.md)、[示例](../examples/workflow-experience-draft.md)。
