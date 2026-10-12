# 默认视觉运行包与会话工具修复验收

> 状态：Verified（Linux Xorg 产品默认视觉包及已识别工具故障；原会话业务任务未重跑）
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-13、M7-14；决策：[DEC-068](../decisions/DEC-068-bundled-vision-and-recoverable-tool-rejection.md)

## 会话诊断与实现

从本机两次产品历史中核对工具结果，只记录散列会话标识与错误类别，见
[脱敏回执](data/bundled-vision-20261011/session-errors.json)。未复制用户提示、回复、
窗口标题、截图或凭据；未修改用户 service.json。问题与修复如下。

| 证据/触发 | 修复与保留边界 |
| --- | --- |
| 3200×2000 BGRA 超过旧 8 MiB 预算 | 窗口/显示原始采集 32 MiB，经 Mirador resize_area 生成最长边 1600 预览；PNG 仍 8 MiB，ROI 仍 1:1 / 8 MiB；元数据提供来源几何与两轴精确比例 |
| 将 width/height 当显示缩放参数，预检连续拒绝后额度耗尽 | 工具卡明确仅 ROI 接受几何；参数拒绝不消耗四次采集额度，开始采集后仍计数 |
| Firefox 语义树超过会话工具 512 节点 | 会话与后观察使用现有 Provider 上限 4096；本地 workflow 的 512 独立预算保持；遍历及子节点枚举合作取消，待遍历节点计入准入上限，失败不发布部分快照 |
| Mirage EUI 无 AT-SPI 树、虚构 @e 引用、激活后复用旧引用 | 保留 unsupported_window、无效及过期引用拒绝；工具/模型指南说明真实引用来源及 OCR/局部编号回退 |
| 无 OCR 导致视觉后条件不可用 | Linux 原生默认自带已验 PP-OCRv5 / ncnn CPU 资源；实际默认启动经公开 Canvas 文本验收 |
| 未派发且无副作用的拒绝使本轮后续修改永久封锁 | 只认可本轮自身 action_id 的证明记录；纠正后可继续，未知/异常/已派发未验证/不确定仍封锁；最终动作拒绝不能借早先激活成功结算 |
| 只读视觉失败后新 ROI 成功仍被旧失败封锁 | 只读视觉门禁使用最新结果，成功的新观察可替代旧失败；不解除修改安全门禁 |
| 服务先于 UI 启动，WM_NAME Latin-1 混入 UTF-8 JSON | X11 Backend 有界读取/验证 EWMH UTF8_STRING，经典 XA_STRING 解码 Latin-1；atom 可在客户端启动前建立 |

没有修改 pinned 依赖源码或引入新并发 owner。现有 Runtime / Mira / Mirador / kairo
承载生命周期与计算。未知应用、真实副作用不确定、失焦、过期及取消不会被当成可重试成功。

## 固定包与实际产品

Mirador / ncnn 与既有 pin 一致，PP-OCR 源精确 commit、五文件源/产物 SHA-256 固定，
离线转换与此前已验版本逐字节一致。构建时可获取或使用固定缓存；运行时无需 Python、
Paddle、ONNX、模型下载或手工 OCR 路径。许可证/NOTICE、锁文件和转换记录随安装布局
交付，见[供应链审计](../supply-chain/bundled-vision-20261011.md)。未打包未验检测/跟踪权重。

在私有 XDG / D-Bus / Xvfb 启动实际已安装 mirage → tray → native，1500×1000 和
3200×2000 均完成默认加载、公开 Canvas OCR、原生保存/重启、自定义/关闭覆盖、损坏
与缺失资源拒绝以及空覆盖恢复。不存在 OCR 配置时自动加载同安装前缀内包；用户显式
自定义或空设置仍优先。headless 服务保持显式启用。没有安装至用户系统或操作用户桌面。

当前桌面入口对应 build/native-release/apps/mirage，已重新构建。正常退出旧托盘后重新
启动即可使用新默认；已有进程不会热换管线。模型供应商与桌面权限仍沿用产品配置，
无需重新配置 OCR 环境。明确保存的关闭/自定义清单需在桌面工具设置中恢复或修正。

## 验证与范围

| 检查 | 实际结果 |
| --- | --- |
| Release 全量 | 67/67 |
| Debug 全量 | 61/61 |
| ASAN + UBSAN（泄漏/错误即失败） | 12/12 |
| TSAN（含真实 AT-SPI 取消夹具） | 9/9 |
| 默认安装产品两种分辨率 | 各 25/25；收尾当前 Backend 重跑 3200×2000 |
| 真实 OCR / 固定资源负例 | 四语料文字/位置 4/4；资源 13 checks |
| 公共头边界 / 本增量 C++ 格式 | 53 个头、零违规；18 个文件通过 |

最终数量、日志 SHA-256、关键源码摘要及本地 DEB 摘要见
[机器摘要](data/bundled-vision-20261011/summary.json)；实际产品
[3200×2000 回执](data/bundled-vision-20261011/installed-4k.json)、
[1500×1000 回执](data/bundled-vision-20261011/installed-1500.json)、
[真实 OCR 四语料回执](data/bundled-vision-20261011/ocr-quality.json)。

回归包含截图预算/比例、预检纠正、513/4097 节点、语义遍历中真实 D-Bus 回复触发取消、
后续正常快照恢复、未知失败与不确定副作用负例、坏资源/源哈希拒绝和 X11 标题编码。
Release / Debug 全量及定向 ASAN+UBSAN / TSAN 结果写入机器摘要；TSAN 的 AT-SPI
夹具沿用仅系统 GLib/GIO 抑制，Mirage 帧不豁免，未执行完整真实 OCR 权重 TSAN。
本增量 C++ 格式、Python 编译、公共头边界和 diff 检查通过。全仓格式目标仍在本轮
未编辑的 apps/native/app.cpp 既有改动中失败，未混入无关格式化。

原用户两次在线业务任务未重跑，此次以脱敏历史、同类错误序列及私有实际产品验收闭合
已识别缺陷，不宣称任意浏览器任务成功。Windows / 原生 Wayland、完整权重 TSAN、
真实宿主全屏/DPI、慢模型与过期引用的 M7-07 边界分别保留，负责人 Mirage 维护者；
具备相应环境或重新提交真实业务任务后补跑。既有五秒观察/引用预算不放宽。

首次新截图夹具复用 operation id、旧编号测试沿用永久无副作用拒绝门禁、默认启动
发现 Latin-1 标题、四个旧夹具把 UTF-8 写为 XA_STRING，均已修正并重跑；失败摘要
保留，不能将初次失败抹作全程成功。收尾产品脚本首次误选不带 Xlib 的系统 Python，
随后又用默认包路径当自定义替换并误期待 restart_required；改用现有 Python 和
先前已验的独立自定义清单后 25/25，不修改产品对相同路径的无重启语义。相关实现与证据仍在当前工作分支，未提交、推送
或发布 release。DEB 是本地可审阅交付产物。

## 复现

```sh
cmake --preset native-release
cmake --build --preset native-release
ctest --test-dir build/native-release --output-on-failure
./build/native-release/apps/mirage start
```

离线构建可传入 -DMIRAGE_VISION_SOURCE_DIR=<固定五文件源目录>；面向最终用户的运行
无需此构建配置。资源测试 tests/integration/bundled_vision_resources_test.py；私有产品
脚本 tests/manual/product_vision_acceptance.py 的 --bundled-default / --tamper-bundle
覆盖默认启动与损坏恢复，只对私有安装副本启用篡改用例。
