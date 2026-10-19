# 连续键鼠动作、持键观察、输入法与 HID 输入调研

> 状态：调研完成；工程建议 Proposed，未变更现行输入契约或产品行为
> 日期：2026-10-19
> 负责人：Mirage 维护者（后续实现及目标平台验收）
> 关联工作：M7-03/09/10/21 的能力扩展调研，不计为这些工作项的实现验收
> 依据：DEC-052/054/064/077/100；工程规范第 5、7、8、9 节
> 本地基线：Mirage HEAD `741fa00` 加现有工作树；Mira `a29374a`、Mirador `cb45a10`
> 范围：公开研究、官方平台文档与当前源码只读核对；未运行真实输入或修改依赖

## 1. 结论

用户提出的能力有充分研究和平台机制可参考。应分别处理四个问题：鼠标轨迹与时序、
跨观察保持输入状态、模型对动作语义的掌握、输入的设备来源。持续按键可以先在现有
软件后端实现，外接物理 HID 是独立的后端选项；更换设备来源本身不解决 IME 状态和
模型选错操作的问题。

维护者进一步澄清：设备层输入的目标是提高应用兼容性，解决部分程序过滤或拒绝软件
键鼠注入导致任务无法完成的问题。因此设备后端是独立的功能需求，应与连续动作并行
评估；验收以目标应用实际接收输入并产生预期效果为准，不要求伪装成某个指定外设。

Mirage 当前具备组合键和完整分段拖拽，但生产工具以调用结束时释放输入为契约，缺少
有所有者的跨观察持键会话。裸 Shift 也不在当前键名词表中。因此用户提到的部分困难
属于可确认的接口限制。应先补动作表达与生命周期，再验证模型是否会根据画面使用。

## 2. 当前实现核对

| 需求 | 当前事实 | 影响 |
| --- | --- | --- |
| 点击前连续移到目标 | X11 `click_sequence_at` 直接 `XTestFakeMotionEvent(..., CurrentTime)` 到目标；Windows `pointer_move` 发一个绝对位置 `SendInput` 事件；Portal click 同样先发目标位置 | 没有从当前位置出发的定时平滑移动 |
| 拖动轨迹 | DEC-100 冻结 16 段线性完整拖动；X11 按下后每段采用 8ms 原生投递延迟；Portal 循环发 16 个位置，没有同等显式节奏 | 已有空间分段，不能推断各后端都具备相同的时间连续性 |
| 普通组合键 | X11 `key_chord` 依次按下各键，随后逆序释放；生产 schema 表达一个完整 chord | `alt+tab` 能表达一次切换，不能保持 Alt 再看图选择 |
| 持键中观察 | DEC-054 不把原始 down/up 暴露为会话工具；shared input 描述明确 `No held keys` | 不支持持键跨模型观察轮次；增加提示词无法绕过契约 |
| 单独按 Shift 切中英文 | `parse_key_chord` 只允许修饰符前缀加基础键；`is_named_key` 没有 shift/alt/ctrl/meta | 单独 `shift` 被拒绝，需新增独立修饰键语义 |
| Unicode 和 IME | X11 文本受已有 keymap 限制；legacy Portal shared text 限 printable ASCII；Win32 使用 `KEYEVENTF_UNICODE` | 直接输入文本、模拟物理键、经 IME 组合输入是不同语义 |

定位：

- [InputProvider](../../desktop/environment/include/mirage/desktop/input_provider.hpp)：
  有底层 `inject_key` / `pointer_button`，完整动作有取消释放与显式 opt-in。
- [键名解析](../../desktop/environment/src/desktop_environment.cpp)：修饰符与基础键词表。
- [X11 后端](../../platform/linux/src/x11_backend.cpp)：`key_chord`、
  `click_sequence_at`、`bounded_pointer_action`。
- [Portal 输入](../../platform/linux/src/portal_input.cpp)与
  [shared 工具](../../integration/mira/src/desktop_shared_input.cpp)。
- [Win32 后端](../../platform/windows/src/win32_backend.cpp)：`type_text`、`pointer_move`。
- [会话工具](../../integration/mira/src/desktop_atom_toolset.cpp)：`desktop.input.key`
  接完整 chord，而非跨调用按键所有权。

