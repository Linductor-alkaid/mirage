# DEC-087 验收记录：ZCode 对齐的输出预算与上下文 preflight

日期：2026-10-14
范围：`integration/mira/src/model_layer.cpp`、`integration/mira/src/conversation_capture.hpp`、
`integration/mira/include/mirage/integration/model_layer.hpp`
决策：[DEC-087](../decisions/DEC-087-zcode-aligned-output-budget-and-context-preflight.md)
验证：Independent-Verification-Agent 独立执行（结果见文末）

## 改动

1. 声明值语义：`ModelLayerConfig.max_output_tokens` 缺省 2048 → 0（未声明），声明值
   原样使用；未声明基线 32000；思考档 `max(基线, 32768)`；validation 放行 0。
2. preflight：`ObservedProvider::infer` 每请求把 wire 预算收缩为
   `min(基线, 窗口 − 已用量 − 1000)`，余量非正保留基线，窗口未声明不收缩。
3. 用量锚点：provider 报告 input（含 cache）+ pinned 估算器增量；会话级有界锚点表。
4. 续写深度 2 → 3；envelope 资金化 21x → 22x（720896 @32768）。

## 测试结果（build/native-release，Independent-Verification-Agent）

- **85/87 通过**（仅 2 例既有 Xvfb+GTK 环境失败）；ASAN/UBSAN 目标套件通过。
- `harness_desktop_tools_test`：720896 信封两处、thinking 32768/512、续写场景
  (a)(b)(c)(d) 全绿——(c) 重写为 3 深度成功（4 请求、三个 partial 全部重放），
  (d) 新增第 4 次截断耗尽（**4 请求**：初始 1 + 续写 3；settle 不再调用模型，
  初版断言 5 为笔误，已由验证代理依 `conversation_loop.cpp:392` 语义修正）。
- `native_agent_integration_test`：default_dialog 的 preflight wire 值实测锚定
  **31341** = 32768 基线 − 427 本地估算（3 个 text part 共 1275 字节 → Σ⌊b/3⌋+3）
  − 1000 预留；m3_budget 场景 16 轮不变（per-request 路由 envelope = per_turn×19
  ≪ 720896，截断深度与轮次无关，成因已注释）。
- 新增 `preflight_budget_scenario`（3 子场景、6 断言）：窗口收缩精确式
  （6000−115−1000=4885）、无窗口对照（8192 基线）、锚点增量（请求 2 wire=4000=
  12000−7000 锚点−0 增量−1000，非空洞——无锚点路径会停在 8192）、余量为零回退
  基线、余量 1 精确收缩（wire=1）、无窗口直通声明值。
- 诊断运行同时实证 many 场景每请求按锚点增量逐步收缩（−1234 递减）、m3_budget
  全程回退基线、"关闭思考"只缩不涨（512=min(基线,余量)）。

## 残余观察（验证代理提出，已确认接受）

1. **极端收缩无下限**：窗口略高于 已用+1000 时 wire 预算可低至 1，必然触发截断-
   续写循环。ZCode 语义本身无下限且由 compact 兜底；Mirage 以续写深度 3 + 显式
   失败收口。为忠实对齐保留原语义。
2. decorator 只收缩 `generation.max_output_tokens`（wire 真值），请求的
   `budget.max_output_tokens`（整轮声明，宿主边界准入用）保持原值——两者语义
   不同步改是刻意的，已加代码注释防误读。
3. 锚点表随 ModelLayer 重建（模型切换）清空，回退纯本地估算，预期交互。
