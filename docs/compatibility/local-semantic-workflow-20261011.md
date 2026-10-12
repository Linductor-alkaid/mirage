# M7-11 本地语义 Workflow 首增量验收

> 状态：Verified（Linux X11/AT-SPI2 首增量；M7-11 全范围仍 In Progress）
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：[M7-11](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-067](../decisions/DEC-067-local-semantic-workflow-grounding.md)

## 已交付

产品 workflow 注册表在 Window、scoped Accessibility 与共享 ActionCoordinator 存在时
增加 `desktop.knowledge.activate`。IR 保存应用/页面/目标和固定后条件，运行时新观察、
唯一解析新引用、权限判定、单写者、前台/几何/标题与 epoch 门禁、语义激活一次、新语义
验证。后条件执行前已有时拒绝再派发；未知状态 Strict 失败、Recoverable 交为 WaitingAgent。
执行已知步骤不需要模型推理，不改变五秒组合预算或旧引用寿命，不修改依赖。

成功 ToolExecutionRecord.result 是 canonical JSON 字符串，保留 action_id、验证证据与
knowledge_digest。Pinned Mira 谓词只比较标量 step_result，示例用 Contains `"verified":true`；
信号在 DryRun 缺席时披露 NotEvaluable，而非真实业务验证。复合原子只有新证据成立才
返回成功字符串。`Exists` 在 DryRun 缺席时直接 false，不适用于本例发布门禁。

本轮同时修正共享 scoped action 的接管边界：Provider 内接管取消 lease 后，Provider
即使返回成功，仍返回可能产生效果的取消失败；后验证不把撤回的权限恢复为成功。
所有生产执行复用既有 WorkflowRuntime/Executor worker 与 Runtime owner，没有新并发路径、
队列或调度器。失败经 Mira ToolExecutionRecord/WorkflowRuntime 进入已有事实与事件面。

## 实际验证

| 配置 | 结果 |
| --- | --- |
| native-debug 全量 | 66/66 |
| native-release 全量 | 66/66 |
| ocr-asan 定向，ASAN + UBSAN halt_on_error / leak detection | 6/6 |
| ocr-tsan 定向，TSAN halt_on_error，setarch x86_64 -R | 6/6 |
| local_semantic_workflow_test | 125 checks，0 failures |
| 实际产品 GTK / AT-SPI2，Debug | 20/20 |
| 实际产品 GTK / AT-SPI2，Release | 20/20 |

定向包含 local_semantic_workflow、desktop_atom_toolset、post_action_verification、
harness_desktop_tools、mira_host、runtime_service。新测试覆盖新引用/目标位置变化、稳定
知识 digest、缺 Provider/owner、缺目标/重名/禁用/已完成、应用/页面失效、512 节点预算、
非法版本/UTF-8/控制字符/持久引用实参、权限/焦点/几何/标题/epoch/旧 scope、取消前/权限
等待中/派发后、运行中超时、接管、前后观察/派发异常与动作无效果。实际 Mira 发布无
动作、Strict verified 完成、Recoverable WaitingAgent、运行中取消与终态幂等、shutdown 后
拒绝、Executor 异常消费和停止后提交拒绝均已验。

外部测试 owner 在私有 XDG、DBus 和 Xvfb 1000×700 中启动**实际 mirage-tray 的嵌入式
Runtime**与系统 GTK3 fixture，关闭模型与 OCR。经 product workflow IPC 发布仓库中的
[IR 示例](../examples/local-semantic-save.workflow.json)，没有 model provider 或网络推理：

- DryRun 只发布，GTK 点击计数保持 0。
- 正例新引用激活 Save，真实 GtkLabel 由 Pending 变为 Saved，Strict completed，计数 1。
- 原页面后条件已有：Recoverable waiting_agent，计数仍 1。
- 插入新工具栏并移动窗口：同一 IR 重新解析，新引用成功，计数 2。
- 两个 Save、错误页面分别 waiting_agent，计数保持 2。
- 控件接受激活但不改变结果：waiting_agent，计数 3，不自动重发。
- 权限 deny：Strict failed，计数保持 3。
- 模型保持 disabled；等待 workflow 触发既有产品退出确认，经匹配 exit_epoch 的
  confirm_quit 后由 Runtime owner 排空并退出 0。私有进程和 socket 随 owner 清理。

早期验收没有被记成通过：fixture 的隐藏按钮仍出现在 AT-SPI 树中，且应用名最初误设，
均在动作前安全拒绝；测试读取了 encode_settings 省略的默认 enabled 字段、遗漏产品退出
确认，以及示例 ID 超长，均修正测试/示例后重跑。未放宽权限、歧义或新证据门禁。
数据、源文件摘要和逐场景状态见[机器回执](data/local-semantic-workflow-20261011.json)，
[Debug 产品记录](data/local-semantic-workflow-20261011/debug-product.json)和
[Release 产品记录](data/local-semantic-workflow-20261011/release-product.json)。

## 复现与开放范围

[操作与示例](../examples/local-semantic-workflow.md)给出发布/运行和私有产品验收命令。
构建使用现有 CMakePresets；sanitizer 两配置沿用已配置的 OCR 构建，但本轮未加载权重，
不把这些结果作为完整权重 TSAN 声明。验证没有操作用户桌面、改用户配置或提交/推送。

M7-11 全范围仍 In Progress：本轮是精确语义单步激活，不包括 OCR/模板知识、原生会话
自动选资产/Agent 恢复、AppModel 多页导航或自动跨重启恢复。Mira/Mirage workflow 目录
仍是进程内投影，IR 文件可本地保存，重启后需重新发布。示例无 Retry/循环，不宣称对
任意外部 IR/恢复 patch 的统一重放约束。固定后条件证明局部 UI 状态，不证明任意业务
提交成功，也不是跨进程原子桌面事务或阻塞调用的强制终止保证。

用户宿主、Windows/原生 Wayland、任意应用/DPI/跨屏未在此范围验证。负责人 Mirage
维护者；补跑条件为相应平台与支持语义的目标应用、初态和权限准备完成。当前无需用户
配合，下一增量先冻结 OCR/模板知识契约与选择/恢复边界，再用同一 workflow 路径验收。
