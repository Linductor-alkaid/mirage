# M6：原生前端与桌面进程形态重构

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-06
> 决策：[DEC-033](../decisions/DEC-033-native-agent-frontend.md)
> 前置：既有 M5 服务与 IPC；本轮交付不依赖完整 M5 发布验收。
> 建议发布点：暂不新增发布标签。

## 目标与范围

先提供 EUI-NEO dev 原生窗口和 ZCode 参考的 Agent 对话页面，再逐步迁移真实 IPC
及统一入口/托盘所有权。原生 UI 分步交付 Linux 首步。2026-10-05 维护者明确要求清除旧 TS/CEF 前端，按 DEC-037 退役其源码、构建和打包入口。
不修改 Mira / Mirador，不实现新的 Agent 内核，不把预览当作真实对话。

## 工作项

| 编号 | 状态 | 结果与验收 |
| --- | --- | --- |
| M6-01 | In Progress | EUI dev 完整提交锁定、bundled 构建、原生无边框窗口与窗口控制；Linux 构建/运行已取证，Windows 保持待验收 |
| M6-02 | Completed | Linux 首步窄侧栏与对话页，本地会话/草稿/未发送消息、主题、滚动、容量限制；截图、模型测试与键鼠交互取证通过 |
| M6-03 | Completed | Linux 首步模型设置与通用 harness：真实 session.list/open/chat/history 与事件；Executor 管理请求、停止与断线，离线故障注入和真实SiliconFlow模型验收；Windows与后续扩展另验 |
| M6-04 | In Progress | 独立入口、前端/托盘分进程、退出活动确认与整体 shutdown、打包替换 CEF |
| M6-05 | Completed | 整个会话页对齐ZCode：用户气泡/Agent Markdown、居中草稿与自增高输入、工具条/上下文引用、复制与键盘交互；按DEC-035验收 |
| M6-06 | Completed | Linux首步ZCode上下文比例圆环与详情；接通Mira最近请求输入Token，显式窗口预算，未知状态与IPC兼容测试；依据DEC-036 |
| M6-07 | Completed | Linux首步ZCode服务商配置与附件/权限/占比/模型/思考工具栏；真实UTF-8附件、受限工具权限、模型目录、每轮reasoning与回归；供应商推理互通和Windows另验 |
| M6-08 | Completed | Linux首步删除TS/CEF/Web devbridge，保留原生IPC golden/依赖门禁；更新CI/CLI/打包，DEB提取启动通过；Windows安装及整体生命周期另验 |
| M6-09 | Completed | Linux直接API Key/系统凭据、空草稿延迟会话创建与历史删除；依据DEC-038，覆盖持久化/失败/活动拒绝与真实界面 |
| M6-11 | Completed | Linux会话视觉减重、悬停复制、选中文字引用与最后一轮编辑重发；依据DEC-039，验证服务端上下文替换、失败保留与真实交互 |
| M6-12 | Completed | 随应用交付固定中文字体，统一Markdown行内基线；依据DEC-040，验证混排、输入、最小明暗窗口与打包资源 |
| M6-13 | Completed | Linux输入工具栏模型按钮按文字收紧并限制180px；沿用DEC-035/037，验收短/长名称、最小/正常明暗窗口与选择入口 |
| M6-14 | Completed | 会话顶部显示当前标题，跟随会话切换；沿用DEC-035，验收空草稿、生成标题、历史切换、长标题及侧栏收起 |
| M6-15 | Completed（Linux） | 右侧工具组与ZCode视觉密度；DEC-041，正常/最小明暗与长名称验收 |
| M6-16 | Completed（Linux X11） | 光标附近中文IME定位；DEC-041，依赖公开入口核查/最小补齐及焦点/缩放/多行验证 |
| M6-17 | Completed（Linux） | 真实流式预览、等待动效与用时；DEC-041，终态/取消/失败/背压/关闭验证 |
| M6-19 | Completed（Linux） | 修复EUI Vulkan生命周期探针，四种窗口/渲染组合CI与SDK消费复验；维护者2026-10-05明确授权，沿用DEC-041 |
| M6-20 | Completed（Linux X11） | 在真实原生窗口补验输入框IME、真实服务流式回复与等待计时，修正验收中发现的会话体验问题；沿用DEC-041，维护者2026-10-06再次明确范围 |
| M6-18 | Completed（Linux） | 接入Mira PR#76修复与公开ConversationLoop，保持Executor版本；依赖审计/回填/取消回归 |
| M6-22 | Completed | 会话侧栏/服务名输入间距、厂商预设、Anthropic Messages接入与MiniMax-M3真实识图；DEC-043 |
| M6-21 | Completed（Linux X11） | 侧栏统一内容缩进/图标轴；ZCode服务导航、未命名草稿、连接配置和模型列表；保存ACK、失败保留、真实配置重载验收，依据DEC-042 |
| M6-23 | Completed（Linux X11） | 实际模型页35项交互修复/复验；DEC-044，324项renderer、ACK、断线重连与最小明暗证据 |
| M6-25 | Completed（Linux X11） | 托盘内嵌Runtime、独立前端准入/复用、左键两项菜单、整体退出与活动确认；DEC-045 |
| M6-24 | Completed（Linux X11） | 修复开发desktop入口绕过Service；真实GIO冷启动、Key保存/替换与服务复用验收 |
| M6-28 | In Progress | 原生前端 PR 栈 CI 验收、按依赖合并与已合并分支/过期 build 清理；[维护计划](maintenance-ci-acceptance-20261007.md) |
| M6-10 | Completed | 依工程规范9.4整理Mira/Mirador独立反馈台账，复核已登记问题并向Mira上游提交可复现反馈；不修改依赖或升级pin |

