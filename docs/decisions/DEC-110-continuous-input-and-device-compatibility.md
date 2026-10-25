# DEC-110：连续键鼠动作、持键观察与设备输入兼容性

> 当前恢复规则：人工复核及永久围栏部分已由
> [DEC-113](DEC-113-autonomous-action-recovery.md) 替代；其他能力与权限约束继续有效。

> 状态：Proposed（M7-25 契约与预算冻结稿已交付待评审；平台选型分别在 M7-29/30 原型前冻结，质量门槛在 M7-31 执行前冻结）
> 日期：2026-10-19
> 负责人：Mirage 维护者
> 冻结里程碑：M7-25；平台选型分别在 M7-29/30 原型前冻结，质量门槛在 M7-31 执行前冻结
> 替代/被替代：无；拟扩展 DEC-052/054/064/077/100，与 DEC-091/109 协同
> 修订：2026-10-19 新增"M7-25 契约冻结稿"一节（能力位、会话键词表、预算数值、
> 状态机、版本化回执），代码与 golden 对账已验证；评审通过前本节仍为冻结稿。
> 修订：2026-10-20 按维护者要求明确全量键鼠动作默认采用本地自然时序，模型无需提供
> 时间；完整动作执行并释放后才返回动作结果或进入后置验证。全量迁移尚未完成，点击子集见后续修订。
> 修订：2026-10-20 默认自然输入方向继续推进；验证策略按 DEC-056 当日已接受修订：
> 普通动作无默认业务后条件硬门禁；分层门控 v2 首增量已接入源码，默认自然时序与
> 新动作组能力仍待 M7-25/26，详见 DEC-056 首增量契约和专项验证记录。
> 修订：2026-10-20 继续实施：三条物理点击工具已迁移为 4.0.0，本地默认自动平滑接近，
> 不接收模型移动时间；具体范围、策略版本及证据见文末“自动接近点击首增量”。
> 修订：2026-10-21 M7-27 继续接通固定窗口内的持键移动，advance 1.1.0；焦点转换及
> 未验平台继续开放，具体契约与门禁见文末。

## 背景与问题

维护者要求键鼠行为具有连续时序：指针从当前位置移动到目标，按键有按下时长、组合
与释放顺序，并支持按住键时观察屏幕，再决定后续输入。例如按住 Alt、多次轻按 Tab、
根据切换器画面选择窗口，最后释放 Alt；以及按住按钮或组合键取得瞬态界面图像。
输入法还需支持单独 Shift 切中英文、切换输入源、预编辑和候选确认。

维护者进一步明确，设备层输入的目标是提高应用兼容性：部分程序过滤或拒绝软件
键鼠注入，使任务无法完成。能够从设备输入路径操作这些程序是独立需求，须与动作
能力并行评估，不能延后为鼠标动画优化。

当前 Mirage 点击多为直接定位，拖动为有界分段路径；生产 key 工具执行完整 chord
后释放；本轮修订增加裸修饰键的完整 tap。DEC-054 的动作所有权解决了单次动作清理，但尚未
表达跨观察持键。研究与源码依据见
[连续输入与 HID 调研](../research/2026-10-19-continuous-input-and-hid.md)。

## 拟议决策

### 1. 统一动作语义，分别声明时序、状态与设备能力

由 Mirage 的 InputProvider/动作上下文提供平台中立的时序与输入状态；Platform
Backend 负责事件映射、设备投递与平台状态回读。Mira 继续承载规划、工具调用和任务
生命周期，Mirador 提供观察/变换/识别，不将平台句柄、驱动或 HID 报告类型暴露给上层。

能力声明至少区分：平滑移动、定时按压、独立修饰键、跨观察持有、持有期间取图、
按键重复、IME 状态/控制、软件注入、内核虚拟设备、外接 HID。对具体后端、源与
当前授权声明可用性和限制；不因能点击便推断能持键，不因 HID 枚举成功便推断目标
应用接受输入。能力缺席时在副作用前拒绝，不偷偷改成瞬移、粘贴或其他后端。

动作区分完整 tap/chord、定时 hold 和受控会话中的 down/up。修饰键可独立按下、
持有、释放，也可与其他键或鼠标动作交叠；键名明确左右修饰键/物理键位与逻辑字符
的语义，具体词表在 M7-25 冻结。直接输入最终文本、物理键语义和 IME 组合输入
分别选择，不把 Unicode 文本传输宣称为物理键盘输入。

### 2. 鼠标从当前状态平滑运动，按键按明确时序执行

#### 默认自然时序与完成边界（2026-10-20 维护者要求）

目标是所有鼠标、键盘输入都由 Mirage 本地动作层生成并执行自然时序，覆盖移动、
单击/右键/多击、拖动、滚动、按键、组合键及逐键文本输入；不能只为可选平滑点击
提供入口。模型表达目标、按钮、次数、键或文本等动作意图，无需返回移动时长、
轨迹点、按压间隔或速度曲线。生产工具不以通用 `glide_ms`/`duration_ms` 为启用
自然行为的条件；面向模型的常规动作 schema 以不提供这些时间入口为目标。明确的
“按住一段时间”属于任务语义，可单独提供有界参数；跨观察持键仍按第 3 节处理。

本地策略依据距离、已知目标宽度、动作种类及后端预算选择轨迹、耗时、dwell、
多击间隔、组合键交叠/释放顺序、滚动节奏与逐键输入间隔。目标宽度未知时采用
有界默认策略，不要求模型补时间。拟人基线体现连续位置和自然加减速、合理按压
与间隔，不以随机抖动、误击、过冲或故意错字作为默认。策略须可复现并带版本；
具体自动耗时规则与各类默认时序在 M7-25 修订中冻结，M7-26 按后端校准验收。
已在目标位置时允许省略空移动，仍完成自然按压/释放。直接文本传输和语义 Invoke
保持其实际语义，不能冒充逐键输入或添加虚构鼠标轨迹。

普通点击的完整执行边界为：

`准入/前置守卫 → 回读起点 → 自动规划并连续移动 → 到达及目标重验 → 按下/停留/释放 → 执行回执（按需附观察/显式条件证据）`

普通完整动作默认不要求 `verify_kind`，也不强制在返回前运行 OCR、语义后条件或
独立模型验证。按 [DEC-056 的分层修订](DEC-056-post-action-verification.md)，
无中途状态依赖的有界动作组可在组末统一观察；状态依赖处及任务结束时再检查相关
结果。未请求业务条件不产生全局 unverified 围栏，执行完成也不冒充业务已验证。

