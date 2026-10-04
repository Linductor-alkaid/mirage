# Product

<!-- impeccable:product-schema 1 -->

## Platform

native desktop（Linux / Windows）

2026-10-03 起按 DEC-033 迁移到 EUI-NEO 原生独立窗口，首步入口 apps/native / mirage-native。当前已接入独立 Runtime Service 的真实 IPC、模型设置与通用 Agent harness（M6-03、DEC-034）；整体入口与托盘生命周期仍由 M6-04 推进。现有 ui/app + CEF 实现保留为 legacy，对应的旧设计规则只适用于该实现。

## Users

- 主用户：在自己 PC（Linux / Windows 桌面）上使用通用 Agent（Mira）完成真实工作的个人用户——让 Agent 进入他们的真实桌面环境执行任务、同时保留监督与控制权。
- 次用户：配置与维护 Agent 能力（模型、技能、MCP、权限）的高级用户；同一界面内完成，无独立管理端。

（来源：`docs/design/Mirage：Linux - Windows 桌面端设计方案.md` 产品目标节；推断项：无。）

## Product Purpose

Mirage 是基于 Mira 构建的桌面端产品，为 Mira 通用 Agent 提供完整的 PC 运行环境、桌面交互能力与产品界面。核心闭环：用户在 Mirage 提交任务 → Mira 规划并产生行为 → Desktop Environment 执行桌面动作 → Desktop Observation 回流 → Mira 验证并继续。成功意味着：用户信任 Agent 在自己的桌面上动手做事——每一步可见、可查、可批、可停。

## Positioning

唯一耦合面是 Local IPC 契约：UI 是独立进程，仅经版本化 wire 协议（v1）与 Runtime Service 通信（DEC-006 决策 1/2）。与"浏览器里的 chat 网页"不同，Mirage 掌握完整桌面 Provider（应用/窗口/无障碍/截屏/输入/剪贴板/文件/进程/通知），能给出结构化桌面观察（Semantic + Visual 快照）并让用户以 ElementReference 精确指向屏幕对象——这是纯网页 chat 产品无法真实复制的能力。

## Operating Context

- 单机桌面应用：托盘常驻、全局快捷键、紧急停止（Human Takeover：阻止新自主动作、收敛进行中输入、恢复前重新观察）。
- 事件驱动：hello 握手（五态 host）、task.submit/inspect、事件订阅（seq 单调、有界队列 drop-oldest + overflow 事件）；服务端可能不支持事件订阅（降级轮询）。
- 会话页是 harness 主界面：SessionsSidebar + ThreadView（消息流 + 活动卡 + 批准卡）+ Composer（对话/执行双模式）+ RunDrawer（运行时间线 + 观察流）。
- 另有工作流页（库/编辑器/运行三联）、资源页（M2+ 占位）、设置页（常规/外观/模型/记忆/技能/MCP/权限/运行时 八类）与全局层（状态栏、命令面板 Ctrl+K、通知/批准中心、紧急停止）。
- 开发与验收以 mock transport 先行（以 M1 真实服务 wire 行为为模板），契约由 golden vectors 双端锁定。

（来源：`docs/design/Mirage 前端设计规范与信息架构.md` §3、`docs/design/mirage-ipc-protocol-v1.md`、`ui/README.md`。）

## Capabilities and Constraints

