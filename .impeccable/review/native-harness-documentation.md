# 原生模型设置与通用 harness 文档合并

日期：2026-10-04。范围：M6-03、DEC-034，`apps/native / mirage-native`。
本轮是既有视觉系统的普通扩展，按已授权的事实合并同步文档，不重选视觉方向。

## 读取与比对依据

读取 `PRODUCT.md`、`apps/native/DESIGN.md` 与其 design.json；核对
`app.cpp`、`chat_model.cpp`、`runtime_bridge.cpp`、
`docs/design/native-agent-frontend.md`、DEC-034。文档流程来自 Impeccable
`reference/document.md` 及 `reference/new-work.md` 的普通扩展/文档 handoff 约定。

已打开并检查以下既有 Linux 截图，不重新构建或渲染：

- `native-harness-model-final-light.png`、`native-harness-model-final-dark.png`：
  中性明暗 Palette、模型分类、四个字段、协议选择及固定页底操作。
- `native-harness-model-final-min-light.png`、`native-harness-model-final-min-dark.png`：
  最小窗口沿用阅读列与侧栏结构，表单独立滚动，页底操作可见。
- `native-harness-model-min-scrolled.png`：滚动到凭据变量与协议区，服务环境凭据说明可见。

- `native-harness-chat-reply-light.png`、`native-harness-chat-reply-dark.png`、
  `native-harness-chat-reply-min-dark.png`：
  实际 SiliconFlow 会话的“你能做什么”回复、已发送/已回复角色、服务确认的模型；
  回复开头紧随角色，内部段落空行保留，最小窗口正文独立滚动。

浅色回复首次读取误为外观页，已要求重采并重新打开最终文件；最终三张回复图均为上述真实会话。

## 合并内容

- `PRODUCT.md`：修正原生仍仅本地 preview、未接入 session/model 的旧事实；
  记录独立服务 IPC、模型配置持久化、单一 Agent 会话及真实停止/终态，
  服务历史与 UI 草稿/主题/侧栏偏好的所有权边界。
- `apps/native/DESIGN.md`：保留现有 North Star、Palette、字体候选、图标、
  圆角、阅读列及品牌资源；补充 16px 模型字段、44px 输入、92px 字段行、
  滚动表单与固定页底几何，以及 focus/保存/忙拒绝/断开状态。
- `apps/native/.impeccable/design.json`：保留既有 primitive metadata、
  布局与资源来源扩展；同步实际 Agent/服务消息示意，增加模型输入与应用按钮
  的独立面板示意和模型/会话状态扩展。示意不构成原生第二套实现。

所有模型展示以服务确认的 live_model 为准；未保存表单保持独立草稿。
回复展示仅移除首尾 CR/LF 空行，保留内部空行、缩进与服务原始历史。
通用 harness 复用 MiraRuntime / ModelGateway / builtin wait，
临时 Adapter 由 MIRA-20261004-001 追踪，不直接调用设备 AgentLoop；
当前无自动截图、桌面工具、RPA 或 workflow。

## 保留与限制

仓库根 `DESIGN.md`、legacy CEF 与全部 asset 文件未修改；既有品牌图来源和
sidecar provenance 记录保留。未执行 build/test/render/commit；此文档核对
以源码、现有截图和已记录设计决策为依据，不宣称重新运行供应商测试。
DEC-034 记录 SiliconFlow 真实文字/工具验收，MiniMax 受 pinned TLS SNI
缺口 MIRA-20261004-002 影响。仅 Linux 在当前验证范围；Windows、流式回复、
统一入口/托盘和活动退出确认继续保留为 M6-04/后续未验收项。

既有 drift 只记录，不自行修复：系统中文字体全部缺失时仍回退装饰字体。
外观偏好声明已按最终源文件同步为“即时应用 · 本次运行内保留外观偏好”，
仅进程内记忆语义未变。

本记录是文档事实核对，不替代 finish reviewer 的视觉 verdict。
