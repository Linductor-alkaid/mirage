# 原生 Agent 前端

EUI-NEO dev 固定到 4691fc0（完整 pin 见 dependencies.lock.json）。当前页面参考
ZCode 的侧栏、阅读列和 composer，C++20 / GLFW / OpenGL，没有 Chromium。

在仓库根目录构建：

```bash
git submodule update --init --recursive
cmake --preset native-release
cmake --build --preset native-release --target mirage-native mirage-service
# 先在一个终端启动服务，再在另一个终端启动前端
./build/native-release/apps/mirage-service
./build/native-release/apps/native/mirage-native
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
保存会合并写入服务的 service.json 并应用；已有活动轮次时先停止。
Linux 已验证系统凭据与重启调用；Windows 凭据路径尚待目标环境验证。
当前 EUI 临时密码适配不支持撤销/重做，遮蔽时复制只得到掩码，见 EUI-20261005-004。

当前接入真实IPC和通用模型/工具harness，只注册Mira自带wait，不观察屏幕或执行RPA。
工具回填为受限JSON文字上下文，见MIRA-20261004-001。本机SiliconFlow真实文字/工具
已验证；MiniMax暂受pinned TLS SNI缺口影响，见MIRA-20261004-002。
关闭仅退出前端进程；托盘联动及整程序退出属于M6-04。

源码：app.cpp（视图）、chat_model.*（UI线程有界服务状态投影）、window_controls.*
（私有GLFW窗口适配）、runtime_bridge.*（Executor IPC owner）。任何后续后台任务必须使用 Mira Executor，禁止调用 EUI
async/network/audio 来绕过项目生命周期约束。

EUI 当前构建和图像解码问题与临时适配见
[反馈记录](../../docs/dependency_feedback/eui-ledger.md)，不修改 pinned 依赖。
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
`~/.local/share/applications/`），引用当前构建的可执行文件和 PNG。普通构建不会修改
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
`mirage start` 启动服务、托盘与原生窗口；开发树自动寻找 `apps/native/mirage-native`，
可用 `--no-tray` 关闭托盘启动，`--shell PATH` 指定原生二进制。原生前端支持
环境变量 `MIRAGE_NATIVE_SOCKET` 指定 IPC 路径；`mirage start --socket PATH` 通过子进程环境传递端点（经该入口启动的托盘再次打开窗口也继承它）。完整活动退出确认仍在 M6-04，不由本次退役验收。

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
