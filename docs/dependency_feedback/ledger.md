# 依赖问题反馈台账

> 状态：Active
> 负责人：Mirage维护者
> 更新日期：2026-10-12
> 维护规则：[工程规范9.4](../project/project-standards.md)
> 本轮工作项：[M7-06](../plans/m7-visual-tools-and-input.md)；既有 M6 记录保留

## 分依赖台账

| 依赖 | 详细台账 | 已确认问题 | 说明 |
| --- | --- | --- | --- |
| Mira | [Mira台账](mira.md) | 既有状态见详细台账；M7新增3项：3项Resolved（Linux 合成图范围；Messages SSE 截断流 PR#87；网络阶段超时可重试 PR#88，均合并并消费复验） | Runtime/模型/工具/workflow及随Mira交付的Executor；PR#86 已消费，三方言 fixture 与 MiniMax 截图工具在线 2/2 |
| Mirador | [Mirador台账](mirador.md) | 1项 Open | M7-06：常量缩放失真；公开复现，待上游提交 |

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

公开API核对、影响和验收见[Mira台账](mira.md#mira-20261006-001anthropic-messages方言缺口)。本轮Resolved（Linux本地范围），[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已创建未合并；无临时调度设施。

## MIRA-20261006-002：Messages 思考控制与签名回填

详见 [Mira 台账](mira.md#mira-20261006-002)。Linux 本地修复与授权 pin 复验完成；[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81) 未合并。

## MIRA-20261008-001：Responses / Chat Completions 方言丢弃思考输出

详见 [Mira 台账](mira.md#mira-20261008-001)。两服务在线对照确认：Messages 方言思考内容端到端可见，Chat Completions 的 `reasoning_content` 只留摘要或被丢弃。Open；[上游 issue#83](https://github.com/Linductor-alkaid/mira/issues/83) 已提交。

## MIRADOR-20261009-001：常量缩放失真影响真实 OCR

详见[Mirador 台账](mirador.md#mirador-20261009-001)与
[公开复现](repro/README.md#mirador-常量缩放复现m7-06)。Open，待上游提交；未修改 pin。
