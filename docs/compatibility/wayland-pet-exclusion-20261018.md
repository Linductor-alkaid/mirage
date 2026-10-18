# M8-06：GNOME Wayland 桌宠避让（2026-10-18）

> 状态：In Progress（实现与 Linux 隔离验证；真实 compositor 待验）
> 负责人：Mirage 维护者
> 决策：[DEC-105](../decisions/DEC-105-wayland-pet-exclusion.md)、[DEC-106](../decisions/DEC-106-optional-pet-exclusion.md)
> 基线：`dcb9113` 加当前工作树，保留 M6-46 正文保留修复

## 范围与实现

针对维护者正在使用的 GNOME Wayland 会话：当前 pet 是 XWayland 窗口，截图是
Portal ScreenCast。实现严格核验受管 PID / XRes / 唯一协议窗口后，撤下整组角色/
气泡/阴影；等待 WM_STATE WithdrawnState、服务端 unmapped 和 WM 列表移除确认。
只支持自有 UTILITY pet 和 GNOME Shell；纯 X11 保持 Shape 路径。其他 compositor、
纯原生 Wayland pet 与 Windows 的避让仍待各自适配，不能泛化本记录。

Host 创建并显式共享 PresentationContext，传给 pet owner 与 Linux Screen Backend，
存储隐藏确认后的 CLOCK_MONOTONIC 水位。Portal consumer 请求 Header 元数据，
新水位时重新激活既有 stream 并移除缓存；排队旧帧和旧缓存不能成为隐藏后的证据，
源 PTS 必须晚于水位。缺失/损坏/未来时间戳明确失败，等待新帧仍受现有 Portal
deadline/cancel 约束。没有额外授权请求、XWayland 根截图或截图涂抹降级。

重叠租约最后释放才恢复；原本隐藏的窗口不显示，原本可见的窗口恢复几何和置顶等
WM hints，不请求激活。UI owner 增加 map_state 采样，撤下时即使没有 iconify 回调
也进入既有暂停/恢复/退出路径。没有新增业务 worker、timer、队列或 wire 字段。
测试 WM 线程仅模拟平台亲和，fixture RAII join，符合 AGENTS 的测试脚手架豁免。

## 环境与复现

Linux x86_64，GCC 13.3.0，CMake 3.28.3，Ninja。本机只读版本查询为 GNOME Shell
46.0。运行中的维护者进程及其会话文件未修改、未重启。所有自动测试使用私有
Xvfb、XDG 与脚本 provider，不调用在线模型，不发送消息或操作维护者桌面。

```bash
cmake --build --preset native-release -j3
ctest --preset native-release -j2
# 以下五类业务测试在 asan、ubsan、tsan 各构建并执行；另含两个状态隔离 fixture。
cmake --build --preset asan --target wayland_pet_exclusion_test pet_exclusion_test \
  portal_stream_retention_test pet_exclusion_tools_test pet_observation_test -j2
ctest --preset asan -R '^(wayland_pet_exclusion_test|pet_exclusion_test|portal_stream_retention_test|pet_exclusion_tools_test|pet_observation_test)$'
ctest --preset ubsan -R '^(wayland_pet_exclusion_test|pet_exclusion_test|portal_stream_retention_test|pet_exclusion_tools_test|pet_observation_test)$'
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan \
  -R '^(wayland_pet_exclusion_test|pet_exclusion_test|portal_stream_retention_test|pet_exclusion_tools_test|pet_observation_test)$'
python3 tests/manual/pet_preview_acceptance.py --build build/native-release \
  --output /tmp/mirage-wayland-pet-withdrawal-native --withdrawal
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check
python3 -m py_compile tests/manual/pet_preview_acceptance.py
git diff --check
```

wayland_pet_exclusion_test 的 WM 是受控协议夹具，GNOME 名称用于测试分支选型，不是
真实 GNOME compositor。测试覆盖 WM/utility 缺失、缺共享帧水位、正常撤下/恢复、
嵌套租约、原先已隐藏、WM hints/几何恢复、运行中取消、WM 不确认的超时、外部重映射、
身份改变与窗口消失。Portal 测试使用真实有限 PipeWire loop 和受控 CPU buffer，
验证晚到旧 PTS、相等水位、缺元数据、损坏/未来时间戳、新 PTS 与 retained cache。
原生脚本的 --withdrawal 只验证真实 pet 的 unmap UI 生命周期，不冒充 Portal 授权
或真实 GNOME 画面通过。

## 验证结果

| 门禁 | 实际结果 |
| --- | --- |
| 最终 Native Release 完整构建与回归 | 构建成功；102/102，109.53 秒 |
| ASAN：五类业务测试和两个隔离 fixture | 7/7，2.69 秒 |
| UBSAN：同上 | 7/7，2.49 秒 |
| TSAN：同上（禁 ASLR） | 7/7，2.92 秒 |
| 真实 pet/tray 私有进程的 unmap 暂停/恢复/退出 | 33/33 |
| 格式、公共头边界、Python 语法、diff 空白检查 | 通过；62 headers / 0 violations，保留 DEC-096 两处既有 seam |

