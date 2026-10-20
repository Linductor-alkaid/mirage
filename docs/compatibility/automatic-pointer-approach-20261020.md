# 自动接近点击与 native-release 验收（2026-10-20）

> 状态：Linux 限定实现与构建验收完成；GNOME 只读起点实测通过，在线模型完整点击/Windows 验收开放
> 负责人：Mirage 维护者
> 工作项：M7-25/26/31；[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)
> 基线：HEAD d09fa8d + feat/desktop-input-policy 工作树，包含前一增量 DEC-056 v2

## 实现范围

三条物理点击工具 vision.click / vision.click_region / session.click 为 4.0.0，
无模型 glide_ms 参数；自动计算距离/宽度时长、minimum-jerk 连续移动、到达守卫、
点击/释放、返回/可选业务条件。语义 Invoke 保留原语义。

策略版本 pointer_motion_policy v1，120～650 ms（零距离不移动），编号/区域总操作
上限 3 s，Portal 外层保持 5 s。复用已有有限 Executor 调用与平台分段投递，没有新增
线程/定时器/队列或生命周期 owner。依赖 pin 未变；JSON 继续使用 Mira，轨迹复用
Desktop 已有生成器，目标稳定性继续使用 Mirador。

Portal 自动起点来自当前授权流的 Cursor metadata；无元数据时明确拒绝，未提供
直接点击 bootstrap。格式/来源变化不沿用其他流的旧注入位置。到达观察只用于
点击前守卫，不当作点击后的业务证据。取消或到达目标变化不点击；已发生移动保留
现有副作用及不确定性处理，尚未引入通用已知部分执行前缀契约。

不包含：滚动/拖动的默认接近与时序迁移、按键/文本/dwell、多击间隔、跨观察持键、
新动作组、Windows 路径、旧通用 ElementTargetExecutor 的非会话物理回退迁移。
DEC-110 整体与 M7-25/26 仍开放。

## 环境及验证方法

Linux 7.0.0-38-generic x86_64、GCC 13.3.0、CMake 3.28.3。debug 为 native/OCR OFF，
asan/ubsan 为 native ON、OCR OFF；native-release 使用现有 native + OCR 产品配置。
Mira a29374a、kairo 32027c2、Mirador cb45a10、eui-neo 63117f7，均未修改。

- visual_glide_tools_test：无时间参数的默认移动、距离/宽度 golden、后台不支持、
  移动失败、移动中取消、到达焦点/目标变化、原地无空移动、多击/按钮与旧参数拒绝。
- x11_smooth_move_test：私有 Xvfb，独立 X 连接采样自动接近的在途位置、单调路径、
  到达及耗时；不是通过最终坐标冒充连续移动。
- portal_input_test：私有同步 Notify seam，2 倍像素比例下的自动计时、分段投递、
  完成前无按钮事件、未知起点不点击，以及原取消/释放/落后矩阵。
- portal_screen_fixture：隔离总线校验 metadata cursor mode 选择；固定头解码覆盖
  非对齐/截断、无效 ID、越界/负坐标；原授权/失败路径继续跑。未连接真实 compositor。
- desktop_image_supply_test：共享点击内部 Move 与到达守卫后才 Click；无条件输入不
  后做业务 OCR；新观察、来源序号、授权/拒绝、复用及显式后条件保持各自范围。
