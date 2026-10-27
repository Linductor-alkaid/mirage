# DEC-118 显著性门控与渐进快照验收记录

> 日期：2026-10-27（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-23；决策：[DEC-118](../decisions/DEC-118-significance-gated-perception-and-scoped-snapshots.md)
> 验证方式：独立验证（Independent-Verification-Agent）编写并执行测试，主循环修复，
> 二轮复验通过；本文只记录可复现命令与结果边界。

## 环境

Linux x86_64，GCC 13.3.0，HEAD 为 f3addab 之上的工作树（本分支
feat/continuous-semantic-binding）。测试不操作用户桌面；AT-SPI 场景运行于私有
D-Bus + 脚本化 WindowProvider，无需真实 X11/Portal。

## 命令与结果

```bash
cmake --build build/debug -j
ctest --test-dir build/debug -R 'desktop_atom_toolset_test|local_semantic_workflow_test|\
task_perception_test|continuous_perception_consumer_test|desktop_image_supply_test|\
provider_contract_test|desktop_observation_test|atspi_backend_test|\
atspi_scoped_snapshot_test|visual_scene_test|fake_visual_backend_test|\
continuous_capture_test|significance_gating_test' --output-on-failure
# 100% tests passed, 0 failed out of 15
```

| 测试 | 结果 | 覆盖 |
| --- | --- | --- |
| significance_gating_test（新） | 116 checks, 0 failures（两次运行稳定） | 微变化抑制、媒体确认/静默、关键帧节流、miss→stopped、OCR 全周期识别、实体事件不门控、kGlobal 屏障（e2）、gate off 回退、journal media 透传 |
| atspi_scoped_snapshot_test（新） | 48 checks, 0 failures | max_depth 下钻与 `(+N children)`、subtree 展开、过期 ref not_found、窗口改名回退解析、多窗口歧义 |
| provider_contract_test | 463 checks, 0 failures | 折叠节点渲染 |
| task_perception_test | 134 checks, 0 failures | 既有实体生命周期无回归 |
| desktop_image_supply_test | 4479 checks 全通过 | focus 工具契约（新增） |
| desktop_atom_toolset_test | 161 checks, 0 failures | 不支持 scoped 后端的明确拒绝（新增） |
| 其余 9 项既有套件 | 全部通过 | 无回归 |

ASAN/UBSAN 专项（同工作树 build/asan、build/ubsan 目标构建）：受影响套件
（significance_gating、atspi_scoped_snapshot、task_perception、
continuous_perception_consumer、desktop_image_supply）无内存错误/未定义行为报告
（门控测试在 sanitizer 下运行为场景重写中间态；最终场景集以 debug 复验为准，
生产代码在两次 sanitizer 运行间无差异）。

## 独立验证发现并修复的缺陷（两轮）

1. 媒体状态机坐标空间混用（归一化存储 vs 像素比较）——修复为比较前 denorm。
2. 预测式 OCR hold 被 mirador 准入拒绝/产生盲周期——撤回 hold（OCR 恒全帧），
   登记 [MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)。
3. kGlobal 帧误确认媒体候选（task_perception 回归）——kGlobal 不作确认证据；
   已活跃区域跨 kGlobal 由几何包含继续抑制。

## 未覆盖/保留项

- `--perception-period-ms` CLI 与 ServiceConfig 链路无可执行 harness（clamp 在
  connect_continuous_desktop 层已测）。
- keyframe 节流的真实时钟跨界、Windows UIA scoped 能力位、TSAN 专项。
- 10Hz 采样 CPU/RSS、真实 Portal 媒体任务在线 A/B、fullscreen 播放的事件抑制
  实机行为——保留在 M9-12/M9-23 验收表，不以单测宣称。
- MIRADOR-20261027-001 的可验收结果依赖上游 evidence-carryover 交付后复验。
