# 任务内 Wayland 共享与应用恢复：2026-10-11

> 工作项：M7-10 / M7-14（In Progress）
> 决策：[DEC-074](../decisions/DEC-074-task-scoped-wayland-observation.md)
> 负责人：Mirage 维护者

## 真实用户失败

最新会话 `1a6b966e3a734347ab039b37fb32d782`、turn
`86ad31ab18972fb6fe032a4a273d44f4`，记录时间 2026-10-11 16:27:29.863 +08，
用户重启后点击共享。application.list 成功，screen.capture 成功，原图 3200×2000、
预览 1600×1000。随后 open_uri 使用旧 scope，返回 stale_observation：
dispatched=false、side_effect_may_have_occurred=false、input_released=true。
没有实际派发浏览器操作。加载的 tray/native 与当时 Release 哈希相同，不是旧二进制。
只读取脱敏尺寸/状态，不保存用户屏幕、URI、密钥或完整会话日志。

旧五秒应用候选在共享/模型等待后过期；旧单帧共享也不允许操作后的再次观察。
新实现本地重新核对最多 60 秒候选，仍要求最新 scope/session/task/epoch；保留
任务独有共享供新帧，普通动作只淘汰旧图片，接管/取消关闭共享。最后一次派发前拒绝
仍失败，诊断明确该次操作没有副作用；不得吞掉或重发未知/不确定错误。

## 实际 GNOME

维护者配合选择显示器并点击共享，执行 Native Release `portal_screen_test --probe-host-tool`。
首图原始 3200×2000/预览 1600×1000，等待 6.1 秒并模拟动作换代后，同一共享取得
新 artifact；随后 takeover/resume 的旧会话被拒绝并关闭，共 22 checks、0 failures。
未触发真实键鼠或浏览器操作，未调用在线模型、读取密钥或保存图像。
日志 `/tmp/mirage-wayland-increment2-host-tool.log` 仅尺寸及计数。

## 自动验证与交付

Debug 全量 67/67、Native Release 全量 73/73；最终失败封闭与拒绝文案修正后，
Debug/Release/ASAN/UBSAN/TSAN 各复验五个相关测试加状态隔离两项，共各 7/7。
ASAN 使用 detect_leaks=0，UBSAN 独立配置，TSAN 使用既有 GLib suppression 与
setarch x86_64 -R；未增加 suppression。最终 Portal 夹具 43 场景、packed pixels
18 checks，供图 245、动作后验证 578、动作契约 72、GUI fallback 243 checks。

新增用例包含任务会话十类生命周期、五类实际 Mira canonical loop 供图/终态、
候选及权限等待各 6.1 秒后成功派发、目标消失/重复/枚举异常/取消/epoch 变化、
首失败不再次授权、创建/预取消不访问 Portal。已有 X11 的输入引用五秒过期门禁保留。

实际 Release 与解包 DEB 产品各 37/37，11 次零模型本地视觉 workflow；默认
PP-OCR/ncnn、真实 Canvas 六次点击与两轮 Runtime owner 退出保持正确。包内四个
二进制 SHA-256 均与最终 Release 一致；本地包 out/packages/mirage_0.1.0_amd64.deb。
解包夹具初次缺 native 路径/测试字体，修正 staging 和公开 fixture 字体参数后补跑
37/37；这次失败保留日志，不作为产品失败或通过结果。

公共 Core/Platform 头 46 个检查无 GLib/PipeWire/Mira/Mirador/kairo include；
clang-format 检查与 git diff --check 通过。真实用户状态两文件 SHA-256 保持，
凭据内容未暴露。机器摘要位于
[data/task-scoped-wayland-observation-20261011/summary.json](data/task-scoped-wayland-observation-20261011/summary.json)。

当前用户进程仍是重建前已加载的代码；桌面快捷入口沿用最终本地 Release。
从托盘完整退出 Mirage 后重新启动，再测试原任务。关闭会话窗口本身不会更新托盘
Runtime。重试需要一次系统共享，成功后本任务可以重新观察；无需配置新运行环境。

## 未完成边界

RemoteDesktop 联合授权、键鼠按下/释放、逻辑坐标映射、多屏/DPI/热插拔、KDE/wlroots、
原在线浏览器业务均未在此增量完成。负责人 Mirage 维护者；补跑需联合输入会话、
目标平台及维护者操作系统授权。M7-10/14/15 保持 In Progress。未修改 pinned 依赖，
未安装系统 SDK、修改用户凭据/历史、提交、推送、合并或发布。
