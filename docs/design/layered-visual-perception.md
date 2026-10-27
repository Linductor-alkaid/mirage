# 分层视觉感知：LLM 语义 / 识别 / CV 跟踪

> 状态：Draft→Partial Implemented（本机验证范围见验收记录；目标态各项单独验收）
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 工作项：M9-23；决策：[DEC-118](../decisions/DEC-118-significance-gated-perception-and-scoped-snapshots.md)

## 目标

让整个视觉层高效低耗地运行：模型不反复取帧；帧发生变化但变化不影响语义或不
重要时，不触发模型可见事件。为此把视觉栈分为三层，越往下越廉价、调用越频繁；
越往上越昂贵、调用越稀少。

## 三层职责与承载

### 顶层：LLM 语义标注与注意力（Mira）

全局理解图像语义、给元素标注任务含义、声明关注区域。尽可能少地使用。

- 承载：pinned Mira 会话 + Mirage `desktop.vision.annotate`（对象语义回写，
  M9-13）、`desktop.vision.focus`（本决策新增的注意力焦点）。
- 触发：语义场景/事件文本能回答时零调用；关键帧、任务边界、状态依赖时按需。
- 图像进入上下文的通路只有语义场景文本、节流的关键帧预览（`event_frame`）与
  显式 capture 的按需供图。

### 中层：OCR / YOLO 识别（mirador + PpOcr/Yolo 后端）

框定并标注元素、引用元素语义，随元素出现/消失事件调用。

- 承载：mirador `PerceptionSession`（budgeted OCR、detector SPI、证据融合、
  稳定 ID）；Mirage `VisualObservationPipeline` 在变化门控后调度。
- 触发：出现/消失/文本变化事件与显著非媒体变化。媒体活跃周期照常执行全帧
  预算化 OCR——集成侧的"媒体周期跳过 OCR"因 mirador 周期账本的证据声明契约
  撤回（[MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)，
  DEC-118 第 2 条）。YOLO（OmniParser icon_detect，单帧 ~1.5s CPU）保持显式
  mark 路径（DEC-097），不进后台每帧。
- 输出：`TextRegion`/`DetectionRegion` → 融合 → `SemanticSnapshot` 区域 →
  `VisualSceneObject`（text/category、incarnation、content_revision）→ 语义场景。

### 底层：CV / 跟踪（每采样周期，5–10Hz 可配置）

用封闭图形与图像相似度判断新目标/旧目标重现/目标消失，跟踪上层标注的元素并
产出元素事件。

- 已承载：mirador `change_detection`（dHash 指纹 + 分块差 + 连通合并，不变帧
  p50 ≈3.5ms）、`ContinuousPerception` 时序实体（stable_id、instance_generation、
  确认窗口去抖、ObservationState 区分 observed/occluded/lost）、`VisualIndex`
  （内容哈希/感知哈希/模板三层相似度，已接模板探测）、`geometric_proposal`
  （封闭图形证据，DEC-060 可选阶段）。
- 本轮新增（DEC-118 第 1 条）：显著性分类与媒体活动区状态机，抑制不影响语义
  的帧变化；布局屏障（kGlobal）不作为媒体确认证据，已活跃区域跨 kGlobal 由
  几何包含继续抑制。
- 目标态（登记未实施）：`ObjectTracker`/NanoTrack 接入生产跟踪通道；平台
  damage（XDamage / PipeWire VideoDamage）把"静态不取帧"下沉到源；事件→推理
  唤醒通路；媒体周期 OCR 跳过（MIRADOR-20261027-001）。

## 数据流

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

## 验收边界

- 单测覆盖显著性分类（静态/微变化/媒体/区外真实 UI 变化）、媒体状态迁移、
  OCR 跳过与恢复、关键帧节流、焦点过滤、渐进快照与窗口回退解析。
- 未跑：真实 Portal 长任务 A/B、10Hz 采样 CPU/RSS、媒体任务的在线完成率。
  这些保留在 M9-12/M9-23 验收表，不以单测宣称。
