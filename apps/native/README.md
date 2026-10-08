# 原生 Agent 前端

EUI-NEO dev 固定到 4691fc0（完整 pin 见 dependencies.lock.json）。当前页面参考
ZCode 的侧栏、阅读列和 composer，C++20 / GLFW / OpenGL，没有 Chromium。

在仓库根目录构建：

```bash
git submodule update --init --recursive
cmake --preset native-release
cmake --build --preset native-release --target mirage mirage-native mirage-tray
# 统一入口启动托盘内的 Runtime，再由托盘启动独立前端
./build/native-release/apps/mirage start
```

Debug 使用 native-debug。Linux 需要 OpenGL/Mesa、X11 开发包及常见 CJK 字体，
例如 fonts-noto-cjk；当前通过 X11/XWayland 运行。依赖全部使用 pinned 仓库内
bundled 源码，不在 configure 时获取浮动依赖。Windows preset 可配置，但未真机验收。

- 空会话居中输入，消息出现后输入固定到底部并随文字增高；Enter发送，Shift+Enter换行，Ctrl+Enter保留发送兼容。
- Agent回复支持Markdown；复制动作仅悬停或聚焦时显示。拖选同一消息后弹出“引用选段”，最多4段、总8KiB，和草稿编码后合计最多16KiB。
- 输入工具条依次提供附件、访问权限、上下文比例、模型、思考深度与发送/停止。上下文圆环显示最近一次成功模型请求的输入Token占比；点击查看用量、配置窗口预算、百分比与当前引用数。缺失用量或窗口预算时显示未知，不从草稿/bytes估算。
- Ctrl+N 新建本地草稿，首次发送才创建远端会话并进入侧栏；反复新建复用空白草稿。历史行垃圾桶确认后删除，运行中须先停止；侧栏切换保留各自草稿；底部齿轮打开设置，在“外观”中选择浅色/深色。
- 拖动顶部空白移动窗口，边缘/角缩放；右上角最小化、最大化/还原、关闭。
- 运行时发送按钮变为停止；停止当前轮次采用协作取消。未绑定远端的草稿清空需要确认。
- UI最多显示24会话、最近40轮（80条消息），草稿16 KiB；服务准入与推理预算显式拒绝。

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

当前接入真实IPC和通用模型/工具harness，只注册Mira自带wait，不观察屏幕或执行RPA。
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
模型选择、思考深度、发送。默认权限只开放当前注册的 wait 工具；只读不开放任何工具。
权限不授予文件系统、命令、桌面或 RPA 能力。每个会话保留自己的权限与思考档位。

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
