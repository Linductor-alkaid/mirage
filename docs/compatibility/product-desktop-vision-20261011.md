# M7-12 桌面视觉产品启用验收

> 状态：Verified（Linux Xorg 产品启用预览；其他 M7 门禁独立保留）
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：[M7-12](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-066](../decisions/DEC-066-product-desktop-vision-settings.md)

## 交付范围

原生设置新增「桌面工具」，可选择/填写本地 OCR 清单，保存、重新读取并分别显示
编译支持、当前管线和待重启配置。`vision.get/set` 合并持久化已有模型、权限、目录与
Runtime 配置；相对路径、控制字符、超长路径和损坏旧文件明确拒绝，读写失败不报告
成功。保存后由下一次托盘 owner 启动校验资源；本轮未增加热切换、并发 owner 或调度器。

修正产品根实际未启用 Linux Desktop Provider 的集成遗漏：产品显式探测 X11、AT-SPI2、
Application 与 Notification；不可用保持缺失。headless 默认不改变，显式 OCR 需要
Screen 探测。现有 Mira ConversationLoop、Mirador PP-OCR、Executor 生命周期与权限
门禁继续承载执行，本轮没有修改 pinned 依赖或锁定指针。

新增 native-vision-debug/release 预设。统一启动器向新托盘传递清单，覆盖持久化值；
已有托盘拒绝新覆盖，普通启动复用。显式空覆盖可暂时关闭 OCR，恢复坏资源后的设置
访问，并不改写保存值。普通 native 构建仍默认关闭 OCR；模型、字典不自动下载/分发。

## 实际产品验证

外部 Python owner 在私有 XDG/DBus/Xvfb 中启动实际 mirage → tray → native，使用真实
PP-OCR 权重。没有操作用户桌面、改变用户配置或安装系统依赖。Xvfb 1500×1000、
窗口 1180×820 / 最小 860×620；Linux 7.0.0-34-generic、GCC 13.3、CMake 3.28.3。
Release 通过此前已缓存的 pinned NCNN source 构建，XRes 使用既有临时 SDK；资源沿用
[审核清单](../supply-chain/real-ocr-resources-20261009.md)，未纳入仓库。

- 带视觉 Release 无模型运行 19/19 checks：原生保存、保持模型配置、重启加载、实际
  产品 OCR 识别公开 Canvas「任务已保存」、已有服务覆盖拒绝、普通启动复用、保存关闭
  待重启、显式启动覆盖、坏资源拒绝、空覆盖恢复并清空配置。
- 普通 native-debug（OCR OFF）11/11 checks：配置可保存但重启加载明确拒绝；空覆盖
  恢复入口展示不支持/未运行，并可清除待启用路径。
- 带视觉 Release 的完整在线产品运行 23/23 checks：实际原生输入被接纳，MiniMax-M3
  经 anthropic.messages.v1 成功调用 desktop.vision.ocr 和 desktop.vision.mark，工具均
  complete、服务轮次 ok，读出「会话」「默认权限」。本轮在默认会话权限下提交只读任务，
  仅有这两个观察工具调用，没有桌面修改；不以此代替真实修改任务后条件验收。
- 明暗主题最小窗口检查通过，路径、重启说明、模型识图提示及平台范围可读。工具截图
  是服务终态到达后的第一帧，整体状态栏尚显示回复中；服务历史是该轮终态事实源。

[最小浅色设置页](data/product-desktop-vision-20261011/settings-min-light.png)、
[最小深色设置页](data/product-desktop-vision-20261011/settings-min-dark.png)、
[保存/待重启](data/product-desktop-vision-20261011/settings-saved-light.png)、
[原生工具过程](data/product-desktop-vision-20261011/conversation-result.png)。
[完整机器回执](data/product-desktop-vision-20261011.json)包含检查、源文件/日志摘要与 artifact
摘要；[合成会话结果摘要](data/product-desktop-vision-20261011/native-history.json)保留工具状态与计数，
原始过程回执仅在本机保存，不进入 Git；
[实际产品观察](data/product-desktop-vision-20261011/observation.json)保留公开语料区域。

## 回归和失败历史

