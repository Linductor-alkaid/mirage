# 原生 Agent 前端

EUI-NEO dev 固定到 4691fc0（完整 pin 见 dependencies.lock.json）。当前页面参考
ZCode 的侧栏、阅读列和 composer，C++20 / GLFW / OpenGL，没有 Chromium。

在仓库根目录构建：

```bash
git submodule update --init --recursive
cmake --preset native-release
cmake --build --preset native-release --target mirage-native-product
# 统一入口启动托盘内的 Runtime，再由托盘启动独立前端
./build/native-release/apps/mirage start
```

`mirage-native-product` 同时构建 launcher、service、tray、native 和 pet；native
presets 显式开启 pet，不依赖旧 cache。已有托盘 owner 时启动入口会复用它，
加载新二进制需在任务空闲时正常退出托盘再打开，不能只覆盖文件就认定升级完成。

Debug 使用 native-debug。Linux 需要 OpenGL/Mesa、X11 开发包及常见 CJK 字体，
例如 fonts-noto-cjk；当前通过 X11/XWayland 运行。依赖全部使用 pinned 仓库内
bundled 源码，不在 configure 时获取浮动依赖。Windows preset 可配置，但未真机验收。

- 空会话居中输入，消息出现后输入固定到底部并随文字增高；Enter发送，Shift+Enter换行，Ctrl+Enter保留发送兼容。
- Agent回复支持Markdown；复制动作仅悬停或聚焦时显示。拖选同一消息后弹出“引用选段”，最多4段、总8KiB，和草稿编码后合计最多16KiB。
- 输入工具条依次提供附件、访问权限、上下文比例、模型、思考深度与发送/停止。上下文圆环显示最近一次成功模型请求的输入Token占比；点击查看用量、配置窗口预算、百分比与当前引用数。缺失用量或窗口预算时显示未知，不从草稿/bytes估算。
- Ctrl+N 新建本地草稿，首次发送才创建远端会话并进入侧栏；反复新建复用空白草稿。历史行垃圾桶确认后删除，运行中须先停止；侧栏切换保留各自草稿；底部齿轮打开设置，在“外观”中选择浅色/深色。
- 拖动顶部空白移动窗口，边缘/角缩放；右上角最小化、最大化/还原、关闭。
- 运行时发送按钮变为停止；停止当前轮次采用协作取消。未绑定远端的草稿清空需要确认。
- UI不限制会话个数、每会话显示最近40轮（80条消息），草稿16 KiB；服务准入与推理预算显式拒绝。

模型设置在齿轮 → 模型：填写HTTP(S) origin、API路径、模型ID、协议及 API Key（默认遮蔽，可切换显示）。
可选上下文窗口预算为2048–2000000 Token，留空/0表示未知；填写供应商支持的窗口预算，
它同时应用至Mira ProfileLimits，不自动发现供应商容量。
API Key 保存在 Linux Secret Service / Windows Credential Manager，service.json 仅保存引用。
已存 Key 不回传界面；留空保留，点击“移除”后保存才清除。系统钥匙环不可用/锁定时提示失败，
请解锁后重试；不使用明文后备存储。旧 credential_env 配置保留读取兼容，新增配置直接填 Key。
服务首次未连接时可先选择预设、输入或粘贴Key草稿，切换/取消提供未保存确认；点击模型页上方刷新重连。
连接后保留新Key并恢复该服务保存的配置，再点击保存应用。未连接时不写入配置或钥匙环。
保存会合并写入服务的 service.json 并应用；已有活动轮次时先停止。
Linux 已验证系统凭据与重启调用；Windows 凭据路径尚待目标环境验证。
当前 EUI 临时密码适配不支持撤销/重做，遮蔽时复制只得到掩码，见 EUI-20261005-004。

