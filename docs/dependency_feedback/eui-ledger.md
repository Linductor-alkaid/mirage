# EUI-NEO 集成反馈

EUI 为维护者授权的 UI 专用依赖（DEC-033），不属于 Mira / Mirador 能力台账。
以下逐项维护历史证据与上游状态；已提交或升级复验的条目分别记录链接。

## EUI-20261013-001：RGBA8/BGRA8 转码只写每行首像素

- 状态：Resolved（本地 pin 已消费根因修复；回归补充 PR 仍 Open）；负责人：Mirage 维护者；关联：M8-02/03、BUG-20261013-001。
- 复现版本：旧 pinned dev `a7e625fecc1db1c1acb4850a14fb785e20600041`。2026-10-13
  维护者授权处理，核对最新 dev 后确认根因已由上游 PR #97 / f8caff1 修复；Mirage
  当前升级至 89c2d0f（完整 SHA 见锁文件），原窗口能力保留。
- API 核对：公开 `eui::ImageFrame::convertToRgba8()` 与 `ImageStream` 文档说明
  RGBA/BGRA CPU 帧经 RGBA fallback 绘制；OpenGL 原生动态纹理仅接受多平面 YUV。
- 复现：2×1 RGBA 输入 `[255,0,0,255,0,255,0,255]`，转换返回 true，输出却是
  `[0,255,0,255,0,0,0,0]`。逐列循环未递增目标指针；对于透明画布右边缘的桌宠帧，
  每行首像素也被写成透明，导致整个角色不可见。UI 文字仍可见，不能用文字 alpha
  断言代替角色区域像素验收。源码定位 `core/render/image_stream.cpp` 的公开方法。
- 排除应用职责：最小复现不涉及 WebP、时钟、窗口、颜色管理或 Mirage；九态源 SHA、
  解码尺寸/时长与 CPU RGBA alpha 均通过。RGBA 字节逐像素保留是该公开方法的语义。
- 最小期望修复：目标地址按 `x*4` 推进，保留 stride/多行与 straight alpha；对
  RGBA、BGRA、透明和不透明、多列与带 padding 的输入验证完整输出，而非只检查返回值。
- 历史单一临时 Adapter（本轮已移除）：`apps/pet/texture.cpp` 在 UI 线程直接上传当前 RGBA 到一个
  512×512 RGBA8 OpenGL 纹理，经公开 `gpuDevice()/importGpuImage()` 与
  `image.texture(image, revision)` 接入。恢复原 GL unpack/binding 状态，生命周期由
  GpuImage 的 owner 引用及 EUI GPU fence 持有；onShutdown 清除应用引用。无新增线程、
  队列、调度器或解码器；当轮没有修改 pinned 代码。
- 差异与风险：当前 Adapter 仅覆盖 OpenGL 预览；主图像纹理固定 1 MiB，驱动、字体、
  framebuffer 与 EUI 自身资源另计。Vulkan/其他平台不据此标记通过。
