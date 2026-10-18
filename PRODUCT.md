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
- 原生引用最多4段、原文合计8KiB；草稿与编码后引用共同受16KiB提交上限，拒绝保留草稿。ACK按引用实例清除本次提交项，保护新草稿/新引用与重新引用。上下文圆环与详情展示最近成功请求的模型输入Token / 显式配置窗口预算，只接受Mira的Exact / ProviderReported用量；工具循环取最终回复调用输入，不累计调用或计入输出，不从草稿/bytes估算。默认窗口未知，缺用量或分母显示未知；已知零显示0%，超额保留原始数字/百分比、图形钳制100%。失败/取消保留此前成功值，新成功缺用量重置未知，旧序列不覆盖新值；用量快照随会话保存，切换、重连和服务重启后仍可查看，且不受最近40轮历史截断影响；旧文件缺少快照时显示未知（M6-06/43、DEC-036）。支持同一消息拖选引用；跨消息选择、自动滚动与链接打开尚不支持；EUI连续CJK间距临时经公共DSL Adapter修正，上游保守换行保留（DEC-035、EUI-20261004-004）。
- 设置 → 模型提供服务 origin、API 路径、模型 ID、直接 API Key 输入、可选上下文窗口预算（Token）与 Responses / Chat Completions 协议。窗口预算留空为未知，非零整数范围2048–2000000；保存后的配置同时映射Mira ProfileLimits，未配置保留既有Mira默认运行预算，UI不宣称自动发现供应商窗口。保存经 model.set 合并写入服务配置并应用；活动轮次时忙拒绝，失败保留已应用模型。API Key 默认遮蔽，保存在系统钥匙环，普通配置仅保存引用；已存 Key 不回传，留空保留、显式移除后保存清除。旧环境变量配置保留兼容读取；composer 显示服务确认的模型，未保存表单不会冒充已应用配置。
- Agent 模式入口经 session.chat(agent=true) 使用 MiraRuntime、ModelGateway、公开 ConversationLoop 与规范工具回填。Linux 会话按实际 Provider 注册结构化观察、完整键鼠及 wait，修改仍经共享权限；显式开启逐模型“识图”后可授权截图并逐请求供给 PNG。修改后强制新观察，按预先声明的局部后条件核验；工具卡区分派发、已核验、未验证、不确定与拒绝，显示目标和证据。M7-08 的真实 X11 激活/回显/提交落盘三类任务已验收，受控 MiniMax 在线选号已验，真实语义表单、其他模型、用户宿主与 Windows/Wayland 仍待验；OCR/SoM 已交付 Linux 首增量；会话不暴露 workflow 执行工具。
- RuntimeBridge 经 Executor 管理有界 IPC 请求与事件投递，服务断开可在模型页重新连接；退出 UI 停止自身 IPC 与 Executor，不关闭外部已有 Runtime Service。真实流式预览、等待动效与用时已接入；统一入口/托盘与活动退出确认仍待后续交付。
- 2026-10-05 按维护者要求删除旧 TS/CEF、npm 工具链与 Web 调试桥；原生前端继续消费版本化 Local IPC，C++ golden 和服务测试保留。后台工作由 Mira Executor 管理。
- 平台与供应商验收：Linux X11/XWayland 为当前验收范围；SiliconFlow和MiniMax真实文字/规范工具循环与提前流式预览通过，SNI已升级复验；Linux XIM/IBus候选光标跟随与中文提交通过。Windows 真机窗口/IME与物理高DPI未验收，原生 Wayland 只读 ScreenCast 单帧由 DEC-073 独立验收，输入未交付。
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

### 默认 OCR 与编号图（M7-13 / M7-14）

Linux 原生产品的 `native-debug/release` 预设默认封装已验证的 Mirador、ncnn CPU 和
PP-OCRv5 mobile 资源，首次启动自动加载，无需填写模型清单或安装推理环境。资源仅在
构建期按固定提交和哈希获取，运行期离线使用；包内包含模型、字典和许可证。
自定义资源可在「设置 → 桌面工具」保存并重启托盘，显式空清单保持关闭。headless
开发服务仍用 `mirage-service --ocr-manifest PATH` 显式启用。默认增加
`desktop.vision.ocr`，并为 visual observation 接入单显示器 PP-OCR 管线。
正式 pin 的公开语料字符/位置复验 4/4，见
[正确性记录](docs/compatibility/real-ocr-correctness-20261009.md)；默认包供应链见
[资源审计](docs/supply-chain/bundled-vision-20261011.md)，资源格式见 [DEC-057](docs/decisions/DEC-057-real-ocr-composition.md)。

