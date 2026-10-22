# Mirage 实施总计划

2026-10-16：[M7-23](m7-yolo-integration.md) 接入真实 YOLO 与 Mirador 修复，In Progress，DEC-097。

> 状态：Planned
> 版本：0.1
> 负责人：Mirage 维护者
> 依据：[《Mirage：Linux - Windows 桌面端设计方案》](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)（下称"设计文档"）
> 更新日期：2026-10-22（M9-06/07 共享采集与 M9-18 Mirador 关系/规划消费；既有里程碑状态保留）

## 当前状态

2026-10-22 后续：Mirador 再更新至 master `2a9f483`，消费其 M9-04 token/覆盖
采纳和 M9-05 时间状态/事件。独立背景宿主只对显式全帧刷新证据采纳，返回
覆盖失效、确认状态、焦点与有界事件；M9-05/07/18 继续 In Progress。规划仍在
当前识别之后，前置 ROI、无帧焦点查询与 native/模型供给保持开放。见
[消费契约](../design/continuous-visual-analysis.md)及[pin 审计](../supply-chain/mirador-continuous-perception-20261022.md)。

2026-10-22：按 DEC-091/109 推进 M9-06/07 的 opt-in 连续采集基础层：kairo worker/
timer、需求 epoch、两代像素背压与共享 Mirador Frame。M9-06 转 In Progress，
M9-07/11/12 保持开放；生产消费者、静态退避、真实平台与产品资源验收仍待交付。
契约见[基础层设计](../design/continuous-capture-infrastructure.md)，实际测试与限制见
[本轮证据](../compatibility/continuous-capture-infrastructure-20261022.md)。

2026-10-20：M7-25/26 点击子集继续交付：三条物理点击工具 4.0.0 默认本地自动平滑
接近，不接收模型 glide_ms；同一 lease 内到达重验后才点击释放。Portal 加同源 Cursor
元数据起点；范围与 native-release 验证见[记录](../compatibility/automatic-pointer-approach-20261020.md)。
其余键鼠时序、动作组和 Windows 等未交付项保持开放。

2026-10-20：维护者接受 [DEC-056 分层验证修订](../decisions/DEC-056-post-action-verification.md)：
普通键鼠不默认设逐动作业务验证硬门禁，执行完成后返回实际结果；按动作组、状态依赖
及任务边界观察验收。显式精确条件仅约束依赖步骤，执行/释放 uncertain 围栏保留。
DEC-059/068/109/110/111、主设计及安全边界同步；M7-25/26/31 与 M7-32～34 跟踪
回执、恢复、默认策略及平台验收。同日 v2 门控首增量已接入源码：普通输入无需业务
条件，显式 depends_on、执行/条件分离回执及无采集结算；新动作组、默认自然时序与
未验平台保持开放，不改变历史完成范围。

2026-10-20：按维护者要求补充 [DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)：
所有键鼠动作默认由本地自动执行自然时序，模型无需提供时间；普通点击完整移动、
按下并释放后才返回动作结果或进入后置验证。X11/Portal 已交付默认自然接近、
完整滚动/拖动/键盘自然时序及裸修饰键 tap，相关完整工具 4.0.0 无模型时间入口；
跨观察 held/IME/设备层与 M7-31 真实模型/平台/手感验收保持开放，
见[专项计划](m7-continuous-input-and-device-compatibility.md)。

2026-10-20：按维护者要求补充 DEC-091/109：高频轻量观测、变化触发结构更新、
低频按需 VLM；Mirador 管通用视觉状态，Mira 管任务注意/理解策略，Mirage 管
采集、调度、产品绑定与动作反馈。当前快照加有界事件日志，先做增量基线、稳定
身份与事件，再扩展语义解释；MiniCPM-o 4.5 仅参考视觉 token 压缩与时分复用机制，
不纳入模型后端选型。长期视觉记忆另行立项。
研究核对见[映射记录](../research/2026-10-20-event-driven-incremental-visual-perception.md)，
M9-05/07/08/11/12/13/18 验收范围同步，原有未完成状态与平台门禁保留。

端侧低负载补充：首先接通现有变化结果对 OCR/检测的实际门控，减少重复采集/
拷贝/编码；无消费者停止生产、静态退避、阶段按需选择。基础方案不新增常驻
编码器、本地 VLM 或推理 GPU 要求，时间分组不另起周期循环。M9-01/07/11/12
按低端参考设备预登记并验收全产品资源与交互门槛，见
[资源门禁](../design/m9-resource-and-progress-policy.md)，不能用 token 节省代替。

2026-10-20：[DEC-111](../decisions/DEC-111-target-text-value-verification.md) 已接受
跨平台目标文本验证方案：系统无障碍读值首选，受控复制可选，局部视觉回退；值条件
与图像观察分别声明，OCR 未命中不能等同派发失败。
[M7-32](m7-target-text-verification.md) 已进入 In Progress，交付平台中立读值 SPI
与一次只读精确比较器；Provider 默认不支持，尚无平台/工具接线。M7-33/34 继续
Planned；复制事务、默认选路及各平台验收尚未交付，不改变已有门禁与完成声明。

2026-10-19：[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)
（Proposed）与 [M7-25～31 专项计划](m7-continuous-input-and-device-compatibility.md)
提出连续键鼠动作、跨观察持键、IME 状态及设备层输入兼容性。虚拟设备/外接 HID
针对应用过滤软件输入的需求，与动作能力并行评估；M7-25 契约冻结稿（能力位、
会话键词表、数值预算、状态机、版本化回执）已交付 `desktop/environment` 契约层
并通过独立对账测试，待维护者评审。设备/平台选型与平台验收未改变。

M10-07/08 的工具预览编码与任务事件退出问题按
[维护记录](maintenance-ui-exit-20261019.md)修复，沿用 DEC-049/095 的契约与预算。

前端、后端本地 OCR 与 pet 性能按[性能维护计划](maintenance-performance-20261019.md)
推进，工作项 M6-47 / M7-24 / M8-13，依据 DEC-108。基于现有观察修复增量实施，
目标平台、sanitizer 与性能证据分别记录，不以缓存命中代表远程供应商生成加速。

M6-46 按 [DEC-104](../decisions/DEC-104-failed-turn-output-retention.md) 修复宿主失败
覆盖模型正文；沿用 Part 承载及失败状态，验证见
[实施记录](../compatibility/native-failed-output-retention-20261018.md)。

M8-06 按 [DEC-105](../decisions/DEC-105-wayland-pet-exclusion.md) 接入 GNOME Wayland
会话中 XWayland pet 的撤下/恢复与 Portal 源帧水位；并按
[DEC-106](../decisions/DEC-106-optional-pet-exclusion.md) 把避让改为能力降级，
不支持的 compositor 保留截图/OCR 并显式标注未排除。真实 compositor 门禁仍开放，
见[验证记录](../compatibility/wayland-pet-exclusion-20261018.md)。

Windows 安装后呈现问题按[维护计划](maintenance-windows-presentation-20261014.md)推进：
图形启动入口、通知承载重复图标、托盘菜单尺寸与间歇黑角分别跟踪，未取证项保持开放。

新增 [M10：基础 Agent 工具集](m10-basic-agent-tools.md)（Proposed）：按维护者要求，
调研本地 ZCode / deepseek-harness、pi 固定提交与 OpenCode 官方文档，规划
`exec` / `read` / `write` / `edit` 的 Provider 扩展、会话接入和 Linux/Windows 验收。
M10-01 源码调研已完成，其他工作项未实施；公开契约、预算与默认权限待
[DEC-095](../decisions/DEC-095-basic-agent-tools.md) 冻结，不改变当前产品授权与能力。
参见[调研与差距表](../research/2026-10-16-basic-agent-tools-survey.md)。