| 检查 | 实际结果 |
| --- | --- |
| 本轮普通 native-debug / native-release 全量 | 各 65/65；29.12 / 20.76 s |
| 收尾错误诊断/读写失败场景后 Debug / Release 定向 | 各 5/5；13.04 / 11.72 s |
| 新 native-vision-release 全量 | 61/61；77.56 s（GTK 外部验收选项默认 OFF） |
| 新视觉 Release 补建最新服务失败场景 | runtime_service_test 1/1；9.69 s |
| 最新 ASAN+UBSAN / detect_leaks / halt_on_error / 串行 | 5/5；17.02 s |
| 最新 TSAN / halt_on_error / setarch -R / 串行 | 5/5；18.79 s |
| IPC golden | v15；配置请求/响应与缺字段负例通过 |
| 静态检查 | 11 文件格式；原生改动行零格式违例；Python 编译和 diff check 通过 |
| 公共头检查 | 53 个，本轮新增第三方暴露为零；既有 MiraHost 的 kairo 签名保留 |

定向五项为 runtime_service_test、ipc_protocol_test、ipc_protocol_golden_test、
persistence_test、harness_desktop_tools_test。原生文件保留 30 条既有格式诊断；没有混入
无关全文件格式化。公共头检查是相对基线的检查，不能解释为全仓没有第三方签名。

首次 golden 结构化 fixture 漏请求 builder，修正后全部重跑通过。启动初验暴露 Screen
未启用，已在产品根修正。初始本地 SSE 方案被 Mira loopback 地址策略拒绝，未放宽策略；
后改为既有授权的 HTTPS MiniMax。首次在线配置 API prefix 错误，修正为 /anthropic/v1。
早期原生脚本点击/选会话不当导致未提交，随后又误把终态 ok 等待成 completed；这些
脚本失败不能归因于模型耗时。修正为显式返回会话、核对真实接纳和协议终态后，最终运行
完整通过；此前受错误谓词影响的成功服务历史及失败日志仍保留在 /tmp 对应目录。

Debug 对同一 1500×1000 产品全屏 OCR 返回 cancelled_or_expired，Release 正例通过。
新增有界错误原因帮助区分期限、epoch 和 backend 失败；五秒观察/引用预算保持。
这不是 Debug 性能问题的完整隔离或运行时间保证，完整权重 TSAN 与宿主全屏/DPI 门禁
继续开放。所有私有产品进程已清理，残留产品 socket 为零；没有提交、推送或发布。

## 复现与后续

见[产品使用步骤](../../apps/native/README.md#桌面工具预览m7-12--linux-xorg)。私有验证：

```bash
python3 tests/manual/product_vision_acceptance.py \
  --build build/native-vision-release \
  --manifest /absolute/path/manifest.json \
  --output /tmp/mirage-product-vision-new-run
# 可选的在线轮次：仅使用已授权的 HTTPS MiniMax 配置目录；凭据只进私有子进程环境。
# 在上述命令追加 --provider /path/to/authorized/minimax
# 普通 OCR OFF 构建负例：改 --build build/native-debug 并追加 --unsupported-build
```

需 Xvfb、dbus-run-session、Python Xlib/Pillow，SNI watcher 使用系统 Python GI；资源须
事先准备并审核。在线运行会调用真实供应商，不自动重试。后续由 Mirage 维护者在目标
Linux Xorg 上，按具体应用准备初态并确认权限，验证实际任务结果和停止/接管行为。
M7-07/09 既有门禁、Windows/原生 Wayland、任意应用/DPI/跨屏与本地知识/workflow 组合
不在本项关闭范围。M7-12 仅闭合 Linux 产品启用入口，不代表整套 M7 已完成发布验收。

## 同日默认运行包后续

本文记录 M7-12 当时的显式启用验收。M7-13 / DEC-068 随后将 Linux 原生产品
默认改为自带已验 OCR 包，覆盖本文的普通 native 默认关闭/不分发资源说明；
原有显式设置、关闭和坏配置恢复路径继续保留。最终证据见
[默认包与会话修复验收](bundled-vision-and-tool-recovery-20261011.md)。
