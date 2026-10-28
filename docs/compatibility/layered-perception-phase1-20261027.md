# 三层感知首阶段：采样/识别基线隔离验证

> 状态：Completed（Linux 参考契约验证；M9-25 平台门禁及后续生产接线仍开放）
> 日期：2026-10-27～28（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-25；关联 M9-23、M9-26、M9-32/33
> 决策：[DEC-121](../decisions/DEC-121-three-tier-perception-delivery.md)
> 基线：`d30502a` 之上的工作树，分支 `feat/continuous-semantic-binding`

## 环境与范围

Linux x86_64，GCC 13.3.0，CMake 3.28.3；Mira `68a8bf17`，Mirador `2a9f4830`。
Debug/sanitizer 为 PP-OCR/YOLO OFF 的受控假后端；native-release 两者 ON。
没有访问用户桌面、下载模型或调用在线模型，没有修改 pinned 依赖或生产开关。

新增 VisualSessionHost `sample_only`，使用原 kairo worker，维持两个 Adapter
比较基线：相邻成功 CV 样本与最后成功识别样本。显式 kSampled 带来源/时刻/
水位，返回两个变化报告，不融合、不 plan/accept/drain、不推进语义确认/年龄。
普通识别准入没有放宽；采样必须同配置完成全帧 bootstrap，分析帧每边 ≤1600。
额外签名在首次采样时才分配，4096B；识别成功后释放，下一采样直接参照新识别
基线。错误、取消/超时、来源/策略不匹配不提交采样水位。

源码范围：`integration/mirador/include/mirage/integration/visual_session_host.hpp`、
`integration/mirador/src/visual_session_host.cpp`、
`integration/mirador/src/visual_observation_pipeline.cpp`、
`tests/integration/continuous_perception_consumer_test.cpp`。
按排序后的路径、NUL、文件内容、NUL 顺序计算的 SHA256：
`5e2e13d78977b53449a4d94690a8fc78116bb59f603fa2774fedff46a65a6a0b`。

## 复现命令

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|significance_gating|model_layer' --output-on-failure

cmake --build build/asan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test -j 3
ctest --test-dir build/asan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test)$' --output-on-failure
cmake --build build/ubsan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test -j 3
ctest --test-dir build/ubsan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test)$' --output-on-failure
cmake --build build/tsan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test -j 3
setarch x86_64 -R ctest --test-dir build/tsan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test)$' --output-on-failure

cmake --build build/native-release --target mirage-native mirage-service -j 4
```

| 验证 | 实际结果 | 限制 |
| --- | --- | --- |
| Debug 初轮专项（修正 fixture 后） | 6/6，9.40s | 4 套业务测试及 setup/cleanup |
| 最终 Debug 相关回归 | 18/18，33.04s | 不是本轮全量 CTest |
| 最终 ASAN 专项 | 6/6，11.44s | 无 ASAN 报告 |
| 最终 UBSAN 专项 | 6/6，11.57s | 无 UBSAN 报告 |
| 最终 TSAN 专项 | 6/6，18.45s | 使用既有 setarch，无新 suppression |
| 公开头边界 | 67 头，0 违规 | DEC-096 两处既有缝保持 |
| 受影响源码格式与 diff | clang-format / git diff --check 通过 | 只限本轮相关文件 |
| native-release 前端/服务 | 构建退出0 | PP-OCR/YOLO ON；未启动或安装 |
| 本地 Markdown 链接 | 413处，0缺失 | 决策/计划/设计/示例/验证及反馈台账 |

测试验证：

- 已识别源后连续6个 CV-only 样本：OCR/detector 调用计数不增加，语义状态和
  last_analyzed_frame 不变，结果无 snapshot/continuous/adoption。
- 每次灰度增量2小于门限8，相邻报告均 none；累计到8时相对识别基线报告变化。
  退出采样恢复识别仍与原识别帧比较，随后采样使用新基线，无进入/退出盲周期。
- 无 bootstrap、重复序号、来源/尺寸/策略/attention/importance/evidence 变化、
  不合法阶段组合及超1600边界明确拒绝；重新识别后可在新源/配置继续。
- 预先取消、运行中第二阶段取消、过期 deadline、无效像素失败均不推进水位；
  同序号的后续有效请求仍可成功，且不受失败白帧影响。
- background companion 的 bootstrap 不泄漏到 foreground image companion；
  正常查询仍返回原识别状态。复用既有 host 异常/拒绝/取消/timeout/shutdown
  生命周期矩阵，不新增 worker 或自建同步队列。

开发期：初版试图复制 Mirador move-only ChangeSignature，编译拒绝；改为识别
成功后释放采样 override，避免复制/重新计算。首轮 fixture 的 query 缺少
expected_source_revision，被正确拒绝；补齐显式来源后专项通过。上述失败轮次
不算通过。后续增加末端取消提交点、1600预算和 companion 隔离，按最终矩阵复验。

全仓 mirage-format-check 仍未通过，停在此前已存在且本轮未修改的
`apps/service/host.cpp:292–293`；未混入无关格式化。负责人 Mirage 维护者，在
独立格式清理后补跑。

## 结论与未执行项

仅证明 Adapter 可以不翻转 digest 地进行独立 CV 采样，保持累积变化和原语义
年龄；不能证明生产媒体期间已减少 OCR、实得5/10Hz、跟踪质量或视频理解。
MIRADOR-20261027-001 仍 Open：自动识别复用/覆盖未知投影由 M9-26 继续验证。

Windows/MSVC 同契约、真实 Portal/屏幕、低端长时间资源、在线 token/内容任务
及生产默认切换均未执行。负责人 Mirage 维护者；Windows runner、授权目标桌面、
固定模型/任务及低端设备就绪后按 M9-25/M9-33 补跑，相关任务保持未完成。
本轮未提交、推送、安装或重启，保留开始前的时间拼图及记忆治理修改。