## 测试矩阵与退出条件

- [x] Linux native-debug/native-release 构建与无边框窗口运行。
- [x] Linux 正常/最小窗口、明暗、空会话/消息、键鼠输入和窗口控制证据。
- [x] 容量边界与会话草稿隔离的模型测试，现有回归不退化。
- [ ] Windows 构建及窗口/中文 IME 真机验证。原因：当前 Linux 环境；负责人：维护者；补跑：Windows CMake native preset 与窗口交互验证。
- [x] M6-03 Linux 真实 IPC、异常/拒绝/取消/超时/shutdown 测试。
- [x] M6-07 Linux 模型配置/有界文本附件/每轮权限与推理、正常/最小明暗界面及修正复核。
- [x] M6-08 原生无 Node/CEF 构建、依赖锁门禁与 Linux DEB 提取启动。
- [x] M6-09 Linux直接API Key、系统凭据、草稿首发与历史删除持久化/失败门禁。
- [x] M6-10 依赖独立台账、Mira四项复核/上游回执与可复跑证据。
- [x] M6-11 Linux悬停动作、选段引用、最后轮次编辑与真实Mira上下文替换/失败保留；原生框架交互截图与sanitizer通过。
- [x] M6-12 Linux固定中文字体/Markdown行内基线；正常/最小明暗混排、输入/选段、Release/ASAN与DEB资源验收。
- [x] M6-13 Linux模型按钮按名称收紧；短/长名称、正常/最小明暗、入口交互与Release/ASAN验收。
- [x] M6-14 Linux顶部标题、空草稿/首次消息/历史切换、长名称及侧栏收起；Release/ASAN回归。
- [x] M6-15 正常/最小明暗右侧组与紧凑会话呈现。
- [x] M6-16 Linux XIM光标定位、候选两次移动与真实中文提交；高DPI/其他IM另验。
- [x] M6-17 实时预览、等待/用时、旧客户端兼容及终态/失败/取消/关闭；ASAN/UBSAN/TSAN。
- [x] M6-18 PR#76公开Loop/规范工具/SNI/workflow复验与依赖PR/pin审计。
- [x] M6-19 EUI四项后端CI/SDK消费、Vulkan各33项、本地OpenGL及Mirage Debug/Release/ASAN回归。
- [x] M6-20 真实窗口IME/流式/计时复验，阅读保位/实际返回底部像素回归、空闲退出唤醒；依赖PR/锁与当前head四项CI通过。
- [ ] M6-04 整体生命周期与安装包验收。

## 风险与实施记录

2026-10-05 / M6-10：维护者要求建立两个依赖的反馈台账并先反馈Mira。以既有
DEC-034与工程规范9.4为依据，保留旧反馈编号/链接，核对pinned与上游master、去重已有
issue，补充复现与验收条件后提交。Mirador无已确认缺口时明确为空，不为填表虚构问题；
完成条件为台账、上游回执、复核证据与范围化Git提交同步。

2026-10-03：维护者明确授权 UI 专用 EUI 依赖与 dev 分支。锁定
`4691fc0a5c1fde6f3e22f1ac454ed87c7a17f722`；包含上游 XIM-filtered X11 keys 修复。
本地预览不启动 Runtime Service、不连接模型、不执行桌面动作；退出丢弃本地会话。
原始未提交 M5 修改保留。

同日验收：[完整证据](../compatibility/eui-neo-dev-20261003.md)。Debug/Release 原生目标
通过；模型 134 checks，ASAN/UBSAN 通过；ASAN GUI detect_leaks=1 退出 0，无诊断；
默认 Debug 回归 50/50；公共头边界 48 headers / 0 violations。截图覆盖正常/最小、
明暗、收起、最大化、草稿、清空确认、长消息。实际键鼠验证拖动/缩放与窗口控制。

独立 impeccable 复核 disposition=ship（修正列表范围）：原三项 material fixes
（旧文档作用域、modal 键盘/清空目标保护、移除 eyebrow）全部 resolved；
[verdict](../../.impeccable/review/native-finish-verdict.md)。原生 DESIGN/JSON 从代码提取，
旧 Web 规范保留并新增原生路由。不把这一结论扩展成完整产品/Windows 验收。

