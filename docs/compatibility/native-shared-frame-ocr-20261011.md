# 原生共享图像 OCR：2026-10-11

> 工作项：M7-10 / M7-14（In Progress）
> 决策：[DEC-075](../decisions/DEC-075-native-shared-frame-ocr.md)
> 负责人：Mirage 维护者

## 最新用户记录

会话 `1a6b966e3a734347ab039b37fb32d782`、turn
`478f15f14c380204f39827833ce916b4`，2026-10-11 16:57:52.097 +08。
application.open_uri 本地候选重验和进程局部验证成功；两次 screen.capture 都成功，
来源 3200×2000、预览 1600×1000；两次 desktop.vision.ocr 都返回旧的通用失败。
实际加载 tray/native SHA 与上一增量 Release 相同，排除旧版本和系统共享失败。

根因是 native OCR 仍走 configured-display PpOcrPipeline::refresh，Portal 没有全局
显示器 ID。模型把没有输入坐标误解为无法读屏，又请求当前工具不提供的 window ID。
这属于 Mirage Adapter 接线错误，已有 Mirador 能分析图像；不构成上游能力缺口。
只保存状态、尺寸、ID 和哈希，未保存用户图像、OCR 文字、URL 或凭据。

## 实现与自动验证

Native OCR 从任务共享捕获新图，经过已有 PP-OCR/ncnn pipeline 的只读图像接口，
不枚举显示器、不改 registry、不生成可点击引用。返回 image_pixels、来源 artifact、
epoch、尺寸和模型版本/调用计数；识图模型取得同源 ImagePart，文本模型仅取得文字。
每次检查 screen.capture 权限，与截图共用一次共享及八帧预算；授权结束后再计算
五秒分析期限。正常取消、接管、终态关闭，分析失败可在仍有效的共享内重新观察恢复。

Canonical Mira loop 新增 16 类 OCR 场景：识图/文本模型、失败后恢复、权限拒绝、
分析失败/异常、取消、epoch/接管换代、NaN/越界、区域/单段/总量预算、非法参数、
模型异常。真实 PP-OCR/ncnn 的 synthetic 权重验得“中A”，验证不枚举显示器、
不改变 registry、预取消/过期/非法图像/停止，并经实际 Runtime 的无显示器后端
调用 canonical OCR 成功；持久工具摘要不包含识别文字。

Debug 全量 67/67，Native Release 全量 73/73。ASAN/UBSAN/TSAN 各五个定向
测试和状态隔离两项 7/7；Portal 夹具另行各 3/3（含状态隔离两项），覆盖 43 场景；结果记录在机器摘要。
ASAN detect_leaks=0，UBSAN 独立配置，TSAN 使用既有 GLib suppression 与
setarch x86_64 -R，未新增 suppression。这三种配置未启用 PP-OCR/ncnn：它们
覆盖 Adapter、共享、DTO、取消与生命周期；真实推理由 Native Release 和产品夹具
验证，不宣称 sanitizer 覆盖真实权重路径。

Release 与解包 DEB 各 37/37，11 次零模型产品 workflow，默认固定 PP-OCR/ncnn、
Canvas 六次实际点击和两轮 Runtime owner 退出均通过。包内四个二进制 SHA 与
Release 相同，包为 out/packages/mirage_0.1.0_amd64.deb；用户不需要配置 SDK 或
OCR 环境。X11 Canvas 证据不代表 Wayland 输入可用。

构建中两处测试编译错误（变量作用域、参数 shadow）和新增推理改变计数期望引起的
一次 Release 测试失败已修复，并补跑上述验证。clang-format 与 git diff --check
通过，27 个 Core/Platform 公开头未包含平台/依赖类型头，真实用户状态两文件 SHA 保持；未修改 pinned 依赖或用户配置。

## 宿主验证与后续

真实 GNOME 原生 OCR 验证待维护者一次共享：
`build/native-release/tests/portal_screen_test --probe-host-ocr` 后接默认 manifest
绝对路径。探针首图、等待 6.1 秒/动作换代后新图、第三帧 OCR 共用一次共享，核对
OCR 同源 ImagePart，再验证 takeover/resume 拒绝旧共享；只记录尺寸和计数，不
保存用户屏幕/文字或调用在线模型。已发出配合请求，未把未执行项标为通过。

运行中的托盘仍加载此前版本；从托盘完整退出后通过现有桌面入口启动，即加载本地
修复版本。关闭聊天窗口本身不能升级 Runtime。原在线视频业务、RemoteDesktop
键鼠、逻辑坐标/多屏/DPI、KDE/wlroots 仍待后续增量；负责人 Mirage 维护者，需
相应目标环境和系统授权。未提交、推送、合并或发布，整体工作项保持 In Progress。

机器证据：[summary.json](data/native-shared-frame-ocr-20261011/summary.json)。

## 后续真实用户证据

2026-10-11，新会话 `5c555168634944d8b708b81b7e7b6668` 的 turn
`49ad94f913d2fa3ef6e9d0dcb5b1ec6f` 在实际加载 DEC-075 Release 后
`desktop.vision.ocr` 返回 complete，106 区域、backend_calls=2、原生 image_pixels
且同源 artifact。真实默认模型指纹与固定 bundle 一致，排除接线未生效。此证据
证明产品实机原生 OCR 成功，不替代探针对三次连续 OCR/接管的断言；另外出现输出
预算拒绝和下一轮取图超时，改由 DEC-076 跟踪。见
[后续诊断](native-ocr-budget-and-capture-diagnostics-20261011.md)。

DEC-076 完成后的独立 GNOME 探针已通过 58/58：一次共享、连续三次 OCR（各
106 区域）、同源 ImagePart 和接管关闭。原先“探针待配合”是历史状态；最新
证据及两轮失败过程保留在上面的后续诊断，未把 Portal 超时声明为已修复。
