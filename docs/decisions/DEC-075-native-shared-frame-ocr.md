# DEC-075：原生共享图像 OCR 与真实能力说明

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-10 第三增量、M7-14；承接 DEC-057/073/074

2026-10-15 局部修订：[DEC-090](DEC-090-continuous-task-execution.md) 不再以整个
主任务累计帧数终止；[DEC-091](DEC-091-local-semantic-perception-and-action-loop.md)
将观察取帧/分析复用与动作后强新鲜度分开。M9-03/06 跟踪实现，以下为历史契约
与证据；权限、受管 worker、单次预算和动作后新证据要求继续有效。

新用户记录已使用最新产品：application.open_uri 本地重验与进程局部验证成功，
ScreenCast 连续供图成功；desktop.vision.ocr 两次失败。根因是 Runtime 的 OCR 回调
仍使用只支持全局显示坐标的 list_displays/refresh 路径，原生 Portal 没有这类 ID。
模型还把无输入坐标误解成无读屏能力，并请求当前工具无法取得的 window id。

1. 工具按真实后端注册：原生桌面不再注册 configured-display OCR。已有 PP-OCR
   pipeline 额外提供已授权图像的只读分析接口，经同一个 Mirador PerceptionSession
   与现有 Executor blocking worker 执行；不再枚举显示器、不另建推理实例/并发设施。
2. 原生 desktop.vision.ocr 接到本轮 DesktopImageTurn 的共享会话；先重新判断权限并
   捕获新图，首次按 DEC-074 触发一次授权，其后复用同一会话。新图取得后起算最长
   五秒的分析期限，不把用户等待授权计入推理时间。每次新图计入八帧上限。
3. OCR 使用已供给的最长边 1600 预览，单图 <=8 MiB，文本框声明 image_pixels；
   返回 source artifact/epoch/维度、模型版本、backend_calls 和最多 128 区域/
   每段 1024 字节/总 16 KiB。不发布 VisualReference、display/window id 或可执行
   坐标，不把 OCR 文本当可信指令或任意网页任务成功证明。
4. 文本模型也可调用此 OCR，但不暴露或供应不支持的 ImagePart；识图模型同时获得
   本次 OCR 同源的新图。图片/文本不写普通持久日志，UI 只保留有界摘要/错误码。
5. 正常取消、超时、拒绝、异常、epoch/授权代际变化、模型失败和关闭保持已有
   生命周期。分析失败不生成文本证据；可在未撤销的同轮共享中新观察纠正，未知/
   不确定动作的 fence 不变。缺 pipeline 时不暴露 OCR。
6. 当前能力说明明确区分“可读图/OCR”和“可操作键鼠”。不请求不存在的 window id，
   被 Mirage 窗口遮挡时可以说明并请求用户移开窗口；无输入能力仍不能自动移窗/点击。
   中文任务的可见回复保持中文。既有 X11 OCR 与后条件契约不变。

验收：canonical ModelLoop 的共享→OCR→文字+同源图片、文本模型、恢复与关闭；
真实 PP-OCR/ncnn 的图像接口及没有显示器的 Runtime 接线；权限/引用/预算/异常/
超时/取消/shutdown/故障注入；Debug/Release、ASAN/UBSAN/TSAN；真实 Wayland
需要维护者配合一次共享。仅修复读屏，不宣称键鼠或原视频业务已经完成。

实现与执行证据见[原生共享图像 OCR 验收](../compatibility/native-shared-frame-ocr-20261011.md)。
真实 GNOME 配合验证和在线业务仍开放，由 M7 计划跟踪。