支持图片的模型同时获得 `desktop.vision.mark`，同源编号图可按全局像素 ROI 裁剪。
允许桌面修改且 Backend 支持完整输入时获得 `desktop.vision.click`：使用本轮 scope、
编号、epoch 和窗口 ID，点击前核对新截图、几何与前台，动作后强制新观察。
编号不声明交互语义；严格像素核对会拒绝动画。契约见
[DEC-058](docs/decisions/DEC-058-scoped-visual-marks.md)，范围见
[验证记录](docs/compatibility/scoped-visual-marks-20261010.md)。

明确未派发的参数拒绝可以在重新观察后修正；错误回执会指出字段名，并保留可解析的
action_id。历史中的失败卡片不会把后续已验证的纠正再次判失败。编号超预算时按提示
缩小 ROI；空编号不能作为目标。未知或不确定输入仍需核验/接管，详见
[恢复验收](docs/compatibility/desktop-rejection-recovery-20261011.md)。

Linux X11 Backend 另支持 `desktop.vision.scroll` / `desktop.vision.drag`（M7-09）：
滚动使用当前编号及两轴离散步数，总量最多 16；完整左键拖拽使用同 scope 起点/
终点编号，最多 2048 像素、16 段、1 秒，取消或超时仍释放自有按钮。继续要求
新截图重验和固定后条件；派发不等于任务成功。首增量及其他平台限制见
[验收记录](docs/compatibility/bounded-scroll-and-drag-20261010.md)。
原生 GTK 滚动与分隔条效果已[另验](docs/compatibility/gtk-pointer-effects-20261010.md)；
原生列表 XDND 已在系统 XRes 可用时通过[正负验收](docs/compatibility/x11-native-dnd-20261010.md)。
缺少 XRes 时安全拒绝；用户 Xorg 的滚动、重排、禁用与取消已分别
[取证](docs/compatibility/user-host-gtk-pointer-20261011.md)，间歇守卫拒绝原因与其他平台仍待验。

动作可预先声明 `verify_kind=visual_text_exists` 与 `expected_text`，随后用新截图/OCR
核验目标窗口内唯一文本命中（confidence ≥ 0.8）；`desktop.action.verify` 只读重验
原条件，不重新输入。受控真实 Xvfb Canvas 四类文本与画面不变负例 5/5，契约见
[DEC-059](docs/decisions/DEC-059-visual-text-postconditions.md)，范围见
[Canvas 验收](docs/compatibility/visual-text-canvas-20261010.md)。用户宿主与无闭合
轮廓图标装配仍待验；本地模板与在线选号增量见下文。

无文字闭合区域可在清单中显式设置 `"geometry_marks": true`（缺省关闭），让
`desktop.vision.mark` 复用 Mirador 几何提案生成编号。只接受面积不超过整帧 1/4 的
几何候选，不赋予按钮/交互语义；普通 OCR 和业务后条件仍使用原路径。受控 Xvfb
两个无文字目标与不重绘负例已验，见
[DEC-060](docs/decisions/DEC-060-geometric-visual-marks.md)和
[几何编号验收](docs/compatibility/geometric-visual-marks-20261010.md)。
可另在清单中配置审核过的 `templates`，以本地 BGRA8 patch 匹配闭合几何候选。
编号映射中的 `template_id` 表示唯一外观匹配；点击仍使用当次 scope/编号。受控 Xvfb
双模板、位置交换与不重绘负例已验，配置见
[资源说明](docs/supply-chain/visual-template-resources-20261010.md)，边界见
[DEC-061](docs/decisions/DEC-061-geometric-template-resources.md)和
[模板验收](docs/compatibility/geometric-template-resources-20261010.md)。
MiniMax-M3 已实际从公共编号图区分两个图案，位置交换和不重绘门禁 3/3，见
[在线 Canvas 验收](docs/compatibility/online-visual-canvas-20261010.md)。候选没有模板
身份提示；不重绘时宿主拒绝成功回答。无闭合轮廓图标、用户宿主、跨主题/尺度与
其他在线模型仍待验。


