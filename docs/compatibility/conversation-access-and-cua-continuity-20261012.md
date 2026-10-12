# 会话访问模式与原生观察链路（2026-10-12）

> 状态：M6-38 Completed（Linux 本地）；M7-10 In Progress
> 工作项：M6-38、M7-10 第七增量
> 决策：[DEC-080](../decisions/DEC-080-conversation-desktop-access-modes.md)、[DEC-081](../decisions/DEC-081-native-cua-capability-and-observation-continuity.md)
> 负责人：Mirage 维护者

同日后续界面澄清：维护者要求会话菜单只有三项，下方菜单/滚动证据为先前实现；
安装包也已随澄清重建，最终菜单与包摘要见[仅三模式菜单验收](conversation-mode-selector-20261012.md)。权限与观察链路
的自动证据仍有效，后续界面验证独立记录。

## 产品与执行边界

会话输入框访问按钮提供对话模式、视觉模式、完全访问。对话模式在服务端强制纯对话，
即使 IPC 请求同时带 agent=true 也没有桌面工具或屏幕图；视觉模式沿用 read_only 的
观察/OCR 工具，完全访问沿用 default 的完整工具集合。授权不能提升当前任务模式。
模式在运行、提交和授权在途时锁定，避免已接纳任务和按钮状态不一致。

菜单按模式启用授权操作。选择完全访问时，支持且尚无记住授权会请求现有授权流程；
视觉模式首次真正读屏才请求共享，不额外申请键鼠；对话模式不申请桌面权限。已有令牌
可以保留，忘记授权仍可执行。系统是否接受记住选择与产品模式分别显示，不能承诺永久权限。
设置页与会话菜单共享状态和请求门禁。授权失败保留草稿，错误区可滚动并保持位置。

偏好编解码迁到 persistence 的 pinned-free facade，内部使用 Mira JSON 和既有有界
原子存储。原六字段保留；明确未知/损坏的访问值回退到对话模式，旧缺省配置保持兼容。
没有新增线程、队列、并行监控或 pinned 依赖修改。

## 最新会话故障

就地读取最新两轮的相关工具回执，不复制用户历史或应用列表。
一次 desktop.window.list 未在本轮开放，被 Mira 模型桥拒绝。另一轮截图后 OCR 产生
新图，旧 image_scope 两次点击 dispatched=false；重新捕获后点击成功，随后 OCR
在 stream_state=3、formatted=1、buffer_ready=0 的共享流中超时。后续键盘调用因
共享会话已关闭而拒绝。这些证据不能归结为模型延迟或全部键鼠传输失败。

修复在 Mirage 层：每轮从注册表列出工具闭合集合，说明像素点击可用于窗口取焦；
OCR 显式返回新 image_scope 与 input_actionable，旧图返回 stale_image_scope。
即便内容相同，图引用也独立生成。工具未开放时仍 fail closed，提示改进不能保证模型
永远不会提出错误调用。

PipeWire 流保存一个最多 32 MiB 的自有最新帧副本，立即归还源 buffer，避免单 buffer
生产者被阻塞。静态画面允许再次观察；格式变化、暂停、断开、取消或尚无首帧不能返回
失效缓存。source_sequence 和动作水位限制动作后取图，只有新增源帧才能进入验证，
旧缓存不能证明点击或业务目标完成；源时间仍标记未知。实际目标证据仍由既有验证器检查。

## 自动验证

Debug 全量 70/70、Native Release 全量 76/76。ASAN、UBSAN、TSAN 定向各 8/8，
包含六个业务测试与私有状态 setup/cleanup：图像供给、native Agent、IPC golden、
偏好持久化、PipeWire 帧所有权与 Runtime 生命周期。ASAN detect_leaks=0:halt_on_error=1，
UBSAN halt_on_error=1:print_stacktrace=1；TSAN 使用 setarch x86_64 -R，无新增
suppression 或 race 报告。公开帧契约变更后先完成全量构建再运行全量测试。

补充工具清单的实际模型请求断言：缺 window.list 时仍有 session.click 和对应路线，
提供的工具全部出现在本轮清单中；Debug、Native、ASAN、UBSAN 各补跑 3/3（含状态
fixture），TSAN 8/8 已包含该补充。截图 → OCR → 新引用点击/键盘/文本/滚动在实际
Mira ConversationLoop 中通过；模拟源帧在动作后重复则不能获得成功后条件。

