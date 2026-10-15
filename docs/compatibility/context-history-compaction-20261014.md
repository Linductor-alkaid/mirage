# DEC-088 能力 2/3 消费验证：工具历史 Layer 0 压缩（2026-10-14）

> 状态：Completed（fixture 范围）；负责人：Mirage 维护者
> 关联：[DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)、
> [MIRA-20261014-006](../dependency_feedback/mira.md#mira-20261014-006)、
> [MIRA-20261014-007](../dependency_feedback/mira.md#mira-20261014-007)、
> [消费审计](../supply-chain/mira-history-compaction-20261014.md)、
> [实施计划](../plans/context-compaction-implementation.md)

## 实现摘要

- 上游缝：mira `9e7b729` → `93e2eae`（授权合并的 PR#96，`set_history_transformer`
  可选宿主历史压缩缝）；mira Debug 全量 108/108，PR 24 项 checks 全绿。
- Mirage 侧：`DesktopContextBuilder`（`integration/mira/src/desktop_context_builder.{hpp,cpp}`）
  只把工具往返段映射为 Layer 0 `ContextItem`（kind=ToolCall/ToolResult、
  authority=VerifiedState、priority=Disposable、consumed=true、
  `replaceable_by_reference=false`——引用标记会丢弃 ToolCallPart/ToolResultPart
  破坏工具协议、tool_call_key=provider_call_id、sequence=原索引），经
  `StandardContextManager::prepare`（`ConservativeTokenCounter`）以桌面水位
  （trim 0.45 / checkpoint 0.70 / hard 0.90、图片预算 10000）取得压缩决策；
  checkpoint 水位单条丢弃拆对时按 audit 将孤侧整对收缩；assistant 文本逐字
  透传，回放保持循环原始条目（Layer 0 只出决策、不出渲染）。
- 接线：`model_layer.cpp` 在声明了上下文窗口时为每个 run 接线该变换；
  `reserved_output_tokens` 取 per-call 线上输出（`config.max_output_tokens_per_turn`，
  与 DEC-087 preflight 同基准）。窗口未声明 fail-open；窗口小于 per-call 输出时
  诚实降级（无可用输入预算，压缩无从帮助）。
- 可观测：loop 侧 `HistoryTransformApplied` / `HistoryTransformDegraded` 事件；
  `DialogCompletion.history_transforms` / `history_transform_degradations` 透传。

## 验证过程（Independent-Verification-Agent，两轮）

第一轮发现高优先级接线缺陷：reserved 误用整 run 输出包络（≥720896），使
`ContextLimits::validate` 在一切现实窗口拒绝、压缩必然降级——修复后复验 PASS。

最终测试 `tests/integration/desktop_context_builder_test.cpp`：**1205 项断言**。

- builder 单测：无窗口/无管理器 fail-open 逐字节返回；无工具条目原样；余量充足
  全保留；压缩触发时旧配对整对消失（call/result 同存同亡、call 先于 result、
  回放为原 history 的逐字节 subsequence、非工具文本逐字保留）；映射经脚本化
  IContextManager 逐字段断言（kind/authority/priority/consumed/
  replaceable_by_reference/tool_call_key/sequence/水位/预算/keys）；两种拆对
  方向整对收缩；manager 错误透传。
- 接线级（ModelLayer + 脚本 provider + fake 桌面）：余量窗口 12 轮任务
  `history_transforms == 12`、`degradations == 0`、全 12 对保留；压力窗口
  （4096 窗口 + 2304 per-call 输出）15 轮 capture 任务真实触发 checkpoint 级
  压缩——`history_transforms == 15`、末请求首轮配对双侧消失、第 15 对在场、
  每个请求配对合法（成对、call 在前、无重复 id）、观测值与 Layer 0 算术逐
  token 吻合；未声明窗口对照 `history_transforms == 0` 且回放完整；微小窗口
  （per-call 输出超过窗口）钉住诚实降级契约。
- 回归：Mirage Debug 全量 **82/82 通过**；ASAN（detect_leaks=1）**1205 checks
  通过、无 sanitizer 报告**；ASAN 下 `harness_desktop_tools_test` 与
  `desktop_image_supply_test` 加验通过。
- 门禁：mirage-format-check、mirage-boundary-check 通过。

## 复验：上游计数器修复（MIRA-20261014-007，PR#97）消费（2026-10-14）

上游 [PR#97](https://github.com/Linductor-alkaid/mira/pull/97)（授权合并
`7391df7`，pin 与 lock 同步）把 `ConservativeTokenCounter::estimate_item`
扩展为计入全部回放载荷字节：`ToolCallPart.arguments` 序列化字节、
`ToolResultPart` 按 tool bridge wire 形态（inline JSON / failed summary /
工件引用）、`ThinkingPart` text+signature。本节记录修复后的 Mirage 消费
复验（Independent-Verification-Agent，PASS）：

- **判别性回归**：新增永久测试
  `large_tool_receipts_drive_checkpoint_compaction`——5 对工具往返、每对
  result 携带 8 KiB 文本回执，16384 窗口 / 1024 预留输出 → 输入预算
  14336 token、checkpoint 水位 10035。新计数估算 **10585 token**（audit
  逐项上界之和）：贪心预算全保留、checkpoint strip 必然触发，断言最老对
  整对丢弃（reason `watermark_checkpoint_drop`）、配对合法、幸存回执逐字节
  保真、desktop 水位 0.45/0.70/0.90 原样声明。
- **旧算术反证**：以脚本化 ITokenCounter 精确复现修复前地板算术
  （单 part 工具项恒 20 token），驱动真实 `StandardContextManager` 跑同一
  夹具：估算 240 token（预算的 1.67%）、零丢弃、水位 Normal、checkpoint
  不触发——证明该测试的压缩确实由载荷字节驱动（新估算约为旧值的 44 倍）。
- 既有接线级测试全部保持：4096 窗口 15 轮 `history_transforms == 15`、
  `degradations == 0`、每请求配对合法、首轮整对消失；余量窗口全量保留；
  微小窗口诚实降级。两处依赖旧地板算术的测试注释已随修复更新。
- 回归：Mirage Debug 全量 **82/82 通过**（本二进制 942 → **1004 项断言**）；
  ASAN 下 `desktop_context_builder_test` + `harness_desktop_tools_test`
  **4/4 通过、无 sanitizer 报告**。

## 已知限制与未执行项

- ~~**水位精度受 [MIRA-20261014-007](../dependency_feedback/mira.md#mira-20261014-007)
  影响**~~ 已解决：上游 PR#97 计入工具载荷字节后，压缩触发由真实载荷规模
  驱动（见上方复验章节）；大载荷少轮次任务（如 5 轮 16KiB OCR）现在按
  载荷越过水位触发。
- 真实会话验收（15 轮任务 input tokens −30%~−40%、DEC-088 Phase 3 端到端
  回归）未执行：需真实模型 API 与桌面环境。负责人：Mirage 维护者；补跑条件：
  真实 provider 桌面会话环境。
- TSAN / Windows 平台未执行（缝回调为 run() 调用者线程内同步逻辑；测试无平台耦合）。
