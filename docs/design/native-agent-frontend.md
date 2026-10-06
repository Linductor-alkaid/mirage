# 原生 Agent 对话页

当前入口为 `mirage-native`（M6-01/02/03，DEC-033/034），一个 EUI-NEO GLFW/OpenGL
窗口进程。UI 主线程持有有界 `ChatModel`，视图以 EUI DSL 合成，私有窗口适配负责
系统窗口动作。RuntimeBridge 通过 Executor blocking worker 连接独立 Runtime Service，MpscChannel 将有界事件交回 UI；模型请求在服务内执行。

界面最多显示24个会话、每会话最近40轮（80条消息），每条草稿最多16 KiB。
会话、轮次和回复以IPC快照/事件为事实源；草稿、主题、侧栏宽度保留在UI进程内。
提交后显示运行中，终态回复或明确错误；停止按钮调用session.chat.cancel。
服务重启恢复的旧会话保留产品ID，首次harness提交延迟打开当前Mira会话并建立有界映射；
历史、编辑重发与续聊仍使用稳定产品ID（DEC-039），不恢复旧Task或桌面task.submit对象。

## Direction contract

THESIS：桌面 Agent 的对话工作台；先读清当前会话，再在底部描述任务。

OWN-WORLD：参考 ZCode 的 Zai Light/Dark 中性灰、窄侧栏、细分隔线、紧凑工具条，
深色实心发送按钮；随应用交付的 Noto Sans SC 中文无衬线字体，图标使用 Font Awesome。

STORY：用户在设置配置模型服务，返回会话提交文字任务，读取Mira回复或明确失败；
必要时停止轮次。通用harness先于RPA，当前不提供屏幕操作或workflow功能。

FIRST VIEWPORT：1180×800 无边框窗口，260px可调侧栏与60px原生标题条保留；
空会话为居中问候与672px输入框。有消息后，右对齐轻灰用户气泡、无边框Markdown回复，
主区宽≥864px时阅读列扣除96px留白，否则扣除48px，最大800px；底部16px圆角输入随文字增高。
附件/访问权限/上下文占比/模型/思考深度/发送构成紧凑工具条；最小860×620保持操作可达。

FORM：用户指定 ZCode 参考，code-led 原生桌面对话页；seed key：user-zcode-native。

FINISH：unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## 参考

