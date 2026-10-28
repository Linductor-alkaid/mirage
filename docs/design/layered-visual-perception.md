# 分层视觉感知：LLM 语义 / 识别 / CV 跟踪

> 状态：In Progress（本机验证范围见验收记录；目标态各项单独验收）
> 更新日期：2026-10-28
> 负责人：Mirage 维护者
> 工作项：M9-23/24、M9-25～33；决策：[DEC-121](../decisions/DEC-121-three-tier-perception-delivery.md)
> 前序：DEC-118 显著性门控；DEC-120 时间拼图
> 实施：[完整交付计划](../plans/m9-layered-perception-delivery.md)

## 目标

让整个视觉层高效低耗地运行：CV 维持对象状态，识别确认内容，LLM 判断任务
含义；非相关变化合并，不使模型反复取帧。像素变化、识别证据变化和任务语义
变化是三种事实，轻量层不能保证理解全部语义。每层保留来源、年龄、覆盖和
不确定性；底层越廉价、频率越高，顶层越昂贵、调用越少。

## 三层职责与承载

2026-10-28生产增量：[DEC-128](../decisions/DEC-128-layered-perception-production-profile.md)
将配置OCR的Linux服务默认切为独立采样/识别、200ms采样、5000ms完整巡检及有界
跟踪；旧Host的签名与跟踪也迁移到CV工作图。Windows保持legacy默认。下述目标态
的精确覆盖、任务语义和完整视频门禁仍独立，不能从默认切换推断已经完成。

### 顶层：LLM 语义标注与注意力（Mira）

全局理解图像语义、给元素标注任务含义、声明关注区域。尽可能少地使用。

- 承载：pinned Mira 会话 + Mirage `desktop.vision.annotate`（对象语义回写，
  M9-13）、`desktop.vision.focus`（本决策新增的注意力焦点）。
- 触发：语义场景/事件文本能回答时零调用；关键帧、任务边界、状态依赖时按需。
- M9-29 目标：对象语义绑定 source_epoch + stable_id + incarnation +
  content_revision，变化即失效；任务焦点可跟随已验证对象。当前 annotate 仅支持
  text/category，focus 是消费者像素过滤，尚未完成外观/关系语义与对象跟随。
- 模型输入优先语义场景文本，图像通路包括节流的关键帧预览（`event_frame`）、
  [DEC-120](../decisions/DEC-120-temporal-storyboard-observation.md) 的稀疏时间拼图
  （`storyboard`）和显式 capture 的按需供图。

### 中层：OCR / YOLO 识别（mirador + PpOcr/Yolo 后端）

框定并识别元素，复用有证明的未变内容。M9-26/28 目标：启动、新几何/视觉候选、
文本 ROI 变化、低置信跟踪、布局屏障和证据过期触发识别；出现/消失是识别确认
后的输出，不能作为唯一识别前置。

- 承载：mirador `PerceptionSession`（budgeted OCR、detector SPI、证据融合、
  稳定 ID）；Mirage `VisualObservationPipeline` 在变化门控后调度。
- DEC-118兼容接线（legacy）：发生变化后识别，再作显著性事件门控；媒体活跃本身不会跳过全帧
  OCR——集成侧的"媒体周期跳过 OCR"因 mirador 周期账本的证据声明契约
  撤回（当前 Adapter 的证据身份/准入限制，不等于 Mirador 底层不能做 change-only；
  [MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)）。
  M9-25 提供明确不推进覆盖的 sample_only；M9-26 按
  [DEC-122](../decisions/DEC-122-deferred-recognition-unknown-evidence.md) 接入默认关闭的
  媒体识别延后：相邻/累计变化均在确认媒体区域且未到刷新期限时，只采样 CV。
  保留历史对象但标 pending/未确认，原识别年龄不续期，旧标签跨未知区间失效。
  区外/来源/尺寸/焦点变化和到期强制真实识别；显式 OCR 不受节流。
  未调用 plan/accept 声明跨帧 reuse；精确复用仍开放，不能以旧快照证明新帧覆盖。
  YOLO（OmniParser icon_detect，既有单帧 ~1.5s CPU 测量）保持显式
  mark 路径（DEC-097），默认不进后台。DEC-126在默认关闭的async实验里，完整
  识别会调用已配置检测器；局部区域按文字/检测hint或未知组合分别选择能力。
  无变化仍零识别，不开启每帧YOLO。
