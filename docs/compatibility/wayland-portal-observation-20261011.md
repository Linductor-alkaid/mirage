# 原生 Wayland Portal 观察：2026-10-11 首增量

> 工作项：M7-10（In Progress）；关联 M7-14 / M7-15、BUG-20261011-001
> 决策：[DEC-073](../decisions/DEC-073-wayland-portal-observation.md)
> 范围：GNOME 原生 Wayland 单帧观察；X11/Xvfb 输入回归

## 宿主结果与失败定位

用户确认看到 GNOME 截图界面并已点击截图。14:28:48 生成 PNG，文件为 1,100,683
字节、3200×2000；只读取文件元数据和 PNG 头，未查看或复制用户图像。Screenshot
Portal 返回 Response=2，GNOME 日志为 `InteractiveScreenshot didn't return a file`。
保存成功与 URI 回传失败是两个事实；没有定位 GNOME 内部为何未返回 URI，不将其
误判为用户取消或权限拒绝，也不扫描截图目录作为自动采集来源。

首增量转用授权 ScreenCast/PipeWire：实际 GNOME Portal ScreenCast v5，系统 PipeWire
1.0.5。用户选择显示器共享，Platform 取得 3200×2000 BGRA 图像，7 checks 通过。
最终 Native Release 的 canonical `desktop.screen.capture` 调用、1600×1000 预览、
Mira ImagePart 实际供给及 PNG 内存资产、只读标记、无坐标/image_scope、重复请求
门禁共 17 checks 通过；不使用模型或用户凭据。只保存维度、错误码和检查计数，
未保存屏幕图像。获取一帧即销毁流并关闭共享 Session、私有 D-Bus 连接。

## 产品行为与工程边界

自动选择的 Wayland 会话使用 Portal ScreenProvider，Window/Input/Clipboard Provider
不把 XWayland 当作完整原生桌面；显式私有 display 保留 X11 路径。工具只开放
`desktop.screen.capture({"source":"desktop"})`，没有虚构显示器 ID、ROI、SoM 或区域
动作。实际模型识图能力及 screen.capture 权限仍独立判定，授权完成后起算五秒供图
期限，received_at_ms 不冒充采集时间。每轮只请求一次；失败不得等待后重复弹窗。

授权前优先检查 ScreenCast；Get 失败还需有界 Introspect 明确接口不存在才使用
Screenshot，协议错误/权限拒绝不能切换协议重复授权。CreateSession、SelectSources
（单显示器、不持久化）、Start、OpenPipeWireRemote、首帧共用 30 秒期限，取消/
超时/Session.Closed/总线断开均收敛。所有工作在现有 Executor 有限任务内；GMainContext
和 pw_loop 是私有平台事件循环。回调仅接纳有界协议状态/一个 buffer，像素校验与
复制在回调外执行，不使用 pw_thread_loop/RT_PROCESS 或自建调度设施。

PNG 解码使用平台 libpng；流只接受有界单平面 CPU packed RGB，校验尺寸、预算、
stride、chunk offset/extent，拒绝异常帧/DMA-BUF。供图复用 Mira、Mirador resize_area，
没有改 pinned 依赖。平台/Runtime/Core 公共契约没有 GLib/PipeWire 类型。

构建者可安装发行版 libpipewire-0.3-dev / libspa-0.2-dev，正常 pkg-config 自动发现。
本机未安装系统开发包：从发行版下载 1.0.5-1ubuntu3.3 的 SDK，在 /tmp 解包后通过
MIRAGE_PIPEWIRE_SDK_ROOT 编译，对系统已安装的运行库链接。产品打包要求该能力已编译，
DEB 自动依赖 libpipewire/libpng 并推荐 Portal 与桌面后端；最终用户无需配置 SDK 或
OCR/ncnn。具体 DEB 的二进制/依赖与 SHA 在机器证据中记录，没有安装到系统。

## 自动验证与过程问题

私有总线夹具覆盖 Screenshot 24 场景、ScreenCast 17 场景；另有 18 个 CPU 像素
边界检查：成功/早响应、取消/拒绝/超时、请求路径核对、缺 session/streams、
多流/无效 node、缺 FD、撤销/迟到/异常类型；PNG 本地 URI、symlink/FIFO/文件与像素
预算；实际工具权限/供图/重复授权。6.1 秒模拟授权仍可供图，3200×2000 缩放正确。
取消和协议错误核对 Request/Session.Close；夹具均不接触用户桌面。

Debug 全量 67/67；末轮五个配置 Portal 定向各 3/3（41 场景、18 像素边界检查）。Native Release 全量 73/73；ASAN、UBSAN、TSAN 定向各 7/7（含两个隔离 fixture）；
ASAN 使用 detect_leaks=0，未宣称 LeakSanitizer 验收；TSAN 沿用项目系统库抑制，
真实 PipeWire 正例为 Debug/Release 宿主验证。末轮取消前资源清理复验与最终日志以[机器证据](data/wayland-portal-observation-20261011/summary.json)为准。
实际 Release 与 DEB 解包 OCR/workflow 各 37/37（11 个 workflow，模型关闭）；所有
CTest/产品验证使用私有 XDG state/config/data，私有 Xvfb 显式 XDG_SESSION_TYPE=x11。

过程负例保留：首轮总线 socket 1 秒读取超时误伤人工授权，修正为鉴权完成后清除
socket timeout，有限请求自己管理期限；截图 Response=2 分类改为 portal_failed。
一次 CTest 在旧 binary 上执行，method_denied 的旧分类失败；最终构建后复验。
CPU 测试把 16 字节图像预算误写为 31 字节拒绝，已修正为 15 字节。产品夹具首次
选错 /usr/bin/python3 缺 Xlib，使用已有 Miniconda 测试环境复验，没有安装额外运行库。
首次 TSAN 使用未重新配置的 CTest，缺少项目已有系统 GLib/GIO 抑制，产生系统库报告；
重配后沿用原 suppression，不新增抑制。末轮审查补充 CreateSession 成功后、
SelectSources 调用前撤销的负例，提前取消时也清理未提交的 GVariantBuilder。首次公共头检查错误地包含专用 Integration
适配头，改按 Core/provider 边界检查，53 头无第三方 include。构建与测试重叠的中间
Release 全量不作为最终验收；重新完成最终完整测试。

## 使用和未完成范围

从托盘完整退出 Mirage，再从现有桌面入口重启，新建支持识图的会话；允许桌面截图，
在系统共享窗口选择显示器。可请求“观察当前屏幕并说明看到的内容”。这轮是只读观察，
不能以此宣称原 Bilibili 业务已完成。用户配置/历史 SHA-256 与 mtime 保持，未改真实
凭据、Portal 权限或桌面设置。

RemoteDesktop 键鼠、持续共享会话/恢复 token、流与逻辑坐标映射、多显示器/DPI/热插拔、
DMA-BUF、KDE/wlroots、在线模型视觉规划及原浏览器业务仍开放；负责人 Mirage 维护者，
补跑条件为后续输入会话实现与对应目标桌面准备好。M7-10 / M7-11 保持 In Progress，
未提交、推送、合并或发布，不以只读观察关闭完整 Wayland 能力。