当前接入真实 IPC 和 Mira ConversationLoop。Linux 产品托盘按实际 Provider 注册桌面观察、
完整键鼠输入与 wait；截图须为模型显式开启「识图」，OCR/编号图须另配置本地资源。
通用循环由Mira公开ConversationLoop承载，使用规范工具part回填；MIRA-20261004-001/002
已升级复验。SiliconFlow与MiniMax真实文字、工具往返与提前流式预览均通过。
当前正文/输入14EM、22px行距，上下文/模型归入右侧组；等待有动效及真实用时。
Linux XIM/IBus候选跟随光标并已验证中文提交；原生Wayland、物理高DPI与其他IM待验。
依赖修复PR与完整证据见[本轮验收](../../docs/compatibility/native-conversation-progress-20261005.md)。
应用列表首次打开会先启动托盘，注册就绪后由托盘打开窗口；托盘已驻留时只打开或
恢复窗口，不新增托盘实例。Linux 兼容 GNOME 缓存的旧前端命令，将已登记的 GIO
桌面调用转发到同树统一启动器；直接 CLI/显式端点仍须通过前端所属 PID 准入。

关闭窗口仅退出前端；Agent 服务和任务在托盘继续驻留。左键单击托盘显示菜单，“打开应用”重开或激活
单个窗口；“退出应用”有活动 Agent/Workflow 时显示取消/停止确认，再回收整个应用。
`mirage start --no-shell` 仅驻留托盘，普通启动会复用它。前端在创建窗口前验证托盘
及所属子进程身份，不能直接启动或连接 headless Service。Linux 需要 SNI 通知区宿主；
注册失败会明确拒绝启动，不打开孤立窗口。进程契约见 DEC-045；安装包/Windows 仍属 M6-04。

源码：app.cpp（视图）、chat_model.*（UI线程有界服务状态投影）、window_controls.*
（私有GLFW窗口适配）、runtime_bridge.*（Executor IPC owner）。任何后续后台任务必须使用 Mira Executor，禁止调用 EUI
async/network/audio 来绕过项目生命周期约束。

EUI 当前构建和图像解码问题与临时适配见
[反馈记录](../../docs/dependency_feedback/eui-ledger.md)；已授权修复以独立上游PR提交锁定，
不在应用构建时静默修改依赖。
计划与验收见 [M6](../../docs/plans/m6-native-frontend.md)。

应用继承 Mira 的红发蓝眼角色图标，来自维护者指定的本机最新 Mira，
原始 PNG 与来源摘要保存在 assets/；CMake 随构建复制资源，运行无需 ~/mira。
Windows EXE 的 ICO 资源已配置，尚未在 Windows 构建验证。

侧栏使用像素完全一致的 mira-ui.png，移除元数据以规避 EUI 的 PNG/SVG 误判
（EUI-20261004-003）；原图及完整元数据保留，窗口图标仍使用原图。

Linux 开发窗口的 Dock 图标需要与窗口 appId 相同的桌面条目。显式注册当前构建：

```bash
cmake --build --preset native-release --target mirage-native-register-desktop
```

目标写入 `$XDG_DATA_HOME/applications/org.mirage.native.desktop`（未设置时为
`~/.local/share/applications/`），引用当前构建的 `mirage start` 启动器、独立前端/托盘路径和 PNG。普通构建不会修改
用户桌面配置。重新打开应用后生效，应用列表也可通过 Mirage 启动；清理构建树或更换
路径后需重新注册。此开发入口不等于 M6-04 的安装包/托盘入口。

会话栏分隔线支持拖动调宽（224–400px，最小窗口会限制上限），点击分隔线后
左右方向键每次调8px。收起/展开保留宽度；设置里的“返回对话”保留当前草稿。
Ctrl+, 打开设置，Escape 先关闭弹层再返回对话；设置中 Ctrl+N/Ctrl+Enter 不操作
隐藏会话。主题和侧栏宽度只在当前进程内记忆，关闭后重置。