- 输出：`TextRegion`/`DetectionRegion` → 融合 → `SemanticSnapshot` 区域 →
  `VisualSceneObject`（text/category、incarnation、content_revision）→ 语义场景。

### 底层：CV / 跟踪（目标 5–10Hz，当前实得频率待验收）

用封闭图形与图像相似度产生候选与跟踪证据，中层确认识别内容，顶层判断任务
含义。像素差不等于语义变化；跟踪失踪/遮挡不等于确认消失。采集尝试周期可设
100–1000ms（默认 250ms / 4Hz），不代表当前 OCR 串行 worker 已达 5–10Hz CV。

- 已承载：mirador `change_detection`（dHash 指纹 + 分块差 + 连通合并，不变帧
  p50 ≈3.5ms）、`ContinuousPerception` 时序实体（stable_id、instance_generation、
  确认窗口去抖、ObservationState 区分 observed/occluded/lost）、`VisualIndex`
  （内容哈希/感知哈希/模板三层相似度，已接模板探测）、`geometric_proposal`
  （封闭图形证据，DEC-060 可选阶段）。
- 本轮新增（DEC-118 第 1 条）：显著性分类与媒体活动区状态机，抑制不影响语义
  的候选噪声（启发式，不是语义分类器）；布局屏障（kGlobal）不作为媒体确认证据，已活跃区域跨 kGlobal 由
  几何包含继续抑制。
