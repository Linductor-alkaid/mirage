# 会话桌面动作的权限、输入与恢复边界

> 状态：Active
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 依据：[DEC-054](../decisions/DEC-054-scoped-complete-desktop-actions.md)，[DEC-056](../decisions/DEC-056-post-action-verification.md)，[M7](../plans/m7-visual-tools-and-input.md)
> 更新日期：2026-10-20（DEC-056 分层门控 v2 首增量已接入源码，平台/部署范围另行取证）
> 适用说明：下述逐动作强制验证/全局 unverified 门禁及终态重采集描述保留迁移前
> 实现与历史证据；当前 v2 子集以文末分层修订及 DEC-056 首增量契约为准。

## DEC-077 原生 Wayland 任务授权

交互任务首次 capture 的 interaction=true 经 screen.capture 与 input.inject 双门禁，
再请求系统 RemoteDesktop 键盘、鼠标和单显示器授权。系统须返回两种设备权限及合法
逻辑尺寸；仅共享显示器或关闭控制开关返回 portal_input_denied，关闭会话且不重弹。
同一任务所有输入与观察共用 session owner，关闭 consumer 后关闭 Portal 会话。

四个 desktop.session 工具只接受本任务已供模型的图像、epoch 和图内矩形，六十秒
意图上限；派发前重取图逐像素核验目标，沿既有 ActionCoordinator lease、取消和
释放失败 fence。无全局焦点保证，用户外部输入仍可能竞争。局部新图或同源 OCR
证据不能证明网页业务完成，释放失败与不确定副作用不自动重发。

文本最多 4096 字节 printable ASCII，非 ASCII 在权限等待和输入派发前明确拒绝，
不猜测输入法快捷键、不改用户布局、不覆盖剪贴板。文本历史只保存字节数和作用域。
系统未交帧、设备撤销、超时/接管均显式失败，不能把旧帧作为新观察。
范围和实机条件见 [DEC-077](../decisions/DEC-077-wayland-task-scoped-input.md) 与
[验收](../compatibility/wayland-task-input-20261011.md)。

## 观察与目标

会话只注册真实 Provider 支持的工具。read_only 无副作用工具；default 每次修改仍经过
共享权限控制器。窗口激活使用 window.activate，语义点击、完整组合键和文本使用
input.inject，resource 为窗口 ID。门禁等待后重新检查观察及取消状态。

window.list、window.front、semantic_snapshot 返回 schema=2、observation_scope、
environment_epoch、captured_at_ms。作用域只在签发它的轮次/会话内有效，后续观察替换
前一份，最长 5 秒；有效期用单调时钟判定，captured_at_ms 仅为展示时间。窗口激活需
来自 list 的 ID；输入需来自 front/semantic 的前台窗口，派发前几何和前台不符即拒绝。
semantic 的 backend_scope 在 AT-SPI2/UIA 已有锁内验证，阻止同名 @e 引用换代后执行。
语义树、窗口标题和模型工具结果均保持非可信数据，不提升为系统指令。

每次桌面输入/窗口激活推进共享 epoch；接管和恢复也推进 epoch，旧观察失效。该计数
覆盖 Mirage 管理的动作，不覆盖所有外部 OS 事件。用户在核验后改变焦点或同时按键仍
可能与系统输入竞争，无法宣称跨进程原子桌面事务。动作后的局部条件核验见下文。

## 输入所有权与恢复

ActionCoordinator 无队列，活动 lease 期间第二个会话或 workflow 输入明确拒绝。
X11 完整 key/type 预检所需键与环境修饰键；用户已持有输入时 input_busy，保留原物理
状态。完整 click 预检按钮。Backend 只释放本动作尝试按下的键/按钮，释放不检查已经
取消的 token。文本先解析整批 UTF-8/键盘映射，64 KiB 上限，换行映射 Return；中文
缺映射时整批拒绝，不偷偷修改布局、使用剪贴板或丢弃字符。

普通动作预算 1 秒，受到轮次剩余时间收紧；平台调用超时为协作式，Xlib round-trip
不能被强制中断。取消在完整 stroke 之间检查，已派发内容可能无法撤销。回执必须区分
拒绝与 uncertain，并保留 side_effect_may_have_occurred / input_released；模型回答
和 input_released=true 都不能证明目标业务完成。未知副作用不自动重发。
schema/type 校验由 Mira 在进入 handler 前拒绝，沿用其失败 ToolResultPart；成功进入
动作 handler 后的派发/拒绝/不确定回执使用 schema=2。上游 handler 异常保护仍可返回
安全的通用失败结果，不将错误伪装成成功回执。

