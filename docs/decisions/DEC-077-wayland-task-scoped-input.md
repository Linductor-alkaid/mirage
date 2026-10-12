# DEC-077：任务内 Wayland 共享与完整键鼠动作

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-10，承接 DEC-074/076

## 背景与问题

只读共享已经接入产品，但原生 Wayland 不能以 XTest 操作用户真实桌面。
维护者要求补键鼠，使视觉观察能进入完整动作及后置验证闭环。授权、流坐标和输入
释放必须归属任务，不向 Mira 暴露平台对象或未经映射的桌面坐标。

## 决策

通过 RemoteDesktop.CreateSession / SelectDevices（keyboard + pointer）、
ScreenCast.SelectSources（单显示器）及 RemoteDesktop.Start 建立同一任务拥有的会话。
不持久化授权、不使用 XWayland 注入；只读 capture 保留原 ScreenCast 路径。
产品 capture 的 interaction=true 明确请求输入，经 screen.capture 和 input.inject 双门禁
后才触发系统界面。失败关闭，同任务不重新授权。

同任务复用一个 PipeWire consumer，由现有 Executor 任务驱动私有循环；不增加后台
worker。最多保留一张待复制的 buffer，回调有界排空 16 个 ready buffer；复制后立即
归还。consumer 在 Portal session 前关闭。授权后首帧单独起算五秒，不让人操作弹窗的
时间挤占首帧预算；后续取图仍五秒。静态画面不交帧的问题保留目标平台门禁。

平台无关 DesktopCaptureSession 提供完整 click/key/text/scroll 动作。点击使用图像内部坐标，
后端依据最近原始帧尺寸和 Start 返回的 compositor size 映射 stream-local 逻辑坐标；
不暴露 Portal、全局坐标、窗口 ID。缺失/不合法 size 或设备授权拒绝时拒绝输入。
Notify D-Bus 方法为首增量，不建立 EIS 连接或新增依赖。

每次按下进入有界释放表；取消、超时、调用失败均尝试释放，不重试有副作用的动作。
释放失败经 ActionCoordinator fence 可见。所有操作由现有 Executor 调用任务串行推进，
不增加线程、调度器或共享队列。ActionCoordinator 的取消 probe 在锁外执行，
准入状态和 epoch 在锁内核对，避免带 authority 检查的取消 probe 重入死锁。
文本先验证 UTF-8，再限制为 printable ASCII；GNOME 46 对无 keymap 对应的 Unicode
keysym 返回 D-Bus 成功但丢弃字符，因此非 ASCII 在动作前返回 text_transport_unavailable。
每个成功键通知间隔 10 ms，由同一有限任务推进；间隔不证明应用已消费输入。
输入法可能变换 ASCII/Enter，必须据新图验证文字和业务，不能把键通知完成当成文字命中。
后续 Unicode 需要单独实现并验收文本传输（如有明确权限的 Clipboard Portal），不默默
覆盖用户剪贴板或猜测输入法快捷键。前置取图自己的软期限返回 input_pre_capture_timeout，
与用户取消分开；无输入副作用，不重弹授权。

产品工具绑定本任务最近已供模型的 image_scope/epoch 与图内矩形，允许 60 秒意图，
动作前重新采集并核对目标矩形（逐像素核对由 [DEC-083](DEC-083-target-region-stability-reverification.md) 修订为分层主体稳定判定）；动作后从同一共享会话取得新图。
shared_frame_observed 只证明动作释放及新图取得；shared_text_exists 可用同源默认 OCR
核对声明文字。二者均不证明焦点身份或整个业务任务完成。Wayland 无可靠前台查询，
因此禁止伪造 X11 焦点保证；模型必须依据新画面确认业务结果，用户接管立即撤销 authority。

## 验收

隔离 Portal fixture 检查路由、设备授权、size/DPI 映射、释放/失败/取消及关闭；
Mira 工具 fixture 检查 owner/scope/epoch、无图拒绝、目标变更和真实验证闭环。
GNOME 实机只操作专用测试窗口，在用户系统授权后验证点击、组合键、文本和滚动。
Windows、多屏跨屏、其他 compositor 与在线业务保留 M7-10 未完成状态。

## 备选与影响

保留只读或仅用应用快捷工具不能满足本次输入要求；用 XWayland 注入不能覆盖原生桌面。
EIS/libei 是后续传输候选，本轮采用现有 Portal 通知，不引入新依赖。增加 task-scoped
Provider 方法、四个输入工具和局部证据投影；默认方法保留旧 Backend 的只读行为。
真实输入法与静态画面限制不能用协议假实现通过代替，工作项继续 In Progress。

## 关联记录

[M7 计划](../plans/m7-visual-tools-and-input.md)、
[本轮验收](../compatibility/wayland-task-input-20261011.md)。

依据：[RemoteDesktop](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)、
[ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)。
GNOME 行为依据：[Mutter 46 keyval 映射](https://github.com/GNOME/mutter/blob/46.0/src/backends/native/meta-virtual-input-device-native.c)。
