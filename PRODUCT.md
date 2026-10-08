# Product

<!-- impeccable:product-schema 1 -->

## Platform

native desktop（Linux / Windows）

2026-10-03 起按 DEC-033 迁移到 EUI-NEO 原生独立窗口，首步入口 apps/native / mirage-native。当前已接入独立 Runtime Service 的真实 IPC、模型设置与通用 Agent harness（M6-03、DEC-034）；整体入口与托盘生命周期仍由 M6-04 推进。2026-10-05 按 DEC-037 删除旧 ui/app、CEF 和 Web devbridge 的源码、构建及打包路径；现行前端仅为原生 EUI，旧设计文档保留为历史。

## Users

- 主用户：在自己 PC（Linux / Windows 桌面）上使用通用 Agent（Mira）完成真实工作的个人用户——让 Agent 进入他们的真实桌面环境执行任务、同时保留监督与控制权。
- 次用户：配置与维护 Agent 能力（模型、技能、MCP、权限）的高级用户；同一界面内完成，无独立管理端。

（来源：`docs/design/Mirage：Linux - Windows 桌面端设计方案.md` 产品目标节；推断项：无。）

## Product Purpose

Mirage 是基于 Mira 构建的桌面端产品，为 Mira 通用 Agent 提供完整的 PC 运行环境、桌面交互能力与产品界面。核心闭环：用户在 Mirage 提交任务 → Mira 规划并产生行为 → Desktop Environment 执行桌面动作 → Desktop Observation 回流 → Mira 验证并继续。成功意味着：用户信任 Agent 在自己的桌面上动手做事——每一步可见、可查、可批、可停。

## Positioning

唯一耦合面是 Local IPC 契约：UI 是独立进程，仅经版本化 wire 协议（v1）与 Runtime Service 通信（DEC-006 决策 1/2）。与"浏览器里的 chat 网页"不同，Mirage 掌握完整桌面 Provider（应用/窗口/无障碍/截屏/输入/剪贴板/文件/进程/通知），能给出结构化桌面观察（Semantic + Visual 快照）并让用户以 ElementReference 精确指向屏幕对象——这是纯网页 chat 产品无法真实复制的能力。

## Operating Context

- 当前原生进程经 Local IPC 连接 Runtime Service，显示会话历史、真实通用 harness 回复、运行/停止状态和上下文用量。
- 会话页为侧栏、消息区与输入栏；设置页提供外观和模型配置。工作流、资源、技能、MCP、批准中心及全局紧急停止仍属于后续产品范围。
- `mirage start` 启动服务、独立原生窗口与托盘。活动会话退出确认和统一生命周期尚由 M6-04 实现验收；启动入口不等于这一闭环已完成。
- IPC v1 的 C++ golden 与运行时测试锁定契约，UI 只展示服务确认或明确标记未知的状态。

（现行来源：`apps/native/README.md`、`docs/design/native-agent-frontend.md`、`docs/design/mirage-ipc-protocol-v1.md`；目标架构见桌面端总体设计与 M6 计划。）

## Capabilities and Constraints