轨迹执行不依赖模型逐点返回，不在移动中等待模型补时间。动作 lease 与同一
action identity 覆盖整个执行链；内部入队/派发进度只能作为进度事实，不得在尚有
轨迹或按键待执行时向模型结算成功，也不得提前触发 DEC-056/109 的后置验证。
前置与途中安全核验继续执行；途中画面变化不能冒充点击后的验证证据。动作完成
表示本次输入已执行且自身按下的键/按钮已确认释放；应用业务成功仍由后置验证
判定。取消/失败时可在受控清理结算后返回失败或不确定，不强行完成点击。

受控持键会话是显式例外：本次 down/tap 等推进步骤结算后可返回 `holding` 并取图，
不必等整场会话释放，但不得作为普通完整动作成功或会话业务完成。其最终验证仍
在显式释放、确认释放并取得新观察后执行。

#### 轨迹、守卫与迁移

能力齐备时，点击采用“读取当前位置 → 自动生成有时长的路径 → 分段投递 →
终点重验 → 点击并释放”。起点来自平台回读或同源可信观察，目标来自现有引用与坐标变换；
起点不可得或目标身份不足时拒绝该动作。移动与按住按钮的拖动分开表达。

初版采用可复现的 minimum-jerk 加减速基线，并按距离、有效目标宽度和预算校准
耗时；不默认加入随机抖动、绕弯、误击或过冲。路径保护包含 hover/弹窗副作用、
拖动路径和接收端，不能只核验起点。执行中目标变化或用户介入时停止或有界重规划；
改向时保持运动连续性或先受控停止，不拼接造成速度突变的静止起止曲线。

键盘执行显式 dwell、组合交叠与释放顺序。持续 down、OS 自动重复和显式多次 tap
分别声明；后端未证明重复语义时不以重复 tap 冒充持续按住。轨迹点数、时长、速度、
加速度和在途事件均有上限，数值随平台/设备能力在实现前冻结。记录期望与实际投递
时刻及偏差，晚到时停止或重规划，不瞬时补发积压轨迹/按键；不宣称硬实时。

早期 `glide_ms` opt-in 是过渡实现：省略参数仍直接定位点击，不满足上述默认要求。
M7-25 须冻结无模型时间参数的默认策略及兼容方案；M7-26 迁移所有适用工具路径，
不能仅修改提示词，或通过默认 schema 值要求模型代为计时。点击首增量已按文末契约迁移；其余路径保留
DEC-100 的既有 16 段/一秒等历史契约；新默认的总动作预算、工具版本与旧客户端
处理须同步冻结并验收。能力或可信起点不足时明确拒绝/报告限制，不能把瞬移兜底
宣称为自然动作已完成。

### 3. 建立由 Task 所有的跨观察输入会话

Runtime Service 在唯一 ActionCoordinator 下持有会话资源，Task 是逻辑 owner。
同一共享桌面保持一个写者；输入会话跨工具返回保留 lease，允许同一任务持键、
取图、追加操作和释放，不让其他会话或旧式工具绕过该 owner。观察不释放按键，
每次新动作仍通过权限、当前上下文及观察新鲜度核验。

候选会话身份包括 task/session、授权源、后端与设备实例、authority epoch、会话
generation、单调 command sequence、绝对 deadline 和预算版本。宿主持有按键/按钮
集合，模型只获得状态投影；不能用模型自报的 held keys 决定清理。重连、重启或
来源变化不自动继承旧持键状态，不重放已执行命令。

推进状态为 `ready -> executing -> awaiting_feedback -> executing/releasing`，
释放后进入终态 `released`；释放或状态确认失败进入 `fenced`。取消/超时从任一
活动态进入 releasing，终态幂等，迟到命令和模型结果不得恢复输入。反馈等待包括
截图、OCR 和模型决策，其耗时计入持有预算，不能因不断截图或重复查询而续期。

普通按压与明确需要等待决策的持键分别设最大时长。最大持有键/按钮数、会话总时长、
无指令期限、动作点数、存量与清理预算在 M7-25 冻结；除显式重新准入外不得自动
续期。预算未冻结前不开放生产持键工具。候选 begin/advance/observe/release 是逻辑
操作，不是本记录注册的新工具名或允许模型执行任意输入脚本的接口。

回执区分准入、投递、受控持有、已释放和不确定，并携带当前状态版本、剩余预算及
副作用信息。现有 `input_released=false` 表示失败封锁，不能复用它表示正常 hold；
M7-25 必须冻结版本化回执与旧客户端行为。按键持有成功不表示业务完成，只有显式
释放并完成后观察，才允许把该输入会话作为干净终态结算。

### 4. 持键期间的反馈与输入守卫保持闭合

Alt-Tab 的标准行为是：`Alt↓ -> Tab↓↑ -> 观察 -> 再次 Tab 或 Shift+Tab ->
观察 -> Alt↑ -> 验证活动窗口`。按住显示菜单、蓄力或瞬态信息的任务同样允许中途
取图。无需每个轨迹点请求模型；确定时序由本地执行，判断分支才消费新反馈。

会话显式声明当前允许的交互上下文。系统切换器等有意焦点转换只能在受支持且已验证
的转换范围内进行；不沿用“始终同一前台窗口”守卫直接放宽为任意桌面输入。无法
辨别预期转换与外部焦点改变时停止推进，结算并重新观察。当前 image_scope 的更新
不等于旧会话授权失效，也不授予旧坐标继续输入；权限/source/authority 改变仍为屏障。
DEC-109 的连续感知是可选反馈来源，按需截图可独立闭合首增量。

取消、接管、异常、模型失联、撤权、设备断连和 shutdown 均停止新事件并进入释放。
只释放本任务持有的输入；物理输入与本任务同键重叠或来源归属不明时保守处理，
不得全局 key-up 干扰用户。撤权只允许执行本任务必要清理，不继续业务动作。
释放 Alt 或鼠标按钮可能确认切换/拖放，清理有副作用时回执必须如实报告。

设备离线或平台无法确认释放时封锁后续写入，不能让 owner 析构或 resume 抹去问题。
外接 HID 原型须验证控制链路中断后的设备侧释放/看护能力；没有该能力时显式限制
其持键支持。主机 soft deadline 不等于硬件在绝对时限内必定释放。

