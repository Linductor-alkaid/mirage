---
name: Mirage 原生 Agent 对话页
description: EUI-NEO 原生 ZCode 会话、服务商配置与有界文本附件的中性明暗设计系统
colors:
  light-background: '#f8f8f8'
  light-sidebar: '#f0f0f0'
  light-surface: '#ffffff'
  light-hover: '#e8e8e8'
  light-selected: '#e2e2e2'
  light-text: '#202020'
  light-muted: '#6d6d6d'
  light-border: '#dfdfdf'
  light-action: '#222222'
  light-inverse: '#ffffff'
  dark-background: '#161616'
  dark-sidebar: '#1d1d1d'
  dark-surface: '#222222'
  dark-hover: '#2c2c2c'
  dark-selected: '#333333'
  dark-text: '#eeeeee'
  dark-muted: '#a1a1a1'
  dark-border: '#353535'
  dark-action: '#eeeeee'
  dark-inverse: '#161616'
  light-user: '#f0f0f0'
  dark-user: '#222222'
  dark-composer: '#2b2b2b'
  light-markdown-accent: '#1a70b8'
  dark-markdown-accent: '#80beff'
  light-code-background: '#eeeeee'
  dark-code-background: '#222222'
  light-quote-background: '#f0f0f0'
  dark-quote-background: '#202020'
  light-context-ring: '#737373'
  dark-context-ring: '#b1b1b1'
typography:
  headline:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 26px
    fontWeight: 500
    lineHeight: 1.5
  brand:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 18px
    fontWeight: 600
    lineHeight: 1.5
  title:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 21px
    fontWeight: 600
    lineHeight: 1.5
  message:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
    lineHeight: 22px
  body:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 17px
    fontWeight: 400
  navigation:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
    lineHeight: 1.5
  hint:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 13px
    fontWeight: 400
    lineHeight: 1.5
  input:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
  button-label:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 15px
    fontWeight: 400
  settings-heading:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 24px
    fontWeight: 600
    lineHeight: 1.5
  provider-selector:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
  model-heading:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 24px
    fontWeight: 600
    lineHeight: 1.5
  model-field:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    fontWeight: 400
  markdown-h1:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 18px
  markdown-h2:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 16px
  markdown-h3:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 15px
  markdown-code:
    fontSize: 13px
  reference-body:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 14px
    lineHeight: 22px
  composer-label:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 13px
  composer-notice:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 12px
    lineHeight: 18px
  composer-shortcut:
    fontFamily: Noto Sans SC, sans-serif
    fontSize: 11px
    lineHeight: 16.5px
rounded:
  input: 4px
  control: 7px
  message: 10px
  panel: 12px
  provider-chooser: 10px
  composer: 16px
  reference: 14px
  markdown: 8px
  message-action: 5px
spacing:
  session-gap: 4px
  text-gap: 8px
  input-inset: 12px
  sidebar-inset: 20px
  dialog-inset: 24px
  user-horizontal: 12px
  user-vertical: 8px
  thread-gap: 14px
  markdown-block-gap: 8px
  reference-gap: 16px
components:
  button-ghost:
    backgroundColor: transparent
    textColor: '{colors.light-text}'
    rounded: '{rounded.control}'
    size: 36px
  button-ghost-hover:
    backgroundColor: '{colors.light-hover}'
  button-primary:
    backgroundColor: '{colors.light-action}'
    textColor: '{colors.light-inverse}'
    rounded: '{rounded.control}'
    size: 36px
  button-primary-disabled:
    backgroundColor: '{colors.light-hover}'
    textColor: '{colors.light-muted}'
  button-primary-hover:
    backgroundColor: '{colors.light-muted}'
  button-pressed:
    backgroundColor: '{colors.light-selected}'
  session-row:
    backgroundColor: transparent
    textColor: '{colors.light-text}'
    rounded: '{rounded.control}'
    height: 48px
  session-row-selected:
    backgroundColor: '{colors.light-selected}'
  composer:
    backgroundColor: '{colors.light-surface}'
    rounded: '{rounded.composer}'
  input:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.input}'
    padding: 12px
    typography: '{typography.input}'
  dialog:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.panel}'
    width: 460px
    height: 252px
  dialog-delete:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.panel}'
    width: 460px
    height: 316px
  theme-option:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    typography: '{typography.navigation}'
    rounded: '{rounded.control}'
    width: 116px
    height: 40px
  theme-option-selected:
    backgroundColor: '{colors.light-selected}'
  theme-panel:
    backgroundColor: '{colors.light-surface}'
    rounded: '{rounded.panel}'
    height: 100px
  theme-panel-narrow:
    height: 126px
  provider-selector:
    backgroundColor: transparent
    textColor: '{colors.light-text}'
    rounded: '{rounded.control}'
    height: 32px
    typography: '{typography.provider-selector}'
  model-input:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.control}'
    height: 32px
    padding: 12px
    typography: '{typography.model-field}'
  model-apply:
    backgroundColor: '{colors.light-action}'
    textColor: '{colors.light-inverse}'
    rounded: '{rounded.control}'
    width: 80px
    height: 32px
    typography: '{typography.button-label}'
  user-bubble:
    backgroundColor: '{colors.light-user}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.message}'
    padding: 8px 12px
    typography: '{typography.message}'
  agent-reply:
    backgroundColor: transparent
    textColor: '{colors.light-text}'
    typography: '{typography.message}'
  reference-pill:
    backgroundColor: '{colors.light-hover}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.reference}'
    width: 176px
    height: 28px
  context-control:
    backgroundColor: transparent
    textColor: '{colors.light-context-ring}'
    rounded: '{rounded.control}'
    width: 36px
    height: 32px
  context-popover:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.panel}'
    width: 320px
    height: 224px
    typography: '{typography.label}'
  reference-popover:
    backgroundColor: '{colors.light-surface}'
    textColor: '{colors.light-text}'
    rounded: '{rounded.panel}'
    width: 320px