Supervisor 的 request_desktop_takeover() 取消当前 lease、拒绝新动作；resume_desktop()
仅在 Runtime 正在运行、无活动 lease 且无释放失败时成功，之后必须重新观察。输入
Provider 异常不能证明清理成功，会保守 fence。input_released=false 不允许普通恢复
抹掉：当前实现需维护者检查实际按下状态、完成外部清理，再重启 Runtime。未提供自动
强行恢复接口；shutdown 报告 desktop input release did not complete。

Runtime shutdown 先停止准入、取消/排空既有 Executor 任务，最后由外部 owner 关闭
Executor。这里的 lease 只管理资源所有权，没有私有线程、等待队列或调度设施。

## 工具示例与结果

先调用 desktop.window.front 或当前窗口的 desktop.accessibility.semantic_snapshot，
复制该次结果中的 scope/epoch。以下字段是占位示例，不能自行生成观察身份：

```json
{"window_id":"w2","observation_scope":"<latest-scope>","environment_epoch":7,"key":"ctrl+shift+t","verify_kind":"window_title_equals","expected_title":"New tab"}
```

同一组身份用于 type_text 时增加 text；语义 click 使用 ref，例如 @e1。一次动作结束
后必须重新观察再发下一次动作。底层派发回执示例（会话会附加下面的核验信息）：

```json
{"schema":2,"status":"dispatched","verified":false,"window_id":"w2","side_effect_may_have_occurred":true,"input_released":true}
```

取消或失败可能产生副作用的回执示例：

```json
{"schema":2,"status":"uncertain","verified":false,"side_effect_may_have_occurred":true,"input_released":false,"error_code":"input_release_failed","message":"desktop provider failed"}
```

普通会话过程/持久化 Part.input 隐去 text，只保留 window_id、text_bytes、scope/epoch；
规范模型工具往返仍含用户输入文本，不把上述脱敏解释成模型未收到载荷。失败消息不泄漏
Provider 异常细节。

## 动作后的局部条件核验

四个会话修改工具在派发前固定 verify_kind 与期望值。window_focused 检查目标前台，
window_title_equals 精确比较标题，semantic_node_exists 在新语义树中要求角色/名称
唯一命中。后两者分别需 expected_title、expected_role/expected_name，每项最多
1024 UTF-8 字节。激活默认检查目标前台；输入未声明条件仍可派发，但不能成为成功任务。
条件匹配只证明声明的局部状态；模型不能通过事后修改条件把失败改成成功。

handler 结束和输入租约结算后强制采集，核对前台、采集前后标题/几何、任务/会话/epoch
与取消；标题 64 KiB、语义 512 节点、32 动作记录、单次采集协作预算 1 秒。
语义预算超限必须明确失败。Mira evaluate_observation 检查观察时间/质量，时间戳仅展示。
回执的 verification 含固定 condition_digest、新 evidence_id、实际 observed_digest、
captured_at_ms 和状态，不复制原始期望文本。摘要不是脱密或任意业务成功的证明。

异步回显可用只读 desktop.action.verify(action_id) 再采集；只接受本轮最近动作，
旧/异会话 ID、改变条件或取消明确拒绝，环境换代不能得到已核验。未核验动作阻止下一修改；可能存在
副作用的失败保持本轮 fence，即使后条件偶然匹配也不能自动恢复或重发输入。
独立只读核验不能清除释放失败 fence。

宿主终态再采集最近动作，所有动作都 verified 且无规范修改/核验工具失败才保留回答，
并声明局部核验范围；其他情况 failed/Cancelled，不透传模型“成功”文字。判定不依赖
可截断的 UI 过程。工具卡从回执解析目标与状态，旧派发结果不能显示“已核验”。
真实 X11 的激活、回显与提交落盘证据见
[M7-08 验收](../compatibility/post-action-verification-20261009.md)；真实语义表单、视觉
条件与 Windows/Wayland 仍须各自补验，负责人 Mirage 维护者。

## 图片采集与模型输入

[DEC-055](../decisions/DEC-055-desktop-image-supply.md) 接通标准 PNG 与逐请求输入。
显式 `supports_images=true` 的模型才提供显示器枚举和截图；默认 false。
枚举只给来源 ID/边界，截图须在像素采集前判定 `screen.capture`，resource 为
`display:<id>`、`window:<id>` 或 `roi:<display-id>`。允许识图不等于授权截图。

