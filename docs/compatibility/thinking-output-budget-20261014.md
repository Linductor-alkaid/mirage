# DEC-082 验收记录：思考型输出预算地板提升（16384 → 32768）

日期：2026-10-14
范围：`integration/mira/src/model_layer.cpp`（`kThinkingOutputFloor`）、`model_layer.hpp` 注释
决策：[DEC-082](../decisions/DEC-082-reasoning-default-and-harness-budgets.md) 2026-10-14 部分更新
实证：会话 `db6b8942…` 第三轮（2026-10-14 02:42，「重试一次」）

## 失败实证

DEC-086 修复后重试「查看存储空间」：应用启动全部 `dispatched/verified`
（DiskUtility、baobab、窗口点击均直接派发——围栏死锁已消除），任务推进正常
（16 步模型 / 14 次工具调用），但最后一轮模型响应在**纯思考阶段**连续三次撞
每调用 16384 输出上限（原始 + 2 次续写），以
`output-limit continuation budget exhausted` 显式失败。

## 端点探测证据（api.deepseek.com/anthropic，deepseek-flash）

| 探针 | 参数 | 结果 |
|---|---|---|
| A | max_tokens=16384 + adaptive | `stop=max_tokens`，16384 全部耗在 thinking（34422 字符），无可见文本 |
| D | max_tokens=32768 + adaptive | 端点接受；`stop=end_turn`，输出 21905 tokens（thinking 38368 字符 + 正文 4965 字符）完整收尾 |
| E | 16384 + adaptive + effort=low | 仍在 16384 截断（思考略短，24942+3374 字符）——effort 非解法 |
| F | 16384，省略 thinking 字段 | **仍产生 thinking**（33223 字符）并截断——该端点思考不可关闭且计入 max_tokens |

结论：思考强度与输出预算在该端点强耦合且思考不可关；16384 对复杂观察任务的
自然思考深度必然不足。32768 实证可用（对齐用户参考的 ZCode 默认 32000 量级）。

## 修复

`generation_budget` 思考档地板 16384 → 32768（新常量 `kThinkingOutputFloor`），
21x profile 资金化同源更新（信封 21×32768=688128）；有界续写（2 次）保持兜底。
测试锚定同步：`native_agent_integration_test`（floor/资金化断言）、
`harness_desktop_tools_test`（344064→688128、16384u→32768u 及注释）。

## 验证

Independent-Verification-Agent 独立执行：`harness_desktop_tools_test` 793 checks
全绿（floor 支配 + 688128 信封锚定）；`native_agent_integration_test` 406 checks
全绿（锚定修正一处：`default_dialog` 场景声明 `context_window_tokens=32768`，
线上值按设计钳制为 `min(32768, max(4096, window/2)) = 16384`——旧锚定 16384 恰好
同时是旧地板与钳制值，地板提升后两者分离）；`post_action_verification_test`
无回归；全量 ctest 85/87（仅 2 例既有 Xvfb+GTK 环境失败，前轮已用改动前代码
复现）。

## 残余观察（补充）

- **窗口钳制可抵消新地板**：显式声明 `context_window_tokens < 65536` 的模型，
  思考档线上预算被钳回 `window/2 < 32768`，本文记录的截断问题会复现。当前
  配置的三个模型（deepseek 无声明、MiniMax/HUA 声明 1M）均不受影响；钳制语义
  （reply 须与 prompt 同窗）是有意策略，声明窗口的思考模型预算策略留待后续
  决策（如剩余上下文估算替代静态钳制）。
- 模型多轮花费 1-5KB 思考纠结「attach 的截图与工具结果图片身份」（同一截图
  经工具结果与下一轮 supply 重复出现）——是思考深度放大的质量侧因素，记录为
  图片供给去重的后续观察，不在本修复范围。
- `config.max_output_tokens` 高于端点 wire 上限时依赖 provider 显式报错（既有
  SSE 降级路径可见），未做本地声明表。
- 真实端点验收：重跑存储空间任务（third turn 即本修复的验收环境），预期单次
  调用内完成含思考的完整回答。
