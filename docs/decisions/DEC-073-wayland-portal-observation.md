# DEC-073：原生 Wayland Portal 观察与能力边界

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-10 首增量；关联 M7-14、M7-15、BUG-20261011-001

用户授权继续接入 Wayland。当前 GNOME 原生会话根窗口 XGetImage 返回 BadMatch，
X11 窗口表也无法覆盖原生 Firefox；连接 DISPLAY 成功不是完整桌面能力。

1. Linux 产品自动选择会话后端：启用桌面且未指定私有 X display 时，原生 Wayland
   优先使用 XDG ScreenCast/PipeWire 单帧观察，不暴露 X11 window/input。仅编译时缺
   PipeWire 或授权前确认 ScreenCast 接口缺失时使用 Screenshot；请求失败后不得自动
   换协议再次弹窗。显式 display 的 Xorg/Xvfb
   路径保留；默认无桌面能力的构造不触发 Portal。缺编译依赖或总线时明确不可用。
2. Screenshot Portal 只返回图像 URI，缺少受控输入会话坐标。因此 ScreenProvider
   增加平台无关的桌面观察能力查询及 capture_desktop 默认拒绝实现；首增量返回
   只读 BGRA 图像。不得将像素坐标包装成物理显示器坐标，不提供虚构窗口或显示器。
   工具按能力暴露 source=desktop，无 source_id、ROI、SoM 或区域点击。
3. desktop.screen.capture 先经过现有 screen.capture 权限；Portal 的系统授权保持
   独立，ScreenCast 由用户选择一个显示器共享；Screenshot 以 interactive=true 请求
   系统截图界面，用户选择内容；用户取消、拒绝、
   缺 Portal、超时分别为明确错误。每轮一次 Portal 请求；
   失败后不再次弹授权，模型应说明原因。授权等待最长 30 秒，遵循父取消/任务期限，
   结果到达后重新起算五秒供图期限；用户等待不是图像采集时间。
4. Request Response 先订阅再调用，核对 sender、request path 和回复类型，取消或
   超时调用 Request.Close，迟到回复丢弃。临时 GMainContext 在调用方 Executor
   有限任务内推进；回调只接纳一个有界结果，不启动线程、定时器或私有调度器。
5. URI 仅接受本地 file，读取用户所有的 regular file，拒绝符号链接/设备/FIFO，
   压缩数据 ≤16 MiB、解码像素 ≤调用方预算及32 MiB，尺寸有上限。平台 libpng
   负责 PNG 解码；核对 pinned Mirador 公开接口没有 PNG 解码能力，不重复实现
   视觉推理。文件名、URI 和图像不得写入普通日志或仓库；文件由 Portal 管理。
6. GNOME 宿主证据：2026-10-11 14:28 用户完成截图，PNG 已保存，但 Portal 返回
   Response=2，日志为 InteractiveScreenshot didn't return a file；不能将其分类为
   用户未截图或拒绝权限，不能扫描截图目录作为隐式授权替代。
7. 首增量 ScreenCast CreateSession → SelectSources（单个 MONITOR、不持久化）→
   Start → OpenPipeWireRemote → 单帧；全过程共用 30 秒期限，Session.Close 在所有
   退出路径执行，响应路径/sender 校验与 Screenshot 一致。Session.Closed 或总线
   断开立即收敛。PipeWire SDK 仅在 Platform 内；调用方 Executor 有限任务推进
   pw_loop，不用 pw_thread_loop 或 RT_PROCESS。回调只保存有界格式/状态和最多一个
   buffer；像素复制及业务处理在回调返回后进行，像素格式、stride、offset、chunk
   与预算均校验。每次捕获销毁 stream/core/context/loop；Platform owner 平衡
   pw_init/pw_deinit。首帧取得即关闭共享，不保存授权 token，不开放输入坐标。
8. 后续 RemoteDesktop + ScreenCast/PipeWire 联合会话单独增量：系统授权、会话
   关闭/撤销、设备释放、流像素与逻辑坐标对应先冻结，再开放键鼠能力。不得用只读
   Screenshot 图像驱动 XTest，也不得以首增量宣称 Wayland GUI 任务已完成。

验收：私有 D-Bus Portal 夹具覆盖成功、早响应、拒绝、取消、超时、错误路径/URI、
坏 PNG、文件/像素预算、迟到结果；实际会话工具供图与权限/重复请求门禁；X11
回归、ASAN/UBSAN 和 TSAN。真实 GNOME 需要用户对系统截图提示操作，独立记录；
未执行保持开放。M7-10 为 In Progress。

接口依据：[Screenshot](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)、
[Request](https://flatpak.github.io/xdg-desktop-portal/docs/requests.html)、
[ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)、
[PipeWire](https://docs.pipewire.org/page_tutorial5.html)、
[RemoteDesktop](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)。

2026-10-11：成功 ScreenCast 的一次性 Session 生命周期由
[DEC-074](DEC-074-task-scoped-wayland-observation.md)扩展为任务内复用；失败不重复授权、
只读图无输入坐标等边界保留。原首增量验收记录不改写。
