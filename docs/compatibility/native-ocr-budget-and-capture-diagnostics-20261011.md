# 原生 OCR 预算与共享取图诊断：2026-10-11

> 工作项：M7-10 / M7-14（In Progress）
> 决策：[DEC-076](../decisions/DEC-076-bounded-native-ocr-and-capture-diagnostics.md)
> 负责人：Mirage 维护者；工作树变更尚未提交/评审

## 新会话的三个层次

会话 `5c555168634944d8b708b81b7e7b6668`：

- `49ad94f913d2fa3ef6e9d0dcb5b1ec6f` 实际默认 ncnn OCR 成功，106 区域，
  backend_calls=2；同轮另一图返回 native OCR output budget exceeded。
- `d7929f5eaac6caa9530671a6197f6560`，17:47:23.033 +08，两次 screen.capture
  成功，后续 OCR 重新取图返回 portal_timeout；其前已读到网页 404。
- 模型两次使用动作前的 application scope/epoch，拒绝且 dispatched=false；
  重新 application.list 后能派发，但又打开相同失败网址。进程局部验证不证明页面
  已加载，导航选择和无 Wayland 输入能力仍限制原任务完成。

实际加载的 tray/native 哈希与 DEC-075 Release 相同。不存在凭据失效或 OCR 接线
未生效的证据。只提取尺寸/状态/计数/ID，不保存用户画面、识别文字、URL 或密钥。

## 实现

JSON 的 16 KiB 总预算改为完整区域前缀，带 truncated、total_region_count、
region_count；保持原有顺序，不截断单段 UTF-8，也不生成可点击引用。无效几何/
单段超额仍拒绝。原生 detector 候选 cap 为 512，X11 仍 128；已有 Mirador/ncnn
识别后验证全部结果，再映射至最多 128 区域，最后应用 JSON 预算。候选上限与
五秒分析期限保持有限，不保证任意密集画面都能成功；截断不能证明某文字不存在。

Portal Platform Backend 取图失败保留 remote/stream 阶段和 PipeWire 状态、格式/
缓冲标志；不记录第三方原始错误。失败关闭、不再次授权及五秒上限不变。能力说明
要求动作后刷新 application.list，并区分 URL 派发、页面验证和业务完成。

## 真实宿主过程

首轮 Native Release `portal_screen_test --probe-host-ocr` 使用固定 bundle manifest，
一次共享取得三张图，包括等待 6.1 秒及普通动作换代后取图；未复现超时。OCR 则在
128 候选 cap 返回 result_too_large，31 checks、1 failure；据此增加 DEC-076 的
候选分析预算和显式前缀。保留 `/tmp/mirage-ocr-budget-host.log`，不能把这次失败
记为通过。已准备第二轮同一授权中连续三次 OCR 与接管断言，维护者同意配合。

实机超时根因仍待阶段证据，未盲目增加共享超时或静默重试。原在线视频任务和
RemoteDesktop 键鼠尚未闭合，M7-10/14/15 整体 In Progress。补跑需要实际 GNOME
共享环境/维护者操作系统授权，负责人 Mirage 维护者。

## 自动验证

最终验证执行后同步计数及机器摘要；未执行项不标完成。用例覆盖普通/控制字符 JSON
预算、候选前缀、128/160/512/513 候选上限、完整文字/原顺序、真实 canonical loop、
无文本持久化与只读边界。五种配置使用既有私有 XDG/DBus/Xvfb 隔离，sanitizer
配置不启用真实 ncnn；实际推理由 Native Release 与产品/DEB fixture 覆盖。

## 第二轮与独立复验

第二轮同一共享先成功返回 105 个区域，随后 native_ocr_expired，41 checks、
1 failure。此时并行运行多配置回归，故不能把超时直接归因为平台流。完成所有
回归/构建后单独运行同一 Release 探针，维护者重新共享一次，五张新图（含
6.1 秒/普通动作换代）、连续三次真实默认 PP-OCR/ncnn 均成功，各 106 区域，
backend_calls=1/2/3；来源 artifact 与每次 ImagePart 相同，takeover/resume
拒绝旧共享并关闭，58 checks、0 failures。记录在
`/tmp/mirage-ocr-budget-host-quiet.log`，不保存画面/文字或调用模型。

并行负载确实影响分析期限，独立复验支持当前正常读取路径，但不证明任意负载下
都在五秒内完成。原用户的 portal_timeout 本轮未复现，只补诊断，不标根因已修复。

## 最终自动验证与产品交付

Debug 全量 67/67、Native Release 全量 73/73；ASAN/UBSAN/TSAN 各五项相关
测试+状态隔离两项 7/7（供图/OCR、PP-OCR Runtime、候选管线、后条件、GUI）。
ASAN 使用 detect_leaks=0，UBSAN 独立配置，TSAN 使用 setarch x86_64 -R 与
既有 GLib suppression，未增加 suppression；不宣称 sanitizer 覆盖真实 ncnn。
诊断平台代码的 Portal 43 场景在此前本轮各配置已通过；末轮完整 Debug/Release
再通过 Portal 夹具。

执行命令：`cmake --build --preset <配置>`；Debug/Release 使用
`ctest --test-dir build/<配置> -j4 --output-on-failure`，sanitizer 用上面五个测试
的定向匹配（命令及日志 /tmp/mirage-ocr-budget-*-suite-final.log）。
实机使用 `build/native-release/tests/portal_screen_test --probe-host-ocr`，参数为
`/home/linductor/mirage/build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json`。

实际 Release 与解包 DEB 产品各 37/37，11 次零模型 workflow、默认 OCR/ncnn、
Canvas 六次点击和 Runtime owner 清理通过。包内四个二进制 SHA 与最终 Release
一致，包为 out/packages/mirage_0.1.0_amd64.deb；clang-format / git diff --check
通过，用户配置/历史状态 SHA 保持。此前两个新增夹具失败分别为 resize 保留了旧
首条测试文字、重复 worker source ID，修正 assign/独立 source ID 后补跑全部
上述检查；保留失败日志，不算产品通过。未修改依赖、凭据、用户历史或系统 SDK。

机器证据：[summary.json](data/native-ocr-budget-and-capture-diagnostics-20261011/summary.json)。
运行中托盘仍加载 DEC-075，应从托盘完整退出，再经原桌面入口启动 DEC-076 本地
Release；无需配置环境。稳定性与原业务未关闭，未提交/推送/发布。
