# 会话活动排序验证（2026-10-11）

> 状态：Completed（Linux 本地验证）
> 工作项：M6-37
> 决策：[DEC-079](../decisions/DEC-079-session-activity-order.md)
> 负责人：Mirage 维护者

## 行为

左侧会话列表使用 ChatModel 最多 24 项的历史排序投影，最近更新在最上方。发送、
新回复/失败、流式内容与工具过程计入活动；选择会话、草稿、历史查询不计。底层会话
数组不移动，因此当前选择、指针、草稿、阅读位置与事件映射仍保持稳定。
恢复使用现有轮次 recorded_at_ms，终态结算会更新时间并随原有历史持久化；没有
新 schema/wire 字段、第三方改动或并发任务。查询历史明确传入记录时间且过程回填
不触发活动；重复/迟到事件不让会话乱跳。相同时间采用活动顺序与稳定远端 ID。

## 验证

模型覆盖旧会话更新上移、选择与草稿保留、重复 pending/历史、预览重复和终态迟到、
旧轮次事件、工具过程、清空、历史乱序恢复、缺时间平局及历史时间超前/时钟回拨。
真实 EUI 原生渲染检查侧栏两行 y 坐标在更新后互换，并检查当前选择与草稿不变，
包括浅色正常与深色小窗口。Runtime 门控轮次检查结算时间晚于接纳，重复历史读不改时间。

Debug 定向 6/6、Native Release 定向 8/8；两项业务加状态 setup/cleanup 的
ASAN、UBSAN、TSAN 定向各 4/4。模型 412 checks / 0 failures；最终原生渲染 22,978 checks / 0 failures，
浅色正常、深色小窗口截图检查完成，实际旧会话上移且草稿仍保留。最终隔离夹具
另复跑 3/3（含 setup/cleanup）。未重新宣称全量 69/75 已按本次变化复跑。

DEB 已更新到 out/packages/mirage_0.1.0_amd64.deb，包内四个可执行文件与最终 Release
SHA-256 一致。没有安装系统、修改依赖 pin、提交或推送。

首次原生测试在深色第二轮重复发送同样“新回复”，正确没有活动更新，却仍断言上移。
该夹具已修正为第二次不同回复；不将重复规范响应作为新活动，也不为通过测试改排序策略。
原有运行日志位于 `/tmp/mirage-dec079-*`；不读取用户会话内容或连接在线推理。
Windows 真机界面未执行，本次 Linux 验证不替代平台体验验收；负责人 Mirage 维护者，
在 Windows native preset 可用时补同样排序/选择/恢复检查。

直接截图执行首次遗漏了 CTest 的 XDG 隔离，写入了用户 preferences.json。发现后
按本轮开始前 SHA-256 验证恢复到完全相同的原字节；模型配置和会话历史的五份文件
未变。随后测试程序在 main 入口自建临时 CONFIG/STATE/DATA 目录，直接运行也不
再依赖 CTest 的隔离。原值恢复与最终六份文件 SHA 检查记录在机器摘要，不将此次
偏好写入隐瞒为从未发生。实际产品代码与权限偏好未受影响。

复现命令：

```sh
ctest --preset debug -j 2 --output-on-failure -R 'native_chat_model_test|runtime_service_test|ipc_protocol_golden_test|persistence_test'
ctest --preset native-release -j 2 --output-on-failure -R 'native_chat_model_test|native_conversation_view_test|runtime_service_test|native_agent_integration_test|ipc_protocol_golden_test|persistence_test'
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --preset asan -j 2 --output-on-failure -R '^(native_chat_model_test|runtime_service_test)$'
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset ubsan -j 2 --output-on-failure -R '^(native_chat_model_test|runtime_service_test)$'
setarch x86_64 -R ctest --preset tsan -j 2 --output-on-failure -R '^(native_chat_model_test|runtime_service_test)$'
# 在 build/native-release/apps/native 中运行，程序自行隔离 XDG 配置：
../../tests/native_conversation_view_test /tmp/mirage-dec079-ui-final
```

机器摘要见 [summary.json](data/session-activity-order-20261011/summary.json)。
