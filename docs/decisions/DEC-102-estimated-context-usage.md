# DEC-102：缺少供应商用量时显示请求输入估算

> 状态：Accepted
> 日期：2026-10-18
> 负责人：Mirage 维护者
> 冻结里程碑：M6
> 调整：DEC-036 的“仅供应商用量、缺失显示未知”条款；其余语义继续生效

## 背景与问题

维护者要求参考本机 `~/ZCode`，在模型不返回上下文用量时仍能看到占用。
ZCode 29628c9a 的 `core/src/runtime/methods/context-usage.ts` 提供独立的本地估算快照，
覆盖系统提示词、消息和工具，并标记 estimated；`bootstrap/src/zcode-protocol/session-mapper.ts`
的界面投影主要读取会话状态及持久化消息用量，不能将其描述为 UI 总会自动估算。

已核对 pinned Mira 的 `model_budget.hpp`、`context_manager.hpp` 与 context-memory
公开文档。`mira::estimate_input_tokens(ModelRequest)` 已用于本项目请求预算，
此能力可直接承载显示降级，无需自研 tokenizer 或修改依赖。

## 决策

- 最近成功请求有有效 Exact / ProviderReported input_tokens 时优先使用，包括真实零值。
  缺失、不完整、Estimated 或无效的供应商计数使用实际发送请求的本地输入估算。
- 在现有 ObservedProvider / ConversationCapture 内记录最近一次成功模型调用的请求估算，
  由现有结算路径投影。工具循环取最终回答请求，不累计调用、不计入输出 Token，
  不从 UI 最近40轮消息、草稿或被裁剪的过程文本重建请求。
- `context_usage` 新增可选布尔 `estimated`；true 表示本地保守估算，缺失/false
  保持既有供应商报告语义。标记与 input/window/model 一起经事件、历史和逐会话文件保存。
  旧消息及文件缺标记仍能读取，真实用量的旧 wire 字符串不变。
- UI 在估算数字/百分比前显示“约”，来源显示“上次请求 · 本地估算”；窗口预算未知时
  仍显示估算输入 Token，分母与百分比保持未知。不将默认窗口当作供应商容量。
- 未发送过请求、旧文件无用量且无可还原的完整请求时仍显示未知；新请求成功后更新。
  pending/失败/取消保留上次值，编辑替换清空，迟到响应保护沿 DEC-036。
- 不改变 Mira 的 ModelResponse.usage、预算对账或真实用量锚点；估算仅用于产品展示。
  不新增异步任务、锁、队列或网络调用，沿现有 Executor worker 与会话存储 owner。

## 备选方案与影响

拒绝把字符比例或 UI 历史片段伪装为精确 Token。直接把 ModelResponse.usage 改为
Estimated 会影响通用预算语义，因此保留独立展示字段。估算采用 pinned 估算器既有口径，工具 schema、图片和供应商
协议额外开销可能存在偏差，不构成精确计费、实时占用或上下文准入保证。

## 验证方式

覆盖真实优先/真实零值、缺失及低质量用量降级、最终工具请求估算、IPC/文件标记兼容与
错误类型、重启保留来源、未知窗口、明暗/最小窗口文案及既有迟到事件测试。
Linux Release 与 ASAN/UBSAN/TSAN 定向验证；Windows 未验证部分另记补跑条件。

## 关联文档和工作项

- [DEC-036](DEC-036-context-usage-presentation.md)
- [M6-45](../plans/m6-native-frontend.md)
- [IPC 契约](../design/mirage-ipc-protocol-v1.md)
