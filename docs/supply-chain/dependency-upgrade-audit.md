# 依赖升级审计

本文件记录 Mirage 直接依赖（pinned `mira`、`mirador` 及其内嵌依赖）的每次升级审计：
版本差异、对 Mirage 接触面的影响、许可证核对与回归验证证据。依据
[工程规范](../project/project-standards.md) 第 9.1 节与第 10 节（依赖升级走独立 MR、
在 `docs/supply-chain/` 记录审计结论）。

---

## 2026-09-20：mira → cf0af75、mirador → 50f5349

- **分支**：`chore/deps-upstream-sync`（升级前两者分别 pin 在 `255e4cf`、`fbcf6c4`）。
- **动机**：上游 master 前进，跟进上游能力交付；无 Mirage 侧能力缺口驱动
  （`docs/dependency_feedback/ledger.md` 本次无新增条目）。

### 版本差异

| 依赖 | 旧 pin | 新 pin | 上游主要交付 |
| --- | --- | --- | --- |
| mira | `255e4cf` | `cf0af75` | M7 工具模块（TM0 契约重定义 DEC-042、TM1 registry lifecycle、TM2 LLM exposure projection）；内嵌 executor pin 升级 |
| mira → executor | `e2dc8ca`（v0.4.0-100） | `2ae4fc8`（v0.5.0） | 正式发布区间（`e2dc8ca..2ae4fc8`）仅含打包/发布脚本与 src 内部修复（task_monitor、lockfree_queue）；公开 `include/` 零 diff——任务级协作取消（`task_cancellation.hpp`）、TimerHandle（`timer.hpp`）、串行上下文、总量有界 admission 等在 v0.4.0-100 时已全部在 master 上 |
| mira → mbedtls | `068ff08`（v3.6.7） | 不变 | — |
| mirador | `fbcf6c4`（v0.1.0-18） | `50f5349`（v0.2.0-17） | v0.2.0：M5 display contract、X11/Windows GDI/Android MediaProjection capture adapter 示例、kDisplay 空间经 adapter transform 开放、fuzz 入口与隐私负向测试；v0.2.0 之后：M6 实验性几何区域提议与验证基准（`geometric_proposal.hpp` 为新增实验头） |
| mirador → googletest | `063de7e` | 不变 | — |

### Mirage 接触面 API 影响分析

对 Mirage 全部依赖 include 点做了逐一 diff（全仓扫描 `#include <mira/*> <executor/*>
<mirador/*>` 得到接触面）：

- **executor**（`blocking_io`、`stop_token`、`executor`、`types`、
  `serial_execution_context`、`comm/{topic,mailbox,channel,types}`）：`include/` 在
  `e2dc8ca..2ae4fc8` 区间零变化。API 风险解除。
- **mira**（`json`、`environment`、`version`、`runtime`、`core_contracts`、
  `adapters/simulator/simulator_environment`）：仅 `json.hpp` 有 25 行 diff，为纯
  格式化（换行调整），无签名变化。其余零变化。mira M7 新增的 `tool_module*.hpp`
  等新头未被 Mirage 使用。
- **mirador**（`pixel_format`、`backend_info`）：零变化。M5/M6 的新能力
  （fusion `kDisplay`、geometric proposal）均为增量 API，未触及 Mirage 现有用法。

**结论：本次升级对 Mirage 是纯指针前滚，无 API 适配需求。**

### 许可证核对

- mira：新 pin 仍无 LICENSE 文件，`dependencies.lock.json` 的 UNLICENSED 注记维持，
  待上游补齐后更新。
- executor：MIT（不变）；mirador：MIT（不变）；mbedtls：Apache-2.0 / GPL-2.0-or-later
  （不变）；googletest：BSD-3-Clause（不变）；sqlite（vendored，audit-only）不变。

### 回归验证证据

- **构建**：`cmake --preset debug && cmake --build --preset debug` 退出码 0，118 个
  目标全部通过；configure 输出 5/5 pin verified（mira `cf0af75`、mira:executor
  `2ae4fc8`、mira:mbedtls `068ff08`、mirador `50f5349`、mirador:googletest `063de7e`）。
- **测试**（Independent-Verification-Agent 独立执行，日志 `/tmp/ctest_{debug,asan,ubsan}.log`）：
  - debug：`ctest --preset debug` 24/24 通过（unit 11 / integration 13 / protocol 2 /
    smoke 1 覆盖），31.18s。
  - ASAN：构建退出码 0，`ctest --preset asan` 24/24 通过；verbose 重跑全量输出中
    ASAN/LSAN 报告 0 次。
  - UBSAN：构建退出码 0，`ctest --preset ubsan` 24/24 通过；verbose 重跑全量输出中
    "runtime error" 出现 0 次（verbose 补跑是因为 UBSAN 默认可恢复、仅摘要模式存在
    静默报告风险）。
- **未覆盖项**：TSAN 与 release preset 未运行。本次无跨上下文状态或关闭路径的代码
  变更（API 接触面零变化），TSAN 留待下次并发相关改动时随常规 TSAN 覆盖执行。

### 审计结论

通过。submodule 指针与 `dependencies.lock.json` 同步更新于同一变更；无许可证变化、
无 API 适配、无回归。executor v0.5.0 的任务取消/定时句柄等新能力为后续 Mirage
工作项（如长任务的排队期取消）提供现成支撑，按需另行引入。