```json
{"source":"roi","source_id":"<listed-display-id>","x":-110,"y":-35,"width":8,"height":4}
```

坐标为全局桌面像素，ROI 位于声明显示器内；display/window 不接受 ROI 字段。
采集前后核验几何、任务/会话/epoch、取消和帧尺寸；窗口截图包含遮挡。
每轮最多 4 次授权尝试，原始与编码图分别最多 8 MiB，单次 5 秒，保留最新一张，
无静默缩放。4K 整图超预算拒绝，可由模型明确选择较小 ROI。
敏感 PNG 仅在有界 MemoryArtifactStore 内存存放，替换/轮次结束时 erase；最多
5 秒，动作/接管/恢复推进 epoch 后退役。到下一请求若已退役只发送重拍提示。

工具回执及普通过程只含 ID/digest/MIME/大小、来源、尺寸/原点、1:1 变换与 epoch/时间，
不含像素/base64、窗口标题或密钥。真实图片和非可信来源文本以 Sensitive User 输入
送入当前模型请求，不升级为系统指令，不累积到工具历史。Source 核验 MIME、敏感级别、
长度和 digest；Guard 拒绝外来/迟到/缺失图片。supplier 异常或预算 drop/degrade
令 completion 失败，不能沿用纯文本继续来宣称识图成功。Provider 异常细节不外泄。
PNG、三方言 fixture 和合成截图在线证据见
[图片供给验收](../compatibility/desktop-image-supply-20261009.md)。

## 平台和证据

本轮真实输入证据为私有 Xvfb/X11；AT-SPI2 的同名旧 scope 有独立 fixture。UIA 的
scope 检查仅有源码变更，Windows 完整 key/type 不声明支持，工具不会注册；Windows/
Wayland 的构建与运行由维护者在目标环境执行 M7-10。视觉 registry 的同锁 scope/ref
边界已具备，视觉 click 工具与截图坐标变换留在 M7-07。

验收与准确复现命令见[完整输入验证](../compatibility/scoped-complete-input-20261009.md)。

## 显式 OCR 资源与文本

M7-06/DEC-057 使用可选 --ocr-manifest；默认不获取或加载模型，不暴露 OCR 工具。
配置模型后，desktop.vision.ocr 和 IPC desktop.observe.visual 每次经 screen.capture
权限读取配置显示器；权限、取消、期限或来源变化失败会拒绝结果。复用公开 Refresh
策略，感知指纹相同不视为文字相同。框/文本/输出预算见
[DEC-057](../decisions/DEC-057-real-ocr-composition.md)。

OCR 文本属于非可信外部内容，仅回填规范模型 ToolResult；工具过程和持久化历史只
保留数量、来源、scope/revision 等元数据。模型主动复述的文本仍属于正常助手输出，
这不是全文泄露防护保证。工具失败被规范结果终态门禁保留，模型文字不能覆盖失败。
verified=false 表示未验证业务目标，独立 OCR 的 @v 不作为点击目标；只有同源编号
图携带的 scope/编号可经视觉点击的完整前置门禁消费。正式 pin 的字符/位置 4/4 见
[正确性记录](../compatibility/real-ocr-correctness-20261009.md)。

## 同源编号与视觉点击

M7-07 按 [DEC-058](../decisions/DEC-058-scoped-visual-marks.md) 绑定编号与本轮 scope、
epoch、同源原图和有限有效期；接受窗口 ID 和编号，不接受模型坐标。点击需要再次
授权 screen.capture 和 input.inject，并在共享 lease 内重新采集；画面、显示器几何、
前台或窗口移动即拒绝。scope/ref 在 registry 锁内核验，部分裁剪/画外目标不可选。
完整 click 的释放失败 fence，不确定结果不重发；后观察和终态门禁覆盖视觉输入。
严格像素相同只证明前置画面未变，不证明区域可交互或业务完成；OS 外部变化仍可能
发生在最终查询与注入之间。图片仍是 Sensitive 外部数据，编号不会提升其信任等级。

输入前显式再次 mark 必须按新图片重新选号，旧 artifact/scope 即时退休；刷新
使用原采集预算，不能延长旧引用、沿用旧编号绑定或清除失败动作。验收中选择
关闭模型思考只改变请求生成配置，不改变权限、引用年龄或动作后验证规则。

