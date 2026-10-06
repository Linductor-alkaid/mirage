# Mirador 反馈台账

> 状态：Active
> 负责人：Mirage维护者；处理方：Mirador上游
> 更新日期：2026-10-05
> 工作项：[M6-10](../plans/m6-native-frontend.md)
> 总入口：[依赖反馈](ledger.md)；维护规则：[工程规范9.4](../project/project-standards.md)

## 核对基线

pinned版本：`fb0dc3f85393b77845de8bffeab4201dae97c5d9`；能力核对以
`third_party/mirador/include/`与`third_party/mirador/docs/`的公开API、设计、决策为准。
现有[M3验证记录](../plans/m3-mirador-integration.md)未登记Mirador缺口。
本轮仅建立独立台账，不新增Mirador代码或进行新一轮视觉/性能/目标平台验收。

## 问题索引

| 编号 | 主题 | 状态 | 复现/版本 | 上游 | 跟进负责人 |
| --- | --- | --- | --- | --- | --- |

当前没有已确认条目；不向上游提交无证据的问题。不表示所有能力和平台已经验收。

## 登记规则

新增编号`MIRADOR-YYYYMMDD-NNN`，不可复用。每条先排除API误用、平台限制与Mirage
Provider职责，记录版本/公共API/测试核对、最小复现、影响、期望语义、最小能力、延期影响、
临时边界与移除条件、可验收结果、负责人、状态和按日期追加的上游回执/迁移记录。
没有对应能力缺口时保留空台账；Open只表示待处理，上游关闭后仍须授权升级和复验。