- post_action_verification_test：保留私有 X11 真实激活/回显/提交落盘/拒绝恢复任务。

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset <asan或ubsan> -j 2 --target visual_glide_tools_test desktop_image_supply_test post_action_verification_test x11_smooth_move_test portal_input_test portal_screen_test smooth_move_schedule_test
ctest --preset <asan或ubsan> -R '^(visual_glide_tools|desktop_image_supply|post_action_verification|x11_smooth_move|portal_input|smooth_move_schedule)_test$|^portal_screen_fixture$' --output-on-failure -j 3
```

实际结果：Debug 全量 **101/101**；ASan、UBSan 第一组各 9/9（7 个用例 + setup/cleanup）
通过，无 sanitizer 报告。补充对默认平滑能力的测试桩适配、共享到达变化及旧版本拒绝后，
在 asan/ubsan/native-release 分别执行：

```bash
cmake --build --preset <preset> -j 2 --target visual_glide_tools_test desktop_image_supply_test visual_marks_test gui_fallback_test local_visual_workflow_test
ctest --preset <preset> -R '^(visual_glide_tools|desktop_image_supply|visual_marks|gui_fallback|local_visual_workflow)_test$' --output-on-failure -j 3
```

各 7/7（5 个用例 + setup/cleanup）通过，每个配置合计覆盖 10 个不同相关用例。
最终自动工具矩阵 130 检查/0 失败；共享图片矩阵 2733 检查通过。私有 X11 自动接近
400 px / 32 px 宽目标，计划 458 ms、实测 458 ms；独立连接看到在途位置，单调、到达
及耗时断言通过。此数字仅为该夹具证据，不是产品性能保证。隔离 Portal 41 个场景通过。

```bash
cmake --build --preset tsan -j 2 --target visual_glide_tools_test desktop_image_supply_test x11_smooth_move_test portal_input_test portal_screen_test
setarch x86_64 -R ctest --preset tsan -R '^(visual_glide_tools|desktop_image_supply|x11_smooth_move|portal_input)_test$|^portal_screen_fixture$' --output-on-failure -j 3
cmake --build --preset tsan -j 2 --target visual_glide_tools_test desktop_image_supply_test
setarch x86_64 -R ctest --preset tsan -R '^(visual_glide_tools|desktop_image_supply)_test$' --output-on-failure -j 2
```

TSan 首组 7/7、补充组 4/4 通过（均含 setup/cleanup），5 个不同用例无 race 报告；
沿用本机进程级 ASLR 兼容命令，不修改系统配置。

首轮 Debug 回归暴露的旧桩未声明 timed 能力/未提供指针回读、单次 move 日志断言及
自动计时的旧 600 ms 固定断言，均按新契约修正；未删取消、释放、授权或目标守卫负例。
最终全量再次通过。

## native-release 更新

```bash
cmake --build --preset native-release -j 2 --target mirage-native-product
```

完整 product 目标包含 CLI、服务、托盘、native 前端及 pet，不能以只编译前端替代。
完整 product 构建通过；随后构建并运行同首组的 7 个相关 release 用例，9/9 通过
（含 setup/cleanup），再运行上述补充组 7/7 通过。最终产物包含本次自动接近及此前
DEC-056 v2 变更。CLI `--version` 可执行（mirage 0.1.0 / platform linux）。

`mirage service status` 对默认 socket 返回不可达；进程名探测确认本机没有运行中的
mirage-tray / mirage-native / mirage-service / mira_pet。本轮只更新 build/native-release
产物，未启动应用、替换已安装包或执行真实桌面输入；没有中断现有产品任务。

文档本地链接、JSON golden 和 git diff --check 已检查；third_party/ 与依赖锁未变。

## 实测限制与补跑

未执行在线模型完整点击任务、其他 compositor 的 Cursor 元数据首次起点验收、
Windows 构建/实机、人工手感或 P50/P95/成功率对比。GNOME 只读首次起点补验见下节。脚本桩和私有 X11 不替代这些
证据。负责人 Mirage 维护者；补跑条件为对应系统/授权会话、应用和模型就绪，先登记
输入方法、任务及阈值，再按 M7-31 取证。此记录不宣称硬实时或全量键鼠自然时序已交付。


## 最新会话故障修复增量

会话 e544a96b（2026-10-20 14:58 最后保存）的两次 desktop.session.click 都是
pointer_origin_unknown，dispatched=false、side_effect_may_have_occurred=false、
input_released=true；均未发送点击。失败租约推进 epoch 后，随后两次键盘又报
interactive_image_required。这是起点读取及无副作用拒绝处理问题，未发生点击后
业务验证失败；本记录不保存用户任务正文、截图或凭据。

环境：GNOME Shell 46.0、Mutter 46.2、xdg-desktop-portal-gnome 46.2、PipeWire 1.0.5。
Portal AvailableCursorModes=7，支持 metadata。初次真实只读探测视频正常但 Cursor
缺失；补 Format 后协商仍缺失。消费端最大 256×256 RGBA 的 Cursor 请求无法与
Mutter 固定 384×384 分配求交，调整到有界 512×512 后读回有效起点。
[Mutter 46.2 源码](https://raw.githubusercontent.com/GNOME/mutter/46.2/src/backends/meta-screen-cast-stream-src.c)
的 SPA_PARAM_Meta 声明固定分配；Format 事件后按
[PipeWire stream API](https://docs.pipewire.org/group__pw__stream.html)
通过 pw_stream_update_params 协商 Header/Cursor。

新增 DesktopCaptureSession::pointer_position 只读已采集帧状态；共享点击在 lease
之前检查起点、尺寸和来源序号，无起点/错来源/尺寸/越界/查询异常不推进 epoch。
回归包含原图及 epoch 下的键盘恢复；移动后、释放失败、撤权/接管仍按原围栏处理。

新增手动工具（默认构建和 CTest 均不运行）：

```bash
cmake --build --preset debug -j 4 --target portal_cursor_probe
./build/debug/tests/portal_cursor_probe
```

仅在已有 remembered authorization 时采集，20 s 协作期限，唯一 kairo Executor
owner 消费 future 后关闭；不调用 input、不输出像素/坐标/令牌。真实授权流结果：

```text
capture_ok=1 error=
cursor_available=1 error= extent_matches=1 sequence_matches=1
```

该结果证明当前 GNOME 流的自动接近前置起点已可用，不等于真实模型完整点击成功。
portal_stream_retention_test 在真实 Format 回调处截获元数据协商，并用 SPA filter
核验固定 384×384 可协商、1024×1024 被有界拒绝，覆盖协商失败、同流无新数据保留、
元数据消失/越界退役。desktop_image_supply_test 覆盖上述预检及键盘恢复。

本轮实际执行：

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset <asan或ubsan或tsan> -j 2 --target desktop_image_supply_test portal_stream_retention_test portal_screen_test
ctest --preset <asan或ubsan> -R '^(desktop_image_supply|portal_stream_retention)_test$|^portal_screen_fixture$' --output-on-failure -j 3
setarch x86_64 -R ctest --preset tsan -R '^(desktop_image_supply|portal_stream_retention)_test$|^portal_screen_fixture$' --output-on-failure -j 3
cmake --build --preset native-release -j 2 --target mirage-native-product desktop_image_supply_test portal_stream_retention_test portal_screen_test portal_input_test visual_glide_tools_test
ctest --preset native-release -R '^(desktop_image_supply|portal_stream_retention|portal_input|visual_glide_tools)_test$|^portal_screen_fixture$' --output-on-failure -j 3
```

Debug **101/101**；ASan、UBSan 各 **5/5**（3 个相关用例 + setup/cleanup），
无 sanitizer 报告。native-release 完整 product 重新链接通过，相关 **7/7**（5 个用例
+ setup/cleanup），CLI --version 可运行。TSan **5/5**（3 个用例 + setup/cleanup）通过，无 race 报告。
未发现运行中的 Mirage 服务/前端/托盘/pet，本轮未启动或重启产品，没有执行真实
鼠标/键盘输入。文档本地链接 488 项及 JSON golden、git diff --check 通过，依赖锁与
third_party 未变。

完整模型点击与手感仍由 Mirage 维护者在
GNOME 授权、测试应用及模型就绪后按 M7-31 补跑；没有将只读探测标作完整验收。
