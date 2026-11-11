# 原生任务的连续感知接线

> 2026-10-27 当前契约：按 [DEC-117](../decisions/DEC-117-runtime-resident-perception-and-autonomous-input.md)
> 将已有生产者提升为 Runtime owner，独立任务租约、共享最新帧、本地事件等待及
> text_only 保留输入意图见[常驻设计](runtime-resident-perception.md)。下文旧的单任务
> 源生命周期、OCR 退休输入图和普通像素重验仅保留历史，不再约束当前实现。

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-05/06/07/11/12/18；最新会话修复关联 M7-18/25/26
> 依据：[DEC-091](../decisions/DEC-091-local-semantic-perception-and-action-loop.md)、
> [DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)

## 本轮交付契约

维护者授权修复最新会话问题并进行 M9 接线。首个产品消费者使用任务已授权的原生帧，
不为接线重开 Portal、不在另一个 worker 使用交互 session、不增加后台周期取图。
默认 PP-OCR pipeline 开启图像空间连续分析；单一 VisualSessionHost worker 内维护
独立的 display 与 image-space session/coordinator，二者复用既有 backend，串行执行，
不新增模型驻留或 Executor owner。独立状态避免图像 ROI/source reset 清掉 display
模板、混入全局引用或把历史查询当成当前观察。共用原有 latest-wins admission、取消、
future 和 shutdown；查询也使用这一个 mailbox。

图像空间附加缓存预算为 frame 1 MiB、result 2 MiB；连续协调器 512 KiB，关系视图
512 KiB、计划 64 KiB、实体 256、关系 1024、事件 256。输出沿用 OCR 的区域/JSON
预算，不附完整关系表或事件正文。未请求图像空间分析的嵌入保持原行为。

同一 session/task/task_epoch/authority/源类型/ROI 原点与尺寸/预览尺寸具有稳定来源键；任一变化推进
source revision。普通动作 epoch 不等同于 source revision，但仍限制当前工具证据
和输入授权。读取缓存状态必须匹配当前任务的来源键并检查 authority；返回最后分析
帧和年龄，无帧查询不 capture、不 OCR、不形成当前可执行引用或核验证据。

预算化 OCR 保留现有五秒上限、K=64 和 partial/deferred 契约。连续融合和规划采用
其真实结果，不把部分、缓存或未完成证据登记为新的完整覆盖；本轮是 advisory 状态，
不伪造 accept_work 或确认事件。正常取图、动作及后条件仍走原有新鲜度和源帧水位。

文本优先入口允许显式 text_only OCR：已授权原始帧在预算内缩放后直接分析，不生成
PNG/artifact，也不发送图像。返回 image_pixels 文本框、来源元数据与连续状态摘要；
输入仍需显式取得并供给图像的新引用。旧 image_scope 在文本入口被退休，不能依靠
历史图片继续派发动作。需要视觉细节时显式 capture；不降低旧输入守卫。

## 后续验收边界

后台采样/静态退避、ROI 增量复用、焦点策略、事件订阅/gap 恢复、目标标定及完整
文本输入授权继续按专项计划推进。本轮须验证产品 callback 接通、同 worker 隔离、
来源重置、无帧查询、无 PNG 文本路径、部分 OCR、不跨权限/任务复用、拒绝/异常/
取消/期限/关闭；真实低端设备资源、Windows 与 compositor/pet 像素验收保持开放。


## 当前验证

本轮实现已通过最终 Debug 全量、三个 sanitizer 专项、native-release 产品与受控
PP-OCR/native agent 回归；窗口/应用另有当前宿主的只读探针。测试失败修复、
实际计数、二进制摘要和未验收项见
[验证记录](../compatibility/native-observation-and-continuous-wiring-20261022.md)。
背景采集需求/生命周期未接到交互 session，不能把这个按需消费者当作连续后台监控。

## 2026-10-22：动作迟到后的观察消费者

按 [DEC-113](../decisions/DEC-113-autonomous-action-recovery.md)，M7-26 在 input_lag
且 input_released=true 时保留同一授权流；严格更新的同源帧使输入路径恢复可用。
连续第三次迟到停用该路径，完整输入成功后重置故障 episode。text_only OCR 和
vision.state 继续按来源键、任务/权限、期限与 advisory 约束运行；text_only 不产生
图像动作引用。新观察支持当前状态重规划，不证明旧动作发生或任意用户目标达成。
M10-07 的 exec/write/edit 在恢复观察后重新开放；独立 open_uri 可用其自身应用
观察直接推进，随后仍需检查页面。释放/传输失败和撤权不由新帧自动修复。

此收敛沿用同一串行 Executor 调用和既有 Mirador worker，不开启后台采样。
真实模型的下一次 text_only/state 成功记录仍需产品重启后的授权会话验收；
受控管线测试只证明接线与守卫，不证明真实桌面的识别质量或延迟。

## 2026-10-23～24：任务后台事件消费者（DEC-115 / M9-21）