---

# Design System: Mirage 原生 Agent 对话页

> 范围：`apps/native` / `mirage-native`，M6-01/02/03/05/06/07/08/09、DEC-033/034/035/036/037/038。事实源：
> `app.cpp` 的 `Palette`、`text()`、`button_style()`、`conversation_page()` 与 `compose_page()`；
> `chat_model.hpp/.cpp` 的附件/引用预算、提交编码、ACK 与成功轮次用量；`attachment.cpp` 与 `runtime_bridge.cpp` 的显式文件选择和 Executor 读取边界；`context_usage.hpp` 的比例、数字与圆环；`markdown_adapter.hpp` 的公共 DSL 兼容边界；
> `secret_input.hpp` / `secret_edit.hpp` 的遮蔽输入与编辑边界、`conversation_preview.hpp` 的有界删除摘要；
> `window_controls.cpp` 的窗口尺寸与初始 DPI 换算。产品依据为仓库根目录 `PRODUCT.md`，
> 方向契约为 `../../docs/design/native-agent-frontend.md`。
> frontmatter 同时记录明暗两态；组件条目使用默认浅色，深色按同名 Palette 角色替换。
> `.impeccable/design.json` 承载布局、原生边界与组件示意，不是另一个原生实现。

## Overview

**Creative North Star: "桌面 Agent 的对话工作台"**

中性灰底、窄会话栏和居中的阅读列构成原生桌面对话工作台。细分隔线与轻微色阶承担结构，实心发送控件集中表达主操作。视觉参考用户指定的 ZCode Zai Light/Dark；使用的是 Mirage 自己的 EUI DSL 页面，未复制参考项目的业务实现。

当前表面经独立 Runtime Service 展示真实 Agent 会话，并在设置页配置模型。本地草稿在首次提交获接纳并产生实际消息后进入历史，删除经服务确认后移除。会话标题、草稿、轮次状态与服务提示保持既有信息层级；发送、运行、回复、停止或失败来自服务回执与事件。本原生系统是 DEC-037 旧 TS/CEF 前端退役后的现行视觉契约，仓库根目录同步此记录；原 Web Mission Console 的炭蓝/奶油/琥珀、仪表 caps 和灯阵仅保留为历史设计背景，既有版本可从 Git 历史查看。

**Key Characteristics:**
- 明暗两态共享中性灰角色；默认浅色。
- 随应用交付的 Noto Sans SC 中文无衬线文字与 Font Awesome Solid 操作图标；品牌继承 Mira 原始角色图。
- 可调宽并收起的侧栏、右对齐用户气泡、无卡片 Markdown 回复与随文字增高的 composer。
- 设置入口集中外观与模型；主题即时应用且仅在当前运行中保留，模型配置由服务保存并确认，API Key 直接输入并默认遮蔽。
- 平面容器、细描边、有限圆角。
- 清晰展示实际轮次状态、已应用模型、附件/引用来源与服务连接结果；上下文比例取最近成功请求的输入 Token 与显式窗口预算，未知时明确说明。

## Colors

两套控件 Palette 都使用中性灰，颜色原始值以 frontmatter 为准。Mira 角色图保留原始红发蓝眼色彩，这些图片颜色不扩展为控件强调色 token。

### Primary

- **实心操作墨色 / 浅色操作字**：`light-action` 与 `dark-action` 用于发送和对话框确认，搭配对应 `inverse`；深色主题下操作按钮反转为浅色。
- **操作反馈灰**：填充按钮 hover 使用 `muted`，所有按钮 pressed 使用 `selected`；不额外制造色相变化。

### Neutral

- **页面灰、侧栏灰、容器面**：`background`、`sidebar`、`surface` 分别承担页面、导航与设置/弹层的色阶。用户气泡使用 `user`，深色 composer 与其弹出面板使用 `dark-composer`；助手回复直接落在页面背景。
- **阅读墨色、辅助灰**：`text` 承担正文和标题，`muted` 用于说明、图标、消息角色与服务状态提示。
- **边界灰、悬停灰、选中灰**：`border` 分隔区域与容器，`hover` 提供交互反馈，`selected` 标记当前会话、外观导航与所选主题。

上下文圆环使用 `context-ring` 灰色角色，底环透明度 25%、占用弧透明度 70%；未知态只保留底环。进度条使用 border 底轨与 muted 填充。

Markdown 链接/强调色、代码块与引用块背景使用对应专用角色。`markdown-accent` 不表示链接已可打开。

**The Native Scope Rule.** 现行产品页面复用原生 Palette；已退役 Web/CEF 的琥珀控制台 tokens 不迁入新表面。

## Typography

**Body Font:** 固定加载随应用交付的 `assets/NotoSansSC-Regular.otf`（Noto Sans SC Regular 2.004），中文与拉丁文字共用同一字面；不按本机系统字体或 TTC 首个字面变化。Git 内保存完整 OTF 的 xz/tar 归档，构建时离线解包，包含 OFL-1.1 许可；原始来源、摘要和无字形变更说明见 `apps/native/assets/fonts/provenance.json`。