维护者确认新增 [M9：持续任务与本地语义感知闭环](m9-continuous-tasks-and-local-perception.md)
（In Progress）：正常主任务不按累计模型/工具次数终止，感知优先返回文本事实与目标
引用，本地完成变化检测、定位、等待和后条件核验，图片按需回退。依据为
[DEC-090](../decisions/DEC-090-continuous-task-execution.md)、
[DEC-091](../decisions/DEC-091-local-semantic-perception-and-action-loop.md)。
M9-02 已获授权合并 Mira 持续循环 [PR #99](https://github.com/Linductor-alkaid/mira/pull/99)，
a29374a 的 Linux 依赖消费验收已完成（独立升级 PR #97）；M9-03 产品接线后续推进。旧
DEC-088 实施计划转为历史记录，未完成请求级与真实长任务验收统一由 M9 承接。

DEC-091 同日补充连续帧性能/内存预算、按已提交基线增量更新和对象语义保留；
M9-13 负责 LLM 对 OCR/detector/闭合/track 对象的来源可追溯标定，M9-07/12
验证整条链路资源与慢消费者场景，未变化部分不重复识别/标定。

2026-10-19：[DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)
（Proposed）细化 M9 的 pet 可见共存（17）、任务注意策略/选择性通知（18）、目标
局部输入门控（19），与 Mirador #41 协同；采集、识别、发布分频，语义绑定由 Mirage
持有并复用 Mirador 视觉状态。新增工作项均未实施，现行 DEC-107/108 默认值与
未通过的平台门禁保留，不能以方案讨论宣称每帧整屏无遮挡或硬实时。

同日补充：M9-06/07/08 以模型理解、OCR/YOLO/Tracker 及动作正确性为质量下限，
采用低清巡检/概览、按需清晰 ROI 与有界轻量跟踪，降低 CPU/内存；
[调研记录](../research/2026-10-19-lightweight-tracking-and-adaptive-perception.md)核对微型无人机
与现有 Mirador 能力。实际档位/收益待 12 实测，不改变 pet 素材质量或依赖 pin。

最新 Native 会话的过程展示容量饥饿、输入法干扰和执行/失败期间用量缺失分别登记
M9-14/15/16（Proposed）；
桌宠状态请求延迟退出及重启入场回归归 M8-03/05/12。
[调查记录](../analysis/native-session-truncation-pet-ime-20261015.md) 区分已确认缺口、
隔离复现和现场待补证；尚未修改产品代码，不把既有门禁通过视为新问题已修复。

Native-release pet 缺席回归按 [DEC-092](../decisions/DEC-092-native-product-pet-delivery.md)
新增 M8-12（In Progress）：完整产品构建、缺件/运行版本诊断及干净启动验收；正式
安装、许可和偏好继续归 M8-08。首增量的基础状态层、构建/诊断及限定验证见
[实施记录](../compatibility/m9-incremental-scene-and-pet-build-20261015.md)；既有门禁保留。

维护者推进 [M8：mira_pet 常驻状态展示](m8-mira-pet.md)（In Progress）：提供由托盘
Runtime owner 持有的独立置顶状态窗口，主窗口退至后台时仍显示真实执行阶段；动画
按运行动作分别截取 intro/loop/outro，运行时只循环持续段。v5 九态源素材已登记，
looking/typing 与启动/退出已确认；once 不沿用容器无限循环。只读状态投影、IPC 与
纯播放时钟、异步资源加载与可选 Linux 独立窗口预览已接入；最小化/恢复和隐藏退出
已通过单屏 GNOME XWayland 现场复验与七态 20 圈接缝验收；位置恢复、无状态条/Dock、
首轮播放与失败后待机已按维护者要求落地。GPU/其他平台与目标避让仍待验收。进程、状态、
窗口与播放契约见 [DEC-084 提案](../decisions/DEC-084-mira-pet-process-and-animation.md)
和 [mira_pet 设计](../design/mira-pet.md)。现场结果仅适用于已记录的单屏 XWayland 条件；
M6/M7 状态与原开放门禁保持原记录。

M8-06 前新增 M8-11：思考气泡用一行连续横向滚动体现进行状态，回答气泡用一句
简短核心概括；方案与示意见 [气泡设计](../design/mira-pet-bubbles.md)、
[DEC-089](../decisions/DEC-089-mira-pet-bubbles.md)。Runtime/IPC/Linux 原生实现已交付并通过 fixture；真实思考入口已消费Mira PR#94合并提交22d9577，
MIRA-20261014-004在公开API范围解决；本机Release已更新，MiniMax公开算术在线10/10。
证据见 [实施记录](../compatibility/mira-pet-bubbles-runtime-20261014.md)；角色与气泡整组视觉占位纳入 M8-06，原编号和开放门禁保留。

M8-06 转 In Progress：按 [DEC-103](../decisions/DEC-103-pet-desktop-exclusion.md)
交付 Linux X11 桌面工具整窗排除，覆盖角色和气泡全部呈现；观察、输入与核验共用
租约，不放宽外来遮挡。同步修复隐藏退出检查依赖慢速状态同步的回归，隔离原生
进程 38/38；[实施证据](../compatibility/mira-pet-desktop-exclusion-20261018.md)
记录首增量及开放门禁；第二增量补齐直接 IPC 观察的排除、全程预算与新鲜视觉
代次检查，见[观察入口证据](../compatibility/mira-pet-observation-20261018.md)。
第三增量补齐空 shape 期间的解码/呈现暂停与最终气泡剩余时间扣减，复用现有
控制 timer，见[隐藏暂停记录](../compatibility/mira-pet-hidden-presentation-20261018.md)。
真实 compositor/其他平台仍待补跑。M8-01～05 的持续使用
稳定反馈作为现场进展保留，不据此替代尚未执行的平台、性能和完整输入闭环验收。

下一阶段新增 [M7：视觉工具与键鼠输入闭环](m7-visual-tools-and-input.md)
（In Progress，M7-01～06、08 Linux 首交付 Completed）。当前原生会话已接结构化观察、安全完整输入、标准 PNG/每请求图片、可选真实 OCR、
同源编号图与动作后核验，已补固定 OCR 文本条件，继续推进平台与宿主验收；依据为 [能力核对](../research/2026-10-09-visual-tools-and-input-capability-review.md)
与 [DEC-052 提案](../decisions/DEC-052-visual-tools-and-input.md)。M2/M3 的历史验收
不等于当前会话已接真实视觉后端；M6 既有状态与未验收项不变。

维护者已明确授权 Mira 升级至 698fc77，PR#86 的逐轮图片供给入口可供 Adapter 消费，
见[升级审计](../supply-chain/mira-image-supply-20261009.md)。M7-05 上游 API 阻塞解除，
M7-04 artifact 与 M7-05 source/supplier/识图接线已完成，三方言 fixture 和 MiniMax
合成截图在线 2/2，反馈本地 Resolved。范围及未验证项见
[图片验收](../compatibility/desktop-image-supply-20261009.md)。

M7-08 按 [DEC-056](../decisions/DEC-056-post-action-verification.md) 固定动作前后条件，
强制新观察、只读重验与宿主终态门禁。真实 X11 应用的激活、文本回显、提交落盘
三类任务通过，见[核验证据](../compatibility/post-action-verification-20261009.md)。
只证明本轮声明的局部条件；真实语义表单、在线业务规划、用户宿主与 Windows/Wayland
仍未完成。M7-06 已完成 Linux 首交付：正式 Mirador pin ae8cf87 包含三项 OCR 修复，
真实语料字符与位置 4/4，见[正确性复验](../compatibility/real-ocr-correctness-20261009.md)。
宿主 DPI/桌面、彩色多行场景与权重分发许可继续由后续门禁承接。
M7-07 按 [DEC-058](../decisions/DEC-058-scoped-visual-marks.md) 进行同源编号图、局部
裁剪及有 scope 的视觉点击首增量已接通，Debug/Release 59/59、opt-in PP-OCR 58/58，
见[验证记录](../compatibility/scoped-visual-marks-20261010.md)。M7-07 仍 In Progress，
第二增量按 [DEC-059](../decisions/DEC-059-visual-text-postconditions.md) 补固定 OCR 文本
后条件，实际 Xvfb Canvas 正/负例 5/5，见 [Canvas 验收](../compatibility/visual-text-canvas-20261010.md)。
第三增量按 [DEC-060](../decisions/DEC-060-geometric-visual-marks.md)接显式闭合几何编号，
受控无文字按钮两目标与不重绘负例已验，见
[几何编号验收](../compatibility/geometric-visual-marks-20261010.md)。第四增量按
[DEC-061](../decisions/DEC-061-geometric-template-resources.md)装配本地模板并匹配几何
候选；受控双模板、位置交换与不重绘负例见
[模板验收](../compatibility/geometric-template-resources-20261010.md)。第五增量用实际
MiniMax-M3 从无身份提示的编号图选号，位置交换与不重绘门禁 3/3，见
[在线验收](../compatibility/online-visual-canvas-20261010.md)。第六增量按
[DEC-062](../decisions/DEC-062-visual-capture-scope.md)把视觉供给范围化到 scope
窗口并交付宿主验收入口（实际摆放几何、遮挡守卫、焦点宽限），离线回归全绿；
XWayland 预跑守卫按实际摆放生效，mutter 拒绝为合成点击移交焦点、harness
fail-closed，正式宿主在线取证待操作者配合，见
[宿主取证](../compatibility/user-host-visual-canvas-20261010.md)。其他在线模型、
无轮廓图标、跨主题/尺度等仍保留门禁，高分辨率局部预采集由第七增量承接。

第七增量按 [DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)接可选局部
预采集、同范围像素重验与 owner regional OCR，默认整屏路径和预算保持。
正常/ASAN 实际权重各 12/12，默认路径回归通过；在线补证因 DNS 失败、完整权重
TSAN 期限门禁仍开放，见[4K 小窗口验收](../compatibility/regional-visual-capture-20261010.md)；
完整 4K/DPI、跨屏、用户宿主与原生 Wayland 门禁仍独立验收。第八增量已确认
人工取焦并修复 WM 映射时 fixture 全白问题；切换真实 Xorg 后受控离线三例与
权限/焦点门禁分别 3/3、2/2。实际 MiniMax 宿主闭环 1/2、选号 2/2，第二例因
五秒引用过期拒绝输入，第三例未运行；在线与完整权重 TSAN 门禁继续保留，见
[宿主取证](../compatibility/user-host-visual-canvas-20261010.md)。

第九增量复用现有 Mira Disabled/Adaptive 配置，验收入口默认值保持；输入前
显式重观察与失败不恢复边界已验，五配置定向各 3/3、普通/ASAN 实际权重各
12/12。MiniMax none 补跑仅不重绘负例通过，两个选号后的请求在五秒内，但
网络间歇失败阻止正例闭环；私有三例与宿主 none 门禁仍开放，见
[模型视觉耗时取证](../compatibility/model-visual-latency-20261010.md)。

维护者调整网络后新增一次私有 none 补证：验收 2/3、选号 3/3，本批无 DNS
错误，右目标与不重绘负例闭合；左目标标注完成到点击请求 7408 ms，五秒期限
拒绝且零输入。真实 Xorg none 前置仍未通过，M7-07 保持 In Progress；下一步
先补公开模型请求生命周期耗时取证，再据证据改进请求路径，原期限与失败门禁
不变。历史失败与本批[独立回执](../compatibility/data/network-adjusted-online-visual-canvas-20261010.json)
分别保留，无代码/依赖变更或新回归声明。

后续顺序经维护者要求重新评估：认可受控 X11 视觉/输入/验证基础阶段，下一
主线推进 M7-09 滚动/完整拖拽；M7-07 总状态和未完成增强门禁保留，不要求所有
在线模型逐步满足五秒回合时间。2026-10-10 时 M7-11 列 Proposed，核对本地知识目标解析
与已承载 Mira workflow 的组合：保存语义/步骤，执行时重新观察取得新引用，
通过本地已知步骤减少模型往返。当轮未实现该组合，见
[阶段定位与推进路线](../research/2026-10-10-local-grounding-and-m7-sequencing.md)。

2026-10-11 维护者明确授权 M7-11 后，DEC-067 语义首增量已通过实际产品 workflow +
GTK/AT-SPI2：本地知识新解析、权限门禁、一次激活和新证据，不需模型往返。Debug/Release
全量各 66/66，sanitizer 定向各 6/6，实际产品两构建各 20/20。M7-11 仍 In Progress，
当轮会话选择及跨重启自动恢复保留后续；OCR/模板第二增量已准入，见
[验收与边界](../compatibility/local-semantic-workflow-20261011.md)。

2026-10-11 的 DEC-069 第二增量接入本地 OCR/模板 workflow，同一知识在窗口移动、
模板换位后新解析，唯一页面/目标门禁与新后条件由已有视觉动作闭环承载，无模型请求。
实际产品同时修复周期维护重叠占满四 worker 导致观察/IPC 等待的问题；未改 pinned
依赖、提高动作期限或线程数。范围和验证见
[第二增量验收](../compatibility/local-visual-workflow-20261011.md)。M7-11 保持 In Progress，
下一步为原生会话资产选择/恢复边界及可持久恢复的资产装配。

M7-09 首增量按 [DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)接通
Linux X11 同源编号滚动和完整左键拖拽，复用既有 Executor、动作 lease 与固定
后条件门禁。双引用在同一 registry 锁内解析，限制轮步/距离/时长，运行中取消、
接管、焦点变化和超时后仍释放自有按钮；不确定结果禁止重发。实际 XTest 与
Mira fixture 的证据、目标平台补跑条件见
[验收记录](../compatibility/bounded-scroll-and-drag-20261010.md)。M7-09 全范围
保持 In Progress，Windows/原生 Wayland、用户宿主和任意 DPI/跨屏仍待各自验收。

第二增量通过系统 GTK3 原生滚动/分隔条的实际效果与失败门禁，工具/核验组合
六场景已验；X11 拖拽使用有界 8 ms 平台输入步进、清理仍零延迟。原生列表
XDND 暴露拖放图层与归属守卫兼容性问题，拒绝/释放负例已验但正例尚未交付；
未保留形状尝试或按 PID/类型绕过。Debug/Release 62/62、sanitizer 定向各
9/9，详见[应用效果验收](../compatibility/gtk-pointer-effects-20261010.md)。

第三增量按 [DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md)接入可选
系统 XRes：服务端确认辅助图层与目标是同连接，且图层下方仍命中原目标。
GTK 原生列表重排与新后条件正例已验；中途取消/焦点变化、图标上下外来遮挡、
重父窗口与内部遮挡、容量拒绝以及缺扩展严格回退见
[原生拖放证据](../compatibility/x11-native-dnd-20261010.md)。M7-09 仍 In Progress。

第四增量在用户实际 Xorg 3200×2000、GTK 整数 2× 下，经人工取焦/松键取得
滚动、列表重排、禁止重排与首段拖动后取消四类独立证据，原生状态、后条件及
按钮/owner 释放分别核对。不是连续同批 4/4；间歇像素变化、较晚取消前遮挡
拒绝仍开放，失败历史见[宿主验收](../compatibility/user-host-gtk-pointer-20261011.md)。
新增私有 2× 与管道交接回归，修正旧超时 fixture 的空请求访问；不放宽产品
期限或输入守卫。M7-09 总状态不变，其他平台与任意应用兼容性继续另验。

第五增量交付测试边界的有界拒绝诊断、已知像素变化负例与 2× 管道交接回归；
宿主另取得一次 50 逻辑像素后的取消/释放证据。Debug/Release 各 65/65、两种
sanitizer 串行定向各 12/12，缺 XRes 保持严格回退。诊断增加查询会改变时序，
未把单次通过解释为间歇根因修复，详见
[诊断验收](../compatibility/x11-pointer-diagnostics-20261011.md)。M7-09 仍 In Progress。

2026-10-11 / M7-12 Linux 产品启用预览已闭合：产品 Provider 探测、原生桌面工具设置、
OCR 清单持久化/启动覆盖及坏资源恢复已接通；实际原生 MiniMax OCR/编号图任务完成，
完整产品检查 23/23、OCR OFF 恢复 11/11。独立 native-vision-release 已构建，普通
native 构建仍默认关闭 OCR。回归、失败历史与使用入口见
[产品启用验收](../compatibility/product-desktop-vision-20261011.md)。M7-07/09 全范围、
其他平台/任意应用可靠性、完整权重 TSAN 与 M7-11 组合路线保留各自状态，后续围绕
具体应用任务效果与停止/接管取证推进。

2026-10-11 / M7-13、14：Linux 原生产品默认固定封装已验 Mirador / ncnn / PP-OCRv5，
已修复用户两次会话暴露的工具预算、无副作用拒绝恢复与 X11 标题编码问题；
默认启动、安装布局、坏包与关闭恢复已在私有实际产品中验收。
该增量由 DEC-068 承载，见[验收记录](../compatibility/bundled-vision-and-tool-recovery-20261011.md)。
既有明确视觉设置仍优先；原始两次在线业务任务未重跑，完整 M7 各开放门禁不改变。

2026-10-11 / M7-14 第二增量：最新会话默认截图/OCR 已成功，参数拒绝的完整 JSON
在 Mira 512 字节错误摘要处截断，误封锁纠正；已修复有界回执与旧失败卡片收尾判定，
补齐字段/编号阶段诊断。Release 68/68、Debug 62/62、两组 sanitizer 定向各 9/9，
真实 ToolExecutor/X11 恢复及实际构建/DEB 产品各 37/37 已验，见
[DEC-070 验收](../compatibility/desktop-rejection-recovery-20261011.md)。M7-14 重新打开
In Progress，保留首轮证据；原用户在线任务、提交评审待闭合，M7-11 不因此关闭。

同日收尾审计补齐旧测试默认持久化目录隔离。最终 Release 70/70、Debug 64/64、
sanitizer 定向各 11/11，均包括 2 个隔离 fixture；隔离后的回归不再改变用户历史文件
摘要/mtime，测试目录正常回收。早期回归的历史元数据变更保留在验收，不覆盖或删除。

项目骨架与依赖接线已初始化（pinned `mira` / `mirador`，见
[DEC-001](../decisions/DEC-001-dependency-pinning.md)）；M1 里程碑计划已建立，见
[M1：Mira Host 与基础 Runtime](m1-mira-host.md)。M1 进行中：`M1-01` 项目初始化、
`M1-02` Mira Host 生命周期（状态集冻结见
[DEC-004](../decisions/DEC-004-mira-host-status-set.md)）、`M1-03` Desktop Environment
绑定适配器（绑定与参考 Provider 边界见
[DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)）、
`M1-04` Runtime Service 与 Local IPC（IPC 机制定案见
[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)）、`M1-05`
Filesystem / Process Provider 收紧（范围/预算/取消语义见
[DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)）、`M1-06`
Desktop Permission 框架雏形（Capability 判定与确认挂点见
[DEC-010](../decisions/DEC-010-m1-permission-framework.md)）与 `M1-07` 持久化骨架
（本地配置与 Runtime Recovery State，见
[DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)）已完成；
2026-09-16 里程碑退出条件复核通过（5/5，验证记录见
[M1：Mira Host 与基础 Runtime](m1-mira-host.md)），M1 Completed。M2 里程碑计划已
建立，见 [M2：Desktop Environment 核心 Provider 与 Linux
Backend](m2-desktop-environment.md)；`M2-01`（核心 Provider 契约与
DesktopObservation schema v1.0，
[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）、`M2-02`（X11
骨架闭环，[DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)）、
`M2-03`（AT-SPI2 AccessibilityProvider）、`M2-04`（Input / Clipboard 完整 +
Permission 词表扩展）、`M2-05`（Application / Notification Provider 与
ScreenProvider 完整）与 `M2-06`（Observation 组装与 runtime 接线：
DesktopObservation 按需组装器、绑定 observe 面如实上报与 Semantic Snapshot
进入 Agent 观察面、DEC-002 Mbed TLS 默认值复核冻结）已完成；2026-09-20 M2 里程碑
退出条件复核通过（6/6，验证记录见
[M2：Desktop Environment 核心 Provider 与 Linux
Backend](m2-desktop-environment.md)），M2 Completed。M3（Mirador 视觉集成）里程碑
计划已建立，见 [M3：Mirador 视觉集成](m3-mirador-integration.md)；pinned
mirador 已升级至 v0.3.0（`6fa92ec`，几何区域提议契约经上游 DEC-018 阶段 1
转正），审计记录见[依赖升级审计](../supply-chain/dependency-upgrade-audit.md)；
`M3-01`（视觉集成契约 DEC-016）、`M3-02`（集成层适配器）、`M3-03`（Visual
Observation 与解析闭环）、`M3-04`（Visual Cache 与几何提议）、`M3-05`
（runtime 接线与端到端闭环）与 `M3-06`（DEC-006 壳选型 PoC 与冻结：CEF 冻结，
取证见[壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)）已完成；
2026-09-21 M3 里程碑退出条件复核通过（8/8，验证记录见
[M3：Mirador 视觉集成](m3-mirador-integration.md)），M3 Completed。
UI 并行轨道
[M1.5](m1.5-ui-parallel-track.md) 已完成（wire schema 事实源
[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、事件订阅、
dev bridge、harness 前端壳）。M4（Windows Backend）里程碑计划已建立，见
[M4：Windows Backend](m4-windows-backend.md)；`M4-01`（Windows Backend 骨架闭环：
Win32 窗口 / 采集 / 输入，工具链双门禁决策见
[DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)）、
`M4-02`（AccessibilityProvider：UIA 语义树 → SemanticSnapshot 与
ElementTarget 解析 / 语义动作）、`M4-03`（ClipboardProvider（Win32）与
ProcessProvider 的 CreateProcess 有界承载）、`M4-04`（ApplicationProvider：
开始菜单快捷方式发现、启动 / 运行态 / TERM 等价协作式终止）与 `M4-05`
（NotificationProvider：承载决策
[DEC-018](../decisions/DEC-018-windows-notification-carrier.md)——
`Shell_NotifyIcon` 气球/横幅 + open() 探测 fail closed 降级，toast 留 M5
重议）、`M4-06`（产品进程 Windows 化：命名管道 IPC、Windows 持久化、
service / CLI 进程形态、全树 MSVC 构建）与 `M4-07`（Windows 端到端闭环：
observe → action → observe 经真实 Win32 / UIA 前端与
`MiraEnvironmentBinding` 绑定取证；里程碑退出条件逐项复核）已完成；
M4 里程碑 Completed，退出复核通过（全树 MinGW 交叉复验随台账
`MIRA-20260922-001` 缺口关闭挂账）。M5（Desktop Product）里程碑计划已建立，
见 [M5：Desktop Product](m5-desktop-product.md)，状态 In Progress；`M5-01`
（前端工具链定案与 CEF 二进制锁定：npm / Vite / React 19 / vitest 复核确认、
ESLint 新定选、`dependencies.lock.json` schema v2 工件 pin 与 npm 树哈希门禁、
沙箱与 GPU 正式方案，DEC-006 2026-09-26 修订）、`M5-02`（CEF 产品壳骨架与
壳内 IPC 传输路径：锁定工件 configure 期消费门禁、`apps/desktop` bootstrap
进程形态壳骨架、`runtime/ipc` SessionClient、壳内 UI 对真实 `mirage-service`
的 hello / 订阅 / 任务往返验收，DEC-019）、`M5-03`（权限异步确认面：DEC-020
替换 M1 同步确认挂点——`AsyncConfirmationHub` 有界等待 + 超时/容量/取消
fail closed，协议 v1 附加扩展 `permission.request` 事件与
`permission.respond` / `permission.list` 请求面，golden vectors 双端门禁
meta.version 3，任务驱动器取消探测接线）与 `M5-04`（会话与消息契约面：
DEC-021——`session.list` / `session.open` / `session.history` 请求面与
`session.updated` / `session.message` / `session.turn` / `session.output`
事件集，`task.submit` 会话绑定，pinned 会话投影经 `SessionJournal` 承载，
`MiraHost` 会话面开启 DEC-008 迁移路径第一步，golden vectors 双端门禁
meta.version 4）已完成。2026-09-26 双依赖前滚（mira `1348515`、mirador
`fb0dc3f`，[升级审计](../supply-chain/dependency-upgrade-audit.md)）交付上
游工具引用 / Skill 发布 / MCP 接纳与 mirador 目标跟踪 Experimental 等能力，
消费路由见 [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)：
`M5-05` 绑定升级后的 pinned 工作流 / 工具契约面，Tools / MCP 产品面与
mirador 跟踪消费分别挂账 POST-05 / POST-04。`M5-05` 第一轮（工作流契约面，
[DEC-023](../decisions/DEC-023-workflow-contract-face.md)）已完成：
`workflow.list` / `workflow.save` / `workflow.publish` / `workflow.delete` /
`workflow.atom.catalog` / `workflow.runs` / `workflow.run` / `workflow.cancel`
请求面与 `workflow.run_updated` 事件（golden vectors 双端门禁 meta.version 5），
pinned `WorkflowRuntime` 经 `MiraHost` 工作流承载面服务化托管，运行监控事件经
`WorkflowEventBridge` 从 pinned 事件词表转译；编辑器完整版接真实面与桌面原子
工具注册为该工作项第二轮（均已完成）。`M5-06` 第一轮（会话页接真实面，
[DEC-025](../decisions/DEC-025-session-page-productization.md)）已完成：
UI 侧消费 `M5-04` 的 `session.*` 契约面（wire 零变更），会话列表 / 消息流 /
观察台全部接契约路径，模拟域退出会话页，对话模式在模型循环接入前如实降级；
第二轮观察面协议扩展批次（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）
已完成：`desktop.observe` 按需观察请求面（语义快照投影 / 视觉状态 /
`visual_snapshot_ref` 进 UI 观察面，M3 非目标兑现）与 `workflow.get` 定义读
取面（编辑器跨会话编辑）随 golden vectors v5 → v6 上线，步级运行事件与会话
管理面评估后挂账（PR #58 CI 全绿取证已补录里程碑计划）；第三增量会话管理面
`session.close`（DEC-026 挂账②兑现）已完成：pinned `close_session` 承载 +
服务注册表条目移除 + 主会话 `invalid_state` 守卫 + 会话页删除入口，
`session.updated` 增补关闭发布点，golden vectors v6 → v7；第四增量对话模式
真实化（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)，DEC-025
挂账③兑现，DEC-008 迁移路径第二步纯对话形态）已完成：模型层装配（pinned
ModelProfile/Router/Provider/SocketTransport/Gateway + SecretRef 环境变量
解析 + TLS 通道按需挂接）与 `session.chat` 对话面（异步 turn +
session.chat_updated 生命周期 + session.chat.history 快照），Composer 双模
式信息架构完整兑现，golden vectors v7 → v8——`M5-06` 工作项收口；设置-模
型类目（Profile 管理面、凭据承载升级）属 M5-08 联动范围。`M5-07` 批准中
心与权限管理产品化（[DEC-028](../decisions/DEC-028-permission-policy-face.md)）
已完成：批准中心（Overlays / WallDisplay）接 DEC-020 异步确认面
（permission.list/respond/permission.request），设置页权限矩阵替换为
DEC-010 十一能力真实矩阵（policy.get 事实源 / policy.set 全量应用立即生
效 + 合并文档持久化），hello `policy` 能力位，golden vectors v8 → v9，
DEC-011 默认拾取与写回翻转兑现，默认策略收紧留观 M5-08。`M5-08` 设置全
量与产品化规模复核已完成：LocalSettings 扩展 model / runtime 两块（设置
-模型类目配置输入与队列容量、连接规模可调），会话历史与对话线程跨重启
持久化落地（session-state.json，DEC-021 挂账①兑现），POSIX 端点对端凭
据校验（SO_PEERCRED）与 M1 遗留守护纪律修复（BUG-20260916-001）落地，
会话重命名产品别名（DEC-026 挂账⑤）落地。`M5-09` Desktop Overlay 已完成
（[DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md) 承载机制随实现
定案并登记）：Overlay 由 `mirage-service` 进程承载（与 GUI 生命周期解耦）、
平台承载落 Platform Backend 私有前端（Windows 双分层窗口：置顶 / 每像素
alpha / 跨进程点击穿透 + 确认入口交互窗；Linux X11 shape 覆盖层先例，无合
成器依赖，Wayland 原生会话如实降级为无载体）；UI 面兑现目标高亮、即将执行
操作提示、确认入口与 Observation 调试观察面（`--overlay on|debug`，默认
off）；与任务执行流的事件联动经服务内部事件织物（事件订阅横幅 +
`AtomOverlayFeed` 动作缝 + 确认面镜像，wire 零变更，DEC-020 应答路径扩展
为 IPC 客户端或 Overlay 确认入口 first-response-wins）。`M5-10` Tray 进程
形态已完成（[DEC-030](../decisions/DEC-030-tray-carrier-and-task-pause-face.md)
承载机制与任务暂停/恢复面随实现定案并登记）：`apps/tray` 单 Executor owner
经 Local IPC 订阅运行状态（EXEC-02），任务暂停/恢复经 `task.pause` /
`task.resume` 附加扩展（v10；pinned pause 家族投影 + 驱动操作边界驻留），
快速进入 Mirage 经兄弟壳二进制发现拉起；Windows 承载沿用 DEC-018 通知区面
的产品化菜单扩展，Linux 以 StatusNotifierItem + 最小 dbusmenu（纯 gio）并
在无指示器宿主会话如实降级为无载体。`M5-11` 打包与更新通道已完成
（[DEC-031](../decisions/DEC-031-windows-installer-generator.md) NSIS
定案、DEC-018 toast 重议不重开）：Linux .deb（CPack DEB）+ GPG 签名 apt
仓库脚本（fail closed）+ Windows NSIS 安装器 + Authenticode 签名脚本
（证书注入 fail closed）+ 更新清单 ed25519 工具链与五步演练。`M5-12`
Windows 应用内更新器已完成
（[DEC-032](../decisions/DEC-032-update-client-fail-closed.md)）：清单
严格解析 + ed25519 验签（libcrypto EVP，OpenSSL 缺席构建整体 fail
closed）+ 原子切换与逆序回滚 + HTTP fetch（信任锚为清单签名与
Authenticode 双层，非 TLS）+ `mirage update check/apply` CLI；差分维持
全量优先不承诺；更新器零私钥（keyring 隔离既定）。`M5-11` 打包与更新通道已完成
（[DEC-031](../decisions/DEC-031-windows-installer-generator.md) 安装器
生成器定案 NSIS、DEC-018 toast 重议结论不重开）：Linux `.deb` 由 CPack
DEB 产出（四产品进程 + postinst/prerm 尽力而为脚本），apt 仓库 GPG
签名/验签脚本 fail closed（签名 key 发布机隔离，本机临时演练 key 演练
全过）；Windows NSIS 安装器脚本 + Authenticode 签名脚本（证书注入 fail
closed，发布机取证补跑）；更新清单 ed25519 签名生成与 fail-closed 验签
脚本（本地五步演练：签验/篡改拒/恢复/回滚收敛）。

## 交付边界

- [ ] `SCOPE-01` Mira Host：在 Mirage Runtime 中承载 Mira 实例，绑定 Desktop
      Environment，向产品层转发 Agent 事件（设计文档第 11 节）。
- [ ] `SCOPE-02` Desktop Environment：Application / Window / Accessibility / Screen /
      Input / Clipboard / Filesystem / Process / Notification 的统一 Provider 接口
      （设计文档第 5 节）。
- [ ] `SCOPE-03` Desktop Observation 与 Semantic Snapshot：结构化桌面状态、Element
      Reference 与按需感知（设计文档第 6、7 节）。
- [ ] `SCOPE-04` Mirador 视觉集成：截图采集对接 Mirador OCR / 检测 / 几何 / Visual
      Cache，形成 Visual Reference（设计文档第 8 节）。
- [ ] `SCOPE-05` Windows Backend：UI Automation + Win32 + 系统截图输入的完整平台实现，
      与 Linux 后端共享同一 Desktop Environment API（设计文档第 10 节）。
- [ ] `SCOPE-06` Desktop Product：Agent Workspace、Task / Workflow / Execution 视图、
      Desktop Overlay、权限管理、后台 Runtime Service 与 CLI（设计文档第 12-15 节）。
- [ ] `SCOPE-07` Linux Backend：AT-SPI2 + X11 + Wayland + XDG Desktop Portal
      （设计文档第 10 节）。
- [ ] `SCOPE-08` mira_pet：独立受管进程、真实状态投影、透明置顶与分段循环动画；
      先交付 Linux X11 v5 九态预览，其他平台独立验收（M8）。
- [ ] `SCOPE-09` 基础 Agent 工具：exec/read/write/edit 与桌面工具共存，具备文件
      分页、修改前观察/冲突检查、进程取消及有界输出；M10 Proposed，待 DEC-095 冻结。

明确不包含（由 pinned Mira 提供，Mirage 仅适配）：

- Agent Harness 内核：Observe -> Reason -> Plan -> Act -> Verify 循环、Context、
  Memory、Tool/MCP 框架、Workflow / Subagent 执行模型。
- Agent Memory、Workflow 数据模型的持久化（Mirage 只持久化自身产品状态，设计文档
  第 16 节）。
- 模型推理与视觉模型运行时（由 mirador 的 backend 契约和其 integrations 承载）。

## 不可破坏的架构约束

- [ ] `RULE-01` 依赖方向固定为 `apps/ui -> runtime -> desktop -> platform`；
      `integration` 是 pinned 依赖的唯一边界层，其余层不得直接包含 mira / mirador /
      executor 头文件（公共接口不暴露第三方类型）。
- [ ] `RULE-02` 平台细节（UIA、AT-SPI2、X11、Wayland、Win32、Portal）只存在于
      `platform/`，Desktop Environment 接口与 Observation / Action 语义在两个平台上
      保持一致。
- [ ] `RULE-03` 所有并发任务、定时、取消和生命周期管理必须经 Executor（随 pinned mira
      引入）；自研代码禁止 `std::thread`、`std::async`、自建线程池与 detached worker。
- [ ] `RULE-04` Task / Session / Desktop Action 必须有稳定 ID、取消上下文和生命周期
      所有者；终态幂等，迟到结果不得复活已取消任务。
- [ ] `RULE-05` 键鼠输入、进程执行、文件写入、剪贴板等有真实副作用的动作必须经过
      Desktop Permission 判定（含用户确认路径），并与 Agent Trace 关联（设计文档
      第 15 节）。
- [ ] `RULE-06` `third_party/` 只以 pinned submodule 指针变更，必须与
      `dependencies.lock.json` 同步；configure 阶段校验失败即构建失败。
- [ ] `RULE-07` 队列、缓存和并发 operation 必须有容量或预算上限；背压、拒绝、超时和
      取消必须转化为明确结果与事件。
- [ ] `RULE-08` 声明性能、实时性或跨平台支持前必须留下符合工程规范第 7 节的验证证据。

## Executor 并发边界

Executor 由 pinned `third_party/mira/third_party/executor` 提供，能力路由见根
`AGENTS.md` 与工程规范第 9.3 节。

- [ ] `EXEC-01` Runtime Service 是每进程唯一的 Executor owner：初始化、任务准入、
      排空与 `shutdown(true)` 顺序由其负责；产品由托盘内嵌 Runtime，Tray 复用该 owner；
      GUI/CLI 经 IPC 调用。原生 UI 的独立 IPC Executor 边界按 DEC-033/045 管理。
- [ ] `EXEC-02` Mira Host 的任务提交、Agent 事件流转发使用 Executor 公开能力与
      `executor::comm` 组件；事件到 UI 的分发映射为 `Topic` / `LatestMailbox`，不得
      自建广播队列。
- [ ] `EXEC-03` Platform Backend 的平台事件循环（UIA/AT-SPI2/Win32 消息循环）按
      Executor 外部事件循环互操作边界接入；线程亲和的平台调用封装在 Backend 内。
- [ ] `EXEC-04` 桌面输入注入、截图、OCR 请求等阻塞或耗时操作使用 blocking worker /
      timer 能力承载；输入注入的取消路径必须可解除阻塞。
- [ ] `EXEC-05` 依赖（mira / mirador，含其内嵌 executor）能力缺口按工程规范第 9.4 节
      登记 `docs/dependency_feedback/ledger.md` 并在实现中引用编号。
- [ ] `EXEC-06` mira_pet 复用托盘 Runtime 子进程 owner 与 IPC 事件织物；桌宠进程
      唯一 Executor owner 管理 IPC worker、有限资源加载和 timer 唤醒，持有并消费
      handle/future，UI 主线程持有播放/GPU 状态；取消与 shutdown 不等待动画收尾。
- [ ] `EXEC-07` M10 基础工具复用 Runtime owner；有限准备/结果消费使用 Executor，
      长文件/管道等待使用可唤醒 blocking worker，容量拒绝、提交点取消与回收可观察；
      不自建文件等待队列、后台 job 调度器或平行监控系统（Proposed）。

## 里程碑索引

| 里程碑 | 内容 | 建议发布点 | 状态 | 依赖 |
| --- | --- | --- | --- | --- |
| [M1](m1-mira-host.md) | Mira Host、Runtime Service + IPC、Filesystem/Process Provider、CLI | `release-alpha` | Completed | - |
| [M1.5](m1.5-ui-parallel-track.md) | UI 并行轨道：wire schema 事实源、IPC 事件订阅、dev bridge、前端（浏览器形态，按 DEC-013 harness 优先统一壳组织：会话/工作流/设置 + 统一壳骨架） | -（随开发线交付，产品化 UI 属 M5） | Completed | M1 |
| [M2](m2-desktop-environment.md) | Desktop Environment 核心 Provider + Linux Backend、Semantic Snapshot、Element Reference | `release-beta` | Completed | M1 |
| [M3](m3-mirador-integration.md) | Mirador 集成：OCR / 检测 / 几何 / Visual Cache、Visual Reference | `release-gamma` | Completed | M2 |
| [M4](m4-windows-backend.md) | Windows Backend（UIA / Win32 / Capture / Input） | `release-delta` | Completed | M2 |
| [M5](m5-desktop-product.md) | Desktop Product：Workspace、Workflow UI、Execution Trace、Overlay、权限 | `release-epsilon` | In Progress | M3、M4 |
| [M6](m6-native-frontend.md) | 原生前端与桌面进程形态重构 | 暂不新增标签 | In Progress | 既有 M5 服务/IPC |
| [M7](m7-visual-tools-and-input.md) | 会话桌面工具、真实视觉与完整键鼠输入 | 桌面工具预览版（标签未定） | In Progress | M2/M3、M6 Runtime owner |
| [M8](m8-mira-pet.md) | mira_pet 独立置顶状态窗口、逐动作循环片段与真实执行投影 | Linux X11 桌宠预览版（标签未定） | In Progress | M1 IPC、M6 Runtime owner、M7 动作/观察执行缝 |
| [M9](m9-continuous-tasks-and-local-perception.md) | 持续任务、请求级 compact、文本观察、变化/跟踪与本地动作/等待闭环 | Linux 持续任务与文本观察预览（标签未定） | In Progress | M1/M3/M6/M7 已交付能力；M9-02 Linux pin 消费已完成，产品接线另列 M9-03 |
| [M10](m10-basic-agent-tools.md) | exec/read/write/edit、文件版本/提交、cwd/输出、会话与产品接线 | Linux 基础工具预览 → Windows 四工具预览（标签未定） | Proposed | M1/M5/M6/M7 已交付能力；不等待 M8/M9 整体关闭 |

拆分与合并顺序：先契约后实现（Desktop Environment 接口先于任何 Backend）、先骨架后
功能（每个 Backend 先以最小动作打通 Observation -> Action -> Observation 闭环）、
先 SPI/假实现后真实依赖（视觉后端先用 fake/identity backend 验证集成层）。

## 尚未冻结的决策（暂定默认值）

| 编号 | 主题 | 暂定默认值 | 负责人 | 最迟冻结 |
| --- | --- | --- | --- | --- |
| DEC-110 | 连续输入与设备兼容性 | Proposed：M7-25 已有契约冻结稿；2026-10-20 补全量键鼠默认本地自然时序、无模型时间参数与完整执行后返回/验证要求，契约及兼容迁移待修订；M7-26 已交付 X11/Portal 完整动作默认自然时序与裸修饰键 tap；持键/IME/设备及真实平台验收未完成 | Mirage 维护者 | M7-25 修订/评审；设备原型前 M7-29/30，任务阈值执行前 M7-31 |
| DEC-095 | 基础 Agent 工具集 | Proposed：四工具/alias、观察与提交点、预算、权限迁移和平台边界见提案 | Mirage 维护者 | M10-02 |
| DEC-090 | 持续任务与有界资源 | 方向 Accepted；无进展判定、公开循环 API/准入及存量在实施前冻结 | Mira/Mirage 维护者 | M9-01～04 |
| DEC-091 | 文本观察与本地闭环 | 方向 Accepted；增量状态/事件驱动理解/按需 VLM，观察与事件契约、连续帧预算、语义绑定/跟踪及动作/等待 schema 待冻结（DEC-109 细化） | Mirage 维护者 | M9-05/07/08/09/10/13/18 |
| DEC-092 | Native pet 完整交付 | 默认携带/自动启动 Accepted；聚合目标、布局、能力/版本诊断在实施前冻结；显式偏好另属 M8-08 | Mirage 维护者 | M8-12/08 |
| DEC-002 | Mira TLS 通道适配器 | 已定案（[DEC-002](../decisions/DEC-002-build-test-baseline.md) 变更记录）：`MIRA_WITH_MBEDTLS=OFF` 于 M2 复核冻结（运行面无模型网关/TLS 传输，mbedtls pinned 子模块在位可随时重开）；接入真实模型网关且通道要求 Mbed TLS 时按记录触发条件重开 | Mirage 维护者 | M2（已冻结） |
| DEC-004 | Mira Host 状态集 | 已定案（[DEC-004](../decisions/DEC-004-mira-host-status-set.md)）：五态 `Stopped/Starting/Running/Stopping/Failed`，M1 冻结并写入设计文档第 11.1 节 | Mirage 维护者 | M1（已冻结） |
| DEC-005 | DesktopObservation 契约 | 已定案（[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）：schema 1.0 字段集、结构化 SemanticSnapshot（预算 + 确定性渲染）、多提示 ElementTarget 与解析顺序契约（reference → accessibility → Mirador 视觉（M3）→ VLM 显式），M2-01 冻结 | Mirage 维护者 | M2（已冻结） |
| DEC-006 | UI 技术路线与分发打包 | 已定案并冻结（[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md) 2026-09-21 修订节）：Web 前端 + 嵌入式渲染壳冻结为 CEF（PoC 取证见[壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)，Electron / Tauri 否决理由封存）；`.deb` / Windows `exe` 安装包；更新通道冻结为 Linux GPG 签名 apt 仓库唯一路径、Windows 双层签名应用内更新器（全量优先，差分 M5 评估） | Mirage 维护者 | M3（已冻结） |
| DEC-007 | Local IPC 机制 | 已定案（[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)）：Unix domain socket（Linux）/ 命名管道（Windows，M4），长度前缀 + JSON 帧格式，协议 v1 请求面，`apps/service` 进程形态；传输与帧格式自 M1-04 冻结 | Mirage 维护者 | M1（已冻结） |
| DEC-012 | IPC 事件订阅与 wire schema 事实源 | 已接受（2026-09-16 评审通过，[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)）：协议 v1 附加事件帧 + 订阅 op（版本号不递增），[mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md) 为契约事实源（M1.5-01 落地，golden vectors 双端门禁），事件分发经 `executor::comm` 承载 | Mirage 维护者 | M1.5 |
| DEC-013 | 前端信息架构与设计规范 | 已定案（[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)）：harness 优先统一壳（会话默认落地、workflow 一级入口、设置八类），workflow 编辑器为对齐 mira Workflow IR v1 的结构化步骤序列；规范见 [《Mirage 前端设计规范与信息架构》](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)，M1.5 落地骨架、M5 验收基线 | Mirage 维护者 | M1.5 |
| DEC-016 | Mirador 视觉集成契约 | 已定案（[DEC-016](../decisions/DEC-016-mirador-visual-integration-contract.md)，M3-01）：Visual Reference `@vN` 生命周期对齐 `@eN` 整体替换、`visual_snapshot(_ref)` schema 1.0 → 1.1 加法演进、mirador 结果到感知面的映射边界、一图像源一 session 一 blocking worker 串行承载（EXEC-04）、fake/identity backend 默认验证形态（真实模型后端属集成方） | Mirage 维护者 | M3（已冻结） |
| DEC-022 | 上游能力消费路由（2026-09-26 升级批） | 已定案（[DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)）：`M5-05` 绑定升级后 pinned 工作流 / 工具契约面（TR0/TR2、Skill 执行、Degraded 事件；`atom.catalog` 经工具暴露投影；MCP 连接配置以 DEC-039 为承载边界）；Skill / 工具管理产品面挂账 POST-05；mira 记忆 / 上下文 / 时间策略经 `Mira::core` 隐式消费、无 Mirage 直接面；mirador M7 目标跟踪转正前观察、转正后按 POST-04 立项（DEC-016 加法通道） | Mirage 维护者 | M5-05（绑定面）；POST-04 / POST-05 按各自触发条件 |
| DEC-023 | M5 工作流契约面（协议 v1 扩展）与 WorkflowRuntime 服务承载 | 已定案（[DEC-023](../decisions/DEC-023-workflow-contract-face.md)）：协议 v1 附加扩展 `workflow.*` 八请求 + `workflow.run_updated` 事件 + hello `workflows` 能力位；草稿 = pinned `not_validated` 版本、发布 = `publish_validated` DryRun 门禁、删除 = 产品目录条目移除（pinned 追加式历史不动）；运行监控事件经 `WorkflowEventBridge` 从 pinned 事件转译、快照事实源 `workflow.runs`；TR2 工具引用挂载 / Skill 执行注册 / Degraded 呈现与桌面原子工具注册挂账至第二轮及 POST-05；同内容草稿遮蔽可运行版本的 pinned 语义经台账 `MIRA-20260927-001` 登记 | Mirage 维护者 | `M5-05` 第一轮（已完成）；第二轮（编辑器真实化 + atom 目录人口） |
| DEC-024 | M5 桌面原子工具集（atom.catalog 人口与 ToolCall 执行路径） | 已定案（[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)）：integration/mira `DesktopAtomToolset` 在绑定环境非空 Provider 上构建 pinned `BuiltinToolRegistry`（初始 13 原子，缺席不注册、目录如实反映能力）；宿主 attach 经 `set_tool_registry` 安装，ToolCall 步派发真实桌面动作（副作用原子按 pinned W-02 须带 verification 谓词；DryRun 门禁只规划不派发）；权限经 `AtomPermissionGate` 回调缝接服务共享 `PermissionController`（RULE-05 与任务驱动同门）；`workflow.atom.catalog` 转 exposed view 投影，wire 契约不变（golden 仍 v5）；捕获 / 元素动作 / 指针输入与 TR2 挂载挂账后续轮 | Mirage 维护者 | `M5-05` 第二轮（桌面原子工具注册增量）；编辑器真实化增量随后 |
| DEC-025 | M5 会话页产品化（会话面消费、模拟域退役与对话模式承载） | 已定案（[DEC-025](../decisions/DEC-025-session-page-productization.md)）：会话列表 = `session.list` 快照事实源 + 派生标题（重命名/置顶/删除/导出/fork 无 wire 面不呈现，挂账协议扩展评估）；消息流 = `session.history` 重同步基线 + `session.message` 增量 + 任务快照步骤卡，turn/output 作观察流事件源；对话模式在模型循环接入前如实降级（DEC-008 迁移路径第二步挂账）；模拟域退出会话页；观察台 = 任务快照时间线 + 会话事件观察流（通知面语义）；观察面协议附加扩展（视觉状态呈现，M3 非目标完整兑现，golden v5 → v6）挂账另立增量 | Mirage 维护者 | `M5-06` 第一轮（已完成）；观察面 / 会话管理面 / 对话模式挂账后续增量 |
| DEC-026 | M5 观察面协议附加扩展与工作流定义读取面（golden v6 批次） | 已定案（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）：`desktop.observe` 按需请求 + `ObservationView` 载荷（帧成员恒在、语义快照投影 1024 节点 wire 预算 + `truncated` 显式标记、`visual_snapshot_ref`/`visual_regions` 同现同缺视觉承载，请求即必须 fail closed；无事件形态——M1 驱动形态无观察生产者，驱动器观察投影评估结论）；`workflow.get` + `WorkflowDefinitionView`（head 定义正文由服务侧产品目录保留，pinned 库无正文读取 API 属 W-03 上游设计非缺口），编辑器跨会话编辑解除；hello `observation` 能力位；步级运行事件（无已承诺消费方）与会话管理面（删除=pinned `close_session` 承载挂账 `session.close`、重命名=产品别名 M5-08、导出=客户端投影、fork 不立项）评估后挂账；产品视觉管线接线挂账与 Overlay 联动 | Mirage 维护者 | `M5-06` 第二轮（已完成）；视觉管线点亮 / `session.close` / 步级运行事件挂账后续增量 || DEC-026 | M5 观察面协议附加扩展与工作流定义读取面（golden v6 批次） | 已定案（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）：`desktop.observe` 按需请求 + `ObservationView` 载荷（帧成员恒在、语义快照投影 1024 节点 wire 预算 + `truncated` 显式标记、`visual_snapshot_ref`/`visual_regions` 同现同缺视觉承载，请求即必须 fail closed；无事件形态——M1 驱动形态无观察生产者，驱动器观察投影评估结论）；`workflow.get` + `WorkflowDefinitionView`（head 定义正文由服务侧产品目录保留，pinned 库无正文读取 API 属 W-03 上游设计非缺口），编辑器跨会话编辑解除；hello `observation` 能力位；步级运行事件（无已承诺消费方）与会话管理面（删除=pinned `close_session` 承载挂账 `session.close`、重命名=产品别名 M5-08、导出=客户端投影、fork 不立项）评估后挂账；产品视觉管线接线挂账与 Overlay 联动 | Mirage 维护者 | `M5-06` 第二轮（已完成）；视觉管线点亮 / `session.close` / 步级运行事件挂账后续增量 |
| DEC-027 | M5 对话模式真实化（模型层装配与 session.chat 对话面） | 已定案（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)）：纯对话形态 = Text 契约单轮 ModelRequest → ModelGateway 推理（不复用桌面 AgentLoop）；`integration/mira::ModelLayer` 装配 pinned ModelProfile/Router/Provider/SocketTransport/Gateway，SecretRef 经环境变量在传输边界解析（fail closed），TLS 通道按需挂接、https 无通道 fail closed 永不降级；`session.chat` 异步 turn 受理回执 + `session.chat_updated` 生命周期事件 + `session.chat.history` 快照，hello `chat` 能力位；对话线程服务内存易失、不进 pinned 会话投影；测试经 `ModelProviderOverride` 注入（生产 provider SSRF 姿态拒回环为上游安全设计）；真实端点连通性挂账部署态取证；凭据升级/流式/上下文策略挂 M5-08 与后续 | Mirage 维护者 | `M5-06` 第四增量（已完成，工作项收口）；真实端点取证 / 凭据升级 / 流式挂账后续 || DEC-027 | M5 对话模式真实化（模型层装配与 session.chat 对话面） | 已定案（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)）：纯对话形态 = Text 契约单轮 ModelRequest → ModelGateway 推理（不复用桌面 AgentLoop）；`integration/mira::ModelLayer` 装配 pinned ModelProfile/Router/Provider/SocketTransport/Gateway，SecretRef 经环境变量在传输边界解析（fail closed），TLS 通道按需挂接、https 无通道 fail closed 永不降级；`session.chat` 异步 turn 受理回执 + `session.chat_updated` 生命周期事件 + `session.chat.history` 快照，hello `chat` 能力位；对话线程服务内存易失、不进 pinned 会话投影；测试经 `ModelProviderOverride` 注入（生产 provider SSRF 姿态拒回环为上游安全设计）；真实端点连通性挂账部署态取证；凭据升级/流式/上下文策略挂 M5-08 与后续 | Mirage 维护者 | `M5-06` 第四增量（已完成，工作项收口）；真实端点取证 / 凭据升级 / 流式挂账后续 |
| DEC-028 | M5-07 批准中心与权限策略面（policy.get/set 与 DEC-011 扩展） | 已定案（[DEC-028](../decisions/DEC-028-permission-policy-face.md)）：批准中心接 DEC-020 异步确认面（permission.list 快照 + permission.request 通知 + permission.respond 先到先得，TS 传输缺口补齐）；`policy.get` / `policy.set` wire 面（PolicyView：全 DEC-010 规则集 + read_roots 资源范围；全量覆盖校验、规则立即生效、read roots 重启生效）；`PermissionController` 线程安全 policy/set_policy；settings `permission` 块扩展为全词表 map（加法演进）+ 默认拾取与写回翻转（DEC-011 修订）；默认策略收紧留观 M5-08 | Mirage 维护者 | `M5-07`（已完成）；默认收紧 / read-roots 热更挂账后续 |
| DEC-029 | M5-09 Desktop Overlay 承载机制（服务进程承载 + Platform Backend 私有前端 + 事件织物联动，wire 零变更） | 已定案（[DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md)）：Overlay 由 `mirage-service` 承载（任务执行流宿主，GUI 关闭仍呈现）；`desktop/overlay_carrier.hpp` + `overlay_surface.hpp` 纯 std 公共头为唯一新契约面（不进 DesktopEnvironment 冻结 provider 集、不进 IPC wire）；Windows 双分层窗口（视觉窗 `WS_EX_TRANSPARENT` 跨进程穿透 + 每像素 alpha；交互窗仅确认横幅可点）；Linux X11 shape 覆盖层（override-redirect + XShape 双 shape，无合成器依赖），Wayland 原生 / 无交互桌面 open() 返回 null fail closed；呈现循环 = Executor `IBlockingIoWorker`，流侧 `LatestMailbox` 最新状态语义；`AtomOverlayFeed` 动作缝（DEC-024 先例同型）；确认入口经 `AsyncConfirmationHub::resolve` 同路径应答（first-response-wins 不变），呈现以 hub 快照 `is_pending` 校验；`--overlay on|debug` 默认 off | Mirage 维护者 | `M5-09`（已完成）；Windows 运行级取证 / Xvfb 呈现取证 / 独立测试验证轮挂账后续 |
| DEC-030 | M5-10 Tray 承载机制与任务暂停/恢复面（task.pause/resume 附加扩展 + 双平台指示器承载） | 已定案（[DEC-030](../decisions/DEC-030-tray-carrier-and-task-pause-face.md)）：`apps/tray` 单 Executor owner 经 Local IPC（EXEC-02）；`task.pause`/`task.resume` v1 附加扩展（v10，与 task.cancel 同形守卫），pinned pause 家族投影 + 驱动操作边界驻留（resume 后新 step id 续驱；pinned"不支持执行级续跑"关系如实声明）；Windows 承载 = DEC-018 Shell_NotifyIcon 面产品化菜单扩展（无 AUMID/无 WinRT）；Linux = StatusNotifierItem + 最小 dbusmenu（纯 gio 族，DEC-015 纪律），无指示器宿主 null 响亮退出；快速进入 = 兄弟壳二进制发现 | Mirage 维护者 | `M5-10`（已完成）；Windows 托盘运行级取证 / 真实指示器宿主取证 / 独立测试验证轮挂账后续 |
| DEC-031 | M5-11 Windows 安装器生成器定案（NSIS）与打包/更新通道交付 | 已定案（[DEC-031](../decisions/DEC-031-windows-installer-generator.md)）：生成器 = NSIS（`packaging/windows/mirage.nsi` + build-installer.ps1，fail closed），WiX 否决（.NET 工具链绑定、MSI 特性超需）；载荷 = 四产品进程 + 可选桌面壳 CEF payload；AUMID 注册表键随安装器交付（DEC-018 载体半边成立，快捷方式属性插件留发布机）；Authenticode 签名脚本证书注入 fail closed（证书不入仓库）；Linux .deb = CPack DEB（shlibdeps 自动依赖 + postinst/prerm），apt 仓库 GPG 签名/验签脚本 + 更新清单 ed25519 签名与 fail-closed 验签（本地五步演练全过）| Mirage 维护者 | `M5-11`（已完成）；真实安装/升级/卸载取证 / makensis 构建 + Authenticode 签名取证 / 图标资产挂账后续 |
| DEC-032 | M5-12 Windows 应用内更新器核心与通道 fail-closed 行为（runtime/update 库 + CLI，全量优先） | 已定案（[DEC-032](../decisions/DEC-032-update-client-fail-closed.md)）：runtime/update 库——清单严格解码（schema/字段/预算全 fail closed）、ed25519 验签（libcrypto EVP，OpenSSL 缺席构建整体 fail closed 于 crypto 面）、HTTP fetch（无 TLS，信任锚为清单签名+Authenticode 双层；3xx 不跟随；超预算 fail closed；POSIX/WinSock 双端口）、原子切换（staged 预检→备份→aside+rename→全成删备、败则逆序回滚）；CLI `mirage update check/apply`；托盘/壳触发面接同核（后续接线）；更新器零私钥（信任锚公钥随包分发）| Mirage 维护者 | `M5-12`（已完成）；Windows 运行级全量更新演练 / OpenSSL 运行时随包分发挂账后续 |

`DEC-084` 的 native 产品携带/自动启动选择已由
[DEC-092](../decisions/DEC-092-native-product-pet-delivery.md) 冻结，M8-12 尚未实施；
最小只读状态快照、各平台窗口策略仍按 M8 原门禁验收。
WebP 源 + 显式 RGBA 帧控制及可执行文件相对资源布局已用于预览，
240 逻辑像素角色画布与 X11 位置恢复按维护者要求实现，安装与预算验收仍开放。
负责人为 Mirage 维护者，分别在 M8-01/02/04 取证冻结，
详见 [决策提案](../decisions/DEC-084-mira-pet-process-and-animation.md)。

## 跨里程碑通用完成定义

- [ ] `DOD-01` 实现落在 `RULE-01` 约定的层内；公开头文件不含第三方类型。
- [ ] `DOD-02` 新增并发路径受 Executor 管理，取消与 shutdown 闭合，失败可见。
- [ ] `DOD-03` `debug`、`release`、`asan`、`ubsan` 预设构建通过；涉及跨上下文状态时
      `tsan` 通过；`ctest` 无 skip 冒充成功。
- [ ] `DOD-04` 新契约有对应单元/集成测试与负向用例（拒绝、越界、取消、超时）。
- [ ] `DOD-05` 计划状态、设计文档、决策记录与验证证据同步更新（工程规范第 8 节矩阵）。
- [ ] `DOD-06` Commit / MR 符合工程规范第 10 节；`dependencies.lock.json` 与
      submodule 指针一致。

## 延后项与触发条件

- `POST-01` macOS / 移动端支持：仅当出现明确的桌面端之外的宿主需求时立项；设计文档
  范围限定 Linux / Windows。
- `POST-02` 多实例 Runtime Service（多用户 / 多会话隔离）：待单实例形态在 M1-M2 验证
  后，依据真实隔离需求立项。
- `POST-03` 远程 Desktop Environment（SSH / 容器内桌面）：待本地 Backend 稳定且出现
  真实远程使用场景时立项；接口设计需提前保留 Provider 远程化的可能。
- `POST-04` mirador M7 跨帧目标跟踪消费（视觉观察增强）：触发条件 = 上游
  M7-10 go/no-go 判定 GO 且 `object_tracker` 契约计入兼容性承诺；消费点在
  `integration/mirador`（PerceptionSession 管线 + 变化门控短路），wire 走
  DEC-016 加法通道；路由见
  [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md) 决策 4。
- `POST-05` Tools / MCP 产品面真实化（Skill 列表 / 发布 / 吊销、工具兼容状
  态、MCP 服务器连接配置）：触发条件 = `M5-05` 工作流契约面落地且出现产品需
  求；经 IPC 镜像 pinned DEC-040 发布生命周期与 DEC-039 接纳契约，不自建
  skill 存储；路由见
  [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md) 决策 2。

## 2026-10-03 原生前端接手

维护者要求轻量原生窗口与 ZCode 参考的会话页，按 [DEC-033](../decisions/DEC-033-native-agent-frontend.md) 新建 [M6](m6-native-frontend.md)（In Progress）。先实施 EUI dev 锁定和本地预览；真实 IPC 与整体托盘退出分别归 M6-03/04。既有 M5/CEF 验收保留，不作为原生端功能等价证据。


2026-10-04 / M6-03：按维护者澄清，先交付通用Agent harness，RPA workflow暂不接入。
DEC-034在单一integration Adapter中复用Mira模型/工具/运行控制面；依赖缺口
MIRA-20261004-001（通用入口/工具输入）与002（TLS SNI）保留台账。本机SiliconFlow
实际问答与工具回填通过，MiniMax未通过；M6-04入口/托盘整体退出仍按原计划。
具体状态和验证见[M6](m6-native-frontend.md)及[验收](../compatibility/native-harness-20261004.md)。

M6-03 Linux通用harness首步已完成，真实模型/取消/背压/关闭与界面验收见上述证据；
M6整体仍In Progress，M6-04统一入口/托盘与Windows运行取证未完成。

2026-10-04 / M6-05：Linux原生会话页按ZCode源码对齐完成，包含Agent Markdown、
增长输入、引用原文/来源检查及真实上下文入口；测试与独立修正评分见
[会话验收](../compatibility/native-zcode-conversation-20261004.md)。
M6整体与M6-04状态不变，未将Wayland截图限制或Windows/IME待验收标记完成。


2026-10-04：M6-06 Linux首步上下文占用圆环完成，真实最后请求输入Token/显式窗口预算
经现有IPC展示；Debug/Release、ASAN/UBSAN、TSAN、真实模型和上下文扩展视觉复核通过。
详见[M6](m6-native-frontend.md)及[验收](../compatibility/native-context-usage-20261004.md)。
窗口未知和重启旧历史明确展示未知；M6整体与Windows/统一入口进程任务状态不变。

2026-10-05：维护者要求 ZCode 模型配置/指定输入栏顺序并清除旧 TS 前端，按 [DEC-037](../decisions/DEC-037-native-model-composer-and-web-retirement.md) 新增 M6-07/08。旧 CEF/TS 技术路线已退役，M5 历史验收保留；当前前端为 EUI 原生。完整入口/托盘退出确认仍在 M6-04。

M6-07/08 Linux首步已完成，模型目录、真实文本附件、权限/推理请求与指定工具栏接通，
旧Web源码/构建/打包已清除。Debug空闲回归48/48，原生针对与sanitizers通过，Linux
原生DEB生成和提取启动通过；负载下旧订阅测试失败和Windows等未执行项保留记录。
[模型与输入验收](../compatibility/native-model-composer-20261005.md) /
[退役验收](../compatibility/native-retirement-20261005.md)。M6整体与M6-04状态不变。


2026-10-05 / M6-15至18完成Linux范围：紧凑会话、右侧模型/用量、真实流式与用时，
Linux XIM候选光标跟随及公开Mira ConversationLoop；两项依赖修复分别提交上游PR，
原Executor版本保留，锁与供应链审计同步。[验收](../compatibility/native-conversation-progress-20261005.md)。
M6整体/入口托盘M6-04仍未完成，Windows/Wayland/物理高DPI另验。


M6-19完成Linux依赖收尾：EUI四种窗口/渲染CI组合全部通过，Vulkan生命周期修复与
SDK消费、Mirage原生回归通过。同步最新dev修复并移除库层异常临时覆写；Mira流式PR
12项CI也已成功。未合并依赖PR，完整入口/托盘M6-04保持后续工作。
[验收](../compatibility/eui-vulkan-followup-20261005.md)。


2026-10-06 / M6-20完成Linux会话复验：真实窗口IME/多行、真实流式/用时、右侧工具组，
补齐长回复阅读保位与返回底部、空闲关闭唤醒。EUI公开offset修复更新PR#88/pin与锁，
当前head四项CI及原生Debug/Release/ASAN/UBSAN回归通过；无新增Executor并发路径。
[最终验收](../compatibility/native-conversation-finish-20261006.md)。M6整体仍In Progress，M6-04保持Planned。

2026-10-06 / M6-21完成Linux侧栏与模型服务编辑：统一图标/文字轴、未命名草稿、服务/模型分层、实际Key保存/刷新/调用；来源对照与未覆盖的完整ZCode能力见[验收](../compatibility/native-provider-editor-20261006.md)。M6整体状态和M6-04不变。

2026-10-06：M6-22按DEC-043完成Linux本地范围的侧栏/服务标题间距、11项厂商预设和Messages真实模型接入；MiniMax-M3真实识图使用公开Provider，图片附件产品面尚未立项。M6整体仍In Progress。

M6-22 Linux本地验收完成：Debug50/50及相关Release/ASAN/UBSAN、受控ASLR TSAN均通过；MiniMax-M3真实图片2/2及工具/流式通过。上游MessagesPR#80独立提交，详见[证据和未覆盖范围](../compatibility/provider-presets-and-vision-20261006.md)。M6保持In Progress。

2026-10-06：M6-23按DEC-044修复原生模型设置事务与按键逻辑；真实Release设置35/35、renderer324 checks及Debug/Release/ASAN/UBSAN相关回归通过。离线取消、断线失败保留、重连继续保存取证；目标平台/Windows既有CI问题另验，M6整体保持In Progress，见[验收](../compatibility/model-settings-interactions-20261006.md)。


2026-10-06：M6-25 按 [DEC-045](../decisions/DEC-045-tray-runtime-owner.md) 完成 Linux
托盘内嵌 Runtime 与独立前端生命周期；18 项真实产品检查、8/8 回归、ASAN/UBSAN
各 3/3、TSAN 关闭 ASLR 后 1/1。旧开发 headless 入口保留；产品前端不允许脱离
托盘。证据见 [托盘验收](../compatibility/tray-runtime-owner-20261006.md)，M6-04 保持
In Progress，等待 Windows、原生 Wayland 和安装包补验。


2026-10-06 / M6-25 补验：BUG-20261006-006 的最小化恢复由所属 FrontendProcess
平台边界处理，GNOME 条目恢复、私有 WM 21/21 和有界 PID 平台测试通过。窗口
更新通知不再是恢复的前提，Linux 范围 Completed；Windows/包装仍未验收。


2026-10-06 / M6-25 菜单增量：按维护者要求左键显示“打开应用”“退出应用”，修正
Linux DBusMenu 宿主格式，保持 Runtime 活动退出确认。Linux 协议/私有真实产品
23/23、相关 Release 3/3、ASAN/UBSAN/受控 ASLR TSAN 各 1/1 通过；真实通知区鼠标
与 Windows 补验仍未执行，不改变 M6-04 范围。当前用户实例待重启，
[证据](../compatibility/tray-runtime-owner-20261006.md)。


2026-10-06 / M6-25 应用列表入口补验：BUG-20261006-007 的旧 GNOME 入口通过原生
bootstrap exec 转发到统一启动器，真实窗口仍只由已注册托盘创建。维护者实机点击
确认，当前/旧入口私有验收分别 24/24、28/28，ASAN/UBSAN 入口分支各 6/6。
Linux 入口范围 Completed，Windows/原生 Wayland/包装范围保持待验收，见
[入口补充](../compatibility/tray-runtime-owner-20261006.md)。

2026-10-07：M6-26 Linux X11 交付：会话栏按模型选择思考方式/深度，设置页重复开关移除；
空内部会话不进入持久化/恢复，非空历史 ID 与自定义容量门禁保留。默认 25 槽预留 1 主会话。
Mira Messages 自适应思考/签名工具回填经上游 PR#81 和精确 pin 交付，Executor 未变。
真实 MiniMax 原生开关/预览、工具循环及 Release/消毒器验证见[验收](../compatibility/session-thinking-20261007.md)。
整体 M6 的 Windows/Wayland 与包装边界继续未完成。

2026-10-07：M6-27 修复会话到底部按钮层级，真实指针点击、正文移动、恢复末尾跟随在
Linux 原生正常/最小明暗通过；Release/ASAN/UBSAN 相关各 2/2。正式桌面入口加载新版，
同时完成 M6-26 锁屏后重启补验；[证据](../compatibility/conversation-scroll-20261007.md)。


2026-10-08 / M6-33：按DEC-051完成Linux原生侧栏/主题过渡、逐模型思考声明和显式模型
目录获取；Release52/52与相应sanitizer/真实公开目录验证通过。
[验收和补验边界](../compatibility/native-motion-model-discovery-20261008.md)；M6整体保持In Progress。

2026-10-11：M7-14 第三增量修复缺失凭据清理误报，避免新 Key 写入已知 GNOME 临时
默认集合；会话增加固定应用启动与 HTTP(S) 交付，沿既有权限/取消/验证链。Release
71/71、Debug 65/65、sanitizer 定向各 9/9、实际产品与新 DEB 各 37/37，用户配置与
历史保持。活动 Key 需新版重新保存一次以持久化；网页业务和提交评审仍待验，M7-14
整体保持 In Progress。详见 [DEC-071](../decisions/DEC-071-credential-cleanup-and-application-uri.md)
及 [验收](../compatibility/credential-and-application-uri-20261011.md)。

2026-10-11 / M7-15 准入：维护者授权补齐快捷工具不可达时的通用 GUI 路线。
DEC-072 冻结截图区域操作、窗口/显示上下文及界面推进条件；Linux X11 首验，
复用现有 Mira 循环、Mirador 变换/OCR 和 Runtime owner，不修改 pinned 依赖。
工作项与真实验证状态由 [M7 计划](m7-visual-tools-and-input.md)跟踪。

2026-10-11 / M7-15：通用 GUI 首增量完成本地验收，截图区域点击、桌面上下文及
新后条件进入默认工具链；真实 X11 GUI-only 测试闭环 243 checks。Debug 66/66、
Native Release 72/72、sanitizer 各 11/11、产品/包各 37/37；在线自主选路与真实
浏览器目标、其他平台和提交评审未闭合，M7-15 维持 In Progress。
[验收](../compatibility/general-gui-fallback-20261011.md)。

2026-10-11 / M7-10 首增量：维护者授权原生 Wayland 接入，DEC-073 冻结默认只读
Portal 观察与能力收缩。GNOME Screenshot 已保存 PNG 但未返回 URI，改用授权的
ScreenCast/PipeWire 单帧；真实宿主返回 3200×2000。现有 Mira 供图与 Mirador 缩放
复用，X11 业务路径保留。M7-10 In Progress：输入、持续会话、坐标与业务验收未闭合，
详见[M7 计划](m7-visual-tools-and-input.md)及[验收](../compatibility/wayland-portal-observation-20261011.md)。

2026-10-11 / M7-10 第二增量：DEC-074 将成功的 Wayland 共享绑定到任务，后续新观察
复用一次授权；应用操作本地重新核对候选，终态区分派发前拒绝和派发后不确定结果。
真实 GNOME 连续新图与接管关闭 22 checks 已验；完整键鼠/浏览器业务仍开放，见
[M7 计划](m7-visual-tools-and-input.md)与[验收](../compatibility/task-scoped-wayland-observation-20261011.md)。

2026-10-11 / M7-10 第三增量准入：最新用户记录暴露 Portal OCR 错走显示器路径，
按 DEC-075 接入已有 PP-OCR/ncnn 的任务内只读图像分析，并纠正当前能力说明。
实现及验证状态见 [M7 计划](m7-visual-tools-and-input.md)；键鼠和在线业务仍开放。

2026-10-11 / M7-10 第三增量本地验收：Debug 67/67、Native Release 73/73，
ASAN/UBSAN/TSAN 定向各 7/7、Portal 各 3/3；Release/DEB 产品各 37/37。
真实 GNOME OCR 探针待一次系统共享，在线视频业务与 RemoteDesktop 输入未关闭，
见[验收](../compatibility/native-shared-frame-ocr-20261011.md)。

2026-10-11 / M7-10 第四增量：DEC-076 修复原生 OCR JSON/候选预算的整次拒绝，
输出显式只读前缀；实机独立探针一次共享连续三次 ncnn OCR 与接管关闭 58/58。
原共享取图超时未复现，已增阶段诊断，稳定性、Wayland 输入与业务仍开放，详见
[M7 计划](m7-visual-tools-and-input.md)与
[验收](../compatibility/native-ocr-budget-and-capture-diagnostics-20261011.md)。

2026-10-11 / M7-10 第五增量（DEC-077）：任务内 RemoteDesktop + ScreenCast 完整
click/key/text/scroll 接入产品图像绑定工具与局部验证。隔离 Portal、完整动作和 Mira loop
自动验证见本轮记录；GNOME 解锁、开启系统控制且英文输入状态下，动态公共窗口
六次完整动作及连续回图、精确 ASCII URL/Enter、接管与 Unicode 拒绝 11/11。
其他输入法、静态画面取图仍开放，非 ASCII 派发前明确拒绝。
仍 In Progress，最终构建/消毒器与范围见 [本轮记录](../compatibility/wayland-task-input-20261011.md)。

第五增量本地收尾：Debug 68/68、Native Release 74/74，ASAN/UBSAN/TSAN 定向各 7/7，
默认 PP-OCR/ncnn 零模型产品验收 37/37；DEB 二进制与最终 Release 匹配，用户状态
六份 SHA 不变。TSAN 聚合测试期限修正及先前失败保留在证据中，未扩产品预算。

2026-10-11 / M7-10 第六增量（DEC-078）：设置入口、系统钥匙环单次令牌轮换与
任务/重启恢复协议已接入，管理等待受 Runtime Executor 管理；默认逐任务询问。
维护者要求先完成自动验证、暂缓系统授权，GNOME 跨任务/跨进程免弹窗验收保持
未完成，由 Mirage 维护者在解锁桌面并允许控制和记住选择后补验。自动结果见
[记住授权验收](../compatibility/remembered-desktop-authorization-20261011.md)，M7-10 保持 In Progress。

2026-10-11 / M6-37：维护者要求左侧会话最近更新者置顶，DEC-079 采用原生有界排序
投影与历史规范记录时间，保留选择和草稿，查询历史不计更新。具体自动/原生渲染及
打包验收见 M6 计划与[会话排序记录](../compatibility/session-activity-order-20261011.md)。

2026-10-12 / M6-38、M7-10 第七增量：DEC-080 提供会话三种访问模式及授权联动，
DEC-081 修复最新会话工具清单、OCR 新引用和静态 PipeWire 最新帧所有权；
动作后源水位禁止缓存替代新证据。M6-38 Linux 本地 Completed；M7-10 真实
GNOME 共享、授权恢复及浏览器目标门禁仍 In Progress。自动验证和包证据见
[本轮验收](../compatibility/conversation-access-and-cua-continuity-20261012.md)，
外部方案依据见[官方 CUA 调研](../research/2026-10-12-cua-native-access-and-observation.md)。

2026-10-12 / M6-38 同日界面澄清：会话访问菜单只保留三个模式，删除该处授权设置，
选择后收起。Linux 定向渲染、交互和包匹配通过，见
[菜单验收](../compatibility/conversation-mode-selector-20261012.md)。

2026-10-12 / M6-26、M6-38、M7-07、M7-10：三模式菜单按 ZCode 锚定按钮上方；
DEC-082 对 MiniMax-M3 默认显式使用独立思考通道，并分离单调用与整任务预算，
修复开启思考时 7 轮提前结束。Debug 70/70、Native 76/76、sanitizer 各 6/6，
三档合成在线复验 3/3，9 工具/10 调用任务完成，最终 DEB 四程序 SHA 匹配。
M6 本地增量完成，M7 原用户任务/授权恢复保持开放；默认异常尚未在线稳定复现。
未安装/重启产品或提交/推送，见[验证记录](../compatibility/reasoning-and-menu-anchor-20261012.md)。

2026-10-12 Git 交付：维护者要求整理提交，并明确授权将同期选路修改一起核验。
依赖、桌面、平台、原生 UI 与 Runtime 分组提交，代码终点 6703251；最终组合
Debug 70/70、Native 76/76、ASAN/UBSAN/TSAN 选路各 5/5，源码稳定性与包内
四程序校验通过。本地提交不关闭既有真实平台/原用户任务门禁，未推送或合并。
完整范围与证据见[Git 交付记录](../compatibility/m7-git-delivery-20261012.md)。

2026-10-12 安装包更新：按维护者要求将 adfe65b 网络恢复修复与已有 DeepSeek
兼容修复纳入 Native Release，补齐 DEB 的 Phosphor 字体/许可证与厂商图标。
全量 76/76 通过，四程序、17 项界面资源和五项 OCR 资源解包校验通过，已更新
out/packages/mirage_0.1.0_amd64.deb。未安装/重启用户产品；真实会话与目标平台
开放门禁不变，见[安装包验收](../compatibility/native-package-refresh-20261012.md)。

2026-10-12 / M6-41：HUA Chat Completions 点号工具名引发首轮请求拒绝，已复用
Provider 别名适配并更新本机。定向 Debug/Release/ASAN/UBSAN 与真实 HUA 调用/
回放对照通过；共享工作树的同期目标稳定性全量五项视觉失败仍开放，未覆盖已验
安装包或自动重跑原任务，见[验收](../compatibility/hua-tool-alias-20261012.md)。

2026-10-12 / M6-41：按供应商调研和维护者授权，统一三方言的模型侧工具名称、Host
指令及回放/返回适配；121 项协议检查、七配置 loop/registry 与消毒器定向通过。
本机 Native Release 已更新；共享工作树全量 75/78，三个桌面视觉测试失败由同期
修改补验，整包和原任务门禁保持开放。见[统一适配验收](../compatibility/unified-tool-alias-20261012.md)。

2026-10-15：维护者确认长程任务不按步骤/工具累计次数终止，优先本地视觉语义
与动作/等待闭环。建立 DEC-090/091/092、M9（Planned）及 M8-12（Planned）；
M9-02 受 MIRA-20261015-002 阻塞。旧 DEC-088 与其实施清单转为历史，未完成
验收迁入 M9。仅文档与静态一致性检查，未实施代码、升级依赖或重启用户产品。

2026-10-15 / M9-01/05/13、M8-12 首增量：新增有界对象增量/语义状态层与 Mirador 映射；
native 产品聚合、默认 pet 与缺载体诊断已接入，Debug/ASAN/UBSAN 与私有桌面
32/32 取证见 [首增量记录](../compatibility/m9-incremental-scene-and-pet-build-20261015.md)。
尚未接 native 语义观察/LLM 标定或取消步骤上限，不更新现有用户 release 实例。


2026-10-15：维护者明确授权 Mira 原开发仓库修复及 PR 流程，已创建持续循环
[上游 PR #99](https://github.com/Linductor-alkaid/mira/pull/99)（Debug 109/109，
1,025 轮 / 1,024 工具脚本通过）。M9-02 等待交付/消费，Mirage pin 未变；
反馈 MIRA-20261015-002 保持 Open，详见 M9 计划与依赖台账。


2026-10-15：M9-02 的 Mira a29374a 消费验收完成，独立升级
[PR #97](https://github.com/Linductor-alkaid/mirage/pull/97) 已创建。Debug 干净分支
82/82、当前工作区 84/84，ASAN/UBSAN 各 5/5，持续契约与 native 产品编译通过。
M9-03/04 与真实长任务验收保持开放；证据和平台限制见
[pin 审计](../supply-chain/mira-continuous-loop-20261015.md)。


2026-10-17 / M6-42：原生输入卡顿首轮分析与应用层优化，16KB多段/40轮样本
输入路径中位72.54→21.56ms；Release、ASAN/UBSAN定向验证通过。单段长文本
及历史开销未关闭，EUI-20261017-001待授权修复依赖，实机与Windows另验；
方法、完整统计和限制见[性能记录](../compatibility/native-input-performance-20261017.md)。


2026-10-17 / M6-42第二阶段完成本机修复与取证：先更新最新EUI dev，再完成有界
字形测量缓存、通用场景重复验证后提交issue#113/PR#114（四项CI全绿），最终pin
63117f7保留旧Windows修复；原生历史接入公开高度缓存，12种输入场景p50/p95均
改善，单段16KB/40轮51.64→16.98ms。Release5/5、ASAN/UBSAN各4/4；实机、
Windows及上游评审另验，详见[性能记录](../compatibility/native-input-performance-20261017.md)。


2026-10-18 / M6-43：会话上下文用量快照保存及重启恢复的Linux数据路径完成，
历史截断仍可读取，失败/取消、未知与旧事件语义有界；Native Release构建和定向回归、
ASAN/UBSAN/TSAN通过。实机与Windows补跑保持开放，见
[M6工作项](m6-native-frontend.md)及[验收记录](../compatibility/native-context-usage-20261004.md#2026-10-18m6-43-会话自身保存用量快照)。

2026-10-18 / M6-44：按维护者要求取消原生及服务的默认会话数量限制，逐会话 JSON
存储与旧文件迁移由 [DEC-101](../decisions/DEC-101-session-files-and-unlimited-count.md)
约束。Linux 实现、迁移/回滚和原生缓存回归通过，Release/ASAN/UBSAN 五类测试及
TSAN 三类测试通过；Windows 与真实桌面保持开放，见 [M6](m6-native-frontend.md)
和[验收记录](../compatibility/session-files-20261018.md)。


2026-10-18 / M6-44实机补验：完整产品重新构建、启动并正常退出重开；用户8会话19轮
迁移后逐对象比对与备份一致，真实服务39会话准入通过且测试会话已清理，窗口与运行
映像验证通过。Linux真实桌面缺口关闭，Windows仍开放，见
[验收记录](../compatibility/session-files-20261018.md#2026-10-18真实-wayland-桌面启动与重启补验)。


2026-10-18 / M6-45：按DEC-102补齐缺供应商usage时的请求输入估算，来源明确标注并
随会话保存；Native Release产品已重建。Release定向、原生渲染与ASAN/UBSAN/TSAN
通过，Windows和真实供应商缺usage仍待补跑，详见[M6计划](m6-native-frontend.md)与
[上下文验收](../compatibility/native-context-usage-20261004.md#2026-10-18m6-45-无供应商用量时显示本地估算)。


## 2026-10-18：M7 / M8 观察节奏与桌宠恢复

维护者最新重试出现累计帧耗尽、重复截图和桌宠长时间消失。按
[DEC-107](../decisions/DEC-107-capture-cadence-and-presentation.md) 删除累计截图额度，
复用近期/区域观察与动作验证回执，保留历史规划意图及独立的新鲜帧守卫；桌宠取帧后恢复，
隐藏不再清空解码动画。计划归属 M7-10/14/19、M8-06，
[验证和限制](../compatibility/observation-efficiency-20261018.md) 记录实际测试与补跑条件。
M7/M8 的真实桌面、在线业务与其他平台门禁保持开放，负责人为 Mirage 维护者。


2026-10-20 继续推进 DEC-109/110/111：M9-07 显式后台变化准入与 M7-32
平台无关精确读值比较均进入 In Progress；Shift 完整 tap 可由既有 key 工具执行。
生产感知默认、真实持键会话、平台读值与新值条件接线未因此标记完成。实现、
验证结果及补跑条件见[本轮证据](../compatibility/dec109-111-input-progress-20261020.md)。


2026-10-20 最新会话回归归入 M9-01/09/12（BUG-20261020-001）：修正预览坐标
错误的恢复提示、历史失败事实供给、视觉重复身份与普通动作 epoch 耦合，以及
可修正参数错误误封整工具。双击/系统快捷键策略同步更新，已有 unknown/release
和权限守卫保留；不以脚本取代真实聊天应用/模型验收。SSE 最后一次协议错误
未取得脱敏事件流，不据旧错误字符串宣称归因或已修复。见
[调查与验证记录](../analysis/native-action-recovery-20261020.md)。

2026-10-21 补查：native-release 离线复验旧 SSE 截断样本 3/3 正确到达 Incomplete；
公开 provider 诊断转发没有缺失。此次失败流未留存，无法从进程内事件存储恢复，
本次 SSE 根因继续开放；没有重放原任务或改动依赖解析器。


2026-10-21：M7-27 进入 In Progress，首接线为 X11/XInput2 固定前台窗口内的受控
持键/截图与版本化独立工具族，以及 Portal 同次调用的本地定时长按。看护复用
ModelLayer 的一个 Executor blocking worker，任务终结与权限换代进入主动释放。
普通 input_released 回执语义不变；系统切换器焦点转换、未验平台及真实业务门禁
继续开放。详见[实施与验收证据](../compatibility/controlled-input-sessions-20261021.md)。
本轮 Debug 全量 106/106、ASan/UBSan/native-release 定向各 15/15；完整 native-release
product 与公开头文件边界检查通过。M7-27 仍按剩余门禁保持 In Progress。

2026-10-21 后续：M7-27 接通 X11/XInput2 固定窗口内持有中移动，advance 1.1.0
只接收目标整数坐标，本地规划时长，移动后保持 holding；取消、权限换代、焦点变化
和越界经原释放路径结算。工具 schema/golden、Xvfb 真实轨迹与 ModelLayer 调用链
及 sanitizer 证据见[本增量记录](../compatibility/controlled-input-travel-20261021.md)。
系统切换器焦点转换、Portal 跨调用持有、Windows 与真实模型/设备门禁继续开放，
M7-27 保持 In Progress。

2026-10-21 设备守卫后续：M7-27 受控 X11 持键最低 XI 2.1，补齐外部 grab 期间的
raw 来源观测与固定设备路由/拓扑变化屏障；同键/同按钮归属不确定不发 up，其他
外部输入只触发本任务已知输入清理。Xvfb、API 故障注入与实际 sanitizer 结果见
[本增量记录](../compatibility/controlled-input-devices-20261021.md)。不把协议设备
夹具当作物理拔插证明，剩余平台与 M7-27 的系统切换器门禁继续开放。

2026-10-21 系统切换器后续：M7-27 接入 Openbox 3.6.1 的独立 window_switch 工具族，
由宿主持 Alt、正反选择和清理，允许全屏截图；原窗口焦点/几何与单写者 lease 不放宽。
实际 Openbox/Xvfb 与 ModelLayer 脚本闭环、故障和 sanitizer 验证见
[本增量记录](../compatibility/controlled-window-switcher-20261021.md)。其他管理器、
Portal/Windows、真实物理设备与在线模型门禁继续开放，M7-27 保持 In Progress。

2026-10-21 native-release 后续：完整 mirage-native-product 已更新（含 native、
pet、OCR/YOLO 和资源打包），Release 定向回归 19/19、39.57 s，公开头文件边界
零违规，launcher --version 正常。命令和产物哈希见
[后续验证记录](../compatibility/controlled-window-switcher-20261021.md#2026-10-21native-release-更新与提交前验证)。
本次未启动/重启产品；M7-27 的其他平台与真实环境门禁继续开放。