会话页参考ZCode main 29628c9的ConversationTimeline、ConversationRowView及ChatPromptEditor；
源码布局对齐及实机验证见[会话验收](../../docs/compatibility/native-zcode-conversation-20261004.md)。
EUI Markdown通过公开排版适配支持同一消息拖选；尚不支持跨消息、自动滚动或点击链接。引用展开为用户文字上下文，
不冒充文件附件。中文段间距临时适配见EUI-20261004-004。


2026-10-05 / DEC-037：旧 `ui/`、CEF 壳与 Web devbridge 已删除，native 是唯一前端。
该退役工作原为三进程入口；2026-10-06 由 DEC-045 改为托盘内嵌 Runtime 加独立
前端。`--shell PATH` 可指定前端，`--no-shell` 仅驻留托盘；`--no-tray` 必须同时
`--no-shell`，只用于 headless 开发。`MIRAGE_NATIVE_SOCKET` 指定 IPC 路径但不能
绕过所属进程准入；托盘再次打开窗口沿用原端点。Linux 生命周期证据见
[托盘验收](../../docs/compatibility/tray-runtime-owner-20261006.md)。

模型页采用 ZCode 服务商分栏，可添加至多 12 个命名模型配置。保存并应用会合并持久化目录，
活动轮次中切换被拒绝，失败保留已应用配置。输入栏从左到右为附件、访问权限、上下文圆环、
模型选择、思考深度、发送。只读保留已注册的观察工具；默认权限可调用已注册的修改工具。
实际权限仍由服务策略判定，文件读取另受已配置路径范围限制。每个会话保留自己的权限与思考档位。

附件选择需 Linux 的 zenity/kdialog（Windows 使用系统对话框）；只接受 UTF-8 普通文本，
最多 4 个、合计 8 KiB。附件与引用、草稿受 16 KiB 总请求上限；展开可查看并移除，
发送成功回执只清除本次提交的附件。加载错误可见，清空后迟到加载不会重新填入附件。
当前不支持图片、PDF 或目录附件。原生模型页启用 reasoning_effort 后，提供默认/最少/低/中/高；
默认省略参数，其他档位经 Mira generation API 映射。启用表示用户配置的供应商能力，
供应商仍可能拒绝，失败会直接显示，不静默重试。

### 选段与最后输入编辑（DEC-039）

最后一条已终结用户输入悬停显示编辑按钮，点击后在输入框修改，取消恢复原草稿。
重发接纳并持久化成功后替换原轮次的用户文字与Agent回复，更早历史保留；实际模型输入
排除旧轮次。运行或提交期间禁用编辑，拒绝保留原历史。工具的外部副作用不会撤销。
重启后的历史会话可延迟重新绑定Mira运行会话继续harness对话；删除/关闭释放对应映射。
[Linux验收](../../docs/compatibility/native-conversation-revision-20261005.md)。


2026-10-06 / M6-20：底部流式跟随、上翻阅读保位、终态保位，箭头或键盘返回最新。
EUI公开offset同步修复已按维护者授权追加PR#88；依赖源码不在应用构建时注入补丁。
真实原生窗口IME多行候选、实际模型预览/等待用时与上下文验证见
[最终验收](../../docs/compatibility/native-conversation-finish-20261006.md)。

思考方式/深度直接在会话输入栏选择（DEC-046）。MiniMax-M3 提供默认/关闭/开启；
M3.1 Flash Preview 提供官方深度。服务投影可用选项；OpenAI兼容模型可在设置页
按模型声明思考能力，未声明的未知模型仅默认。空的运行主会话不再累积进历史，重启仍保留非空会话。


2026-10-08（DEC-051 / M6-33）：侧栏开合采用220ms三次减速，主列随可见宽度连续定位；
明暗主题在240ms内同步插值文字、底色和边界。反向切换从当前值继续，拖动侧栏直接跟手。
外观页“减少动态效果”可直接切换，偏好只在本次运行内保留。
模型页可逐模型声明OpenAI思考能力，Anthropic选项沿用按模型ID核验的配置；保存后会话页
显示服务确认的思考档位。填写Base URL和Key后点击“获取模型”，候选最多256项，点击加入
编辑副本再保存。已存Key只用于完全匹配的服务地址/协议，地址或Key变更会使旧候选失效。
目录请求由Runtime Service的Executor与Mira HTTP传输承载；独立IPC连接隔离网络等待，
会话控制按Executor SerialExecutionContext保持提交顺序。验收见
[验证记录](../../docs/compatibility/native-motion-model-discovery-20261008.md)。


