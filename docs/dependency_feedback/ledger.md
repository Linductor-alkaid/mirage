# 依赖问题反馈台账

> 状态：Active
> 负责人：Mirage维护者
> 更新日期：2026-10-05
> 维护规则：[工程规范9.4](../project/project-standards.md)
> 本轮工作项：[M6-10](../plans/m6-native-frontend.md)

## 分依赖台账

| 依赖 | 详细台账 | 已确认问题 | 说明 |
| --- | --- | --- | --- |
| Mira | [Mira台账](mira.md) | 1项Open、3项Resolved、1项Accepted | Runtime/模型/工具/workflow及随Mira交付的Executor；本轮先向Mira反馈 |
| Mirador | [Mirador台账](mirador.md) | 0项 | 明确初始化为空；有可复现证据后登记，不推断不存在未来问题 |

executor经pinned Mira传递引入，编号使用`MIRA-YYYYMMDD-NNN`，不直接向Executor提交。
Mirador使用`MIRADOR-YYYYMMDD-NNN`。EUI是用户授权的UI依赖，其既有问题仍在
[独立EUI台账](eui-ledger.md)，不混入本索引。台账不改变依赖锁定或引入额外并发设施。

条目必须包含版本/API核对、现象与复现、应用职责排除、影响、最小期望能力、延期影响、
验收与临时措施移除条件；每项维护负责人、Open/Proposed/Accepted/Resolved/Rejected及
上游链接/复核时间。缺陷状态与提交状态分开；上游修复后须授权升级并复验后再标记Resolved。

## 历史编号入口

以下标题保留原锚点，已有代码/文档引用仍有效；详细证据和跟进在分依赖台账维护。

## MIRA-20260922-001：executor 的 MinGW-w64 交叉构建失败

详见 [该条目](mira.md#mira-20260922-001)。

## MIRA-20260927-001：同内容草稿记录遮蔽可运行版本

详见 [该条目](mira.md#mira-20260927-001)。

## MIRA-20261004-001：通用对话 harness 缺少无观察循环入口

详见 [该条目](mira.md#mira-20261004-001)。

## MIRA-20261004-002：OpenSSL TLS Adapter 未发送 SNI

详见 [该条目](mira.md#mira-20261004-002)。

## MIRA-20261005-001：真实流式预览与Chat Completions SSE

详见[该条目](mira.md#mira-20261005-001)。

## MIRA-20261006-001：Messages协议

公开API核对、影响和验收见[Mira台账](mira.md#mira-20261006-001anthropic-messages方言缺口)。本轮Resolved（Linux本地范围），[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已创建未合并；无临时调度设施。
