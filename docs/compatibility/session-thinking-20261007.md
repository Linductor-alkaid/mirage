# 会话思考与空历史恢复验收

> 状态：Completed（Linux X11 本机范围）
> 日期：2026-10-07
> 负责人：Mirage 维护者 / Codex
> 工作项：M6-26；依据：DEC-046、MIRA-20261006-002
> 状态：codex/session-thinking，父提交 6bdbcd4 之后本记录所在提交

## 问题与契约

原用户实例 session.list 返回 17 条：16 个恢复失败会话及 1 个内部主会话。
磁盘 17 条记录均无 journal 或 chat_turns；每次运行的内部主会话被写入历史，下次恢复占据
max_sessions=16。新草稿首次发送因名额耗尽被拒绝。排除空记录的保存和恢复，从源头消除累积；
非空历史保持 ID、编辑与续聊能力。恢复预留主会话，超量非空历史拒绝启动并保留原文件，
不能用部分恢复覆盖数据。默认容量 25 对齐 UI 的 24 个会话及 1 个内部主会话，自定义限额不放宽。

模型设置不再提供思考启用开关。model.get 投影有界 reasoning_options；model.set 不信任客户端
写入该能力。每轮 session.chat 冻结会话栏选项，服务端及 ModelLayer 依照实际模型再次验证。
MiniMax-M3 默认关闭，提供默认/关闭/开启；M3.1 Flash Preview 常开，提供 low/medium/high/xhigh/max，
不能套用 M3 的开关。明确映射 Claude Fable 5.1、Opus/Sonnet 4.6；未知 Messages 模型只显示默认。
OpenAI 兼容方言保留已声明 reasoning_effort。未对未持有凭据的模型宣称在线验证。

