# 原生上下文占用验收（M6-06）

> 状态：Completed（Linux首步）
> 日期：2026-10-04
> 负责人：Mirage维护者
> 决策：[DEC-036](../decisions/DEC-036-context-usage-presentation.md)

## 环境与来源

Linux GNOME，GLFW/OpenGL XWayland，2×缩放；逻辑1180×800和860×620，明暗主题。
ZCode参考源码29628c9acdb81b703bbd4080c207a0e7ce5e276e的contextUsage.tsx与
ai-elements/context.tsx：双圆环、紧凑Token摘要、比例进度条。运行中ZCode截图权限限制
沿用DEC-035，不宣称像素级对照。EUI/Mira/Executor pin未变化，无third_party源码修改。

## 数据与实现

Adapter消费Mira公开ModelResponse.usage，不新增估算或调度设施；现有结算/广播/历史
链路携带最后一次成功模型输入用量。model.context_window_tokens为显式预算，默认未知。
UI圆环20px，24视框、r10/stroke4，SVG路径替代dash实现；点击36×32按钮展开320×224
详情，数字14/16px、6px进度条；真实零/未知/超额均有明确语义，只有图形填充截断至100%。
草稿不影响上次请求用量；迟到的旧序列不覆盖新用量。失败/取消保留上次成功请求，
成功但缺用量更新为未知；重启旧历史暂不保存用量，不伪造Token精度。

## 已执行技术验证

- Debug构建原生UI、service、native_agent_integration_test、native_chat_model_test和IPC测试。
- Debug CTest针对4/4通过：native_chat_model_test、native_agent_integration_test、
  ipc_protocol_test、ipc_protocol_golden_test；模型180 checks、harness64 checks，含用量与配置检查。
- Release原生UI构建与模型CTest1/1通过。
- ASAN（detect_leaks=1）、UBSAN各新模型/harness CTest2/2通过。
- TSAN经setarch x86_64 -R运行两测试，180+64 checks无race诊断；故障注入的
  model driver failed日志为既有scripted provider异常场景，不是未处理测试失败。
- 协议覆盖可选字段/旧消息、负Token拒绝；配置整数范围/类型拒绝；profile预算实际映射。
  工具循环验证2468是最终调用输入，而非1234+2468的累计或输出Token。
- 真实隔离SiliconFlow Qwen/Qwen3.5-4B服务/UI发送成功：input_tokens=391，
  配置测试窗口预算128000，UI显示0.3%；预算不是供应商自动发现容量。
  事实：[JSON](../../.impeccable/review/native-context-live-usage.json)。凭据只从本机Mira
  配置读取至子进程环境，未写入证据；用户原配置未修改。隔离service的旧会话恢复为历史，
  新UI创建活动会话测试，符合已有M6-03边界。

## 界面验收与待验

五张比例/未知、明暗/尺寸截图均经独立复核，disposition=ship，仅上下文扩展范围：
[verdict](../../.impeccable/review/native-context-finish-verdict.md)。
额外实测设置中abc非法输入显示范围错误且保留草稿，恢复128000并保存后model.get
确认值正确；[完整截图/交互索引](../../.impeccable/review/native-context-evidence.json)。
UI点击入口和Escape关闭、主题和尺寸变化均实机执行。native DESIGN/sidecar、PRODUCT、协议契约与计划已同步；
[独立文档核对](../../.impeccable/review/native-context-documentation.md)关闭review的文档前置项。
Windows未执行（无目标环境）；负责人Mirage维护者，条件为Windows EUI dev构建及GUI
真机验证比例/弹层/保存。实际IME无新增验证；本轮无新的异步或shutdown任务路径。
未创建commit/MR，保留既有脏工作树。


## 2026-10-18：M6-43 会话自身保存用量快照

本轮替代上文“重启旧历史暂不保存用量”的限制；历史验收记录保留。
依据更新后的 [DEC-036](../decisions/DEC-036-context-usage-presentation.md)。
工作树基于 `0cd719c`，分支 `fix/session-context-usage`；Ubuntu 24.04.5 x86_64、
GCC 13.3.0，Mira a29374aa、Mirador cb45a102、EUI 63117f77，依赖 pin 未变。

会话拥有独立的 `context_usage` / `context_usage_sequence`，成功结算后经现有保存路径
写入 `session-state.json`，服务启动恢复。历史响应在顶层传回快照，最近40轮不包含成功
请求时仍能读取。失败/取消和普通 pending 保留上次成功值，成功缺用量清空旧值；编辑
替换清空并推进序列，接纳回复尚无序列时 UI 也阻止旧值回流。同轮 pending 的迟到历史
不能清除已结算事件的新用量。窗口预算与模型 ID 固定为该次请求的配置。

