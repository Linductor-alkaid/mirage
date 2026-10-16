# DEC-060：无文字闭合区域的产品编号装配

> 状态：Accepted（维护者授权继续推进）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 扩展：[DEC-058](DEC-058-scoped-visual-marks.md)、[DEC-059](DEC-059-visual-text-postconditions.md)

## 选择与依据

Mirador VisualIndex 比较已定位的 patch，不扫描整帧定位模板；当前 session 的模板
消费依赖 Detector 区域。产品先启用已有闭合几何提案，形成无文字矩形按钮的同源
SoM 候选。使用 pinned SegmentGrowingLineDetector、filter/merge/propose_regions、
EvidenceSet 和既有 serial blocking worker，不增加模型、matcher、线程或队列。

## 契约

OCR manifest schema 1 新增可选布尔值 `geometry_marks`，缺省 false。非布尔值在
资源装载与 worker 启动前拒绝。配置进入整个 manifest 的 canonical revision；
变更后需重启服务，既有跨 revision/引用换代与动作新鲜度门禁继续生效。

仅 `desktop.vision.mark` 开启几何阶段，普通 OCR/固定文本后观察保持原有识别路径。
使用 Mirador 默认参数和明确预算：灰度转换 4 MiB、工作内存 4 MiB、最多 1024
线段、64 提案；超过预算拒绝整个结果，不截断。合并后的输出继续受 128 区域、
8 MiB 图片和 16 KiB 编号元数据限制。任何阶段失败不发布可执行编号。产品只融合 tight 面积不超过整帧 1/4 的几何候选，
防止窗口/面板容器通过 containment 把内部控件合成一个编号；原始提案保留在分析回执，
64 提案预算在筛选前执行。较大闭合控件不在此增量支持范围。空文本 OCR 区域不进入融合，避免无文字识别框
将多个几何候选合并；backend 原始回执保留，不改识别或匹配算法。

几何区域没有文字、模板身份或交互语义；闭合轮廓不等于按钮。模型可选择同源图中
的编号，但点击仍要求固定目标窗口、权限、scope、epoch、完整区域与完全相同的
新截图。成功只由动作前固定的窗口/语义/OCR 条件及新观察判定，几何变化不充当
业务成功证据。

## 验证与边界

使用真实几何算法验证无文字区域、默认关闭、负原点、裁切、预算、取消和 shutdown。
受控 Xvfb Canvas 验收使用真实 Screen/XTest/PP-OCR：无文字目标编号点击后绘制
固定业务文本；画面不更新必须拦截成功回答。脚本选号只证明工具装配。

模板文件装配、无闭合轮廓图标、在线模型选号、用户宿主及 Windows/Wayland/DPI
仍为 M7 后续项。未把现有 API 的定位职责解释为依赖缺口，未修改 pinned 源码。


2026-10-10 后续增量：模板文件装配与几何候选 patch 消费由
[DEC-061](DEC-061-geometric-template-resources.md)承接，双模板/位置交换/不重绘负例
见[模板验收](../compatibility/geometric-template-resources-20261010.md)。原始几何对象
仍无身份或语义；仅当前唯一模板证据另附 ID。无闭合轮廓等其余门禁继续开放。

2026-10-16 / M7-23：合入本轮实测调优，mark 的 min_length=16、
deviation_tolerance=4；依据 [参数消融](../compatibility/mirador-tuning-and-capabilities-20261016.md)。
更新上游细描边修复与可选语义检测不改变几何候选无交互语义的契约，见 DEC-097。
