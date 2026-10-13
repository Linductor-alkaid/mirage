# 桌宠重启与历史终态回归

> 日期：2026-10-14
> 工作项：M8-03/05；BUG-20261014-001
> 负责人：Mirage 维护者
> 状态：自动回归通过；真实桌面重启目视验收待执行

维护者报告：上次任务失败后独立停止并重启桌宠，播放失败动作而非入场。
Runtime 保留旧失败快照，新进程播放器的消费身份为空；首份快照若先于资源加载
到达，还会替换未就绪的入场。此问题属于 UI 播放时序，不是依赖能力缺口。

播放器新增显式 `begin_startup`，保留本地入场直到素材就绪并播完一次，解码失败
释放门禁。首份 Runtime 成功/失败快照以 owner/source/state 作为单条历史基线，
入场后展示待机，同身份刷新和隐藏恢复不重播。后续新任务/新 owner 结果正常播放，
首份活动快照后发生的失败保留为实时结果；安全状态和退出仍立即打断。
UI 不再以进程启动后的固定 4482 ms 推断入场完成。没有新增定时器、worker、IPC
或持久化字段；Runtime 的失败结果不变。

执行命令：

```bash
cmake --build --preset debug --target pet_animation_test -j 4
cmake --build --preset native-release --target mira_pet pet_animation_test -j 4
cmake --build --preset asan --target pet_animation_test -j 2
cmake --build --preset ubsan --target pet_animation_test -j 2
ctest --preset debug -R '^pet_animation_test$' --output-on-failure
ctest --test-dir build/native-release -R '^pet_animation_test$' --output-on-failure
ctest --preset asan -R '^pet_animation_test$' --output-on-failure
ctest --preset ubsan -R '^pet_animation_test$' --output-on-failure
```

四组 CTest 各 3/3（含 setup/cleanup）；播放器各 250 项检查无失败，无 sanitizer
报告。覆盖历史失败/成功先于解码到达、完整入场、同快照刷新、隐藏恢复、新任务/
owner、活动快照后实时失败、安全打断及入场解码失败。本机 native-release 桌宠
已重建；正在运行的旧进程不会自动替换，下一次独立启动消费新二进制。

未执行真实桌面失败后停止/重启的目视复验，负责人为维护者，补跑条件为更新后的
桌宠进程及已有失败任务；步骤为托盘停止桌宠、等待停止完成、重新开启，确认入场
一次后待机，再执行新失败任务确认仍播放失败。其他平台、性能、安装与输入闭环
门禁保留原状态，不据上述时钟测试宣称通过。

CI native EUI 作业原先没有启用桌宠预览。本轮增加 `libwebp-dev`，显式开启
`MIRAGE_ENABLE_PET_PREVIEW` 并构建 `mira_pet`、播放器、资源解码/加载与状态集成
测试，沿用该作业的测试隔离 fixture。该门禁证明编译及自动回归，不替代 compositor
目视验收；远端结果在 PR #86 中核验后再合并。

## PR #86 CI 可选平台路径修复

首轮 `9a3b7e2` CI 的 native EUI 构建因缺少 Xtst SDK 进入 Overlay stub，析构
`unique_ptr<Surface>` 时遇到不完整类型。stub 补充无平台类型的空 `Surface` 定义；
以 `c++ -std=c++20 -Iplatform/include -Iplatform/linux/include
-Idesktop/environment/include -fsyntax-only platform/linux/src/overlay_backend_stub.cpp`
原样复现失败，修复后加 `-Wall -Wextra -Wpedantic -Werror` 编译通过。CI 同时安装
`libxtst-dev`，使 native 预览编译对应的真实 X11 平台入口。

Debug/ASAN/UBSAN 的 `x11_backend_test` 则在未编译 Portal SDK 时错误要求其 Provider
存在，并空指针解引用/超时。测试使用平台 target 的 `MIRAGE_PORTAL_AVAILABLE` 属性
区分预期能力，SDK 缺席明确验证 `Unavailable`；不改变产品能力判断。带 Portal 的
debug 与禁用 pkg-config 的隔离构建分别 CTest 3/3 通过（含 fixture，约 2 秒），
覆盖两种编译路径。隔离命令：

```bash
cmake -S . -B build/ci-no-portal -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPKG_CONFIG_EXECUTABLE=/bin/false -DMIRAGE_FETCH_DEPENDENCIES=OFF \
  -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF -DMIRAGE_BUNDLE_VISION=OFF
cmake --build build/ci-no-portal --target x11_backend_test -j 4
ctest --test-dir build/ci-no-portal -R '^x11_backend_test$' --output-on-failure
```

上面的本地完整构建消费工作树，含同时存在的其他开发修改；远端 CI 只验证 PR head。
所有远端门禁通过前不合并；不把上述局部验证标作 M8 平台验收完成。

## 连续会话的投影等待饥饿（BUG-20261014-002）

`339434d` 的 Debug/ASAN/UBSAN/TSAN 全量门禁通过，Release 的容量测试在第 9 轮
等待终态超时；本机 Release 两次复跑分别在第 11/10 轮复现，均被 120 秒期限
终止。调试栈保存在 `/tmp/mirage-pr86-capacity-gdb.txt`：IPC blocking worker 与
多个 dialog worker 等待串行 future，串行上下文空闲。独立公开 API 探针的
10000 次串行及 30000 次并发调用均通过，不据此登记依赖缺口。

旧 driver 的终态已公布，但末尾仍等待 `refresh_pet_from_dialog` 的嵌套派发；连续
新轮次占用固定池，串行 ticket 的发布无法继续。私有投影适配器采用 kairo
LatestMailbox 合并最新请求，保留一个 `submit_on_with_handle` 的结果与取消句柄。
请求方不等待 future，25 ms 普通 timer 消费已就绪结果并补派最新请求；shutdown
由外部 owner 停止/取消并消费结果，适配器活至 Executor 排空。拒绝、异常及取消
仍沿 Executor 可观察设施记录，原有 stderr 诊断保留；不修改 pinned 代码。

`native_agent_integration_test` 修复后本机 Release 连续三次通过（7.27/7.07/7.06
秒）。新增 `pet_projection_refresh_test` 验证四个池 worker 在串行回调未完成时均
能返回、80 次请求合并、任务异常、总量准入拒绝、排队取消、运行中停止请求、queued
soft timeout 及 shutdown；Release 连续三次通过，ASAN/UBSAN/TSAN CTest 各 3/3
通过（含隔离 fixture）。运行中回调是有限快照单元，停止等待其收敛，不强杀任务。
queued soft timeout 在可出队时检查，不中断正在运行的 blocker。

同轮 native EUI 已构建桌宠并通过四项 pet 测试，但既有会话视图触发 30 秒超时。
本机该视图原样 Debug/Release 各 3/3 通过（5.16/1.86 秒）；将末尾无限
`glfwWaitEvents` 改为 1 秒有界等待，并要求 900 ms 内收到关闭唤醒，保留断言而非
放宽整套测试期限。远端仍需验证此修改。CI native 门禁同步加入投影适配器测试。