### 5. 设备层输入以目标应用兼容性为验收目标

保留现有软件后端，并为设备层适配保留同一 InputProvider 边界。纯软件部署先验证
Linux uinput 与 Windows VHF；明确需要真实 USB 外设输入的部署并行验证外接 HID。
两条路线共享动作、所有权与回执，平台选型根据实际目标、权限和部署成本分别冻结。

| 候选路径 | 定位及限制 |
| --- | --- |
| XTEST / SendInput / Portal | 继续服务已验证应用；软件派发成功不证明应用接收，不能用轨迹自然度替代设备兼容性 |
| libei/EIS | Wayland 授权模拟输入候选；普通客户端可经正常输入栈接收，compositor 仍可区分模拟来源，单列验证 |
| uinput / VHF | 内核虚拟设备候选；分别涉及设备访问权限或 Windows 驱动开发/部署，不能只凭设备枚举认定兼容 |
| 外接 USB HID | 目标机实际枚举外接键鼠；需硬件、传输、相对位移校准、键盘布局、报告容量与断连释放验证 |

本决策不锁定第三方固件、驱动或库，不授权修改依赖、安装系统驱动或更改设备访问
策略；实际引入前按供应链与权限流程落实。后端可用性、部署前提与已验应用范围
向调用方可见，声明“应用/版本/环境下可操作”，不承诺任意程序兼容或来源不可识别。

后端在会话准入时固定，可按用户配置和已验证应用能力选择；活动会话不热切换设备。
切换必须先结算旧会话、确认输入释放、重新观察并重新准入。输入可能已部分生效时，
禁止因画面未变而换后端重发。重复 command sequence 不重复产生副作用；不能确认
设备是否接收时返回不确定。设备 ACK 只证明传输/报告接纳，不证明目标应用业务成功。

兼容性记录须区分：应用过滤注入、依赖设备输入、Windows UIPI/权限阻止、焦点/
布局/IME/时序错误。至少用一个明确过滤规则的受控 fixture 验证路径差异，并用实际
受影响应用验证收益；缺少真实目标时只能关闭 fixture 范围，不能宣布问题整体解决。

### 6. 观察并控制 IME，验证最终文本接收端

输入状态区分布局、引擎、激活、中英文转换、预编辑、候选和焦点归属；每项携带
来源、采集时间及 known/unknown。Fcitx5/IBus/TSF 等访问限制在 Platform Backend
内处理，读不到某状态时不把引擎名称推断成中文模式。

独立 Shift、Ctrl+Space、Win+Space 等按当前配置和上下文使用，切换后取新证据；
有可用状态设置能力时可避免盲目 toggle，具体动作须如实标记。输入法候选可通过
语义或截图识别；预编辑提交与表单提交分开验证，切窗后重新确认焦点与转换状态。
不静默改系统布局、永久偏好或剪贴板，也不把直接 Unicode 输入冒充经 IME 输入。

### 7. 所有推进、延时、传输与清理受 kairo 管理

Runtime Service 保持进程内唯一 Executor owner，Task/动作会话显式持有任务与 timer
句柄。有限推进使用公开 `submit_auto`/`submit_cancellable` 并消费 future；需要等待
的轨迹步骤和期限看护使用公开 timer 能力，防重入并核验 generation/sequence。
取消 timer 不代表已排队/运行回调消失，迟到回调必须自拒绝；准入拒绝或资源不足时
不能留下无看护持键，按清理路径结算。

长期阻塞设备 I/O 使用公开 blocking worker 能力；实现前按 pinned 集成指南加载
对应 capability card。跨上下文有界命令用 MpscChannel，最新状态用 LatestMailbox，
快照/订阅分别用 DoubleBuffer/Topic；控制与结算不得被 latest-wins 丢弃。平台或传输
回调只做有界校验/投递。任务健康、背压、失败与关闭沿用 Executor 设施，不另建
调度、线程池或监控系统。

普通 timer 为 soft scheduling，不承诺固定周期与绝对释放时限。若实际设备需要
realtime/low-latency 路径，先核对 pinned 公开能力、平台条件并单独验收。关闭顺序为
停止动作生产者/准入 → 取消并释放 → 唤醒/回收 I/O worker → 结算有限任务与句柄 →
外部非 worker owner `shutdown(true)`；清理失败保留封锁和失败事实。

## M7-25 契约冻结稿（待评审）

平台中立契约落在 `desktop/environment/include/mirage/desktop/input_session.hpp`（实现
`input_session.cpp`），机器可读冻结稿与完整状态/负向矩阵为
`tests/desktop/data/m7_input_session_contract.json`，由 `input_session_contract_test`
与代码逐项对账。以下为冻结内容摘要；数值调整须升 `budget_version` 并同步 golden。

### 能力位（声明顺序即位序）

`software_injection`、`smooth_move`、`timed_press`、`independent_modifiers`、
`cross_observation_hold`、`hold_capture`、`key_repeat`、`ime_state`、`ime_control`、
`kernel_virtual_device`、`external_hid`。后端在会话准入时声明固定集合；任何命令都要
求至少一个注入源位（software_injection / kernel_virtual_device / external_hid），
能力缺席在副作用前拒绝。

### 会话键词表

- base 键复用 InputProvider KeySym 基词表（可打印字符、命名键、f1..f12）。
- 裸修饰符 `ctrl`/`alt`/`shift`/`meta`：逻辑修饰键；平台区分左右时后端映射到左侧
  物理键并在回执/文档如实标注。
- 带边修饰符 `ctrlleft`/`ctrlright`、`altleft`/`altright`、`shiftleft`/`shiftright`、
  `metaleft`/`metaright`：显式物理键位；不区分左右的后端必须拒绝。
- 组合前缀（`ctrl+c` 等）不是会话键名：会话以独立 down/up/tap 命令序列表达组合，
  完整 key 工具扩展接受裸 `ctrl`/`alt`/`shift`/`meta`，映射逻辑左侧修饰键，
  在同次调用中按下、自然停留、释放；不建立 held lease。`ctrl+shift` 等以修饰键
  结尾的组合仍拒绝；左右侧键与跨调用 down/up 留在受控会话工作项。
  base 键词表 `is_base_key_symbol` 保持不含修饰键。新工具版本沿本轮 4.0.0。

