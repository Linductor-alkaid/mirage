# EUI-NEO 集成反馈

EUI 为维护者授权的 UI 专用依赖（DEC-033），不属于 Mira / Mirador 能力台账。
以下逐项维护历史证据与上游状态；已提交或升级复验的条目分别记录链接。

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

- 状态：Resolved；负责人：Mirage 维护者；影响：GCC Linux Release 构建。
- 版本：dev / 4691fc0；复现：native-release 构建，core/platform/platform.cpp:286
  报 exception handling disabled（文件对话框临时目录错误捕获）。
- 核查：eui_apply_compile_options 在非 Debug 使用 -fno-exceptions，平台源码却无条件
  try/catch；不是 Mirage 调用错误。应用尚未使用文件对话框也会因库构建失败受影响。
- 临时集成：仅在应用 CMake 追加 eui_neo / mirage-native 的 -fexceptions，保留已知错误
  捕获语义，无上游源码修改。风险：库体积可能增加，尚无性能保证。
- 期望与移除：上游统一异常策略并覆盖 Linux Release 构建，升级验证后移除覆写。
- 延期影响：历史通过target属性绕行；新平台仍需独立验证。早期未提交，当前上游[#86](https://github.com/sudoevolve/EUI-NEO/pull/86)已合并，dev88a9ec1升级与Release验证后移除库层覆写；验收见[M6-19](../compatibility/eui-vulkan-followup-20261005.md)。

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

## EUI-20261005-006：Markdown行内段按各自轮廓居中导致基线跳动

- 状态：Open；负责人：Mirage维护者；版本：dev 4691fc0a。
- 复现：公开MarkdownBuilder输出 `天地人口田上下大小 ABC xyz 012345，。！？`，中文
  被拆为独立seg，每段Text使用VerticalAlign::Center。轮廓较矮的“口”、标点与大字
  分别被居中；相同字体/字号下文字高低不齐，替换字体本身不能消除此行为。
- 核查：公开MarkdownStyle没有行内垂直对齐或baseline配置；TextPrimitive的Center
  是每个文字段的ink bounds居中，Top则保留由字体ascent定义的自然基线。不存在
  可直接配置MarkdownBuilder基线的公开选项，保留原字号/行高才能对照复现。
- 期望：行内段共用基线，支持普通/强调/链接/标点/代码/不同字体；提供公开baseline
  或行内对齐能力，以混排截图验证，不靠每字不同的经验偏移。
- 临时边界：markdown_adapter.hpp通过公开DSL Element，普通字体seg中的.text使用Top
  和统一行框内边距，包括带装饰的段；代码恢复独立字号并整体居中，普通文字默认字体一致。控件和图标仍居中，
  不改解析/换行/块高，不建线程或调度器，不修改上游。
- 差异/风险：代码保持平台monospace，不宣称不同字体的ascender精确匹配；依赖公开
  组件的seg/text ID模式，升级必须重新验证。原换行/高度预算沿用EUI。
- 验收：native_conversation_view_test实际原生渲染、中英数字标点/强调/链接/代码与
  输入、正常/最小明暗窗口；证据native-typography-20261005.md。
- 移除条件：上游公开能力可保持共同基线并通过同一混排/选段回归后删除本修正。延期
  影响仅限原生Adapter维护；本记录尚未向上游发送消息或issue。

## EUI-20261005-007：字号单位缺少EM选项，CJK字体被行度量缩小

- 状态：Open；负责人：Mirage维护者；版本：dev 4691fc0a。
- 复现：TextPrimitive默认NotoSansSC-Regular.otf 2.004，fontSize16；字形明显小于
  16px CSS EM。字体units_per_EM1000、ascender1160、descender-288；渲染按
  1000/1448缩放请求字号。不同字体的升降部总高度不同，换字体后可见大小变化明显。
- 核查：公开TextStyle/TextBuilder/InputBuilder提供fontSize但没有EM/line-metrics
  单位选项；修改文件路径或fontWeight不改变此语义。不是损坏字体或缺失回退。
- 期望最小能力：文档化字号单位，提供EM字号或字体度量换算接口；测量/绘制/输入/
  Markdown共享单位，跨字体与正常/最小窗口回归。
- 临时边界：UI typography.hpp将Noto SC设计EM字号乘1.448后交给公开组件，测量也
  使用同一换算；正文行高24不变。InputBuilder行高为请求字号乘1.2，composer测量
  与其一致。Markdown标题仍使用组件的请求字号加6行高。图标与代码不套中文系数，
  Markdown行内代码恢复原独立字号与整段居中。没有字体文件/上游修改或并发路径。
- 风险：系数只对当前字体度量有效，原生测试断言字体版本所需度量；代码按独立字号重新测宽，缩小背景并平移后续段；上游保守
  换行/高度预算仍保留，不宣称跨字体严格基线与完整Chromium像素匹配。
- 验收：原生混排/输入/编辑/选择、字体度量断言与正常/最小明暗截图；移除条件：上游
  公开EM字号及统一度量交付后复跑本轮验证并移除转换。仅本地台账，未向上游反馈。

## EUI-20261005-008：Linux输入法候选框缺少光标定位

- 状态：Accepted；负责人：Mirage维护者；依据：DEC-041、M6-16。
- 核对：dev 123f0c5，公开IME cursor rect在Linux为空实现，bundled GLFW固定PreeditNothing，无公开XIC位置入口。输入框已传正确行矩形，排除应用坐标遗漏。
- 复现：X11/IBus中文输入，候选窗口使用IM默认位置而非输入框；输入窗口移动和多行输入均无法提供caret定位。
- 最小修复：平台层协商PreeditPosition并保留Nothing回退；公共窗口内像素接口更新XNSpotLocation，EUI桥接行底坐标与framebuffer/window换算。
- 资源：字体集与XIC随窗口销毁；不引入线程、队列或业务调度。未知/不支持的IM保留原有输入行为，不宣称定位保证。
- 验收：私有Xvfb+真实IBus XIM确认位置样式、三次光标位置；中文候选/提交与Enter过滤保持正常；应用渲染/缩放回归。
- 授权：2026-10-05维护者允许依赖修复并要求上游PR；已提交[上游PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)，首轮pin df8ab1c（M6-19升级为ed1deb6）；私有Xvfb/IBus/libpinyin候选(220,440)→(400,280)与光标一致，提交“你好”。上游未合并，保持Accepted。
- 延期/移除条件：没有修复时保留原候选位置；同步可审查PR提交及锁文件后复验。Wayland原生后端不在本轮（Mirage当前使用X11/XWayland）。


## EUI-20261005-009：Vulkan CI生命周期探针绑定OpenGL

- 状态：Accepted；负责人：Mirage维护者；工作项M6-19，沿用DEC-041。
- 复现版本：dev123f0c5及当时pin df8ab1c；已核对render_backend.h公开windowRenderApi及其他后端探针。
- 证据：PR#88 CI两项Vulkan在dsl_app_lifecycle_probe.cpp:11因glad/glad.h不存在失败；GLAD并未被此探针使用。
- 后续问题：探针硬编码RenderApi::OpenGL，移除include仍会创建与Vulkan backend不匹配的窗口；这是依赖测试夹具问题，不是Mirage选择错误。
- 最小修复：删除不用的GLAD include，复用公开windowRenderApi选择配置后端，不增加渲染实现或并发设施。
- 影响/延期：Vulkan CI无法通过；保持Mirage既有OpenGL后端，不能将两项OpenGL通过宣称全矩阵通过。
- 验收：四种窗口/渲染组合build、unit/probe、SDK消费者；维护者明确授权修改并要求保留上游PR。[PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)新增独立提交ed1deb6；两种Vulkan各33/33及SDK消费者通过，OpenGL生命周期回归通过；远程run37338533567四项后端/SDK消费全部success，状态Accepted（上游PR尚未合并）。

## EUI-20261006-001：重新compose的offset未同步到实际滚动状态

- 状态：Accepted；负责人：Mirage维护者；工作项M6-20，依据DEC-041及既有依赖修复授权。
- 核对：pin ed1deb6的公开scrollView.offset、scrollState、onChange与组件文档；Runtime首次
  初始化读取offset，此后仅钳制旧内部位置，忽略应用新请求。不是调用私有API或坐标错误。
- 复现：长回复向上阅读后，设置offset为末尾；DSL元素显示末尾值，实际文本和滚动条仍停在
  原位置。两张真实EUI渲染截图一致；原生回归新增读取视口像素比较，避免仅测请求参数。
- 影响：流式内容增长、窗口缩小和“回到最新”无法正确移动实际画面；不修复不能完整交付M6-17。
- 最小修复：记录上一次配置offset，新请求同步实际位置并停止旧惯性；onChange回写当前位置
  与未改变配置保留用户惯性；更新viewport dirty rect。不新增业务队列、线程或定时器。
- 验收：程序跳转、onChange回写、未控滚动、增长跟随与缩短钳制单测；原生真实视口像素、
  阅读时终态保位、返回最新、Release/ASAN与上游四项构建CI。修复追加独立依赖提交并更新
  [PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)，pin/锁同步后保留Accepted直至上游合并。
- 移除条件：升级包含相同语义的上游dev后复验；没有产品层私有状态重建或滚动绕行。

EUI-20261006-001验收补充：ff1e757已普通push；PR#88当前head的
[run37348553548](https://github.com/sudoevolve/EUI-NEO/actions/runs/37348553548)整体success、四项job全成功。
ui_state单测、原生实际视口像素和Debug/Release/ASAN/UBSAN各2/2通过，反馈保持Accepted待上游合并。

## EUI-20261008-001：首帧显示和更新期尺寸失效

- 状态：Accepted；负责人：Mirage 维护者；工作项：M6-35；依据：DEC-033。
- 授权：2026-10-08 维护者要求定位、向上游提交中文 issue、修复并提 PR。
- 版本：旧 Mirage pin fd9c1a9；上游 dev c444e53 同样存在；修复集成 pin a7e625f。
- 上游：[中文 issue #93](https://github.com/sudoevolve/EUI-NEO/issues/93)、
  [PR #94](https://github.com/sudoevolve/EUI-NEO/pull/94)，尚未合并。
- 独立框架缺陷：窗口创建已显示，初始化和首次绘制尚未完成；输入/compose 回调改变
  drawable 后，App runner 继续按更新前尺寸绘制和 present，并清除回调的待重绘。
- 复现：320×240 在 update 后改为 480×360，未修复探针稳定报
  `presented old-size frame after resize during update` 和 `resize lost pending repaint`。
- 修复：主/子窗口隐藏创建，首次有效绘制后、紧接 present 前显示；帧节拍等待后重读
  尺寸，更新后复核尺寸/DPI/指针比例，零时间重排一次；再次失效则保留重绘并唤醒。
- 验证：四组合独立 Release 配置/构建/ctest 各 34/34；新增探针 ASAN+UBSAN 通过。
- 责任边界修正：框架修复单独接入后，真实 XWayland 拖拽仍捕获 149/300 黑色中心
  样本；主要整窗频闪还涉及 Mirage 未复用既有 `beginWindowResize`。应用侧改为系统
  接管后，维护者物理鼠标验证拖拽顺畅、无频闪。不能把本 PR 单独称为该症状的完整修复。
- 限制：原生窗口 map/swap 不是原子操作，无合成器的直接 XGetImage 仍可能读取短暂
  空表面；没有承诺全部平台/驱动绝对无黑帧。Windows/原生 Wayland 待维护者补验。
- 无新增线程、队列、调度器或依赖；证据与复跑方式见
  [依赖升级审计](../supply-chain/eui-frame-presentation-20261008.md)。