Windows、Linux 实际 IME 候选退格与原生 Wayland 未执行/未承诺，补跑条件见兼容性
记录。EUI 两项构建问题在独立台账记录，源码 pin 不变；M6-01 仍等待 Windows。
本轮未创建 commit/MR、未替换安装包和托盘入口；M6 里程碑保持 In Progress。

同日视觉返工：用户指出当前窗口字体偏小、文字与 UI 未对齐。重新打开 M6-02，
统一单行文字/图标的 ink-center 行框，扩大导航、正文与输入字体，并复验正常/最小窗口；
验收前不沿用上轮 ship 结论。

视觉细化 v2 已复验：18px 输入/消息、16px 导航、34px 空态标题；单行文字/图标共享
ink-center 行框，长标题按实际测量省略。Debug/Release GUI 构建与 Release 模型
CTest 1/1 通过；正常/最小、明暗、收起、中文草稿/消息/确认截图已更新。
独立文档提取同步 native DESIGN/JSON；最终全量视觉复核见
[native-v2-final-verdict](../../.impeccable/review/native-v2-final-verdict.md)，仅覆盖所列 Linux
实机画面，不替代 Windows 与真实运行生命周期验收。M6-02 恢复 Completed。

2026-10-04 / M6-02：维护者指定继承本机最新 Mira 红发蓝眼角色图标；资源来自
本地 Mira 472e430 的 docs/mira.png，原样独立导入，不升级 pinned Mira 代码。
本轮接入侧栏品牌、原生窗口图标及 Windows EXE 图标资源；托盘保持尚未启动的预览状态。
图标接入与 Linux 验收通过，详见 [图标验收](../compatibility/mira-icon-20261004.md)：
Debug/Release 构建、模型 CTest 1/1、原始/派生像素与窗口图标全像素核对、正常/最小
明暗主题实机截图。EUI PNG/SVG 误判登记 EUI-20261004-003，采用 UI 资产边界
无损兼容副本；未改第三方源码。Windows ICO 仍待真机验证，M6-01 状态不变。

本轮独立视觉复核 disposition=ship，仅覆盖四张 Linux 图标继承及品牌行布局截图；
[图标 verdict](../../.impeccable/review/native-mira-icon-verdict.md)。系统窗口图标以
_NET_WM_ICON 全像素校验为技术证据，不将视觉复核结论扩大到托盘或 Windows。
native DESIGN/JSON 与资产 provenance 已同步，M6-02 Linux 范围维持 Completed。

2026-10-04 / M6-02 Dock 补验：维护者反馈 Dock 仍无图标，重新打开此资产关联验收。
窗口 WM_CLASS 已为 org.mirage.native、_NET_WM_ICON 有效；缺失对应 desktop entry。
本轮补齐 Linux 用户级开发启动条目与显式注册目标，不切换旧安装包或托盘入口。

Dock 补验的 desktop entry/显式注册已完成，Debug/Release 构建与 desktop-file-validate
通过；Gio 正确解析 Mira PNG，真实窗口 class 匹配，GNOME Dock 无障碍节点已识别
Mirage。Dock 像素截图未获取，不沿用上轮视觉 ship；负责人/补验条件与独立注册复核
见 [图标验收补充](../compatibility/mira-icon-20261004.md)。本轮未修改会话或任务路径。

独立注册复核 disposition=recapture：身份/资源关联修复和隔离 XDG 注册通过，
Dock 像素截图仍需维护者在允许截图的桌面补验，不表述为已通过视觉验收。

2026-10-04 / M6-02：维护者确认 Dock 图标已生效；历史自动截图缺项保留，
以维护者实际观察确认功能结果。新增侧栏调宽和设置/外观页，重新打开 M6-02。
沿用 DEC-033 与既有 ZCode 中性风格：侧栏224–400px，随窗口保留至少520px主区；
记忆本次进程内用户宽度，收起/展开不重置。设置采用分类导航+右侧设置行，
底部齿轮入口替代主题切换；浅色/深色只在外观页选择，立即生效。
退出设置保留当前会话/草稿/消息；不新增未实现的设置分类或系统主题同步。
状态为 In Progress，等待构建、边界拖动与键鼠/截图验收。

侧栏/外观页 Linux 本轮验收通过，M6-02 恢复 Completed。Debug/Release构建、
四配置模型CTest各1/1、ASAN GUI detect_leaks=1退出0无诊断；鼠标/键盘调宽、
最小尺寸、主题、设置返回和草稿保护已实测。独立视觉复核 disposition=ship，
仅本轮窗口内UI；native DESIGN/JSON同步完成。
[验收证据](../compatibility/native-settings-20261004.md) /
[独立复核](../../.impeccable/review/native-settings-finish-verdict.md)。Windows与真实
Agent/托盘生命周期保持原计划边界，未创建commit/MR。

