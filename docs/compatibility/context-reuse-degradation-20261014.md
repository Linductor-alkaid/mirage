# DEC-088 能力 1/5 消费验证：输入复用标注与预算感知图片降级（2026-10-14）

> 状态：Completed（fixture 范围）；负责人：Mirage 维护者
> 关联：[DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)、
> [MIRA-20261014-005](../dependency_feedback/mira.md#mira-20261014-005)、
> [消费审计](../supply-chain/mira-input-reuse-20261014.md)、
> [实施计划](../plans/context-compaction-implementation.md)

## 实现摘要

- pin 消费：mira `22d9577` → `9e7b729`（授权合并的上游 PR#95，capability 1 身份缝 +
  capability 4 续写锚点差异化）；mira Debug 全量 107/107。
- 能力 1（Mirage 侧）：`DesktopImageTurn` 持有跨轮 digest/计数
  （`offered_digest_`/`reuse_count_`），`reuse_identity()` 暴露 `InputReuseMeta`，
  `model_layer.cpp` 经 `set_model_input_identity` 接线；只有真实含图供给推进计数，
  退役/降级/无图轮暂停（不重置），digest 变化清零。
  `DialogCompletion.supplied_input_reuse_annotations` 透传循环计数。
- 能力 5（Mirage 侧）：`ObservedProvider::anchored_remaining` 以 DEC-087 锚点给出
  供给时剩余预算（窗口 − provider-reported 输入 − 1000 预留，上界语义），
  `DesktopImageTurn` 按阈值带降级：>20000 正常；10000<余量≤20000 图片 + Internal
  警告；≤10000 扣图改文字描述（含剩余 token 与最后图片尺寸），`expect_image_`
  不置位故 `validate_request` 不要求图片。无窗口声明或无锚点 fail-open 不降级。

## 实际执行的验证（Independent-Verification-Agent，两轮）

测试：`tests/integration/desktop_image_supply_test.cpp` 新增 4 个测试函数 + 2 个
fixture（`input_reuse_identity` / `budget_degradation` / `anchored_remaining_unit` /
`reuse_annotation_end_to_end`），85 条新断言；基线 910 → 995 checks，负向对照
（人为破坏一处断言）确认 harness 可捕获。

- 复用身份：首次含图供给 meta（count=0，digest == `ImagePart.source.digest`
  十六进制）；连续供给 1→2；重捕获内容变化清零；退役轮/降级轮/无图轮
  `reuse_identity()` 为空且计数暂停。
- 降级阈值带：25000/nullopt 无警告；20000/15000/10001 图片 + 恰 1 条
  `[Context budget low]` 警告；10000/8000 无 ImagePart、文本含剩余 token 与
  "Last image: 128x64 pixels."，且 `validate_request` 通过（不要求图片）。
- `anchored_remaining`：无窗口/无锚点 nullopt；`Estimated` 用量不写锚；
  ProviderReported 4200 + 窗口 50000 → 44800。
- 端到端（脚本化 provider + fake 桌面，3 轮恒定像素 capture + 1 轮应答）：
  `supplied_input_reuse_annotations == 1`，仅第 4 请求含 Internal
  `[System note: The desktop screenshot above is the same as in the previous 2
  turns...]`，前 3 请求无。
- 回归：Mirage Debug 全量 **81/81 通过**；ASAN 变体（detect_leaks=1）
  **995 checks 通过、无 sanitizer 报告**。
- 契约同步：pin 9e7b729 将部分文本续写反馈改为锚点式（"Continue from: \"…\""），
  `tests/runtime/harness_desktop_tools_test.cpp` 两条旧文案断言已同步为
  `Continue from: "The window list shows"` / `Continue from: " two windows open"`
  （锚点可由脚本 partial 与 `kAnchorChars=50` 独立推导），复验通过。
- 门禁：mirage-format-check、mirage-boundary-check（0 violations）通过。

## 未执行项与补跑条件

- 真实会话对照验收（DEC-088 Phase 2/3 验收：有/无标注思考 token −10%~−20%、
  15/30 轮任务 tokens 曲线、压缩触发观察）未执行：需要真实模型 API 与真实桌面
  环境。负责人：Mirage 维护者；补跑条件：配置真实 provider 的桌面会话环境后按
  实施计划 Phase 3 场景执行。
- 能力 2/3（Layer 0 集成 + 桌面水位）未实施，被
  [MIRA-20261014-006](../dependency_feedback/mira.md#mira-20261014-006) 阻塞
  （ConversationLoop 历史回放无宿主压缩缝，含渲染器角色保真约束证据）。
- TSAN/UBSAN 本轮未跑（改动为集成层纯顺序逻辑；mira 侧维持既有纪律）。