原生进程软件渲染采样：可见 CPU 20.00%、隐藏 2.33%（单核百分比）；隐藏正常
退出 0.216 秒，owner 失联后退出 5.281 秒。CPU 不是硬件能耗，Xvfb 不是 Wayland
compositor。可审阅的[结构化证据](data/wayland-pet-withdrawal-native-20261018.json)
保留 33 项结果，完整输出在 `/tmp/mirage-wayland-pet-withdrawal-native`。

## 能力降级轮（DEC-106，2026-10-18）

维护者确认避让按能力降级：未知 Wayland compositor、无可识别 XWayland WM 或
未编译 backend 返回显式未排除的被动租约，不发隐藏请求、清除过期帧水位，桌宠
保持可见且截图/OCR 照常；工具对象结果附加
`pet_presentation={excluded:false,reason:unsupported_compositor,
pixels_may_include_pet:true}`，desktop.observe 在 `environment_state` 注明未排除。
只有预先探测的能力不支持才降级；已支持路径的身份失败、撤下失败、超时、取消、
帧水位失败与恢复失败仍报原错误。键鼠沿用既有遮挡与权限检查。

本轮独立验证（由 Independent-Verification-Agent 执行并回报证据）：

- 修复两处：非 X11 构建的 `Unsupported` 被动路径此前不清除 `not_before_ns`
  （现持有 PresentationContext 并清零）；`pet_exclusion_test` 的 Wayland 旧断言
  仍期望报错（现更新为被动租约语义）。
- Native Release 全量 ctest 102/102 通过。
- ASAN/UBSAN/TSAN 抽查：wayland_pet_exclusion_test 44 checks、
  pet_exclusion_tools_test 37 checks、pet_exclusion_test 83 checks（TSAN）均
  0 failures；pet_observation_test 75 checks（含"已排除时 environment_state 不含
  pet_presentation"负向断言）在四种构建通过。TSAN 因环境 ASLR 熵需
  `setarch -R` 运行。
- 非 X11 独立编译检查 8/8：被动租约无错误、`excluded()==false`、水位 456→0、
  取消传递、release 幂等、release 后 verify 报错、无 PID 拒绝、取消拒绝。
- 上述为隔离 Xvfb/协议夹具证据，不改变真实 GNOME compositor 与真实 Portal
  生产者的开放门禁；M8-06 保持 In Progress。

日志前缀为 `/tmp/mirage-wayland-pet-`，最终 sanitizer 采用 `asan-test-guard.log`、
`ubsan-test-guard.log`、`tsan-test-guard.log`；原生脚本为 `native-verified.log`。
最终整套回归为 `test-guard.log`、构建为 `build-guard.log`，静态检查为
`static-guard.log`。磁盘上的 native-release 产品程序已更新；运行中的旧实例未自动替换。

## 过程记录与剩余门禁

构建阶段修正成员初始化顺序、当前 SPA SDK 头路径，以及测试 Xlib None 宏污染
kairo 头的包含顺序。初版选择 Iconify，隔离 WM 曾给出成功，但静态核对本机匹配的
Mutter 46 发现 UTILITY / skip-taskbar 窗口不可最小化，故改为 Withdraw，并更新夹具
模拟实际撤下、WM hints 清理和列表更新；初版成功不作为有效 GNOME 支持证据。
初版日志及中间回归保留在同前缀的 `build*.log`、`*-final.log`。撤下实现整套回归
曾通过 102/102（109.43 秒，`full-test-verified.log`）；最终复核补上同会话伪造
_NET_CLIENT_LIST 的格式校验，增加 8-bit XA_WINDOW 属性负向用例，先核验类型/容量
再解释 Xlib Window 数组，并补跑所有相关门禁。最终版本日志采用 `*-guard.log`。

下一步由 Mirage 维护者从托盘整体退出并重新启动新 native-release，在新会话上：

1. 桌宠开启，授权桌面视觉观察，要求截图/识别公开文字；核对截图不含 pet、角色和气泡。
2. pet 移至测试目标上方，在公开测试界面执行可逆点击/输入；核对避让、核验后恢复、原焦点。
3. 执行中取消、关闭 pet 或整体退出，核对没有残留隐藏窗口/键鼠按下状态、能重新启动。
4. 连续多次截图/OCR，确认已有授权的 stream 复用、静止桌面取得新帧且没有反复授权。

旧会话可能保留重复确定失败 guard，不能用其判断新路径是否仍失败。真实 GNOME
compositor/Portal 新帧、第三方 shell 扩展、多屏/DPI/全屏和硬件 GPU 未自动执行；原因是
当前 CU 无原生桌面控制。负责人：Mirage 维护者；补跑条件为目标桌面和受管进程重启。
未执行项不登记为完成，M8-06 保持 In Progress。
