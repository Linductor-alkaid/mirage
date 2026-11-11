# DEC-063：视觉局部预采集与同源输入核验

> **2026-10-27 部分条款失效：** 普通动作的像素重验/到达截图硬门禁及单任务感知所有权，以 [DEC-117](DEC-117-runtime-resident-perception-and-autonomous-input.md) 为准。冲突原文保留历史，不再作为当前实现或验收要求；其余条款继续有效。


> 状态：Accepted（维护者授权在第六增量后继续推进）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 扩展：[DEC-058](DEC-058-scoped-visual-marks.md)、[DEC-062](DEC-062-visual-capture-scope.md)

## 背景与选择

范围供给已经限制上传像素，但编号/后条件仍先采集并分析整屏，4K BGRA8 会超过
8 MiB capture 预算。ScreenProvider 已提供全局 ROI 采集，Mirador 已提供坐标变换；
选择在采集前应用显式范围，不抬高全屏预算、不缩放、不实现新采集或匹配算法。

## 契约

审核过的 OCR manifest schema 1 新增可选 roi_capture 布尔值，缺省 false，参与
canonical revision。启用时 mark 的显式 crop（包括 capture_scope 的窗口默认范围）
通过 ScreenProvider::capture_roi 直接采集、在该像素帧分析与编号；无 crop 仍整屏，
不隐式选择窗口。局部编号是该帧独立的 scope/序列，不复用整屏编号；mark 升至 1.2.0，参数/结果字段不变。
默认关闭保留 DEC-058 对整屏分析后裁剪及编号的兼容行为。

EnvironmentVisualRefresh 增加可选 capture_roi：为空表示 frame 是整屏；非空表示
frame 的全局像素范围。display_geometry 仍是物理显示器身份/布局，image_geometry
仍是供给图片范围。ROI 必须完全位于所选显示器，尺寸与捕获帧一一对应；使用
Mirador translation 从 ROI 原点映射到全局，marked frame 不再二次偏移。

mark 验证 capture_roi、image_geometry、display_geometry 和帧尺寸；click 在同一
ROI 再采集，逐像素比较，然后核验显示器、窗口、焦点、scope/epoch、年龄与租约。
范围之外的像素变化不退休局部图，但目标窗口/显示器变动仍拒绝；范围内变化明确
拒绝输入。边界错误、超预算与未实现 ROI 的 provider 不回退整屏。

管线另提供显式 refresh_region 用于 owner 已确定范围的 OCR 后观察。该方法直接
ROI 采集，保留全局 OCR bounds 与完整 display identity；普通 refresh 不变。
宿主 harness 在此模式为固定 Canvas 范围装配 OCR 回调，范围校验仍由 DEC-062
与动作前固定窗口/后条件门禁执行。不会从模型输出自动改变 owner 的范围。

capture 仍 8 MiB、geometry gray/work 各 4 MiB、128 regions、16 KiB 元数据、32×32
模板指纹等预算不变。所有分析继续由既有 serial worker/future 管理，取消、期限、
拒绝、失败和 shutdown 沿既有路径；没有新线程、队列或调度器。

## 验证与限制

核验 4K 显示器上的小窗口 ROI、负原点、坐标/编号、同范围二次采集、范围内像素
变化、窗口/显示器变化、默认兼容、坏布尔值、越界/尺寸/预算拒绝、取消与关闭。
真实 Xvfb 4K canvas 用原 Screen/XTest/Mirador/PP-OCR 路径验证正负闭环与资源
预算。4K 小窗口取证不代表整屏 4K、任意 DPI/缩放、跨屏窗口或原生 Wayland 支持；
真实宿主仍沿第六增量 runbook 补证，物理取焦门禁不绕过。未修改 pinned 依赖。


实施与取证见[4K 小窗口验收](../compatibility/regional-visual-capture-20261010.md)：
正常/ASAN 权重场景各 12/12、完整编译与定向回归通过。在线 4K 补证 DNS 失败，
完整权重 TSAN 业务期限与用户宿主物理取焦仍开放；该决策的 Accepted 不代表
M7-07 或这些门禁 Completed。
