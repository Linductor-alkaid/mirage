# 分层感知复盘与视频时间拼图调研

> 状态：Completed（日志、公开契约与原始研究核对；非端到端性能验收）
> 日期：2026-10-27（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-23 / M9-24；决策：DEC-118 / [DEC-120](../decisions/DEC-120-temporal-storyboard-observation.md)
> 基线：Mirage `d30502a`；Mira `68a8bf17`；Mirador `2a9f4830`

## 会话事实与版本边界

读取 `$XDG_STATE_HOME/mirage/sessions/b96ee31994b740029a5da4d7a27daf7f.json`
（未设 XDG 时为 `~/.local/state/mirage/sessions/`）中唯一 chat_turn，用户要求
“去 bilibili 找个你喜欢的视频看看”。这里只保存脱敏统计和故障类型，不复制日志
中的其他应用、窗口标题、网页全文或图像。

- timing.inference：22 次、88266ms；timing.capture：12 次、161ms；显式 OCR：
  2 次、3438ms。持久化工具 parts 中实际是 **8 次 screen.capture、4 次 wait、
  2 次 semantic_snapshot、2 次 OCR、2 次点击**。因此 DEC-118/M9-23 中“12 次
  显式 capture”应理解为旧统计口径错误：12 是 timing 采集次数，包含其他路径，
  不能与 8 条显式工具调用混同。未记录的内部调用不能按差额臆测来源。
- 快照两次失败分别为 `not_found: no such window`（窗口标题变化后旧 ID）和
  `snapshot_too_large: ... node budget of 4096`（超大可访问性树）。
- 会话创建到回复约 107.7s；回复描述的视频时长为 03:41。轨迹证明导航、读取页面
  和播放画面；没有完整视频覆盖或音频证据。不能据标题、简介、两帧变化宣称看完，
  也不能仅据这些日志证明模型到底理解了多少实际内容。
- 两项相关提交均在此会话之后：`f3addab` 为 14:50:48，`d30502a` 为 17:53:13。
  所以这份日志不是 DEC-117/118 最新实现的在线回归；不能拿它证明新门控失效。
  本轮检查之初未发现运行中的 Mirage 进程，native-release 前端产物时间为
  13:51，也不能作为新源码已加载的证据。本轮后续已重新构建前端和服务，未启动。

## 当前实现：已交付与尚缺能力

| 问题 | 当前代码事实 | 尚需处理 |
| --- | --- | --- |
| 反复取图 | Runtime 常驻源、自动事件文本、state/wait、event_frame、显著性门控和已消费图像去重均已接线 | 无在线 A/B；事件→推理唤醒未完成；提示不能替代工具/任务验收 |
| OCR 是否必须由 LLM 调用 | `continuous_image_observation.cpp` 自动 `run_ocr=true`，结果进入对象和 semantic_scene；显式 OCR 是补充 | 改变帧仍先执行识别再作媒体事件门控，减少事件不代表减少 OCR；当前不是每帧轻 CV + 按事件识别的完整实现 |
| YOLO | `background_detector` 默认关闭，现有 OmniParser icon detector 主要在显式 mark；不是通用视频物体模型 | 先测预算与领域适配，再考虑 bootstrap/新对象/低置信区域调用，不能让它按每帧运行 |
| accessibility 失败 | max_depth/subtree 渐进快照、单窗口标题回退及明确多窗口歧义已实现并有单测 | 实站超宽层级仍可能超预算；稳定窗口 token 尚未替换；不能保证任意树都可在一次请求返回 |
| 底层跟踪 | change_detection、ContinuousPerception 实体状态、VisualIndex 与几何能力可复用 | ObjectTracker/NanoTrack 未接生产逐帧通道；目前稳定 ID 关联不能称为像素跟踪/重识别已经完成 |
| 顶层标注与焦点 | annotate 目前仅允许已供给对象的 text/category 语义；focus 是像素 ROI 通知过滤 | 外观/关系语义标注、跟随对象的注意力尚未由当前 annotate/focus 完整承载 |
| 视频理解 | DEC-118 增加媒体活动与节流关键帧、focus 和行为提示 | 没有播放器时间、覆盖区间、音频或“已看内容”验收；新增拼图只补时间对照输入 |