2026-10-04 / M6-03：维护者要求模型设置与通用 Agent harness。按 DEC-034 扩展
模型 IPC 和通用 harness 轮次，原生界面消费服务事件与历史；保留普通 dialog 兼容。
当前实施中，真实供应商凭据及 Windows 验收不由离线测试代替。

维护者纠正范围：本轮不开始RPA workflow，AgentLoop指通用harness需求。核对后
登记 MIRA-20261004-001，撤去设备loop/截图尝试，采用模型网关与工具公开API的单一
有界适配；不引入PNG依赖或桌面工具。本轮尚待验收。


M6-03首步Linux通用harness已接入：模型设置保存/即时应用、真实IPC会话/历史/事件、
任务停止与服务断线，未采用设备AgentLoop。本机SiliconFlow Qwen/Qwen3.5-4B真实文字
和工具循环通过；MiniMax TLS SNI缺口登记MIRA-20261004-002，未修改pinned。
首轮51/51 Debug回归、ASAN/UBSAN/TSAN已执行，最终故障注入/界面复核仍在收尾。
[验收证据](../compatibility/native-harness-20261004.md)。

M6-03 Linux首步验收完成。最终Debug完整51/51，测试夹具排队取消竞态修正后针对
新集成再通过1/1；Release针对4/4，ASAN/UBSAN各2/2，TSAN经setarch -R运行
51 harness + 141 chat checks无race诊断。覆盖Executor容量拒绝、事件溢出恢复、
配置损坏不替换当前模型。真实SiliconFlow文字与wait工具循环通过，MiniMax缺口保留。
文档提取已同步PRODUCT/native DESIGN/sidecar；独立视觉复核disposition=ship，仅
原两项修正及八张Linux截图范围：[最终verdict](../../.impeccable/review/native-harness-final-verdict.md)。
Windows、跨服务时代会话恢复、流式与工具扩展未由本次验收覆盖；M6-04仍Planned，
M6里程碑仍In Progress。没有创建commit/MR或更改pinned代码。

2026-10-04 / M6-05：维护者明确要求整个会话页完全参考ZCode，按DEC-035重构呈现与输入。
保留已接入的真实harness及设置；不把未提供的Token、文件/MCP/桌面能力伪装为可用。

M6-05 Linux首步验收完成：整页按ZCode当前源码对齐，真实Markdown回复/复制、
多行增长输入、引用计数/原文来源检查/逐条移除、上下文事实和草稿保护已取证。
Debug针对3/3、Release1/1、ASAN/UBSAN各1/1（166 checks），ASAN GUI正常退出无诊断。
独立复核的两项修正（引用预览、默认Button文案）均resolved，disposition=ship，
仅修正列表范围：[verdict](../../.impeccable/review/native-zcode-final-verdict.md)。
native DESIGN/JSON及PRODUCT已同步，[验收](../compatibility/native-zcode-conversation-20261004.md)。
运行中Wayland ZCode截图拒绝，未作像素级对照；Windows/真实IME/Markdown框选及链接回调、
真实Token统计保持明确限制或待验证。M6整体仍In Progress，M6-04仍Planned；未创建commit/MR。


2026-10-04 / M6-06 Linux首步完成：按DEC-036接通Mira最终成功调用输入Token、可选IPC
投影及显式窗口预算，ZCode比例圆环与详情落地。真实SiliconFlow391/128000显示0.3%；
未知、模型预算非法值/恢复保存、明暗和最小尺寸均取证。Debug针对4/4、Release1/1、
ASAN/UBSAN各2/2、TSAN180+64 checks通过。独立上下文扩展复核disposition=ship，
其文档同步前置已关闭；native DESIGN/JSON、PRODUCT、IPC契约和验收记录同步。
[验收](../compatibility/native-context-usage-20261004.md) / [verdict](../../.impeccable/review/native-context-finish-verdict.md)。
窗口预算由配置提供，旧服务恢复历史用量未知，草稿不作伪实时Token计数；Windows待验。
M6整体仍In Progress、M6-04仍Planned；未创建commit/MR或修改pinned源码。


2026-10-05 / Git交付checkpoint：原master上的本轮原生实现已转移到
`codex/native-agent-workbench`，按依赖锁定、运行时harness/用量IPC、原生workbench三个
逻辑提交整理。提交者沿用仓库本人配置，不改写master或已发布历史。
只格式化本轮代码；旧CEF、CLI启动器、打包及M5计划的既有改动保留在工作树。
凭据内容/大文件检查通过；只纳入原生资产和有引用的截图、审查/测试证据，构建树与日志
不进入提交。格式/公开头文件检查以暂存树导出验证，避免把旧工作树内容误称为本轮门禁。
格式化后Debug全树构建及51/51 CTest、Release原生构建及模型1/1复验通过。
Windows和整体入口/托盘待验状态不变；此次只作本地提交，未push或创建/合并PR。
详见[Git验收记录](../compatibility/native-git-checkpoint-20261005.md)。