- 新原生前端：C++20 + pinned EUI-NEO dev（GLFW/OpenGL），无 Chromium；依赖只在 UI 层使用，不使用 EUI async/network/audio 承载业务并发。
- 当前原生界面：新建/切换服务会话、独立草稿、随文字增高的16px多行输入、中文粘贴、普通对话/Agent模式、真实发送/回复/停止状态、Markdown回复、整条复制、文字引用与可检查来源/逐条删除的引用弹层、真实上下文容量、滚动、明暗、侧栏调宽与收起、窗口控制。UI 最多显示 24 个会话、每会话最近 40 轮（80 条消息），草稿最多 16 KiB；草稿、主题与侧栏宽度只保留在当前 UI 进程。会话历史由服务持有，服务重启恢复的旧会话只恢复历史，新轮次使用本次服务的活动会话。
- 原生引用最多4条、原文合计8KiB；草稿与编码后引用共同受16KiB提交上限，拒绝保留草稿。ACK按引用实例清除本次提交项，保护新草稿/新引用与重新引用。上下文圆环与详情展示最近成功请求的模型输入Token / 显式配置窗口预算，只接受Mira的Exact / ProviderReported用量；工具循环取最终回复调用输入，不累计调用或计入输出，不从草稿/bytes估算。默认窗口未知，缺用量或分母显示未知；已知零显示0%，超额保留原始数字/百分比、图形钳制100%。失败/取消保留此前成功值，新成功缺用量重置未知，旧序列不覆盖新值；服务重启旧历史的用量未知（M6-06、DEC-036）。精细框选与链接打开尚不支持；EUI连续CJK间距临时经公共DSL Adapter修正，上游保守换行保留（DEC-035、EUI-20261004-004）。
- 设置 → 模型提供服务 origin、API 路径、模型 ID、凭据环境变量名称、可选上下文窗口预算（Token）与 Responses / Chat Completions 协议。窗口预算留空为未知，非零整数范围2048–2000000；保存后的配置同时映射Mira ProfileLimits，未配置保留既有Mira默认运行预算，UI不宣称自动发现供应商窗口。保存经 model.set 合并写入服务配置并应用；活动轮次时忙拒绝，失败保留已应用模型。API Key 从 Runtime Service 的环境读取，配置不保存密钥；composer 显示服务确认的模型，未保存表单不会冒充已应用配置。
- Agent 模式入口经 session.chat(agent=true) 使用 MiraRuntime、ModelGateway 和 Mira 自带 wait 工具；当前不提供截图、桌面工具、RPA 或 workflow。通用循环暂位于 ModelLayer Adapter，能力反馈为 MIRA-20261004-001；未直接调用设备 AgentLoop。
- RuntimeBridge 经 Executor 管理有界 IPC 请求与事件投递，服务断开可在模型页重新连接；退出 UI 停止自身 IPC 与 Executor，不关闭外部已有 Runtime Service。流式回复、统一入口/托盘与活动退出确认仍待后续交付。
- 旧 Web 前端保留其 TS 契约镜像、主题与真实 IPC 面。原生迁移继续消费版本化 Local IPC，后续所有后台工作由 Mira Executor 管理。
- 平台与供应商验收：Linux X11/XWayland 为当前验收范围；SiliconFlow 真实文字/工具循环已通过。MiniMax 受 pinned TLS SNI 缺口影响（MIRA-20261004-002）；Windows 真机窗口/IME 与流式回复未验收，不宣称原生 Wayland 支持。
- 当前原生会话基线按用户指定的 ZCode 整页参考：中性明暗背景、672px居中空态、右对齐用户气泡、无卡片Markdown回复、最大896px活动阅读列与16px圆角增长composer，工具顺序为加号/模式/模型/上下文/发送。Linux原生截图与ZCode源码/布局对齐已复核，两项指定修正评分均resolved；运行中Wayland ZCode截图因ScreenshotWindow AccessDenied未完成像素对照。旧 mission-console 风格不约束新原生页面。

## Brand Commitments

- 名称：Mirage；原生界面使用 Mira 原始角色图与 "Mirage" 字标，保留已记录资源来源；legacy Web 的 "M" + "Mirage 控制台" 仅描述其旧实现。
- 界面文案语言：简体中文为主（现有视图与设置页均为 zh-CN；推断项：维持 zh-CN 为默认）。
- 当前视觉要求：用户指定参考 ZCode 的对话页面；既往 Web 前端的风格化要求仅作为历史，不覆盖这次原生方向。

## Evidence on Hand

- 原生当前事实：`apps/native/app.cpp`、`chat_model.cpp`、`runtime_bridge.cpp`；设计与边界见 `docs/design/native-agent-frontend.md`、DEC-034/035/036，原生视觉系统见 `apps/native/DESIGN.md`。

- 上下文占用证据：`docs/compatibility/native-context-usage-20261004.md`、`.impeccable/review/native-context-finish-verdict.md` 与 `.impeccable/review/native-context-live-usage.json`。Linux正常/最小窗口明暗及未知态已复核；真实SiliconFlow请求391输入Token / 显式测试预算128000，UI显示0.3%，不代表供应商窗口自动发现。

- 设计规范与 IA：`docs/design/Mirage 前端设计规范与信息架构.md`（拓扑树、路由表、会话页/工作流页/设置页构成、全局层）。
- 协议事实源：`docs/design/mirage-ipc-protocol-v1.md` + `tests/runtime/data/ipc_protocol_golden.json`。
- 现有实现：`ui/app`（shell/workspace/tasks/execution/appearance 视图、主题系统、StatusBadge/StepCard/ActivityCard/ApprovalCard/Composer 组件）——作为产品事实证据，视觉上按用户要求整体替换。
- 无真实用户数据/测试imonial/截图素材；演示数据一律以 mock 生成，不得虚构商业声明。

## Product Principles

1. **可信优先（Operate）**：监督感高于表达欲——状态、证据、批准、停止永远一级可达；表达性让位于可扫读性，品牌活在精确的细节里。
2. **过程透明**：Agent 的每一步（规划、工具调用、桌面观察、验证）以结构化活动流呈现，可折叠可回放，不黑箱。
3. **控制常在**：对话/执行双模式、批准中心、紧急停止是产品骨架而非附加功能。
4. **契约先行**：视觉与交互可超前于 IPC 面演进，但一律 mock 先行、契约锁定，不制造平行事实源。
5. **平台克制**：不出现 Linux/Windows 平台细节；壳差异由 Platform Backend 吸收。

## Accessibility & Inclusion

- 遵循设计规范 §2.4：动效尊重 `prefers-reduced-motion`；对比度门槛（已按 WCAG 校准状态四色）。
- 键盘可达：命令面板、快捷键体系（桌面应用形态的既有预期）。
- 无产品特定的无障碍强制标准记录；对比度校准值见 `ui/app/src/theme/`。
