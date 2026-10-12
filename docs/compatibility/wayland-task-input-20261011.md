# Wayland 任务内键鼠验证（2026-10-11）

> 状态：In Progress
> 工作项：M7-10 第五增量
> 决策：[DEC-077](../decisions/DEC-077-wayland-task-scoped-input.md)
> 负责人：Mirage 维护者

## 范围与事实

已实现 RemoteDesktop CreateSession/SelectDevices/Start 与同会话 ScreenCast.SelectSources /
PipeWire capture；单共享显示器，keyboard+pointer 必须都获准。不持久授权，不旁路系统
控制限制。DesktopCaptureSession 拥有完整点击/组合键/ASCII 文本/滚动输入，与全局 InputProvider
区分。按下失败亦进入释放表；释放失败经 ActionCoordinator fence，有限执行受既有 Kairo
任务管理。修复取消 probe 在 ActionCoordinator 锁内重入导致的死锁，新增专门回归。

产品 `desktop.session.*` 工具经第一张 interaction=true 的截图请求授权，task/session/
已供图像/epoch/authority/意图年龄与图内矩形验证后，再取得原图核对目标像素。
前置取图和后置证据均计入 32 帧任务预算，只读八帧不变。后置通过同一会话新图回流
Mira；shared_frame_observed / shared_text_exists 仅为局部证明，工具卡明确显示局部核验。
输入内容只在 canonical 请求中保留，产品历史投影只保存字节数/范围；不记录用户桌面像素。

## 自动验证

- 隔离 Portal bus：50 个场景（原 43 + RemoteDesktop 7），授权路由、设备拒绝、缺失 size、
  请求路径不匹配、取消、释放会话及失败不重新授权；不接触用户桌面。
- 私有输入协议 seam：37 checks / 0 failures；DPI 2:1 映射、按键及反序释放、Unicode 派发前拒绝、轴步数、
  无效参数、设备拒绝、按下失败、取消、超时、异常、释放失败、Executor shutdown。
- DesktopImageTurn：19 个共享输入契约场景；前后取图、owner/scope/epoch、未供图、权限拒绝、目标像素变化、
  释放失败 fence、初次授权拒绝不重弹、后置失败、OCR 不匹配和无效证据。
- 真正 Mira ConversationLoop：capture -> click -> chord -> ASCII text -> scroll -> final，
  4 次完整输入，同一任务会话新图、局部验证和历史投影脱敏。

本轮最终 Debug 全量 68/68、Native Release 全量 74/74；ASAN、UBSAN 定向各 7/7。
ASAN 使用 detect_leaks=0:halt_on_error=1，UBSAN 使用 halt_on_error=1:print_stacktrace=1。
TSAN 首轮并行和串行复跑中核心四项及状态隔离通过，但 50 场景 Portal 聚合测试均
超过原 120 秒测试夹具期限；没有 sanitizer race 报告。仅将 TSAN 聚合夹具期限设为
180 秒，不改变每个 Portal 请求或产品动作的期限。最终 setarch x86_64 -R 下定向
复跑 7/7，Portal 聚合耗时 123.96 秒，无新增 suppression 或 race 报告；不替代既有
完整 OCR 权重的 TSAN 业务门禁。

实际 Native Release 产品隔离验收 37/37，11 次无模型 workflow，默认 PP-OCR/ncnn、
六次 Canvas 点击及 Runtime 干净退出。DEB 已重建，包内四个可执行文件 SHA-256 与
最终 Release 一致；没有将隔离 X11 产品验收当成 Wayland 实机证明。
用户配置/历史六个文件 SHA-256 保持不变，git diff --check 通过；本轮未提交或推送。

复现命令：

```sh
ctest --test-dir build/debug -j 2 --output-on-failure
ctest --test-dir build/native-release -j 2 --output-on-failure
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --test-dir build/asan -j 2 --output-on-failure -R 'portal_input_test|portal_screen_fixture|desktop_image_supply_test|action_contract_test|post_action_verification_test'
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan -j 2 --output-on-failure -R 'portal_input_test|portal_screen_fixture|desktop_image_supply_test|action_contract_test|post_action_verification_test'
setarch x86_64 -R ctest --test-dir build/tsan -j 2 --output-on-failure -R 'portal_input_test|portal_screen_fixture|desktop_image_supply_test|action_contract_test|post_action_verification_test'
/home/linductor/miniconda3/bin/python3 tests/manual/local_visual_workflow_acceptance.py --build build/native-release --output /tmp/mirage-dec077-product
GTK_IM_MODULE=simple /usr/bin/python3 tests/manual/wayland_input_fixture.py /tmp/mirage-wayland-input-host/state.json
# 上一条为前台窗口程序；另一个终端启动探针，按系统弹窗先开启控制再共享。
build/debug/tests/wayland_input_host_test /tmp/mirage-wayland-input-host/state.json
```

