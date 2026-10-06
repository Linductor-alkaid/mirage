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
