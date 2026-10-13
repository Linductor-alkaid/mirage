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
