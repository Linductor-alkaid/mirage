# 非 Anthropic 方言思考展示验收（2026-10-08）

> 状态：Completed（Linux 本地范围）
> 负责人：Linductor-alkaid
> 关联：MIRA-20261008-001（上游 [PR#84](https://github.com/Linductor-alkaid/mira/pull/84)、DEC-052）、[依赖升级审计](../supply-chain/dependency-upgrade-audit.md) 2026-10-08 节
> 本轮工作项：M6-26 后续（DT-04 宿主侧复验 + 会话展示闭环）

## 范围与结论

mira PR#84 升级后，Chat Completions 方言的模型思考输出（`reasoning_content`，
含流式增量）以有界 `ThinkingPart` 到达会话过程，并在真实原生 UI 中默认渲染为
折叠的"思考过程"行、点击展开显示全文。Messages（MiniMax-M3，adaptive）回归
正常。未发现产品缺陷。

## 验证分层

1. **服务级（纯对话与 harness 双路径）**：私有 socket 直连 Runtime Service，
   SiliconFlow `Qwen/Qwen3.5-4B`（chat-completions）settle 回合携带 kind="thinking"
   part（纯对话 1059 字符；`agent=true` harness 路径 95 字符）；MiniMax-M3
   adaptive 回归（107/285 字符）。
2. **真实 UI 端到端**：`tests/manual/native_conversation_acceptance.py
   --dialect-thinking`（私有 Xvfb/DBus/ibus，真实 mirage-tray + mirage-native），
   经真实 composer 发送、settle 后断言 thinking part、目视核验截图：
   折叠态为单行"思考过程/已完成"（chevron 朝右），点击后展开显示思考全文并
   出现复制入口。结果 `thinking_parts=1`、`thinking_chars=168`、展开点击
   (700,152)、diff bbox [320,152,1112,320]。驱动脚本同期修复三处自身问题
   （`--dialect-thinking` 子进程参数转发、history 轮询缺 session_id 的协议
   拒绝、展开判定排除 composer 光标伪影），产品源码零改动。

## 证据

- 私有运行产物：`dialect-thinking-results.json`、`thinking-collapsed.png`、
  `thinking-expanded.png`（/tmp 私有验收输出，证据描述如上，不入库）。
- 验收模块：[native_dialect_thinking_acceptance.py](../../tests/manual/native_dialect_thinking_acceptance.py)。

## 剩余范围

- Responses 方言无真实 reasoning 供应商（SiliconFlow 无 /v1/responses、
  HUA 上游剥离 reasoning）：该方言以上游 fixture（增量/终态/逐字节切点/
  redacted/预算失败闭合）为验收依据，真实供应商补跑条件与上游 DT-04 一致。
- 折叠行的再次折叠（toggle back）未断言；多轮往返与多 provider 同方言
  抽样留待后续。
- 部分供应商（如 HUA）上游不返回思考内容，此时无思考行属供应商行为，
  非丢弃缺陷。
