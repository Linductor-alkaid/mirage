# DEC-105：GNOME Wayland 会话的桌宠避让

> 状态：Accepted
> 日期：2026-10-18
> 负责人：Mirage 维护者
> 冻结里程碑：M8-06

## 决策

维护者要求继续支持 Wayland 桌宠避让。首步覆盖 GNOME Wayland 会话中现有
XWayland 桌宠与 Portal ScreenCast 的组合，不改变纯 X11 的 Shape 租约。
纯原生 Wayland 桌宠及其他 compositor 单独验收，不通过删除环境检查冒充支持。

继续使用受管 PID、XRes、协议标记与唯一窗口验证。桌宠声明 UTILITY 类型，GNOME
Shell 对此类型不执行普通窗口映射/销毁动画。Mutter 禁止最小化 UTILITY 及
skip-taskbar 窗口，因此平台采用 XWithdrawWindow，等待 WM_STATE 为 WithdrawnState、
服务端窗口已 unmapped 且 WM 的 _NET_CLIENT_LIST 已移除窗口。原本已经隐藏的
受管 IconicState 或 WithdrawnState 窗口保持隐藏。租约期间反复验证，最后释放
恢复原映射状态、置顶等 WM hints 和原几何，不主动激活窗口。

Host 显式创建共享的 Platform PresentationContext，交给受管 pet 与 Linux Screen
Backend；它只持有资源的单调时间水位，不创建线程、队列或调度器。WM 确认隐藏后
记录 CLOCK_MONOTONIC 水位。Portal 的 PipeWire consumer 在水位改变时重新激活
既有 stream，丢弃旧缓存，要求完整帧的 SPA_META_Header.pts 晚于水位。缺时间戳、
过期帧、无法隐藏/恢复、取消与超时都报告明确失败，不能退回 XWayland 根窗口截图。
恢复只发生在最后一个租约释放之后；已取消路径仍清理，进程关闭不等待动画。

使用 pinned Mira 已有工具装饰、取消与 kairo 生命周期；Mirador 不提供窗口生命周期
或 Portal 帧来源能力，产品平台集成由 Mirage 承载，不修改任何依赖。

## 依据与限制

[EWMH](https://specifications.freedesktop.org/wm/latest-single/) 定义窗口类型和隐藏状态。
[GNOME Shell 窗口动画实现](https://github.com/GNOME/gnome-shell/blob/gnome-46/js/ui/windowManager.js)
只对普通及对话框类型执行相关动画；[Mutter 窗口能力](https://github.com/GNOME/mutter/blob/gnome-46/src/core/window.c)
禁止最小化非普通/skip-taskbar 窗口，因此不能使用 Iconify 请求代替撤下。
这些事实限定适配范围，不能泛化为其他 compositor。
[Mutter 46 的 ScreenCast 实现](https://github.com/GNOME/mutter/blob/gnome-46/src/backends/meta-screen-cast-stream-src.c)
将源帧时间写入 Header PTS；实机仍须确认时间域和隐藏后的实际像素，不将离线协议
fixture 记为真实 compositor 通过。非标准主题/扩展的窗口动画仍需实机验收。

## 验证

私有 Xvfb 配受控 WM 验证撤下确认、嵌套租约、已隐藏恢复、身份与窗口变化、取消、
拒绝/超时、清理；真实 PipeWire loop 的私有 buffer fixture 验证旧缓存、排队旧帧、
缺 PTS、损坏帧与新帧水位。测试运行在现有 kairo 有限任务中，增加 sanitizer 回归。
GNOME Wayland 原生桌面取证和维护者重启实测保持 M8-06 退出门禁。