- 验收与移除：最小字节复现、真实角色区域/alpha/帧推进、切换与隐藏、GPU 关闭均
  留存于 [M8 资源与生命周期证据](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。
  本轮授权升级后删除 FrameTexture，恢复容量 1 的公开 ImageStream。中文
  [issue #106](https://github.com/sudoevolve/EUI-NEO/issues/106) 与目标 dev 的
  [PR #107](https://github.com/sudoevolve/EUI-NEO/pull/107) 补充 stride/透明 alpha
  回归；生产根因修复复用已合入的
  [PR #97](https://github.com/sudoevolve/EUI-NEO/pull/97)，不重复提交。
  四组合 Release 配置/完整构建和 ctest 各 35/35；三类故障注入均被测试发现。
  Mirage 升级后的实际角色与关闭结果见
  [升级审计](../supply-chain/eui-rgba-upgrade-20261013.md)。

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
  [窗口呈现验收](../compatibility/native-frame-presentation-20261008.md)。

## EUI-20261017-001：长文本输入重复加载相同字形轮廓

- 状态：Accepted（已授权修复、集成并验证，待上游评审）；负责人：Mirage 维护者；工作项：M6-42；初始版本：bcfaa2ea。
- 公开能力核对：`components/input.h` 提供 multiline/fontSize/value，
  `core/render/text.h` 提供 measureTextMetrics/measureTextSize；没有可配置的
  advance 缓存、测量预算或长段增量排版策略。输入模型已有硬段增量缓存，但
  单个硬段内的编辑会重测整段及其软换行；这不是应用另建输入器的理由。
- 复现：构建并在私有 Xvfb 运行 `native_input_performance_test --unbroken`；
  16000 字节档位混排草稿、40 轮历史、连续输入“字”。并行构建期间诊断样本
  total p50 72.18ms、p95 78.22ms，数据仅用于定位，最终无编译负载结果另记。
- 采样：GDB 对基准主线程进行 18 次中断栈采样，多次命中
  `InputModel::updateChangedParagraphs -> measureLines -> measureTextMetrics ->
  shapeWithFallback -> FT_Load_Glyph -> cff_slot_load/cf2_interpT2CharString`；
  应用的整段高度测量也命中同一 FreeType 路径。原始合成栈在
  `/tmp/mirage-input-stacks.log`，不含真实对话。
- 源码原因：`loadGlyphAdvance` 对每个字符调用 FT_Load_Glyph 以取得 advance，
  即使同一 FontFace/物理字号/字形在当前段中反复出现；Noto Sans SC OTF 的
  CFF 轮廓被反复解释。绘制 atlas 的位图缓存不能复用为该 advance 查询缓存。
- 首轮建议（现已按末尾跟进实施）：在 EUI 字体测量层增加有界的 face/字号/glyph
  advance 缓存，保持 Tab、combining mark、彩色 fallback 缩放及字体切换语义；
  如仍不足，再考虑复用软行测量结果。字体 owner 销毁/配置变化必须使缓存失效。
  不在 Mirage 复制字体实现，不以换字体、拆字符串或减少输入容量掩盖问题。
- 验收：普通/大字号、CJK/Latin/emoji/组合字符/Tab advance 与原实现逐项一致；
  重复字符大段输入前后 p50/p95 和 glyph load 次数对照；字体/缩放切换、长时间
  随机字符下内存预算、IME/选区/撤销/滚动、ASAN/UBSAN 与现行后端构建矩阵。
- 首轮影响/延期（修复前）：多段 composer 的应用层优化可独立交付；单段长文仍可能超过一帧
  预算，不能宣称全面消除掉帧。移除条件：获明确授权的独立 EUI 修复验证并
  同步 pin/锁文件后，复跑 Mirage 基准与交互回归。本次未修改 pinned 代码。
- 证据：[输入性能验证](../compatibility/native-input-performance-20261017.md)。


EUI-20261017-001 / 2026-10-17跟进：维护者授权先切最新dev ad7e9e7，再修复、验证
通用收益后反馈。b59b6e9新增固定face的512槽advance/height缓存，72组两字体/两字号/
6场景/3轮配对指纹逐位一致；普通测量约10–75倍加速，大量不同汉字基本持平，原始
统计随上游[issue #113](https://github.com/sudoevolve/EUI-NEO/issues/113) /
[PR #114](https://github.com/sudoevolve/EUI-NEO/pull/114)交付，四项远程CI全通过。
Mirage pin63117f7保留既有Windows修复，消费缓存并接入公开contentKey；最终12种
输入场景p50/p95均改善，16KB单段/40轮51.64→16.98ms（配对中位）。不宣称巨大
不同字形工作集、任意历史或硬件GPU/Windows帧率。独立四后端各36/36+SDK1/1，
最终集成额外Windows-only探针skip；Mirage Release5/5、ASAN/UBSAN各4/4。
状态保持Accepted等待上游评审，不代维护者合并PR。见
[升级审计](../supply-chain/eui-text-performance-20261017.md)与
[最终输入矩阵](../compatibility/native-input-performance-20261017.md)。
