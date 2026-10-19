# 依赖问题反馈台账

> 状态：Active
> 负责人：Mirage维护者
> 更新日期：2026-10-19
> 维护规则：[工程规范9.4](../project/project-standards.md)
> 本轮工作项：[依赖复用整改](../plans/dependency-reuse-remediation.md)（REUSE-16/17/18
> 台账一致性收口）；既有条目分别溯源至 M6、M7-06、M8/M9 各工作项

## 分依赖台账

2026-10-16 台账一致性整改（[REUSE-16/17/18](../plans/dependency-reuse-remediation.md)）：
修正索引与详情的过期状态（Mirador 三条、MIRA-20261005-001、20261014-005 孤儿表、
20261004-001/002 与 20261006-001/002、20261008-001 的上游合并回执）；为
MIRA-20261013-001 补齐状态/负责人字段；补登记 DEC-085 系列上游交付
[MIRA-20261016-002](mira.md#mira-20261016-002)（追记，Resolved）、kairo 0.7.0 延期
能力 [MIRA-20261016-003](mira.md#mira-20261016-003)（Open）、RGBA→PNG 编码缝
[MIRA-20261016-004](mira.md#mira-20261016-004)（Open）；为 M10 预备清单预留的
语义相似度确认登记 [MIRA-20261016-001](mira.md#mira-20261016-001)（Proposed 占位）。

长程任务调查新增 [MIRA-20261015-002](mira.md#mira-20261015-002)：pinned
ConversationLoop 的 model/tool 累计上限没有公开的“不设总次数限制”模式；增大数值
仍会终止正常长任务，`max_turns=0` 也不表示持续运行。状态 Open；上游已按维护者
授权提交并合并 [PR #99](https://github.com/Linductor-alkaid/mira/pull/99)，24/24
检查成功；pin 已升级至 a29374a 且 Linux 消费验收完成，公开能力缺口为 Resolved，见
[审计](../supply-chain/mira-continuous-loop-20261015.md)。
初次调查只登记本地
API/源码与真实 16 步失败证据，未提交上游、未改依赖。方案见
[持续任务与本地视觉闭环](../analysis/session-budget-and-native-pet-regression-20261015.md)。

M8-11 新增 [MIRA-20261014-004](mira.md#mira-20261014-004)：公开正文预览不提供
实时思考分类入口；已由 [Mira PR#94](https://github.com/Linductor-alkaid/mira/pull/94) 的独立公开 thinking sink 解决，授权消费合并提交22d9577并复验fixture，状态Resolved。PR已合并；供应商在线与平台验证单独记录。

DEC-088 Phase 1 新增 [MIRA-20261014-005](mira.md#mira-20261014-005)：ConversationLoop
供给缝无跨轮输入复用身份标注入口，已由
[PR#95](https://github.com/Linductor-alkaid/mira/pull/95) 解决（授权合并 9e7b729）；
Mirage 随 pin 消费跨轮计数并复验 fixture，状态 Resolved；真实会话对照实验待真实
provider 环境补跑。相同截图逐轮重现属 DEC-053 契约内行为，但循环与模型无从得知
复用身份，重复识图浪费思考 token。

DEC-088 能力 2/3 新增 [MIRA-20261014-006](mira.md#mira-20261014-006)：ConversationLoop
工具历史回放无宿主可插入的压缩缝，已由
[PR#96](https://github.com/Linductor-alkaid/mira/pull/96) 解决（授权合并 93e2eae）；
Mirage 随 pin 消费 DesktopContextBuilder 并复验 fixture，状态 Resolved；真实会话
token 验收待真实 provider 环境补跑。

DEC-088 能力 2/3 新增 [MIRA-20261014-007](mira.md#mira-20261014-007)：
ConservativeTokenCounter 不计工具载荷字节（ToolCallPart.arguments /
ToolResultPart.result），工具密集请求水位系统性低估、压缩触发偏晚；已由
[PR#97](https://github.com/Linductor-alkaid/mira/pull/97) 解决（授权合并
7391df7），Mirage 随 pin 消费并复验（载荷驱动压缩触发回归 + 旧算术反证），
状态 Resolved。

DEC-088 能力 4 新增 [MIRA-20261015-001](mira.md#mira-20261015-001)：部分文本
续写锚点按字节切取，多字节脚本下产生非法 UTF-8 feedback；真实会话验收发现，
已由 [PR#98](https://github.com/Linductor-alkaid/mira/pull/98) 解决（授权合并
560da5ec），Mirage 随 pin 消费，状态 Resolved。真实会话验收证据见
[dec088-live-acceptance-20261015](../compatibility/dec088-live-acceptance-20261015.md)
（能力 1 live PASS；能力 2/3 修复后 live 触发收缩、重放段约 −81%；能力 4 live
触发受 provider 小 max_tokens 流式不稳定阻塞）。

| 依赖 | 详细台账 | 已确认问题 | 说明 |
| --- | --- | --- | --- |
| Mira | [Mira台账](mira.md) | 既有状态见详细台账；Open 5 项：MIRA-20260922-001（MinGW 构建）、MIRA-20261008-002（-Wuseless-cast）、MIRA-20261012-002（ProviderCircuit cooldown）、MIRA-20261013-001（`Result::value()` 防线）、MIRA-20261016-003（kairo 0.7.0 延期能力跟踪）；另有 MIRA-20261016-004（PNG 编码缝）与 20261016-001（前瞻 Proposed）；MIRA-20261016-002 为追记 Resolved | Runtime/模型/工具/workflow及随Mira交付的Executor；PR#86 已消费，三方言 fixture 与 MiniMax 截图工具在线 2/2 |
| Mirador | [Mirador台账](mirador.md) | 3 项均 Resolved（本地） | M7-06 常量缩放/连通域边界/PP-OCR 预处理三项随上游 PR#35 合并（master `ae8cf87`）并经授权升级 pin 复验收口；OCR 权重许可与真实桌面/DPI 样本等剩余项不随本组关闭，见 mirador.md 合并回执 |

executor经pinned Mira传递引入，编号使用`MIRA-YYYYMMDD-NNN`，不直接向Executor提交。
Mirador使用`MIRADOR-YYYYMMDD-NNN`。EUI是用户授权的UI依赖，其既有问题仍在
[独立EUI台账](eui-ledger.md)，不混入本索引。台账不改变依赖锁定或引入额外并发设施。

条目必须包含版本/API核对、现象与复现、应用职责排除、影响、最小期望能力、延期影响、
验收与临时措施移除条件；每项维护负责人、Open/Proposed/Accepted/Resolved/Rejected及
上游链接/复核时间。缺陷状态与提交状态分开；上游修复后须授权升级并复验后再标记Resolved。

## 历史编号入口

以下标题保留原锚点，已有代码/文档引用仍有效；详细证据和跟进在分依赖台账维护。

## MIRA-20261012-001：网络阶段超时不可重试，单次网络停滞判死整轮

详见 [Mira 台账](mira.md#mira-20261012-001)。Resolved；真实 DeepSeek 会话
（2026-10-12 09:56）单次 TLS/write 阶段停滞 ≥10s 使第 6 次模型调用以
"exchange stage deadline exceeded" 判死整轮，与同会话 10 秒前 4 次成功带图请求对照，
排除负载与工具因素。上游 [PR#88](https://github.com/Linductor-alkaid/mira/pull/88)
经维护者授权合并（57c3062）：阶段 deadline 携带 retryable 提示、total 预算约束保持
不可重试、循环恢复白名单 admitting 该码；pin 已消费并复验：mira 105/105（Debug+ASAN），
Mirage 71/71 全量 + 集成 356 checks（含 retryable 重试成功/不可重试立即失败/post-write
恢复预算恰好用尽三个行为用例），ASAN/UBSAN 干净。Mirage 侧配套放宽 profile
tls/write deadline 至 20s/30s 并按 RULE-08 信封阈值启用恢复预算（capacity ≥ 10）。

## MIRA-20261009-002：Messages SSE 解析器对 max_tokens 截断流 fail-closed

详见 [Mira 台账](mira.md#mira-20261009-002)。Resolved；真实 MiniMax 在线抓流与离线
逐字节复现（孤立 `content_block_stop`、`tool_use` 参数 JSON 截断，均报
"invalid Messages SSE lifecycle or budget"），入库复验工件
[minimax-cutoff-sse.cpp](repro/minimax-cutoff-sse.cpp)。用户感知为 minimax 会话
思考档位切换后整轮 SSE 报错（实为档位切换改变每轮输出预算，截断概率随之变化）。
上游 [PR#87](https://github.com/Linductor-alkaid/mira/pull/87) 经维护者授权合并
（c75378b），pin 已消费并复验：真实抓流 4/4 解码为 Incomplete 终态。无 Mirage 侧
临时缓解。

## MIRA-20261009-001：ConversationLoop 工具产生图片的逐轮结构化输入入口

详见 [Mira 台账](mira.md#mira-20261009-001)。Resolved（Linux 合成图范围）；[PR#86](https://github.com/Linductor-alkaid/mira/pull/86)
已交付公开入口，已授权升级至 698fc77，公开 API 探针与上游供给 fixture 本机通过。
M7-04/05 图片链路、三方言 fixture 与 MiniMax 在线两例已通过，见
[图片验收](../compatibility/desktop-image-supply-20261009.md)。本轮未操作 #85，沿用最后回读 Open。
[升级审计](../supply-chain/mira-image-supply-20261009.md)记录回归范围。

## MIRA-20260922-001：executor 的 MinGW-w64 交叉构建失败

详见 [该条目](mira.md#mira-20260922-001)。

## MIRA-20260927-001：同内容草稿记录遮蔽可运行版本

详见 [该条目](mira.md#mira-20260927-001)。

## MIRA-20261004-001：通用对话 harness 缺少无观察循环入口

详见 [该条目](mira.md#mira-20261004-001)。

## MIRA-20261004-002：OpenSSL TLS Adapter 未发送 SNI

详见 [该条目](mira.md#mira-20261004-002)。

## MIRA-20261005-001：真实流式预览与Chat Completions SSE

详见[该条目](mira.md#mira-20261005-001)。

## MIRA-20261006-001：Messages协议

公开API核对、影响和验收见[Mira台账](mira.md#mira-20261006-001anthropic-messages方言缺口)。Resolved（Linux本地范围），[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已合并（2026-10-08 master d5c7d06 复核含 PR#79/80/81）；无临时调度设施。

## MIRA-20261006-002：Messages 思考控制与签名回填

详见 [Mira 台账](mira.md#mira-20261006-002)。Linux 本地修复与授权 pin 复验完成；[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81) 已合并（2026-10-08 master 复核）。

## MIRA-20261008-001：Responses / Chat Completions 方言丢弃思考输出

详见 [Mira 台账](mira.md#mira-20261008-001)。两服务在线对照确认：Messages 方言思考内容端到端可见，Chat Completions 的 `reasoning_content` 只留摘要或被丢弃。Resolved（PR#84 已合并并授权消费，2026-10-08 真机复验收口，详见 Mira 台账）。

## MIRADOR-20261009-001：常量缩放失真影响真实 OCR

详见[Mirador 台账](mirador.md#mirador-20261009-001)与
[公开复现](repro/README.md#mirador-常量缩放复现m7-06)。Resolved：上游 PR#35 经
维护者授权合并（master `ae8cf87`）；Mirage 当前的 `v0.3.0-desktop-yolo` 分支 pin
（`cb45a10`）包含该修复，YOLO 增量见下条。

## MIRADOR-20261016-001 / 002：桌面感知质量与 YOLO 预处理

见 [Mirador 台账](mirador.md)。001 已有 issue #36/PR #37；002 对应 issue #38，
本轮维护者授权直接修复并通过分支 pin 接入 Mirage，M7-23 / DEC-097。
上游 PR 评审、合并状态独立记录，不以本地分支 pin 冒充 master 已发布。

## MIRADOR-20261018-001：PP-OCR rec 逐框串行，密集帧超交互预算

新增 [MIRADOR-20261018-001](mirador.md#mirador-20261018-001)（Open）：pinned
`PpOcrBackend` rec 阶段逐框串行、无批处理/截断/部分结果，密集桌面帧（512 候选）
无法进入 DEC-076 冻结的 5 秒 OCR 预算；生产会话连续 `native_ocr_expired` 与本地
基准（359 框 ≈12.9s，rec 占 98%，多线程 rec 在默认调度下反而劣化 1.6–1.8×）为证。
Mirage 侧临时边界为 det 四线程、rec 单线程；上游能力交付前，密集整屏 OCR 依赖
DEC-107 的"分析未完成"反馈与 ROI 小图绕行。2026-10-19 已提报上游
[mirador#41](https://github.com/Linductor-alkaid/mirador/issues/41)，进展随 issue 跟踪。
