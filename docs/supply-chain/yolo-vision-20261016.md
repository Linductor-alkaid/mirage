# YOLO 视觉候选供应链（2026-10-16）

M7-23 / DEC-097，维护者要求接入真实检测模型。复用 Mirador opt-in detector_yolo
和既有 ncnn commit `e54f7b1f88434e1d844ea0551b880a1cfb079ce1`；无新增 runtime。
Mirador 更新包含 PR #37/#39 与 issue #38 预处理配置修复，本次固定 SHA `932d425fe114f0b5b3919cde2ef49ba5d2d5b0ac`，见 [PR #40](https://github.com/Linductor-alkaid/mirador/pull/40)；为待评审分支 pin，未合并 master。

离线 UI 权重为 OmniParser v2 `icon_detect`，固定来源、原 checkpoint 与转换后的
param/bin 大小及 SHA-256 见 `packaging/vision/yolo.lock.json`。来自 Mirador 已验证的
离线转换工具；源码 checkpoint、ONNX 和权重均不入 Git。构建只从显式本地目录读取，
不默认拉取模型、不运行转换框架、不在用户运行时下载。

`icon_detect` 模型按 AGPL-3.0 管理，Mirage 已消费 AGPL-3.0 的 Mira；分发时随 vision
包保留模型来源、转换说明、锁文件及完整许可证。Mirador MIT、ncnn BSD-3-Clause
既有 notices 继续保留。转换权重不改变训练参数，仅归一输出布局。

未指定离线模型目录时，产品仍装配 OCR；显式声明检测器后任何缺失/损坏均拒绝启动。
已测试的模型是单类 UI 候选，不等于通用目标识别，也不能证明控件可用或动作成功。
