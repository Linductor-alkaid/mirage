# M6：原生前端与桌面进程形态重构

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-19
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
| M6-47 | In Progress | DEC-108：历史消息保留行几何，仅构建视口及预留区的正文/控件；性能和回归见[维护计划](maintenance-performance-20261019.md)。 |
| M6-46 | In Progress | DEC-104：失败轮次保留已生成正文，错误单列；Native全量101/101、ASAN/UBSAN/TSAN各6/6（含fixture）、原生28,636 checks通过；维护者重启实例与Windows待补验，[证据](../compatibility/native-failed-output-retention-20261018.md)。 |
| M6-45 | Completed（Linux本地） | DEC-102：缺有效供应商用量时复用Mira请求输入估算并标注来源，随会话保存；Release7/7、原生28,457 checks、ASAN/UBSAN各7/7、TSAN5/5（含fixture）；Windows及真实供应商缺usage待补跑，[证据](../compatibility/native-context-usage-20261004.md#2026-10-18m6-45-无供应商用量时显示本地估算) |
| M6-44 | In Progress | DEC-101 Linux 数据路径与原生缓存验证通过：默认不限个数、逐会话 JSON、旧格式迁移、坏文件隔离、删除/失败回滚；Release/ASAN/UBSAN 五类测试、TSAN 三类测试通过。真实 Wayland 桌面8会话19轮迁移/退出重开、服务39会话准入通过；Windows待补跑，[证据](../compatibility/session-files-20261018.md)。 |
| M6-43 | Completed（Linux 数据路径） | 按 DEC-036 保存会话自身的上下文用量快照，历史截断、切换、重连及服务重启后可读取；Release9/9、ASAN/UBSAN各7/7、TSAN5/5（含fixture）；实机与Windows待补跑，[证据](../compatibility/native-context-usage-20261004.md#2026-10-18m6-43-会话自身保存用量快照) |
| M6-42 | In Progress | 本机性能矩阵已交付：EUI最新dev基础的字形缓存PR#114四项CI全绿，pin63117f7保留Windows修复；纯尺寸/封顶测量与公开历史高度缓存，12场景配对p50/p95均改善；16KB单段/40轮51.64→16.98ms。Release5/5、ASAN/UBSAN各4/4，实机/Windows及上游评审保持开放；[证据](../compatibility/native-input-performance-20261017.md) |
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
| M6-27 | Completed（Linux X11） | 修复会话“到底部”鼠标操作；真实指针命中、阅读位置、流式跟随回归；沿用 DEC-041 |
| M6-26 | Completed（Linux X11） | 会话思考按模型选择、Messages 自适应/签名回填与空历史恢复；DEC-046，真实 MiniMax 与多次重启验收 |
| M6-25 | Completed（Linux X11） | 托盘内嵌Runtime、独立前端准入/复用、左键两项菜单、整体退出与活动确认；DEC-045 |
| M6-24 | Completed（Linux X11） | 修复开发desktop入口绕过Service；真实GIO冷启动、Key保存/替换与服务复用验收 |
| M6-28 | Blocked | 自有 PR 全绿合并、功能分支及过期 build 已清理；仅 EUI #88 待上游有权限者合并，验收记录分支随 PR 交付；[维护计划](maintenance-ci-acceptance-20261007.md)、[进程 EOF 修复](maintenance-linux-process-eof-20261007.md) |
| M6-36 | Completed（Linux 本地；CI为合入门禁） | 修复黑闪 PR 合入 CI 暴露的既有任务终态过早通知；两worker下旧驱动等待串行域与新驱动竞争，独立 runtime PR 交付；Release52/52、事件用例重复5次、ASAN+UBSAN与TSan各1/1；[证据](../compatibility/task-terminal-notification-20261008.md) |
| M6-33 | Completed（Linux X11） | 侧栏/主题过渡、逐模型思考设置、按URL获取候选；Release52/52、ASAN/TSAN、真实公开目录与原生渲染通过；DEC-051 |
| M6-39 | In Progress | DeepSeek 模型列表路径与 Messages 工具名兼容；DEC-051/043，Debug/ASAN/Release 及隔离服务实测通过；Native Release 安装包已更新并校验，维护者原会话测试待验收 |
| M6-40 | In Progress | 全视觉元素独立图标：集中 icons.hpp 登记（Phosphor 2.1.1 溯源补全）、思考深度分档、权限三模式、附件文件/图片、任务类型、运行会话与对话框语义图标；DESIGN.md 图标目录，渲染审计与图标断言测试待验收 |
| M6-41 | In Progress | HUA Chat Completions 工具名拒绝已修复并更新本机：HTTP 对照/真实工具回放、五配置 loop 回归及 Debug/Release/ASAN/UBSAN 定向通过；同期目标稳定性修改导致全量五项视觉失败，原任务与完整发布门禁保持开放；依据 DEC-043 |
| M6-35 | Completed（Linux X11/XWayland） | 真实 XWayland 持续拖拽改为系统原生接管，维护者实测无黑闪；EUI issue #93/PR #94 四组合本机/CI通过；组合 Release52/52、最终程序化采样365+300无黑色中心；Mirage 两个PR八项CI全成功，维护者授权合入；上游评审与 Windows 实际拖拽保持开放；[证据](../compatibility/native-frame-presentation-20261008.md) |
| M6-37 | Completed（Linux 本地） | 最近内容更新会话置顶，选择/草稿保留、历史稳定恢复；DEC-079，Debug定向6/6、Native8/8、sanitizer各4/4，原生渲染22,978检查与包内四程序校验通过；Windows真机另验 |
| M6-38 | Completed（Linux 本地） | 会话输入框三种桌面访问模式与授权联动；DEC-080 / DEC-078，纯对话/视觉/完全访问权限、明暗/最小窗口及共享状态门禁验证 |
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

## M6-26：会话思考与空会话恢复

- [x] Completed（Linux X11）：会话栏按模型展示思考方式/深度；移除设置页重复开关；修复空会话重启累积；真实 MiniMax 请求验证。依据 DEC-046，依赖缺口 MIRA-20261006-002。验收覆盖空历史多次重启、非空历史保留、参数编码/SSE/工具回填与容量拒绝。

2026-10-07 / M6-26 Completed（Linux X11）：输入栏按服务端能力投影展示思考开关/深度，移除设置页开关。
旧实例 17 个会话均为空历史；恢复/保存排除空记录，默认 25 槽含 24 历史及 1 主会话，
自定义限额继续严格拒绝；保留非空稳定 ID，超量非空历史拒绝启动并保留磁盘。16 空记录迁移、
3 次重启及非空编辑/续聊通过。真实 MiniMax-M3 同一原生会话开启/关闭回复及预览通过，
adaptive wait 工具循环 2 步/1 工具成功。Release 52/52、ASAN/UBSAN 相关各 3/3、
TSAN 208 checks（关闭 ASLR）、Mira 协议 7/7。Mira [PR#81](https://github.com/Linductor-alkaid/mira/pull/81)
及锁文件同步，详见[验收与迁移](../compatibility/session-thinking-20261007.md)。不关闭整体 M6。

## 2026-10-07 / M6-27：到底部按钮

In Progress：维护者报告会话箭头点击无响应（BUG-20261007-001）。沿用 DEC-041 的
阅读位置/输出跟随契约，先在实际原生 view/runtime 注入鼠标按下与松开复现，检查命中层级、
滚动状态同步及界面绘制；修复不改变按钮外观或任务并发，覆盖普通/长输出与恢复跟随。

2026-10-07 / M6-27 Completed（Linux X11）：复现鼠标命中被 z=1 会话子树截获，箭头
根层 z=2 修复；真实 Runtime 指针按下/松开、正文像素、末尾位置与跟随，正常/最小明暗
通过。Release/ASAN/UBSAN 相关各 2/2，renderer 421 checks、format/文档链接通过。
正式 GIO 入口已加载新版，保留 1 个非空历史，新建/删除空会话成功；M6-26 锁屏后启动
补验完成。无依赖、协议或并发变更。[验收](../compatibility/conversation-scroll-20261007.md)。

## 2026-10-07 / M6-28：会话删除按钮悬停

状态：Completed（Linux X11）。负责人：Codex。维护者要求侧栏会话标题的删除按钮仅悬停时出现。
沿用原生前端侧栏契约与现有删除确认流程；验收覆盖默认隐藏、标题悬停显示、
移到删除按钮保持显示、移出隐藏及既有会话回归。无第三方或并发变更。

验收：`cmake --build --preset native-release --target native_conversation_view_test -j 2`
成功；`ctest --test-dir build/native-release -R '^native_conversation_view_test$' --output-on-failure`
1/1通过（431 checks），含新增四项悬停状态检查。`git diff --check`通过。
设计同步至`docs/design/native-agent-frontend.md`。Windows/Wayland及消毒器本次未执行，
负责人Codex，补跑条件为对应平台/消毒器构建环境可用；不扩大本次验证声明或关闭整体M6。

## 2026-10-07 / M6-29：ZCode 控件密度对齐

状态：Completed（Linux X11）。负责人：Codex。维护者指定 https://zcode.z.ai/cn 的应用UI设计，
要求缩小会话与设置中偏大的控件。沿用DEC-041/042/046，不改变业务行为与依赖。
参考官网当前应用展示及固定ZCode公开源码29628c9的28px按钮/32px输入框/14px文字。
范围：输入栏工具、菜单行、外观页主题控件、模型页标题及操作；正文与输入14px保留。
验收：1180×800与860×620明暗原生渲染、长模型名/菜单/主题/保存取消/会话悬停回归。

M6-29验收：Release相关2/2、原生渲染479 checks通过；限定自研目标ASAN/UBSAN（vptr除外）
479 checks无诊断，明暗/常规/最小窗口菜单与实际主题点击验证通过。格式及diff检查通过。
详细来源、尺寸对照、截图与消毒器边界见[验收记录](../compatibility/native-control-density-20261007.md)。
Windows/Wayland/高DPI及全Debug依赖vptr另验，负责人Codex、补跑条件见记录；M6整体不关闭。

## 2026-10-07 / M6-30：统一控件文字内边距

状态：Completed（Linux X11）。负责人：Codex。维护者反馈思考按钮右侧留白不足，要求各模块一致并预防复发。
按DEC-048在Mirage UI边界封装现有EUI按钮，统一8px文字内边距与4px图文间隔，
按字体测量宽度并在受限宽度内省略；输入框复用同一内边距。工具组按内容与相邻控件推导位置。
保留现有事件/禁用/确认/草稿流程，不修改依赖。验收覆盖所有按钮文字边界、
不同思考值、长中英文、明暗/最小窗口及已有操作回归。

M6-30验收：Release相关2/2、原生渲染13,151 checks通过，覆盖所有按钮与输入的文字边界、
九种思考值及明暗/最小窗口；设置/会话操作回归通过。尺寸、边距token和决策同步。
[验收与平台限制](../compatibility/native-control-insets-20261007.md)记录插桩与截图边界；M6整体不关闭。

## 2026-10-07 / M6-31：思考与工具过程渲染

状态：Completed（Linux X11）。负责人：Codex。维护者要求参考本机 ZCode 直接实现并验证。
依据 DEC-049：产品拥有的有界过程投影贯通 Mira Adapter、IPC、历史与原生 UI；
思考/工具默认折叠，参数与结果展开，稳定 ID 保持展开状态。使用既有 Executor 任务和 Topic；
不修改 pinned 依赖，不解析正文伪造工具状态。验收覆盖实际工具循环、取消/失败、历史恢复、
协议容量校验及明暗/最小窗口原生渲染。

M6-31验收：Release全部52/52、UI模型388、集成298、原生渲染14,019 checks通过。
三个目标的全部自研依赖ASAN/UBSAN插桩通过（复用pinned Release，排除vptr）；
完整自研及Mira/Executor TSAN插桩298 checks、无诊断，无抑制规则。
长过程按实际JSON帧预算保留最新轮、显式截断，超过4MiB会话文件在8MiB预算内重启恢复。
思考在模型步骤返回时出现，当前仅wait工具；Windows/Wayland/高DPI待对应环境补验，
负责人Codex，详见[验收与能力限制](../compatibility/conversation-process-rendering-20261007.md)。
不关闭整体M6。

M6-31 / PR#75补验：首轮ASAN/TSAN均因新增大文件恢复用例触及40秒CTest总时限；
总时限改为120秒，保留操作期限及全部断言。本机完整Debug ASAN 298 checks、
0失败（26.28秒），Release相关1/1及格式/公开头边界通过。
最终CI与合并证据见[PR#75](https://github.com/Linductor-alkaid/mirage/pull/75)。

M6-31 / 2026-10-08补验：容量场景隔离至独立Runtime/目录，正常场景2秒期限保持，
容量I/O和模型步骤8秒；收件箱溢出改为有界等待准入/完成。完整Debug TSAN 303 checks、
0失败（24.80秒），最终Release全套52/52、三个目标自研ASAN/UBSAN通过。
详细原因及历史CI失败保留在上述验收记录，最终CI证据见PR#75。

## 2026-10-08 / M6-32：用户消息气泡换行边界

状态：Completed（Linux X11缩放布局）。负责人：Codex。维护者截图显示“你会做什么”的末字换行落在气泡外。
沿用DEC-040字体EM修正及DEC-048文字留白契约，核对实际渲染缩放、字体测量与气泡几何；
使用同一有界布局结果驱动正文宽高与气泡，覆盖中文短句、混排、长串及显式换行，
并验收1×/分数/2×的实际文字布局、明暗/最小窗口和选择复制。

M6-32验收：依据DEC-050使用实际渲染比例下的EUI字形布局，五字短句完整单行；
长消息按真实行数撑开气泡。原生20,013 checks/112组尺寸缩放场景、Release全套52/52、
自研ASAN/UBSAN（复用pinned Release、排除vptr）及选择复制回归通过；
2×明暗合成截图已目检，格式/52个公开头边界/受影响文档链接/diff通过。
[验收与平台边界](../compatibility/user-bubble-layout-20261008.md)记录Windows/Wayland真实显示器待验条件；M6整体不关闭。

M6-31 / 2026-10-08故障注入补验：第三轮CI原生渲染通过，集成测试搬动活跃持久化目录
存在竞争；替换/删除两处故障注入改为原子占用独占临时路径，有界等待并检查建立与清理。
本机Release/完整Debug TSAN各307 checks、0失败，产品代码不变。详情及最终CI链接见M6-31验收记录。

## 2026-10-08 / M6-33：原生交互渐动与模型目录

状态：Completed（Linux适用范围）。负责人：Codex。依据 DEC-051。Linux 范围为侧栏开合与主题的连续过渡、
逐模型思考声明、保存前按 Base URL/Key 请求模型目录并手选添加。现有已保存配置、
Key 保密边界、12 模型上限和非兼容服务手填入口保留。

验收要求：正常/最小明暗窗口开合、拖拽与快捷键中断；模型切换后思考选项与保存 ACK；
协议编解码、凭据不回传、拒绝/超时/取消/关闭和候选列表去重/上限；真实供应商目录在有
授权凭据时补验。Linux/Windows 分别记录完成情况，未执行项不标完成。


2026-10-08验收：Release52/52，ASAN/TSAN各IPC golden与agent集成2/2；
原生渲染及自研ASAN+UBSAN各21412 checks、0失败。真实OpenRouter目录使用synthetic Key
获取成功，无推理调用。模型开关、保存与原生正常/最小明暗页面通过；
[完整命令、失败修复与平台补验](../compatibility/native-motion-model-discovery-20261008.md)。
Windows、物理显示器帧率及Anthropic真实目录保持补验，不关闭M6整体。

2026-10-08 M6-35：框架修复后的 Xvfb 低频采样曾通过，但维护者在真实桌面继续复现；
真实合成器提高采样后记录 149/300 黑色中心样本，撤回“框架修复已完全消除拖拽频闪”
结论。Mirage 将自绘八方向 grip 接入 EUI 已有 beginWindowResize，原生接管后禁止
再逐帧设置几何；维护者物理鼠标实测拖拽顺畅、无频闪。EUI 独立首帧/旧尺寸路径见
[issue #93](https://github.com/sudoevolve/EUI-NEO/issues/93) 与
[PR #94](https://github.com/sudoevolve/EUI-NEO/pull/94)。完整证据、失败及平台补验见
[窗口呈现验收](../compatibility/native-frame-presentation-20261008.md)。

2026-10-08 M6-35 合入验收：依赖 [PR #79](https://github.com/Linductor-alkaid/mirage/pull/79)
已合入 master；应用 [PR #80](https://github.com/Linductor-alkaid/mirage/pull/80)
已同步 master，维护者明确授权合入及清理本次 Mirage 工作分支。两份原提交的八项 CI
全部成功，含 Windows MSVC、Debug/Release、ASAN/UBSAN/TSAN 与原生界面。Linux
功能范围完成；Windows 编译/自动测试不替代物理拖拽验收，上游 #94 评审保持开放。

## 2026-10-11 / M6-37：会话按最近内容更新排序

状态 Completed（Linux 本地）；负责人 Mirage 维护者。维护者要求左侧最新更新的会话位于最上方，
按 DEC-079 提供有界排序投影、实时更新与历史时间恢复，保留当前选择/草稿/阅读位置。
验收包括乱序恢复、重复历史、迟到事件、流式/工具过程、同毫秒、清空/删除及相关回归。

M6-37 本地验收：模型412检查、原生最终渲染22,978检查，均零失败；Debug定向6/6、
Native定向8/8，ASAN/UBSAN/TSAN定向各4/4（含状态fixture），最终自带隔离的原生
夹具另3/3。DEB四程序与Release SHA一致，最终六份用户状态SHA与操作前一致；
直接截图遗漏隔离造成外观偏好写入已按原SHA恢复，夹具现自行隔离配置。见
[完整验收](../compatibility/session-activity-order-20261011.md)。未提交/推送，Windows
真机另验，不关闭M6整体；权限实机补验仍按维护者要求暂缓。

## 2026-10-12 / M6-38：会话三种桌面访问模式

维护者明确指定对话、视觉、完全访问，按 DEC-080 接入会话按钮及授权联动；
服务端强制对话模式工具/屏幕为空，视觉只观察，完全访问仍受系统/服务守卫。
运行和授权等待期间锁定模式，保留草稿与旧默认偏好，损坏访问值不升权。
Linux 本地 Completed：Debug 70/70、Native 76/76，sanitizer 定向各 8/8，
原生渲染 24,934 检查零失败，DEB 四程序与 Release 匹配。
[证据与限制](../compatibility/conversation-access-and-cua-continuity-20261012.md)。
不以 Linux 渲染关闭 Windows 真机或 M7-10 GNOME 授权/恢复门禁；未提交/推送。

2026-10-12 / M6-38 界面澄清（Completed，Linux 本地）：维护者要求选择菜单只有三个选项。
删除会话内授权区域及说明，收窄至 280×120、选中后收起；保留能力限制和完全访问
授权联动。DEC-080 和界面契约已更新；Native 定向 6/6、原生渲染 24,005 检查
零失败，DEB 四程序匹配最终 Release。见[仅三模式菜单验收](../compatibility/conversation-mode-selector-20261012.md)。

2026-10-12 / M6-26、M6-38 同日修正（Completed，Linux 本地增量）：按 ZCode
29628c9 将三模式菜单移到实际按钮正上方，左对齐、间隔 4px；原生明暗/最小窗口
24,009 检查零失败。DEC-082 对精确 MiniMax-M3 默认显式 Adaptive，关闭仍 Disabled，
正文与独立思考分离；实际合成三档 3/3。历史默认正文异常未稳定复现，原用户任务
仍需新版复验，不宣称全部供应商输出已保证。Debug 70/70、Native 76/76，sanitizer
各 6/6；四个 DEB 内程序校验匹配，未安装/启动产品或提交/推送。详见
[验收](../compatibility/reasoning-and-menu-anchor-20261012.md)。

2026-10-12 Git 交付：维护者要求整理提交，并明确授权将同期选路修改一起核验。
依赖、桌面、平台、原生 UI 与 Runtime 分组提交，代码终点 6703251；最终组合
Debug 70/70、Native 76/76、ASAN/UBSAN/TSAN 选路各 5/5，源码稳定性与包内
四程序校验通过。本地提交不关闭既有真实平台/原用户任务门禁，未推送或合并。
完整范围与证据见[Git 交付记录](../compatibility/m7-git-delivery-20261012.md)。

## 2026-10-12 / M6-39：DeepSeek目录与推理路径

用户最初实测 `/anthropic/v1` 可用于推理而模型列表失败，`/anthropic` 可获取列表。
Mira 的公开 `ModelProfile::request_path()` 对 Messages 只追加 `/messages`，预设保留
`/anthropic/v1`；目录对官方 DeepSeek 地址使用与旧 `/anthropic` 配置相同的候选路径，
已存凭据仍按用户原配置精确匹配，同源且不改变其他厂商路由；依据 DEC-051。
随后用用户已保存 Key 补验，两条路径的最小 Messages 请求均返回 HTTP 200，说明当前
会话的 `invalid_request_error` 另有原因：Mirage 的 `desktop.window.list` 工具名带点号，
DeepSeek 返回 HTTP 400 和允许字符规则；映射为 `desktop_window_list` 后 HTTP 200。
按 DEC-043 只在 DeepSeek Provider 边界映射工具名和回放，返回工具调用按原 ID/名称
还原后交给 Mira 校验、执行和可见记录。

验收：Debug/ASAN/Release 集成测试验证候选相同、非 DeepSeek 路由不变，且工具声明、
assistant 回放与返回调用往返映射、碰撞拒绝；用已授权 Key 在隔离服务中以
`/anthropic/v1` 获取模型列表，并发起一轮 Agent 只读对话。
`cmake --build build/debug --target native_agent_integration_test -j 3`，以及
`build/asan`、`build/release` 的同目标 `-j 2` 构建均通过；三者执行
`ctest --test-dir build/<配置> -R '^native_agent_integration_test$' --output-on-failure`
各 3/3 通过。现有 `build/native-release` 缓存指向已删除的
`build/ocr-debug/_deps/mirador_ncnn-src`，重新配置失败；独立 `release` preset 可完成服务端
验证。`git diff --check` 与变更行 clang-format 通过。通过临时配置、独立 socket、
独立 XDG state 启动 Debug 服务，复用钥匙环中的既有 Key：`model.list` 返回 2 个 ID，
`agent=true, access=read_only` 的“你好”会话终态 `ok`，无桌面修改，服务干净关闭。
Key 未输出、写入仓库或传入命令参数。最初一次隔离服务启动未覆盖 XDG state，读取并
重写了原会话状态文件；随后立即停止，原 9 个会话和该失败轮次仍在，正式探针使用独立
XDG state。当前运行的 native-release 仍为旧构建，不能把隔离服务结果当作当前程序验证。
负责人：Mirage 维护者；补跑条件：恢复 native-release 缺失的 OCR 依赖源码，部署此
改动后重启当前程序并用现有 DeepSeek Key 复验。

2026-10-12 安装包交付：维护者明确要求更新安装包并自行测试。恢复同一锁定提交的 ncnn
构建源码后，Native Release 四程序构建与定向集成 3/3 通过；DEB 四程序与构建 SHA 一致，
五项 OCR 资源匹配 manifest/资源锁，已更新 `out/packages/mirage_0.1.0_amd64.deb`。
未安装/启动产品，原会话用户实测保持待验收。见[安装包证据](../compatibility/deepseek-package-20261012.md)。

## 2026-10-12 / M6-40：全视觉元素独立图标

维护者要求为所有按键、菜单与任务状态配独立图标，使窗口极小时仅凭图标可辨认
功能，参照本机 ZCode 的图标语义（Brain、ShieldAlert/ShieldCheck 权限、
Image/FileText 附件、按会话状态区分的列表图标）。本轮把散落在 app.cpp 的
36 个内联 Phosphor 码点集中为 `apps/native/icons.hpp` 登记表并全部替换为命名
常量；补齐缺口：思考深度各档独立图标（默认 sparkle、开启 brain、关闭 prohibit、
最少 lightbulb、低 moon-stars、中 planet、高 star-four、极高 lightning、最高 sun），
权限三模式（对话 chat-text / 视觉 eye / 完全访问 shield-warning 对齐 ZCode 高权限
警示语义），桌面工具页与授权按钮（folder-notch-open / floppy-disk /
arrows-clockwise / shield-check / shield-slash），模型页获取/保存/取消按钮，
外观页字号与代码字号步进按钮，流程任务类型图标（brain / cursor-click /
hourglass / wrench，行体右移 20px），运行会话 circle-notch 状态图标，附件按
扩展名投影图片/文件类型图标并与引用行同构（独立移除按钮），三个对话框与退出
确认的 24px 标题语义图标。设置导航「桌面工具」由 dice-three 改为 monitor。

Phosphor-Regular.ttf 此前无溯源记录；本轮核实其为 npm @phosphor-icons/web
2.1.1 `src/regular/Phosphor.ttf` 逐字节副本（sha256 一致），样式表 1530 个命名
码点全部在打包字体 cmap 中解析，新增 `provenance-phosphor.json`。DESIGN.md
新增「图标目录（M6-40）」并同步受影响几何（取消修改/保存 80→96px、附件/引用
行结构、过程行缩进、对话框标题）。仓库纪律：无第三方代码修改，无新增依赖。

验收（Independent-Verification-Agent，build/native-release + Xvfb）：
`ctest -R 'native_conversation_view_test|native_chat_model_test'` 全部通过，
26,393 / 412 项检查 0 失败；新增 82 条图标断言（思考九档互异、模式三行、
会话运行态、对话框标题、桌面页六钮、模型/外观按钮、附件图片/文件类型、
过程任务类型与 20px 缩进、桌面导航 monitor），并以变异测试证明断言真实
比较渲染字形而非空转；逐按钮几何审计（文字测量、inset=8、图标间隙=4）
全部通过，无按钮宽度不足。测试断言由该 agent 写入
tests/desktop/native_conversation_view_test.cpp。clang-format 相对 HEAD
无新增违规（测试文件 1489/1495 行为 HEAD 既有违规，属无关改动未处理）。
负责人：Mirage 维护者；待验：Windows 平台与真实桌面像素级视觉验证另验。

## 2026-10-12 / M6-04、M6-22、M6-39、M6-40：最新修复安装包

维护者要求重新打包最近修复。Native Release 基于 adfe65b 与已有 DeepSeek 工作树
修复重建，全量 76/76 通过；补齐 Linux DEB 的 Phosphor 字体、MIT 许可证和厂商
图标目录。四程序、17 项界面资源与五项 OCR 资源解包校验通过，已原子更新
out/packages/mirage_0.1.0_amd64.deb 并保留旧包。未安装/启动用户产品或在线调用模型；
原会话、真实桌面与 Windows 门禁保持开放，工作项总状态不变。
见[安装包验收记录](../compatibility/native-package-refresh-20261012.md)。

2026-10-12 本机更新：维护者进一步授权直接更新本机。现有桌面入口指向本仓库
Native Release，已同步程序/资源并重新注册入口；四程序和 17 项界面资源与验证包
SHA 一致，desktop-file-validate 与 CLI 版本检查通过。无运行中产品，不需关闭；
未安装系统 DEB或启动产品，原会话实测门禁保持开放。

## 2026-10-12 / M6-41：HUA 工具名拒绝修复

最新 HUA gpt-6.1-sol 会话首轮 invalid_request_error 已由受控对照定位为点号工具名。
复用 Provider 别名适配至 Chat Completions，五配置 loop/registry 往返及非法名称/
碰撞拒绝验证通过。Debug/Native Release/ASAN/UBSAN 定向通过；真实 HUA 别名
工具调用和 assistant 回放 HTTP 200，隔离服务 minimal 两轮 ok。保留两次网络阶段
超时事实，未修改思考设置或用户会话。已更新本机 Native Release 和桌面入口。
同期 DEC-083 稳定性修改的全量五项视觉回归未通过，M6-41 保持 In Progress；
原任务与整包门禁待维护者补验，未覆盖已验 DEB。详见
[验收记录](../compatibility/hua-tool-alias-20261012.md)。

2026-10-12 / M6-41 供应商名称约束调研：已核对主流官方文档及公开 schema，
确认 Chat/多数 Messages 入口排除点号，64/128 上限与首字符约束随入口变化，
Google 原生声明及 Bedrock Agent/Runtime 另有差异。记录未明示约束的服务与
Responses 顶层规则补证项；建议模型侧默认采用字母开头、字母数字下划线、最长
64 的保守别名。文档调研完成，不将建议自动扩展为代码交付或关闭 M6-41 门禁。
见[调研报告](../research/2026-10-12-provider-tool-name-contracts.md)。

2026-10-12 / M6-41 统一适配准入：维护者明确授权开始适配；DEC-043 固定三方言
统一模型侧保守别名与冲突拒绝。验证普通/强制工具选择、返回解析、历史回放、结果
关联 ID、名称首字符/长度及三方言 loop/registry；公开接口和用户配置不迁移。

统一适配本地收尾：三方言协议 121 项检查通过，七配置真实 loop/registry 往返通过；
Debug/ASAN/UBSAN 定向各 4/4。补齐 PP-OCR/X11 模拟 Provider 名称后追加定向通过。
最终 Native Release 全量 75/78，剩余两个 GTK 与共享图像直接 Registry 验证失败，
尚未进入模型适配路径；M6-41 保持 In Progress，负责人 Mirage 维护者，补跑条件为
同期 DEC-083 实现/夹具一致后重新构建全量。已更新本机四程序与桌面入口，保留已验
旧 DEB。真实供应商/SSE/Windows/原任务门禁未扩大声明，见
[统一适配验收](../compatibility/unified-tool-alias-20261012.md)。


## 2026-10-17 / M6-42：输入性能定位

已复现长草稿输入超帧预算，改用EUI公开尺寸测量与高度封顶后的完整段落前缀。
测试矩阵、基线/优化后统计、首轮sanitizer超时和oracle atlas预算修正均记录于
[验证记录](../compatibility/native-input-performance-20261017.md)。M6-42不关闭：
单段字形测量/长历史仍超预算，EUI修复方案已登记且待明确授权；短草稿长历史需
配对复验，真实compositor、物理高DPI与Windows由维护者取得目标环境后补跑。

2026-10-17 / M6-42 第二阶段：维护者授权先切换 EUI 最新 dev，再修复和验证，确认
通用场景确有性能收益后才提交 issue/PR。本轮最新 dev 为 ad7e9e7；既有 fork
Windows 修复 bcfaa2e/f052729 在最终集成 pin 中保留，性能 PR 独立基于最新 dev。
增加 EUI 公共文字测量的 FreeType 一致性 oracle 与短文/长文/高基数字形/emoji/
大字号基准，配合 Mirage 输入全链路和四种窗口/渲染组合验收。


2026-10-17 / M6-42最终本机取证：EUI四种后端独立36/36、安装消费者各1/1，PR#114
四项远程CI全通过；最终pin63117f7保留旧Windows补丁，额外Windows-only测试明确
skip。Mirage增加有预算的精确历史高度key，正文/工具/状态/展开/字号/几何失效检查
与原生27703项回归通过；Release5/5、ASAN/UBSAN各4/4，四张截图逐字节相同。
3轮交错A/B的12种输入场景p50/p95均改善，原始360条统计及限制见
[完整记录](../compatibility/native-input-performance-20261017.md)。M6-42的真实用户
compositor/物理高DPI/Windows验收由维护者取得目标环境后补跑，上游PR待评审；
本轮不合并PR、不替换已安装应用或重启现有会话。

2026-10-17 / M6-42本机更新：维护者授权提交、CI全绿后合入master。native-release
前端和资源已重建并实际启动，exe摘要与新构建一致、窗口映射；定向6/6通过。
CI新增性能用例构建/执行；本次PR仅含性能范围，保留同期pet工作树修改，详情见
[启动与提交记录](../compatibility/native-input-performance-20261017.md)。


2026-10-18 / M6-43 Linux数据路径完成：按维护者要求补齐会话自身的上下文占用存储，
更新DEC-036、持久化/IPC契约及产品/原生设计。快照独立于最近40轮历史，成功缺用量
清空并存盘；失败/取消保留旧值；编辑接纳与迟到历史不会恢复已失效用量。
Native Release前端/service构建通过；Release定向7项+fixture 9/9，ASAN/UBSAN
各5项+fixture 7/7，TSAN 3项+fixture 5/5。无第三方改动及新增并发设施。
实机GUI与Windows本轮未执行，负责人维护者，补跑条件和完整命令见
[上下文存储验收](../compatibility/native-context-usage-20261004.md#2026-10-18m6-43-会话自身保存用量快照)。


2026-10-18 / M6-44：按维护者要求取消默认会话数量限制，逐会话 JSON 原子存储，
兼容旧文件迁移与坏文件隔离。80 个旧会话恢复并再新建 30 个会话；总历史超旧全局
8 MiB、迁移中断重试、删除/编辑失败回滚及有序关闭通过。修复旧纯任务日志恢复时
未重建 journal_raw 导致再次保存丢失的缺陷。原生切换释放旧线程高度/布局缓存。
Release/ASAN/UBSAN 五类相关测试通过，TSAN 在 setarch -R 下三类通过；首次
失败与实际命令见[验收记录](../compatibility/session-files-20261018.md)。Windows
与真实桌面验收由维护者取得对应环境后补跑，M6-44 保持 In Progress；未重启现有应用。


2026-10-18 / M6-44真实桌面补验：维护者授权重启，完整 mirage-native-product 构建后
启动桌面入口，临时30个空会话使服务达到39项，关闭测试会话后无残留。真实8会话19轮
迁移至8个JSON，并与安全备份逐对象精确相等；正常退出托盘/前端后再次打开，历史与
窗口均恢复，运行映像摘要与新构建一致。保持新窗口打开；Windows仍待维护者取得目标
环境后补跑，[完整证据](../compatibility/session-files-20261018.md#2026-10-18真实-wayland-桌面启动与重启补验)。


2026-10-18 / M6-45：参考本机ZCode的本地估算快照与来源区分，按DEC-102补齐无
usage时的输入估算。复用Mira公开请求估算，不改ModelUsage或预算语义；来源随事件、
历史和逐会话文件保存，原生显示“约 / 本地估算”。Release7/7，原生渲染28,457
checks无失败，ASAN/UBSAN各7/7、TSAN5/5。真实报告优先与零值、低质量降级、
工具最终请求、普通对话、重启及未知窗口均有覆盖。Windows与真实供应商无usage实测
未执行，负责人维护者，条件及命令见[验收记录](../compatibility/native-context-usage-20261004.md#2026-10-18m6-45-无供应商用量时显示本地估算)。


### 2026-10-19：M6-47 性能增量

40 轮长历史仅构建视口附近正文与控件，几何/跟随保留；Linux 私有性能与 sanitizer 子门禁已通过，真实桌面/Windows 留开放。
详见[性能记录](../benchmarks/native-runtime-pet-20261019.md)和
[维护计划](maintenance-performance-20261019.md)。


### 2026-10-20：本机运行目录误删恢复（M6-04/M6-12 维护）

维护者确认另一会话误删整个 build/，运行中字体与 pet 动画资源随之丢失。
按当前源码完整重建并重新启动 native-release，恢复桌面入口；31 项资源摘要、
定向 8/8（含夹具）及真实窗口中文/动画检查通过，19 个会话/状态文件摘要未变。
这是一轮环境恢复，未修改渲染器/动画实现，也不关闭原未完成平台或模型门禁。
准确命令、作用域及证据见
[运行目录恢复记录](../compatibility/native-build-recovery-20261020.md)。