高分辨率显示器上的小窗口可在清单显式设置 `"roi_capture": true`（缺省关闭）：
窗口范围或 mark crop 在采集前生效，编号、全局坐标和点击前像素重验使用同一 ROI。
owner 可为后条件装配局部 OCR，保留物理显示器身份与原预算；默认全屏路径不变。
契约见 [DEC-063](docs/decisions/DEC-063-regional-visual-pre-capture.md)，
[4K 局部验收](docs/compatibility/regional-visual-capture-20261010.md)单独记录证据。
该范围不声明全屏 4K、任意 DPI、跨屏窗口或原生 Wayland 支持。


2026-10-11（M7-12 / DEC-066）：设置增加「桌面工具」，展示真实编译支持、管线运行状态和
待重启配置；提供本地 JSON 清单选择、保存与重新读取。产品托盘显式探测 X11/AT-SPI2/应用/
通知 Provider；Provider 连接失败保持缺失，不以 UI 入口宣称可用。会话权限提示更新为只读
观察、默认按服务权限操作。启动参数只传给新托盘，旧托盘拒绝新覆盖；显式空清单可恢复
资源错误导致的启动失败。该阶段的显式 OCR 装配现由 M7-13 默认固定包扩展；
Linux 原生构建自动链接 OCR 并随包交付模型/字典，运行不下载。

Linux 产品启用预览已通过独立检查 23/23，实际原生 MiniMax-M3 会话完成 OCR/编号图
调用；OCR OFF 构建拒绝和恢复 11/11。范围、失败历史和复现条件见
[产品启用验收](docs/compatibility/product-desktop-vision-20261011.md)；M7 其他门禁保持。

2026-10-11（M7-11 / DEC-067 首增量）：已有 workflow CLI/IPC 可运行本地语义知识，
每次重新观察/唯一解析、权限判定和动作后新验证，已知步骤不依赖模型逐次定位。
失败可交为 waiting_agent；当前不自动恢复，不把进程内版本目录当成跨重启存储。
原生会话选择留在后续范围，见[使用示例](docs/examples/local-semantic-workflow.md)。

2026-10-11（M7-11 第二增量）：workflow 新增 `desktop.knowledge.visual_click`，
保存窗口标题、页面文字与 OCR/模板目标，每次获取新证据、唯一解析、一次点击并验证
新 OCR 后条件。Linux 默认视觉包可直接承载 OCR；应用模板沿用已校验资源清单。
已知步骤无需模型请求；原生会话自动选择、恢复及跨重启资产恢复仍待推进。
见[操作示例](docs/examples/local-visual-workflow.md)与[验收](docs/compatibility/local-visual-workflow-20261011.md)。

2026-10-11（M7-13 / M7-14）：固定视觉包默认启动、32 MiB 高 DPI 采集与 1600 边只读
预览、4096 节点语义树及明确无副作用拒绝恢复已接入。观察和工具参数仍需符合范围、
权限与时效；截图不生成 @e 引用，不确定副作用保持封锁。结果与实际限制见
[验收](docs/compatibility/bundled-vision-and-tool-recovery-20261011.md)。

2026-10-11（DEC-071 / M7-14 第三增量）：修复缺失旧凭据清理误报。Linux GNOME
默认钥匙环为临时 session 时，新密钥保存到已枚举且解锁的 login 集合；不存在/锁定
则失败，不写文件或临时集合。读取失败不等于供应商拒绝密钥；已有临时凭据不自动迁移，
维护者在新版设置重新保存一次。会话新增应用启动与固定应用 HTTP(S) URL 交付，沿
application.launch 权限、取消和动作验证执行；进程局部验证不冒充网页加载或任务成功。

2026-10-11（DEC-072 / M7-15）：会话新增绑定截图来源的区域点击及快捷能力不可达后的
GUI 路线指导；显示器上下文可操作窗口之外的启动器/菜单，窗口上下文保留前台边界。
区域由执行前新截图核对，明确区分局部界面推进和目标页面完成。沿用默认视觉包、权限
和现有工具过程面，不增加环境配置。离线工具链验证不能代替在线模型自主选路验收。

