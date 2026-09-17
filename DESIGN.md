---
name: Mirage 控制台
description: 任务控制台（MISSION CONSOLE）——驾驶舱而非聊天网页的 Agent 监督桌面
colors:
  background: "#141920"
  foreground: "#eae5d8"
  card: "#232b35"
  card-foreground: "#eae5d8"
  popover: "#1a2028"
  primary: "#e8a33d"
  primary-foreground: "#231a06"
  secondary: "#2e3945"
  secondary-foreground: "#cfc9b8"
  muted: "#232b35"
  muted-foreground: "#9aa2ac"
  accent: "rgb(232 163 61 / 14%)"
  accent-foreground: "#f6c453"
  destructive: "#f87171"
  success: "#34d399"
  warning: "#fbbf24"
  info: "#38bdf8"
  border: "#33404e"
  input: "#3a4756"
  ring: "#e8a33d"
  sidebar-background: "#1a2028"
  surface-raised: "#2e3945"
  overlay-scrim: "rgb(10 13 17 / 60%)"
  evidence-highlight: "#7fb3d5"
  evidence-highlight-soft: "rgb(127 179 213 / 16%)"
typography:
  display:
    fontFamily: "'Saira Variable', 'Noto Sans SC', 'Microsoft YaHei', sans-serif"
    fontSize: "34px"
    fontWeight: 700
    lineHeight: 1.04
    letterSpacing: "0.06em"
    fontVariation: "'wdth' 62.5"
  headline:
    fontFamily: "'Saira Variable', 'Noto Sans SC', 'Microsoft YaHei', sans-serif"
    fontSize: "20px"
    fontWeight: 700
    letterSpacing: "0.04em"
    fontVariation: "'wdth' 62.5"
  label:
    fontFamily: "'Saira Variable', 'Noto Sans SC', 'Microsoft YaHei', sans-serif"
    fontSize: "11px"
    fontWeight: 600
    letterSpacing: "0.08em"
    fontVariation: "'wdth' 62.5"
  body:
    fontFamily: "'Saira Variable', 'Noto Sans SC', 'Noto Sans CJK SC', 'Microsoft YaHei', system-ui, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.5
  mono:
    fontFamily: "'Chivo Mono Variable', ui-monospace, 'Cascadia Mono', monospace"
    fontSize: "12px"
    fontFeature: "tnum"
rounded:
  sm: "4px"
  md: "6px"
  lg: "8px"
  pill: "999px"
spacing:
  space-1: "4px"
  space-2: "8px"
  space-3: "12px"
  space-4: "16px"
  space-5: "20px"
  space-6: "24px"
  space-8: "32px"
  row-sm: "24px"
  row-md: "28px"
  row-lg: "34px"
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.primary-foreground}"
    rounded: "{rounded.sm}"
    height: "28px"
    padding: "0 12px"
  button-primary-hover:
    backgroundColor: "color-mix(in oklab, {colors.primary} 88%, white)"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.muted-foreground}"
    rounded: "{rounded.sm}"
    height: "28px"
    padding: "0 12px"
  button-danger:
    backgroundColor: "transparent"
    textColor: "{colors.destructive}"
    rounded: "{rounded.sm}"
    height: "28px"
    padding: "0 12px"
  card:
    backgroundColor: "{colors.card}"
    textColor: "{colors.card-foreground}"
    rounded: "{rounded.md}"
    padding: "16px"
  badge-warning:
    backgroundColor: "transparent"
    textColor: "{colors.warning}"
    rounded: "{rounded.pill}"
    height: "20px"
    padding: "0 8px"
  estop:
    backgroundColor: "color-mix(in oklab, {colors.destructive} 8%, transparent)"
    textColor: "{colors.destructive}"
    rounded: "{rounded.sm}"
    height: "22px"
    padding: "0 10px"
  estop-latched:
    backgroundColor: "{colors.destructive}"
    textColor: "{colors.background}"
  tile-on:
    backgroundColor: "color-mix(in oklab, {colors.primary} 14%, transparent)"
    textColor: "{colors.primary}"
    rounded: "{rounded.sm}"
  approval:
    backgroundColor: "color-mix(in oklab, {colors.primary} 7%, {colors.card})"
    textColor: "{colors.card-foreground}"
    rounded: "{rounded.sm}"
    padding: "9px 12px 12px"
  input:
    backgroundColor: "color-mix(in oklab, {colors.background} 55%, {colors.card})"
    textColor: "{colors.foreground}"
    rounded: "{rounded.sm}"
    height: "28px"
    padding: "0 8px"