### 预算数值（budget_version = 1）

| 预算 | 冻结值 | 依据 |
| --- | --- | --- |
| 单会话最大持有键/按钮数 | 4 | Alt-Tab、Shift+Tab、鼠标按住类任务的实际组合上限 |
| 单会话最大命令数 | 64 | 有界存量；超过须显式重新准入 |
| 平滑移动轨迹点数 | 上限 64，默认 32 | 覆盖全桌面跨度的采样下限，同时限制在途事件 |
| 平滑移动时长 | 50–2000 ms | 下限防瞬移冒充平滑；上限约束 hover 副作用暴露窗口 |
| 单次定时按压 dwell | ≤ 1500 ms | 完整动作在一次命令内结束；更长按住须走会话预算 |
| 无指令期限（含反馈等待） | 15 s | 覆盖截图+OCR+模型决策的典型往返；重复观察不续期 |
| 会话总时长 | 60 s | Alt-Tab/蓄力类任务的保守上界；只可显式重新准入 |

内部显式时长 API 的默认平滑配置为 `minimum_jerk_v1`（确定性直线）；模型物理点击
的默认接近现采用文末 pointer_motion_policy v2 的有界随机曲线。

2026-10-20 评审补充：本表及 `InputSessionCommand.duration`/`dwell` 是当前内部
执行契约；内部接收具体时长不等于要求模型提供时长。默认自动规划、各动作时序、
全量完成边界尚未反映到代码/golden，M7-25 保持 In Progress；点击默认与 glide_ms
迁移已按文末 pointer_motion_policy v1 实施，不改变本内部会话预算表。
修订须同步契约、策略/预算版本、golden 和迁移说明，不能以现有对账通过关闭新增要求。

### 状态机与回执

状态 `ready → executing ⇄ awaiting_feedback → releasing → released`，释放或状态确认
失败进入 `fenced`；完整 6×8 状态×事件矩阵（含全部拒绝项）以 golden 为准。回执
`version = 1`，区分 `admitted/delivered/holding/released/uncertain`，携带状态版本、
命令序号、budget_version、剩余命令数与无指令预算、持有投影、副作用与
`release_confirmed`（仅在 released 置位；正常持有不复用旧 `input_released` 语义）。

### 迁移

旧工具（`desktop.input.key`/`click`/`type_text` 等）在默认迁移验收前保持已验完整动作语义与回执；
新默认必须覆盖语义目标转 Pointer Action、视觉编号、区域与共享会话等实际键鼠入口，
以省略所有通用时间参数的真实工具调用验收，不能仅验底层 Provider 或单一路径。
持键回执为版本化新形态，旧客户端不订阅、不消费。会话 begin/advance/observe/release
是逻辑操作，本轮不注册新工具名；工具暴露在 M7-26/27 接线时按本冻结 schema 与
golden 执行。取消/超时/接管/撤权/设备断连/shutdown 从任一活动态进入 releasing，
终态幂等。

## 依赖与备选方案

已核对 pinned Mira 的 `InputEvent.duration_ms` / `InputSequence` 与环境 API；
Mirage 当前 binding 的 execute 拒绝输入，生产接线经 harness 工具到 Provider。
继续复用 Mira 控制面与 Mirador 观察，不另造 Agent 循环，不把 Android timed gesture
能力宣称为 PC 持键已交付。kairo 选型已按集成指南 by-requirement/scheduling 核对；
目前未确认必须修改依赖的缺口，若出现则先按工程规范 9.4 登记证据。

未选为主方案的替代项：仅改提示词（无法表达缺失动作）；仅加 hold duration（无法
覆盖观察后决定何时释放）；无主裸 down/up（清理不闭合）；每个移动点请求模型
（增加延迟）；随机运动噪声（不保证可靠性）；只换注入 API 或只换 HID（分别不能
覆盖所有过滤及 IME/焦点问题）；不确定后自动切后端重试（可能重复副作用）。

## 影响与风险

涉及 Provider 能力、动作租约、跨轮状态、权限/接管、工具 schema、回执及观察投影，
实现时须同步安全文档、示例、wire/golden 和兼容性记录。持键增加模型等待期间的
桌面占用，设备后端增加部署与释放故障面；这些成本必须计入预算与产品能力说明。
普通完整动作无需建立长会话，保留简单路径；新持键回执与旧客户端不混用。

## 验证方式

- 默认与完成边界：各类键鼠动作省略通用时间参数仍生成并实际执行本地自然时序；
  记录中间事件、移动终点、最后释放、动作结果、后置观察/验证及工具返回的顺序。
  点击的成功动作结果和后置验证均不得早于最后释放；模型慢响应/不返回时间不影响
  已准入轨迹推进。覆盖零距离、多击、拖动、滚动、组合键、逐键文本及所有工具入口；
  能力/起点缺失、取消、落后停止与清理失败不得退化成未声明的瞬移成功。
- 时序：真实事件位置/时间、起点回读、速度曲线、点击/拖动/hover 效果、目标变化、
  时序落后停止、多屏/DPI、单独 Shift、组合交叠、hold 与 repeat 分别验收。
- 反馈：Alt-Tab 正反向、按住显示菜单/信息并截图、模型慢响应/失联、释放与最终
  活动窗口；直接脚本与模型自主使用分别记录，不以接口存在代替模型会用。
- 生命周期：正常、异常、提交拒绝、运行中取消、超时、shutdown；追加撤权、接管、
  设备断连、重复命令、迟到完成、物理同键竞争与释放失败封锁。
- IME：切引擎、Shift 切模式、预编辑/候选翻页/提交、切窗状态变化、未知状态回退；
  对比最终文本和实际接收端，不把候选上屏等同于表单提交。
- 设备兼容：过滤 fixture、实际受影响应用，记录 OS/应用/驱动/固件版本、输入路径、
  权限、投递回执、目标接收和业务后条件；分别报告虚拟设备与外接 HID 收益。
- 资源：长时会话、有界存量、慢模型/慢传输、timer 堆积/拒绝与取消到释放的 P50/P95/
  最大观测值；ASAN/UBSAN、跨上下文 TSAN/故障注入及现有输入回归。

具体数值门槛、真实受影响应用清单与运行条件在相关工作项实施前冻结。本轮只有文档
及静态核对，未实现接口、安装设备/驱动或执行上述验证。负责人 Mirage 维护者；
补跑条件为对应工作项接线完成，目标 OS/compositor、模型、IME 与设备环境可用。