2026-10-05 / M6-07/08 Linux 首步完成：按 DEC-037 交付服务商模型配置、指定顺序输入
工具栏、UTF-8 文本附件与真实 harness 请求。实际附件回复“青柠-37”，输入432/128000
显示0.3%；生产 transport 更换使用公开 worker_name 配置消除实例冲突。模型目录和
保存/放弃使用服务 ACK，推理只对显式声明支持的配置开放，当前只读权限禁用现有wait工具。
旧TS/CEF/devbridge源码、构建消费与打包入口已退役，原生DEB约13.1MiB、提取启动成功。

Debug空闲全量48/48；负载下旧event_subscription_test曾失败，单项及空闲全量复跑通过，
不抹去失败事实。最终原生单元207checks/集成87checks，Debug/Release各2/2；
ASAN/UBSAN/TSAN通过，格式/公开边界/凭据内容检查通过。24张最终实机截图已核验。
独立初审三项修正（附件标题、最小窗口具名配置、PRODUCT当前事实）最终全部resolved，
disposition=ship，仅修正列表范围；四份DESIGN/JSON已独立同步。
[模型与输入验收](../compatibility/native-model-composer-20261005.md) /
[退役验收](../compatibility/native-retirement-20261005.md) /
[最终verdict](../../.impeccable/review/native-model-final-verdict.md)。
工作整理为当前分支上的范围化本地提交；不改写master，不push/合并。Windows/真实IME、
供应商推理互通、root安装与整体托盘退出保持明确缺项，负责人/补跑条件见验收记录。
M6整体保持In Progress，M6-04保持Planned。

2026-10-05 / M6-09：维护者要求直接 API Key、空草稿不进历史、历史可删除，按
[DEC-038](../decisions/DEC-038-api-keys-and-draft-sessions.md) 实施。复用 pinned Mira
SecretRef/ISecretResolver、session close 与现有产品状态存储；系统凭据映射只在平台边界，
不写明文配置，不新增并发设施。等待测试和 Linux 实机验收。


2026-10-05 M6-09 Linux首步完成：直接 API Key 通过系统钥匙环而非明文配置保存；新建只分配
本地草稿，首次发送后进入历史；历史确认删除、活动拒绝和删除持久化已验证。
[验收记录](../compatibility/native-credentials-sessions-20261005.md)：Debug 49/49，Release /
ASAN / UBSAN / TSAN（setarch -R）相关4项各通过，340 ChatModel + 126 integration checks，
系统钥匙环10 checks；本机真实模型及无环境Key的服务重启调用、删除后重启无复活取证。
Windows凭据路径、恢复历史的Agent续跑（DEC-011已有产品状态限制）、托盘整体退出另验，
M6整体仍 In Progress；未升级 pinned 依赖，未发布/合并。

2026-10-05 / M6-10完成：综合台账改为稳定编号入口，分离Mira与Mirador台账；四项Mira
反馈已复核并提交上游#72/#73/#74/#75，回读确认完整正文，状态保持Open。Mirador当前无
已确认缺口，明确空表与登记规则。离线公共API/TLS ClientHello探针退出0复现三项现象，
MinGW POSIX最小表达式预期退出1并复现诊断；mira_host_test 1/1通过（不同内容绕行，
不冒充缺陷修复）。纠正M6-09恢复限制的决策引用为DEC-011；无生产行为、第三方源码或pin变更。

2026-10-05 / M6-11：按DEC-039完成Linux会话减重、悬停复制、选段浮层与末轮编辑重发。
Debug完整50/50；Release/ASAN相关4/4，UBSAN/TSAN相关3/3，原生渲染交互31 checks。
实际Mira夹具请求排除旧末轮而保留更早上下文；覆盖持久化失败、活动拒绝、取消与重启历史
编辑/续聊/删除。四份DESIGN/JSON和IPC契约同步；内联设计修正评分ship（两项范围），
不称独立评审。详见[验收证据](../compatibility/native-conversation-revision-20261005.md)。
Windows、IME、跨消息/自动滚动和live ZCode像素对照未交付，负责人/补跑条件记录于证据。

2026-10-05 / M6-12：按DEC-040使用完整 Noto Sans SC Regular，官方简体中文区域字面2.004；字体压缩归档离线交付，按1.448统一EM字号换算与测量，主题/布局保留，输入最小高52以容纳新行度量。同时修正EUI逐段ink-center
造成的中文字高低跳动，经公开DSL Adapter统一行框，缺口EUI-20261005-006/007本地挂账。
Debug/Release/ASAN相关回归各2/2，原生44 checks / 0 failures；正常/最小明暗、混排、
Markdown与输入12帧，Linux DEB字体/OFL摘要一致。格式、49公共头边界和Git差异检查
通过；无依赖修改、并发变化或模型调用。内联降级设计复核disposition=ship，仅字体范围；
Windows/IME/高DPI仍由维护者在目标平台补跑，M6-04不变。完整证据：
[native-typography-20261005](../compatibility/native-typography-20261005.md)。

