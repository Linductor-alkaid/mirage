# DEC-121：三层视觉闭环与分频交付

> 状态：Accepted（维护者于 2026-10-27 明确认可三层方案并要求制定完整计划及推进）
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 冻结里程碑：M9
> 工作项：M9-25～M9-33；既有入口 M9-23/24
> 替代/被替代：无；细化 DEC-109/117/118/120，不取消其已交付行为

## 背景与问题

[最新会话与研究复盘](../research/2026-10-27-layered-perception-and-video-storyboards.md)
表明：减少模型可见事件没有自动减少 OCR；稳定 ID 尚不能替代逐帧 tracking；
播放状态也不能证明内容理解。维护者确认采用 CV/tracking → OCR/检测 → LLM
语义三层，并借鉴时间/对象记忆分离、事件窗口、时间拼图及平台 damage。

## 决策

### 三种事实、三种频率

| 层 | 职责与输出 | 升级条件 | 承载 |
| --- | --- | --- | --- |
| CV/tracking | 图像差分、几何候选、位置/外观验证、遮挡/失踪/重捕获；不推断任务含义 | 新候选、候选区域变化、低置信跟踪、布局屏障、覆盖过期 | Mirador 公开 change/geometry/ObjectTracker/VisualIndex；Mirage 负责调用策略 |
| 识别 | OCR/适合当前领域的 detector；输出区域、内容、来源、覆盖和年龄 | 候选事件或过期重验；显式精读/动作验证可强制刷新 | Mirador backend/session/fusion/continuous plan/accept/reuse |
| 任务语义 | 全局理解、对象语义标注、注意区域和内容摘要 | 初始歧义、任务阶段改变、相关证据变化/积累、必要细节升级 | Mira 现有任务模型循环；Mirage 校验并供给有界文本/图片 |

CV 目标为授权活跃源实得 5–10Hz，可按负载降频；当前默认 250ms 不在本决策中
修改。采集、CV、识别、事件发布、模型调用分别计数。识别和 LLM 不固定逐帧
运行；无变化不唤醒模型。任务无关注对象时的后台行为沿用 DEC-117，不能擅自
停掉常驻观察。所有新增数值须按下述预算和配对门禁冻结。

### 基线与覆盖

1. 分离相邻成功 CV 样本基线、最近成功识别/发布基线及 Mirador 覆盖账本。CV
   取样成功不推进识别年龄/确认次数/语义状态；小幅累积漂移必须仍可相对识别
   基线触发刷新。取消、超时或失败不得提交成功基线。
2. 首阶段提供 Adapter 显式 `sample_only`：只允许已完成全帧识别 bootstrap 的
   同一 source/geometry/policy/evidence 配置，执行公开签名/变化比较，返回
   `kSampled`，分析帧每边不超过1600像素，不 fuse/plan/accept/drain，也不生成对象消失/确认或新图像引用。
   保留 evidence digest 仅作配置绑定，不将它解释为本周期识别证明；普通识别
   请求的原准入检查继续生效。源/权限/策略改变需重新 bootstrap。
3. 自动跳识别仅在 M9-26 证明“未变内容可复用、变化/未覆盖区域为未知、采样
   失败可见、证据年龄真实”后启用。不能通过空融合抹去对象、伪造 coverage 或
   丢弃事件来减少计数。公开接口若不足，按 MIRADOR-20261027-001 反馈，不修改 pin。

### 状态机与升级

源：`uninitialized → bootstrap → tracking ↔ recognition_pending → tracking`；
异常进入 `degraded`，布局/权限/来源变化进入 `resync_required`，撤权/关闭进入
`stopped`。最多一个在途和一个 latest pending；迟到结果按来源、frame、policy、
ROI/content revision 校验后才能提交，不能复活已退役源或任务。

对象保留 observed/occluded/tracking_lost/unanalyzed/confirmed 等已有语义；确切
消失必须具有有效覆盖及确认窗口。新候选触发识别，不以识别结果事件作为唯一
识别前置。识别失败保留未知和原因，不能静默继承“成功”。

优先级为权限/接管/来源屏障、显式验证、任务相关变化、普通变化、外围巡检。
focus 是消费者局部策略，不能改变其他消费者或授权。媒体启发式只合并噪声，
不宣称像素变化与语义变化等价；视频内容任务保留低频覆盖与最大缺口检查。

### 时间证据与内容任务

