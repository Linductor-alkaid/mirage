# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

（渲染形态：桌面应用自有窗口内的嵌入式 Web 前端，壳为 CEF【暂定默认值，DEC-006 决策 3，M3 冻结】；开发期以浏览器形态 + Vite dev server 运行。）

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

- 技术栈：npm workspaces + Vite + TypeScript；`ui/contracts` 为协议 v1 的 TS 镜像（纯 TS 零运行时依赖，golden 测试锁定，**不改**）；`ui/app` 为前端应用。视图层原为 vanilla TS 极简三 tab 壳，本次推倒重写为完整 harness；组件框架/组件素材由本次调研定选并固定（用户已委托）。
- 设计系统：三层 token（L1 primitives → L2 语义值集 → 组件），TS 单一事实源注入 CSS；5 套内置主题 + 明暗三态 + localStorage 持久化是已交付机制（M1.5-08），重写必须保留该机制与 golden/契约测试。
- 契约现状：会话列表/消息流/工作流管理尚无 IPC 面——UI 一律以 transport 接口 + mock 先行，不阻塞视觉与交互定型；执行模式提交 = `task.submit` 已满足最小闭环。
- 平台边界：Agent 行为层与平台解耦（Platform Backend 承载 UIA/AT-SPI2/X11/Wayland），UI 不出现平台细节。
- 明确未决：渲染壳 CEF vs Tauri（M3 PoC）；组件框架原定"实现评审时定选"，本次定选后回写 DEC-006 决策 6 相关文档。

## Brand Commitments

- 名称：Mirage；现有实现用品牌字标 "M" + "Mirage 控制台"（zh-CN 文案）。
- 界面文案语言：简体中文为主（现有视图与设置页均为 zh-CN；推断项：维持 zh-CN 为默认）。
- 用户对视觉的明确要求（本次委托原文）："有特色，风格化，灵动"——即拒绝平庸的通用后台观感，要求风格化、有生命力；允许为个性承担适度的表达性。

## Evidence on Hand

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
