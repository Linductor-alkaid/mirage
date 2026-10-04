# 原生模型设置与通用 harness 验收（2026-10-04）

关联：M6-03、DEC-034、MIRA-20261004-001/002。负责人：Mirage维护者。
范围：Linux原生UI → 真实IPC → MiraRuntime任务身份 → 模型网关/显式通用工具。
本轮不接屏幕、桌面动作或RPA workflow；不是调用Mira设备AgentLoop。

## 实现与证据

模型页保存origin/API路径/模型ID/协议/凭据变量名称；配置合并写回既有service.json，
首次启动与--config都选择正确保存目标；活动轮次忙拒绝、校验/存盘失败保持旧模型。
前端单个Executor worker顺序发送IPC（持久SessionClient仅允许一个outstanding请求），
事件用128容量MpscChannel，16请求上限。后台接纳不等于完成；future被消费，异常、
取消、超时、预算、连接失败有明确结果。退出只关闭UI的IPC owner，服务整体退出仍属M6-04。

首步工具仅wait。16推理/32工具，单工具2KiB/累计反馈8KiB；网关token总预算可提前拒绝。
反馈是有来源的JSON文字，缺少规范tool-result input的限制见001；不宣称原生wire互操作。
每轮以MiraRuntime task ID执行，终态幂等；UI迟到pending不能覆盖终态，也不能让旧轮次
关闭新轮次的运行状态。UI草稿、主题、侧栏宽度仍是当前进程内状态。

## 验证结果

| 项目 | 实际结果 |
| --- | --- |
| native-debug/native-release构建 | 通过 |
| Debug完整回归 | 51/51通过；测试夹具启动竞态修正后新集成测试再通过1/1 |
| Release针对性回归 | 4/4通过：模型投影、旧协议golden、服务、原生IPC/harness |
| 新集成测试 | 51 checks：完全无screen Provider环境、工具回填、非法工具、错误、模型校验/配置合并、忙拒绝、运行中取消、超时、预算、burst顺序、断线与shutdown、配置损坏保持旧值、事件溢出恢复、Executor容量拒绝 |
| ASAN/UBSAN模型与新集成测试 | 各2/2通过 |
| TSAN | 默认unexpected memory mapping无法启动；setarch x86_64 -R执行51 harness + 141 chat checks通过，无race诊断 |
| 公共头边界 | 48 headers / 0 violations |
| GUI真实保存 | 创建隔离XDG service.json，返回已保存/已应用；未改用户原配置 |
| GUI真实对话 | 本机SiliconFlow Qwen/Qwen3.5-4B，经UI发送中文、IPC事件与历史回显真实回复 |
| SiliconFlow实际模型 | 文字1次推理通过；工具3次推理/2次实际wait执行通过，最终中文回复 |
| MiniMax实际模型 | 文字/工具均TLS校验失败；系统带SNI验证成功，无SNI返回错误域名证书，002记录缺口 |

真实模型配置/密钥来自用户明确授权的~/mira/docs/model_provider；密钥只在测试子进程
环境传递，仓库、IPC、配置与日志仅包含变量名。未关闭证书校验、未改third_party、未提交
上游issue。M6-03的Linux通用harness首步验收范围见M6计划；下面缺项不计入完成。

截图与机器记录：
[模型浅色](../../.impeccable/review/native-harness-model-final-light.png)、
[模型深色](../../.impeccable/review/native-harness-model-final-dark.png)、
[最小浅色](../../.impeccable/review/native-harness-model-final-min-light.png)、
[最小深色](../../.impeccable/review/native-harness-model-final-min-dark.png)、
[最小滚动](../../.impeccable/review/native-harness-model-min-scrolled.png)、
[真实回复](../../.impeccable/review/native-harness-chat-reply-light.png)、
[真实模型结果](../../.impeccable/review/native-harness-live-models.json)、
[源码/运行证据](../../.impeccable/review/native-harness-evidence.json)。

## 未执行与补跑

- Windows真实构建/中文IME/窗口未验收；负责人维护者；补跑Windows native presets与交互。
- MiniMax：负责人维护者与Mira上游；上游补SNI、授权升级pin后重跑两类请求。
- 服务重启恢复的旧会话没有pinned控制面对象，原生端只选本次服务活动会话，不承诺跨时代
  继续运行旧任务；完整会话迁移、流式token显示、扩展工具/MCP后续增量。
- 托盘/统一入口/运行中退出确认属于M6-04，不由本次UI关闭测试代替。

验证命令：Debug完整 `ctest --test-dir build/native-debug --output-on-failure`；
Release针对 `native_chat_model_test|ipc_protocol_golden_test|runtime_service_test|native_agent_integration_test`；
ASAN/UBSAN针对两项native测试；TSAN两项native二进制经 `setarch x86_64 -R`。
最终饱和夹具先等待阻塞任务实际启动，再请求取消并消费future，避免把正常排队期取消
误当作未处理异常。真实供应商返回的首尾空行仅在UI展示时去除，内部换行和原始历史保持。

## 文档与界面收口

PRODUCT/native DESIGN/sidecar已同步真实IPC、模型草稿与应用状态。独立复核
[disposition=ship](../../.impeccable/review/native-harness-final-verdict.md)覆盖两项修正
（旧预览声明、回复首尾空行）和所列八张Linux截图；不替代后端或Windows测试。
M6-03以Linux通用harness首步完成，M6整体仍In Progress。凭据扫描242份项目
代码/文档/证据文件，实际测试密钥命中0；用户服务配置未覆盖。
