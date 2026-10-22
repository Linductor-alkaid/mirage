# 连续视觉空间 ROI 执行验证

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-07/18；配套 M9-11/12
> 基线：Mirage 237f722 的后续工作树，feat/continuous-perception-cycle
> 依据：DEC-091、DEC-109 本轮空间 ROI 实施边界

## 范围与公开接口核对

Mirador pin `2a9f4830f4727498ed4cbad568a1d3ec357ece7e`；开工 fetch master
无新提交。Mira `a29374a` 不变，没有修改依赖、锁文件或引入新组件。按 Mirador
integration 的 By API → Fusion and snapshots 路由核对公开 fusion、continuous、
OCR/Detector 头；按 kairo By API → Blocking I/O 核对沿用的单 worker 生命周期。

实现是 Mirage 的有界执行策略：无状态空证据融合当前帧，ROI-only 规划提前选择
昂贵能力范围；Mirador 承担 ROI 裁剪、缩放/坐标恢复、融合、token 验证、覆盖与
时间确认。全程同一授权共享帧，不拿旧对象定位或历史像素冒充本帧证据。
完整配置、坐标、失败/结算契约及示例见[设计](../design/continuous-visual-analysis.md)。

暂定单次至多 4 个区域、4,194,304 源像素（重叠重复计），只允许收紧，前置与
最终计划合计 64 KiB。证据累计条数也受实体数上限约束。限额不足整体显式失败，
不静默丢掉后续区域，不回退全帧。原单帧、全帧采纳和预算化 OCR 路径沿用。

## 验证方法

Linux x86_64、GCC 13.3.0，实际 kairo blocking worker + model-free SPI：

- 两个不相交的 2×2 ROI，在 8×8 帧上 OCR 每帧实际输入 8 像素，坐标恢复到原帧；
  OCR/Detector 各调用两次。连续静态三帧仍刷新，覆盖/确认/事件不依赖近似 gate。
- 越界、分数像素、零面积/NaN、坐标空间、旋转、空/对象/文本条件与模式冲突
  显式失败；区域数、面积超预算发生在任何 backend 调用前。
- 第二个 ROI 失败后只保留已完成次数/部分原始结果作诊断，不发布快照或采纳；
  后续完整批次须重新积累两次有效观测。两个计划合计超字节预算也不采纳。
- 正常、异常、外部取消、期限、host.stop、Executor 优先 shutdown 及恢复，
  全帧和空间模式都测试；已结算源帧沿用释放验证，默认背景 churn 仍有两代像素上限。

```sh
cmake --build --preset debug -j 4
ctest --preset debug
# 对 asan、ubsan、tsan、native-release 各执行；TSAN 的 ctest 前加 setarch x86_64 -R
cmake --build --preset asan --target continuous_perception_consumer_test continuous_capture_test visual_session_test visual_session_budgeted_test visual_observation_loop_test -j 2
ctest --preset asan -R '^(continuous_perception_consumer_test|continuous_capture_test|visual_session_test|visual_session_budgeted_test|visual_observation_loop_test)$'
clang-format --dry-run --Werror integration/mirador/include/mirage/integration/visual_session_host.hpp integration/mirador/src/visual_session_host.cpp tests/integration/continuous_perception_consumer_test.cpp
cmake --build --preset debug --target mirage-boundary-check
cmake --build --preset debug --target mirage-format-check
git diff --check
```

## 实际结果与局限

Debug 全量 **110/110，通过，305.52 s**；ASAN、UBSAN、TSAN、native-release
定向各 **7/7**（包含隔离 setup/cleanup）通过，无 sanitizer 报告；消费专项
**894 checks**，采集 166、visual_session 281、budgeted 136、observation_loop
114，均零失败。TSAN 仅测试子进程以 setarch -R 关闭 ASLR，与前轮方案一致。
变更的三个 C++ 文件格式通过；公开边界 65 个头零违规（保留 DEC-096 两个既有
seam）。9 份变更 Markdown、413 个本地链接、标题/围栏、M9 编号/开放状态与
pin/lock 检查通过，git diff --check 通过。仓库全量格式门禁仍在未改动的
model_layer.cpp:1528 失败，不据此填写
全量通过；负责人 Mirage 维护者，在截断分类维护分支修正格式后补跑该门禁。

前述 8/64 仅为确定性测试的源像素输入，
没有执行实际模型、计时 A/B、低端设备/4K 多源长跑或 CPU/RSS/唤醒收益基准。
不将少输入像素直接换算为速度或内存节省；Detector 输出坐标与调用数通过 SPI
取证，未增加模型权重、解码器或模型请求。

仍待：对象/文本前置定位、静态精确验证零 forward、外围公平巡检、无帧焦点查询、
native/IPC 消费及实际模型/Windows/真实 Wayland/pet 共存。负责人 Mirage 维护者；
平台接线、授权/设备/预登记阈值齐备后，由 M9-11/12 分项补跑，不关闭 M9 整体。
仓库全量格式门禁的基线 model_layer.cpp:1528 问题按
[已有审计](../supply-chain/mirador-continuous-perception-20261022.md)跟踪，未改动该文件。