设计字号以EM记载；Noto SC的公开组件请求字号为设计值乘1.448（如正文16EM请求23.168）。文字测量、换行与输入使用同一换算，Font Awesome/代码保持独立度量。Markdown标题行高沿用请求字号加6，H1/H2/H3约34.96/32.06/30.62px；正文行高24px。换算见 `apps/native/typography.hpp`，字体度量由原生测试校验（EUI-20261005-007）。

**Character:** 简体中文为主，标题与正文共用 Noto Sans SC 无衬线文字。Font Awesome 7 Free Solid 单独加载图标；不以 Unicode 文字符号代替操作图标。

### Hierarchy

- **Headline**：空态问题，用字号与留白建立阅读起点；没有额外的 eyebrow。
- **Brand / Title**：侧栏品牌与对话框题，权重略高。
- **Settings Heading**：外观页与模型服务标题使用 24px，服务商详情标题使用 18px，说明沿用 Body，主题按钮沿用 Navigation，偏好声明沿用 Label。
- **Model Field**：模型表单的单行输入和标签均使用 16px；标签行高框为 28px，输入行高交由 EUI 组件。协议和页底按钮沿用 Button Label。
- **Message / Body**：用户正文与助手 Markdown 为 14px / 22px；H1/H2/H3 为 18/16/15px，代码 13px，块间隔 8px。引用预览 14px / 22px。弹层正文 28px 行高。Markdown 代码字体与未显式设置的标题行高由 pinned EUI 提供，不猜测为产品 token。
- **Context Detail**：标题与百分比沿用 16px / 500 权重，Token 摘要沿用 14px；二者行框 28px。来源、模型与本次引用行沿用 13px、24px 行框。
- **Navigation / Label / Hint**：会话、工具条、状态与快捷键提示逐级收紧。
- **Input / Button Label**：输入保持正文的 14px；草稿测量与实际输入同用 `ui_font_size(14) * 1.2` 行高（约24.33px）。composer 模式/模型 13px、通知 12px、快捷键 11px，对话框操作 15px。
- `text()` 默认行高为字号的 1.5 倍；输入与部分 EUI 组件保留库默认行高，不把未指定值记成产品规则。
- 单行文字和图标使用同一行框的 ink-center 对齐，文字不再用字号差值手工偏移；图标行高使用图标字号，行框高度由所在行决定。

Markdown 行内段使用 Top 对齐与 `(lineHeight - fontSize) / 2` 的统一行框内边距，普通中文、英文与标点不再逐字按轮廓居中（EUI-20261005-006）。按钮与图标仍保持整个标签的 ink-center。行内代码还原独立的逻辑字号并整体居中，保留上游保守宽度预算。代码继续使用 EUI 的 monospace 字体；pinned EUI 未按字体文件切换字重，现有 fontWeight 值不代表已交付独立粗体字面。

维护者授权按ZCode选择字体，M6-12 / DEC-040；ZCode使用系统无衬线栈，本机中文对应Noto Sans CJK SC，选其同系列官方简体中文区域字面。统一中文字面和Markdown行内基线，不扩大布局与主题范围。验证见 `docs/compatibility/native-typography-20261005.md`。Windows与真实IME的字体表现尚待目标平台补跑。

## Layout

- 初始窗口为 1180×800 逻辑单位，最小为 860×620；初始化按 content-scale 与 framebuffer/window 比例换算系统窗口尺寸。仅记录当前初始换算，不宣称跨显示器动态 DPI 行为。
- 侧栏默认 260px，可调范围 224–400px；实际宽度上限为 `max(224px, min(400px, screen.width - 520px))`。进程内保留用户所需宽度，窗口缩小时只钳制显示宽度；手动收起后宽度为零，展开时恢复当前窗口容纳的宽度。没有自动断点隐藏。
- 分隔条的透明命中区宽 8px，中心在侧栏右边界，从 y=60 延伸到窗口底部；hover 使用 border、拖动/pressed 使用 muted。支持鼠标拖动，键盘聚焦后左右键每次调宽 8px。
- 标题拖动区和分隔线为 60px 高，与方向契约一致。顶部60px标题栏左侧显示当前会话标题（14EM、现有500权重请求），空标题回退“新对话”，随当前会话切换更新；左边距为侧栏右侧28px，侧栏收起时从x=116px开始，右侧预留146px用于窗口按钮；收起侧栏的设置页右侧预留286px避开返回对话按钮。标题按字体度量省略且不改变原始值，文字不拦截标题栏拖动。
- 空会话列为 `min(672px, main_width - 48px)`，水平居中；问候 30px、54px 行框，说明 14px、28px 行框，无建议列表。
- 有消息时列为 `min(800px, main_width - (main_width >= 864px ? 96px : 48px))`，水平居中。用户气泡按文字测量宽度加 24px，最小 80px，最大 `min(576px, thread_width)`，右对齐；助手正文宽 `thread_width - 16px`，无卡片。
- 编辑区高 `clamp(measure(draft, column - 40px, ui_font_size(16), ui_input_line_height(16)) + 24px, 52px, 168px)`；composer 高为编辑区 + 56px 工具区，有附件或引用再加 36px，编辑最后输入再加32px；非编辑态总高 108–224px（有附件或引用 144–260px）。活动 composer 顶部为 `screen.height - composer_height - 28px`；空态期望顶部为 `max(188px, screen.height × 0.29 + 106px)`，按同一底部边界钳制。问候顶部为 `max(80px, composer_y - 104px)`。
- 消息滚动区顶部 72px，高 `max(48px, composer_y - 88px)`；滚动条宽 4px、gap 8px。工具条顶部为 `composer_y + composer_height - 44px`；面板下方 24px 通知/快捷键行从面板底部 + 4px 开始。
- 外观/模型设置保留 `min(800px, main_width - 64px)` 居中列。
- 会话列表为 48px 行高、4px 行间隔；消息行间隔 14px。新建/返回按钮宽度为 `sidebar - 32px`，会话列表宽度为 `sidebar - 24px`，随侧栏一起变化。最小窗口保持同一结构，用户可收起侧栏增加空间。
- 外观页标题框从 y=104 开始，高 48px；说明从 y=160 开始，高 32px。主题面板从 y=232 开始；内容列宽度低于 600px 时由横排变为上下排，面板由 100px 增高至 126px，并省略重复说明，保留偏好声明。