---

# Design System: Mirage 控制台

> 本文描述**已实现**的设计系统（2026-09 冻结于 M1.5 UI 重写）。事实源：
> L1 原始值 `ui/app/src/theme/primitives.ts`；L2 语义清单 `ui/app/src/theme/schema.ts`；
> 主题值集 `ui/app/src/theme/themes.ts`（默认 `mirage-console`）；组件语言与
> `@layer` 结构 `ui/app/src/styles.css`；方向契约
> `ui/app/.impeccable/surfaces/ui-app-index-html.md`。本 frontmatter 中的颜色是
> 默认主题 **mirage-console dark（夜班大厅）** 的语义值经 `var(--mir-*)` 解析一层后
> 的具体值；light 值集与其余 5 套主题见 `themes.ts`。运行时组件只消费
> `var(--primary)` 等语义 token，不消费本文的十六进制字面量。

## Overview

**Creative North Star: "任务控制台（Mission Console）"**

把会话页造成一间任务控制大厅：壁挂大屏公开任务剖面的真相，控制台分工监督，任何时刻可放行、可急停。这是驾驶舱，不是聊天网页——它拒绝的品类默认排布：居中聊天列 + 通用侧栏的后台观感（方向契约 `ui-app-index-html.md`，Apollo-era Mission Control 语系，**非霓虹材料**：无辉光、无渐变彩板、无玻璃拟态；质感来自炭蓝金属底、奶油仪表面板、仪表 caps 刻字与灯阵的物理隐喻）。

监视感高于表达欲（PRODUCT.md 原则 1「可信优先」）：状态、证据、批准、停止永远一级可达。琥珀是唯一的行动/主动读数色，蓝线是证据与剖面的专用色，状态四色语义封闭；表达性让位于可扫读性，品牌活在精确的细节里。

**Key Characteristics:**
- 壁挂屏条（wall）+ 活动栏（rail）+ 签派栏（sidebar）+ 主区（main）+ cue 状态栏（statusbar）的仪表台拓扑，非居中聊天列。
- 炭蓝控制台底 + 奶油仪表面板；琥珀唯一行动色；蓝线剖面蓝为证据强调。
- 灯阵瓦片、警戒急停钮、金色主动作线、composer 对话/执行双模式。
- 紧凑仪表密度：24/28/34px 行阶梯，12–14px 字号，细描边小圆角。
- 三层 token（L1 primitive → L2 语义 → 组件），组件零私定颜色，`style-scan` 测试锁定。
- 6 套主题 × 明暗两态，同一语义契约多值集；`prefers-reduced-motion` 全动效降级。

## Colors

调色板是一间控制室的照明方案：炭蓝（console 阶）是机器外壳，奶油（cream 阶）是仪表面板，琥珀是唯一的行动与主动读数色，蓝线（blueline 阶）是证据与剖面的墨水，状态四色是封闭的信号灯语义。所有颜色定义在 `primitives.ts`（L1）并经 `themes.ts` 映射为 L2 语义 token；组件层禁止出现颜色字面量。

### Primary（行动色：琥珀）
- **琥珀读数（primary，dark `#e8a33d` = `--mir-amber-450`；light `#8a4d08` = `--mir-amber-700`）**：唯一行动色。主按钮、ring、激活位标、composer 金线、批准卡、灯阵待批准闪烁全部用它。深浅两态共用一套语义名。
- **primary-foreground**（dark `#231a06` / light `#fff6e8`）：琥珀上的刻字，近黑/近白，保证对比。
- **accent / accent-foreground**（dark `rgb(232 163 61 / 14%)` / `#f6c453`）：琥珀的低透明大底（hover、mode 选中），前景用更亮的 `--mir-amber-300`。

