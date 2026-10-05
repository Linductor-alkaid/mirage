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
ZCode整页布局替代。现行正文/输入16px，composer随内容增高，取消三条建议。

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
随构建路径变化需重新注册，不替代后续正式安装包入口。

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

会话发送session.chat(agent=true)，使用MiraRuntime任务身份、ModelGateway、公共工具解析器
和BuiltinToolRegistry；只注册wait。16次推理、32次工具、2KiB单工具/8KiB反馈上限，
网关输出token预算也可提前拒绝。工具结果为来源标注的JSON文字回填，MIRA-20261004-001
记录缺少通用loop及规范tool-result输入的问题；不是直接调用设备AgentLoop。
无自动截图、桌面工具或RPA。本机SiliconFlow实际文字/工具循环已通过，MiniMax
受pinned TLS缺少SNI影响（MIRA-20261004-002）。原生tool-result互操作未验收。

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