上述 10-22 接线为按需历史。现在在首次 native capture 成功且 sharing session 可复用后，
Runtime factory 将原 session 包装为 ContinuousCaptureHost 消费者，不打开第二个 Portal
会话或请求新的权限。生命周期限于一个 DesktopImageTurn；取消、撤权、关闭即停止并
释放。前台整个输入/IME/OCR 步骤持有 pause lease，后台让出 session 及在途分析；底层
capture/pointer/input 另以资源锁串行。后台拒绝不取消显式分析，前台可取代后台。

分析共享现有 VisualSessionHost worker 和后端实例，另设有界 background companion，
与 display、显式 image/ROI 状态隔离。每个新 sharing session 加入独立实例标识，
同任务/权限/尺寸也不能继承上一条流的对象状态；同一条流的普通截图不重置来源。帧 ID 同时绑定来源实例，旧 ID 不命中新流同号帧。默认 250 ms 尝试采样；OCR 与 Mirador 的轻量
变化筛查共同驱动事件。变化后强制一个独立复查帧以推进两帧确认；静态跳过 OCR，
不能把跳过说成新鲜确认。后台默认不运行慢的图标 detector；非文本变化仅给出
visual_region_changed，不能宣称已识别对象。显式 mark 继续使用已配置的 detector；
可选后台 detector 必须继承模型置信度阈值。

通用 OCR、稳定 ID、出现/消失/文本/位置或外观确认使用 pinned Mirador；原生对象
事件带 source_epoch、instance_generation、实体 ID、源帧和采集完成时间。一帧首见
可给 observed_unconfirmed，不能当确认生命周期；缺覆盖/遮挡不宣布消失。当前版本
不保证既有对象单帧内容变化或任意极短提示全部被保留。

Mirage 产品层只保留游标/注意过滤/证据寿命：最多 256 事件、256 KiB 逻辑历史，未读
120 秒过期；成功模型响应确认实际供给游标后，60 秒回收已读事件。ACK 用容量 64 的
kairo MpscChannel，拒绝不推进消费水位；拒绝/深度投影 CommStats。焦点与不可变视图
通过 LatestMailbox 传输，旧视图只含弱预览引用。目标 ROI 内更新优先，外围纯内容/
移动/像素变化过滤；出现、消失、首见和源重置不因此静音。Mirador 抑制无变化重复事件。

只为保留事件生成至多 640 边、1 MiB 的原始预览，最多 8 张/8 MiB；最长 120 秒，
对应事件已读后至多 60 秒，消费图片后清理。采集宿主原始帧最多两代、每代 32 MiB；
分析缩至 1600 边/10 MiB，依赖转换/缓存及 ncnn 工作区另有自身预算。上述数字不是
整个进程 RSS 上限，真实资源须按专项记录验收。后台不逐帧编码 PNG，也不调用 VLM。

每次模型请求自动附最多 16 条未读事件，以及至多 8 个实体/1 KiB 的已读摘要；整体
含元数据不超过 8 KiB。pinned supplier 输入只进当轮请求，已读摘要使下一轮仍能关联
刚看到的对象，而不重放完整日志；不额外唤醒
模型轮询，内层拼音规划不消费事件。只有成功主响应且实际请求仍含原事件块才 ACK，
预算裁剪/失败不假读。已成功消费的同一截图停止重复附送；显式 capture 可再次供图。
原目标意图仍受新鲜局部像素、authority、scope 和 epoch 约束；事件不能创建输入引用。

`desktop.vision.event_frame(frame_id)` 按需发布一张历史预览，明确 available/historical/
input_actionable=false 与年龄。过期/已消费/容量淘汰为正常 available=false，不能替换
为一张当前截图冒充旧帧。读预览会退休旧输入图像引用。文本输入优先使用本轮已暴露
的 fill/type_text；捕获回执给出 ime_fill/session_type_text 可用性。已证实不支持的输入
可自动选择其他已授权路线，不放开系统授权、接管或释放屏障。

实际证明和未完成门禁见[验收记录](../compatibility/task-continuous-perception-20261023.md)。

## 2026-10-26：M9-13 单标签语义接线

后台完整 temporal 对象状态进入采集 owner 的 VisualScene，与通知筛选独立；
每次完成分析维护内容版本，经原 reports mailbox 发布有界快照。任务串行 owner
消费快照并维护 LLM 绑定，自动观察块增加 semantic_scene；仍使用 8 KiB 总预算。
新工具 desktop.vision.annotate 只接受实际供给且成功响应的对象引用，宿主回填
request_id。位置变化保留识别文字/类别依赖标签，内容/全局布局/gap 使其挂起。
工具不产生 action reference，不调用模型，不增加 worker/队列/授权。
契约、示例与验证分别见[状态层](visual-scene-incremental-state.md)、
[操作示例](../examples/continuous-object-semantics.md)、
[本轮验收](../compatibility/continuous-object-semantics-20261026.md)。
