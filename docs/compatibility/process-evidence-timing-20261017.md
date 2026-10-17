# 思考展示、完成证据范围与阶段耗时验证

> 日期：2026-10-17
> 状态：Completed（DEC-098 局部增量）
> 负责人：Mirage 维护者
> 工作项：M9-14、M9-09 / M9-11 首增量；各总项保持开放
> 首轮本机验证基线：`0841e28`，叠加本轮改动。PR 整理阶段另行适配 `330cfcc`
> （M9-03 Continuous）；该合入基线的回归与 CI 结果在下方单独记录。

## 修改结果

- 每轮思考展示仍限制为 8KiB。长段保留最多 4KiB 的 UTF-8 首尾摘要，新内容需要
  空间时将较早思考改为明确的省略提示；96 段优先淘汰旧思考/已结算活动，不淘汰
  正在运行的工具。旧历史中的空截断段展开后也解释原因。只改变展示，不改模型
  规范历史或把空展示误判为模型没有思考。
- 动作回执增加 evidence_scope/proves_arbitrary_goal，区分新帧、焦点、进程存在、
  前台标题、文本/语义存在。native 显示对应证明范围。提示明确聚焦、导航、播放
  应采用相应结果证据，复用随请求提供的新观察，已核验条件不要求机械重复 verify。
  不确定副作用与最终只读收尾门禁保留；没有实现从自然语言自动生成任意目标的
  机器验收器，也没有把模型文字声明提升为授权或成功证明。
- 新增固定四组 timings：inference/capture/ocr/settlement 的完成次数和累计毫秒。
  采样使用单调时钟，沿原 Executor 调用和现有事件/历史/持久化传播。失败、取消
  进入的调用也计量；缺记录显示未计量，旧数据不能补算。收尾可包含取帧/OCR，
  各项不是可直接相加的互斥 CPU 时间。原生取帧与 OCR 分开，传统整体 OCR 适配器
  的内部取帧没有伪造独立测量。

## 构建与自动回归

完整 `mirage-native-product` 和相关测试均已成功构建。Release 实际通过以下
12 个不同测试：

`native_chat_model_test`、`conversation_budget_test`、`ipc_protocol_golden_test`、
`persistence_test`、`harness_desktop_tools_test`、`native_agent_integration_test`、
`native_conversation_view_test`、`desktop_image_supply_test`、
`post_action_verification_test`、`x11_visual_marks_test`、`pet_bubble_test`、
`pet_status_integration_test`。

首次完整 CTest 为 10/11（含 setup/cleanup）：唯一失败是容量夹具仍要求 12 轮
文件大于 512KiB。新策略减少了单轮展示量，随后将夹具扩为 24 轮，保留原大文件
边界与 1MiB IPC / 重启恢复验收，未通过放宽断言掩盖失败。修正后单项 CTest
3/3（含 fixture），6.79 秒；其余通过结果不重复充数。

- 完整构建：`/tmp/mirage-process-verified-build.log`。
- 首次完整回归与容量修正：`/tmp/mirage-process-verified-tests.log`、
  `/tmp/mirage-process-harness-tests.log`。
- API 边界/既有 pet 回归：`/tmp/mirage-process-boundary-tests.log`，5/5，3.72 秒。
- ASAN：六个实际测试通过；首次 7/8 的同一容量断言失败，经修正补跑 3/3，
  13.66 秒，无 ASAN 报告。日志为 `/tmp/mirage-process-asan-tests.log`、
  `/tmp/mirage-process-asan-harness-tests.log`。
- UBSAN：六个实际测试及 fixture **8/8**，39.06 秒，无 UBSAN 报告。
  日志为 `/tmp/mirage-process-ubsan-tests.log`。
- 变更 C++ 文件 `clang-format --dry-run --Werror` 与 `git diff --check` 通过。

主要检查包括：长思考后段可读、UTF-8 及省略标记 codec、超 96 段、异常退出时
计时 scope 结算、请求取消/失败计数保留、timings 负值拒绝、事件/历史/重启恢复。
真实共享输入 fixture 断言取帧三次来自初始/输入前/动作后；最终收尾再增加一次，
shared_text_exists 的 OCR 从一次增至两次。没有为记录耗时新建线程、队列或调度器。

## 界面与本机启动

私有 Xvfb 显式帧测试执行 **27836 项检查、0 失败**。只进行一轮批量截图检查，
确认 1180×800 浅色和 760×620 深色下的阶段文本、过程列表和输入框布局。
日志为 `/tmp/mirage-process-visuals.log`，两张参考截图分别为
`/tmp/mirage-process-visuals/process-collapsed-light.png`、
`/tmp/mirage-process-visuals/process-timings-minimum-dark.png`。截图使用合成数据，
不代表本机真实任务的性能数值。

验证时未发现运行中的产品实例；用已配置的同一桌面入口命令启动更新后的
native-release。新 tray PID 863991、native PID 864010、pet PID 864011；
`frontend_ready=true`、`pet_state=running`、`active_work=0`，七个既有会话 ID
全部保留。结果在 `/tmp/mirage-process-live-validation.json`。

## 边界

没有重跑真实供应商模型或用户的鼠标任务，不能宣称实际任务耗时已降低或完全消除
重复观察。新任务会产生独立计量，旧历史只解释省略原因，不恢复已经丢弃的文本。
本轮没有改变生产并发调度；执行异常/取消故障注入和 ASAN/UBSAN，未运行 TSAN。
Windows、真实 compositor 多屏/DPI、完整目标验证器和真实任务耗时对比仍由 Mirage
维护者在对应环境补跑，M9 总项保持开放。协议扩展需要配套更新/重启服务与 native。
