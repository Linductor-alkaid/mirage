# DEC-062：视觉供给窗口范围化

> 状态：Accepted（维护者授权推进 M7-07 用户宿主取证准备）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-055](DEC-055-desktop-image-supply.md)、[DEC-058](DEC-058-scoped-visual-marks.md)、[DEC-057](DEC-057-real-ocr-composition.md)

## 背景

DEC-058 的 `desktop.vision.mark` 无 crop 时供给整屏编号图，`desktop.vision.ocr` 返回
整屏文本。私有 Xvfb 上整屏即公开画布；真实宿主上这意味着用户桌面像素与文本可能
进入模型。宿主取证门禁要求"限定公共测试窗口及其截图范围"，因此需要一个产品级
供给范围机制，而不是仅在驱动里拼 crop。

## 契约

`DesktopToolContext` 新增可选 `capture_scope`（窗口 id）。为空时保持既有整屏观察
契约不变；非空时：

- `desktop.vision.mark`：当前聚焦前台窗口必须等于 scope 窗口，否则拒绝；模型
  crop 只能在 scope 窗口几何内收窄；无 crop 时供给图像即 scope 窗口几何（编号
  渲染、PNG artifact、supplier 均只含该窗口像素），裁剪发生在任何上传之前。
- `desktop.vision.click`：目标 `window_id` 必须等于 scope 窗口；窗口/像素稳定性、
  五秒新鲜度与 DEC-058 全部门禁不变。
- `desktop.vision.ocr`：返回 regions 过滤为完全位于 scope 窗口几何内的文本，窗口
  外文本不返回；输出在 scope 生效时附带 `window_id`；聚焦窗口非 scope 时拒绝。

scope 是供给层约束，不替代权限判定：`screen.capture` 授权沿既有词表；后续若权限
词表支持窗口级授予，可在 runtime 侧把 scope 与授权联动。截屏在 X11 上仍为根窗口
区域抓取后软件裁剪（`ScreenProvider::capture_window/capture_roi` 为既有能力，
管线因编号需要 1:1 显示映射继续整帧采集，整帧不离开进程，出供给边界的只有
scope 裁剪结果）。

X11 根窗口抓取无法感知遮挡：宿主取证要求 scope 窗口置顶且无遮挡；点击前像素
一致性重采集与窗口/几何稳定性检查保证 mark 与 click 之间出现遮挡即拒绝。

## 实现与路由

范围执行在 integration 层工具边界（`DesktopImageTurn` 与 OCR 工具注册处），复用
既有 `VisualObservationPipeline` 的 `mark_roi` 路径与 `ScreenProvider` 能力，无新
线程、队列或采集通道；Executor 纪律不变。`runbook` 入口由验收 harness 提供
`MIRAGE_CANVAS_HOST`（真实显示器）、`MIRAGE_CANVAS_POS`（窗口原点）、
`MIRAGE_CANVAS_OCCLUDE`（遮挡守卫负例）。

宿主 harness 的真实窗口管理器事实（2026-10-10 XWayland 探针证实）：合成器会把
窗口摆到与请求不同的位置，且在 Wayland surface 持焦时拒绝一切 X11 侧取焦
（EWMH source 0/1/2 与 `_NET_WM_USER_TIME` 均被拒）。因此守卫、锚点与点击目标
一律使用 provider 枚举到的实际几何而非请求原点；EWMH 激活有界轮询，仍被拒绝时
harness 经 provider `click_at` 在守卫已验证无遮挡的 Canvas 中心注入一次焦点辅助
点击（click-to-focus 路径），随后排空该事件，脚本化点击记账不受影响；焦点仍不
可得则 fail-closed 拒绝供给。`X11Backend::activate` 固定 source indication 属
平台行为限制，不是 pinned 依赖能力缺口，不进反馈台账。

## 验证

范围语义（未聚焦拒绝、越界 crop 拒绝、无 crop 窗口范围供给、click scope 不匹配
拒绝、OCR 过滤与 window_id、scope 为空不改变行为）由假环境单测覆盖；私有 Xvfb
回归、遮挡守卫负例与真实宿主 XWayland/Xorg 证据记录在对应兼容性文档。未执行的
平台验收不标完成。


2026-10-10 后续扩展：原整屏采集后裁剪行为保持默认；可选 ROI 预采集由
[DEC-063](DEC-063-regional-visual-pre-capture.md)承接，让明确范围在分析前生效，
同范围输入校验与 owner 局部 OCR 不扩大 scope/权限边界。宿主物理取焦门禁不变。