- 新原生前端：C++20 + pinned EUI-NEO dev（GLFW/OpenGL），无 Chromium；依赖只在 UI 层使用，不使用 EUI async/network/audio 承载业务并发。
- 当前原生界面：新建/切换服务会话、独立草稿、随文字增高的14EM多行输入、中文粘贴、通用 Agent harness、真实发送/回复/停止状态、Markdown回复、整条复制、文字引用与可检查来源/逐条删除的引用弹层、真实上下文容量、滚动、明暗、侧栏调宽与收起、窗口控制。UI 最多显示 24 个会话、每会话最近 40 轮（80 条消息），草稿最多 16 KiB；草稿、主题与侧栏宽度只保留在当前 UI 进程。会话历史由服务持有；服务重启后首次harness提交延迟打开当前Mira会话，继续使用稳定历史ID。
- 原生引用最多4段、原文合计8KiB；草稿与编码后引用共同受16KiB提交上限，拒绝保留草稿。ACK按引用实例清除本次提交项，保护新草稿/新引用与重新引用。上下文圆环与详情展示最近成功请求的模型输入Token / 显式配置窗口预算，只接受Mira的Exact / ProviderReported用量；工具循环取最终回复调用输入，不累计调用或计入输出，不从草稿/bytes估算。默认窗口未知，缺用量或分母显示未知；已知零显示0%，超额保留原始数字/百分比、图形钳制100%。失败/取消保留此前成功值，新成功缺用量重置未知，旧序列不覆盖新值；服务重启旧历史的用量未知（M6-06、DEC-036）。支持同一消息拖选引用；跨消息选择、自动滚动与链接打开尚不支持；EUI连续CJK间距临时经公共DSL Adapter修正，上游保守换行保留（DEC-035、EUI-20261004-004）。
- 设置 → 模型提供服务 origin、API 路径、模型 ID、直接 API Key 输入、可选上下文窗口预算（Token）与 Responses / Chat Completions 协议。窗口预算留空为未知，非零整数范围2048–2000000；保存后的配置同时映射Mira ProfileLimits，未配置保留既有Mira默认运行预算，UI不宣称自动发现供应商窗口。保存经 model.set 合并写入服务配置并应用；活动轮次时忙拒绝，失败保留已应用模型。API Key 默认遮蔽，保存在系统钥匙环，普通配置仅保存引用；已存 Key 不回传，留空保留、显式移除后保存清除。旧环境变量配置保留兼容读取；composer 显示服务确认的模型，未保存表单不会冒充已应用配置。
- Agent 模式入口经 session.chat(agent=true) 使用 MiraRuntime、ModelGateway 和 Mira 自带 wait 工具；当前不提供截图、桌面工具、RPA 或 workflow。通用循环复用公开ConversationLoop与规范工具回填，MIRA-20261004-001已升级复验；只注册权限允许的wait。
- RuntimeBridge 经 Executor 管理有界 IPC 请求与事件投递，服务断开可在模型页重新连接；退出 UI 停止自身 IPC 与 Executor，不关闭外部已有 Runtime Service。真实流式预览、等待动效与用时已接入；统一入口/托盘与活动退出确认仍待后续交付。
- 2026-10-05 按维护者要求删除旧 TS/CEF、npm 工具链与 Web 调试桥；原生前端继续消费版本化 Local IPC，C++ golden 和服务测试保留。后台工作由 Mira Executor 管理。
- 平台与供应商验收：Linux X11/XWayland 为当前验收范围；SiliconFlow和MiniMax真实文字/规范工具循环与提前流式预览通过，SNI已升级复验；Linux XIM/IBus候选光标跟随与中文提交通过。Windows 真机窗口/IME与物理高DPI未验收，不宣称原生 Wayland 支持。
- 当前原生会话基线按用户指定的 ZCode 整页参考：中性明暗背景、672px居中空态、右对齐用户气泡、无卡片Markdown回复、最大800px活动阅读列与16px圆角增长composer，工具顺序为附件/访问权限/上下文占比/模型/思考深度/发送。Linux原生截图与ZCode源码/布局对齐已复核，两项指定修正评分均resolved；运行中Wayland ZCode截图因ScreenshotWindow AccessDenied未完成像素对照。旧 mission-console 风格不约束新原生页面。

## Brand Commitments

- 名称：Mirage；原生界面使用 Mira 原始角色图与 "Mirage" 字标，保留已记录资源来源；legacy Web 的 "M" + "Mirage 控制台" 仅描述其旧实现。
- 界面文案语言：简体中文为主（现有视图与设置页均为 zh-CN；推断项：维持 zh-CN 为默认）。
- 当前视觉要求：用户指定参考 ZCode 的对话页面；既往 Web 前端的风格化要求仅作为历史，不覆盖这次原生方向。

## Evidence on Hand

- 原生当前事实：`apps/native/app.cpp`、`chat_model.cpp`、`runtime_bridge.cpp`；设计与边界见 `docs/design/native-agent-frontend.md`、DEC-034/035/036/037，原生视觉系统见 `apps/native/DESIGN.md`。

- 上下文占用证据：`docs/compatibility/native-context-usage-20261004.md`、`.impeccable/review/native-context-finish-verdict.md` 与 `.impeccable/review/native-context-live-usage.json`。Linux正常/最小窗口明暗及未知态已复核；真实SiliconFlow请求391输入Token / 显式测试预算128000，UI显示0.3%，不代表供应商窗口自动发现。

- 设计规范与 IA：`docs/design/Mirage 前端设计规范与信息架构.md`（拓扑树、路由表、会话页/工作流页/设置页构成、全局层）。
- 协议事实源：`docs/design/mirage-ipc-protocol-v1.md` + `tests/runtime/data/ipc_protocol_golden.json`。
- 旧 Web/CEF 实现已按 DEC-037 退役，源码可从 Git 历史回溯；旧前端 IA 仅记录产品演进，不是现行能力证据。
- 现行证据包含 Linux 原生实机截图和获授权本机模型的真实测试回复；测试配置以“验证用配置”标记。无真实用户研究或商业背书；离线夹具与真实供应商结果分别记录。