- 模型页标题从 y=90 开始，高 48px；说明从 y=140 开始，高 32px。服务商/详情共用面板从 y=194 开始，高 `screen.height - 334px`。内容列宽 ≥700px 时内部导航为 176px，导航行高 44px、间隔 6px，内部两侧各留 8px，细分隔线后详情左右各留 24px，标题距面板顶部 16px。内容列宽 <700px 时不显示左栏，详情宽为 `content_width - 48px`，面板顶部改为全详情宽、40px 高的当前服务商名称选择器，距顶部 12px；18px 文字与14px下拉图标。点击后显示带完整可区分名称的滚动列表，选中再进入对应配置。表单从 y=250 开始，高 `screen.height - 402px`；六个字段行高 92px，各含 28px 标签框、8px 间隔与 44px 输入。协议行高 94px，思考能力开关高 40px、说明高 64px，凭据说明高 60px。滚动条宽 4px、与内容间隔 12px。
- 模型页底部状态固定于 `screen.height - 110px`，高 28px；按钮在其下 38px，高 40px。重新连接在详情宽 <480px 时为 36px 图标按钮，否则宽 104px；取消修改宽 80px，停用宽 100px，保存并应用宽 112px；最小窗口滚动表单，页底操作保持可达。

**The Reading Column Rule.** 空态与活动态各按实际列宽策略居中；消息与活动 composer 共用列。设置保留 800px 上限；所有列受主区可用空间限制。

## Elevation & Depth

自定义容器以色调分层和 1px 细描边构成平面结构。按钮、输入与用户气泡不添加阴影；对话框使用 25% 黑色全窗 scrim 和最高交互层。没有自定义页面入场或装饰动效；按钮 pressScale 为 1。

**The Flat Surface Rule.** 自定义按钮、用户气泡和输入不添加阴影；层级靠中性色阶与细描边表达。

## Shapes

输入 4px、消息动作 5px、工具条标签 6px、普通控件/选段浮层 7px、Markdown 块 8px、用户气泡 10px、弹层 12px、引用 pill 14px、composer 16px；实际值以 frontmatter 为准。标题分隔线、侧栏边界与容器边框为 1px；窗口无系统装饰。未最大化时边缘设置 5px 的透明 resize 区、角部设置 14px 的 resize 区；这些命中区不是装饰描边。

## Components

### Buttons

工具栏 icon button 为 36×36px，图标 17px；透明常态使用正文墨色，hover 使用悬停灰。发送采用填充按钮，空白草稿时禁用。新建对话/返回对话默认为 228×36px，宽度随侧栏变化；对话框动作 76×38px。按压无缩放。键盘焦点绘制与禁用处理由 EUI 组件提供，本层没有自定义焦点环 token。

### Mira Brand Image

品牌身份沿用维护者指定的本机最新 Mira 红发蓝眼角色。`assets/mira.png` 是
`~/mira/docs/mira.png` 的透明 PNG 原样副本，来源提交为
`472e43010485131790ca300c574d0ba17e50a711`；SHA-256 与原始来源记录见
[`assets/provenance.json`](assets/provenance.json)。不重绘、不改色、不换用旧 pinned 海报。

侧栏使用 `assets/mira-ui.png`：从原图去除 ancillary metadata 后无损 RGBA 重编码的
1254×1254px 派生图，每个像素及 alpha 与原图完全一致；原图完整保留来源元数据。
这是 EUI-20261004-003 的资源适配：EUI 在 PNG/SVG 判别时扫描前 511 bytes 的 `<svg`，
原 PNG 的 caBX/C2PA 元数据在 byte 305 含 SVG 缩略图，导致误分类和侧栏解码失败。
派生 hash 与转换说明同样记录在 provenance.json；不把该适配解释为画作修改或层级覆盖。

侧栏展开时图像在 (24, 16) 的 28×28px 框内使用 contain，保留比例与透明背景，
不参与命中测试；Mirage 字标从 x=64 开始，宽 `sidebar - 132px`（默认 128px），与图像共用 28px 行框。
窗口图标继续使用原始 `assets/mira.png`；构建将原图与 UI 派生图复制到可执行文件旁的 assets 目录，运行无需本机 Mira 源目录。
Windows EXE 资源使用仅作 ICO 容器转换的 `assets/mira.ico`，替换 EUI 示例图标；
Windows 资源构建与真机显示尚未验证。`trayIcon` 仍预置原始 PNG，但托盘仍关闭。

