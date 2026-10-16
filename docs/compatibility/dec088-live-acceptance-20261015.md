# DEC-088 真实会话验收（2026-10-15，deepseek-flash live）

> 执行：Independent-Verification-Agent，两轮（R1 暴露缺陷 → 修复 → R2 复跑）。
> 端点：`https://api.deepseek.com`（anthropic.messages.v1，deepseek-flash，与用户
> service.json 选中条目同源配置）。凭据经钥匙环 → 环境变量注入 transport，全程
> 不回显、不入报告。驱动：`tests/manual/dec088_live_acceptance`（opt-in 工具，
> `MIRAGE_BUILD_MANUAL_ACCEPTANCE`，不在任何预设/CI 中构建）。脚本化 FakeDesktop
> 提供 d1 显示器截屏工具；usage 面为新加的 `ModelLayerConfig::usage_observer`
> （每请求 provider 上报的 input/output/reasoning tokens，只含计数不含内容）。
> 原始报告与复现工具：`/tmp/dec088/`。

## R1（修复前）——验收抓到两个真实缺陷

| 能力 | 结果 | 证据 |
| --- | --- | --- |
| 1 复用标注 | **PASS** | 同截图 4 连拍：带身份 annotations=2（第 3 次起渲染），`--no-identity` 对照 annotations=0 |
| 2/3 压缩 | **FAIL** | window4k transforms=0、degradations=3，输入线性增长；比值 122.8% |
| 4 续写 | **FAIL** | 64/128/256/512 档 0/13 |

- **缺陷一（Mirage）**：`DesktopContextBuilder` 每 history item 只取第一个 tool
  part 构造 Layer 0 项；真实 provider 把多次调用批量并入一个 assistant 回合，
  未映射 call 的 result 在 Layer 0 无配对 → 每请求 `ToolPairingBroken` 整轮降级。
  离线复现一致。这正是计划与用户调查文档预警的"只取第一个 call key"缺口。
- **缺陷二（上游 mira）**：部分文本续写锚点按字节切 50 字节，中文输出切点落在
  码点内，feedback 携带非法 UTF-8。离线模拟 64/128/256 档全部复现。

## 修复

- Mirage `718cdbd` + `e98f307`：逐 tool part 映射（各自 provider_call_key、
  content 只含该 part、估算保真）+ 整组原子收缩运行到不动点（Layer 0 在批量
  兄弟间拆分时，被丢 item 的幸存 key 级联 falsify，其 result 一并消失）。
  独立验证新增 4 个判别单测（批量映射/混合文本存亡/预算边界拆分/估算和），
  套件 1004 → 1261 断言全绿、ASAN 无报告；对修复前后双向判别。
- 上游 [PR#98](https://github.com/Linductor-alkaid/mira/pull/98)（授权合并
  `560da5ec`，台账 [MIRA-20261015-001](../dependency_feedback/mira.md)）：锚点
  切后丢弃悬空 continuation 字节；`m3` 新增多字节锚点用例。

## R2（修复后，pin `560da5ec`）

**离线反转复现 4/4**：批量形状 transform 成功、provider 配对完整、压力下整组
原子收缩（级联生效）；单调用对照正常压缩。

**压缩（能力 2/3）live——机制生效**：

| 运行 | ok | requests | transforms | degradations | final_input | 比值 | 曲线 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 基线 window=0 | ✓ | 4 | 0 | 0 | 7799 | — | 5265→6426→7694→7799 线性 |
| window=4096 主运行 | ✓ | 4 | 2 | 1 | 5740 | 0.736 | 5265→5837→7470→**5740**（第 4 请求 −23%） |
| window=4096 方差 ×2 | ✓ | 4 | 2 | 1 | 5708/5664 | 0.732/0.726 | 同形态，三次高度一致 |

- R1 的配对风暴消失：三次运行零 provider 协议违规，任务全部完成。
- **压缩真实触发**：峰值 7470 → 收缩至 ~5700；重放段口径（压缩的目标段）从
  ~2534 token 收缩至 ~475 token，约 **19%**。
- 残余一：每次会话恰 1 次 transform 降级——预算边界上"整组全丢 → 空 replay →
  empty-result 降级（fail-open）"，单次、有界、任务不受影响；机制探针证明一般
  批量形状降级为 0。后续可给最新组设保留下限或把 `HistoryTransformDegraded`
  reason 接入可观测面。
- 残余二：总输入比值 0.726–0.736 高于 0.70 线——每请求固定前缀（system+goal+
  工具 schema+当轮图）≈5265 token 不可压缩，该会话形态下比值结构性下限即
  0.675；是指标设计问题而非机制问题。长会话（重放占比更大）下该指标才有区分度。

**复用标注（能力 1）live——PASS**（R1，修复未触及）：annotations on=2 / off=0。

**续写（能力 4）live——无法触发，provider 侧限制**：max_output ≤ 1024 档
DeepSeek anthropic-compat 端点在首个请求流末（usage 上报后）返回
`invalid_request_error`（同装配路径 compaction/reuse 场景 max_output=2304
十次全成）。截断续写阶段未到达；锚点修复已由上游 m3 单测与 pin 源码核验。
64 档曾验证有界续写的显式耗尽路径按设计工作（`output-limit continuation
budget exhausted`）。

## 结论

- 能力 1/2/3 的机制在真实会话中验证生效（1=PASS；2/3=触发、收缩、配对合法、
  任务完成；总输入比值受结构性下限约束，重放段口径 −81%）。
- 能力 4 的上游锚点缺陷已修复并消费；live 触发受 provider 小 max_tokens 流式
  不稳定阻塞，维持"fixture 级验证 + provider 限制记录"状态。
- 原计划"15 轮 <70%"指标在真实模型批量调用形态下按原口径不可达（结构性下限
  0.675 + 会话仅 4 请求）；重放段口径（−81%）是更贴切的机制证据。30 轮验收
  仍被 16 轮硬预算（用户分析文档 P0）阻塞。
