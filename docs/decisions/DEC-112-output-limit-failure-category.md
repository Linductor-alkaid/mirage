# DEC-112：输出上限截断的结构化失败类别与 dialog 正文保留

> 状态：Accepted
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 关联：DEC-085 / DEC-087 / DEC-104、依赖反馈 MIRA-20261022-001

## 问题与决策

输出 token 截断在 Mirage 的两条对话路径上有三种不一致的失败表达：dialog 路径把
`Incomplete(MaxOutputTokens)` 与其他未完成状态折叠成同一条通用错误并丢弃部分
正文；harness 路径续写预算耗尽后只有 `safe_summary` 文本可判别；IPC 对话轮面
（`session.chat_updated` / `session.chat.history`）只有 `failed` + 文本，消费者
必须解析错误文案才能识别截断，事件订阅方则完全不可观测。

决策分三层，全部复用既有面、不新增事件类型、不改 `pending/ok/failed` 状态词表：

1. 集成层（`integration/mira`）：`DialogCompletion` 增加结构化
   `output_limit_truncated` 标记。dialog 路径对
   `Incomplete(MaxOutputTokens)` 单列分支——以专用错误结算，并按 DEC-104 的
   原则保留截断部分正文（`reply_text` 携带，经既有 terminal text Part 通道
   展示）；harness 路径在适配边界内以 `outcome == Failed`、
   `output_continuations > 0` 与 pinned 耗尽 summary 文本锚点联合判别（临时
   方案，见 MIRA-20261022-001，上游结构化字段落地后移除）。
2. IPC 面：`DialogTurnEntry` 与 `ChatTurnUpdatedEvent` 增加 encode-when-set 的
   `error_kind`（存在当且仅当 `status == "failed"`），封闭词表
   `{"output_limit"}`，新类别按 DEC-012 增量加入；decode 校验词表与
   present-exactly-when 约束，缺省表示未分类失败，向后兼容。
3. Runtime Service：settle 与 history 投影把类别透传到事件与快照两个事实源。

自动续写仍是 harness 路径的既定策略（DEC-087，对齐 ZCode 的有界续写）；本决策
只补观测与表达，不改变续写行为。dialog 路径保持单请求形态，"截断后引导用户
续问"（deepseek-harness 模式）由类别字段支撑，属后续产品化选项，不在本决策内。

## 验证与限制

验证覆盖：harness 续写耗尽场景的结构化标记、dialog 截断的专用错误与部分正文
保留（有正文/无正文两态）、IPC 事件与 history 快照的 `error_kind` 往返、
decode 对未知类别与 present-exactly-when 违例的拒绝、既有失败路径不携带类别。
类别字段只覆盖截断一类；其余失败仍为未分类。上游 MIRA-20261022-001 落地前，
harness 判别依赖 pinned summary 文案，属已知临时边界。