Linux Dock 通过 `org.mirage.native.desktop` 关联应用身份与图标；文件标识和
`StartupWMClass` 均匹配 appId `org.mirage.native`。构建生成的 entry 使用可执行文件
绝对路径作为 Exec、同一构建目录 `assets/mira.png` 的绝对路径作为 Icon。
显式 target `mirage-native-register-desktop` 将 entry 复制到
`$XDG_DATA_HOME/applications`（未设置时为 `$HOME/.local/share/applications`），
并在 `update-desktop-database` 可用时刷新数据库；普通构建不写用户 profile。
当前 Release 已注册并重启：Gio 的 Icon/Exec 解析、窗口 WM_CLASS 匹配通过，
AT-SPI 中 GNOME Mirage 按钮的 SHOWING=true。Dock 像素截图因权限限制未完成，
这些关联与可访问性证据不构成图标实际像素显示的视觉验证。

### Navigation

可调宽侧栏承载品牌、新建、会话列表、说明与设置入口。会话行有独立选中底色和 hover；标题 14px，Font Awesome 对话图标 16px。底部齿轮打开设置；侧栏收起后顶部保留展开与设置齿轮，主题操作集中在设置的外观页。设置侧栏以“返回对话”和“外观 / 模型”分类替代会话列表，当前分类填充 selected；收起侧栏时标题栏另有 124×36px 的返回按钮。会话行和窗口标题按实际字体测量可用宽度；超长标题逐个移除 UTF-8 码点并追加省略号，仅缩短显示文本，保留原始会话标题。首次用户提交生成标题时跳过开头空格、制表符与CR/LF，纯空白回退“新对话”；标题取第一行最多16个UTF-8码点。提交文本先保留用户草稿，再追加引用和附件，防止包装标签抢占标题；无草稿/引用的附件提交以“附件：文件名”开始。

### Draft Sessions / History

新建只准备本地草稿，首次发送才创建远端会话；请求被接纳并出现实际消息后才进入历史。
重复新建复用无消息、无远端 ID 且没有提交或删除等待的本地草稿。无消息的本地草稿和远端记录均不生成侧栏行；
零历史使用14px“暂无历史对话”和13px“发送消息后将显示在这里”，共用 muted，居中的空态 composer 保持既有布局。
历史行高40px，标题可用宽为行宽减100px，为右侧36×36px垃圾桶留位（x=行宽−40px、y=2px）。
运行、提交或删除等待时垃圾桶禁用。删除当前行回到草稿，删除其他行保持当前选择与草稿。
服务列表刷新移除已不存在的稳定历史，空历史快照同样移除旧行；本地草稿与运行、提交、删除等待记录保留。

### Appearance Settings

外观页延续 surface、细描边、panel 圆角；没有新增彩色控件或装饰层。主题标签在面板内
(24, 16)，宽屏说明在 (24, 50)。浅色/深色按钮为 116×40px、横向步距 124px（间距 8px），
文字与图标均为 16px。选中项填充 selected，未选中填充 surface，均有 1px border；
hover 使用 hover、pressed 使用 selected。宽屏按钮组从面板右侧 264px 开始、y=30；
窄屏在 (24, 62) 排于标签下方。

选择即时刷新整页 Palette；面板下方 14px 声明“即时应用 · 本次运行内保留外观偏好”，
宽屏距面板顶部 120px、窄屏 136px。没有系统主题同步、磁盘持久化或后台任务。
设置打开时不合成对话输入和 composer，保留会话及草稿；Ctrl+N 与 Ctrl+Enter 不修改背景会话。
Ctrl+, 在没有弹层时打开设置；Escape 先关闭弹层，再返回对话。

侧栏横向调整光标由私有 GLFW Adapter 创建和切换，悬停/拖动结束后复原，
应用 onShutdown 释放；该平台类型不进入设计 token 或公开产品接口。

### Model Settings

服务配置严格对照ZCode 29628c9的SectionLayout、Navigation、ProviderCardSections与ProviderApiFormatSelect。内部服务导航为224px；详情可用宽小于700px时保留56px图标栏，不再切成全宽选择器。导航32px行高、12px面板内缩进；详情24px内缩进。右侧服务标题18px，空服务直接编辑名称，已保存服务在标题更多菜单中重命名/删除。Base URL包含origin和路径，API格式使用下拉菜单（OpenAI Chat Completions / Responses），随后是默认遮蔽的API Key、模型列表、添加模型及当前模型的上下文/思考配置。标签14px，输入32px、inset12px；模型行36px，外层40px。页底状态、取消修改和80×32px保存固定于详情内；保存未就绪时降低按钮不透明度。

服务与模型分别呈现，settings.models仍有12条总预算；provider_id/provider_name是可选产品元数据，旧display_name单模型配置兼容。没有目录时左侧显示本地“未命名服务”；填名称后先保留该草稿行，保存成功ACK才更新左侧名称、目录与live_model。无模型服务允许保存为停用。添加/删除模型在独立编辑副本中进行；共享连接与Key按服务一致更新。显式models空数组清空目录，缺失表示旧客户端保持目录。删除服务释放不再被目录/活动模型引用的Key；清理失败返回警告。保存失败保留全部编辑、Key草稿及已应用模型；取消修改仅在读取成功后丢弃。新增模型前保留当前模型的窗口与思考编辑。思考方式/深度在会话栏选择，选项以服务端模型能力投影为准，默认不传参数。