2026-10-05 / M6-13：模型按钮改为实际字宽加EUI公开控件内边距，宽96–180px，长名称省略；
思考/发送右对齐与实际模型ID保留。Debug原生渲染70 checks / 0 failures，Release/ASAN相关
回归2/2通过，8种窗口/主题/标签组合完成单轮视觉检查。证据见
[模型按钮验收](../compatibility/native-model-button-20261005.md)；Windows仍待目标平台补跑。

2026-10-05 / M6-14：顶部60px标题条显示当前会话，空标题回退“新对话”，长标题按实际字宽省略；
侧栏展开/收起和设置返回入口留足空间。Debug原生102 checks / 0 failures，Release/ASAN
相关回归各2/2通过，一次批量8帧验收通过。见
[会话标题验收](../compatibility/native-session-title-20261005.md)，Windows保持待验证。


2026-10-05 / M6-15/16/17/18 Linux范围完成：按DEC-041收紧会话密度并将上下文/模型归入
右侧组，接通真实流式、单行等待动画和用时。公开ConversationLoop代替私有循环，保持
Executor 2ae4fc8；三项Mira历史反馈复验Resolved。Mira流式PR#79与EUI IME PR#88已提交，
未合并；Mirage以可获取提交锁定，并记录Mira新增AGPL-3.0许可证与EUI fork来源。
Debug针对5/5（含旧订阅），Release/ASAN/UBSAN各4/4，TSAN用setarch -R通过集成及协议；
普通TSAN启动映射失败保留。真实两家模型的提前预览及wait工具往返通过；一次网络超时
保持明确错误。私有IBus/libpinyin候选坐标与光标一致，中文提交成功；一轮会话渲染批量验收。
四份DESIGN/JSON、协议/设计、依赖反馈/审计同步，内联降级复核通过，不称独立评审。
[验收](../compatibility/native-conversation-progress-20261005.md)。Windows、原生Wayland、
物理高DPI/其他IM与完整入口托盘退出待维护者在目标环境补跑；M6整体仍In Progress。

2026-10-05 / M6-19：维护者要求解决EUI Vulkan问题并继续推进。已确认CI探针
无条件包含GLAD且硬编码OpenGL窗口；先按EUI公开windowRenderApi选择配置后端，
验证GLFW/SDL2 × OpenGL/Vulkan、SDK安装消费，再同步PR/pin和Mirage原生回归。
此项不改变Mirage当前OpenGL产品后端，不开启RPA/workflow。

M6-19补充：本地Release暴露旧pin的-fno-exceptions构建失败；上游dev88a9ec1已合并
对应平台/DSL无异常修复（#84/#85/#86）。普通merge保留已发表IME提交，不重写历史；
同步此dev基线后复验Vulkan，不另造异常处理绕行。

M6-19完成：PR#88 run37338533567四项后端矩阵与SDK消费全部success；两种Vulkan本地
各33/33及SDK consumer1/1、OpenGL生命周期回归通过。Mirage Debug/Release/ASAN各2/2，
格式/边界通过；EUI-20261003-002升级后Resolved并删除库层异常覆写，应用自身异常保留。
Mira PR#79 pull_request 12项CI成功。pin ed1deb6与锁/审计同步，无force-push/merge。
[本轮验收](../compatibility/eui-vulkan-followup-20261005.md)。M6整体与M6-04未完成，
物理GPU/Wayland/Windows原生IME/高DPI仍由维护者在目标环境补跑。


2026-10-06 / M6-20：完成维护者再次列出的会话目标。真实输入框候选跟随、多行中文与
真实模型预览/用时验证；新增长回复保位/恢复跟随。EUI-20261006-001修复程序offset，
更新PR#88/pin ff1e757及锁，当前head四项CI全通过；原生回归包括实际视口像素及idle
关闭唤醒。Windows/Wayland/物理高DPI与M6-04另验。[最终验收](../compatibility/native-conversation-finish-20261006.md)。

2026-10-06 / M6-21：维护者要求严格对照ZCode模型管理与侧栏。参考源码固定29628c9；先补齐服务与模型的分层、空目录草稿、保存ACK权威状态，再调整224/56px服务导航、32px控件、连接字段顺序及模型行。旧配置保持兼容，凭据仍由系统钥匙环承载，任务仍由RuntimeBridge/Executor管理。计划验收空目录、命名保存/重载、多模型切换、删除与失败回滚、正常/最小明暗截图。

M6-21 Linux支持能力范围验收完成：Debug/Release/ASAN/UBSAN各3/3，原生渲染203 checks；TSAN170+287 checks，无诊断。真实空配置直接Key保存/刷新/磁盘引用/模型请求通过，241次预览及持久历史一致。格式、锁和49公共头边界通过。严格源码对照与未达到完整1:1的差异（包括连接探测/元数据弹窗/自动保存/排序）明确记录，Windows/Wayland/高DPI另验；不把本项扩展成完整ZCode或M6整体完成。见[完整验收](../compatibility/native-provider-editor-20261006.md)。Runtime提交e3645ae，UI/证据范围化提交单独交付，依赖pin不变。