核对入口：[连续图像管道](../../integration/mirador/src/continuous_image_observation.cpp)、
[周期适配器](../../integration/mirador/src/visual_session_host.cpp)、
[模型供给](../../integration/mira/src/desktop_perception.cpp)、
[ObjectTracker 公共头](../../third_party/mirador/include/mirador/object_tracker.hpp)、
[ContinuousPerception 公共头](../../third_party/mirador/include/mirador/continuous_perception.hpp)、
[集成卡片](../../third_party/mirador/docs/skill/mirador-integration/references/session-and-caching.md)。

## 三层方案的工程收敛

维护者的三层方向适用，但需要明确三种“变化”：像素差、识别证据变化、任务语义
变化。轻量 CV 无法保证识别语义，所以不能承诺“所有无语义变化都无事件、所有
有意义变化都被检出”。所有层都保留证据年龄、来源、覆盖与不确定性。

1. **底层 5–10Hz 目标**：短边/ROI 签名、分块差、几何闭合、已标注对象 tracking
   只产生候选/置信度/失踪状态；跟踪丢失和遮挡不能当确认消失。用 Mirador
   ObjectTracker 的外观/结构验证与重捕获能力，不在 Mirage 再写算法。5–10Hz
   配置只表示采集尝试周期；OCR 串行 worker 忙时 latest-wins 会丢中间帧，不等于
   当前已做到 10Hz 跟踪。需独立测 capture/CV/识别各自实得频率、延迟及 drop。
2. **中层事件识别**：启动/布局屏障、新候选、文本 ROI 变化、跟踪置信度下降或
   有界最大证据年龄时运行 OCR/合适 detector。出现/消失只是识别结果，不能把
   “识别后才有的事件”作为唯一识别触发，否则形成循环依赖。复用未变区域需要
   coverage/reuse 证明，不能把旧 OCR 填入本周期当作新识别。
3. **顶层任务语义**：全局 bootstrap、任务阶段切换、未解歧义或相关内容时间窗
   才升级 LLM；标注绑定 stable ID + incarnation + content_revision，变内容即
   失效。focus 是任务本地通知过滤，不能静默改变其他消费者或采集权限。
4. **防漏与静默**：普通 UI 噪声抑制，appearance/disappearance/reset 和真正的
   任务失败屏障保留；允许有界的过期重验与外围巡检。视频内容任务中，人物动作/
   字幕也可能重要，不能把“播放器 ROI 内变化”一律当噪声；任务注意/最大覆盖
   间隙与事件选帧应分开。可保留低频证据采样而不逐帧唤醒 LLM，积累到有界
   时间窗后按需理解。现有启发式对首帧即全屏、缓慢运动和全局变化仍需单独验收。

**依赖边界**：MIRADOR-20261027-001 仍 Open。非零 evidence digest 要求完成全帧
识别的拒绝发生在 Mirage `VisualSessionHost::submit`，不是直接由 Mirador 返回。
当前版本的适配器会在证据身份改变时重建周期并清比较基线；因此简单翻转 digest
有盲周期。现有记录体现的是“当前集成/账本组合不能安全承载 carryover”，不能
据它宣称 Mirador 底层完全不能做 change-only。后续先验证公开 plan/accept/reuse
协议是否足以在 Adapter 分离采样基线与覆盖基线，保持未识别区域为 unanalyzed；
不足再由上游补最小契约。禁止通过删准入检查或虚构 coverage 绕行。

## 原始项目调研与选择

2026-10-27 回读论文、官方仓库与指定公开文件；未下载模型或视频、未运行第三方
程序。下述是方法借鉴，不是 Mirage 实测收益。