以上是源码与契约核对，不是本轮真实平台行为测试。各平台已交付程度仍以
[M7](../plans/m7-visual-tools-and-input.md)及其验证记录为准。

## 3. 鼠标运动研究

| 研究与一手来源 | 已有证据 | 对 Mirage 的适用性与限制 |
| --- | --- | --- |
| Fitts，1954，[The information capacity of the human motor system in controlling the amplitude of movement](https://www2.psychology.uiowa.edu/faculty/mordkoff/infoproc/pdfs/Fitts%201954.pdf) | 目标距离与允许误差影响指向耗时 | 可按距离和有效目标宽度校准耗时；它不生成轨迹，原实验也不是桌面鼠标 |
| Flash & Hogan，1985，[The coordination of arm movements: an experimentally confirmed mathematical model](https://newmanlab.mit.edu/wp-content/uploads/2017/09/1985-the-coordination-of-arm-movements-an-experimentally-confirmed-mathematical-model.pdf) | minimum jerk 解释近直线点到点手臂运动与钟形速度 | 适合作为初版平滑加减速基线；手部轨迹不等于经过系统加速度映射的鼠标轨迹 |
| Martín-Albo 等，2016，[Strokes of insight](https://jeffhuang.com/papers/KinematicCursor_IPM16.pdf) | Sigma-Lognormal 用多个时间重叠 stroke 重建鼠标轨迹 | 能表达快速主运动与修正；主要研究重建、压缩和意图分析，未交付通用实时桌面控制器 |
| Müller、Oulasvirta、Murray-Smith，2017，[Control theoretic models of pointing](https://eprints.gla.ac.uk/134311/) | 比较四类控制模型，生成逐时刻位置、速度、加速度 | 支持把指向作为动态控制问题，模型拟合存在任务条件差异 |
| Álvarez Martín 等，2021，[Intermittent control as a model of mouse movements](https://arxiv.org/abs/2103.08558) | 观察与预测误差触发间歇修正；在一维往返任务中复现动态特征和变异 | 支持短段执行后必要时修正；一维实验不能证明所有桌面场景的可靠性 |
| Do、Chang、Lee，CHI 2021，[A Simulation Model of Intermittently Controlled Point-and-Click Behaviour](https://dodoseung.github.io/paper/point_and_click_simulation_model.pdf) | 联合模拟视觉反馈、间歇运动、点击时机及鼠标设备传递 | 最贴近完整 point-and-click；受控目标实验不覆盖复杂桌面、键盘或 IME |

工程推论：普通点击可编译为“读取当前位置 → 平滑移近目标 → 新鲜目标复核 → 按下/
释放”。最小 jerk 的静止起止点基线可用 `s(u)=10u^3-15u^4+6u^5`，`u=t/T`，
在起终点间插值；这只是候选轨迹，不是已冻结 API。耗时、采样率和目标宽度参数需
实测校准。运动中的改向需要保持当前速度/加速度或先受控停止，不能反复从静止曲线
拼接造成突变。出现遮挡、hover 菜单或目标移动时重新观察与判断。

研究没有要求每次绕弯、随机抖动或故意过冲。自然运动首先是有连续时序、合理速度和
反馈修正。轨迹中间经过控件可能产生 hover 副作用，平滑移动不能只当成界面动画。

## 4. 持键、视觉反馈与 Agent 研究

| 研究与来源 | 可核实能力 | 边界 |
| --- | --- | --- |
| OSWorld，2024，[§2.4 / Appendix A.3](https://arxiv.org/html/2404.07972v1) | 独立 `keyDown/keyUp`、`mouseDown/mouseUp`、`moveTo(duration)` | 证明动作表达已有先例，没有单列 Alt 持键跨截图任务成功率 |
| Cradle，2024 预印本 / ICML 2025，[正式发表](https://proceedings.mlr.press/v267/tan25h.html)、[早期全文 Appendix B.1](https://arxiv.org/html/2403.03186v2) | press/hold/release、持续时间、组合与视频反馈；示例用 hold 打开并保持轮盘，并明确教模型按键语义 | 早期接口允许无限 hold，不宜直接复制；早期 tween 参数脚注明确原型未使用，不能据此称已实现拟人轨迹 |
| SIMA 2，2025，[§3.2 / §4.2](https://arxiv.org/html/2512.04797v1) | 视频观察、键鼠动作块；The Gunk 示例理解屏幕 HOLD 提示后长按鼠标 | 训练后的游戏 Agent 证据，不证明桌面或输入法泛化 |
| WindowsWorld，Findings ACL 2026，[论文](https://aclanthology.org/2026.findings-acl.750/)、[Appendix B.3 / Fig.11–12](https://aclanthology.org/2026.findings-acl.750.pdf) | 记录中文 IME 与按键模拟冲突，以及切窗焦点失败造成内容贴回源窗口 | 是失败动机，未给出通用 IME 修复方案 |

定时长按与跨观察持键应分开：`hold(key, duration)` 结束即释放，可处理已知时长动作；
按住 Alt 看切换器、按住按钮观察蓄力、按住组合键读取瞬态界面，需要在观察结束后仍
保留按键状态。仅增加 duration 无法完整表达后一类需求。

建议的 Alt-Tab 语义示例（概念描述，当前 Mirage 尚无此工具）：

```text
建立有预算的输入会话
Alt↓ → Tab↓↑ → 截图并识别当前选中窗口
仍需向前：Tab↓↑ → 再截图
需向后：Shift↓ → Tab↓↑ → Shift↑ → 再截图
目标选中：Alt↑ → 新观察验证真正活动的窗口
```

模型负责决定继续/反向/提交，底层负责按键时序。截图可来自 OS 或外部采集，与持键
状态独立。持有期间允许只读观察，不允许另一个动作 owner 改写桌面输入。

工具说明和演示必须明确 tap、hold、release、当前 held keys、输入剩余预算和不确定
状态。增加能力后仍应单独评测模型的自主选路；接口存在不证明模型能熟练使用。

## 5. 输入设备来源

| 路径 | 能力和成本 | 一手资料 |
| --- | --- | --- |
| X11 XTEST / Win32 SendInput / Portal 输入 | 可提供独立按下/释放和多次移动；连续控制无需先做驱动 | 当前 Mirage 已采用这些路径；[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[KEYBDINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput) |
| Linux uinput | 用户态创建内核虚拟输入设备，支持相对运动与键状态；需要 `/dev/uinput` 访问权限 | [内核文档](https://www.kernel.org/doc/html/latest/input/uinput.html) |
| Wayland Portal + libei/EIS | 在授权会话中通过 compositor 输入栈分发；compositor 可以区分合成来源 | [RemoteDesktop Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)、[libei](https://libinput.pages.freedesktop.org/libei/) |
| Windows VHF | 虚拟 HID tree；官方目前要求 kernel-mode HID source driver，增加驱动开发和部署成本 | [Virtual HID Framework](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-) |
| 外接 USB HID | RP2040、USB gadget、PiKVM 等让目标机枚举实际连接的 USB 键鼠外设；需要硬件与校准 | [USB HID gadget](https://cdn.kernel.org/doc/html/latest/usb/gadget_hid.html)、[PiKVM USB](https://docs.pikvm.org/usb/) |

SendInput 受 UIPI 约束，低层钩子也有 injected 标记，不能宣称与物理事件不可区分。
[KBDLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct)
记录相应标志。虚拟 HID 与外置 HID 都不能据此承诺来源或行为不可识别。

直接贴合本需求的 **HIDAgent**（Bigham，2026-01-31，arXiv 预印本）用 RP2040、
串口桥和 HDMI 采集卡构成外接观察/输入工具包，报告五类原型。它说明了硬件路径的
可行性，也专门处理相对运动、系统加速度与坐标校准。然而其公开 `keypress` 是一次
down+up，评估以原型描述为主，不能直接充当 Mirage 跨观察持键的现成实现。
[论文全文](https://arxiv.org/html/2602.00492v1)、[作者仓库](https://github.com/CMUBigLab/hidagent)。

相对设备位移经系统指针加速度才变为屏幕位移；目标屏幕坐标不能直接当作 HID delta。
如果引入这一路径，应验证显示布局、缩放、指针加速度和回读误差，保持既有引用/权限
约束。HID 标准键盘报告也不直接携带任意中文文本，仍依赖键盘布局或 IME。

### 5.1 针对软件注入被过滤的兼容性目标

“输入调用被系统接受”和“目标应用接收到可用输入”必须分别验证。Windows 低层钩子
公开提供 injected 标记，应用可以据此实施过滤；Raw Input 则提供设备来源信息和
设备输入模型，不能假定所有目标只消费普通窗口键鼠消息。
[Raw Input 官方说明](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input)。

建议按失败机制选路，而不把所有无效果都归因于注入过滤：

| 已观察到的机制 | 候选处理与验证 |
| --- | --- |
| 应用过滤软件注入事件 | 对比现有路径、内核虚拟设备与外接 USB HID；逐应用记录是否接收、事件来源及业务效果 |
| 应用依赖设备输入或设备身份 | 测试设备层报告与目标所需的输入模型，不能由 VHF/uinput 名称直接推断兼容 |
| Windows UIPI/完整性级别阻止派发 | 单独诊断进程权限与目标上下文；不把它误记为应用过滤，也不以提高权限推断所有过滤已解决 |
| 焦点、布局、IME 或时序错误 | 修复对应状态与反馈；设备路径变化不能替代这些条件 |

对于纯软件交付，uinput/VHF 值得建立受控兼容性原型；对于明确需要从真实 USB 外设
进入目标机的场景，外接 HID 更贴合需求。两条路线共享平台中立的动作与按键所有权
契约，分别报告设备可用性、权限和目标应用的验证结果。Wayland 的 libei/EIS 是授权
的模拟输入协议，compositor 知道其模拟来源，不应被宣传为物理设备兼容性的等价物。

后端选择应允许显式配置与按应用保存已验证结果。已有输入可能部分生效时，禁止因
“画面暂时没变”就自动换后端重发，避免重复点击、输入或提交；先结算不确定结果并
重新观察。兼容性原型至少包含一个实际会拒绝现有注入的目标或明确规则的受控 fixture，
再以真实目标验证收益；本轮尚未获得具体目标应用，未证明任何后端能解决所有过滤。

## 6. 输入法状态与输入语义

应至少区分键盘布局、输入法引擎、激活状态、中英文转换状态、预编辑/候选状态及其
所属焦点。某些状态无法读取时明确 unknown，并通过任务相关的界面反馈判断。

- Fcitx5 的 `CurrentInputMethod`、`State`、`Activate/Deactivate`、`SetCurrentIM`
  提供框架状态操作；它们不证明能读取每个引擎的全部内部中英状态。
  [Fcitx5 DBus 官方接口](https://fcitx-im.org/wiki/DBus_Interface)。
- IBus 支持查询/设置 global engine，选中引擎与该引擎当前转换模式需分开。
  [IBusBus 官方 API](https://ibus.github.io/docs/ibus-1.5/IBusBus.html)。
- Windows TSF 分别定义 `KEYBOARD_OPENCLOSE` 和 `KEYBOARD_INPUTMODE_CONVERSION`；
  thread manager 状态不等于后台进程可任意读取前台应用的完整输入上下文。
  [TSF compartments](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments)。

建议区分三种用户意图：输入最终 Unicode 文本；发出真实键语义/快捷键；使用输入法
完成拼音预编辑、选择候选并提交。三者分别报告能力，禁止静默改剪贴板或永久更改
用户布局。要求模拟人使用 IME 的任务，应走第三条并验证候选和最终回显。

Shift、Ctrl+Space、Win+Space 的含义取决于配置与当前焦点。优先观测后选择动作，
每次切换后验证，避免未知初态下重复 toggle。切窗后重新确认焦点与输入状态；预编辑
期间 Enter 可能提交候选，后续是否提交表单必须另行确认。

## 7. Mirage 候选落地顺序

以下是调研建议，尚未取代 Accepted 的 DEC-054/100。实现前需冻结扩展决策、工作项
和验收矩阵，不直接把无所有者的裸 down/up 加入现有工具集。

1. **动作时序**：支持独立修饰键、可配置且有界的按下时长、组合键交叠、持续时间
   明确的平滑移动。区分持续 down 与自动重复按键；repeat 必须按后端实测语义验证。
2. **持键输入会话**：Task/Session owner 持有单写者 lease 和本任务的键/按钮集合；
   在预算内允许观察、追加动作、显式释放。异常、模型失联、预算耗尽、取消、接管、
   shutdown 都进入清理；释放失败沿既有 fence 语义处理。预算值经模型延迟与风险
   实测后冻结，不在报告中猜一个统一 TTL。
3. **模型与 IME**：能力投影、操作示例、当前 held 状态和 IME 的部分可观测状态；
   用实际任务验证选路、候选选择和切窗后的文本接收端。
4. **设备后端**：按维护者明确的应用兼容性需求，与动作契约并行评估 uinput/VHF 与
   外接 HID；不将其延后为视觉自然度优化。平台对象和设备协议留在 Platform Backend，
   不渗透到 Mira 行为契约。候选选型与应用验收见第 5.1 节。

持键会话改变了现有守卫假设：Alt-Tab 会进入系统切换器并有意改变焦点，不能简单沿用
“原前台窗口始终不变”。应显式约束允许的切换上下文，分别验证过程和最终接收端。
同样，释放 Alt 本身可能确认切换，输入清理也有业务副作用，回执需保留其不确定性。
用户物理按住的键不能被全局 key-up 当作本任务资源释放；同键重叠和来源归属需按
平台能力检测或保守拒绝，不能只靠一个集合宣称消除了竞争。

### 依赖承载核对

已读 pinned Mira `environment.hpp` 与环境 API：`InputEvent` 已有 `duration_ms` 和
`InputSequence`，说明定时动作无须重新发明通用 Agent 循环；但 Mirage 当前
`MiraEnvironmentBinding::execute` 明确拒绝输入，生产动作经 harness 工具接 Provider。
不能把 Android 的 timed gesture 契约直接宣传为 PC 跨观察按键会话已交付。

Mirador 公开 API 提供图像变换、变化检测、OCR/检测与跟踪，继续复用这些观察能力。
按键所有权、输入法与设备适配归 Mirage。尚未确认必须修改依赖的能力缺口，故本轮
不新增反馈编号；若实现阶段发现缺口，再按工程规范 9.4 给出可复现反馈。

并发方案已按 pinned kairo integration skill 的 by-requirement router 阅读 scheduling
card：有限推进用现有 `submit_auto`/`submit_cancellable` 并消费 future；延时/预算
看护使用公开 timer 能力并持有句柄，取消后阻止迟到动作。普通 timer 为 soft scheduling，
无法承诺物理时长硬上界；真正需要固定周期/低延迟时须另读对应 card 并验证平台条件。
不新增线程、私有队列或调度器，最终 teardown 仍由 Runtime 外部 owner 完成。

## 8. 验收建议与本轮证据边界

| 验收对象 | 建议证明的结果 |
| --- | --- |
| 连续鼠标 | 事件时间与位置轨迹、目标命中、hover 副作用、运动中取消、用户接管、多屏/DPI、目标移动后的停止或修正 |
| 时序键盘 | down/up 顺序、dwell/重叠、持续 down 与 repeat 的差异、组合快捷键、单独 Shift |
| 持键观察 | Alt-Tab 向前/反向选窗；按住显示瞬态界面并截图；释放后验证目标窗口/界面 |
| 生命周期 | 正常、任务异常、提交拒绝、执行中取消、超时、shutdown；另测模型失联、迟到结果、清理拒绝/失败与物理输入竞争 |
| IME | 切换输入源、Shift 切模式、拼音候选/翻页/提交、切窗焦点与模式变化、最终文本和表单提交分开验证 |
| 设备后端 | 实际枚举/事件来源、权限拒绝、断连/重连、按键释放、相对位移校准及目标应用兼容性 |
| 模型使用 | 在真实工具声明下自主完成上述任务，分别记录底层派发、局部验证与整任务成功，不能以脚本通过代替模型会用 |

2026-10-19：本轮完成公开来源检索、源码/公开 API/现行决策核对与调研文档；未执行
构建、sanitizer、模型调用、真实键鼠输入、IME 或 HID 设备实验。这些项目属于后续实现
验收，负责人为 Mirage 维护者，补跑条件为相应 X11/Wayland/Windows 会话、已配置
输入法、目标应用与所选设备/驱动可用。没有将研究或静态阅读计为平台验收通过。