ZCode 参考固定源码 29628c9 的 ThoughtLevelCycleControl：按 provider 选项构造会话选择，
避免设置页再次启用。沿用当前 88×32px 输入栏按钮和右侧排列，本记录不宣称与运行中 ZCode
逐像素相同。参考 [ZCode](https://github.com/zai-org/ZCode/blob/29628c9/packages/ui/src/chat-input-toolbar/ThoughtLevelCycleControl.tsx)、
[MiniMax Messages API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)、
[Claude thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)、
[Claude effort](https://platform.claude.com/docs/en/build-with-claude/effort)。

## 依赖审计

Mira 7795e13 → fbc644be2fabfaa2f8257e579fbc1d368537224c，[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81)
基于 Messages PR#80，尚未合并。新增公开 ThinkingMode/ThinkingPart 和 profile 声明；
同步/SSE 保留签名、redacted 内容及完整 assistant block 顺序，ConversationLoop 在工具结果前回填。
思考不进入正文预览、不作为指令执行。普通 assistant 图片回填仍保留。

Executor 2ae4fc8、mbedtls、sqlite 及其他 nested pin 未变；Mira AGPL-3.0、Mirador MIT、EUI MIT
许可未变，没有新增第三方代码或并发设施。依赖源码修复保留在其独立提交；Mirage 只更新
submodule 与锁文件，模型策略留在 integration Adapter。当前上游 kairo master 需合并前在其基础复验。
新规范内容需要消费者同步重编译；新 UI 解码 optional 投影，旧严格解码器遇到新增字段需升级。

## 自动回归

环境 Linux x86_64、GCC、C++20；native-release、ASAN、UBSAN、TSAN，EUI 与 Mirador pin 未变。

| 验证 | 实际结果 |
| --- | --- |
| Release 全量 ctest | 52/52，无 skip |
| ASAN runtime_service / ipc_protocol / native_agent_integration | 3/3，detect_leaks=0；不据此宣称产品泄漏验证 |
| UBSAN 同上 | 3/3 |
| TSAN native_agent_integration | setarch x86_64 -R 后 208 checks / 0 failures；默认 ASLR 首次运行报 unexpected memory mapping，环境限制 |
| Mira Debug canonical/schema/dialect/SSE/gateway/anthropic/conversation_loop | 7/7；含所有 SSE 双片切点、签名/redacted、预算/非法值/角色/终态、工具顺序与图片回填 |
| Mirage format / Mira architecture | 通过；无新架构违规 |

native_agent_integration 覆盖 16 条空记录迁移、3 次重启不增长、非空 ID/编辑/续聊保留，
容量拒绝、失败/取消/关闭既有路径，以及 model.get 选项、恶意能力投影丢弃、adaptive/disabled
IPC 到 Provider 的参数传递及不支持档位拒绝。未新增异步工作路径；沿用 Runtime 的 Executor owner。

复现：

```bash
cmake --build build/native-release -j 4
ctest --test-dir build/native-release --output-on-failure -j 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan -R '^(native_agent_integration|runtime_service|ipc_protocol)_test$' --output-on-failure
ctest --test-dir build/ubsan -R '^(native_agent_integration|runtime_service|ipc_protocol)_test$' --output-on-failure
setarch x86_64 -R build/tsan/tests/native_agent_integration_test
cmake --build build/native-release --target mirage-format-check
```

## 真实原生窗口与 MiniMax

在私有 Xvfb、DBus、Secret Service 和托盘中启动真实原生窗口，沿真实设置页以已授权本地
MiniMax Key 保存预设；不接触用户原会话，Key 不进入参数、证据或仓库。同一会话在输入栏
开启思考后得到 THINKING_OK，再关闭得到 OFF_OK，两轮均成功并收到 1 / 2 次非空正文预览。
额外 adaptive harness 真正调用 wait：2 model steps / 1 tool call，2 次答案预览，返回 TOOL_OK。
真实供应商返回成功证明当前工具回填互操作；签名字段完整性和顺序由确定性协议夹具断言。

[开关与预览结果](../../.impeccable/review/session-thinking-20261007/thinking-results.json)、
[工具循环结果](../../.impeccable/review/session-thinking-20261007/tool-replay-results.json)、
[开启](../../.impeccable/review/session-thinking-20261007/thinking-enabled.png)、
[关闭](../../.impeccable/review/session-thinking-20261007/thinking-disabled.png)、
[开启回复](../../.impeccable/review/session-thinking-20261007/thinking-completed.png)、
[关闭回复](../../.impeccable/review/session-thinking-20261007/thinking-off-completed.png)。

```bash
python3 -B tests/manual/native_conversation_acceptance.py \
  --build build/native-release --provider /home/linductor/mira/docs/model_provider/minimax \
  --model-settings --preset-minimax --thinking-only \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb \
  --output .impeccable/review/session-thinking-20261007
```

首次夹具遇到首帧未绘制和旧 sni-watcher.json 导致虚假就绪，修正首帧有界等待及清除上轮
夹具元数据后复跑通过；没有把失败尝试计为产品通过。保留可重复的 opt-in 付费入口。

## 产品重新启动与限制

实际用户实例核对时已无 Mirage 进程，磁盘已变为 1 个非空历史（与早前 17 空记录诊断时刻不同）。
从正式应用列表 desktop entry 启动遇到锁屏环境：GNOME ScreenSaver.GetActive=true，
StatusNotifierWatcher 不存在，Dock/AppIndicators 均处于 INACTIVE，程序按准入规则拒绝创建窗口。
本机已安装的 Ubuntu AppIndicators 原为未启用，现已启用；其激活仍需解锁。
本机重新启动验收保持未完成，负责人维护者/Codex，补跑条件为解锁后 watcher 在线，再由
GIO 入口启动、核对非空历史 ID、空主会话不保存、会话 admission 与独立窗口。
[当前结果](../../.impeccable/review/session-thinking-20261007/restart-results.json)只记录元数据；
没有删除或读取非空历史正文/Key。私有桌面的上述原生验证不能代替这项验收。

本轮未交付 legacy enabled/budget_tokens、between_tools、服务端工具、用户会话间 thinking/signature
历史保存或思考正文 UI；当前签名回填范围为同一轮 harness 的工具循环。产品图片附件仍不在范围内。
Windows、Wayland、其他厂商真实凭据与当前 kairo 主干未运行；负责人维护者取得相应构建/桌面/
凭据环境后补验，不能用 Linux/MiniMax 结果替代。整体 M6 保持 In Progress。