## 固定 OCR 文本后条件

[DEC-059](../decisions/DEC-059-visual-text-postconditions.md)只接受动作前固定的非空
expected_text。派发后、只读 verify 和终态均重新授权并采集，不接受旧图、旧 scope、
未执行 backend、跨窗或低置信度/重复文本作为成功证据。源和模型变化即拒绝；取消与
截止时间进入既有 Pipeline。缺 OCR/Screen 能力在派发前拒绝；派发后的权限拒绝
保持未验证并禁止自动重发。条件/识别全文不进入普通工具卡和持久化过程投影。

## 无文字闭合区域编号

[DEC-060](../decisions/DEC-060-geometric-visual-marks.md)通过审核过的 OCR manifest
显式开启 geometry_marks，仅作用于 mark。几何候选 tight 面积不得超过整帧 1/4，
无文字 OCR 框不进入融合；线段/提案预算在候选筛选前执行，超限失败不截断。
闭合轮廓没有交互语义；既有权限、scope、epoch、像素/窗口核对、输入释放与固定
后条件门禁继续适用。窗口框、轮廓变化和几何 confidence 都不能充当业务成功证据。
清单变更参与 revision，并需重启 owner；不会从模型输入自动修改配置或注册模板。


本地模板按 [DEC-061](../decisions/DEC-061-geometric-template-resources.md)进入显式
资源清单，启动时核验 ID、紧密 BGRA8 布局、精确大小/SHA-256 与容量，变更需重启
owner。库由配置像素拥有，注册后不可通过帧 enrollment 扩充；没有自动注册用户
画面的入口。只有全库预算下本次唯一强命中且融合未改变匹配框时发布 template_id；
重复外观、不同位置/身份与容器扩大框不发布身份。OCR 混合证据仍保留真实文本来源。
ID 是外观标签，不能作为指令、权限、业务成功证据或点击引用；输入继续使用当次
scope/ref/编号与固定目标窗口，遵守逐像素新截图、租约、取消和后条件门禁。


在线视觉选号的受控验收沿既有图/引用/权限与后条件门禁，见
[证据](../compatibility/online-visual-canvas-20261010.md)。真实模型只接收私有 Xvfb
公共图案和无身份提示编号；测试 oracle 不进入模型输入。负例实际点击后未出现
固定业务文本，宿主仍拒绝成功回答。该验收不扩大用户桌面的上传授权，也不证明
原生 Wayland/Windows 或跨主题/尺度安全性。


[DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)的可选 ROI 预采集让
显式窗口/crop 范围在像素采集和分析前生效；不支持/越界/超预算不回退整屏。
供给 frame 的 capture_roi、image_geometry 与 display_geometry 必须一致，点击前
在同一 ROI 逐像素重验，并继续核对物理显示器、窗口和焦点。范围外像素变化不使
局部图片失效，范围内变化拒绝输入；全局权限/epoch/租约和后条件并未放宽。局部
OCR 回调只由 owner 装配；模型没有重定向后条件采集范围的入口。

## 有界滚动与完整拖拽

DEC-070 将 canonical 失败 JSON 控制在 pinned Mira 512 字节上限，完整产品验证证据
从本轮宿主持有的记录投影。可恢复性只认同 task/session/epoch 下自身 action_id、
未派发且明确无副作用的记录；摘要自称、伪造/截断/其他轮次、不确定或未释放输入均不能
解除封锁。早先拒绝卡片保留；最终成功必须有后续动作新证据，不能以无条件重试消除失败。
条件正文仍隐藏，历史新增字段名与值类型，不保存 expected_* 的正文。相关负例见
[恢复验收](../compatibility/desktop-rejection-recovery-20261011.md)。

[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)的两个新工具沿用当前
owner、visual_scope、environment_epoch、窗口 ID 和编号。拖拽同时解析两个引用，
起终点均须完全位于当前前台窗口/供给范围；两者不接受模型坐标或跨快照编号。
滚动两轴步数总量 1..16，拖拽非零距离最多 2048 像素、16 段左键路径；完整操作
协作预算最多 1000 ms，亦受五秒引用剩余寿命限制。权限分别检查 input.inject 和
screen.capture，同范围逐像素重验、四次采集预算及 lease 不变。