## Product Principles

1. **可信优先（Operate）**：监督感高于表达欲——状态、证据、批准、停止永远一级可达；表达性让位于可扫读性，品牌活在精确的细节里。
2. **过程透明**：Agent 的每一步（规划、工具调用、桌面观察、验证）以结构化活动流呈现，可折叠可回放，不黑箱。
3. **控制常在**：对话/执行双模式、批准中心、紧急停止是产品骨架而非附加功能。
4. **契约先行**：视觉与交互可超前于 IPC 面演进，但一律 mock 先行、契约锁定，不制造平行事实源。
5. **平台克制**：不出现 Linux/Windows 平台细节；壳差异由 Platform Backend 吸收。

## Accessibility & Inclusion

- 遵循设计规范 §2.4：动效尊重 `prefers-reduced-motion`；对比度门槛（已按 WCAG 校准状态四色）。
- 键盘可达：命令面板、快捷键体系（桌面应用形态的既有预期）。
- 无产品特定的无障碍强制标准记录；现行调色与字号以 `apps/native/DESIGN.md` 和对应 Linux 截图为证据。

2026-10-05（DEC-037）：模型设置采用服务商导航/右侧配置分栏，至多 12 个命名配置，切换需服务确认；凭据仍为服务端环境变量。附件为用户选择的 UTF-8 普通文本，至多 4 个、合计 8 KiB；只读隐藏工具，默认仅 wait，未开放桌面/RPA。思考档位在供应商明确启用 reasoning_effort 后传入每轮请求；默认不传。

2026-10-05（DEC-038）：新建为本地草稿，首次发送后才进入历史；重复新建复用未开始的空白草稿。
历史行提供垃圾桶与删除确认，活动会话须先停止；服务确认并持久化成功后移除，重启不恢复。
模型页直接填写 API Key，存 Linux Secret Service / Windows Credential Manager，普通配置仅存引用；
系统凭据不可用/锁定时明确失败。Linux 首步已验证，Windows 由目标环境另验。

2026-10-05（DEC-039 / M6-11）：会话气泡和Markdown减重，正文16/24；复制仅悬停/键盘聚焦时显示，
移除整条引用按钮，拖选实际渲染文字后弹出引用。最后一条已终结输入支持修改重发；接纳与保存成功
才替换其用户/Agent消息，实际模型输入排除旧轮次，失败拒绝保留原历史。该操作不撤销工具外部副作用。
[验收证据](docs/compatibility/native-conversation-revision-20261005.md)使用显式合成会话和真实Mira请求夹具，
不作为运行中ZCode的像素对比或本轮供应商在线测试。

2026-10-05 / M6-12：原生界面统一使用随应用交付的 Noto Sans SC 简体中文字面，
修正Markdown行内文字高低不齐；字体与OFL许可离线构建/打包。Linux验证见
[字体验收](docs/compatibility/native-typography-20261005.md)。Windows/真实IME待补跑。

2026-10-07（DEC-046 / M6-26）：思考选择只在会话输入栏，选项由服务端根据模型能力提供；
MiniMax-M3 为默认/关闭/开启，M3.1 Flash Preview 为默认及低到最高深度，不能关闭。
设置页不再要求开启思考支持。空的内部会话不计为可恢复历史；默认 24 历史槽及 1 主会话槽，
容量满时明确提示删除历史并保留输入草稿。当前 Linux 原生 MiniMax 开关、流式正文与 wait 工具循环
已验收，参见[记录](docs/compatibility/session-thinking-20261007.md)。


2026-10-08（DEC-051 / M6-33）：侧栏开合采用220ms三次减速，主列随可见宽度连续定位；
明暗主题在240ms内同步插值文字、底色和边界。反向切换从当前值继续，拖动侧栏直接跟手。
外观页“减少动态效果”可直接切换，偏好只在本次运行内保留。
模型页可逐模型声明OpenAI思考能力，Anthropic选项沿用按模型ID核验的配置；保存后会话页
显示服务确认的思考档位。填写Base URL和Key后点击“获取模型”，候选最多256项，点击加入
编辑副本再保存。已存Key只用于完全匹配的服务地址/协议，地址或Key变更会使旧候选失效。
目录请求由Runtime Service的Executor与Mira HTTP传输承载；独立IPC连接隔离网络等待，
会话控制按Executor SerialExecutionContext保持提交顺序。验收见
[验证记录](docs/compatibility/native-motion-model-discovery-20261008.md)。
