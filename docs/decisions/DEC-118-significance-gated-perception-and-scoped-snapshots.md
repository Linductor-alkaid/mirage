# DEC-118：显著性门控分层感知与渐进语义快照

> 状态：Accepted（维护者授权落实分层视觉方案，2026-10-27 会话复盘）
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 工作项：M9-23；关联 M9-22、[DEC-117](DEC-117-runtime-resident-perception-and-autonomous-input.md)
> 依据：[分层视觉感知设计](../design/layered-visual-perception.md)

## 背景与问题

DEC-117 落地后，2026-10-27 14:47 会话（bilibili 找视频任务）仍暴露四类问题：

1. 模型在一个简单任务里显式 capture 12 次、推理 22 次。后台事件流在视频播放时
   每个采样周期都产生 `visual_region_changed`，事件噪音让模型无法把事件当观察
   事实，退回 capture 轮询。
2. OCR 主要通过 `desktop.vision.ocr` 显式工具发生；自动管线的全帧 OCR 在媒体
   变化上空耗，识别文本进入语义场景的覆盖不足。
3. `desktop.accessibility.semantic_snapshot` 两种失败：窗口标题内嵌在 window_id
   中，页面导航后旧 ID 解析为 `not_found`；bilibili 页面超 4096 节点预算整体拒绝，
   没有局部视图回退。
4. "看视频"任务没有媒体时间维度的观察通路：模型只开页面、截两帧即宣称在看，
   没有机制让它以低成本持续感知媒体内容。

## 决策

1. **显著性门控（底层）**。连续采集保持现有 250ms 尝试节奏（周期可配置），在
   `VisualObservationPipeline::observe_image` 的事件出口增加显著性分类：与已确认
   "媒体活动区"（跨帧持续的大块变化、无文本实体）相符的变化不产生逐帧事件；
   无关联实体的微小变化（光标、插入符、闪烁反馈）同样抑制。媒体活动区进入/
   退出各产生一条有界 `media_playback` 状态事件；区内结构性大变化（关键帧）按
   最小间隔节流产生带预览的事件。appeared/disappeared/source_reset 永不被门控。
2. **事件驱动识别（中层）**。识别文本由后台管线在变化门控后自动进入语义场景与
   事件文本，模型仅在语义场景不覆盖时才显式调用 OCR。**预测式"媒体周期跳过
   OCR"经验证撤回**：pinned mirador 的连续周期账本把全帧证据声明与已执行的
   全帧识别阶段绑定（`evidence_digest≠0` 且无 OCR/detector 的后台请求被准入
   拒绝），而置空 digest 又会让下一周期失去变化比较基线（盲周期，吞掉触发
   hold 解除的那次真实 UI 变化）。该能力诉求登记为
   [MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)，
   在上游交付前媒体活跃周期的全帧预算 OCR 照常执行（budgeted OCR 自带预算）。
   布局屏障（kGlobal，变化面积 ≥ 1/2 帧）不作为媒体确认证据；已活跃媒体区跨
   kGlobal 周期按几何包含继续抑制，经常规 miss 计数结束。
3. **注意力焦点（顶层）**。新增 `desktop.vision.focus` 工具，把任务 feed 的注意
   范围（既有消费者本地过滤）暴露给模型：焦点外的普通事件被过滤，appeared/
   disappeared/reset 屏障事件保留。焦点是任务本地的，不改变生产者采样与其他任务。
4. **渐进语义快照**。`SemanticSnapshotLimits` 增加 `max_depth`（深度限制）与
   `subtree_ref`（从当前注册表展开某 `@e` 子树）；深度前沿节点携带直接子节点数，
   渲染为 `(+N children)` 标注。显式请求的范围限定不是静默截断；预算超限的整体
   拒绝契约不变。工具面增加同名参数并在描述中给出下钻路径。
5. **窗口 ID 解析回退**。window_id 仍以标题命名但解析失败时，按 application 段
   回退：该应用恰有一个窗口即解析成功（返回当前标题）；多窗口时报错并提示重新
   window.list。稳定窗口 token（AT-SPI 对象路径）保留为后续工作，不在本轮替换
   ID 格式。
6. **采样周期配置化**。`PerceptionSessionOptions` 增加可选 `sample_period`，
   服务配置可设 100–1000ms；默认保持 250ms。10Hz 端的性能与 CPU 验收未跑前，
   不宣称更高频率的资源结论。
7. 本轮不接入 mirador `ObjectTracker`/NanoTrack、不接平台 damage（XDamage/
   VideoDamage）、不做事件→推理唤醒；三者作为分层设计的目标态登记在计划
   （M9-23 后续项），不假装已交付。

## 后果

- 媒体/动画不再淹没事件日志；模型以 focus + `vision.wait` + `event_frame`
  消费媒体关键帧，而非 capture 轮询。
- 事件出口的分类状态是 pipeline 私有的每源状态，串行推进，不新建线程/队列；
  全部沿用 kairo owner 与既有 mailbox/Topic。
- 语义快照对超大树从"整窗或拒绝"变为"逐层下钻 + 子树展开"，契约失败码不变。
- OCR hold 撤回后，媒体活跃周期的识别成本由 budgeted OCR 预算兜底；减少该成本
  依赖 MIRADOR-20261027-001 的上游能力，不得在集成侧再以 digest 翻转方式模拟。