2026-10-11（DEC-073 / M7-10 首增量）：原生 Wayland 默认授权单帧进入现有 Mira 图片
通道，32 MiB 原图、1600 边预览；共享结束即关闭会话。不暴露 XWayland window/input
作为全桌面能力，不将未映射图片转换为可执行引用。真实 GNOME 3200×2000 单帧已验，
键鼠、持续会话、多屏映射与在线浏览器任务保持开放。

2026-10-11（DEC-074 / M7-10 第二增量）：一次成功共享可用于当前任务最多八次新观察，
任务结束即关闭；每次截图重新检查权限，撤销/取消/接管后不复用旧授权。应用候选在
派发前及权限等待后本地核对，派发前拒绝显示具体恢复方向。真实 GNOME 连续读屏已验，
RemoteDesktop 键鼠和完整网页任务仍开放；用户无需新增环境配置。

2026-10-11（DEC-075 / M7-10 第三增量）：原生 Wayland 的 `desktop.vision.ocr({})`
默认分析本任务共享的新图，使用固定 PP-OCR/ncnn；支持文本模型，无需显示器/窗口 ID
或用户配置推理环境。只读预览坐标不提供键鼠能力。OCR 与截图共用共享和八帧预算，
取消/接管关闭；中文任务的可见回复按中文输出。原浏览器业务仍待在线验收。

2026-10-11（DEC-076）：原生 OCR 总量超限改为明确的部分结果；不截断单段文字，
保留总量/区域数与覆盖标志。共享流超时仍失败并关闭，追加有限阶段诊断。

2026-10-11（DEC-077 / M7-10 第五增量）：原生 Wayland 增加任务内 RemoteDesktop
键鼠与 ScreenCast 同源读屏。Agent 按任务显式请求 interaction=true，一次系统授权后
可用图像绑定的点击、组合键、printable ASCII 文本和滚动。
当前传输的非 ASCII 在派发前明确拒绝；输入法行为和静态画面取图仍有平台限制。取消必须释放，释放失败拦截后续修改；
工具卡显示“授权共享桌面 / 局部已核验”，不把释放/新图事实显示为网页业务已完成。
只读任务维持 ScreenCast，无新增用户推理环境配置；GNOME 需解锁及允许系统控制。
系统共享窗口内先打开“允许远程交互／远程控制”，再点共享；英文状态实机 11/11，
完整 ASCII URL、Enter、点击、滚动及接管通过，其他输入法与静态画面继续验收。
目标平台与实机边界见 [验收](docs/compatibility/wayland-task-input-20261011.md)。

2026-10-11（DEC-078 / M7-10 第六增量）：设置中的「桌面权限」支持授权并记住、
忘记和刷新。系统钥匙环保存单次恢复令牌，独立任务/重启重新建立共享流，旧图与
坐标引用不跨任务复用。用户仍可通过系统停止共享或撤销权限。只读任务恢复已记住
授权时不开放输入。默认不开启记住；本轮完成自动协议与产品回归，GNOME 免弹窗
恢复仍待维护者实际授权，不能显示成已通过实机验收。见 DEC-078 与对应兼容性记录。

2026-10-11（M6-38）：按维护者澄清，桌面授权主入口位于会话输入框的权限按钮菜单。
直接授权并记住、忘记、刷新，与设置页同步状态；系统授权与每轮只读限制独立判定。

2026-10-12（DEC-080、DEC-081）：输入框访问模式为对话、视觉、完全访问，分别
限定纯对话、桌面观察和已授权修改。选择与授权操作联动，运行/等待不能改正在
执行的模式。真实工具集合和 OCR 新引用回流模型；静态最新帧不等于动作后新帧。

2026-10-12（M6-38 界面澄清）：会话访问菜单只保留对话、视觉、完全访问三个选项，
选中后收起；移除该菜单的桌面授权设置与说明。系统授权联动保留，管理入口在设置页。

2026-10-12：会话访问菜单位于访问按钮正上方，左对齐、间隔 4px，保持三种模式。
MiniMax-M3 默认开启独立思考，用户可以显式关闭；正文与折叠思考分开。桌面任务
具有独立的整体期限，避免多步任务受单次模型请求期限限制。