<a id="桌面工具预览m7-12--linux-xorg"></a>

### 桌面工具预览（M7-12～14 / Linux Xorg）

原生产品默认自带固定 Mirador / ncnn CPU / PP-OCRv5 资源，无需配置 OCR 环境：

```bash
cmake --preset native-release
cmake --build --preset native-release --target mirage-native-product
./build/native-release/apps/mirage start
```

首次启动且没有 vision 设置时，自动加载包内资源。模型与字典在构建期固定和校验，
安装包运行不下载、不需要 Python/Paddle。模型页开启“识图”后，截图及编号图可进入
模型请求；桌面权限按会话策略判定。

“设置 → 桌面工具”保留自定义清单入口。修改后从托盘退出整个应用并重启，仅关闭窗口
不会应用变更；明确保存空清单则保持关闭。资源与许可证见
[默认包审计](../../docs/supply-chain/bundled-vision-20261011.md)。

也可首次启动时传递 `mirage start --ocr-manifest /absolute/path/manifest.json`。
这个覆盖只作用于新托盘，已有托盘时明确拒绝；普通 start 继续复用。配置了损坏/丢失的
资源导致启动失败时，先 `mirage start --ocr-manifest ""` 暂停 OCR，进入设置清空或修正路径，
保存后正常退出、重启。空覆盖不改写原配置。原生 native-debug/release 默认带资源；开发者如需瘦构建，可同时设置
`MIRAGE_WITH_PPOCR=OFF` 和 `MIRAGE_BUNDLE_VISION=OFF`，瘦构建不能用于产品打包。

会话选择默认权限后可以描述桌面任务，选择只读时保留观察并禁用修改。工具过程展示调用、
目标及证据；派发不等于任务完成。Linux Xorg 是当前预览范围；完整 Windows/原生 Wayland、
完整 workflow 资产审阅、自动选择/恢复以及跨应用可靠性另验；本段桌面工具入口不作为这些能力的交付证据。

Linux 产品启用检查、原生 MiniMax OCR/编号图轮次与坏配置恢复已取证，见
[验收记录](../../docs/compatibility/product-desktop-vision-20261011.md)。本项不覆盖任意应用的
任务可靠性；先从观察类任务试用，再按目标应用单独验收修改与后条件。

M7-11 的本地语义与 OCR/模板知识已能经同一 Runtime 的 workflow CLI/IPC 执行，无需
模型逐次定位；默认视觉包直接提供 OCR。原生对话自动选择资产与恢复尚未接入，当前
使用[视觉 workflow 示例](../../docs/examples/local-visual-workflow.md)。

### 工作流运行与显式恢复（M15-20 / DEC-149，2026-11-16）

M15-20 首步采用标题栏主入口；现行入口已按 M15-21 / DEC-150 改为侧栏左上
「会话 / 工作流」。工作流侧栏的「运行与恢复」打开既有控制页，再次点击返回流程图；
侧栏收起时标题栏仍可切回对话。切换保留既有草稿与历史。打开工作流空间和点击
「刷新」读取服务目录、运行记录与学习存储状态；资产编辑见下节，CLI/IPC 导入路径保留。

- 目录中已发布且 runnable 的版本可点击「运行默认参数」；使用已确认的版本 digest，
  参数为默认值，服务仍负责权限、准入和验证。接纳后刷新查看实际 Run 状态。
- 选择运行后，分别查看 Run 状态和恢复尝试；等待 Agent 时可「请求恢复」，随后显式
  「查询恢复结果」或「取消恢复」。「停止运行」针对未终结 Run；取消恢复不等于停止 Run。
