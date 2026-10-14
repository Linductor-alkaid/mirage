# 桌宠帧交接与 rest 闪帧修复

> 日期：2026-10-14
> 工作项：M8-03；BUG-20261014-003
> 负责人：Mirage 维护者
> 状态：本地自动回归通过；真实桌面动作切换目视复验待执行

维护者报告：两个动作切换之间有时闪一帧 rest。原 UI 在每次 clip 变化时上传缓存
的 rest 第 8 帧，等待新 WebP 解码，随后再上传目标帧；即便同 tick 素材就绪，也
可能发生两次 ImageStream 提交。这个防透明空帧的占位策略产生了额外待机姿态。

UI 私有 `FramePresentation` 统一决定每个 tick 的一次提交。普通切换期间不提交
占位帧，保持 ImageStream 的最后可见帧/纹理，目标就绪后直接替换。旧片完整解码
数组仍在请求新片前释放；没有增加整片缓存、解码任务、定时器或第三方依赖。
安全状态、owner epoch 变更、退出及加载失败仍立即采用中性帧或透明清图，避免
保留旧输入姿态。中性缓存仍最多一帧；隐藏释放该引用，恢复重建最新目标。实际
idle 状态继续播放 rest，播放器的首轮、once、启动基线与取消规则不变。

执行结果：

- `cmake --build --preset native-release --target mira_pet pet_frame_presentation_test -j 4`
  成功，本机 Release 桌宠已更新；正在运行的旧进程需独立重启以载入新二进制。
- Release CTest 选择 `pet_frame_presentation_test|pet_animation_test|
  pet_resource_loader_test|pet_frames_test`：6/6（含隔离 fixture）。新帧交接测试
  62 项检查无失败，验证预热 rest 后 think→look 的 20 次加载 tick 均不提交中间帧，
  同时验证迟到旧片、目标首帧、真正 idle、重复帧去重、同片新身份、安全降级、隐藏
  恢复和目标已就绪时直接替换清图。
- Debug 对应帧交接/播放器/加载器选择集 5/5（含 fixture）。新测试 ASAN/UBSAN
  分别编译并执行，各 1/1，无 sanitizer 报告。
- `python3 tests/manual/pet_preview_acceptance.py --build build/native-release
  --output /tmp/mirage-pet-frame-handoff-20261014`：私有 Xvfb/ICCCM 实进程与窗口
  检查 31/31，含 alpha、帧推进、拖动、位置恢复、隐藏、独立启停/故障恢复与退出。
  输出留在 /tmp；该 fixture 没有真实模型动作切换，不能代替 compositor 目视验收。

CI native 作业已加入该测试的构建与选择集，本轮未执行远端 CI。真实桌面动作
切换目视复验、其他平台与原 M8 性能门禁未执行；负责人为维护者，补跑条件为重启
更新后的桌宠并执行连续动作任务，核对动作间不插入 rest/透明帧，以及真实待机与
接管/断连仍正常。既有加载器的 generation、取消、期限和容量纪律保持原契约。

公开头边界检查通过（57 个头，0 违规）。全仓 `mirage-format-check` 在本轮之外的
`integration/mira/src/desktop_action_verification.cpp:259` 等处失败；该文件属于已
存在的 DEC-086 开发提交，本轮未修改。负责人为维护者，补跑条件为该路径完成
格式修复后重跑全仓门禁。本轮 C++ 文件另行执行 clang-format dry-run 检查。
