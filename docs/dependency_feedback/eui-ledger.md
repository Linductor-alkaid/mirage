# EUI-NEO 集成反馈

EUI 为维护者授权的 UI 专用依赖（DEC-033），不属于 Mira / Mirador 能力台账。
以下是本地记录，尚未向上游发送消息或创建 issue。

## EUI-20261003-001：GLFW IME 构建门禁使用 CRLF 摘要

- 状态：Open；负责人：Mirage 维护者；影响：Linux native configure。
- 版本：dev / 4691fc0a5c1fde6f3e22f1ac454ed87c7a17f722。
- 复现：Linux LF 检出，直接 add_subdirectory EUI bundled GLFW；
  patch_glfw_x11_ime.cmake 拒绝源码摘要 8ce625aa…b0dae，期望 b1b9e54e…ed12。
- 验证：已修复源文件 LF SHA256 为
  8ce625aa965d6da401ce3f4992fdb53ac4ecd34bc00b22bbaeffab41f72b0dae；
  仅把换行转为 CRLF，SHA256 即为脚本期望
  b1b9e54e20a687ec45e869d0d0e942220d68f087bf0727cb2091c7e9bf56ed12。
  KeyPress/KeyRelease 均已包含 filtered 检查，不缺 IME 行为修复。
- 期望：脚本对 LF/CRLF 采用规范化摘要，原始与输出均验证规范化文本；同时测试
  bundled 已修复源码和 fetched GLFW 3.4 的未修复源码。
- 临时集成：cmake/MirageEuiGlfw.cmake 使用 EUI 文档化的预提供 GLFW target；
  直接编译已修复 bundled 源码，规范化摘要严格锁定。不修改依赖，不新增调度。
- 移除条件：上游修复摘要规则，并经 Linux LF configure 验证后删除预提供适配。
- 延期影响：当前原生页面可继续验证；升级 EUI 必须复核摘要，未知版本 fail closed。

## EUI-20261003-002：Release 禁用异常但平台实现使用 catch

- 状态：Open；负责人：Mirage 维护者；影响：GCC Linux Release 构建。
- 版本：dev / 4691fc0；复现：native-release 构建，core/platform/platform.cpp:286
  报 exception handling disabled（文件对话框临时目录错误捕获）。
- 核查：eui_apply_compile_options 在非 Debug 使用 -fno-exceptions，平台源码却无条件
  try/catch；不是 Mirage 调用错误。应用尚未使用文件对话框也会因库构建失败受影响。
- 临时集成：仅在应用 CMake 追加 eui_neo / mirage-native 的 -fexceptions，保留已知错误
  捕获语义，无上游源码修改。风险：库体积可能增加，尚无性能保证。
- 期望与移除：上游统一异常策略并覆盖 Linux Release 构建，升级验证后移除覆写。
- 延期影响：当前可通过 target 属性继续构建；新平台仍需独立验证。未向上游发消息。

## EUI-20261004-003：PNG 中 SVG 元数据被误判成 SVG 文件

- 状态：Open；负责人：Mirage 维护者；影响：侧栏 image DSL 加载 Mira PNG。
- 版本：dev / 4691fc0a5c1fde6f3e22f1ac454ed87c7a17f722。
- 复现资源：apps/native/assets/mira.png，SHA256 e615d7e768c532e9966e97fbbaeefc4b7212ef386521bef6be680ed3d250bbed。
  标准 PNG 签名，1254×1254 RGBA；caBX/C2PA 元数据在 byte 305 包含 `<svg`。
- 核查：公开 image DSL source/contain 路径；core/render/image_source.cpp 的
  looksLikeSvgFile 在前 511 bytes 任意查找 `<svg`，decodeStaticImageFromPath 因此走
  SVG 解码。isSourceReady 返回 true，但 loadStaticImageFromPath 返回空；相同文件
  的 GLFW 原生窗口图标路径正常，排除了资源缺失与 PNG 像素损坏。
- 期望：文件格式识别优先验证 PNG 等已知二进制签名；只有实际 SVG 文档走 SVG 解码，
  覆盖 PNG 附带 SVG 缩略图元数据的回归。
- 临时集成：仅 UI 资产边界生成 mira-ui.png，去除 ancillary metadata 后无损重编码；
  保持全部 RGBA 像素、透明度和尺寸。保留原图及来源元数据，窗口图标继续使用原图，
  不改 pinned 依赖、不新增线程或任务路径。代价为多携带一份 PNG。
