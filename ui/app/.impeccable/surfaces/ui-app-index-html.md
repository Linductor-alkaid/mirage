---
version: 1
slug: "ui-app-index-html"
primary_target: "ui/app/index.html"
related_targets: ["ui/app/src/app.ts","ui/app/src/main.tsx"]
---

## Scope & Mode

Surface: Mirage harness 前端整体（`ui/app`，Web/CEF 桌面应用）。首画面 = 会话页 `#/chat`。Mode: **Operate**。替换世界（旧三 tab 壳整体退役）；保留契约层、主题机制与全部产品语义。

## Audience / Job / Proof

- Audience：让 Agent 在自己 PC 上真实做事的个人用户；场景 = 长时间隔着屏幕监督一个能动桌面的 Agent。
- Job：看清此刻在做什么、下一步动什么；批准/拒止；随时急停；管理会话与设置。
- Proof：结构化的步骤/观察/证据流（契约 task 步骤 + 观察快照），不是氛围。

## Direction（seed key: f19d0aa6，assigned index 5）

「任务控制台」（Apollo-era Mission Control 语系，非霓虹材料）：壁挂屏条（大字状态动词 + 蓝线任务剖面 + 状态灯阵）+ 签派栏 + 线程流 + 观察台 + cue 状态栏。

## THESIS

把会话页造成一间任务控制大厅：壁挂大屏公开任务剖面的真相，控制台分工监督，任何时刻可放行、可急停。它拒绝的品类默认排布：居中聊天列 + 通用侧栏的后台观感。

## OWN-WORLD

炭蓝控制台底（#20262f 系）+ 奶油面板（#f2ead8 系），琥珀为唯一行动/主动读数色（#e6a23c 系），状态四色封闭语义（成功绿/警告琥珀/危险红/运行蓝）；蓝线剖面图蓝（#6ea4bf 系）。组件语言：仪表 caps 标签、灯阵瓦片、警戒条急停、cue 状态栏、蓝线剖面。字体：Saira Condensed（仪表 caps/大数字）+ Chivo Mono（遥测/代码）+ 系统 CJK 正文；全部本地打包。

## STORY

用户明白：这是驾驶舱不是聊天网页——壁挂屏告诉他 Agent 此刻的状态动词与剖面进度，签派栏是他的任务队列，线程流是完整对话与证据，观察台是桌面回流；待批准事项永远在 Dock 与灯阵里闪琥珀；红色警戒钮随时把一切停下。

## FIRST VIEWPORT（`#/chat`）

1440×900：顶壁挂屏条全宽 h≈96px（左 1/3 大字状态动词「执行中」+ 副行任务目标；中 1/3 蓝线剖面图；右 1/3 主机灯阵 4×2）。其下三栏：签派栏 240px（新建按钮、搜索、置顶/今天分组会话条）；线程流中栏（消息组 + 活动卡 + 批准卡）；观察台 320px（运行时间线 + 观察流 + 上下文用量表）。底部 cue 状态栏 28px：transport/seq/host 五态/紧急停止警戒钮。Composer 固定于线程流底部，上方 Dock 区承载待批准事项。

## FORM

assigned direction（MISSION CONSOLE），seed f19d0aa6，code-led。 Memorable moment：「放行」——批准卡出现时壁挂屏对应节点转琥珀闪烁，点击放行后节点锁定为绿、全局金色主动作线完成一次点亮。

## Signature interactions

1. 状态换幕：壁挂屏大字动词随 host/任务状态瞬时切换（快切 + 灯阵快速点亮 80ms / 慢衰减 600ms），不做软淡入。
2. 观察流触发锁定：运行中自由滚动；用户上滚即锁定自动跟随并出现「已锁定 · 回到实时」cue。
3. 蓝线剖面：步骤序列以描线动画（stroke-dashoffset，ease-out-circ）呈现推进。

## Constraints

契约/主题机制/golden 测试不动；mock 先行（会话/消息为 mock 层，需可辨识）；对比度 ≥4.5:1；`prefers-reduced-motion` 全动效降级为瞬时切换；无网络字体（本地打包）。

## Unresolved

用户未应答决策页（结构化工具探测 + 页面等待均无回应）：按 assigned 方向无监督执行；决策页保持开启，用户改选时按重掷处理。

## FINISH

unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance.