2026-10-06 / M6-22：维护者要求继续对照ZCode调整间距，提供国内外厂商预设与Anthropic协议，并使用本机Mira MiniMax配置验证识图。先核对公开能力，Mira当前仅有两种OpenAI方言；按MIRA-20261006-001登记并经既有依赖修复/PR授权补齐最小Messages方言，保持Executor pin。验收包括真实协议/图片输入、取消和错误、预设保存重载与原生窗口正常/最小明暗状态；不声明未测试厂商可用。

2026-10-06 / M6-22验收：24px侧栏/8px标题输入内边距、11项厂商预设和Messages已交付。完整Debug50/50，相关Release/ASAN/UBSAN各3/3，受控ASLR TSAN2/2；真实MiniMax只填Key保存/刷新/流式会话、工具往返及随机图片2/2通过。来源许可、失败证据、上游PR#80与pin同步。闭合本项Linux本地交付，M6整体不关闭；图片附件产品入口、扩展思考、其他厂商/平台不在本项完成声明内，详见[验收与限制](../compatibility/provider-presets-and-vision-20261006.md)。

2026-10-06 / M6-22 follow-up（In Progress）：维护者报告输入API Key后保存仍灰。定位遮蔽编辑回调捕获上一compose的raw值；连续输入不能稳定落入当前草稿。按DEC-043/既有凭据边界修复最新编辑值与按钮刷新，不改变保存ACK和model_loaded门禁；补验键盘连续输入、粘贴、已保存服务替换与实际保存。