## GNOME 实机

入口：`tests/manual/wayland_input_fixture.py` 为全屏原生 GTK Wayland 公共测试窗口；
`tests/platform/wayland_input_host_test.cpp` 经过实际产品工具注册、共享、图像供给、
完整输入和 DesktopActionVerification。只以公共 fixture 的颜色标记确定位置，PNG 仅
在内存中解码，不存用户截图；状态文件只记录测试输入及点击/滚动计数。

本轮首次尝试中测试入口未启用桌面后端，已修正。后续 Portal 在 CreateSession 即返回
portal_failed，系统无授权弹窗；journal 指明 Mutter `Session creation inhibited`，
ScreenSaver.GetActive 返回 true。因此实机尚未通过，backend_calls/input_count=0。
不调整用户锁屏设置，不把其登记为 Mira/Mirador 缺口。维护者解锁后推进以下复验。

解锁后一次 Start 返回授权不完整，后续成功取得控制权限。静态全屏 fixture 多次在
stream_state=3 / formatted=1 / buffer_ready=0 超时；不能标为共享流已稳定修复。
新增任务内单 consumer 复用与授权后独立首帧预算，公共 fixture 的 GTK spinner 动画使
连续接收可复现；动态画面结果不能外推到静态画面。

result-9 完成十次输入及连续回图、按钮点击和滚动，但 9 checks/2 failures：ASCII URL
漏句点、Enter 未提交，中文只保留拉丁字母。Mutter 明确报告 No keycode found for
Unicode keyval；因此改为前置拒绝非 ASCII，不尝试猜测输入法组合键或写用户剪贴板。
result-10 Start 返回 devices=0，未派发输入；result-11 为六次动作局部回图、点击/滚动
通过，中文拒绝通过，但 ASCII/Enter 仍未闭合，10 checks/3 failures。
其中 settle 断言把有明确无副作用拒绝的尾记录当成已执行动作，测试已修正为
no_effect_rejection；公共窗口单独指定 simple IM context，不修改用户输入法设置。
维护者切到英文后，result-12/13 仍返回 granted_device_types=0，均未派发输入；
维护者随后确认共享前“允许远程交互”开关处于关闭状态。result-14 在先打开控制
开关、再共享及英文输入状态下，11 checks / 0 failures：单任务六次完整输入全部取得
局部回图回执，公共 URL 含句点精确输入并通过 Enter 提交，按钮计数增加一、滚动计数
增加，settle、非 ASCII 无副作用拒绝和接管关闭通过。窗口为 native Wayland，使用
simple IM context 与 spinner 动画；不外推到中文输入法、任意应用或静态画面。
此后不再需要维护者继续保持键鼠静止，已结束并关闭自有 fixture。

原始日志只保留 /tmp/mirage-wayland-input-host/result-*.log；
状态为公共测试输入，不含用户屏幕或历史。

Windows、其他 compositor、跨显示器/拖拽、动态 DPI 和真实浏览器业务仍另验；M7-10 整体
保持 In Progress。用户不需要配置额外 OCR/ncnn/键鼠环境。

## 用户操作与后续门禁

从托盘完整退出旧 Mirage，再从桌面入口启动本轮 Native Release。需要操作桌面时，
系统共享窗口内先打开“允许远程交互／远程控制”，再共享显示器；仅共享画面不授予键鼠。
当前实测 ASCII/Enter 要求英文输入状态，模型仍必须据新图或同源 OCR 验证结果。
拒绝授权后该任务关闭会话，不在同任务自动重弹；重新尝试需新任务。

未完成项由 Mirage 维护者继续推进：静态画面交帧（静止公共 fixture、无动画时补验）、
Unicode 的明确授权文本传输、其他 compositor / Windows / 多屏与真实浏览器任务。
这是 Mirage 平台适配和产品验收范围，不登记为 Mira/Mirador 能力缺口。
机器摘要见 [summary.json](data/wayland-task-input-20261011/summary.json)。
