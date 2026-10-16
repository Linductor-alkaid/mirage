# Mira 升级审计：输入复用身份缝与续写锚点（2026-10-14）

> 状态：Completed（公开 API 与本地消费范围）
> 授权：维护者授权上游开发 PR 与合并后更新 pin（2026-10-14 会话指示）。
> 关联：DEC-088、MIRA-20261014-005、context-compaction-implementation 计划。

## 来源与锁定

旧 pin 为 `22d95776d701c92656ecd583f85982d5b0d303fe`（PR#94），新 pin 为
`9e7b729d0861577b470d45385bdd08301bd1226d`，来自
[Mira PR#95](https://github.com/Linductor-alkaid/mira/pull/95) 的 squash 合并
（2026-10-14 合并，合并前全部 checks 通过：android/linux/quality/sanitizers/windows）。
gitlink 和 dependencies.lock.json 同步。Mira、嵌套 kairo/mbedtls、Mirador、EUI
许可证与嵌套 pin 不变，无新增依赖。

## 差异和承载边界

- `ModelInputIdentityProvider` / `set_model_input_identity`（DEC-088 能力 1）：
  宿主按请求解析一次 `InputReuseMeta`（content_digest / reuse_count /
  semantic_label）；循环只做渲染决策——`reuse_count >= 2` 且供给含图时，在主
  User 信封末尾追加一条 Internal 权限 System note，不进 System role、不提升
  权限。无回调时请求逐字节不变；回调异常按 DEC-053 纪律降级为可见事件。
  `ConversationLoopResult.supplied_input_reuse_annotations` 计数可观察。
- 续写锚点差异化（DEC-088 能力 4）： tolerated max_tokens 截断的三类恢复反馈
  ——部分文本从末 50 字符锚定续写、工具调用截断声明已丢弃并要求完整重试、
  仅思考截断要求简洁纯文本——在既有 PR#92/93 形态上补齐措辞与锚点。
- 事件与统计面只增不改；`ModelInputSupplier` 返回类型不变，与既有三方言
  fixture 兼容。

Mirage 侧消费（集成层，无自研重复实现）：`DesktopImageTurn` 持有跨轮
digest/计数（只有真实含图供给推进计数；降级/退役轮暂停，digest 变化清零），
经 `set_model_input_identity` 接线；`ObservedProvider::anchored_remaining`
以 DEC-087 锚点为 `DesktopImageTurn` 提供供给时预算信号，余量 ≤20000 附加
跳过细读提示、≤10000 扣图改文字描述（fail-open：无窗口声明或无锚点不降级）。

## 实际验证

Mira Debug 全量 107/107（含新增 `m3_conversation_input_reuse_test`）；远端
PR#95 checks 全部通过后经授权合并。Mirage 侧身份接线与降级阈值的单元与
行为验证、ASAN/UBSAN 门禁由
[复用标注与降级验证](../compatibility/context-reuse-degradation-20261014.md)
跟踪（见该证据文档的实际执行结果）。