### Secondary（证据色：蓝线）
- **蓝线剖面蓝（evidence-highlight，dark `#7fb3d5` = `--mir-blueline-400`；light `#2e6f9e` = `--mir-blueline-700`）**：壁挂屏任务剖面 SVG 的描线与节点、工具卡的 kind 刻字、快照卡的 SoM 网格与元素框、上下文用量条。证据专用，**不作行动色**。
- **evidence-highlight-soft**（dark `rgb(127 179 213 / 16%)`）：证据色的软底（快照底、剖面底光）。

### Tertiary（状态四色：封闭语义）
封闭映射，任何主题不得改变 hue（`themes.ts` 头注）：成功绿 / 失败红 / 等待琥珀 / 运行蓝。
- **success**（dark `#34d399` = `--mir-green-400d` / 共享 light `#157f3d`）：完成、OK 灯、放行锁定。
- **destructive**（dark `#f87171` = `--mir-red-400d` / light `#dc2626` = `--mir-red-500`）：失败、急停、takeover。
- **warning**（dark `#fbbf24` = `--mir-amber-400d` / light `#b2540a`）：待批准、锁定提示、模拟标记。与行动琥珀同 hue 不同 token——等待是状态，不是按钮。
- **info**（dark `#38bdf8` = `--mir-sky-400d` / light `#0284c7` = `--mir-sky-500`）：运行中、思维链 live。

### Neutral（炭蓝 console 阶 + 奶油 cream 阶）
- **background**（dark `#141920` = `--mir-console-950`）：控制台炭蓝底；结构条（rail/statusbar/observer）用 `color-mix` 再压暗约 4–6%。
- **card / popover / surface-raised**（dark `#232b35` / `#1a2028` / `#2e3945`）：仪表面板层；light 模式下换用 cream 阶（`#f2ead8` / `#faf6ec`）。
- **foreground / card-foreground**（dark `#eae5d8`）：暖白奶油刻字（inkConsole 常量，`themes.ts`）。
- **muted-foreground**（dark `#9aa2ac`）、**secondary / border / input**（dark `#2e3945` / `#33404e` / `#3a4756`）：次级刻字、细描边、字段描边。
- **sidebar-background**（dark `#1a2028` = `--mir-console-900`）：签派栏。
- **overlay-scrim**（dark `rgb(10 13 17 / 60%)`）：命令面板/弹层下的遮罩。

### Named Rules
**琥珀唯一行动色规则（The One Amber Rule）。** 琥珀（primary）是全界面唯一的行动/主动读数色；蓝线是证据、状态四色是信号灯，三者都不进入行动按钮。金色主动作线（`btn-primary::before`、composer 顶线、批准卡顶缘）是注意力的唯一募集通道，不作装饰。

**语义封闭规则（The Closed-Token Rule）。** `styles.css` 与组件源码（shell/views/state）零十六进制与 `rgb(` 字面量，一切颜色经 `var(--mir-*)` 原始值、26 个语义 token 或对它们的 `color-mix(in oklab, ...)` 派生；唯一例外是外观设置页主题预览 swatch。由 `ui/app/test/style-scan.test.ts` 逐文件锁定。

**状态 hue 不迁移规则。** 状态四色在所有主题、所有模式下保持绿/红/琥珀/蓝的语义 hue；主题只换气质色与中性底，对比度校准只允许加深/变亮（`themes.ts` STATUS_* 常量即按门槛校准的产物）。

## Typography

**Display Font:** Saira Variable（`wdth` 轴，仪表用途一律 `font-stretch: 62.5%` 压缩态）+ Noto Sans SC / Microsoft YaHei 回退
**Body Font:** 同一 Saira Variable（正常宽度）+ 系统 CJK 回退
**Label/Mono Font:** Chivo Mono Variable（遥测/代码/键位），`font-variant-numeric: tabular-nums`