[ZCode](https://github.com/zai-org/ZCode) 的 `DESIGN.md`、`styles.css`（Zai Light/Dark）、
`V4ChatPane`、`ConversationHeader` 与 composer；只参考视觉与布局，未拷贝业务实现。

## 视觉细化（2026-10-03）

单行文字与图标共享行框中心，使用 EUI 的 ink-center 对齐；取消手工字号偏移。
M6-02历史基线为标题34px、正文/输入18px和固定160px composer；M6-05已由下述
ZCode整页布局替代。现行正文/输入14EM，composer随内容增高，取消三条建议。

## Mira 图标继承（2026-10-04）

维护者指定本机最新 Mira 红发蓝眼角色图标。原始透明 PNG 原样导入
`apps/native/assets/mira.png`，来源提交和 SHA-256 见同目录 provenance.json。
侧栏品牌行左侧使用 36×36 contain 图像，Mirage 文字从 x=72 开始，
窗口使用同一 PNG；Windows EXE 使用只作容器转换的 mira.ico。
资源由构建复制到可执行文件旁 assets，运行不依赖源 Mira 路径；Mira 代码 pin 不变。
EUI 当前托盘关闭，仅预置同一 trayIcon 配置，不宣称已实现托盘联动。

侧栏资源为 mira-ui.png：从原图无损 RGBA 重编码，去除会触发 EUI SVG 误判的
元数据；尺寸、每个像素和 alpha 完全相同。原图保持完整，源/派生摘要均记录在
provenance.json。临时兼容边界与移除条件见 EUI-20261004-003，未修改依赖。

Linux Dock 的图标身份由 org.mirage.native.desktop 关联，StartupWMClass 与 EUI
appId/WM_CLASS 均为 org.mirage.native，Icon 引用构建目录的原 PNG。用户级开发
注册通过显式 CMake 目标完成，遵循 XDG_DATA_HOME，不在普通构建中写用户配置；
随构建路径变化需重新注册，不替代后续正式安装包入口。开发条目经 `mirage start`
原三进程修复避免绕过服务导致模型保存禁用（BUG-20261006-005）；产品现按 DEC-045
由托盘内嵌 Runtime 并启动所属独立 UI。

## 可调侧栏与设置（2026-10-04）

遵循维护者继续参考 ZCode 的指示：默认260px侧栏，用户可拖动分隔线调为
224–400px；窗口较小时约束到主区至少520px，保留用户期望宽度以供扩窗恢复。
分隔线可用左右方向键每次8px调宽；收起/展开和设置/对话切换不重置宽度。
宽度、主题仅在当前UI进程记忆，尚不持久化。

底部设置按钮取代主题按钮；设置页左侧提供“返回对话”和“外观”，右侧展示
外观标题、主题设置行、浅色/深色选择与即时应用说明。字体、颜色、细分隔线
沿用既有系统，参考 ZCode SettingsPage / AppearanceSectionContent；模型分类按DEC-034接入真实配置；不增加工作流分类。设置期间隐藏聊天输入并阻止后台聊天快捷键修改会话；
Escape 先关闭模态弹窗，再返回对话，Ctrl+, 打开设置。侧栏收起时仍有设置入口。

## 模型设置与通用 harness（2026-10-04）

“模型”位于设置分类下，右侧滚动表单包含服务origin、API路径、模型ID、直接 API Key 输入（DEC-038），
Responses/Chat Completions二选一。保存通过model.set应用并合并写入服务配置，保留其他块；
活动轮次时忙拒绝，配置校验或存盘失败保持当前模型。API Key 经 Mira SecretRef 从系统凭据读取；旧环境变量配置继续兼容。

会话发送session.chat(agent=true)，使用MiraRuntime任务身份及公开ConversationLoop，
规范ToolResultPart由Mira回填；只注册wait，权限禁止时不注册。最多16次推理、32次工具，
实际推理轮数按Profile输出预算收紧（默认7），无自动截图或桌面工具。
M6-18已接入PR#76的通用loop及SNI修复，SiliconFlow与MiniMax均实测规范wait工具往返。
流式预览由Mira提供，保持临时且非权威，终态回复才进入历史。

IPC请求最多16个、事件通道128条；事件缺口读取历史恢复，终态投影拒绝迟到pending。
服务断开显示错误，可在模型页重新连接；不静默自动提交。模型草稿与已应用配置
分开：composer只显示服务确认的模型；事件缺口也重新读取模型，解除丢失回执的等待。退出UI停止worker、消费future并
关闭其Executor，不终止已有Runtime Service；独立入口/托盘退出确认保留M6-04。

## ZCode会话页对齐（M6-05 / DEC-035）

以公开main 29628c9的Timeline/RowView/Composer实现为依据。源代码精确参考，
运行中Wayland窗口截图被系统拒绝，未完成其像素对照。M6-11更新后用户气泡10px圆角、
12px水平/8px垂直内边距；回复使用EUI Markdown。消息悬停复制/选段引用为真实交互，
引用以用户文字上下文随发送，容量及ACK保护见DEC-035；上下文面板不假造token使用率。


M6-06按[DEC-036](../decisions/DEC-036-context-usage-presentation.md)补齐上下文圆环：
最近成功请求输入Token/显式配置窗口预算，点击详情；未知不显示虚假0%。


## 模型配置与提交意图（M6-07 / DEC-037）

以 ZCode `ModelProviderSectionLayout` 的服务商导航和右侧详情为布局依据；
宽 ≥700px 时内部导航176px；小于700px时使用全宽命名服务商选择器，保留可滚动表单。
至多12个命名配置，目录位于settings.models，活动配置位于settings.model。
切换只在service ACK后更新composer，活动任务/存盘失败拒绝切换；API Key仍经环境SecretRef。

每轮IPC携带access和reasoning；read_only不注册工具，default仅当前wait。
模型声明supports_reasoning后才提供Mira公开minimal/low/medium/high，默认省略。
文本附件由前端Executor有限任务读取；只接受普通文件，8KiB/4个上限，UTF-8校验，
拒绝FIFO/符号链接（Linux），借generation与单调附件ID保护清空/发送ACK竞态。
附件以明确不可信用户上下文送入现有TextPart，未开放Mira file/image能力。

## 旧前端退役（M6-08 / DEC-037）

ui、CEF shell和Web devbridge不再参与源码/依赖/CI/安装包；依赖锁schema3明确native。
保留C++ IPC golden及全部服务/平台边界测试；只移除随组件删除而失效的三个专用桥测试。
Linux安装包只携带native、Mira图标和必要字体，不携带EUI示例音乐/图像或Chromium。
旧文档和M5验收仅为历史；完整进程退出确认和Windows真机验证仍未完成。

模型替换先构建并验证新适配器，再持久化配置并交接旧适配器。生产 SocketHttpTransport 的 worker_name 使用各自 ModelProfileId，避免默认名称在新旧模型重叠期间冲突；同一时刻最多持有旧、新两层，失败销毁候选层并保留旧配置。全部传输 worker 仍由服务唯一 Executor 管理；不创建自有线程或调度器。

紧凑模型页（内容宽 <700px）使用全宽命名服务商选择器替代图标栏，打开后先显示配置名称再选择；Escape/外部点击关闭列表。宽屏仍为176px命名导航。用户任务文本放在序列化附件/引用之前，纯附件轮次以“附件：文件名”开头；会话标题略过首部空白，纯空白回退为“新对话”。

## 直接 API Key 与草稿/历史生命周期（2026-10-05）

依据 [DEC-038](../decisions/DEC-038-api-keys-and-draft-sessions.md)，API Key 默认以掩码输入，
眼睛开关只显示本次输入，已存密钥不回传。留空保留、显式移除后保存清除；供应商切换/取消修改/
保存成功丢弃密钥草稿。单行高44px、字号16px，显示开关36px；沿用原生模型页的六行表单。
EUI 密码输入临时适配局限见 [EUI-20261005-004](../dependency_feedback/eui-ledger.md)。

新建仅分配 UI 草稿；未发送不调用 session.open，也不在侧栏显示。首次发送冻结提交选项，
open ACK 后提交，接纳/真实消息到达才展示历史；重复新建复用无消息的空白草稿。
侧栏历史行保留48px高度，文字为16px、按留出垃圾桶的宽度省略；垃圾桶36px，活动/提交/删除
等待时禁用。删除确认展示首条输入和最近消息摘要，460×316px；关闭焦点后的后台输入受保护；ACK 后才移除本地并重新读取历史，
失败保持记录。删除当前会话回到空白草稿；删除其他会话保留当前选择和草稿。

session.delete 对普通会话复用 Mira close；遗留主会话清除产品对话但保留设备默认身份与任务审计。
持久化删除成功前不改变内存身份。迟到事件按本地稳定 ID 查找，无法复活已删除记录。
所有平台凭据 I/O 沿既有 Executor handler / Mira blocking transport worker，不增加线程或队列。

重连按服务端 session.list 和空历史快照清理已删除的稳定记录；活动请求与本地草稿保留。

## 选段引用与最后输入编辑（2026-10-05 / DEC-039）

正文16px/24px，Markdown标题20/18/17px、块间8px；用户气泡无边框，圆角10px、
内边8px/12px，消息行间14px。消息动作缩至28×26px、12px图标，只在正文/动作区悬停
或键盘聚焦时显示；最后已终结用户输入增加编辑。移除整条引用按钮。

拖选同一消息后保存可见文字范围，在选区附近显示100×32px“引用选段”；首行上方无空间
时放到下方，横向钳制并避开composer。引用保存选段、角色、消息ID和独立实例ID。
Ctrl+C复制选段；Escape、背景点击、滚动、切换、编辑、设置/弹层及来源消息移除取消选区。
通过EUI公开排版后frame和InputModel/TextPrimitive度量适配选择，几何仅按下时采集，
限制16KiB/8192段与当前80消息；移除消息释放缓存。能力缺口EUI-20261005-005仅本地登记，
不修改pinned依赖。跨消息、自动滚动与链接打开未实现。

编辑写入会话草稿，顶部32px提示条显示取消；提交前保留原记录，取消恢复之前的草稿。
请求携带精确replace_turn_id，服务只允许最后已终结轮次，活动/过期目标拒绝。复用原
Executor owner与PhaseGate完成启动协调，接纳且保存成功后ACK/事件标记replaces_turn_id；
历史快照清理旧消息，迟到旧事件不复活原轮次。用量重置未知，新成功回填真实统计。
保存或接纳失败恢复原对话；接纳后推理失败显示新失败轮次，不恢复已经被用户替换的旧输入。
实际模型输入排除旧用户文字与旧回复，保留更早历史；不撤销旧工具对外部世界的副作用，
Mira旧Task终态保留审计。

历史会话首次harness提交经MiraHost公开open_session获得当前运行ID；串行服务维护
稳定产品ID到运行ID的有界映射，Task/OperationContext使用运行ID，IPC/持久化使用产品ID。
列表读取对应运行状态，删除/关闭释放两侧资源；该映射不持久化，不扩展桌面task.submit。

参考ZCode的MarkdownSelectionTooltip/useTextSelection与会话操作源码（29628c9a），
官网页面内容可读，浏览器打开超时，未完成运行中ZCode像素比较。
[验收](../compatibility/native-conversation-revision-20261005.md)区分合成原生渲染与Mira请求证据。

## 固定字面与行内对齐（DEC-040）

M6-12 使用完整 Noto Sans SC Regular 2.004 官方 OTF，压缩归档/精确来源/摘要/OFL
许可随仓库记录，CMake离线解包并复制到应用assets，Linux与Windows打包包含字体与许可。
Markdown行内中文、拉丁与标点使用统一行框，避免逐字ink-center产生上下跳动；单行控件
与图标继续整体居中。字号、主题、代码monospace和布局保持现有契约。公开DSL临时适配
引用EUI-20261005-006；不改第三方依赖。Linux原生混排/输入/选段证据见
[native-typography-20261005](../compatibility/native-typography-20261005.md)，Windows与
真实IME仍由维护者在目标环境补跑。

字号以设计EM为准，Noto SC经typography.hpp乘1.448换为EUI请求单位；测量/换行/
输入/选区一致。正文行高24保留，composer测量匹配InputBuilder请求字号×1.2，
Markdown标题行高请求字号+6；代码/图标不套中文系数，反馈EUI-20261005-007。

2026-10-05 / M6-13：输入栏模型按钮按实际字体测量与EUI公开ButtonBuilder图标、间距、内边距计算，
宽96–180px并受剩余空间约束，超长名称省略；不拉伸占满工具栏。思考与发送继续右对齐，
完整ID在管理模型设置中查看，模型标识和切换流程不变。

2026-10-05 / M6-14：按ZCode WorkspaceHeaderSections的顶部任务名称/长文本截断层级，
在既有60px标题栏显示当前会话标题，空标题回退“新对话”。顶部60px标题栏左侧显示当前会话标题（16EM、现有500权重请求），空标题回退“新对话”，随当前会话切换更新；左边距为侧栏右侧28px，侧栏收起时从x=116px开始，右侧预留146px用于窗口按钮；收起侧栏的设置页右侧预留286px避开返回对话按钮。标题按字体度量省略且不改变原始值，文字不拦截标题栏拖动。


## DEC-041：当前会话密度、进度与输入法（2026-10-05）

本节覆盖之前各轮的视觉数值：正文/输入14EM、行距22px，Markdown标题18/16/15EM，
品牌18EM/28px图、欢迎26EM、顶部标题14EM、历史行40px、新建/返回按钮36px，设置标题24EM。
输入最小50px、上限168px，沿用Noto Sans SC的统一EM测量。附件/权限留左侧；上下文、
实际字宽模型按钮、思考、发送为右侧组，最小860px和长名称均无重叠。

等待行显示轻量三点动效与单调时钟用时；真实预览到达后渲染Markdown，完成后显示本次
观察到的耗时，恢复历史缺少计时不估算。既有Executor仅在活动时注册100ms周期刷新，
停止时取消并在外部owner关闭。IPC预览完整快照限16KiB，订阅显式chat_preview=true；
旧订阅只收到原有事件。客户端校验会话/轮次/单调序列，终态或取消后拒绝预览，事件缺口
仍用历史恢复，不把预览当规范输出或Token统计。

EUI平台层补齐Linux XIM PreeditPosition与窗口内光标行底定位；不在产品读取XIC私有布局。
X11/IBus/libpinyin候选移动和中文提交已实测，原生Wayland/物理高DPI/Windows仍待目标环境验证。
依据[DEC-041](../decisions/DEC-041-native-conversation-density-and-progress.md)，
[验收](../compatibility/native-conversation-progress-20261005.md)。


2026-10-06 / M6-20：长回复跟随由UI线程持有明确状态，底部跟随、上翻保位、终态保位；
回到底部/箭头/键盘恢复，新轮重置。通过EUI公开offset控制，EUI-20261006-001修复同步
配置到实际Runtime滚动并失效重绘，已更新上游PR#88与锁。原生回归比较实际阅读视口像素。
私有真实窗口IME、多行候选、模型流式/用时/上下文已复验，见
[最终验收](../compatibility/native-conversation-finish-20261006.md)。窗口关闭投递平台空事件
以解除UI idle wait，仍仅退出UI；无新增Executor任务或桌面/RPA能力。


## 服务编辑与侧栏对齐（M6-21 / DEC-042）

服务配置严格对照ZCode 29628c9的SectionLayout、Navigation、ProviderCardSections与ProviderApiFormatSelect。内部服务导航为224px；详情可用宽小于700px时保留56px图标栏，不再切成全宽选择器。导航32px行高、12px面板内缩进；详情24px内缩进。右侧服务标题18px，空服务直接编辑名称，已保存服务在标题更多菜单中重命名/删除。Base URL包含origin和路径，API格式使用下拉菜单（OpenAI Chat Completions / Responses），随后是默认遮蔽的API Key、模型列表、添加模型及当前模型的上下文/思考配置。标签14px，输入32px、inset12px；模型行36px，外层40px。页底状态、取消修改和80×32px保存固定于详情内；保存未就绪时降低按钮不透明度。

服务与模型分别呈现，settings.models仍有12条总预算；provider_id/provider_name是可选产品元数据，旧display_name单模型配置兼容。没有目录时左侧显示本地“未命名服务”；填名称后先保留该草稿行，保存成功ACK才更新左侧名称、目录与live_model。无模型服务允许保存为停用。添加/删除模型在独立编辑副本中进行；共享连接与Key按服务一致更新。显式models空数组清空目录，缺失表示旧客户端保持目录。删除服务释放不再被目录/活动模型引用的Key；清理失败返回警告。保存失败保留全部编辑、Key草稿及已应用模型；取消修改按DEC-044从最近已确认目录恢复当前服务，不依赖再次读取。新增模型前保留当前模型的窗口与思考编辑。支持reasoning_effort时可开启思考深度，默认不传参数。

本轮基于公开源码逐项对照和原生实渲染取证；未取得运行中ZCode原生窗口截图，不宣称像素级1:1。Mirage尚未接入的Anthropic、OAuth/套餐、模型连通性探测和额外模型元数据不展示伪实现，差异及补齐条件见M6-21验收记录。

侧栏采用20px行外缩进，品牌/导航/历史图标中心x=44、文字x=64。保存经既有RuntimeBridge/Executor IPC，未新增任务或平台设施。配置存在性与provider元数据是Mirage产品职责，Mira模型网关/公开Profile和生命周期保持复用。

## M6-22：预设与Messages接入

DEC-043在既有ACK/密钥事务下增加11项API Key预设及anthropic.messages.v1，来源固定ZCode config/provider模板与MiniMax官方文档。用户选择预设后只需Key保存，其他连接字段仍可编辑；未知窗口预算保持0，MiniMax-M3按官方1M窗口填充。模型层Messages普通文本/工具/SSE已接入；extended thinking明确停用。侧栏24px外边距/48px图标中心/68px文字轴，服务标题8px内边距且静态显示同轴。MiniMax图片测试使用Mira公开Provider发送合成PNG；当前会话文本附件边界不变，不宣称图片上传已交付。

M6-22 / BUG-20261006-001：密钥遮蔽Adapter的回调读取当前owner持有草稿值；同批次输入从首个undo快照重建整体编辑。成功编辑置dirty并请求页面刷新，保留加载/保存门禁与ACK权威；组件隐藏状态仍仅含掩码。

## M6-23：模型编辑器交互恢复

依据DEC-044，ACK目录与当前编辑副本分别更新；取消恢复当前服务/模型，离线可用。模型增删使用稳定ID并保留选中参数；新模型继承服务开关，容量含其他服务。未保存导航提供继续编辑/放弃选择，服务删除单独确认。内联添加有取消入口，未确认ID不能被保存遗漏；菜单在表单外收起/互斥，加载/保存/弹窗期间门禁覆盖键鼠及IME，键盘Repeat不再次激活。Key粘贴仅处理首尾空白，非法字符有可恢复提示；不增加API/持久化/并发设施。[Linux真实交互与边界验收](../compatibility/model-settings-interactions-20261006.md)。

BUG-20261006-003/004修订加载门禁：初次连接前允许本地预设选择及API Key草稿输入/显隐，其他配置与保存等待ACK。Key草稿参与本地取消、切换和离开页面的未保存确认；首次ACK恢复所选预设已有的地址、模型和凭据引用，同时保留新Key及未保存提示，不自动提交。未命名服务的Key草稿也不被目录加载覆盖。保存或弹窗期间仍禁止键鼠/IME编辑，已有ACK目录独立更新，不引入其他凭据副本或后台设施。


2026-10-06 / M6-25 / BUG-20261006-006：最小化后 EUI 暂停 compose，旧产品 open
只能把恢复事件排入 UI 队列，Dock/托盘打开无法到达 window.show。现由托盘的
FrontendProcess 平台实现直接匹配所属子进程窗口，请求 WM 恢复，再由既有 IPC
状态处理继续 UI 显示/确认。GNOME 实际应用条目及私有 ICCCM/EWMH 夹具补验，
证据见 [托盘验收补充](../compatibility/tray-runtime-owner-20261006.md)。


## 托盘菜单入口（M6-25 / DEC-045）

左键单击常驻托盘弹出“打开应用”“退出应用”两项菜单。“打开应用”经托盘 Runtime
复用/恢复所属前端或重建已关闭的前端。“退出应用”无活动工作时整体关闭；有活动
Agent/任务/Workflow 时先恢复前端显示退出确认，取消保留工作，确认后取消并收敛。
Linux 菜单由 StatusNotifierItem/DBusMenu 宿主呈现，Windows 使用平台弹出菜单；
状态保留在 tooltip，菜单不展示暂停/恢复。既有 Executor owner/有界动作通道不变。


Linux 应用列表的新条目执行 mirage start。为兼容 GNOME 缓存的旧 mirage-native
命令，未携带托盘端点的已登记 GIO 桌面调用在任何窗口初始化前 exec 同树统一
启动器。启动器先等待托盘注册，再由托盘创建所属前端；已有托盘只发 open。
托盘子进程带明确端点，继续注册/所属 PID 准入，不转发或递归启动。依据 DEC-045，
BUG-20261006-007；此兼容只用于产品桌面入口，未放宽直接 CLI 的前端准入。

## 会话思考与恢复（M6-26 / DEC-046，2026-10-07）

思考控件使用 Runtime 在 model.get/model.set 的 settings_json 中投影的
model.reasoning_options；UI 不直接引用 Mira/Integration 类型，也不决定模型能力。
服务按实际模型验证每轮 reasoning，回传字段只读，保存时忽略客户端能力列表。
设置页移除重复启用按钮。MiniMax-M3 的默认/关闭/开启分别省略参数、发送 disabled、
发送 adaptive；M3.1 Flash Preview 的深度映射 output_config.effort，不能关闭。
已核对的 Claude 自适应模型提供相应档位，未知模型保留默认。选项切换后每轮重新验证，
不支持的旧选择归零，草稿不受影响。正文仍不展示思考内容。

持久化与恢复排除无 journal、无已结算 chat turn 的空记录，主会话每次运行重新创建。
默认服务容量为主会话加24个名额，与原生 UI 预算一致；自定义限额仍严格执行。
恢复超限非空历史须拒绝启动并保留磁盘内容，不部分恢复后覆盖历史。
