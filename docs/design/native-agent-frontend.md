# 原生 Agent 对话页

当前入口为 `mirage-native`（M6-01/02/03，DEC-033/034），一个 EUI-NEO GLFW/OpenGL
窗口进程。UI 主线程持有有界 `ChatModel`，视图以 EUI DSL 合成，私有窗口适配负责
系统窗口动作。RuntimeBridge 通过 Executor blocking worker 连接独立 Runtime Service，MpscChannel 将有界事件交回 UI；模型请求在服务内执行。

界面最多显示24个会话、每会话最近40轮（80条消息），每条草稿最多16 KiB。
会话、轮次和回复以IPC快照/事件为事实源；草稿、主题、侧栏宽度保留在UI进程内。
提交后显示运行中，终态回复或明确错误；停止按钮调用session.chat.cancel。
当前服务重启恢复的旧会话没有pinned控制面对象，仅保存历史，新轮次使用本次服务的活动会话。

## Direction contract

THESIS：桌面 Agent 的对话工作台；先读清当前会话，再在底部描述任务。

OWN-WORLD：参考 ZCode 的 Zai Light/Dark 中性灰、窄侧栏、细分隔线、紧凑工具条，
深色实心发送按钮；中文系统无衬线字体，图标使用 Font Awesome。

STORY：用户在设置配置模型服务，返回会话提交文字任务，读取Mira回复或明确失败；
必要时停止轮次。通用harness先于RPA，当前不提供屏幕操作或workflow功能。

FIRST VIEWPORT：1180×800 无边框窗口，260px可调侧栏与60px原生标题条保留；
空会话为居中问候与672px输入框。有消息后，右对齐轻灰用户气泡、无边框Markdown回复，
主区宽≥864px时阅读列扣除96px留白，最大896px，底部16px圆角输入随文字增高。
加号/模式/模型/上下文/发送构成紧凑工具条；最小860×620保持操作可达。

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

“模型”位于设置分类下，右侧滚动表单包含服务origin、API路径、模型ID、凭据环境变量名称，
Responses/Chat Completions二选一。保存通过model.set应用并合并写入服务配置，保留其他块；
活动轮次时忙拒绝，配置校验或存盘失败保持当前模型。API Key只从服务环境经SecretRef解析。

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
运行中Wayland窗口截图被系统拒绝，未完成其像素对照。用户气泡12px圆角、
16px水平/12px垂直内边距；回复使用EUI Markdown。消息复制/引用为真实交互，
引用以用户文字上下文随发送，容量及ACK保护见DEC-035；上下文面板不假造token使用率。


M6-06按[DEC-036](../decisions/DEC-036-context-usage-presentation.md)补齐上下文圆环：
最近成功请求输入Token/显式配置窗口预算，点击详情；未知不显示虚假0%。