加载方式：`main.tsx` 以 npm 包 `@fontsource-variable/saira/wdth.css` 与 `@fontsource-variable/chivo-mono/wght.css` 引入，Vite 本地打包 woff2（含 `wdth` 轴），**无网络字体**（surfaces 契约 Constraints）。字体栈定义于 `styles.css` `@layer base` 的 `--mir-font-body/display/mono`。

**Character:** 仪表刻字（压缩 caps）与遥测等宽承担全部"机器感"，正文保持 CJK 可读性——品牌感来自字宽轴与字距，不来自花哨字体。

### Hierarchy
- **Display（壁挂屏状态动词）**（700, 34px, 1.04, 0.06em, wdth 62.5）：wall 大字动词「执行中」等；窄屏降至 26px。
- **Headline（页面/卡片题）**（700, 20px `--mir-text-2xl`, 0.04em）：页面头 `page-head h1`；卡片题 `card h2` 600/16px。
- **Label（仪表 caps，工具类 `.caps`）**（600, 10–12px, 0.08–0.1em, uppercase, wdth 62.5）：分组标签、字段标签、状态栏段名、灯阵瓦片标签。
- **Body**（400, 14px `--mir-text-base`, 1.5）：正文基准；消息正文 1.65 行高，线程栏 `max-width: 820px`。
- **Mono（遥测）**（Chivo Mono, 11–13px, tabular-nums）：工具参数/结果、时间线日志、obs-stream、kbd、批准详情。
- **Numeral（`.num`）**（Saira wdth 62.5, tabular-nums）：仪表数字读数。

字号阶（`primitives.ts`）：xs 12 / sm 13 / base 14 / lg 16 / xl 18 / 2xl 20 / 3xl 24px。

### Named Rules
**仪表 caps 规则。** 所有小写标签类文本（分组、字段、段名、瓦片标签）一律走 `.caps`：Saira 压缩态 + 600 + 大写 + 0.08em 级字距；不引入第三种字体，也不给正文加字距。

**mono 只讲机器话。** 等宽字体只用于遥测事实（参数、结果、日志、时长、键位）；散文与 UI 文案不用 mono。

## Layout

应用壳是 CSS Grid 仪表台（`styles.css` `@layer layout` 的 `.console`）：

```
'wall wall'     92px   (--mir-size-wall 壁挂屏条)
'rail main'     1fr    (活动栏 52px + 主区)
'rail status'   30px   (--mir-size-statusbar cue 状态栏)
/ 52px 1fr             (--mir-size-rail)
```

主区内部为横向三联：签派栏 264px（`--mir-size-sidebar`，会话搜索 + 置顶/今天分组）→ 线程流（中栏，内容 `max-width: 820px` 居中，composer 固定其底部）→ 观察台 336px（`--mir-size-observer`，运行时间线 + 观察流 + 上下文用量表）。壁挂屏条内再分三格：状态动词（minmax(220px, 1.1fr)）/ 蓝线剖面（2fr）/ 主机灯阵（4 列瓦片）。

**密度：紧凑仪表。** 行阶梯 `--mir-row-sm/md/lg` = 24/28/34px；控件高度对齐行阶梯（按钮/输入 28px，徽标 20px，estop 22px）；间距 4 基阶 `--mir-space-1..8` = 4/8/12/16/20/24/32px；卡片内边距 16px，线程区留白 20×24px。正文 14px、辅助 12–13px、刻字 10–11px。

**响应式降级（桌面产品，CEF 窗口可缩小）**：≤900px 先收观察台与壁挂屏剖面、状态栏 `hide-sm` 段隐藏；≤640px 再收签派栏，壁挂屏转单列（`styles.css` `@layer motion` 内两个 media query）。

## Elevation & Depth

深度靠**色调分层 + 细描边**表达，阴影仅两档、存在感极低（`primitives.ts` L1 + `themes.ts` 可选覆盖）。壁挂屏、rail、statusbar、observer 用 `color-mix(in oklab, var(--background) 88–96%, black)` 的压暗底色表明"凹进的金属面"；面板（card/popover）用亮一档的 token 表明"凸起的仪表"；不使用多层堆叠阴影。