- 恢复请求接纳和恢复结算都不等于 Run 完成，需刷新确认终态。Run 已完成、恢复成功且
  学习开启时可「保存恢复经验」；Lesson 的保存成功/失败单独显示。
- 事件只提示刷新。断线、事件缺口或失败时保留旧快照并标记过期，禁用依赖状态的操作；
  拒绝提示先刷新，再按确认状态操作。断线先在模型页重新连接，再返回刷新。

每次运行最多恢复一次；首次绑定恢复后，模型配置在该服务生命周期内固定，切换模型
需要退出并重启服务。取消后也须查询结算，不会立即释放模型占用。本页没有自动恢复、
重试或后台轮询。UI 投影最多128项目录、256项运行，一次只允许一组最多三项读取或
一项控制，全部经既有 RuntimeBridge/Executor 路径。

本增量复用中性明暗、Noto Sans SC、800px列与860×620最小窗口；独立复核后的两项修正
（选中行对比度、拒绝后先刷新提示）已评分 resolved，仅覆盖该修正清单。
证据为隔离服务/脚本 provider 与 Linux 原生合成状态渲染；未作在线模型、真实桌面、
Windows 或完整 M15 验收声明。详见[决策](../../docs/decisions/DEC-149-native-workflow-controls.md)、
[验证记录](../../docs/compatibility/native-workflow-controls-20261116.md)与
[修正评分](../../.impeccable/review/m15-workflow/finish-verdict.md)。

<a id="workflow-graph-calls"></a>

### 工作流图与独立调用（M15-21 / DEC-150，2026-11-16）

侧栏左上选择「工作流」，从资产列表打开当前定义或点击「新建工作流」。主区左侧是
真实 Mira IR 的顺序节点、前置/完成条件、失败回退与有界回环，右侧是所选节点的检查器；
两侧可独立滚动。图沿用中性明暗、Noto Sans SC、Phosphor 与既有控件，常规1180×800、
最小860×620；收起侧栏可增加空间。本页不提供任意分支、并行调度或节点拖拽。

- 点击节点编辑任务名称、多行执行参数（JSON）、执行条件、完成验证及可选失败处理，
  可更换原子任务/被调用流程；既有 Control 节点可修改有界回环次数。「应用到草稿」
  只更新本地编辑副本；非法参数会显示错误并保留输入。
- 在检查器标记当前节点为任务段起点，再选择同一流程中的结束节点，提取连续任务段为
  独立草稿。段外节点结果依赖等不完整结构会拒绝；切换文档/返回/丢弃会清除起点标记。
  新草稿验证发布后，从原子任务目录插入其调用，不会自动替换来源任务段。
- 节点「进入」或检查器「进入工作流」打开被调用目标当前定义，「返回调用来源」回到
  来源草稿；切换和返回前须保存或丢弃未保存修改。
- 「保存」同步草稿，「验证发布」通过服务门禁后才可运行并被调用。写入检查读取时的
  `expected_head` 摘要，版本冲突不覆盖服务更新且保留草稿；新资产要求目标尚不存在。
  未声明 `safe_workflow_editor` 的旧服务只读，需要更新并重启服务后编辑。断线/事件缺口
  保留但禁用过期草稿；按提示丢弃修改、重新打开当前版本后再操作。

各工作流是独立资产，调用关系不建立父子所有权，也不固定目标版本。调用工具按
`workflow.<id>` 在分派时取得最新已发布目标；未发布草稿不替换目标，已开始的单次调用
不因更新重放。发布前检查直接/间接调用环路并拒绝，重复无环调用允许。默认调用深度2
是额外资源限制；图内有界 Control 回环与工作流之间的调用环路分别处理。

