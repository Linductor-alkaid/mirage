# 2026-10-11 最新产品会话失败诊断

> 状态：诊断完成；只读 Wayland 观察增量已复现，输入与在线业务复验开放
> 工作项：M7-14 / M7-15；原生 Wayland 接入属于 M7-10
> 缺陷：BUG-20261011-001
> 负责人：Mirage 维护者
> 依据：DEC-015 能力如实收缩、DEC-071 应用运行不等于页面完成、DEC-072 GUI 回退

## 会话事实

只读检查实际产品 session-state，最新一轮起始记录为 2026-10-11 11:49:44 +08:00。
用户在原有“打开 bilibili 找视频”会话中要求“再试一次”。终态 failed，错误为
`turn budget exhausted before a final answer`。共 16 个模型步骤、17 个工具调用；
7 个调用失败（2 个应用启动、5 个截图）。未调用区域点击、键盘或文字输入工具。

1. application.list 声明 Firefox running=true；window.list 三次均只有 ChatGPT
   与 Mirage，没有 Firefox 窗口。
2. application.open_uri 成功派发，局部后条件为 `application_running_only`。
   这证明进程存在及已接受链接派发，不证明出现可观察浏览器窗口或页面加载。
3. 等待 5 秒后，模型使用 epoch=1 的旧应用观察启动 Firefox，得到
   `stale_observation`；重新应用观察后再次启动，得到 `already_running`。
   两次均明确未派发、无副作用，不是输入不确定性封锁。
4. 显示器为 eDP-1、3200×2000。三次整屏截图、一次 ChatGPT 窗口截图均为
   `io_error`；第五次采集请求被每轮四次采集预算拒绝。
5. 三次 wait 合计 13 秒。模型继续同路径等待/截图，最终耗尽 16 步。
   没有新的页面证据，用户业务目标未完成。

本轮记录没有模型认证失败；不能据此推断凭据跨重启问题的所有情况均已关闭。
原生 Release service 于 11:42 重建，早于本轮，但分析时没有运行中的 Mirage
进程，无法核对当时已加载二进制的 SHA，不将新 GUI 工具实际注册视为已取证。

## 宿主只读复现与源码定位

11:53 检查当前会话：XDG_SESSION_TYPE=wayland，WAYLAND_DISPLAY=wayland-0，
DISPLAY=:0。Firefox 进程的 GDK_BACKEND=wayland，桌面为 GNOME。
X11 根窗口查询成功，几何 3200×2000、深度 24；不保存图像的 1×1 像素
XGetImage 探针返回空指针，X 错误为 error=8（BadMatch）、request=73（GetImage）。

源码路径为 platform/linux/src/x11_backend.cpp 的 capture_region：所有 display、
window、ROI 采集均读取 X11 根窗口。LinuxDesktopEnvironment 当前只以连接成功
判定 screen/input/window Provider 存在，未区分根窗口可采集与 XWayland 局部能力。
desktop_image_supply.cpp 返回底层 error.code，丢失 XGetImage 的具体错误消息。
model_layer.cpp 的模型步骤上限为 16，工具执行上限为 32。

当前环境已直接复现截图失败。结合会话相同的显示器、窗口列表与错误，最新任务
受 Wayland/XWayland 能力边界影响是强证据推断；产品历史没有保存当时的会话类型，
不能将当前探针冒充任务执行时的完整环境记录。原生 Wayland Firefox 不在 X11
窗口表中，与当前运行模式一致；未证明 open_uri 在 Firefox 内部加载成功或失败。

## 推进顺序与验收要求

1. **M7-14 / M7-15：能力判定和诊断。** 在 Platform Backend 区分 X11 连接、
   窗口覆盖范围、可采集根图及可注入目标；稳定不支持时提供明确原因和恢复条件。
   不以 DISPLAY 存在声明完整桌面能力，也不把该问题登记为 mira/mirador 缺口。
   模型错误应保留有界、脱敏的可操作原因；设置/会话应呈现实际受限能力。
2. **M7-15：有限恢复与终态说明。** 相同确定性平台失败不得通过 wait 重复请求；
   只在存在新的观察或恢复条件时重新采集。旧 scope/epoch 必须先新观察；
   running=true 不应反复 plain launch。没有授权且可执行的替代观察路径时，
   保留已派发动作证据并明确任务未完成，不能以笼统预算耗尽取代原因。
   不重发已接受但页面结果未知的 URI 动作，不靠扩大预算绕过平台限制。
3. **宿主业务复验。** 当前已验证主路径为 Xorg；维护者可注销后选择 Ubuntu on
   Xorg，重新启动 Mirage 与 Firefox，再验证截图、GUI 输入和页面最终证据。
   此操作是当前版本验收条件，不需要重新配置 OCR/ncnn 或下载模型。
4. **M7-10：原生 Wayland 产品能力。** 按平台适配边界设计受授权的采集与输入，
   使用已有 Runtime/Executor 生命周期，验证授权、拒绝、撤销和会话关闭。
   不能仅以切换 Xorg 宣称默认 Wayland 桌面已支持。

本轮只分析日志、源码与只读宿主探针，没有执行用户桌面输入、启动应用、访问网址、
修改配置、改写历史或业务实现。以上修复与 Xorg/Wayland 业务验收均未执行，
负责人为 Mirage 维护者，补跑条件为相应实现与目标会话准备好。

## 2026-10-11 后续修复证据（DEC-073）

默认原生 Wayland 不再将 XOpenDisplay 成功当作全桌面 window/input 能力，改用
Portal 只读观察。Screenshot Response=2 不能统称权限拒绝：用户完成截图，实际 PNG
3200×2000 已保存，GNOME 报 InteractiveScreenshot didn't return a file；Mirage 没有
收到 URI。已改用用户选择显示器的 ScreenCast/PipeWire，宿主取得同尺寸一帧并关闭
共享。该结果关闭读屏传输阻塞，未关闭原 Bilibili 在线业务与原生键鼠门禁。
见[独立验收](wayland-portal-observation-20261011.md)。以上历史诊断范围保持原始事实。
