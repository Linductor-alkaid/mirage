# DEC-058：同源编号图与视觉点击

> **2026-10-27 部分条款失效：** 普通动作的像素重验/到达截图硬门禁及单任务感知所有权，以 [DEC-117](DEC-117-runtime-resident-perception-and-autonomous-input.md) 为准。冲突原文保留历史，不再作为当前实现或验收要求；其余条款继续有效。


> 状态：Accepted（维护者授权继续推进 M7）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)

## 能力路由

采用 pinned Mirador `render_set_of_mark`、`crop`、公开坐标变换；融合快照、编号图和
原图来自同一次 capture/analysis。在已有 VisualSessionHost 的 Executor blocking
worker 内生成编号图，不新增调度器。模板索引沿用 M3-04 的 enrollment/probe；本增量
不把模板匹配或 OCR 区域声明为可点击控件，也不新增未配置的检测后端。

## 契约

`desktop.vision.mark` 仅在真实 OCR 管线、图片供给和授权均可用时暴露。可指定一次
全局桌面像素 ROI；裁剪不缩放、不重新编号，scope/ref 与同次已发布快照对应。
输出为 PNG artifact、显示器/原点/1:1 比例、epoch、scope 和编号映射；图像只经既有
Mira supplier 输入当前请求，保持 Sensitive 外部数据。每轮 capture/mark/点击前重采集共用四次
预算，128 个编号、原图/输出各 8 MiB、元数据 16 KiB，五秒有效期；编号图刷新失败退休旧图。

`desktop.vision.click` 只接受本轮 mark 的 scope、编号、epoch 与目标窗口 ID，不接受
模型坐标。完整可见区域才允许定位；跨裁剪边缘或画外区域拒绝。输入与新采集分别
授权；使用共享 ActionCoordinator 单写者 lease，完整 `InputProvider::click_at` 和
registry 同锁 scope/ref 校验。点击前重新采集同显示器，要求原图像素完全一致、显示器
几何一致、当前窗口及几何稳定、目标完全位于前台窗口。画面变化、换代、窗口移动、
缩放、过期、取消、takeover 一律拒绝，重新观察后再选择。严格像素比较会拒绝动画场景；
不把窗口/屏幕查询与 OS 注入宣称为原子事务。

动作释放后沿用 DEC-056 固定后条件、新观察和宿主终态门禁。成功注入仍仅为 dispatched；
不确定副作用禁止重发，释放失败 fence；本增量后条件仅支持既有窗口/语义条件，视觉
业务条件、无文字图标模板配置和宿主 Canvas 业务验收继续保留为 M7-07 门禁。

## 验证

验证同源编号/裁剪映射、负原点、旧 scope 同名编号、边缘拒绝、画面/几何/前台变动、
授权、取消/超时、worker 关闭、注入失败/释放失败与新观察。Debug/Release 与 sanitizer
证据记录在对应兼容性文档；未执行的平台和业务验收不标完成。

## 后续扩展（2026-10-10）

[DEC-059](DEC-059-visual-text-postconditions.md)增加固定 OCR 文本后条件与严格新
识别证据；实际 Xvfb Canvas 正/负例见
[第二增量验收](../compatibility/visual-text-canvas-20261010.md)。原窗口/语义契约和
未完成的用户宿主/在线模型/图标/平台门禁不变。

2026-10-10 第九增量只明确既有契约的消费边界：输入前显式再次 mark 会退休
旧图片与旧 scope，即便编号仍为 1，也必须从新图重新选择。刷新仍消耗原四次
采集预算；已拒绝的 mutation 和不确定副作用不会被刷新清除，DEC-056 终态门禁
继续保留失败。不新增自动续期、动作重试或失败恢复策略。

验收入口可显式选择现有 Mira ThinkingMode::Disabled（none）或 Adaptive，默认
仍 adaptive；不修改 Mirage 产品默认值。通过既有 DialogProcessSink 记录最多
32 条工具阶段时间，不记录思考正文，不建立第二套任务监控。配置映射与输入前
刷新正/负例见[第九增量取证](../compatibility/model-visual-latency-20261010.md)。