X11 在动作中检查窗口/前台/指针归属和 core 按钮/修饰键，拒绝已有 core 1–5 号
按钮与 Ctrl/Alt/Shift/Super 按住状态。扩展物理按钮不在 core 查询能力内。
只释放本动作尝试按下的按钮，取消、takeover、超时与焦点变化不能跳过释放；
释放失败 fence 后续输入，异常或不确定副作用不自动重发。任意外部变化仍可能
发生在查询与注入之间，不宣称抢占或硬实时中止。

规范请求示例（引用值必须来自当轮 mark）：

```json
{"visual_scope":"<current-scope>","window_id":"<focused-window>","environment_epoch":1,"mark":1,"horizontal_steps":0,"vertical_steps":3,"verify_kind":"window_title_equals","expected_title":"Scrolled"}
```

```json
{"visual_scope":"<current-scope>","window_id":"<focused-window>","environment_epoch":1,"mark":1,"destination_mark":2,"verify_kind":"window_title_equals","expected_title":"Dropped"}
```

scroll/drag 1.0.0 成功派发返回 schema=2/status=dispatched、input_released 和
side_effect_may_have_occurred，随后附原有固定 verification；滚动保留两轴步数，
拖拽保留 destination_mark。过程投影仍由现有脱敏路径处理，expected_text 不进入
普通工具卡。readonly verify 与宿主终态另取新观察；声明条件未出现时必须失败。
私有 Xvfb 证据见[验收记录](../compatibility/bounded-scroll-and-drag-20261010.md)，
不扩展为在线模型、用户宿主或其他平台保证。

GTK 原生分隔条/滚动补证与列表 XDND 限制见
[应用效果验收](../compatibility/gtk-pointer-effects-20261010.md)。X11 motion 的
8 ms 原生交付间隔仍计入单次动作预算，每步继续核对/取消，cleanup release
始终零延迟。原生拖放图层触发归属失败时释放并报告未验证，不按 PID/类型/
标题白名单放行。应用到滚动边界或分隔条禁用时可派发成功，但未达到固定
后条件仍不得结算成功；取消也不回滚已经发生的应用副作用。

M7-09 的 [DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md)允许有界
同连接拖放辅助图层解析。归属由 XRes 服务端资源 base/mask 确认；已可见窗口、
其他连接、第二图层与下方外来遮挡均拒绝；重父窗口内第一个命中也不得越过。
最多 256 个连接、128 根子窗口、额外 128 后代/32 层，超限或查询失败收敛释放。
不按可伪造 PID/标题属性认定归属；同 X11 连接为应用信任边界，无法据此隔离
同连接内恶意代码。服务器/构建无扩展保持原 target_obscured，不静默重试。
取消仍可能让应用接受清理 release，不能解释为业务回滚；实际列表顺序与新
观察后条件独立取证，见[本轮证据](../compatibility/x11-native-dnd-20261010.md)。

后续[用户 Xorg 验收](../compatibility/user-host-gtk-pointer-20261011.md)只捕获
自有 GTK 窗口 ROI；等待人工原生松键后重新确认窗口 ID、前台与物理几何。
宿主不注入取焦/外来遮挡，也不自动重发失败动作。像素变化与较晚取消前的
遮挡失败均留存；首段取消单独要求 GTK drag-begin、错误回执、按钮和 owner
释放、终态未验证，不声称回滚应用副作用。失败像素诊断只输出变化数/包围框。

测试用 X11 指针诊断最多四次/128 根子窗口/256 连接，超量明确输出 truncated。
只写命中类别与计数，连接归属来自服务端命名空间；根 WM frame 的不同连接
不能直接解释为外来遮挡。分类不参与产品权限/归属判定，不导出无关窗口身份
或画面；同连接图标在最上层仍可能遮着外来窗口，原产品拒绝保持。一次带
诊断的较晚取消与已知像素变化负例见
[后续证据](../compatibility/x11-pointer-diagnostics-20261011.md)，不保证时序原子性。


## 本地语义 Workflow（M7-11 / DEC-067）

M7-11 / DEC-067 的 workflow 本地原子遵循同一输入门禁。IR 中的知识是非可信实参，
不能授权输入；只允许精确应用/页面与唯一启用目标，不接受持久执行引用或坐标。新后条件
必须原先不存在，并在同应用/页面的新树中唯一出现；已经完成的页面拒绝再次激活。
权限等待后重验标题/前台/几何，作用域在 Backend 锁内判定。接管在 Provider 派发中
撤回权限时，即使 Provider 返回成功，也返回可能产生效果的取消结果，不能升级为成功。
成功 receipt 是宿主固定生成的 canonical JSON 字符串；Contains 检查不是对任意模型
文本的信任。示例 max_attempts=1，无 Retry/循环；对外部手写 IR 或恢复 patch 的统一
重放约束尚未交付，不宣称自动失败恢复。参见[契约](../decisions/DEC-067-local-semantic-workflow-grounding.md)。

