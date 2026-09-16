# 依赖问题反馈台账

> 状态：Active
> 维护规则：[工程规范第 9.4 节](../project/project-standards.md)
> 更新日期：2026-09-16

本台账登记 Mirage 对两个直接 pinned 依赖 —— `third_party/mira` 与
`third_party/mirador` —— 的能力缺口反馈与获批例外。编号按依赖区分：
`MIRA-YYYYMMDD-NNN`、`MIRADOR-YYYYMMDD-NNN`。"依赖不支持"不构成有效记录，条目必须附
可复现证据、影响范围、期望语义与可验收结果。

executor（`third_party/mira/third_party/executor`）由 mira pin 并交付，属于 mira 的
集成面：Mirage 发现的 executor 层缺口以 `MIRA-*` 条目登记，由 Mira 上游经其自身的
executor 反馈流程消化；Mirage 不直接向 executor 反馈，也不在台账中单列 executor 条目。

| 编号 | 日期 | 主题 | 分级 | 状态 | 关联实现/测试 |
| --- | --- | --- | --- | --- | --- |
| （暂无条目） | | | | | |