PipeWire 所有权夹具 15 检查：无首帧可取消、静态重复、原 buffer 立即归还、新源水位、
独立已发布副本、字节预算、暂停、格式错误、断开及 teardown。采用真实有限 PipeWire
loop，仅包装 buffer 所有权调用，不接触用户图像、系统授权或输入。
IPC golden 1389 检查、persistence 308 检查、native Agent 321 检查、Runtime 594 检查，
均零失败。原生真实渲染 24,934 检查，三个选项、模式联动、等待门禁、指针点击、滚动
保持及草稿保留通过；1180×800 浅色、860×620 深色的状态与错误画面已检查。
[浅色菜单](data/conversation-access-and-cua-continuity-20261012/composer-permissions-light.png)、
[深色完全访问](data/conversation-access-and-cua-continuity-20261012/composer-full-access-small-dark.png)、
[对话模式](data/conversation-access-and-cua-continuity-20261012/composer-conversation-small-dark.png)、
[错误区滚动](data/conversation-access-and-cua-continuity-20261012/composer-permissions-error-scrolled-dark.png)。
Impeccable 源码检测一次，无报告项；渲染检查不据此宣称目标平台授权通过。

DEB 已重建到 out/packages/mirage_0.1.0_amd64.deb，四个包内 ELF 与最终 Release
SHA-256 一致；未安装、重启或终止用户进程。测试自行隔离 XDG 与 Xvfb。
service.json、miragerc、accel_map、preferences.json 四份 SHA 与基线一致；运行状态和
会话历史两份 SHA 在本轮期间变化，不回写/恢复，也不将“六份状态不变”记为通过。
只读取最新会话的相关回执和状态形状，未读取 API Key、复制用户配置或历史进仓库。

最初新 golden 向量失败是夹具未读取 access/agent/reasoning 字段，已补解析和比对。
第一次 Release 夹具构建触发 SPA parser 的数组越界告警，原因是无效 POD 夹具仅分配
基本头部，已改为完整 object 后明确提供无效类型。保留原失败日志，不宣称首次全部通过。
未改生产超时、Mira 未开放工具守卫或 sanitizer suppression。git diff --check 通过。
本轮未提交/推送，不增加依赖缺口或 pin 改动。

复现命令（先构建对应目标）：

```sh
cmake --build build/debug -j2
ctest --preset debug -j2
cmake --build build/native-release -j3
ctest --preset native-release -j2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --preset asan -j2 -R '^(desktop_image_supply_test|native_agent_integration_test|ipc_protocol_golden_test|persistence_test|portal_stream_retention_test|runtime_service_test)$'
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset ubsan -j2 -R '^(desktop_image_supply_test|native_agent_integration_test|ipc_protocol_golden_test|persistence_test|portal_stream_retention_test|runtime_service_test)$'
setarch x86_64 -R ctest --preset tsan -j2 -R '^(desktop_image_supply_test|native_agent_integration_test|ipc_protocol_golden_test|persistence_test|portal_stream_retention_test|runtime_service_test)$'
cmake --build build/native-release --target package
```

各日志 /tmp/mirage-dec081-*；原生渲染 /tmp/mirage-dec080-ui.log 与同名目录，JSON 摘要见
[summary.json](data/conversation-access-and-cua-continuity-20261012/summary.json)。

## 待实机门禁

按维护者「稍后再验，先完成自动验证」的安排，本轮不发起 GNOME 共享/控制弹窗。
M7-10 保持 In Progress。负责人 Mirage 维护者；补跑条件是解锁 Wayland 桌面、
共享显示器时开启远程控制并允许记住选择、钥匙环可读写。先验证静态桌面连续截图/OCR，
再验证截图 → OCR → 使用新引用点击 → 取新帧/目标确认；确认真实浏览器取焦和导航。
一次授权后两个独立任务、所有 Mirage 进程退出并重启恢复，验证是否重复弹窗与令牌轮换。
系统撤销、暂停、显示器变化、输入法、Unicode、Windows 和多屏仍需分别验收。

本轮自动证据不代替以上门禁。运行中的旧 Mirage 不会自动加载新二进制，安装包重建后
仍需维护者保存任务、正常退出并重新启动，再进行系统授权测试。

2026-10-12 同日后续：菜单锚点与模型默认/任务预算继续修正，当前包和最终几何
见[后续验收](reasoning-and-menu-anchor-20261012.md)；本记录图片与 SHA 保留为当轮快照。