### Shadow Vocabulary
- **card**（console dark 覆盖值 `0 1px 2px rgb(8 10 14 / 35%)`）：卡片与浮层的贴地阴影。
- **overlay**（console dark 覆盖值 `0 12px 32px rgb(8 10 14 / 50%)`）：命令面板、菜单、toast。

### Named Rules
**两档阴影规则。** 只有 `--shadow-card` 与 `--shadow-overlay` 两档；新组件不得发明第三档，深度优先用色调分层和描边表达。

## Shapes

小圆角 + 1px 细描边的仪表面板语言。半径阶 `--mir-radius-sm/md/lg` = 6/8/10px（L1 默认）；默认主题 mirage-console 经主题可选字段覆盖为 **4/6/8px**（`themes.ts` consoleTheme.radius），全元件更接近机械切角；胶囊 `--mir-radius-pill` = 999px 只用于徽标、系统行与锁定 cue。描边一律 1px `var(--border)`；强调描边用 `color-mix` 向语义色偏移（如 `--destructive` 55%、`--primary` 40%）而非加粗。灯阵瓦片、工具卡、批准卡均为直角面板 + 顶缘或侧缘的 1px 色线，不使用外发光。焦点统一 `*:focus-visible` 2px `var(--ring)` 外描边。

## Components

组件语言（`styles.css` `@layer components`，组件源码在 `src/shell/`、`src/views/`）：

### 按钮（`.btn` 系列）
- **Shape:** 28px 高（row-md）、radius sm（4px）、1px 描边。
- **Primary:** `--primary` 底 + `--primary-foreground` 字，顶缘 1px 金色主动作线（`::before`，`color-mix(white 55%, primary)`）；hover 提亮至 88% primary 混白；`:active` 下沉 1px。
- **Ghost / Danger:** ghost 透明底 muted 字；danger 透明底 + destructive 55% 描边字，hover destructive 14% 底。禁用 opacity 0.45。

### 灯阵瓦片（`.tile`，signature）
壁挂屏右侧 4 列状态灯（host 五态/seq/待批准等，`src/shell/WallDisplay.tsx`）。74×42px 起步，caps 标签 + 压缩态数值。点亮（`.is-on`）时边框向 `--tile-color` 65% 混合、底 14%、数值转 `--tile-color`——**快点亮 80ms（`--mir-lamp-on`）/ 慢衰减 600ms（`--mir-lamp-off`）**；`.is-blink` 加 `lamp-blink` 步进闪烁。`--tile-color` 只允许取语义 token（success/info/evidence-highlight/primary 等）。

### 警戒急停（`.estop`，signature）
cue 状态栏右端的红色警戒钮：destructive 8% 底 + 55% 描边字，caps 刻字 0.12em。触发后 `.is-latched` 实心 destructive + `lamp-on` 点亮。takeover 时壁挂屏动词转 destructive（`.wall-verb.is-takeover`）。

### 蓝线剖面（`.wall-profile`，signature）
壁挂屏中格的 SVG 任务剖面：`edge` 走 evidence 55% 底线，已完成 `edge-done` 满色 2px，活动节点 `node-active` 用 primary 描边并 `node-breathe` 呼吸（2.2s）。推进 = 描线（stroke-dashoffset，ease-out-circ，surfaces 契约 Signature interaction 3）。

### 工具卡 / 批准卡 / 快照卡
- **工具卡 `.toolcard`**：radius sm 细描边；kind 刻字用 mono + evidence 色；失败态描边向 destructive 45% 偏移；结果区 mono 12px、max-height 180px。
- **批准卡 `.approval`**（signature，"金色主动作线的舞台"）：primary 7% 混 card 底 + primary 40% 描边，顶缘透明→primary→透明的 1px 渐变线；放行后 `.is-approved` 转 success 6% 底，拒止 `.is-denied` 转 destructive 6% 底。
- **快照卡 `.snapcard`**：16:9 桌面回流，evidence 18% 的 8×12 SoM 网格 + 元素框（evidence 60% 描边）+ mono 角标；模拟数据带 warning 色 `snap-sim` 标记。