复用 Mira ModelResponse.usage 的 Exact / ProviderReported 字段；MiraRuntime 的
SessionSnapshot 不提供这类展示元数据，故由 Mirage 产品会话保存，不重建 Token 估算
或 Agent 状态机。不新增异步任务、线程、队列或锁；快照受现有 dialogs.mutex 保护，
存盘沿原有持久化 owner、字节上限及原子替换路径。

### 实际验证

```bash
cmake --build --preset native-release --target persistence_test ipc_protocol_test ipc_protocol_golden_test native_chat_model_test native_agent_integration_test mirage-native mirage-service -j 4
cmake --build --preset native-release --target runtime_service_test session_client_test -j 2
ctest --preset native-release -R '^(persistence_test|ipc_protocol_test|ipc_protocol_golden_test|native_chat_model_test|native_agent_integration_test|runtime_service_test|session_client_test)$'
cmake --build --preset asan --target persistence_test ipc_protocol_test ipc_protocol_golden_test native_chat_model_test native_agent_integration_test -j 3
ASAN_OPTIONS=detect_leaks=1 ctest --preset asan -R '^(persistence_test|ipc_protocol_test|ipc_protocol_golden_test|native_chat_model_test|native_agent_integration_test)$'
cmake --build --preset ubsan --target persistence_test ipc_protocol_test ipc_protocol_golden_test native_chat_model_test native_agent_integration_test -j 3
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset ubsan -R '^(persistence_test|ipc_protocol_test|ipc_protocol_golden_test|native_chat_model_test|native_agent_integration_test)$'
cmake --build --preset tsan --target persistence_test native_chat_model_test native_agent_integration_test -j 2
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan -R '^(persistence_test|native_chat_model_test|native_agent_integration_test)$'
```

- Native Release：前端、service 及上述测试构建通过；定向7项与隔离fixture合计9/9通过。
- ASAN（含 leak）与 UBSAN：各定向5项与fixture合计7/7通过，无 sanitizer 报告。
- TSAN（halt_on_error，关闭 ASLR）：定向3项与fixture合计5/5通过，无 race 报告。
- 首次 Release 编译发现新增测试变量触发 `-Wshadow`，已更名修复并重跑；最后的
  接纳回复序列保护修改后，Release、ASAN、UBSAN 的模型目标均已重建并完成上述复跑。
- 服务夹具执行真实本地 IPC、模型模拟返回、原子存盘与两次 RuntimeService 重启：
  仅返回失败轮次的 `limit=1` 历史仍携带早先用量；另一空会话不串值；重启保留
  input/window/model/sequence；后续成功缺用量及再次重启保持未知。
- 模型测试覆盖会话切换、同轮结算后迟到 pending 历史、旧序列、无序列编辑接纳与显式
  未知的保护。存储与协议覆盖缺字段兼容、零用量/未知预算、非法范围/类型及 golden v22。
- 既有失败注入主动占用 session-state 临时文件，预期产生一次 persist failed 日志；
  测试仍以0 failures结算，不能将该日志误判为未处理异常。
- `git diff --check` 通过；公开头未引入第三方类型；第三方源码、pin 和锁文件未修改。

### 验证范围与补跑

本轮不改变圆环/详情的视觉布局，未新增实机截图、真实供应商请求或 Windows 验证；
自动化证明 Linux 数据和恢复路径，原圆环视觉证据沿用上文。负责人为 Mirage 维护者：
需要在 native 构建的实机中发送请求、关闭并重开服务、切换会话并检查圆环详情；Windows
需在目标机先构建再执行相同流程。旧文件未保存的数据无法补算；默认显示未知，下一次
成功请求返回用量后开始保存。未安装到用户系统、未创建 commit/MR。


## 2026-10-18：更新并启动 native-release

维护者要求“更新native-release”。执行完整产品目标，而非只重建前端：

```bash
cmake --build --preset native-release --target mirage-native-product -j 4
./build/native-release/apps/mirage start --shell /home/linductor/mirage/build/native-release/apps/native/mirage-native --tray /home/linductor/mirage/build/native-release/apps/tray/mirage-tray
./build/native-release/apps/mirage service status
```

构建与启动均成功，Runtime host status=running，托盘 PID945171、前端 PID945190。
运行映像经 `/proc/<pid>/exe` 校验与本次构建路径/文件一致；前端 SHA-256：
`23ec41baf0eefd211577346e35b0188b18afdb64ad9e28c32fe7063f7b8f7994`。
既有桌面入口已经指向 native-release，未改写入口或安装系统 DEB；本轮启动前无运行中
Mirage，因此无需终止用户任务。未发送模型任务、未修改模型配置。启动及映像证据位于
`/tmp/mirage-context-native-release-start.log` 和
`/tmp/mirage-context-native-release-running.json`。
本轮仅验证启动与窗口映射；带真实新用量的重启恢复手动验收仍待维护者操作。