## 产品启用（M7-12 / DEC-066）

产品托盘在 Linux 显式探测 X11、AT-SPI2、应用和通知 Provider；连接失败保持能力缺失，
不以 UI 开关代替平台能力。vision.set 只保存本地资源路径，不授予 screen.capture /
input.inject，也不改变运行中的输入 owner。启动仍校验清单和权重；无匹配 OCR 构建
或坏资源拒绝启动。`mirage start --ocr-manifest ""` 可暂时关闭 OCR 后修正持久化路径；
指定覆盖时拒绝复用旧托盘，不默默忽略参数。默认与只读会话的工具装配和共享权限沿用
DEC-054/055/058，不放宽引用年龄、像素、前台及释放守卫。网络传输继续拒绝私有/回环
模型地址；产品验收没有为本地 HTTP 夹具放宽该策略。

## DEC-071 应用交付边界

会话 `desktop.application.launch/open_uri` 使用独立的 application.list scope、会话 ID、
五秒时效和 epoch；授权后重验并占用同一 ActionCoordinator，能力为 application.launch，
资源为已安装 application_id。只接受固定 Provider 应用入口，无命令、环境变量或 URI
handler 任意选择。URI 为至多 2048 字节、有 host、无 userinfo/控制符/空白的 HTTP(S)，
Linux GIO 做严格解析并传递单一 argv，不经过 shell。运行中应用仍可接收 URI；所有
交付子进程与普通启动共用 256 项资源上限，并沿平台现有机会式回收。派发后取消、
异常或不确定结果不自动重试。

application_running 只证明新 Provider 查询的进程状态，不证明窗口归属、网页加载、
导航或业务目标；后续需要新 window.list 与页面观察，不能激活无关应用替代浏览器。
产品工具卡片仅记录 uri_digest/uri_bytes，不复制 URL/query 正文；模型的 canonical
输入仍包含为执行而提供的 URL。依旧不能保证模型自由回复不会复述用户输入。

Secret Service 删除缺失条目是幂等成功；锁定、重复及服务异常保持失败。Linux 已知
GNOME session 临时集合不能承载新持久密钥，默认指向它时选择已枚举 login 集合，
其锁定/不存在则明确失败。正常其他默认集合沿现有行为；不泛化未知服务的持久性。
保存先提交再清理，清理警告不代表新配置回滚。旧临时密钥由用户在新版重新保存一次，
不从工具输出读取、迁移或暴露用户密钥。

## DEC-072 通用截图区域输入

`desktop.vision.click_region` 以当前 screen.capture 的唯一 image_scope、epoch、
图片像素区域及 window/display 上下文定位，只有图片已经进入同一任务模型请求才可操作。
不接受裸坐标、无图工件摘要、历史 scope 或检测编号冒充图片身份；无障碍和编号路径保持。
窗口上下文要求区域在原前台窗口内；显示上下文匹配 display_id，授权 input.inject 的
资源为 display:<id>，可操作桌面/面板/菜单。设置 capture_scope 时采集和动作均受其限制。

最多保留 32 MiB 源像素，区域重验 8 MiB，四次采集、一秒完整左键；来源几何、显示器及
前台有/无状态都要保持。Mirador 变换恢复预览坐标，区域每行源像素与执行前新捕获一致
后才派发。无关区域变化不拒绝；目标区域改变、换图、取消/接管、权限拒绝则不派发。
供图仍五秒；已供图意图最长六十秒，不续期引用，也不能拿旧图验证结果。

foreground_changed 固定动作前前台状态，foreground_title_equals 检查新前台准确标题，
display_text_exists 绑定显示器与唯一高置信度新 OCR。前两者允许有意打开/切换窗口，
旧 window_focused 等条件仍要求原窗口。条件必须动作前固定；界面推进不能冒充页面或
业务完成。已派发未验证只允许只读重验，不确定结果或清理失败继续封锁，不能换路重发。

## Wayland 持久授权（DEC-078）