对象存量与时间事件窗口分离：对象保持当前状态及可失效语义标签，事件保留
有界历史、证据 ID、观察时刻和缺失。优先文本/对象差分；需要时间概览时采用
DEC-120 的 2–4 帧拼图，细节再升级单帧/ROI。视频观察区间、播放器进度和声音
分别记录，观察时钟不能推算播放时间。没有音频/字幕来源不得声称听懂旁白。

内容任务状态为 `locating → playback_confirmed → observing_content →
coverage_assessed → answered/insufficient/interrupted`。打开网页或定时等待不能
直接跃迁 answered。用户要求完整观看时，覆盖到结束且有内容证据才可声称完成；
未要求全片时按任务需要观察并说明实际范围，不能硬编码“两帧/十秒即完成”。

### 并发、预算与诊断

- Runtime 仍为唯一 Executor owner。现有 capture worker、视觉 worker、任务
  consumer owner 清晰；后续分离 CV 与慢识别上下文仍使用 kairo blocking worker、
  LatestMailbox（帧/最新计划）、DoubleBuffer（完整状态）、Topic（多订阅事件）。
  所有 future 消费，提交拒绝/覆盖/drop/失败/取消均经既有诊断可见。
- 首阶段不新增 worker，Adapter 两个签名各 4096B；CV 不保留原始像素。后续沿用既有 256
  对象、256 事件、8 张/8MiB 预览、单 consumer ≤16 事件/8KiB、2–4 帧/640²
  拼图上限，不因新层而乘消费者复制整屏；队列和识别计划均显式有界。
- CV/recognition worker 分离前，冻结在途/待处理帧、ROI、模板、跟踪池、模型
  和编码峰值的统一字节表，并纳入 M9 资源门禁；NanoTrack 不作为默认依赖。
- 关闭：停生产 → 取消/退役源和计划 → 唤醒阻塞等待 → 回收 worker/有限任务 →
  外部 owner shutdown(true)。禁止 worker 自等待和私有调度/平行健康系统。

## 备选方案

1. 全帧变化后始终 OCR/YOLO：保留正确性回退，但不满足目标分频成本。
2. 只看 UI 输入/平台 damage：可减少静态采集，但会漏被动变化，且不提供语义。
3. 仅 dHash/区域面积过滤：便宜但不能证明文字/动作未变，不能作为证据复用证明。
4. 直接引入 VideoAgent/StreamForest 完整模型栈：超出端侧预算及现有模型接口；只
   借鉴状态/时间组织，不复制 GPU 编码器或模型内 embedding/token 合并。
5. 固定周期 LLM 或每帧拼图：仍会浪费推理，拼图只是一种按需输入格式。

## 影响与风险

引入显式采样结果和两条基线，旧默认路径不变。M9-26 后逐步在实验开关启用；
生产默认切换需要配对资源、漏检、平台及内容任务证据。风险为静态重验成本、
跟踪漂移/相似对象、慢变化/字幕/全屏媒体漏选、识别队列积压、上下文证据过期。
现有 UI icon detector 不当作通用视频对象/动作检测模型。

## 验证方式

按[交付计划](../plans/m9-layered-perception-delivery.md)逐项验收：基线/账本、源/
策略/权限重置、正常/异常/拒绝/取消/超时/关闭，跟踪漂移与重捕获、识别实际
调用计数、视频内容与覆盖；Debug/ASAN/UBSAN，跨上下文与生命周期加 TSAN。
固定帧集比较单图、多图一次请求、拼图和 ROI 拼图，统计质量、token、延迟、
CPU/RSS及漏报；没有模型或目标平台时保留未执行，负责人为 Mirage 维护者。

## 关联文档和工作项

- [分层设计](../design/layered-visual-perception.md)、[完整交付计划](../plans/m9-layered-perception-delivery.md)
- [M9 总入口](../plans/m9-continuous-tasks-and-local-perception.md)、M9-12 资源/平台退出门禁
- [当前依赖反馈](../dependency_feedback/mirador.md#mirador-20261027-001)

## 后续生产配置（2026-10-28）

[DEC-128](DEC-128-layered-perception-production-profile.md)在维护者授权下部分替代
本文的Linux默认关闭/整体门禁前不得开放约束，按本机配置验收开放layered默认
与稳定CLI，并记录高清内存分档和legacy回退。本文其余覆盖、身份、年龄、取消/
生命周期和预算契约继续生效；本阶段历史证据不升级为完整平台或视频内容验收。