本轮基于公开源码逐项对照和原生实渲染取证；未取得运行中ZCode原生窗口截图，不宣称像素级1:1。Mirage尚未接入的Anthropic、OAuth/套餐、模型连通性探测和额外模型元数据不展示伪实现，差异及补齐条件见M6-21验收记录。

### API Key Field

DEC-038（2026-10-05）取代此前“凭据环境变量名称”的表单说明；旧环境变量只保留服务端兼容读取，现行 UI 直接填写 API Key。
第五行沿用92px字段行、28px标签、8px间隔、44px输入和16px字号。输入宽为详情表单宽减44px；
右侧36×36px眼睛按钮置于行内x=宽−40px、y=4px。“移除”64×28px位于标签行右侧，已配置或待移除时显示；保存中禁用。
默认掩码为星号，显示开关只作用于本次输入。已存 Key 不加载到输入；占位“已配置；填写新 Key 可替换”，
留空保留，显式移除后保存清除，待移除时占位改为“保存后移除 API Key”。
本次 Key 限于2048字节可打印 ASCII（33–126）；保存成功、取消修改和服务商切换丢弃密钥草稿。
撤销/重做禁用；遮蔽时复制仅包含掩码。此输入通过单一 EUI Adapter 实现，依据 EUI-20261005-004，
不将缺少密码输入能力解释为通用输入规范。Key 保存在系统凭据服务，普通配置仅存引用；
凭据不可用或锁定等错误沿用固定状态行，成功后的旧凭据清理 warning 也沿用该行，不添加颜色或额外面板。
Linux Secret Service 为当前验收范围；Windows Credential Manager 的目标平台验证另行记录。

### Messages / Markdown

用户消息为右对齐轻灰气泡，10px 圆角、无 border、水平 12px / 垂直 8px 内边距。已完成助手回复直接使用 EUI 公共 MarkdownBuilder，无外层卡片或角色标题；运行/失败行保留图标与明确状态。展示只移除回复首尾 CR/LF 空行，保留内部格式与服务原始历史，没有桌面执行卡。非运行中消息下方预留28px动作区，复制仅在正文/动作区悬停或键盘聚焦时显示；最后一条用户消息另有编辑。按钮28×26px、图标12px、圆角5px、muted色，焦点时1px muted边界；没有整条引用按钮。所有 icon-only button 显式 `.text("")`，避免默认文字泄漏。

连续 CJK 段额外间距由单一 `markdown_adapter.hpp` 经公开 DSL 修正，反馈 EUI-20261004-004；保留上游解析、保守换行及高度预算，升级后按反馈移除。支持标题、列表、粗体、代码和表格。拖选同一消息的实际渲染文字后显示100×32px“引用选段”浮层，13px标签、12px图标、7px圆角、1px边界；空间不足时放在选区下方，避开标题和composer。单一选择适配器见EUI-20261005-005；Ctrl+C复制选段，Escape/滚动/切换/编辑/背景点击取消。尚不支持跨消息选择、拖选自动滚动或链接打开。

最后一条用户输入可进入编辑：composer顶部增加32px提示条和56×26px取消按钮，标签/取消使用12px。进入时保留原草稿，取消恢复；接受重发才替换原轮次两条消息，并重置用量为未知，新的成功请求回填真实用量。运行/提交期间禁用编辑，重发不撤销外部工具副作用。

### Inputs / Fields

多行输入位于 composer 的 (4px, 4px + refs_height + edit_height)，宽 `column - 8px`，内部 inset 12px，14px 文字，增长到 168px 后内部滚动。背景与 focused 背景保持 composer 颜色，边框透明、hit 边框为零，无阴影；composer 外框聚焦时由 border 变为 muted。

Enter 发送、Shift+Enter 换行、Ctrl+Enter 保留发送兼容；composition 状态阻止 IME 候选确认提前提交。设置、模态或工具条 popover 打开期间阻止背景输入。提交未接纳或失败时保留草稿；合成中文粘贴不算真实中文 IME 验收。

### Composer / Context

浅色为 surface、深色为 dark-composer，16px 圆角、1px 边界。工具顺序为附件、访问权限、上下文比例、模型、思考深度、发送。加号/发送 36×36px、图标 17px；权限 110×32px；上下文入口 36×32px，内置 20×20px 圆环；模型按实际字宽加图标/内边距收紧，宽 96–180px（受剩余空间约束）、高 32px，长名称省略显示；完整模型 ID 可在“管理模型”设置中查看；思考 88×32px。标签 13px、下拉图标 9px。权限与思考各自保留于当前本地会话，提交时冻结；调整会话选择不改写已冻结的在途请求。只读不注册工具，默认只使用当前已注册 wait，不表示完整桌面访问。模型入口以“服务商名称 · 模型 ID”列出已保存配置，并提供“管理模型”；服务 ACK 后更新已应用模型，活动任务或保存失败拒绝切换。模型只有默认能力时入口说明该模型未提供其他思考选项，不能选择档位。运行期间发送改为停止，调用 session.chat.cancel；提交等待或附件读取期间禁用发送；空草稿/引用/附件、断开连接或删除等待时同样禁用；首次提交按需创建远端会话。禁用实心发送使用 hover 底色与 muted 图标，避免正常行动色暗示可提交。