用户主动选择记住后，仅布尔偏好进入普通配置。恢复令牌位于系统钥匙环的专用
产品项；使用前删除旧单次令牌，Start 验证键鼠权限、流尺寸和替换令牌后保存新值。
钥匙环失败明确报错，不降级到明文配置、环境变量、日志或任务历史。
授权状态不等同于权限决定：动作仍验证任务、scope、epoch、供图和输入意图；只读
会话永不开放输入。跨任务恢复新建流，不继承旧截图、引用或按下状态。
忘记使客户端授权代数失效，运行中操作协作停止、后续操作拒绝；等待模型的旧流
在下次推进/任务结束关闭。删除客户端令牌不宣称立即注销系统所有许可；用户可以
用系统停止共享。实机恢复和系统撤销后重授权仍须目标平台补验。

## 完整点击序列（DEC-099）

`desktop.session.click`、`desktop.vision.click`、`desktop.vision.click_region` 的
可选整数 `click_count` 接受 1–32，默认 1。例如 `click_count=3` 表示一次动作内
在同一位置连续发送三组按下/释放；2 表示双击。权限、目标重验和单写者 lease
覆盖整个序列，之后统一取证；次数不增加观察预算，也不延长动作期限。
非法次数在授权和输入前拒绝。取消、超时或失败阻止后续按下，已拥有按钮仍须释放；
有不确定副作用时禁止重发整组，释放失败继续封锁输入。
`desktop.input.click` 仍为一次语义激活。未实现完整序列的后端显式拒绝多次点击。

## 统一鼠标动作（DEC-100）

三条目标路径以相同参数提供 click / scroll / drag：编号使用
`desktop.vision.click/scroll/drag`，图片矩形使用
`desktop.vision.click_region/scroll_region/drag_region`，授权共享帧使用
`desktop.session.click/scroll/drag`。

click 和 drag 均可选择 `button=left/middle/right`（默认 left）；click 另有
`click_count=1..32`（默认 1）。scroll 的 `horizontal_steps` 和 `vertical_steps`
默认 0，正数右/下、负数左/上，两轴绝对值之和必须为 1..16。
拖动的两端必须绑定同一观察：编号用 `destination_mark`，区域/共享用
`destination_x/y/width/height` 图片矩形，按两端中心执行 16 段线性路径。
距离最多 2048 源桌面像素，单次完整动作期限最多一秒（协作检查，不承诺阻塞 OS
调用硬实时）。X11 两端需位于同一前台窗口，Portal 两端需位于同一授权 monitor。
不能通过持续按住接口拼接跨工具手势；不确定副作用禁止整组重发。

区域拖动在一次 guard capture 中重验两端及中间矩形，固定窗口/显示几何和焦点；
共享拖动在同一来源流的新帧中重验两端及中间区域，保留原授权代数与流身份。
它们与原编号动作共用动作后条件和收尾验证。失败释放仍封锁输入，不能把成功发送
解释为拖放业务完成。Windows 的完整动作 opt-in 仍未交付，不从 Win32 原始按键 API
推导模型工具已可用。

例如，已取得有效目标后，三个 click 工具均可追加
`"button":"right","click_count":1`；两个区域 drag 工具使用同样的
`"button":"middle","destination_x":150,"destination_y":150,
"destination_width":20,"destination_height":20` 字段。此处仅展示新增参数，
调用仍必须提供各路径的 scope/epoch/目标；v2 的 verify_kind 为可选条件，见下节分层策略。

## 目标文本值与复制验证（DEC-111，待实现）

[DEC-111](../decisions/DEC-111-target-text-value-verification.md) 冻结后续平台中立的
`text_value_equals`：动作前绑定同一目标、完整预期值、比较规则及允许的取证方法。
无障碍读值首选；可靠实际值不等不能被 OCR 放行，读取不可用和视觉未匹配不能冒充
派发失败。无确定性读取时在动作前选择视觉路径，不把模型读图投影成精确值成功。
既有固定条件／不确定执行围栏和历史回执不因文档更新而改变。

受控复制默认关闭；只有目标／焦点、新复制来源、多格式剪贴板及选区的有界恢复
均可证明时才可选。复制、恢复是事前声明复合操作内的真实副作用，使用现有权限与
输入 owner；只读 action.verify 不允许发送探测按键。用户／其他程序写入后不覆盖
新剪贴板；恢复失败和来源不明显式记录。受保护输入不读值、不复制，普通事件／历史
不增加文本值或原剪贴板正文。当前纯文本 ClipboardProvider 不满足多格式恢复条件。

