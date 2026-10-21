# M8-07 预算 OCR 下游消费与受控验收

> 状态：Accepted（接线与受控验收完成，2026-10-21；MR 合入待负责人授权）
> 日期：2026-10-21
> 负责人：Mirage 维护者
> 依据：[上游 DEC-025](../../third_party/mirador/docs/decisions/DEC-025-budgeted-ocr-and-partial-results.md)（Accepted）、
> 上游 [M8 计划](../../third_party/mirador/docs/plans/m8-budgeted-ocr.md) M8-07、
> [DEC-076](../decisions/DEC-076-bounded-native-ocr-and-capture-diagnostics.md) 五秒交互 OCR 预算、
> 台账 MIRADOR-20261018-001（见下方"台账归属"）
> 上游交付：mirador `cb45a102..5565436`（M8-01…06，PR #43/#44/#46/#47）

## 交付与边界

Pin 升级到 mirador master `5565436`，消费上游预算 OCR 契约（opt-in
`BudgetedOcrBackend` SPI + `ocr_batch.hpp`；旧 `OcrBackend`/`run_ocr`
签名与全有或全无语义零变化）。`PpOcrPipeline` 与旧后端并列创建
`PpOcrBudgetedBackend`（det/rec 清单配置同源，`rec_unit_upper_bound=100 ms`
为上游 M8-06 冻结参考），创建失败整管线 fail-closed——预算路径缺失不允许
静默降级回会超时的全量路径。

DEC-076 交互图片 OCR 路径（`read_image_ocr`）切换到预算执行：K=64 +
`finalize_reserve=500 ms`（上游 M8-06 冻结参考配置）+ 既有 5 秒硬 deadline。
临近 deadline 时由上游预测性收尾守卫软停止并返回 partial 报告（逐行
kRecognized/kDeferred 完成态、停止原因、det 触顶诊断），取代旧路径"超时丢弃
全部已完成行"的失败形态；tripped deadline / 取消仍显式
`kTimedOut`/`kCancelled`。诊断贯通会话主机
（`VisualAnalysisResult::OcrBatchDiagnostics`，含 deferred 几何）与 mira 层
`ImageOcrDiagnostics`（纯 std/desktop 类型，DEC-003 无 mirador 类型泄漏）；
`backend_calls()` 汇总两条执行路径。

**Partial 不是授权**：预算报告只进入只读图片 OCR 结果（文本 + 几何 +
完成态事实），不发布 actionable 视觉引用——`read_image_ocr` 路径本身不产生
可执行引用（DEC-075），验收以 registry scope 不变断言固化。观察/融合路径
（`refresh`）维持旧全量语义与既有预算，不在本次范围。

**台账归属**：MIRADOR-20261018-001 的登记条目随 `feat/desktop-input-policy`
分支提交（其上游回执与移除条件记录在该条目内），本分支只承载 pin 升级、消费
接线与验收证据；该分支合入 master 时由条目跟随更新状态与本次报告回链。

## 受控验收（AC8-10）

- 工具：`tests/manual/ppocr_budget_acceptance.{cpp,py}`（EXCLUDE_FROM_ALL，
  默认 CTest 不捕获桌面、不依赖权重）。驱动确定性渲染 1600×1000 密集中英文
  场景（3×23 网格），同一管线连续 3 次 `read_image_ocr`（各 5 s deadline），
  门禁：每次 ok 且 ≤5,000 ms、预算诊断齐全、K=64 绑定
  （partial / rec_line_limit / recognized=64 / deferred=detected−64）、
  非空行 ≥32、三次文本 FNV digest 逐位一致、visual reference registry
  不变；报告与场景/清单/字体 SHA-256 落 `report.json`。
- 权重：受控路径 `ppocrv5-mobile-v1`（`PP-OCRv5-mobile-ncnn-mirador-contract-v1`，
  manifest 摘要见报告），不入仓、不下载。
- 执行记录（由 Independent-Verification-Agent 独立构建与执行，证据目录
  `/home/linductor/m8_07_mirage_iva/`，两轮独立进程 run1/run2）：

  - 场景 SHA-256 两轮一致（`ac9fbee3…`，69 行网格，detected=69）；
  - 每次调用 detected=69、**recognized=64、deferred=5=69−64**、partial=true、
    stop=`rec_line_limit`、det_limit_hit=false；耗时 run1 3172/1756/2985 ms、
    run2 2729/2746/3197 ms，全部 ≤5,000 ms；
  - 跨进程文本 FNV digest 逐位一致（6 次调用全部 `41f4881bf1f14c55`）；
  - `REGISTRY untouched=1`（partial 不产生 actionable 引用）。

## 证据表

Linux x86_64、GCC 13.3；执行者 Independent-Verification-Agent，主循环仅实现
与修复（round 1 发现一项 legacy-only 回归与 11 处格式违例，主循环修复后
round 2 复验通过）。全量日志见 `/home/linductor/m8_07_mirage_iva/`。

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| Release 全量 ctest | 96/96（含新增 `visual_session_budgeted_test`） | 全仓回归（setarch -R，CI 同款） |
| visual_observation_e2e_test | 118 checks / 0 failures（与 master 基线一致；round 1 暴露的 legacy-only 回归已修复并复验） | legacy-only 管线契约 |
| ppocr_pipeline_test | 144 checks / 0 failures | 管线合成契约（含双后端创建、预算 direct 读取） |
| visual_session_test | 206 checks / 0 failures | 既有主机层契约回归 |
| visual_session_budgeted_test（新增） | 136 checks / 0 failures × Release/ASAN/UBSAN 三构建 | 预算分支：字段映射、recognized/deferred 拆分、三 stop 原因诊断、det_limit 透传、未配后端 fail-closed（无静默回退）、partial 与 detector 共存、永不走能力缓存、后端失败整请求失败 |
| ASAN / UBSAN | 重点 7 测试 + 验收二进制 `--calls 1`：sanitizer 零报告 | 改动路径内存/UB 安全（sanitizer 开销下 5 s 放不下 rec 属预期，功能门禁以 Release 为准） |
| AC8-10 受控验收 | 两轮独立进程全部门禁 PASS | 见上文执行记录 |
| clang-format / clang-tidy | 6 个改动文件 + 新测试文件全部 clean | 剩余一条 `VisualAnalysisRequest` padding 诊断为基线既有（逐字节复现于 58a02e3） |
| DEC-003 边界 | `integration/mira/include` 零 mirador 类型命中 | 头文件层边界 |

## 已知边界与补跑条件

- 性能数字为本机受控环境（与上游 M8-06 基准同机族、powersave 未固定频点），
  跨机绝对值不可比；Windows/Android 维持构建级结论，不声明 5 秒。
- `soft_budget` 显式旋钮未由产品路径启用（deadline 预测性守卫已覆盖 DEC-076
  预算）；调用方仍可按 DEC-025 §3 显式传入。
- 场景为合成渲染网格，不代表真实桌面混合密度；真实桌面质量收益归后续
  真实会话观察（上游 M8-06 同样保留 `linux-ocr-desktop-real-v1` 补跑项）。