2026-10-06 / M6-22 follow-up Completed（Linux本地，BUG-20261006-001）：修复密钥Adapter批次/快照和刷新，Debug相关3/3、Release/ASAN/UBSAN各renderer1/1；真实Release窗口合成Key粘贴保存/键盘替换ACK通过，不发起模型请求。证据见[API Key保存修复](../compatibility/provider-presets-and-vision-20261006.md#api-key保存按钮修复bug-20261006-001)。

2026-10-06 / M6-23 In Progress：维护者要求自行打开模型设置页全面实测并修复。沿用DEC-042/043服务草稿、ACK、Key边界；在私有Xvfb/DBus/钥匙环运行真实Release进程，逐项检查新增/编辑/切换/删除服务与模型、预算/协议/开关、Key显示/清除、保存/取消、错误/重连和最小窗口键盘交互。使用合成配置/Key、不发起模型推理，不操作用户现有状态。确认缺陷后冻结DEC-044交互修复，补自动化与实际窗口复验；不扩展Agent/RPA能力。

2026-10-06 / M6-23 Completed（Linux X11，BUG-20261006-002）：真实Release应用35/35设置交互通过，修复取消跳服务、模型参数丢失、禁用状态、重复ID、未保存导航、菜单/内联取消、删除确认及Key校验。Debug/Release相关各3/3，ASAN/UBSAN renderer各1/1，renderer324 checks；断线保存保留、离线取消和重连继续保存取证。格式/49公共头边界通过，原生CI补齐CURL/Xvfb并加入renderer。依赖/Executor不变，目标平台和CI限制明确保留，[验收](../compatibility/model-settings-interactions-20261006.md)；M6整体不关闭。

2026-10-06 / M6-23 follow-up Completed（Linux X11，BUG-20261006-003）：真实窗口复现首次未连接时预设被加载门禁禁用；按DEC-044修订允许本地预览，首次ACK后保持预设选择及已有保存配置，字段/保存门禁和草稿保护不变。断线/恢复27项、正常连接23项实际点击通过；Debug/Release相关各3/3、ASAN renderer1/1，renderer341 checks，格式/49公共头边界通过。[验收](../compatibility/preset-selection-20261006.md)。无依赖或并发变更；目标平台限制保留，M6整体不关闭。

2026-10-06 / M6-23 follow-up Completed（Linux X11，BUG-20261006-004）：实际窗口复现初次未连接时API Key事件仍被加载门禁拦截，改为可键入/粘贴/显隐的本地草稿，取消与未保存导航闭合；首次ACK恢复已存连接/模型/凭据引用并保留新Key，显式保存后ACK生效且其他服务不变。断线/恢复39项及正常连接Key保存/替换通过，无推理；Debug/Release相关各3/3、ASAN renderer1/1，renderer349 checks、格式/49公共头边界通过。[验收](../compatibility/api-key-input-20261006.md)。不改依赖、并发或M6-04整体入口，目标平台限制保留，M6整体不关闭。

2026-10-06 / M6-24 Completed（Linux X11，BUG-20261006-005）：维护者报告Key后保存仍灰。当前注册的开发desktop entry直接运行mirage-native，实际只存在UI进程；依DEC-007/037将其接入已交付mirage start，先确保Service就绪再启动独立UI/Tray。注册目标同时构建启动器/Service/Tray，保持Dock身份。从生成的desktop entry真实启动、无需手动预启Service完成Key保存/替换，核实Service复用；不扩展UI任务设施或托盘整体退出确认。

M6-24验收：真实desktop冷启动三个独立进程，Key-only保存、替换、凭据不落明文及Service复用通过；Release相关4/4。详见[入口保存验收](../compatibility/desktop-model-save-20261006.md)。维护者随后要求托盘内嵌Runtime；本记录保留为旧拓扑修复证据，新拓扑由DEC-045另立项。

2026-10-06 / M6-25 In Progress：维护者明确要求Agent服务归托盘进程；统一入口只启动托盘，托盘Runtime就绪后启动独立前端，无托盘时前端拒绝创建窗口。沿用现有Mira/Executor/IPC/托盘平台能力；验证冷启动、重复启动、无宿主拒绝、前端关闭重开、活动退出取消/确认、宿主失败与有界清理。依DEC-045更新进程所有权，不修改pinned依赖。


M6-25 Linux 交付已完成：真实双进程/托盘注册、关闭重开、退出取消/确认、宿主终止
共 18 项通过；Runtime/IPC/界面/凭据回归 8/8，ASAN 与 UBSAN 各 3/3，TSAN 在
关闭 ASLR 后 1/1。托盘为 Runtime 唯一 owner，前端需在线托盘且为其所属子进程；
先前 Key 保存在新拓扑下再次通过。详见 [托盘验收](../compatibility/tray-runtime-owner-20261006.md)。
Windows/Wayland/真实通知区像素与安装包仍由 M6-04 继续验收，不关闭整体 M6。


2026-10-06 / M6-25 重新打开：维护者反馈 Dock/应用列表打不开窗口。实机确认窗口处于
_NET_WM_STATE_HIDDEN；EUI 最小化跳过 compose，原恢复请求放在 compose 内不能被消费。
在 FrontendProcess 平台边界直接激活所属子进程窗口，补最小化恢复/宿主退出验证后再关闭。


BUG-20261006-006 已修复：托盘通过 Platform Backend 恢复自己持有的前端，不依赖
最小化时暂停的 compose。GNOME 应用列表条目实际恢复、私有 WM 21/21、所属 PID/
错误 PID/256 窗口预算平台测试通过；Release 相关 4/4 加平台 1/1、ASAN/UBSAN
相关各 2/2 加平台各 1/1。Linux 范围恢复 Completed，未更改 Windows/包装验收状态。


2026-10-06 / M6-25 菜单增量 In Progress：维护者要求左键单击托盘显示仅“打开应用”/
“退出应用”两项。沿 DEC-045 复用 open/quit 与活动确认；修正 Linux DBusMenu 根树
及宿主互通格式，菜单提示保留在 tooltip，移除暂停/恢复的可见条目，不修改依赖。


2026-10-06 / M6-25 菜单增量 Completed（Linux 协议/私有产品范围）：左键菜单声明、
标准根树、两项标签/启用状态、签名互通通过；真实产品 23/23，Release 相关 3/3，
菜单后端 79 checks；ASAN/UBSAN/受控 ASLR TSAN 各 1/1。首次 ASAN 发现事件字符串
和夹具 GMainLoop 泄漏，修复并复跑通过。当前用户实例保留，待正常重启加载；真实
GNOME 通知区鼠标菜单及 Windows 留给维护者目标环境补验，不计为已完成。详见
[托盘验收](../compatibility/tray-runtime-owner-20261006.md)菜单增量。


2026-10-06 / M6-25 入口补验 In Progress：维护者再次明确应用列表统一入口，冷启动先
托盘后前端，托盘已在线时只打开/恢复前端。实际 GNOME 点击日志仍直接执行旧
mirage-native，虽磁盘条目已是 mirage start，旧入口被前端准入拒绝；GIO 读新条目
不能替代 GNOME 现存缓存验收。沿 DEC-045 为已登记的旧桌面调用增加启动器兼容
转发，实际窗口仍由托盘创建，直接 CLI/显式端点的非所属前端继续拒绝。移除临时
入口诊断，补冷启动/已驻留/关闭重开/循环守卫验证，不修改依赖。


BUG-20261006-007 / M6-25 入口补验 Completed（Linux）：维护者实机应用列表点击确认
窗口与托盘同时出现；随后 GIO 再打开复用相同托盘与前端。真实私有当前入口 24/24、
旧缓存入口 28/28（含已有托盘/关闭前端后从桌面入口重开）、Release 相关 3/3；
ASAN/UBSAN 原生入口拒绝/exec 循环分支各 6/6。临时诊断条目已撤除，开发注册目标
重新写入正式 mirage start，未修改 pinned 依赖。证据和剩余平台范围见
[托盘验收入口补充](../compatibility/tray-runtime-owner-20261006.md)。