新路径由 [M7-32～34](../plans/m7-target-text-verification.md) 实施和分平台验收；
本次决策未交付这些能力，不扩大现有输入或剪贴板权限。

## 执行、反馈与业务验收分层（2026-10-20，Accepted，v2 首增量已实现）

按 [DEC-056](../decisions/DEC-056-post-action-verification.md) 修订，普通键鼠动作
默认不要求 verify_kind/业务条件；在本地序列完成、释放及异常结算后返回实际执行
结果。输入层错误处理、权限、当前目标/焦点/遮挡/变换守卫及取消/接管屏障继续执行。
无可见变化或未请求后条件不能单独被判为输入失败，不强制逐动作 OCR 或额外模型验证。

上下文稳定的有界动作组允许组末统一观察；组内失败即停止并报告完成前缀，不能整组
自动重放。状态依赖处须取得相应证据；依赖图、组预算及回执版本在 M7-25 冻结后接线，
不授予模型任意脚本、旧引用复用或跨未知焦点继续输入的权限。

显式业务条件失败/不可用只阻止其依赖步骤；允许只读取证、重规划和通过输入守卫的
恢复操作。恢复不能删除原条件或以弱证据冒充精确值匹配。普通“业务效果未知”与
“执行副作用未知/释放失败”必须分开：后者继续封锁并遵守现有恢复规则，不能仅凭
新截图解除，也不自动换后端或重发。只读 action.verify 不发送复制或其他输入。

终态按用户目标验收，可复用仍有效且晚于相关动作的观察；普通任务可基于模型对该
观察的判断作答，标明其证据等级。宿主不再要求全历史动作 verified 或无条件末次
重采集；显式必需条件未满足、结果证据不足或输入安全围栏未清除时不得报告完成。
条件、证据摘要及脱敏规则继续有效，正文和用户剪贴板不增加持久副本。

本节与 DEC-109/110/111 一致；M7-25/26/31 迁移策略，M7-32～34 按需提供值证据。
旧会话与旧回执不热切换语义，新策略须配套版本兼容及平台/恢复负向验收后启用。

v2 首交付采用修改工具 3.0.0、只读 verify 2.0.0；三条物理 click 后续按 DEC-110
升级 4.0.0。version proposal 不匹配由 registry 拒绝。`not_requested` 仅表示未选业务条件，`execution_status=completed` 与
`input_released=true` 不产生业务已验证的事实；过程投影校验 policy_version=2、
工具完成状态和 verified=false 后才显示“已执行”。`depends_on` 最多 8 个唯一 ID，
仅接纳同 authority/当前动作 epoch 下的匹配条件。普通检查点与条件证明在供给
本次模型请求时核验 5 秒新鲜度；推理耗时不单独使已观察结果失效。条件 ID 绑定
仅供该请求的下一次修改尝试消费，派发入口不重新采集/更换图像引用，原 Provider
的目标/焦点/像素检查仍须通过；此绑定不能证明推理期间桌面未变化。未消费的
过期证明须显式只读重验，不能只刷新时间戳。该请求的接纳受 owner deadline/取消
及当前 epoch/authority 约束，新输入立即清除。结算不采集，较早条件的成功不能解除较新
动作的执行不确定性围栏。
具体字段、记录窗口与回归边界见 DEC-056；新增动作组接口及其他平台实测仍待交付。


## 默认自动接近点击（2026-10-20）

DEC-110 点击子集将 vision.click、vision.click_region、session.click 升至 4.0.0，
删除模型 glide_ms 入口，默认本地自动计时。接近仍占用同一输入 lease；到达后必须
重验目标像素/焦点/几何，再点击释放，最后返回或进入可选业务条件；到达观察不作为
点击后目标达成证据。取消、目标变化或移动失败不继续按下，已发生移动如实报副作用。

Portal 自动接近只接受当前授权流的 Cursor metadata 起点，按同流尺寸映射；没有
有效元数据时拒绝，不能用直接点击建立起点或跨来源复用旧坐标。元数据大小/ID/帧内
坐标有界校验，格式切换退役。原低层显式时长与旧注入位置兼容代码不是新模型工具的
降级路线。X11 检出物理按钮已按下时拒绝移动；平台边界、期限与落后停止继续有效。
参数、策略与受限验收范围见 DEC-110 及[验证记录](../compatibility/automatic-pointer-approach-20261020.md)。
