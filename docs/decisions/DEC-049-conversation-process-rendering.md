# DEC-049：会话过程投影与折叠渲染

状态：Accepted。日期：2026-10-07。工作项：M6-31。

维护者要求参考本机 ZCode（29628c9）实现模型思考与工具过程。其 ReasoningRowView 默认
折叠思考，ToolLayout 默认折叠工具参数/结果，并用稳定行 ID 保留交互状态。
Mirage 复用 pinned IModelProvider 的规范 ThinkingPart/MessageOutput/ToolCallOutput
与 BuiltinToolHandler 的真实结果，在 integration 边界只投影显示数据，不复制推理/调度。
过程投影放在独立无平台类型的 Mirage::conversation 契约，IPC 与持久化共用校验。
每轮最多96段、每字段16KiB、内容总量64KiB；超限明确标记 truncated。
完整快照经既有 Executor Topic 广播；历史为丢事件的恢复面。终态/关闭/取消拒绝迟到更新。

思考、工具摘要为28px行，默认折叠；展开显示有界、换行的普通文本，工具参数/结果明确分区。
正文保留现有 Markdown、选择/复制与跟随。redacted 思考与签名不投影。
展开状态仅随保留的消息和段存在，删除历史时清理，不建立无界全局 Map。

公开预览接口仅有正文（ModelPreviewSink），思考在完整模型步骤返回后出现；不承诺逐 token
思考预览。现有工具仅 wait，不声称接入桌面工具。Windows/Wayland 验证需对应环境。

历史响应按实际JSON编码字节预算保留最新轮并维持顺序，不超过1MiB帧；
旧轮省略以history.truncated显式标记，最新单轮也无法容纳时明确拒绝。
会话文件预算由512KiB扩大至固定8MiB，JSON解码使用同一上限；仍严格限制每会话/轮/字段，
不无限增大或吞掉存储失败。容量验收包含大于默认JSON4MiB文档限制的真实历史恢复。
