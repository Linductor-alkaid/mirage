# M7-23：检测模型装配与感知修复升级

> 状态：In Progress
> 负责人：Mirage 维护者
> 日期：2026-10-16
> 所属计划：[M7](m7-visual-tools-and-input.md)
> 依据：DEC-097、DEC-016/057/060/068；维护者授权更新修复及接入 YOLO。

- [x] 更新 Mirador gitlink/依赖锁，消费轮廓、变化检测、跟踪性能修复和真实 YOLO 验证。
- [x] 修复 YOLO 模型预处理选择；默认兼容、模型配置与缓存身份可区分。
- [x] 产品 manifest 装配 detector，mark 中融合、编号与引用；离线包支持和供应链证据。
- [x] 坐标、故障、生命周期、真实像素标注与 ASAN/UBSAN 验收。
- [x] 同步文档、创建 PR、记录实际 CI 状态。
- [ ] PR 的跨平台 CI 与评审收尾（维护者；按下述 PR checks 失败项补跑）。

## 验收边界

不得把同输入 ONNX 框一致解释为所有模型框都正确。记录每个有真值目标的重合度、
中心偏差和 anchor 命中；桌面原点、ROI 到全局变换单独验证。无目标真值的宿主/
网页仅作视觉复核。真实点击、多屏/混合 DPI 与 Windows 不作为本批已验结论。

## 验证记录

2026-10-16：实现和本机验收完成，见 [证据](../compatibility/yolo-mirage-integration-20261016.md)。
Mirador [PR #40](https://github.com/Linductor-alkaid/mirador/pull/40)；
Mirage [PR #98](https://github.com/Linductor-alkaid/mirage/pull/98)，保留 In Progress
以记录待评审/CI。当前上游 13/14 checks 成功，clang-format/clang-tidy 仍在运行；
Mirage 8 项 CI 已启动，未将其记为通过。负责人维护者；若失败，按对应 job 日志
复现并修复，CI 完成后再更新工作项。
Release 10/10、严格 ASAN/UBSAN 7/7、真实模型产品标注 11/11 采集与编号映射；
表单最终框 6/6、IoU≥.8429、中心偏差≤1px。未把 11 图映射通过标成整体质量通过。