- DEC-123 首批实验：在后台 companion 的原 kairo worker 内接 `ObjectTracker`，
  最多8目标/64KiB池、每目标16²固定模板/4位置、1MiB验证工作；change gate复用
  不续期外观验证序号。NCC/PSR近邻验证、uncertain/lost和近邻重捕获分别报告；
  重叠歧义、预算、全局布局屏障和验证失败触发同帧像素识别回退。来源/策略/
  实例变化隔离模板；识别内容/位置不一致时重建并失效旧内容版本。前台 companion
  不分配轨迹池，停止/join后释放池。输出位置仅advisory，不恢复旧任务含义。
  几何观察用公开 crop/灰度/线段/闭合proposal，仅一个≤128²变化ROI、128线段、
  8候选；新候选请求识别，未用相同矩形证明同身份。配置与证据见
  [接口示例](../examples/layered-visual-sampling.md#dec-123-有界后台跟踪实验)。
- 目标态（登记未实施）：全局运动补偿、远距离重捕获、结构/外观联合身份验证、
  NanoTrack按需注入和精确文字reuse；平台
  damage（XDamage / PipeWire VideoDamage）把"静态不取帧"下沉到源；事件→推理
  唤醒通路；精确覆盖复用（MIRADOR-20261027-001）；媒体识别延后目前仅为 DEC-122 实验。

## 数据流

当前生产路径（未默认启用分频）：

```
Platform capture (周期可配 100–1000ms, 默认 250ms)
  → 缩放分析帧 (≤1600 边)
  → 变化门控 (mirador detect_change: 无变化 → skipped, 零 OCR/编码)
  → ContinuousPerception (实体出现/消失/文本变化, 确认去抖)
  → 显著性门控 (DEC-118: 媒体区/微变化抑制, 关键帧节流)
  → PerceptionJournal (有界 256 事件/8 预览/120s)
  → 每轮 supply_perception (≤16 事件/8KiB) + vision.wait 唤醒
  → 模型: 语义场景文本 → (不足时) event_frame 关键帧 → (状态依赖) capture
```

目标路径（M9-25～33 分阶段验收）：

```
授权 capture / Backend damage（缺 metadata 时退回周期采集）
  → latest 原始帧（独立 capture 水位）
  → CV 签名 / 几何候选 / 有界 ObjectTracker
      ├ 相邻采样差分、相对识别基线累积变化、跟踪/覆盖年龄
      ├ 无需识别 → 保留年龄/未知状态，不刷新 OCR 成功证据
      └ 候选/文本/低置信/布局/过期 → latest 有界识别计划
            → OCR / 领域适合的 detector → 版本校验 → Mirador plan/accept/reuse
  → 单 owner 对象状态（版本化语义） + 有界时间事件窗口
  → 消费者本地 focus / 优先级 / 合并（关键屏障保留）
  → Mira: 文本优先 → 时间拼图 → 按需单帧/ROI
  → 语义标注 / 下一阶段注意力 / 内容覆盖评估
```

采样、识别和发布三条基线独立，低幅累积漂移不因相邻差分小而永久忽略。
每源一个在途/一个 latest pending；慢识别不应同步阻塞 5–10Hz CV。M9-25 沿用
单 worker 验证阶段契约；M9-28 按
[DEC-124](../decisions/DEC-124-bounded-recognition-dispatch.md) 先交付私有慢识别
派发器。[DEC-125](../decisions/DEC-125-layered-capture-owner.md)
新增默认关闭的产品异步实验：capture owner 独占双签名/模板和journal，独立派发
worker转交原VisualSessionHost执行识别；后者保持单上下文，不并发访问backend。
原帧结果更新识别基线，后续帧未知时暂停当前semantic_scene，保留原时间事件/预览。
[DEC-126](../decisions/DEC-126-selective-regional-recognition.md)再接一个有界ROI的
能力选择：已确认的纯文字/检测hint分别请求OCR/检测，未知组合执行可用能力。
真实模型重放补充8像素路由容差，要求实际交叠且无其他已确认框歧义；工作ROI
保留变化与旧框的并集。hint不完整或混合/邻近目标有歧义时组合执行，不将容差
视为语义或覆盖证明。
局部结果走原Host的raw ROI调用，不调用融合/覆盖/时间实体推进；只保留未确认历史
观察，空结果不确认消失。同区域签名抑制重复识别，同输出摘要抑制重复通知。
完整签名/年龄保持，外围变化与年龄强制完整巡检，迟到结果沿用代次屏障。
精确覆盖和历史对象身份投影、实得频率/质量仍待验收；生产默认路径仍为串行。

## 状态、所有权和资源

源状态：uninitialized/bootstrap/tracking/recognition_pending/degraded/
resync_required/stopped；对象状态复用 Mirador observed/occluded/tracking_lost/
unanalyzed 与确认窗口。源/权限/策略屏障使旧计划与标注失效，迟到结果必须
重新校验，不得复活退役源/任务。sample_only 返回 kSampled，无当前语义快照、
无识别覆盖、无完成证明；不调用 fuse/plan/accept/drain。

Runtime 唯一 Executor owner；跨上下文 latest 帧/计划、完整状态、多订阅事件
分别使用 kairo LatestMailbox/DoubleBuffer/Topic，不能自建调度/队列。先停生产、
取消/唤醒、回收 worker/有限任务，再由外部 owner shutdown(true)。

旧Host Adapter基线最多两个64²灰度签名（8KiB，Mirador内部签名另计4KiB），
额外CV基线仅显式sample_only后分配；独立采样owner按DEC-128另有提交签名（4KiB）。
沿用对象/事件/预览/模型供给和拼图预算。跟踪模板、ROI/在途帧、识别模型、编码
峰值与共享存量在 M9-28/32 冻结统一字节表；DEC-126另保留一个局部64²签名、
≤32个无字符串路由hint和一个输出摘要，裁剪临时缓冲≤3MiB，摘要序列化预留16KiB；不能因分层而隐藏新的常驻开销。
DEC-124 派发器最多两份≤10MiB像素代次（在途/待消费结果与 latest），消费者
保留原帧同样占槽；单完成结果元数据动态容量≤512KiB（至多保留两个，共1MiB）。weak mailbox 不增加旧帧所有权，
结果未消费时不启动下一项。普通 latest 替换不取消在途识别，代次屏障取消并
丢弃迟到结果；这里只是接线前原语预算，不冒充完整产品内存峰值。
调用/跳过原因、失败/拒绝/超时/取消、背压/drop 和年龄经既有 Executor/comm
诊断及感知计数可见，不建平行健康系统。

首阶段 sample_only 的使用与失败语义见
[接口示例](../examples/layered-visual-sampling.md)；它只建立两种变化事实，不
替代 M9-26 的精确覆盖校验，也不默认抑制产品 OCR。实验配置、未知证据投影
和回退见同一示例的 DEC-122 小节。除既有比较签名外，延后策略仅增加至多256对
对象ID/实例等元数据；沿用现有 worker，不增加全帧缓存。

## 验收边界

- 单测覆盖显著性分类（静态/微变化/媒体/区外真实 UI 变化）、媒体状态迁移、
  默认媒体期间保持 OCR、关键帧节流、焦点过滤、渐进快照与窗口回退解析。
  DEC-122 实验另测实际 OCR/detector 调用数、历史对象/年龄、标签失效和区外/
  期限恢复；识别确认仍需要真实观察，CV-only 不计入字幕去抖次数。显著性门控
  本身不降低 OCR 成本，调用次数验证也不代表真实视频内容质量通过。
- 未跑：真实 Portal 长任务 A/B、10Hz 采样 CPU/RSS、媒体任务的在线完成率。
  这些保留在 M9-12/M9-23 验收表，不以单测宣称。

## 视频时间理解与拼图实验（M9-24）

`desktop.vision.storyboard` 一次接收 2–4 个 feed 已保留的 frame_id；按观察时间
排序，复用 Mirador 缩放，生成最多 640×640 单 PNG。格位映射、年龄、时间间隔、
source_epoch、dropped/filtered/resync 随 JSON 供给，不产生额外采集/识别。
图片实际被模型消费后才 ACK 预览，历史拼图不作为动作引用或当前状态验证。
源切换、缺帧和过期明确报告，不补帧或把稀疏证据当完整视频。

视频任务必须区分打开/播放与实际内容理解，描述所观察区间及局限。像素媒体
启发式不能发现所有语义变化；全屏、慢动作、字幕和音频仍需独立覆盖。拼图不
引入音频/播放器时间，也不保证 token 等比例下降。更完整的三层路由、任务相关
选帧与 A/B 方法见[调研](../research/2026-10-27-layered-perception-and-video-storyboards.md)。

M9-30 的内容任务状态为 locating → playback_confirmed → observing_content →
coverage_assessed → answered/insufficient/interrupted。观察区间、播放进度和
音频来源独立，用户要求完整观看时必须有内容与结束覆盖。当前提示和拼图工具
尚不能自动完成这条状态机；相关退出条件见交付计划。

## 分层工作图（DEC-127）

独立采样owner不再用最长边1600的识别图直接计算CV签名、跟踪或几何候选。
复用Mirador area缩放到最长边320、转Gray8，三者共用工作图坐标；64²完整/局部
签名从该工作图生成。变化框与跟踪框按左右/上下边界分别floor/ceil映射回识别图，
再沿既有来源映射回桌面；源尺寸变化即撤销基线，不只比较工作图尺寸。
CV模板任一边不足8像素时排除跟踪，不能据模糊外观确认身份；完整年龄巡检不变。
优先整数缩小倍率`ceil(最长边/320)`，宽高除倍率取整：1024×512取256×128，
1001×503取250×125；以320作上限而非必须填满的画布。CV失败透出稳定错误类别
`cv_failed:<Mirador错误名>`，不以通用cv_failed掩盖超时/预算/参数失败的区别。

能力路由容差与裁剪context均为每轴4个CV像素回映、至少8个识别图像素。
已知框与实际变化先取并集再加context，仍受全帧1/4上限、歧义阻挡及32-hint完整性
约束。1600×900下固定8像素不足，曾误选YOLO并切断文本；修正及失败样本见
[第七阶段记录](../compatibility/layered-perception-phase7-20261028.md)。容差不是语义覆盖。

OCR与YOLO从识别图或其ROI独立准备模型输入，沿用OCR检测侧960、文字行高48/
宽至多640、YOLO640；不从CV图放大识别。小于层上限的图不为固定缩小比例继续降质。
LLM历史拼图仍为最多640²。识别输入的更小上限必须通过独立文本/框/召回对照；
置信分高及低分辨率空结果都不能证明无损。旧Host低频识别缓存内部处理未随本轮迁移。

临时CV彩色图至多409600字节，灰度工作图至多102400字节，局部裁剪至多102400
字节；与签名/模板及既有识别像素槽、模型缓存分别核算。首次缩放仍访问来源像素，
新增预处理也有成本；不以工作图字节减少宣称全进程CPU/RSS或5–10Hz已验收。

## 生产迁移与回退（DEC-128）

Host的sample_only、识别准入和session变化比较均使用降采样图。变化区域回映到
识别图坐标；发布的snapshot副本替换change，Mirador内部snapshot不直接流出Adapter。
识别run_ocr/run_detector继续接原识别图及模型预处理，CV指纹不作能力缓存键。
纯CV调用不增加能力缓存，Refresh/ReadWrite互通及同CV尺寸下原布局屏障有专门回归。
旧Host跟踪模板也缩放，过小目标排除，输出按边界回映；前台精读保持细节输入。

静态画面在识别在途时合并相同能力/ROI请求；独立64²提交签名（4KiB）比较
最后实际接纳帧，累计缓慢变化或工作变化仍更新latest。
这避免反复识别静态画面，不改变原帧时间、未知状态、年龄或必要确认。
资源/精度实际证据见[生产记录](../compatibility/layered-perception-production-20261028.md)，
指标属于受控像素producer，不能替代Portal/Windows或完整视频/音频测试。

## 按对象版本绑定注意区域（DEC-129）

M9-29首增量沿原任务FeedLease实现consumer-local策略，扩展focus的object_ref
替代参数。引用由已完成模型请求的实际供给认证，PerceptionSemantics再核对当前
source/context、对象ID/incarnation/content_revision；FeedLease在每次view/wait
重验原观察版本。确认的同版本移动更新区域，未知/缺失/过期/换源/gap/失败则
单向失效并恢复全局事件；旧对象不自动复活。坐标模式也按来源版本撤销旧过滤。

PerceptionView/模型供给投影attention状态；wait通过现有可取消等待的有界检查
在无新cursor时也识别失效，不增加worker/定时器/模型轮询。无活动注意时取消检查
不额外读取观察快照；有活动策略时重验会增加有界元数据读取和对象搜索，未声明
整进程零开销或新增频率保证。每消费者仅一项、无像素，字符串/绑定合计≤4KiB；
复用既有至多256对象快照，不新建对象/事件缓存。ACK与历史帧预算不变。

M9-29多目标重要性、跨未知帧精确继承仍开放，不能以本增量
关闭整个任务语义层。使用与失败见[示例](../examples/versioned-task-attention.md)，
验证见[注意力验收](../compatibility/versioned-task-attention-20261028.md)。


## 模型供给的普通事件批次（DEC-130）

消费者supplier按[DEC-130](../decisions/DEC-130-lossless-event-batching.md)对已有日志
作无损表示压缩：同来源/对象/实例/文本、确认且连续的位置或外观事件，在≤1秒内
最多8次合成一行，保留末次平面字段和此前逐次prior_samples。不等待更多帧，
不改变底层事件产生、显著性门控或日志；位置/外观共用事件类型，不能据此忽略
颜色、形状等中间变化。出现/消失、内容/关系、未确认观察、媒体帧与来源屏障不合并。

单次最多16行，事件数组含分隔符≤7000B，整个供给≤8192B；容量不足先缩短
当前组的连续前缀，外层预算再裁整行。只有实际请求包含并完成的供给才ACK末次
呈现cursor。原256条/256KiB日志和8帧缓存不增加，生产事件与其他消费者不受影响。
这减少重复元数据，不代表语义去噪或模型token收益，详见[验收](../compatibility/lossless-event-batching-20261028.md)。