加号提供“添加文本附件”和“查看附件与引用”，明确说明 UTF-8、最多 4 个与合计 8 KiB。同步系统文件对话框只接收用户主动选择的文件，读取交给前端唯一 Executor 的有限任务；拒绝非普通文件、二进制、无效 UTF-8 与超限，Linux 还拒绝符号链接/FIFO，取消选择无错误。附件以文件名与完整文本加入明确标记的不可信用户 TextPart 上下文，不提供图像/二进制上传或自动目录读取。附件和引用出现后展示 176×28px“附件 N · 引用 N”计数 pill，图标 11px、文字 12px；点击打开宽 `min(320px, column)`、高 `min(352px, screen.height - 160px)` 的滚动预览。附件展示文件名、字节数、13px / 22px 文本和移除入口；引用展示完整文字、来源角色、消息 ID，并可逐条删除，文字 14px / 22px、行间距 16px、滚动条宽 4px / gap 6px。引用最多 4 段、合计 8 KiB；附件另有相同上限，二者与草稿一起受编码后 16 KiB wire 上限。引用/附件实例有独立单调 ID，ACK 仅清除该次提交实例；附件 generation 防止已清空会话接纳迟到读取结果。

上下文圆环采用 24×24 SVG 视框、半径 10、描边 4，原生显示为 20×20px；用 SVG 路径弧绘制占用，避免依赖 dash-array 支持。点击或经 EUI 键盘操作展开宽 `min(320px, column)`、高 224px 的详情，浅色使用 surface、深色使用 dark-composer，按上下文工具的 `column - 180 - model_width` 锚点定位并钳制到 composer 可用宽度；与 composer 间隔 8px、顶部至少 68px。标题与百分比在顶部，下一行展示带千位分隔的“输入 / 窗口 Token”；下方是 6px 高、3px 圆角的比例条，以及来源、模型、本次引用数三行。未知时显示灰色空环、“未知”与“Token 用量尚不可用 / 等待模型返回 Token 用量”；已有输入但分母未知时显示“输入 / 未知 Token”，提示到模型设置填写窗口预算。真实零用量配合已知分母显示 0%；原始 Token 数和百分比可以超过 100%，仅圆环与进度条图形钳制到 100%。

分子仅来自 Mira ModelResponse.usage 的 Exact / ProviderReported input_tokens，按最近一次成功请求显示“上次请求 · 模型输入用量”；工具循环取最终回复调用的输入，不累计各次调用，不计输出 Token，不从草稿或 bytes 估算。失败/取消保留前一次成功值；新成功轮次缺少用量则回到未知；迟到的旧序列不能覆盖较新的用量。服务重启后的旧历史不持久化用量，显示未知。截图中的 391 / 128,000 Token = 0.3% 来自真实请求与显式测试预算，128,000 不是自动发现的供应商窗口。

模型 popover 高 `min(352px, 68px + 44px × 配置数)`，动作/权限 popover 高 164px，思考 popover 按服务端选项数确定高度；各面板按对应工具的横向锚点定位（附件/引用 8px、权限 48px、上下文 `column - 180 - model_width`、模型 `column - 140 - model_width`、思考 `column - 136px`），再钳制到 composer 边界；共用 12px 圆角、1px 边界，透明 dismiss 层 z=20、面板 z=21，位于 composer 上方且顶部不小于 68px。

### Dialog

对话框在全窗 scrim 上居中；标题、正文、确认/取消属于同一面板。确认清空和删除均固定到打开弹层时选定的会话 ID；切换当前选择不会改变目标。
scrim 点击与 Escape 关闭弹层；弹层打开期间快捷键和输入受保护，面板拦截点击，防止穿透关闭。

删除确认使用 frontmatter 的 `dialog-delete`（其他确认沿用 `dialog`），标题“删除这段对话？”沿用21px / 600权重。
面板内首条输入摘要在(24, 67)的26px行框中用14px正文色显示，下一行在(24, 94)用14px muted 显示“最近消息 · 角色：摘要”，
二者都来自固定目标。每条摘要先在首个CR/LF或256字节处截断，保留完整UTF-8码点，将控制字符替换为空格，
有余文时追加省略号；再按实际字体测量单行可用宽度，避免完整长消息参与渲染和测量。
正文从y=131开始，使用17px / 28px：“这段对话及其上下文将被删除，无法恢复。其他对话会保留。”
取消和填充“删除”按钮沿用76×38px、15px，位于面板底部以上60px。
确认提交后等待 service ACK 才移除历史并刷新服务列表；失败保留行和可读提示。

## Do's and Don'ts

### Do:
- **Do** 使用同一 Palette 角色切换明暗，让文字、边界与操作同时变化。
- **Do** 保持文字与图标的职责分离；操作图标使用随应用配置加载的 Font Awesome Solid。
- **Do** 保留 Mira 品牌图的原始画作、比例、透明背景与来源记录。
- **Do** 按服务回执显示发送、运行与终态；composer 只显示服务确认的模型。
- **Do** 保持附件/引用文本、来源与删除入口可检查；上下文用量标明上次请求和配置预算，缺少事实源或分母时明确未知。
- **Do** 将新原生页面的视觉扩展同步到本目录和仓库根目录的现行设计记录。

### Don't:
- **Don't** 将设置草稿包装成已应用模型，或将通用文字/工具轮次包装成桌面任务执行。
- **Don't** 以 ZCode 参考截图作为应用内素材；交付的品牌 raster 仅来自已记录来源的 Mira 原始角色图。
- **Don't** 将 Web/CEF 的仪表 caps、琥珀主色与灯阵语法默认套入原生对话页。

## 2026-10-05：会话密度与真实执行反馈（DEC-041）

