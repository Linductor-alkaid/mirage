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
- [x] 2026-10-23 本机构建交付跟进：native-release 配置已锁定 UI 权重，重建 DEB，
  核对包内 detector/权重/许可证并实际执行检测；依据 DEC-097，
  [验收记录](../compatibility/yolo-native-package-20261023.md)。

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

2026-10-16 / CI 修复与合并授权：维护者要求修复全部 CI 并在验证通过后合并。
Mirage run 38912335032 的格式门禁在 apps/service/host.cpp 首次失败；按门禁
相同递归清单检查 309 个文件，共发现父工作分支遗留的 25 个格式不合规文件，
仅以 clang-format 18.1.8 修正格式。完整格式检查及 60 头/0 违例边界检查通过。
其余 CI 和上游 #40 的静态分析仍待完成；最终合并以对应最终提交全部检查成功为条件。

2026-10-16 / CI 根因补齐：Linux debug/release/ASAN/UBSAN/TSAN 均复现相同
两项失败，没有 sanitizer 新诊断。SessionClient 的 DEC-094 只应用了调用端超时，
未跳过过期队列帧/迟到响应且 run() 仍保留关闭 sweep；按已提交修复补齐，增加
跨 slice 排队、16 总量准入及停止后拒绝。几何回归改为核对外梯度边界
`[7,7,33,25]`、越界 context `[-1,-1,49,41]`、执行 ROI `[0,0,48,40]`
及全局框 `[100,0,48,40]`，保持精确坐标断言。

Mirador run 38909877501 全部 14/14 检查成功，已按维护者授权合并 #40；
Mirage 改 pin 合并提交 cb45a10（与验证头 932d425 的树相同）。修复回归的
Release 与严格 ASAN/UBSAN 各 4/4（含状态 fixture）成功；Mirage 最终跨平台 CI 仍待完成。

2026-10-23 / 本机构建补充模型：维护者要求将 YOLO 补入当前构建。复用已转换且
摘要匹配的本机模型，显式配置 `MIRAGE_YOLO_MODEL_DIR`；native-release 与 DEB
均包含 detector、权重、锁文件和许可证。完整 Release 120/120、0 skip，147.22 s；
构建与解包清单分别完成表单/空白真实推理各 2/2，YOLO 每图一次，像素与编号引用检查
通过。新包已交付至 out/packages，旧包保留备份；不据此关闭既有跨平台/评审待办。
命令、摘要和未验范围见上述验收记录。