## 关联文档和工作项

- [CU 动作执行与验证门控调研（2026-10-20）](../research/2026-10-20-computer-use-action-verification.md)：
  10 个项目/工具栈区分执行、观察与业务验收；维护者已接受按 DEC-056 修订默认逐动作
  硬门禁，v2 首增量已接入源码。“完整执行后返回或验证”不构成新增逐动作 OCR/VLM 要求。
- [M7-25～31 专项计划](../plans/m7-continuous-input-and-device-compatibility.md)、
  [M7](../plans/m7-visual-tools-and-input.md)、[总计划](../plans/mirage-implementation-plan.md)。
- [主设计第 5.1 节](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md#51-连续输入与设备兼容性提案)。
- [DEC-054](DEC-054-scoped-complete-desktop-actions.md)、
  [DEC-100](DEC-100-unified-pointer-actions.md)、
  [DEC-109](DEC-109-continuous-perception-attention-and-pet-coexistence.md)。
- 平台和研究的一手来源、能力限制及本地代码定位统一保留在
  [调研文档](../research/2026-10-19-continuous-input-and-hid.md)。


## 2026-10-20：自动接近点击首增量（M7-25/26）

维护者要求继续推进平滑移动并更新 native-release。本次将三条物理点击入口
`desktop.vision.click`、`desktop.vision.click_region`、`desktop.session.click`
改为默认本地自动接近，工具版本为 **4.0.0**，schema 删除 `glide_ms`，不新增模型
时长参数。语义 Invoke 保持其实际语义；滚动/拖动、逐键及多击的 dwell/间隔、
受控持键和 Windows 迁移仍未在本首增量交付，DEC-110 整体保持 Proposed。

独立策略 `pointer_motion_policy::version=1`（不改 held-session budget_version=1）：

- 起点由 InputProvider 当前指针回读；Portal 从当前授权流的 Cursor 元数据取得帧内
  起点，按该流的 frame/logical 比例映射，不使用另一共享来源或旧注入位置冒充回读。
- 目标与宽度来自已验证编号/区域。距离 d 用同一坐标空间的欧氏距离，宽度 w 取目标
  的短边；未知宽度默认 32，最低取 8 像素。时长为
  `ceil(120 + 90*log2(1 + d/w)) ms`，截到 120～650 ms；零距离不发空轨迹。
  这是可复现的初始产品策略，不宣称已经过人类数据/目标平台性能校准。
- 复用 `minimum_jerk_v1`、默认 32 采样点、4 ms 采样间隔下界与既有 64 点上限；
  X11 每段即时 flush，Portal 每段同步投递。睡眠返回后再次检查取消/时限，落后
  250 ms 停止，不补发积压、不瞬移兜底。X11 检出物理按钮已按下时拒绝移动。
- 编号/区域点击的本地总操作上限 3 s，含接近与守卫，最后点击最多使用剩余时间及
  原 1 s 子预算。Portal 沿既有 5 s 外层边界推进，内部 Move/Click 各自保持有界。
  均受更早的任务期限/撤权/接管约束，不声称阻塞平台调用可被硬中断。

完整顺序为 `准入/目标守卫 → 自动接近 → 到达守卫 → 点击及释放 → 回执/可选条件`。
同一 action lease 覆盖全部阶段；Portal 的内部 Move 只返回给宿主，随后只读取图
核对同源新帧及目标，再在同一 lease 内派发 Click，不把 Move 完成当作模型工具成功。
到达观察不发布新 image_scope，不让模型中途重新规划；后续业务验证仍在完整点击
结算后按 DEC-056 v2 执行。到达守卫沿用 Mirador 的目标稳定性能力和平台焦点/几何
检查；移动/取消/目标变化时不得派发点击，已有副作用如实返回并保留现有保守围栏。

Portal 在支持 Cursor metadata 的 ScreenCast 上选择 cursor_mode=4，并请求有界
SPA_META_Cursor；解析限定在固定头部，验证有效 ID 与帧内坐标，格式变化即退役起点。
缺少可信起点时 `pointer_origin_unknown`，不得提示用一次瞬移点击建立起点；无元数据
的 compositor 仍可能无法使用此自动点击路径。普通截图不因此增加权限提示或后台循环。
2026-10-20 最新会话故障修订：在 PipeWire 的每次 Format 事件后通过
pw_stream_update_params 完成 Header/Cursor 缓冲元数据协商；Cursor 允许到 512×512
RGBA 加固定头的有界容量，覆盖 Mutter 46 的固定 384×384 分配。只请求 256×256
会使视频仍可用而 Cursor 协商无交集，不能把截图成功视作起点已就绪。

同日后续共享流修复：Cursor-only 缓冲（Mutter 46 使用零像素长度及 CORRUPTED
chunk 标记）只更新元数据并归还，不退役共享流、不覆盖待消费的有效像素，也不推进
像素 source_sequence 或源时间戳。空/损坏像素在现有调用期限与取消范围内等待有效帧；
仅有鼠标更新不能通过点击后的新帧/隐藏桌宠时间围栏。实际非空映射或布局错误仍失败。

DesktopCaptureSession::pointer_position 只读当前已采集帧的起点、尺寸和来源序号，
不触发授权 UI 或输入；共享点击在获取 action lease 前检查它与刚采集帧一致。
缺失、越界、错来源/尺寸或查询异常在无副作用阶段拒绝，不推进输入 epoch，原图
仍只在原有新鲜度/授权范围内可用于键盘等独立操作。取得 lease 后仍保留移动、
到达及释放守卫，不允许借此绕过接管、撤权和已发生副作用的不确定性围栏。

旧低层显式时长 API 留作内部能力/兼容测试，不能由新模型 schema 选择直达路径。

契约 golden：`tests/integration/data/automatic-pointer-motion-v1.json`；验证与构建命令见
[自动接近点击验收](../compatibility/automatic-pointer-approach-20261020.md)。M7-25/26
保留 In Progress；不把三条点击入口交付当作全量键鼠自然时序交付。

## 2026-10-20：有界随机接近 v2（M7-25/26）

维护者要求加入时长随机性、曲线路径及末端微调/轻微过冲。默认物理点击采用
pointer_motion_policy v2；模型工具 schema/version 不变，仍只提供目标意图。
旧低层显式时长 smooth_move_to 默认 minimum_jerk_v1 保持确定性；仅携带宿主生成
的 PointerMotionVariation 时使用自然路径。该值不能由模型指定。

- 基础耗时公式保持 v1，乘以 [0.88,1.12] 的随机系数后向上取整，再限制到
  120～650 ms；零距离不读取随机源、不移动。短时限不足时拒绝，不缩短轨迹兜底。
- 每次操作在现有 Executor 调用内使用本地随机源生成种子；不建立全局 RNG、线程、
  定时器或队列。随机源异常明确返回无副作用错误。固定种子可重建时长和采样点。
- 平滑弧线叠加在 minimum-jerk 主进度上，横向弯曲幅度最多 min(24,0.08d,0.75w) 像素；
  末端平滑修正最多 min(3,0.10w,0.015d) 像素。35% 的计划允许沿运动方向加入
  最多 min(3,0.10w,0.01d) 像素的末段过冲；是否能在像素取整后观察到依距离而异。
  不逐点添加白噪声，避免高频抖动。小距离/小目标自动收紧。
- 复用 32 点默认/64 点上限与 4 ms 节拍下界；最后一个采样点和时间必须精确等于
  目标及计划总时长。X11/Portal 在平台边界将随机中间点裁到当前桌面/授权流内部，
  屏幕边缘可能使曲率变小。最终落点不随机偏移。
- 点击仍在全部移动、回到目标、到达守卫之后；取消、释放失败及落后 250 ms 的
  语义保持不变。新增轨迹是产品策略，未以真实人类数据校准，不宣称等同真人行为。

验证通过后更新 native-release；证据见[有界随机轨迹验收](../compatibility/natural-pointer-motion-20261020.md)。M7-25/26/31 保留开放，
不扩大到未实现的键盘、滚动、拖动、受控持键或 Windows 路径。

## 2026-10-20：完整滚动、拖动与键盘自然时序增量（M7-25/26）

按维护者继续实施要求，新增 input_cadence_policy v1（held-session budget_version
不变），范围为 X11/Portal 已有完整动作。策略在宿主生成种子，模型不提供时间。
数值是产品默认值，尚无人类样本校准，不声明硬实时：

| 阶段 | 本地默认范围 |
| --- | --- |
| 单键停留 | 45～85 ms |
| 文本字符间隔 | 35～85 ms |
| 修饰键依次按下间隔 | 12～28 ms |
| 正常逆序释放间隔 | 8～18 ms |
| 每步滚轮停留/投递结算 | 8～16 ms |
| 相邻滚轮步间隔 | 30～65 ms |
| 拖动按下/落点停留 | 45～80 / 35～65 ms |

滚动按原符号和精确步数逐步派发；拖动先自然接近起点并重验源/目标，再按下，
以 220～900 ms 的有界 minimum-jerk 曲线移动，禁用前向过冲，中间点限制于授权
窗口/共享范围，精确到达后释放。滚动也先接近并重验，不能退化为起点瞬移。
键盘 chord 依次按下修饰键和主键，停留后逆序释放；文本逐字符按下/释放并留间隔。
Unicode/IME 能力沿既有后端限制，不将粘贴或最终文本传输冒充物理打字。

完整指针/按键动作上限 3 s，文本上限 15 s，仍受父 operation 更早的期限约束。
派发前核对完整计划能否放入剩余预算；过长文本须拆分，不能先输入一部分才发现
必然超预算。取消、撤权、期限、派发失败和异常时跳过正常释放间隔，立即尝试释放
本动作拥有的输入；清理不受取消抑制，无法确认释放沿原 action fence 上报。

纯计划复用 Desktop 轨迹与独立时序函数；平台等待是现有有限 Executor 任务内
有界同步 continuation，每 ≤4 ms 检查停止，不创建线程/队列/独立调度 owner。
每段已超过计划 250 ms 时停止，不回放积压事件。该路径不引入跨工具持键或后台
控制循环，M7-27 与 Windows 保持开放。实施及验证结果须在专项计划追加，不以此
契约修订宣称已通过验收。


## 2026-10-21：M7-27 受控持键首个接线范围

维护者授权推进长按与持键开发。首个生产接线限定固定前台窗口：X11/XInput2 通过
独立 `InputSessionBackend` opt-in 声明受控持有和期间截图能力，其他 Provider 默认
明确不支持。`desktop.input_session.begin/advance/observe/release` 是独立 1.0.0 工具
族，正常 holding 不使用旧 `InputOutcome::input_released=false`。begin 绑定当前
任务/会话、真实前台窗口及几何、期望 action epoch；advance 使用宿主 session id、
最新 state_version 和连续 sequence，重复或迟到命令不重放。

宿主持有既有 ActionCoordinator 单写者 lease、实际待释放键/按钮集合；普通写入
在会话期间拒绝 busy，截图可继续。首增量开放 key_down/up/tap、button_down/up/tap，
最多四项持有、64 个变更、15 秒 idle、60 秒 total，沿预算 v1；observe 与截图不续期。
鼠标按钮只在当前指针位于绑定窗口内时操作，不提供持有中的移动或自由脚本。
长按使用 `press=long` 意图，由 Desktop 本地种子规划 1080～1240 ms（小于既有
1500 ms 完整按压上限），模型无时间参数；普通点按维持既有自然时序。

X11 driver 独立持有平台连接，检查窗口销毁/配置、前台及几何、键/按钮读回状态，
并监测 XI2 raw 输入；物理接管停止推进。同键竞争或事件预算耗尽导致归属不明时，
不能向可能由用户持有的输入盲发 up，而是封锁写入并明确报告不确定。原生物理
设备、X 服务故障及 compositor 仍需实测，不承诺所有输入来源均可区分或硬期限。
系统切换器/Alt-Tab 的焦点转换、held pointer travel、OS repeat、IME 与跨设备能力
未验，不扩大本次范围；默认不修改键盘重复或系统配置。

同一 ModelLayer 复用一个 kairo blocking worker；命令通过容量 8、RejectNewest 的
MpscChannel，状态使用 LatestMailbox。每个准入的新 session 使用独立 Core，前一
worker run 必须 join 后才能重绑；旧 facade/请求不能停止或推进新 owner。平台连接
和 held 状态仅由该 worker 使用，等待的 condition variable 只唤醒 comm 数据面，
没有第二套队列/线程/计时 owner。父取消、权限策略换代、接管、期限、平台错误、
模型拒绝/异常或任务终结都会进入释放；无活动输入时 worker 停止，不后台空转。
停止是 soft、协作式的，不把 4 ms 等待切片宣称为平台往返或释放硬保证。

Portal 另提供同次调用完整长按：`desktop.session.click/key` 升为 4.1.0，新增可选
`press=normal|long`；默认行为不变。长按点击只能 click_count=1，仍自动接近并重验
落点；完整预算不足在按下前拒绝，释放完成后才返回/验证。DesktopCaptureSession
需独立声明 timed_press_supported，不能从已有 click 推断支持。Portal 跨调用持有、
Windows 受控持键及真实系统切换器继续开放。证据与实际门禁见
[本轮记录](../compatibility/controlled-input-sessions-20261021.md)。


收尾边界：已知完整按压计划必须放入剩余 session/operation 预算，否则按下前拒绝。
执行中的协作停止同时检查父取消和 Executor worker stop token，不能只在空闲循环
检查。已确认释放的已知失败按 DEC-056 的既有策略，在释放代际及之后的新桌面
观察到达后才允许恢复结算；旧代际截图不能退休失败，取消/接管与释放 uncertain
不通过该路径消除。当前会话状态在截图后进入 awaiting_feedback，但不续期。

## 2026-10-21：M7-27 固定窗口内的持键移动

本次推进将既有 `smooth_move` 命令接入 X11/XInput2 受控会话，不改变焦点转换契约。
`desktop.input_session.advance` 升为 1.1.0，新增 command=smooth_move 和全局桌面
int32 坐标 x/y；begin/observe/release 保持 1.0.0，input_session_v1 与预算 v1 不变。
移动不得混用 key/button/press 字段，键鼠按压命令不得携带坐标；模型不能提供时间、
路径点数或轨迹。工具在授权后读取指针，用已有 automatic_pointer_duration 规划
距离相关时长（零距离以 50 ms 作为内部合法计划，平台不发 motion），上限沿用
650 ms；平台在持有 lease 的 worker 上重新读回真实起点并生成 minimum_jerk_v1。
持有中的轨迹采用单调基线，避免弯曲/过冲引入额外拖放落点；不改普通点击策略 v2。

后端只在原绑定窗口及当前 X screen 可见范围内移动：起点、目标和全部采样点在
派发前检查，每段继续核对焦点/几何/生命周期、实际持键状态、XI2 物理干预和指针
所在窗口；按时派发并同步读回每段坐标，不在结束时一次性提交缓冲轨迹。取消、
权限换代或迟到超过既有 250 ms soft 容差时停止余段，不补发、不重试、不回滚坐标。
每次移动消耗一个命令，不改变 held 集合；截图期间仍受原 idle/total 预算看护。
完整计划无法放入 session 或 operation 剩余预算时拒绝移动并清理已有持键。

沿用一个受管 blocking worker、容量 8 FIFO、LatestMailbox 和 ActionCoordinator；
没有新增线程、调度器或跨上下文可变状态。Mira 的通用离散 InputSequence 不提供
桌面窗口绑定的 held owner，Mirador 不提供输入派发；此能力属于 Mirage 的平台适配。
实现核对 pinned kairo by-requirement → blocking-io card 与公开 executor/blocking_io
接口；路径等待复用已有同步有界平台延续，stop/join/future 消费与清理顺序不变。

示例（session_id、版本与 sequence 必须取当前宿主回执，截图后先更新状态）：

```json
{"session_id":"<host-id>","state_version":3,"sequence":2,"command":"smooth_move","x":340,"y":190}
```

可在 key_down 或 button_down 后移动、截图、继续移动，最后显式 release；holding
只证明受控输入状态，不证明拖动业务成功。几何范围内新出现的遮挡窗口会在段间守卫
或读回失败时停止，已发生的移动如实保留。系统切换器/Alt-Tab、跨窗口拖放、Portal
跨调用持有、Windows、真实物理竞争与在线模型验收仍开放。
golden 与负例见 `tests/integration/data/controlled-input-travel-v1.1.json`，实际结果见
[持键移动验收记录](../compatibility/controlled-input-travel-20261021.md)。

## 2026-10-21：M7-27 XI2 来源与设备变化守卫

继续落实持键期间的输入归属和设备失效边界。X11 受控会话最低要求改为 XI 2.1：
XI 2.0 的 raw sourceid 未定义，且其他客户端的 active grab 可以阻止 raw 反馈，
不能据此承诺识别同键竞争。依据 X.Org
[XI2 RawEvent 协议](https://www.x.org/releases/current/doc/inputproto/XI2proto.txt)，
旧服务器在任何按下前拒绝；不影响普通完整输入入口的能力声明。

会话在订阅 raw/HierarchyChanged/DeviceChanged 后冻结当前 ClientPointer、配对
master keyboard 与两项 XTEST slave 的 id/use/attachment/enabled 信息，不热切换。
任何拓扑变化结束当前会话；未损坏原注入路由时只释放已知拥有的输入。原路由被移除、
禁用、重挂接、能力改变，或事件来源/容量无法判断时，保留不确定输入投影并封锁
后续写入，不向归属不明的键/按钮补发 up。正常 SlaveSwitch 通知不等于设备失效，
仍由随后 raw 事件判别真实干预，避免把自身 XTEST 活动当作设备丢失。

独立 Xvfb slave 注入用于演练不同来源、同键/同按钮、外部 active grab 与拓扑
变化；这是服务器协议/归属夹具，不是真实硬件拔插验收。沿用原 Executor blocking
worker、FIFO、mailbox、取消和 shutdown 顺序，不新增并发 owner 或修改依赖。
实际验证与剩余硬件门禁见
[设备守卫验收记录](../compatibility/controlled-input-devices-20261021.md)。

## 2026-10-21：M7-27 显式窗口切换器首增量

在私有 Xvfb + Openbox 3.6.1 中，正反选窗及持 Alt 截图期间原窗口保持实际焦点，
Alt 释放后才提交选择。本增量限定这个已验证 profile，不放宽固定窗口焦点/几何
守卫。Platform Backend 独立 opt-in `window_switcher_supported` 和专用 driver，
以 `_NET_SUPPORTING_WM_CHECK` 双向身份、WM name 和 `_OB_VERSION` 限定当前支持
的管理器，生命周期内持续复核；其他 profile 默认不支持。

新增独立 `desktop.window_switch.begin/advance/observe/release` 1.0.0 与
`window_switch_v1` 回执。沿用原会话字段和预算 v1，固定会话工具版本/回执不变。
begin 需要新鲜原窗口 id/epoch，同时授权 `window.activate` 和 `input.inject` 的
`window_switcher` 资源；window capture_scope 非空的任务不注册该工具族。宿主
自动持 Alt，advance 只接收 forward/backward、session_id、最新版本和下一命令序号。
正向是一次 Tab tap，反向由宿主安排 Shift down/Tab tap/Shift up；完整已知命令/
时序预算在首个事件前核对。没有模型键、坐标、时长或脚本参数。

两个工具族共享同一个 owner、worker 和 lease；不得跨工具族推进或释放会话。
平台专用 driver 也拒绝非 Alt/Shift/Tab 命令，固定会话拒绝组合跨观察 Alt-Tab。
observe/截图不续期；截图应选完整 display，以覆盖系统切换器。原焦点提前变化、
窗口/管理器/设备变化、父取消、接管、撤权、超时、模型失败和 shutdown 均进入原
清理路径，归属/释放不确定保留 fence。没有新并发设施或依赖修改。

release 负责释放 Alt，可能提交当前选窗；取消/超时等必要清理也可能提交选择，
不假称恢复原窗口。释放确认只表示输入已收敛，回执始终 verified=false。任务需在
释放后取得新鲜前台窗口观察验证目标；配置自定义按键或提前切焦点时可明确失败。
Openbox 的正反选窗、截图与模型脚本验收见
[切换器验收记录](../compatibility/controlled-window-switcher-20261021.md)。
GNOME/KDE 等其他管理器、Portal、Windows 和在线自主模型验收继续开放。

## 2026-10-22：Portal 节奏迟到后的只读收敛（M7-26）

维护者授权修复输入迟到及共享会话收敛。本增量保持现有 250 ms 迟到守卫和
不确定输入围栏，仅在 input_lag 且确认输入释放时保留同一授权流的只读观察。
后续输入在 backend 与 Adapter 双层拒绝；新图像的 input_actionable=false，
新帧不能消除既有动作不确定性。传输失败、释放失败、撤权及其他不明确失败仍
关闭会话，不自动重授权或重放输入。

失败附带有界阶段、样本索引、计划/实际耗时、迟到量和投递事件数，不含输入
正文或桌面像素。诊断是单次动作事实，不构成独立监控/调度设施；同步平台延续
仍在原 Executor 任务内执行。M7-26/31 保持 In Progress，真实 compositor 的
迟到来源和交互手感须实机验证。

## 2026-10-22：自主恢复规则替代

[DEC-113](DEC-113-autonomous-action-recovery.md) 部分替代本文此前的人工复核、
永久只读与全局执行围栏：已释放的不确定输入经当前观察和重规划恢复，独立
ApplicationProvider 路径不依赖旧点击；历史失败保留，不改为成功。取消、
接管、撤权和无法确认释放仍按具体状态阻止受影响路径。此前验证记录保留历史含义，
不代表当前默认策略；当前实现及限制见 DEC-113 和对应验收记录。

## 2026-10-23：M7-28 部分输入法状态观察

维护者授权按计划推进键盘与输入法。首增量为平台中立 InputMethodProvider 与独立
只读 `desktop.input.state` 1.0.0，默认不具备 Provider 的环境不注册工具。Linux 在
显式开启桌面 surface 时提供 GIO 查询，native Wayland 不依赖全局 InputProvider；
无 GIO 的构建保持无能力。IBus SDK 可选，仅用于取得当前会话的地址文件路径，
显式 IBUS_ADDRESS 不依赖该 SDK。没有新增 pinned 依赖或平台类型公共泄漏。

一次最多分别返回 Fcitx5/IBus 两项投影，不能据多框架运行推断目标应用的路由。
每项包括 engine、activation、conversion_mode、layout、focus_context、focused_window、
preedit_active、candidates_visible；每字段包含 optional value、source、captured_at_ms、
unknown reason。Fcitx5 State 表示最近输入上下文的 active/inactive/no_context，不等于
当前窗口的中英文模式；IBus GlobalEngine 和 CurrentInputContext 分别投影引擎及
输入上下文标识，不将 context path 当 window id，不将引擎声明布局当当前布局。
无可靠通道的字段保留 unknown；没有预编辑正文、候选正文或已提交文本采集。

仅查询已运行服务，不启动服务，不创建输入上下文、不接管面板、不改变布局/偏好/
剪贴板。调用目标冻结到 D-Bus unique owner，结束复核 owner；服务变化使本项退役为
unknown。字段为各次有界读回而非原子焦点快照，无输入授权、动作结算或业务验证效果。
默认总预算 1000 ms（Provider 合法范围 1..2000 ms），单次查询 200 ms；独立 GLib
context 仅承载有限平台 continuation，callback 只保存 completion，取消后消费 completion
才销毁存储。取消/超时整体失败，不返回可误用的部分成功。预算为协作式，未声明硬期限。

切换沿用已有授权的单独 Shift、组合快捷键与候选确认键；每次切换后重新观察，unknown
时以语义/截图补证。状态查询本身不能证明候选提交或最终目标文字正确。真实引擎、
候选翻页/提交、切窗及 Windows 门禁保持开放，具体实现证据见专项计划。

同日维护者补充视觉/OCR 使用方式：优先从实际可见的“中/英”标识取得模式证据，
需要时完整点按 Shift 并重新观察；中文按短拼音段输入，识别当前候选及其显示编号，
本页无目标时按当前配置用 `=` 下一页、`-` 上一页，每页重新观察，命中后按该页显示
数字。不得沿用旧页编号、把未读清当不存在，或在页不变/重复/边界后无限翻页。
若有未解决预编辑，先确认切换会不会提交/丢弃，避免盲目切模式；快捷键依实际
输入法配置，不强制修改系统偏好。选字提交、输入框最终文字和 Enter 表单提交分别
验证；切窗后重看焦点与模式。该策略通过 Mira 现有观察/规划循环和既有按键、
文本及 OCR 工具执行，Platform Backend 只负责派发，不新增宿主 Agent 循环。
