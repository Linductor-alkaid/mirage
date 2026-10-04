# 原生会话页 ZCode 对齐验收

- 日期：2026-10-04；负责人：Mirage 维护者 / Agent 执行。
- 工作项：M6-05；决策：[DEC-035](../decisions/DEC-035-zcode-conversation-presentation.md)。
- 平台：Linux GNOME Wayland / XWayland，GLFW / OpenGL，2×显示缩放。
- 状态：Linux本轮完成；下列平台/交互限制保持待验收。

## 参考与范围

[ZCode main](https://github.com/zai-org/ZCode/tree/29628c9acdb81b703bbd4080c207a0e7ce5e276e)，
本地只读clone及远程main同为29628c9acdb81b703bbd4080c207a0e7ce5e276e。
依据ConversationTimeline/ConversationRowView/ConversationComposer/ChatPromptEditor与
composer toolbar：居中空态、右对齐轻用户气泡、无卡片Agent回复、Markdown、
底部增长输入、加号/模式/模型/上下文/发送工具条。
保留Mira身份、侧栏和设置。正文/输入16px，适配此前维护者提出的可读性要求。

运行中的ZCode使用Wayland；GNOME ScreenshotWindow返回AccessDenied。
未绕过桌面截图授权；本轮为源码与布局对齐，不宣称运行中ZCode像素级复刻。
设置、托盘/退出、RPA、屏幕观察、桌面工具不在本轮范围。

## 已实现交互与边界

- Enter发送；Shift+Enter换行，Ctrl+Enter保留发送兼容；IME composition事件阻止提前提交。
- 普通对话/Agent切换对应实际agent布尔值；运行中不切换模式。
- 模型入口显示当前应用模型，并打开真实模型设置。
- 消息整条复制；消息引用以UI内文字快照进入现有harness普通输入。
- 引用数量pill可打开有界滚动预览，显示真实引用原文、角色和消息ID，并逐条移除。
- 引用最多4条、总8KiB；引用编码+草稿受16KiB提交上限，拒绝不清草稿。
- ACK按引用实例ID清除已提交项；新的草稿/引用，包括同消息重新引用，保持不变。
- 上下文展示已载入历史轮数、引用数、文字bytes；Token无服务事实源，不显示虚假占用。
- 沿用运行中停止按钮、服务终态和明确失败。没有增加异步/并发路径。

EUI公共Markdown组件支持标题、列表、粗体、代码、表格等。连续CJK额外gap采用
公开DSL单一Adapter修正，详情[EUI-20261004-004](../dependency_feedback/eui-ledger.md)。
上游保守换行/高度预算保留；Markdown细粒度框选及链接点击仍缺公开回调。
引用作为普通用户文字发送，服务历史中会包含展开后的引用；未新增结构化附件wire契约。

## 实际验证

- Debug / Release：mirage-native、native_chat_model_test构建成功。
- Debug targeted CTest：native_chat_model_test、ipc_protocol_golden_test、native_agent_integration_test，3/3。
- Release targeted CTest：native_chat_model_test，1/1。
- ASAN / UBSAN：native_chat_model_test各1/1，166 checks / 0 failures。
- ASAN GUI：detect_leaks=1，真实Markdown、引用预览/滚动后正常关闭，exit0，无诊断。
- UI主线程新增状态，无新增跨线程通信/生命周期；既有bridge及harness不变。本轮未重复TSAN。
- 本机已授权的SiliconFlow Qwen/Qwen3.5-4B，经真实服务请求标题、加粗列表和三行C++代码；
  回复完成，真实剪贴板读取与原文一致。密钥仅服务环境，未写入截图/文档/仓库。

正常1180×800、最小860×620明暗主题、空态、多行草稿、Markdown、引用、
上下文、12行输入截图与交互结果在`.impeccable/review/native-zcode-*`。
逐张核对最终11张截图：没有空白/黑屏或文件名对应错误；回复为历史末段视图。
[截图/源码哈希及测试结果](../../.impeccable/review/native-zcode-evidence.json)、
[交互记录](../../.impeccable/review/native-zcode-interactions.json)。

独立复核初次disposition=fix，要求引用可检查及清除默认Button文案；单批修正后
[评分](../../.impeccable/review/native-zcode-final-verdict.md)两项均resolved，disposition=ship，
仅本修正列表范围。完整初次评审见[review](../../.impeccable/review/native-zcode-finish-verdict.md)。
native DESIGN/JSON/PRODUCT由独立documenter从代码提取同步，
[提取记录](../../.impeccable/review/native-zcode-documentation.md)。未创建commit/MR。

## 未验证事项

Windows与真实中文IME候选确认尚未本轮验证。负责人为维护者；补跑条件为Windows
原生构建/运行环境和已启用中文IME的交互桌面。不得将合成XTest中文粘贴当成IME验收。
Wayland运行中ZCode像素对照需桌面授权允许截图；无授权时保持源码对照结论。

复现交互：启动独立服务和Release前端；切换到有回复的会话，复制并读取剪贴板，
点击引用动作或加号→引用上一条回复，点击计数pill检查原文/来源并删除；
输入12行观察增高与滚动；Shift+Enter换行、切换会话返回核对草稿；设置→外观切换明暗，
窗口缩至860×620检查底部操作。实际模型配置沿用上一轮隔离测试环境，未改用户默认配置。