当前目录只在当前服务内保留，「保存」与「验证发布」不代表跨服务重启存储；页面在
编辑前与确认后均提示这一边界。长期学习元数据已独立持久化，不能用它代替目录保存。
M15-20 的运行、显式恢复与单 Run 一次/绑定模型至服务重启限制保持。完整候选审阅、
目录持久化、自动恢复和完整 M15 仍待推进。本轮证据为 Linux 合成状态原生渲染与隔离
服务/脚本 provider，不作为 Windows、在线模型或真实桌面验收。详见
[DEC-150](../../docs/decisions/DEC-150-workflow-graph-composition.md)与
[本轮验证](../../docs/compatibility/workflow-graph-calls-20261116.md)。

### 输入性能基准（M6-42）

```bash
cmake --build --preset native-release --target native_input_performance_test
(cd build/native-release/apps/native && ../../tests/native_input_performance_test)
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --unbroken)
```

私有 Xvfb/XDG 环境，不连接模型；输出编辑、布局、更新、渲染完成及总耗时的
p50/p95/max。覆盖 64/4096/16000 字节草稿、无历史/40轮历史，以及无换行长段。
软件渲染测量不代表真实桌面 FPS，方法与限制见
[验证记录](../../docs/compatibility/native-input-performance-20261017.md)。


2026-10-17：输入性能优化消费EUI有界字形advance缓存，并让历史滚动区复用公开
高度缓存；消息/工具/字体/视口变化精确失效，64KiB key超限安全回退。固定方法
3轮A/B覆盖的12种场景p50/p95均改善；软件渲染统计不代表任意设备60fps。升级
依据和四后端/许可证证据见[审计](../../docs/supply-chain/eui-text-performance-20261017.md)。


2026-10-23（M6-48/49）：当前会话使用精确源快照复用逐消息几何及过程段高度；
contentKey 改为紧凑 revision，超过 64 KiB 的历史仍可命中。缓存只保留当前会话，
至多 80 条、8 MiB 源快照与每条两个宽度，超预算安全回退。新增长会话基准：

```bash
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --long-history)
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --long-history --unbroken)
```

覆盖 40 轮各不相同的长回复、折叠思考，以及流式更新；标准 CTest 也包含多段长历史。
精确条件、前后数据和平台限制见[性能记录](../../docs/benchmarks/native-long-session-20261023.md)。
EUI 最新 dev 已合入 #107/#111/#114，集成保留未合入的 Windows 透明修复 #112，
见[升级审计](../../docs/supply-chain/eui-dev-20261023.md)。


2026-10-23（M6-50）：维护者授权的 EUI [PR #117](https://github.com/sudoevolve/EUI-NEO/pull/117)
新增公共 Markdown viewport 构建/布局复用，当前已消费。正文及 text 过程段传入局部视口，
保留完整滚动高度；选择开始时用公共完整 builder 按需取得全正文 UTF-8 几何，
平常帧不保留离屏文字控件。`native_input_performance_test --long-visible` 单独测量
约 16 KB 中文/Latin/emoji 段落的变化预览；数值仍包含字体、渲染和 glFinish。


2026-10-23（M6-51）：消息行高和固定间距能证明溢出时，省去 ScrollView
完整宽度探测中的变化长文排版，最终 gutter 宽度的内容高度仍精确计算。
证明不足或接近边界时完整测量，实际正文/选区/滚动条几何保持一致。
方法、回归、受干扰样本和独立配对结果见
[溢出探测记录](../../docs/benchmarks/native-overflow-probe-20261023.md)。


2026-10-23（M6-52）：同一回复内的稳定过程段和活动正文分别保留几何，预览追加
仅重测变化部分；完全离屏 text 过程段跳过 Markdown 构建。精确源/ID、展开、
字体/缩放和实际宽度变化仍使相关缓存失效，保留原滚动和 Markdown 语义。
独立长过程基准：

```bash
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --long-history --settled-parts)
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --long-history --settled-parts --long-visible)
```

短/长活动正文、回归与平台限制见[稳定分段记录](../../docs/benchmarks/native-settled-segments-20261023.md)。