| 原始来源 | 已核对的方法 | Mirage 取舍 |
| --- | --- | --- |
| [IG-VLM 论文](https://arxiv.org/html/2403.18406v1)、[官方评测代码](https://github.com/imagegridworth/IG-VLM/blob/main/eval_gpt4v_multiplechoice.py) | 多帧按时间拼图交给图像 VLM，提示明确左上到右下顺序；示例设 6 帧；论文讨论空间细节与时间覆盖的权衡 | 最适合现在的图像供给接口，先做 2–4 帧有界实验，不照搬其多选题中强迫猜测的提示 |
| [VideoAgent 官方仓库](https://github.com/YueFan1014/VideoAgent)、[tracking.py](https://github.com/YueFan1014/VideoAgent/blob/main/tracking.py) | 先建时间与对象记忆，再用工具按问题取证；实现包含 RT-DETR/ByteTrack 和 CLIP/DINOv2；文档测试设备是 RTX 4090 | 借鉴“对象状态 + 时间证据”分离和增量复用；不引入整套高负载预处理、Python/GPU 服务或长视频全量索引 |
| [StreamForest 论文](https://arxiv.org/abs/2509.24871)、[官方仓库](https://github.com/MCG-NJU/StreamForest) | 持久事件记忆与短时细粒度时空窗口并存，按时间距离/内容相似度等组织压缩 | 借鉴有界近期细节与较长期摘要分层；其特征/token 机制依赖匹配模型，不能等同普通 PNG 拼图 |
| [Screenpipe 事件采集设计](https://github.com/screenpipe/screenpipe/blob/main/docs/EVENT_DRIVEN_CAPTURE_SPEC.md) | 历史设计提出输入/窗口事件触发采集、可访问性优先和 OCR 回退、空闲采样兜底；文档明确标为 Historical，不能当当前源码/参数事实 | 借鉴事件触发与被动变化兜底并存，不照搬定时阈值或“有可访问性文字即跳过整个画面 OCR”，否则漏播放器等视觉区域 |
| [PipeWire metadata 官方接口](https://docs.pipewire.org/meta_8h.html) | 提供 SPA_META_VideoDamage 类型 | 可作为 Platform Backend 的 ROI/静态帧提示；具体 compositor 是否协商并实际提供必须实测，damage 也不提供语义 |
| [OmniParser 官方项目](https://microsoft.github.io/OmniParser/) | 针对 UI 截图的交互区域检测与图标功能描述 | 现有 icon detector 服务 GUI grounding；不能把它视为视频人物/动作识别器；内容理解仍需 VLM 或另行验收的领域 detector |
| [ShowUI 论文](https://arxiv.org/abs/2411.17465)（沿用前轮已核对资料） | UI 结构引导视觉 token 选择 | 继续以对象差分和任务 ROI 优先；不在应用侧伪造其模型内压缩 |

优先级：事件去重 + 识别分频/缓存证明 > ROI/关键帧选择 > 时间拼图打包 > 专门
视频编码器/模型内压缩。前三项互补，拼图不会减少 capture/OCR 的生产成本。

## 时间拼图：可尝试的范围与试验

本轮按 DEC-120 落实 `desktop.vision.storyboard(frame_ids)`：最多 4 张既有 640 边
预览，保比例缩至 320 边放入 2×2 画布，输出单 PNG 和 JSON 格位/观察相对时间。
没有额外采集、识别或模型循环；消费后 ACK、后续不重复供图。不改变已有事件
帧 8 张/8MiB/120s 的容量，也不把稀疏采样称为完整视频。

适合：粗粒度动作、步骤前后对照、场景概览。限制：全桌面预览缩小后播放器 ROI
占比可能很小；字幕、小代码与细动作退回单帧/ROI。当前 focus 只过滤事件，不
裁剪拼图；下一阶段应在授权源/坐标变换固定后比较 ROI 拼图。声音需音轨/字幕，
该工具没有音频。时钟是观察时钟，不是播放器时钟，暂停/跳播不能据此推算。

单张图的计费/视觉 token 取决于实际模型的分块、缩放和 detail，图片数量从 4 到
1 不自动等于成本降到 1/4；640×640 是本轮像素预算，不是 token 承诺。

后续 A/B 固定模型和任务，比较：A 单帧按需、B 四张独立图一次请求、C 2×2 拼图、
D 任务 ROI 拼图 + 单帧细节升级。相同区间与候选帧对照，分别记录输入 token、
模型调用数、capture/OCR/detector 次数、CPU/RSS、端到端延迟、动作顺序/字幕
正确率、漏事件、虚假“看完”率和任务完成率。不能只比较图片张数。

视频任务验收应包含：打开/播放、内容取证、时间区间与覆盖、回答具体内容问题、
说明未观察部分；用户要求看完整段时覆盖到结束或明确报告不足。不能设一个固定
“等 10 秒/看两帧”门槛替代任务含义。后续可用本地可复位视频：无声动作、字幕
讲解、慢变化、短暂插入、首帧全屏、暂停/跳播、音频主导分别验证。

## 未完成与负责人

M9-23：ObjectTracker 生产接线、recognition carryover、任务相关选帧、平台 damage
与事件唤醒仍开放。M9-24：本轮仅打包/消费契约，真实视频质量、ROI、音频和
在线 token/资源 A/B 未执行。负责人 Mirage 维护者；在固定模型、可复位媒体任务
及目标平台准备好后按 M9-12 验收，不把单测当完成率或低端资源证明。