### Composer（双模式，signature）
线程流底部 820px 卡片。顶部模式切换（对话/执行 `.mode-btn`，选中用 accent）；输入非空或有待批准时 `composer.is-live` 点亮顶缘主动作金线（scaleX 0→1，`--mir-dur-medium` + `--mir-ease-circ`）；上方 `.composer-dock` 承载待批准事项（warning 色条目）。执行模式展示步骤构建器（`.step-row` 150px/1fr/auto 网格）。

### 命令面板 / 浮层
`.palette`（Ctrl+K，top 14vh、620px、popover 底 + overlay 阴影 + `palette-in` 入场）、`.popover-panel` 通知/批准中心、`.menu-panel`、`.toast`（右下角、状态栏上方、左缘 2px 状态色线）。遮罩统一 `--overlay-scrim`。命令面板、菜单项 hover/highlight 一律 accent。

### 状态徽标与状态点
`.badge`：胶囊、20px 高、前置 6px 圆点，`--badge-color` 只取封闭语义（is-success/warning/danger/info/muted），字用 Saira **87.5%** 宽度（比仪表 caps 略宽的徽标刻字）。状态栏 `.status-dot` 8px：is-ok/is-warn/is-bad/is-run 映射四色，is-run 步进闪烁。

### 观察台
`.observer` 336px 凹进面：时间线 `.tl-step`（8px 节点，is-ok/is-failed/is-running 映射状态色，is-running 步进闪烁 1.2s）；观察流 `.obs-stream` mono 11px，**trigger-lock**——运行中自由滚动，用户上滚即锁定自动跟随并出现 sticky 的 warning 色「已锁定 · 回到实时」cue（`.obs-lock-cue`）；上下文用量表 `.ctx-meter` 三段全部由 evidence 派生（55%/100%/45% 混合），进度变化走 `--mir-dur-medium` transform。

## Motion

动效 token 全部在 `primitives.ts`（L1，非主题化），语义是"仪器响应"：快、有端点、无弹性装饰。

**时长分层（`--mir-dur-*`）：**
- `--mir-dur-instant` 90ms —— 按压、表格行 hover 等即时反馈
- `--mir-dur-fast` 160ms —— 按钮配色、浮层入场（pop-in）
- `--mir-dur-standard` 300ms —— 状态换幕（verb-swap）、消息入场（msg-in）、toast
- `--mir-dur-medium` 450ms —— 展开类（composer 金线、ctx-bar 推进）
- `--mir-dur-slow` 650ms —— 换幕级大过渡（预留上限档）

**缓动：** `--mir-ease` cubic-bezier(0.2,0,0,1)（默认）；`--mir-ease-out` cubic-bezier(0.16,1,0.3,1)（入场）；`--mir-ease-circ` cubic-bezier(0,0.55,0.45,1)（剖面描线/金线）。

**灯阵语法：** `--mir-lamp-on` 80ms / `--mir-lamp-off` 600ms——快点亮、慢衰减（annunciator 物理隐喻）。关键帧 `lamp-on`（opacity 0.4→1）用于点亮，`lamp-blink`（steps(1,end)，1→0.35）用于待批准/运行闪烁（1.3s 或 1.6s）。

**状态换幕快切：** 壁挂屏大字动词切换是一次 300ms 快切 + 6px 上移入位（`verb-swap`），不做软淡入长驻（surfaces 契约 Signature interaction 1）。全部关键帧集中在 `styles.css` `@layer motion`（verb-swap / lamp-on / lamp-blink / node-breathe / msg-in / pop-in / palette-in / toast-in / caret）。

**降级：** `prefers-reduced-motion: reduce` 双保险——`primitives.ts` 把全部时长 token 归零；`styles.css` 再以全局 `animation/transition-duration: 0.01ms !important` 兜底。新动效必须消费 `--mir-dur-*`/`--mir-ease*`，禁止私有 duration。

## Theming（明暗两态与 6 主题机制）

**两态气质：** dark = 夜班大厅（默认气质：炭蓝 console 阶底 + 暖白刻字 + 亮琥珀），light = 白班控制室（奶油灰底 `#d6d0c2` + cream 面板 + 深琥珀 `--mir-amber-700`）。状态四色 light 用共享校准常量（console 用更深一档 `CONSOLE_STATUS_LIGHT`），dark 统一用 `*400d` 亮档。

