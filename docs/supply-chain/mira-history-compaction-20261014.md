# Mira 升级审计：会话历史压缩缝（2026-10-14）

> 状态：Completed（公开 API 与本地消费范围）
> 授权：维护者授权上游开发 PR 与合并后更新 pin（2026-10-14 会话指示）。
> 关联：DEC-088 能力 2/3、MIRA-20261014-006、context-compaction-implementation 计划。

## 来源与锁定

旧 pin 为 `9e7b729d0861577b470d45385bdd08301bd1226d`（PR#95），新 pin 为
`93e2eae6ceb6d2d0ce99e41601fcaf7ed0d91e04`，来自
[Mira PR#96](https://github.com/Linductor-alkaid/mira/pull/96) 的 squash 合并
（2026-10-14 合并，合并前 24 项 checks 全部通过：android/linux/gcc+clang/
quality/sanitizers/windows）。gitlink 和 dependencies.lock.json 同步。Mira、嵌套
kairo/mbedtls、Mirador、EUI 许可证与嵌套 pin 不变，无新增依赖。

## 差异和承载边界

- `HistoryTransformer` / `ConversationLoop::set_history_transformer`（DEC-088
  能力 2/3 缝）：每轮装配时把循环持有的规范化工具往返 history 交给宿主回调一次，
  以返回列表替代**本轮回放**；循环自有 history 永不改写。协议契约：返回条目
  role 仅限 User/Assistant 且工具调用与结果配对。降级面：Error（附 code）、
  异常、空结果（往返历史是模型对已执行工作的唯一记忆）、System role 或空内容
  条目——一律发 `HistoryTransformDegraded` 状态事件并回退原始回放；成功发
  `HistoryTransformApplied`（items_in/items_out）。
  `ConversationLoopResult.history_transforms` / `history_transform_degradations`
  计数可观察。未接线或 history 为空（首个模型轮）时请求装配逐字节不变。
- API 文档 `docs/api/model-agent-loop.md` 新增"工具历史压缩缝"章节（含配对
  收缩的宿主职责说明）。台账登记的渲染器 `role_for` 角色保真约束由此规避：
  缝形态为通用 history 变换，不改 Layer 0 契约。

Mirage 侧消费（集成层）：`DesktopContextBuilder` 只把工具往返段映射为 Layer 0
`ContextItem`（ToolCall/ToolResult、consumed、`replaceable_by_reference=false`
防止引用标记破坏工具协议、配对键 provider_call_id、sequence=原索引），以桌面
水位（trim 0.45 / checkpoint 0.70 / hard 0.90、图片预算 10000）调用
`StandardContextManager::prepare` 取压缩决策；checkpoint 水位单条丢弃可能拆散
配对，builder 按 audit 将孤侧整对收缩；assistant 文本逐字透传，回放保持循环
原始条目（Layer 0 只出决策、不出渲染）。`model_layer.cpp` 在声明了上下文窗口
时接线该变换（fail-open：未声明窗口保持原始回放），测试与验收见
[历史压缩消费验证](../compatibility/context-history-compaction-20261014.md)。

## 实际验证

上游：mira Debug 全量 ctest **108/108**（含新增
`m3_conversation_history_transform_test`：透传逐字节一致、压缩事件与计数、
四类降级、取消轮不调用）；format / docs / sbom / platform-boundary /
architecture 门禁全部通过；PR#96 24 项 checks 全绿后经授权合并。
Mirage 侧消费验证由 IVA 独立执行，结果见消费验证文档。