M6-15/16/17/18：正文/输入14EM，正文22px行高；标题14EM、品牌18EM/28px图像，
会话行40px、新建按钮36px，外观/模型标题24EM。保留Noto SC换算与中文基线适配。
工具组按实际模型宽度定位：附件/权限靠左；model_x=column-140-model_width，
context_x=model_x-40，思考/发送维持右边距，popover复用实际锚点。

等待只有三点低幅脉冲与“思考中/正在回复 · 用时”一行；已收到的Markdown直接显示。
用时来自本UI观察到pending后的steady_clock，终态冻结；历史未知时不编造时长。
Executor周期句柄100ms刷新活动会话，取消句柄、停止worker、消费future、shutdown后释放UI。
preview是≤16KiB完整快照，经服务Topic与UI MpscChannel传递，不持久化，不改变Token占比；
新尝试清空，乱序/终态后/取消后的预览拒绝。最终规范响应替换预览。

公开ConversationLoop替代临时文字回填（MIRA-20261004-001）。仅wait工具；默认2048输出token
对应7次模型请求上限，32次工具，预算按profile公开whole-run规则推导；用户大输出预算时
减少轮数并将单轮预算钳制至profile上限的一半。没有桌面观察或RPA能力。
Linux使用EUI平台公开XIM光标入口；详细互操作、环境限制和验收见
[进度验收](../../docs/compatibility/native-conversation-progress-20261005.md)。此前16px、私有循环和无流式段落为历史实现，以上替代。


## 2026-10-06：流式阅读位置（M6-20）

当前会话在底部持续跟随；用户上翻或拖动滚动条后保留阅读位置，终态不强制跳转。
再次到达底部恢复跟随；未跟随且内容可滚动时，在(x + column/2 - 18, composer.y - 58)
显示36×36px、14px向下箭头，surface底色、1px border、既有hover/pressed色；点击或聚焦后
Enter/Space返回最新。程序offset使用修复后的EUI公开scrollView，不重建私有Runtime状态。
真实窗口正常/最小明暗、IME候选、等待/流式/用时证据见
[最终验收](../../docs/compatibility/native-conversation-finish-20261006.md)，渲染夹具验证实际视口变化。


M6-21侧栏统一20px行外缩进，图标盒24px的中心x=44、文字x=64；品牌图像32/20起点、24×24，返回/新建、分类、历史及底部入口沿同一轴。

## M6-22：服务预设与协议入口（2026-10-06）

本轮沿用ZCode服务导航分组：已配置/本地未命名草稿在前，未配置的厂商置于“预设服务”下，选择后填充Base URL、协议与一个默认模型；只需Key即可提交。11项API Key预设来源为固定29628c9的config/provider/zcode-builtin.json，额外MiniMax国际入口由官方手册核对。预设不创建已保存记录、不自动调用服务，未保存编辑切换需先保存/取消；成功ACK后服务名称/模型与侧栏更新。字段仍可编辑。

会话侧栏外边距24px，品牌图/操作图标左边36px、中心48px，带图标的标题左边68px；顶/底分隔线同为24px，历史列表同外边距。服务标题编辑器位于logo后28px、文字inset8px，静态标题从36px开始，切换编辑不跳位。原有窗口大小、明暗Palette、Noto字体和会话密度保持。

协议选择新增Anthropic Messages；普通文本、工具与流式由Mira公开方言执行。首阶段未接入thinking的边界由DEC-046替代；可用思考选项直接在会话栏选择。模型层真实识图验证与产品附件功能分开：MiniMax-M3能通过Mira公开Provider读取合成图片，当前会话附件仍为文本，尚无图片上传入口。不宣称完整ZCode或未测试厂商已可用。

M6-22厂商导航采用有来源记录的品牌资产，窄窗口以56px图标栏/2px滚动条和悬停名称保持可辨识。预设不是已确认配置；11项默认模型取固定ZCode源码，只对MiniMax-M3有真实互操作证据。图片上传入口尚未交付。验收：docs/compatibility/provider-presets-and-vision-20261006.md。

## 托盘退出确认（DEC-045）

窗口关闭保留常驻 Runtime。托盘请求退出且存在活动工作时，打开/激活同一个前端，
在中性 scrim 上显示 460×240 确认卡，复用现有明暗颜色、字体与按钮尺寸。正文给出
活动计数，操作为“继续运行”和“停止并退出”；Escape 等同继续运行，scrim 不确认，
其他全局快捷键不能穿透。按钮传当前 exit_epoch，过期确认由 Runtime 拒绝。宿主
连接断开后窗口正常关闭，不提供脱离托盘的离线产品入口。

## 会话思考（DEC-046）

设置页移除思考启用按钮。会话输入工具栏最后的思考按钮沿用紧凑32px高度、字号与向下
箭头；菜单只显示服务投影的选项，MiniMax-M3为默认/关闭/开启，M3.1 Flash Preview为
默认加低/中/高/极高/最高。消息、字体、阅读列保持现有风格；未知模型提示使用默认。

2026-10-07 / M6-27：到底部箭头作为会话视口之上的覆盖层（z=2），高于 scrollView
滚动条贡献的整个内容子树（z=1），低于弹层；绘制与鼠标命中使用同一层级。
保持原有位置/36px 外观和 Enter/Space 操作。鼠标回归通过真实 Runtime 按下/松开派发，
同时断言正文像素移动、末尾位置及恢复跟随，不能只调用 onClick/onKeyEvent 替代点击。