- 移除条件：上游修复格式识别，升级 pin 后在明暗主题中验证原图 image DSL 加载成功，
  删除派生副本并恢复侧栏引用。延期影响：当前页面可继续验收，新增带元数据图像需复核。
- 验证与来源：[图标验收](../compatibility/mira-icon-20261004.md)；派生摘要见
  ../../apps/native/assets/provenance.json。本记录未向上游发送消息。

## EUI-20261004-004：Markdown 在连续汉字间添加固定间距

- 状态：Open；负责人：Mirage 维护者；版本：dev / 4691fc0。
- 复现：公开 MarkdownBuilder 渲染 `Agent harness 是提供推理环境和交互功能的基础框架。`。
  整条复制取得原文无汉字间空格，真实窗口每个汉字之间却有4逻辑px空隙。
- 核查：components/markdown.h 的公开组件将非ASCII拆成码点，inline layout 对所有
  segment 插入固定4px gap；MarkdownStyle 无 gap 参数，fontFamily/fontSize 不解决。
- 期望：按原始文本空白和Unicode断行机会布局连续文字；粗体边界不凭空增加间距；
  覆盖中文/英文混排、显式空格、标点、链接、粗体、代码和最小宽度。
- 临时边界：apps/native/markdown_adapter.hpp 只经公开DSL Element调整同一行中
  连续CJK文本段的位置，且必须在原始Markdown中确实相邻；保留公共MarkdownBuilder
  的解析/样式/换行与高度预算。不读取私有解析类型、不复制解析器、不改pinned代码。
- 差异与风险：上游保守换行/高度预算仍保留；格式标记边界及显式空格不压缩。
  Adapter依赖组件生成的segment ID模式，升级时必须重新实机验证。
- 移除条件：上游正确保留文本空白并经上述混排回归和正常/最小窗口验证后删除Adapter。
  本条仅本地记录，没有向上游发送消息；验证见native-zcode-conversation-20261004.md。

## EUI-20261005-004：InputBuilder 缺少密码输入模式

- 状态：Open；负责人：Mirage 维护者；版本：dev 4691fc0a。
- 公开 components/input.h 的 value/build 与 InputModel 无 password/echo/obscure 接口。
  API Key 若直接使用普通 input 会明文展示，无法满足默认遮蔽和显式显示。
- 单一临时 Adapter：apps/native/secret_input.hpp，隐藏时 EUI 状态仅持有 ASCII 遮罩，
  根据公开 InputModel 的上一选择区/当前光标重建插入删除；Ctrl-Z/Y 不处理，
  遮蔽时复制只能复制遮罩。显示/隐藏和新输入使用现有字体/样式，不改第三方代码。
- 期望最小能力：文档化的 password 模式，隐藏渲染、光标/选择/粘贴/删除和撤销一致。
- 移除条件：上游公开接口交付并通过同一编辑/显示和遮蔽测试；当前原生适配由
  单元测试与实际键鼠验证，延后系统密码模式不会阻断直接 API Key 交互。

## EUI-20261005-005：缺少只读正文和Markdown的文字选择接口

- 状态：Open；负责人：Mirage维护者；版本：dev 4691fc0a。
- 复现：公开TextBuilder/MarkdownBuilder输出仅有静态文字，未提供选择范围、选中文字、
  选区矩形或selection-changed回调。InputModel有字体测量、UTF-8光标和换行接口，
  但不能把完整Markdown作为可编辑输入替代正常排版。
- 影响：ZCode式会话选段引用；用户应引用实际可见选段，跨粗体/换行仍保持UTF-8边界。
- 期望最小能力：只读文字/Markdown的有界选择控制器，输出可见文本与选区矩形、来源，
  支持拖选、取消、复制与主题色；不要求产品引用语义下沉。
- 单一Adapter：apps/native/selection_adapter.hpp利用公开DSL排版后frame及InputModel/
  TextPrimitive字体测量；纯几何选择由text_selection.hpp承载。每条最多16KiB/8192段，
  当前视图最多80消息，切换/移除后释放缓存。无新线程、队列或调度器，无上游修改。
- 行为差异：选区限同一消息；拖选不自动滚动或跨消息；引用来自实际渲染文本而非整条源
  Markdown；选区矩形沿各字形段，保留上游排版间隔。Escape/滚动/切换/编辑时取消。
- 验收：native_conversation_view_test真实EUI排版/渲染与指针事件，跨中文/粗体选择、
  悬停复制、浮动引用与最小明暗截图；native_chat_model_test覆盖UTF-8选区和引用预算。
- 移除：上游公开控制器交付并通过同一验证后删除Adapter。仅本地台账，未向EUI发送反馈。