**机制（`schema.ts` + `theme-manager.ts`）：**
- 主题 = 同一 26 token 语义契约（L2 全量清单）的多套值集；`buildThemeStylesheet()` 产出 `:root[data-theme='<id>'][data-mode='light|dark']` 规则对，`main.ts` 挂载一次。
- `ThemeManager` 把 `data-theme`/`data-mode` 写在根节点，切换只换属性不重载；偏好 `{themeId, mode}` 持久化于 localStorage `mirage.appearance`，mode 支持 light/dark/system（system 监听 `prefers-color-scheme`）；非法值回退默认。
- 主题值集只允许颜色；radius/shadow 是独立可选字段——**布局零位移门槛：主题不得引入影响布局的属性**。
- 组件与视图不感知主题，只消费语义 token（`theme-manager.ts` 头注）。

**6 套内置主题（`themes.ts` BUILT_IN_THEMES）：** `mirage-console` 任务控制台（默认，DEFAULT_THEME_ID）、`mirage-dawn` 晨蓝（严格等于规范 §2.2 基准值）、`mirage-nordic` 冷杉、`mirage-ember` 暖沙、`mirage-matcha` 抹茶、`mirage-ink` 玄墨。设置页外观区以 2×2 色板卡片预览（`.theme-card`）。

**新主题门槛（`themes.ts` 头注 §2.6）：** light/dark 各提供全 26 token 值集；状态四色保持语义 hue 且四色相互可区分；对比度达标——正文/文本 ≥4.5:1（surfaces 契约 Constraints），状态色 UI 边界 ≥3:1（STATUS_LIGHT 校准注释）；语义封闭映射不变。机制与 golden/契约测试（`ui/app/test/theme.test.ts`、`theme-manager.test.ts`、`style-scan.test.ts`）不得为新增主题改动。

## Do's and Don'ts

### 正确扩展（新增组件/主题的操作规程）
- **Do** 新组件的一切颜色经 `var(--mir-*)` 原始值或 26 个语义 token，需要衍生态时用 `color-mix(in oklab, var(--token) N%, ...)`；组件局部自定义属性只允许别名语义 token（先例：`WallDisplay.tsx` 的 `--tile-color: var(--success)`）。
- **Do** 新控件复用行阶梯与字号阶（高度 24/28/34px，字 12/13/14px），新间距走 4 基 `--mir-space-*`，不引入新档位。
- **Do** 新动效消费 `--mir-dur-*`/`--mir-ease*`/`--mir-lamp-*`；状态指示类复用 `lamp-on`/`lamp-blink`，并确认 reduced-motion 下退化为瞬时。
- **Do** 新主题在 `themes.ts` 增加完整 light/dark 值集并通过上文「新主题门槛」；不改 `schema.ts` 的 L2 清单与 `styles.css` 结构。
- **Do** 改完跑锁定测试：`ui/app` 下 `npx vitest run test/style-scan.test.ts test/theme.test.ts test/theme-manager.test.ts`。

### Don't:
- **Don't** 在 `styles.css` 或组件源码出现十六进制/`rgb()` 颜色字面量——`style-scan.test.ts` 会逐文件拒绝（唯一例外：外观页主题预览 swatch 的内联展示）。
- **Don't** 把蓝线证据色（evidence-highlight）或状态四色用作行动按钮/主 CTA；行动色只有琥珀。
- **Don't** 引入网络字体或第三种字族；仪表刻字一律 Saira wdth 62.5 caps，遥测一律 Chivo Mono。
- **Don't** 发明第三档阴影、外发光、霓虹辉光或玻璃拟态——本世界是非霓虹材料的控制台。
- **Don't** 在主题值集里携带影响布局的属性（radius/shadow 之外的一律不放），也不要让主题切换产生位移。
- **Don't** 动效绕过 motion token 写死 duration/easing，或新增不随 `prefers-reduced-motion` 降级的动画。
