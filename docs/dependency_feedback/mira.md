# Mira 反馈台账

> 状态：Active
> 负责人：Mirage维护者；处理方：Mira上游
> 更新日期：2026-10-25
> 工作项：台账维护性收口见[依赖复用整改计划](../plans/dependency-reuse-remediation.md)
> （REUSE-16/17/18）；既有条目分别溯源至 M6-10、M7、M8/M9 各工作项。
> 总入口：[依赖反馈](ledger.md)；维护规则：[工程规范9.4](../project/project-standards.md)

## MIRA-20261015-002

主题：ConversationLoop 缺少不按累计步骤/工具次数终止的长程任务模式。

- 编号修正：并行工作将原未发布的持续循环反馈与已发布的 UTF-8 续写问题同编为
  `MIRA-20261015-001`；本项改用唯一编号 `MIRA-20261015-002`，保留原续写反馈编号。
- 状态：Resolved（公开能力与 Linux pin 消费范围）；负责人：Mirage 维护者；处理方：Mira 上游。
- 上游：[PR #99](https://github.com/Linductor-alkaid/mira/pull/99)，2026-10-15
  按维护者明确授权在 `/home/linductor/mira` 原开发仓库实现并提交；分支
  `feat/continuous-conversation-loop`，首提交 `7e2b73e`。维护者确认 CI 全绿后明确
  授权合并及 pin 更新，已 squash 合并为 `a29374aaf05b956cbd5ceeafc65bba4c6172675e`
  （24/24 SUCCESS）；gitlink/lock 已更新，消费侧契约和 Linux 回归通过。
  独立升级 [Mirage PR #97](https://github.com/Linductor-alkaid/mirage/pull/97) 已创建。
- 关联：[长程任务与本地视觉闭环方案](../analysis/session-budget-and-native-pet-regression-20261015.md)、
  [DEC-090](../decisions/DEC-090-continuous-task-execution.md)；实施工作项为
  [M9-02/03](../plans/m9-continuous-tasks-and-local-perception.md)，维护者明确要求
  正常长程任务不因模型步骤数停止。
- 核对版本：pinned `7391df70036cbdb035dea5ebaec99c46cbabe5ae`。
  已核对公开 `include/mira/conversation_loop.hpp`、`runtime.hpp`、
  `task_checkpoint.hpp`、`docs/api/model-agent-loop.md`，以及现有循环实现和
  Mirage 的 budget fixture。
- 2026-10-15 开工复核：其他工作已消费 pin `560da5ec`，该版本仅修复续写锚点，
  公开循环仍只有有限 max_turns/max_tool_executions，本项继续 Open；未在本项升级依赖。
- 旧版本接口事实：`ConversationLoopConfig` 仅有有限整数 `max_turns` 和
  `max_tool_executions`，没有 optional/unlimited 模式；`run` 只提供完整阻塞
  调用，没有可持久化本次 loop history 的 step/resume 接口。`max_turns=0`
  使循环零次执行；`max_tool_executions=0` 使首个非空工具批次超限，不是不限。
  `build_request` 还由总轮次推导 max_requests/输出包络，单改宿主数值不足。
- 实际证据：2026-10-15 09:56 +08:00 的本地会话记录显示
  `turn budget exhausted before a final answer（16 个模型步骤、17 次工具调用）`。
  Mirage 此处确实配置 16/32，说明宿主选型是直接触发因素；当前依赖接口没有
  能满足新产品需求的显式无总次数上限配置。
- 最小复现步骤：脚本 provider 前两次输出合法只读工具、第三次输出最终文本，
  配置 max_turns=2，预期当前 loop 在第三次请求前返回 MaxTurns；再配置 0，
  循环不发模型请求。工具上限同理用超过已配置批次数的合法调用复现。
  初次调查未执行动态用例；后续上游 PR #99 新增并运行有限/零值兼容测试，
  同时完成同一 task/epoch 的 1,025 模型轮 / 1,024 工具脚本；上游全量 Debug
  109/109 通过。此结果针对 PR 分支，不代表 Mirage 当前 pin 已修复。
- 应用职责排除：Mirage 负责持续任务的产品默认值、内存窗口、权限和无进展策略；
  但将计数上限改为最大整数只是延迟终止，且放大/溢出预算包络。不断重启
  ConversationLoop 会丢失其私有历史和未完成协议上下文，不能作为透明续行。
  MiraRuntime 的 resume_task 与 TaskCheckpoint 也不等于恢复该局部 loop。
- 最小期望能力：公开显式的无累计步骤/工具次数上限模式，保留有限配置兼容性；
  每次请求准入、输出与操作期限保持有限，恢复/输出续写仍有明确上限；持续循环
  可取消/可关闭，历史压缩缝继续可用。累计统计、事件和 history 存储必须适合
  长程运行，不以整数最大值或无限包络伪装资源预算。
- 可接受的替代交付：公开可推进并可恢复同一 loop 的协议/状态契约，由 pinned
  Mira 承载调度与状态；须证明不重放已执行副作用、不复活已终态任务。
- 影响及延期：只在 Mirage 提高 16/32 无法满足维护者要求；默认长任务仍可能
  在任意正常进度处失败。Mirador 文本观察可减少消耗，但不能消除此停止条件。
- 临时措施：无依赖绕行。可独立推进本地语义观察和 pet 交付，不能宣称主循环
  问题已解决。后续上游改动/依赖消费仍按授权与审计流程执行。
- 验收：有限模式兼容；无总次数限制模式下超过原模型/工具上限仍能完成；长程
  compact/统计/内存有界；模型/工具异常、准入拒绝、输出续写耗尽、执行中取消、
  deadline、shutdown 均有明确结果。测试负责人：Mirage/Mira 维护者；补跑条件：
  上游契约及实现交付、获准消费后执行集成回归与真实长程任务。

- 上游交付进展（2026-10-15）：PR #99 / Mira DEC-056 提供显式
  ConversationLimitMode::Continuous、Request scope（model contract 2.0）、提交式
  HistoryTransformer、有界历史/逐轮窗口及饱和统计；工具执行走仅向同步向前循环
  开放的私有入口，避免永久积累 operation 墓碑。取消/期限/失败仍结算，有限模式
  保留。脚本 compactor 输入最多 5 条、提交历史最多 3 条、结果窗口 4 条，
  注册表历史 operation 数为 0；这不是实机 RSS 或真实 provider 长任务保证。
  上游最终实现 `23e3ff0` 已复验 Debug 109/109、ASAN+UBSAN 6/6、TSAN 6/6；
  连续模式按连续故障/截断段限制恢复与续写，独立成功工具轮后重置，累计数仅统计。
  远端 CI 与评审结果由 PR #99 跟踪；Mirage M9-02/03 待
  PR 交付、依赖升级审计及接线回归，不能提前标为 Resolved。

## M6-10历史核对基线与当前状态

- Mirage pinned Mira：`13485151bf531bff065eadf37be502061de2ab3e`。
- 2026-10-05核对Mira本机与GitHub master：`472e43010485131790ca300c574d0ba17e50a711`。
  已核对四项相关公开头/API文档、测试与对应实现路径，未发现已交付修复；
  本轮动态复现针对pinned版本，master仅作源码/API核对，不宣称动态复验master。
- M6-10未修改第三方代码或升级pin；M6-18经维护者授权接入PR#76及流式PR#79，当前pin见锁文件。executor仍由Mira传递交付。
- `Open`表示缺口待处理；是否已提交上游由“上游”列独立记录。
  `Resolved`须同时有上游修复、Mirage授权升级及对应复验，issue关闭不自动表示已迁移。
- 旧编号保留；历史证据与本轮离线复现区分，不把工作绕行的通过当成缺陷修复。

| 编号 | 主题 | 类型/分级 | 状态 | 本轮证据 | 上游 |
| --- | --- | --- | --- | --- | --- |
| MIRA-20261015-002 | ConversationLoop 缺少不按累计步骤/工具次数终止的模式 | P1：长程任务循环能力缺口 | Resolved | a29374a 消费契约：1,025 轮/1,024 工具；干净 Debug 82/82，ASAN/UBSAN 各 5/5 | [Mira PR #99](https://github.com/Linductor-alkaid/mira/pull/99) 已合并；[升级 PR #97](https://github.com/Linductor-alkaid/mirage/pull/97)、[接线 PR #103](https://github.com/Linductor-alkaid/mirage/pull/103) |
| MIRA-20260922-001 | Executor MinGW POSIX native_handle转换 | 构建缺陷 | Open | 最小编译复现同类错误 | [#75](https://github.com/Linductor-alkaid/mira/issues/75)，已提交 |
| MIRA-20260927-001 | 同digest草稿遮蔽已验证workflow记录 | 版本语义缺陷 | Resolved | 公共版本API离线复现 | [#74](https://github.com/Linductor-alkaid/mira/issues/74)，已提交 |
| MIRA-20261004-001 | 无屏幕通用harness与规范工具回填入口 | P2：harness边界结构性缺口 | Resolved | 无屏幕环境在模型路由前失败；核对输入契约 | [#73](https://github.com/Linductor-alkaid/mira/issues/73)，已提交 |
| MIRA-20261004-002 | OpenSSL ClientHello缺少SNI | 传输缺陷 | Resolved | socketpair捕获真实adapter ClientHello，无server_name | [#72](https://github.com/Linductor-alkaid/mira/issues/72)，已提交 |
| MIRA-20261008-001 | Responses/Chat Completions方言丢弃思考输出 | P2：方言能力缺口 | Resolved | 双服务在线对照 + 升级后真机复验（chat-completions 1059字符思考可见，Messages回归正常） | [#83](https://github.com/Linductor-alkaid/mira/issues/83)，PR[#84](https://github.com/Linductor-alkaid/mira/pull/84)已合并 |
| MIRA-20261008-002 | core_contracts纳秒转换触发-Wuseless-cast | 构建摩擦 | Open | GCC/libstdc++全量消费TU复现useless_cast错误 | 未提交 |
| MIRA-20261009-001 | ConversationLoop 工具产生图片的逐轮结构化输入入口 | P2：多模态工具接入能力缺口 | Resolved | 授权消费 698fc77；三方言 fixture、MiniMax 截图工具在线识图 2/2（Linux 合成图） | [#85](https://github.com/Linductor-alkaid/mira/issues/85)，[PR#86](https://github.com/Linductor-alkaid/mira/pull/86) |
| MIRA-20261009-002 | Messages SSE 解析器对 max_tokens 截断流 fail-closed | P1：真实供应商截断流不可解码 | Resolved | 真实 MiniMax 抓流复现；PR#87 合并后 pin 消费 c75378b，mira 105/105 + Mirage 全量回归 + 真实抓流 4/4 解码为 Incomplete 终态 | [PR#87](https://github.com/Linductor-alkaid/mira/pull/87)，已合并 |
| MIRA-20261012-001 | 网络阶段超时被标记不可重试，单次停滞判死整轮 | P2：传输错误分类与重试语义缺口 | Resolved | 真实会话复现（DeepSeek 单轮 turn 失败）；上游 CI 24/24 + 本地 mira 105/105；pin 消费 57c3062 后 mirage 71/71 全量 + 集成 356 checks 行为复验 | [PR#88](https://github.com/Linductor-alkaid/mira/pull/88)，已合并 |
| MIRA-20261014-004 | 无公开实时思考分类预览入口 | P2：呈现能力缺口 | Resolved | 独立 thinking sink；三方言分片、取消重试与 Runtime/IPC fixture 复验 | [PR#94](https://github.com/Linductor-alkaid/mira/pull/94)，授权消费合并提交22d9577 |
| MIRA-20261014-005 | ConversationLoop 供给缝无跨轮输入复用身份标注入口 | P2：多轮上下文成本缺口 | Resolved | PR#95 授权合并 9e7b729；Mirage 随 pin 消费跨轮计数并复验 fixture；真实会话对照待真实 provider 补跑 | [PR#95](https://github.com/Linductor-alkaid/mira/pull/95)，已合并 |
| MIRA-20261013-001 | `Result<T>::value()` 对 error 态 UB 解引用 | P2：公开 API 防线缺口 | Open | 非法 JSON `.value()` 触发 SIGSEGV 且无符号栈（DEC-085 测试现场）；详见详细条目 | 未提交（待维护者评估） |
| MIRA-20261016-001 | 语义相似度计算能力（M10 记忆系统前瞻确认） | P3：前瞻能力确认 | Accepted（部分交付） | pin 68a8bf1 已有 embedding 生产者/索引修复与向量召回契约；模型供给、质量与产品接线未验收 | 上游 [PR#103](https://github.com/Linductor-alkaid/mira/pull/103)，无需重复提报已有契约 |
| MIRA-20261016-002 | 会话循环对 provider error 与输出截断的韧性/续写语义（DEC-085 系列） | P1：真实会话整轮失败 | Resolved（追记） | PR#91（error 分类入恢复白名单）/PR#92（截断恢复反馈差异化）/PR#93（有界输出续写）均已授权合并并随 pin 消费复验；见 [DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md) | [PR#91](https://github.com/Linductor-alkaid/mira/pull/91)/[92](https://github.com/Linductor-alkaid/mira/pull/92)/[93](https://github.com/Linductor-alkaid/mira/pull/93)，已合并 |
| MIRA-20261016-003 | kairo 0.7.0 延期能力：QoS 标注与 AdaptiveScheduler 触发条件未到期 | P3：分层采用延期能力 | Open | 上游 DEC-054 分层采用；QoS/AdaptiveScheduler 延期并记录触发条件；见 [审计](../supply-chain/mira-kairo-070-20261013.md) | [PR#89](https://github.com/Linductor-alkaid/mira/pull/89) 已合并（延期部分未交付） |
| MIRA-20261016-004 | 观察管线无 RGBA→PNG 转码缝，host 自理编码 | P2：多模态供给依赖自研编解码 | Open | `observation.hpp` 明示 host 负责 RGBA→PNG；Mirage 以 `desktop_png.cpp` 手写 DEFLATE/PNG 承载（integration 边界内） | 未提交 |
| MIRA-20261022-001 | ConversationLoop 终态截断原因缺少结构化表达 | P2：可观测性缺口 | Resolved（Linux 消费范围） | 68a8bf1，terminal_incomplete_reason 替代文本锚点；结果/事件与消费回归见[审计](../supply-chain/mira-upstream-sync-20261025.md) | [PR#108](https://github.com/Linductor-alkaid/mira/pull/108)/[109](https://github.com/Linductor-alkaid/mira/pull/109) |

<a id="mira-20261009-001"></a>

## MIRA-20261009-001：ConversationLoop 工具产生图片的逐轮结构化输入入口

- 状态：Resolved（Linux 合成图范围，授权消费及下游/在线复验通过）；日期：2026-10-09；负责人：Mirage 维护者，处理方：Mira 上游。
  关联 [M7-05](../plans/m7-visual-tools-and-input.md)、[DEC-052](../decisions/DEC-052-visual-tools-and-input.md)。
- 需求：通用会话可调用截图工具，截图经宿主权限、编码与受控 artifact 发布后，在
  下一轮作为规范 ImagePart 进入 ModelRequest，保持原生 ToolCallPart/ToolResultPart
  对应关系、预算、取消、admission 与纯文本终态语义。
- 首次反馈核对版本：`bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5`；公开
  `conversation_loop.hpp` 仅提供 event store、registry 和 run(spec, context)；
  `AgentLoopSpec` 没有结构化输入字段；`BuiltinToolHandler` 返回 `Result<JsonValue>`；
  `ToolResultPart` 的 result 为 JSON，large_payload 是可选 ArtifactRef，不是 ImagePart
  输入供给入口。`model_contracts.hpp` 已有 ImagePart，网关/Provider 能表达图片。
  API 文档的 ConversationLoop、BuiltIn 执行边界及
  `tests/m3/m3_conversation_loop_test.cpp` 的原生工具往返同时核对。
- 可复现证据：公开 API 形状探针
  [conversation-image-api.cpp](repro/conversation-image-api.cpp)，Linux x86_64/GCC 13.3.0：

  ```bash
  c++ -std=c++20 -fsyntax-only -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
  ```

  2026-10-09 退出 0：handler 固定 JSON，ModelInputItem 不能转换为 JSON，模型内容
  variant 可构造 ImagePart。探针只证实类型边界；公共类声明核对证实无宿主逐轮输入
  供给缝。本轮未执行图片工具运行测试，未宣称供应商不支持图片。
- 应用职责排除：截图、编码、权限、坐标、artifact source 注入属于 Mirage。
  注入 store 只能让 Provider 获取已经进入请求的 ImagePart，不能将工具 JSON 自动
  转成图片输入；artifact 元数据/base64 字符串不能代替此语义。直接使用 ModelGateway
  自建 model/tool 循环会重复 ConversationLoop。AgentLoop 设备入口有观察，但与当前
  无设备决策 schema 的通用工具会话需求不同；不是参数配置错误或平台权限限制。
- 建议最小能力：提供宿主可控、每轮有界的结构化输入供给入口，或等价的多模态工具
  结果契约；在工具完成后、下次模型调用前接纳同任务/epoch 的 ModelInputItem。
  不替宿主采屏、不固定桌面策略、不将工具外部内容提升为指令权限。
- 验收：合成截图工具→artifact→下一轮真实 ImagePart；工具 call/result 不丢失；
  三方言 fixture、预算与图片保留/淘汰策略、取消/迟到/epoch 失配/供给失败均可观测；
  文本会话零变更；至少一家已声明识图能力的模型端到端验证。
- 影响与延期：阻塞 M7-05/07 的 VLM 看图能力，不阻塞 M7-02/03 的结构化观察、
  语义输入和真实 OCR 文本工具。当前不做私有循环临时方案、不修改 pinned 代码。
  上游交付后须授权消费、同步锁与供应链、移除限制并按上述验收复跑后才记 Resolved。
- 上游提交：维护者于本会话明确要求提交，已创建
  [Mira issue #85](https://github.com/Linductor-alkaid/mira/issues/85)。回读确认编号、
  作者 Linductor-alkaid、完整正文与 Open 状态；未修改 pinned 代码或升级依赖。

| 日期 | 跟进 | 下一步 |
| --- | --- | --- |
| 2026-10-09 | 公共 API/文档/测试核对及编译探针；登记 Proposed | M7-01 冻结需求后，由维护者安排上游反馈与消费验收 |
| 2026-10-09 | 维护者授权提交；gh 身份确认、上游 master/API 与现有 issues 核对，探针复跑退出 0；创建 #85 并回读全文，状态改为 Open | Mira 上游定案公开扩展；Mirage 维护者跟进交付、授权消费与 M7-05 验收 |

2026-10-09 上游更新核对：PR#86 已合并至 d5e6673，最新 master 为 698fc77，交付
set_model_input_supplier、每请求预算与 drop/degrade 事件/计数。两轮 CI 各 12/12 jobs
成功；本机原 pin 的缺入口探针、新 master 的入口/签名探针均退出 0。该正向探针仅使用
临时导出的上游公共头，不构成 Mirage 动态消费。#85 回读仍 Open；条目改 Accepted，
该次核对时 gitlink/lock 仍 bfcb8e7，不宣称下游图片闭环完成。候选 SHA、契约、CI 与下一步
见[更新核对](../compatibility/mira-image-supply-review-20261009.md)。

2026-10-09 授权升级：维护者明确授权依赖升级，gitlink/lock 固定到 698fc77；
实际 pinned 公共头正向探针、上游 `mira_m3_conversation_input_supply_test` 本机运行通过。
供给 fixture 验证规范图片/工具往返、预算/drop/degrade、重试、取消、能力与 Messages
约束，但不构成 Mirage 截图/source 接线或真实模型验收。回归结果见
[供应链审计](../supply-chain/mira-image-supply-20261009.md)，条目保持 Accepted。
授权升级回归完成：native-debug/release 各 54/54，联合 ASAN/UBSAN 6/6，
TSAN 两项 0 failures、无竞态报告，公共头/格式通过；没有完成 Mirage 图片端到端。

2026-10-09 下游收口：M7-04/05 PNG artifact/source/每请求 ImagePart 与显式识图配置
交付，三方言 fixture、MiniMax-M3/adaptive 实际截图工具在线识图 2/2。最终
native-debug/release 各 55/55，联合 ASAN/UBSAN 5/5、TSAN 图片 112 checks/无竞态，
PNG 独立解码与设置 renderer 通过；范围、精确命令和失败记录见
[图片验收](../compatibility/desktop-image-supply-20261009.md)。本地改 Resolved；未操作
上游 #85，沿用最后回读 Open。未新增第三方源码补丁或并发绕行；其他平台与业务验收
仍由 M7-08/10 负责，不扩大该反馈的解决范围。

## 2026-10-05复核证据

Linux/GCC、既有native-debug构建；[完整复现程序](repro/mira-contract-probe.cpp)、
[运行器](repro/run-probe.py)、[编译最小例](repro/mingw-native-handle.cpp)及
[复跑说明](repro/README.md)。复现是反馈取证，不是以“缺陷存在”为预期的产品回归门禁。
有限工作经pinned Executor submit_auto执行、future.get消费，外部owner最终shutdown。
TLS仅用非阻塞本地socketpair发送ClientHello，无互联网请求、无凭据、无真实桌面动作。

实际输出：

```text
loop: required.screen=true; outcome=Failed before model routing
tool input: JsonValue wire result is not a ModelInputItem
workflow: digest resolves not_validated; latest runnable=dry_run_passed
openssl: DNS host supplied; ClientHello server_name extension absent
```

MinGW-w64 GCC13 POSIX最小例`-fsyntax-only`退出1：`invalid static_cast from HANDLE
(void*) to std::thread::native_handle_type (long long unsigned int)`。只核验该表达式，
本轮未重跑全树Windows构建、未进行MSVC实机验证；后两项沿用历史证据，维护者在目标环境补跑。
`ctest --preset native-debug -R '^mira_host_test$'` 1/1通过，现有用例明确用不同内容
发布绕过同digest问题，不能当成缺陷已解决。

## 应用侧限制的分类

M6-09发现的重启旧Agent历史不能续跑，当前由Mirage按
[DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)恢复产品记录而未重建运行身份造成。
Mira公开open_session会创建新运行身份；是否采用产品ID/运行ID映射与历史重放，需先完成
Mirage恢复设计。不把该现象直接登记为Mira缺陷，也不要求终态任务复活；若明确的安全恢复
需求无法由公开能力承载，再单独按9.4登记。系统钥匙环、窗口/IME/托盘与EUI密码模式均属
其他承载方，不混入Mira反馈。

<a id="mira-20260922-001"></a>

## MIRA-20260922-001：executor 的 MinGW-w64 交叉构建失败

- **现象与可复现证据**：MinGW-w64 x86_64 GCC 13.2.0（posix 线程模型，用户前缀
  `~/.local/mirage-mingw`，DEC-017 决策 2 引导方式）交叉构建 pinned executor：

  ```
  third_party/mira/third_party/executor/src/executor/blocking_io_executor.cpp:157:28:
  error: invalid 'static_cast' from type 'HANDLE' {aka 'void*'} to type
  'std::thread::native_handle_type' {aka 'long long unsigned int'}
      157 |         auto self_handle = static_cast<std::thread::native_handle_type>(GetCurrentThread());
  ```

  复现命令：`MIRAGE_MINGW_PREFIX=$HOME/.local/mirage-mingw/usr cmake --preset win64-cross
  && cmake --build build/win64-cross`（默认 all 目标；2026-09-22，M4-02 交叉构建预检）。
  MSVC（windows-latest，VS 18 2026）不受影响：win32 线程模型的
  `native_handle_type` 即 `void*`，同语句合法（M4-01 CI 已实测 executor 随全树
  configure 通过、子集构建成功）。
- **根因**：executor 源码假定 `std::thread::native_handle_type` 可从 `HANDLE`
  static_cast 而来；MinGW-w64 的 posix 线程模型下它是整数类型，与 `void*` 之间无
  隐式转换关系。这是可移植性缺陷，不是 Mirage 用法错误（已核对 pinned 版本源码，
  无 API 选型或配置替代）。
- **影响范围**：MinGW-w64 交叉门禁只能构建不含 executor 的目标子集（platform /
  desktop / integration / 各测试——M4-01 起的既定门禁形态）。不阻塞 MSVC 主工具链
  与当前任何工作项；M4-06（产品进程 Windows 化）若要求全树 MinGW 交叉构建则被此
  缺陷阻塞，届时期望以 MSVC 门禁 + 本条目状态评估是否放行。
- **期望语义与建议的最小能力**：executor 源码不依赖具体线程模型的
  `native_handle_type` 表达（移除该 cast，或经 `native_handle()`/条件编译适配
  posix 模型）；修复后 MinGW-w64 posix 与 MSVC 双工具链均可构建。
- **可验收结果**：以 pinned executor 在 MinGW-w64 GCC（posix）下 `ninja`/`make`
  构建零诊断为验收；Mirage 侧验证方式 = `cmake --build build/win64-cross`（默认
  all）成功。
- **Mirage 侧临时措施**：无（不修改 pinned 代码；Windows 门禁在 M4-06 前按
  DEC-017 决策 9 只构建 platform 子集，子集不含 executor）。

<a id="mira-20260927-001"></a>

## MIRA-20260927-001：同内容草稿记录遮蔽可运行版本

- **现象与可复现证据**：pinned `WorkflowRuntime` 的库是内容寻址、追加式版本链
  （`workflow_versioning.hpp`）。对同一 `WorkflowDefinition` 内容先 `publish_workflow(
  ..., NotValidated)` 存草稿、再 `publish_validated(...)` 门禁发布（M5-05 的
  `workflow.save` → `workflow.publish` 自然产品流），会在库中产生两条 content digest
  相同的记录：`NotValidated` 草稿在前、`DryRunPassed` 在后。此后经库路径
  `create_run(workflow_id, digest, ...)` 以该 digest 运行时，
  `resolve_workflow_version`（`workflow_versioning.cpp:89-99`）按 `std::find_if`
  取**首条**匹配记录——即 `NotValidated` 草稿——`workflow_version_is_runnable`
  判否，运行被拒（"workflow version is not validated; only DryRunPassed or
  Validated versions may run"）。可复现证据：`mira_host_test` 的 workflow surface
  场景（M5-05，2026-09-27）：save 与 publish 提交同一定义 JSON 后，`start_workflow_run(
  workflow_id, published_digest, ...)` 复现该拒绝；将 publish 内容改为不同 digest
  后运行成功。
- **根因**：`resolve_workflow_version` 对同 content digest 的多条记录取首条，
  而"同一内容 + 不同 validation"的多记录形态在 draft→publish 流中是 pinned
  `publish_validated` 自身交付的合法序列（追加式设计允许、dedupe 仅作用于 head
  同容同证）。这不是 API 选型错误或配置错误：Mirage 无法经现有公开 API 让该
  digest 解析到可运行记录（库读取/删除 API 均不存在）。
- **影响范围**：编辑器"保存草稿 → 原样发布 → 运行"流。不阻塞
  "编辑 → 保存 → 修改 → 发布（不同内容）"与"直接发布 → 运行"流（M5-05 第一轮
  测试即以此流取证）。运行注册表以发布回执 digest 为默认运行版本的产品语义
  在受影响场景下 fail closed（显式 pinned 拒绝），无静默错误。
- **期望语义与建议的最小能力**：`resolve_workflow_version` 对同 digest 多记录
  取**最新**匹配（与 `latest_runnable_workflow_version` 的 rbegin 遍历方向一致，
  语义为"该内容的当前验证状态"）；或 `append_version_record` 拒绝同 digest、
  不同 validation 的新记录迫使调用方显式处理。任一均可消除遮蔽。
- **可验收结果**：以 M5-05 场景复核——save→publish 同内容后，库路径
  `create_run(workflow_id, published_digest)` 成功准入；或同容异证追加被显式
  拒绝。
- **Mirage 侧临时措施**：无代码绕过（不修改 pinned、不自建版本解析）。产品面
  在受影响场景呈现 pinned 的 fail-closed 拒绝；M5-05 第二轮编辑器接线以
  "发布必经内容变更或直接发布"为既定流，并在 UI 呈现 pinned 拒绝原文。上游
  消化后移除本条目对流约束的引用。


<a id="mira-20261004-001"></a>

## MIRA-20261004-001：通用对话 harness 缺少无观察循环入口

- **版本与核对**：pinned Mira 1348515 的 agent_loop.hpp、model-agent-loop.md、
  tool_executor.hpp、model_contracts.hpp、agent_harness_test.cpp；本机最新 472e430
  的公开 AgentLoop 接口相同。AgentLoopConfig 无观察策略；Full 观察固定 required.screen。
- **复现**：将不提供 screen_capture 的 IEnvironment 交给 AgentLoop，即使目标只是
  文字问答，也在第一次 ModelGateway 调用前以 unsupported screen observation 失败。
  此行为是现有设备闭环语义，无法靠配置改成通用对话。
- **影响**：M6-03 的通用 Agent harness 不需要自动截图、离散输入或 RPA workflow；
  现有 AgentLoop 不可直接承载。ModelGateway / BuiltinToolRegistry / MiraRuntime
  可以复用，未发现已公开的独立 ConversationalAgentLoop。
- **期望最小能力**：公开无观察的 model -> tool proposal -> execute -> result -> model
  循环，工具/请求预算、取消、epoch admission 与终态回执沿用既有契约；文本回答即
  会话终态，不强制 done/action JSON 或设备验证。同时补齐 ModelRequest 中的规范
  tool-call/result input 项，当前 build_tool_result_input 的产物无法直接加入 ModelRequest.input。
- **临时边界**：仅 integration/mira/src/model_layer.cpp 的 bounded harness adapter，
  最多16次推理/32次工具；复用网关解析与工具注册表。工具结果以来源标注的有界
  JSON文本回填（与 pinned loop 相同约束），不是原生 function_call_output；不接桌面
  Provider、workflow 或自动屏幕观察，不建立线程/队列/调度器。Runtime Service 负责
  task/session/cancel，Executor 负责执行与排空。上游交付通用入口后移除该 adapter。
- **验收**：没有屏幕 Provider 的环境中真实 provider fixture完成问答与两次推理的工具
  回填；非法提案、错误、取消、超时/预算、关闭不产生桌面副作用。
- **状态/责任人**：Resolved（见 M6-18 升级复验：PR#76 交付公开入口并授权消费，
  harness 边界按上游入口收敛）；维护者与 Mira 上游。历史延期影响（保留单一适配
  边界与受限工具回填语义）随上游交付解除；登记时状态为 Open 的记录保留不重写。

<a id="mira-20261004-002"></a>

## MIRA-20261004-002：OpenSSL TLS Adapter 未发送 SNI

- **版本/核对**：pinned 1348515 的公开 openssl_tls.hpp、model_transport.hpp、TLS相关测试；
  OpenSslTlsChannelFactory 的 initialize 已由 Mirage 正确调用。TlsOptions 无SNI开关。
  最小握手复现后核对 adapters/net/openssl_tls.cpp 的 setup：设置了 hostname verification，
  未调用 SSL_set_tlsext_host_name；不是关闭证书校验即可解决的应用配置问题。
- **复现证据**：本机 Mira MiniMax 配置，已初始化工厂的真实 harness 返回
  tls certificate verification failed。相同主机/系统信任库，Python ssl 带 SNI 握手验证成功；
  无 SNI 的证书 subject=*.unionpayintl.com，带 SNI 的 subject=*.minimaxi.com，均只握手，
  未发送 HTTP 或凭据。SiliconFlow 相同 adapter 真实文字/工具请求通过。
- **影响/期望最小能力**：需要SNI路由的HTTPS模型端点不可用。setup 在 connect 前设置
  TLS server_name，同时保留系统信任库、链验证、主机名验证，IP字面量按TLS规范处理。
- **可验收结果**：需要SNI的fixture及MiniMax端点使用正确证书完成握手；错误证书仍拒绝，
  不降级HTTP或关闭验证。凭据不参与握手诊断。
- **临时措施/移除条件**：无绕过；沿用 pinned TLS fail closed，真实可用的SiliconFlow用于
  当前harness验收。实现引用本编号，上游修复并经授权升级pin后补验MiniMax。
- **状态/负责人**：Resolved（M6-18 复验：SNI 随 PR#76/0a099ba 消费链路验证通过，
  "ClientHello SNI 均通过"）；维护者与Mira上游。登记时状态为 Open 的记录保留；
  MiniMax 端点复验随后续在线验收另行记录。

## 跟进记录

| 日期 | 编号 | 行为与回执 | 下一步 / 负责人 |
| --- | --- | --- | --- |
| 2026-10-05 | MIRA-20261004-002 | 复核pinned与master、离线复现并提交[#72](https://github.com/Linductor-alkaid/mira/issues/72)；API回读确认作者/编号/完整正文/Open | Mira上游处理；Mirage维护者在上游交付后评估授权升级并按该条验收复跑 |
| 2026-10-05 | MIRA-20261004-001 | 复核pinned与master、离线复现并提交[#73](https://github.com/Linductor-alkaid/mira/issues/73)；API回读确认作者/编号/完整正文/Open | Mira上游处理；Mirage维护者在上游交付后评估授权升级并按该条验收复跑 |
| 2026-10-05 | MIRA-20260927-001 | 复核pinned与master、离线复现并提交[#74](https://github.com/Linductor-alkaid/mira/issues/74)；API回读确认作者/编号/完整正文/Open | Mira上游处理；Mirage维护者在上游交付后评估授权升级并按该条验收复跑 |
| 2026-10-05 | MIRA-20260922-001 | 复核pinned与master、离线复现并提交[#75](https://github.com/Linductor-alkaid/mira/issues/75)；API回读确认作者/编号/完整正文/Open | Mira上游处理；Mirage维护者在上游交付后评估授权升级并按该条验收复跑 |
| 2026-10-08 | MIRA-20261008-001 | 复核pinned fbc644b与master d5c7d06（含PR#79/80/81合并）、源码路径核对与真实服务在线对照后提交[#83](https://github.com/Linductor-alkaid/mira/issues/83)；API回读确认作者/编号/完整正文/Open | Mira上游处理；Mirage维护者在上游交付后评估授权升级并按该条验收复跑 |

本轮仅反馈，四项均保持Open。未修改Mira/Executor/Mirador源码或pin，未向Executor或Mirador提交issue。

<a id="mira-20261005-001"></a>

## MIRA-20261005-001：真实流式预览与Chat Completions SSE

- 状态：Resolved（上游 PR#79 已合并、pin 消费链路复验通过）；负责人：Mirage维护者；范围：M6-17/18、DEC-041。
- 核对：3716dbf的公开InferOptions/ProviderInferOptions无实时sink；take_last_preview仅在infer返回后可用；OpenAiCompatibleProvider流式入口仅Responses，ConversationLoop固定默认选项。
- 复现：配置Chat模型并启用stream被Provider拒绝；Responses回调只供内部parser消费，UI在终态前没有公开文本事件。已排除Mirage的显示/连接配置问题。
- 影响：无法真实增量输出；私自解析协议会重复Mira已有模型职责。
- 最小能力：有界非权威完整快照sink、两种协议SSE归约、ConversationLoop选项透传及最后usage。
- 语义：预览不得进入历史、触发工具或作为成功依据；重试清空、取消停止投递、终态覆盖；断流失败。
- 延期影响：保留等待状态直至最终回复，不模拟打字。2026-10-05维护者授权修复并要求上游PR。
- 验收：分片中文、工具参数、预算/终态拒绝、实时回调、异常隔离/取消和下游晚到预览门禁。
- 移除条件：同步上游修复提交与锁文件、完成集成验收；已提交[上游PR#79](https://github.com/Linductor-alkaid/mira/pull/79)，本地pin 0a099ba集成验证通过；2026-10-08 master 复核确认 PR#79 已合并（d5c7d06 含 PR#79/80/81），消费侧以 0a099ba 及后续 pin 持续承载流式预览，状态改 Resolved。


## M6-18升级复验

3716dbf（PR#76）加流式补丁0a099ba已同步锁文件；`run-probe.py --fixed`退出0，
workflow digest解析已验证版本、ClientHello SNI均通过。公开ConversationLoop及规范
ToolResultPart在集成测试及两家真实供应商工具往返通过。因此#74/#73/#72对应三项为
Resolved，历史探针现象保留不重写；Executor MinGW#75仍Open。
MIRA-20261005-001的上游PR#79尚未合并；本地修复通过不冒充上游合并。
详见[本轮验收](../compatibility/native-conversation-progress-20261005.md)。


M6-19收尾回读：Mira PR#79的pull_request run37332976997在当前pin 0a099ba上12/12 jobs
成功，含Windows/Android、GCC/Clang、sanitizers与quality；同SHA push另有未完quality，
未合并PR。依赖CI通过不替代Mirage Windows界面真机验证。

## MIRA-20261006-001：Anthropic Messages方言缺口

- 状态：Resolved（Linux本地验收范围）；负责人：Mirage维护者；[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已合并（2026-10-08 master d5c7d06 复核含 PR#79/80/81）。
- 基线：0a099ba；公开model_profile.hpp仅有Responses/Chat Completions，model_dialect.hpp无Messages mapper；本机最新公开头亦无Anthropic能力。
- 复现：protocol_dialect_from("anthropic.messages.v1")返回nullopt；ModelLayer拒绝配置，不存在可选公开映射器。不是URL或Key配置错误。
- 影响：维护者要求的Messages与仅提供此协议的厂商无法接入；延期会使预设入口成为假功能。
- 最小能力：固定方言、/messages路径、SecretRef认证模式、文本/图片/工具往返、严格SSE终态/预算与错误；复用现有transport和Executor。
- 验收：离线协议/SSE/认证fixture及MiniMax-M3真实图片和工具请求；不隐式fallback到其他协议，不吞掉未知行为/extended thinking。
- 临时措施：无；按明确授权在Mira独立分支修复并提交PR，随后同步Mirage pin/lock/回归。嵌套Executor不改。

2026-10-06：Mira 7795e13交付Messages公开方言、认证与SSE/工具/图片映射；7/7协议回归，ASAN/UBSAN/TSAN解析/认证及变异种子通过，MiniMax-M3两图与工具往返实测通过。Mirage同步gitlink/lock，50/50 Debug及相关Release/ASAN/UBSAN回归通过；TSAN受控ASLR两项通过。其他厂商/平台不在本记录互操作结论内，负责人维护者按各自授权环境补跑；上游CI另行跟踪。无临时私有循环，Executor不改。

<a id="mira-20261006-002"></a>

## MIRA-20261006-002：Messages 思考控制与签名回填

- 状态：Resolved（Linux 本地范围）；负责人：Linductor-alkaid；关联 Mirage M6-26 / DEC-046。
- 版本：7795e13；已核对 model_contracts.hpp、model_dialect.hpp、ConversationLoop 与 m3_anthropic_test。现有 GenerationOptions 只有 reasoning_effort，Messages 明确拒绝；SSE 对 thinking block 返回 CapabilityMismatch。
- 复现：设置 Messages generation.reasoning_effort=High，encode_request 拒绝；输入 thinking content_block_start 同样拒绝。不是 Mirage 参数选型错误：公开契约没有 thinking mode 或签名内容。
- 影响：MiniMax-M3 的 adaptive thinking 与 Claude 扩展思考无法从会话启用；不能丢弃签名后继续工具调用。
- 最小能力：明确的 thinking mode；有界 thinking/redacted blocks；规范序列化及完整 assistant block 工具回填；保留不支持方言的显式拒绝。
- 验收：同步与 SSE 内容/签名一致，工具请求回填顺序不变，非法模式/角色/越界/缺终态拒绝，真实 MiniMax 开关可用。延期则会话栏只能显示已确认可用能力。依赖修复按已有授权实施并提交上游 PR。

- 获批修复：Mira fbc644be2fabfaa2f8257e579fbc1d368537224c；[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81)，基于 Messages PR#80，已合并（2026-10-08 master d5c7d06 复核含 PR#79/80/81）。Executor 与其他内嵌 pin 未变。
- 收敛：DEC-051 的 ThinkingMode / ThinkingPart、SSE 有界签名与 redacted 回填由 Mira 承载，Mirage 仅负责模型选项投影。7 项上游协议测试、Mirage 52 项 Release 回归与真实 MiniMax 开关/工具循环通过；[完整验收](../compatibility/session-thinking-20261007.md)。没有临时并发或协议实现。其他厂商在线/Windows 与当前 kairo 主干未复验，维护者取得目标环境后补跑。

<a id="mira-20261008-001"></a>

## MIRA-20261008-001：Responses / Chat Completions 方言丢弃思考输出

- 状态：Resolved（2026-10-08 收口，见下方复验收口段；chat-completions/Messages 真机 + Responses fixture 范围）；负责人：Linductor-alkaid；关联 Mirage M6-26 / DEC-046、MIRA-20261006-002。
- 版本：pinned fbc644be2fabfaa2f8257e579fbc1d368537224c（2026-10-07）。已核对 `model_dialect.cpp`、`model_chat_sse.cpp`、`model_anthropic.cpp`/`model_anthropic_sse.cpp` 及 model_contracts 回放解码。
- 复现：Anthropic Messages 方言把 `thinking` block 与 `thinking_delta` 映射为 `ThinkingPart`（model_anthropic.cpp:368、model_anthropic_sse.cpp:160）；Chat Completions 方言把非流式 `message.reasoning_content` 归入 `UnknownOutput` 且只留 payload digest（model_dialect.cpp:1109-1113），流式 `delta.reasoning_content` 在 `ChatCompletionsSseParser::reduce` 中被直接丢弃（model_chat_sse.cpp:104-106 只累积 content/refusal）；Responses 终态解码对 `reasoning` item 无分支，落入 UnknownOutput 摘要（model_dialect.cpp `decode_responses_terminal_body`）。已排除 Mirage 选型/配置问题：`reasoning_effort` 三方言均正常编码上线，MiniMax（Messages）思考内容端到端可见，Chat Completions 服务在带 `reasoning_effort` 请求下返回的思考文本无法到达 `ThinkingPart`。
- 影响：仅在 Chat Completions / Responses 方言上提供思考输出的模型（DeepSeek-R1 系、经 Responses 代理的 reasoning 模型等）无法在会话过程展示"思考过程"；文本被静默降级为摘要，用户感知为"开启思考无效果"。
- 最小能力：两方言把 reasoning 增量/终态映射为有界 `ThinkingPart` 输出（与 Messages 方言同语义：预算截断、redacted 保留摘要、不做权威历史输入）。
- 延期影响：这两类服务的思考档位仅影响上游生成行为，无可见思考内容；Mirage 侧不私自解析协议重复 Mira 职责。
- 验收：两方言 fixture 覆盖增量/终态/越界丢弃/redacted 语义；真实 reasoning 模型各一轮增量可见；Messages 行为不回退。
- 临时措施：无（不旁路解析 SSE）。

2026-10-08 复验收口：授权升级 pin 至 master bfcb8e7（[上游 PR#84](https://github.com/Linductor-alkaid/mira/pull/84) 已合并，DEC-052；内嵌 kairo ef821dc 同步，DEC-048）。真机复验按 DT-04 宿主侧口径执行：chat-completions（SiliconFlow Qwen/Qwen3.5-4B，默认档）思考 1059 字符经 `ThinkingPart` 端到端可见，修复前同形态供应商返回被丢弃；Messages（MiniMax-M3，adaptive）回归正常。Responses 方言暂无真实 reasoning 供应商（SiliconFlow 无 /v1/responses，HUA 上游剥离 reasoning），以该方言上游 fixture 为验收依据，真实供应商补跑条件与 DT-04 一致。构建与回归证据见[依赖升级审计](../supply-chain/dependency-upgrade-audit.md)；真实 UI 端到端展示验收（默认折叠行 + 点击展开）见[会话思考展示验收](../compatibility/native-dialect-thinking-20261008.md)。状态改记 Resolved（chat-completions/Messages 真机 + Responses fixture 范围）。

<a id="mira-20261008-002"></a>

## MIRA-20261008-002：core_contracts.hpp 纳秒转换在 GCC/libstdc++ 下触发 -Wuseless-cast

- 状态：Open；负责人：Linductor-alkaid；关联 Mirage 消费构建（MirageWarnings `-Wuseless-cast`）。
- 版本：pinned bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5（2026-10-08，PR#84 之后）。已核对 `include/mira/core_contracts.hpp:186-197`。
- 复现：GCC/libstdc++（nanoseconds::rep 即 int64_t）下，任一 Mirage 目样包含该头即报 `useless_cast`：`wall_nanos`/`monotonic_nanos` 对 `duration_cast<nanoseconds>(...).count()` 再做 `static_cast<std::int64_t>`。上游注释表明该 cast 为 libc++（rep 非 int64_t）刻意保留。不是 Mirage 误用：旧 pin 无该代码。
- 影响：Mirage 自有目标以 `-Werror -Wuseless-cast` 作为质量门禁，pinned 头在所有消费 TU 触发错误；无法在消费侧按头抑制。
- 最小能力：与 libc++ 兼容且 libstdc++ 下无冗余 cast 的写法（例如以 `std::chrono::nanoseconds::rep` 为目标类型的条件转换，或隔离到非 inline 辅助）。
- 延期影响：Mirage 在 GCC 下停用 `-Wuseless-cast`（见 `cmake/MirageWarnings.cmake` 注释）；自有代码少一项冗余转换检查。
- 验收：Mirage 恢复 `-Wuseless-cast -Werror` 全量构建通过；上游交付后按授权升级复验并移除临时措施。
- 临时措施：`MirageWarnings.cmake` 对 GCC 改为 `-Wno-useless-cast`（含移除条件注释）；仅影响 Mirage 自有目标的告警面，不改 pinned 代码。

<a id="mira-20261009-002"></a>

## MIRA-20261009-002：Messages SSE 解析器对 max_tokens 截断流 fail-closed

- 状态：Resolved（上游 PR#87 合并 c75378b，已授权消费并复验）；负责人：Linductor-alkaid。关联 Mirage M7 会话桌面工具（harness 路径）、MIRA-20261008-001 的 Messages 思考链路。
- 上游：[mira PR#87](https://github.com/Linductor-alkaid/mira/pull/87)（分支
  `fix/messages-sse-cutoff-tolerance`，提交 c8915fd）：孤立 `content_block_stop` 合成空文本块闭环；
  `tool_use` 参数校验移至 `finish()`，仅 `stop_reason=max_tokens` 容忍截断并保留空参块（解码为
  Incomplete/MaxOutputTokens）；未 start 块 delta、重复 stop、其他终态下的截断仍 fail-closed。
- 版本：pinned 698fc77c151fa93b1b52ea4dd25cb95a3a60bee5（2026-10-09）。已核对
  `src/model/model_anthropic_sse.cpp`（`reduce` 的块生命周期校验与 `content_block_stop`
  的 `tool_use` 参数解析）、`src/model/conversation_loop.cpp:264`（每步请求
  `max_tokens = config_.max_output_tokens_per_turn`）。
- 复现：真实 MiniMax-M3（`api.minimaxi.com/anthropic`，2026-10-09 授权在线抓流）在
  `max_tokens` 截断时发出两类非规范流，均被 `AnthropicMessagesSseParser` 以同一条
  `invalid()`（"invalid Messages SSE lifecycle or budget"，ProtocolViolation）拒绝：
  1. 孤立 `content_block_stop`：截断落在块调度与首个 `content_block_start` 之间时，
     服务端发出从未 start 的块的 stop（`reduce` 中 `position >= blocks_.size()` →
     invalid）。max_tokens=1 时 4/4 复现。
  2. `tool_use` 参数 JSON 截断：截断落在 `input_json_delta` 内部时，块以不完整 JSON
     （如 `{"path":"/tmp`）收口，`content_block_stop` 的 `parse_json` 失败 → invalid。
     tools + max_tokens=32/64 时 2/2 复现。
  入库离线复现（合成流固定形状，逐字节喂入）：
  [minimax-cutoff-sse.cpp](repro/minimax-cutoff-sse.cpp)，输出 A/B 均
  FEED-ERROR: invalid Messages SSE lifecycle or budget，规范截断对照（截在 thinking
  块内部）可解码。已排除 Mirage 侧职责：请求编码为 pinned mapper 产物，三档思考
  （默认/adaptive/disabled）与历史组合在线 20+ 轮均未触发；用户报告的"思考模式变化"
  相关性实为档位切换改变每轮输出预算（2048↔8192），截断概率随之变化，重放原失败
  上下文三档均不触发，模式切换本身无服务端错误。
- 影响：Messages 方言 + 工具（Mirage 通用会话 harness 默认路径）下，任何生成触及
  每轮 `max_tokens` 的回合都可能整轮失败，用户可见 "model call failed: invalid
  Messages SSE lifecycle or budget"，无重试价值（同类长生成复现概率高）；会话无法
  自愈，属于产品级可用性缺陷。纯文本截断不受影响（规范流可解码为 length 终态）。
- 最小能力：`AnthropicMessagesSseParser` 对截断形态宽容——(a) 忽略针对未 start 块的
  `content_block_stop`（视为空块闭环）；(b) `tool_use` 参数在截断（stop_reason=
  max_tokens）下不可解析时，产出有界的 length/incomplete 完成态或可诊断的专用错误
  （"tool arguments truncated by output budget"），而非生命周期违规。不放宽其他
  生命周期校验（乱序 start、用量回退、缺失 message_stop 等仍 fail-closed）。
- 延期影响：Mirage 以适配层缓解（harness 每轮输出下限提高至 8192，引用本编号），
  截断概率下降但未消除；长工具参数（大文本写入类动作）仍可能整轮失败。
- 验收：入库复现 A/B 改为可解码（A 为空 length 完成、B 为可诊断截断结果或带
  digest 的 length 完成），既有 Messages SSE fixture（签名/redacted/预算/切点）全部
  不回退；真实 MiniMax 截断回合可终态到达会话（失败也须带正确 stop_reason 语义）。
- 临时措施：无（维护者指示直接上游修复；Mirage 不旁路 SSE 解析、不调每轮预算）。
- 消费与复验（2026-10-09）：维护者授权合并 [PR#87](https://github.com/Linductor-alkaid/mira/pull/87)
  （merge commit c75378ba1374450ae667af64561f86db1f90f4c4，仅 model_sse.hpp /
  model_anthropic_sse.cpp / m3_anthropic_test.cpp，无嵌套 pin 变化），Mirage pin
  同步升级至该提交（dependencies.lock.json 同步，工作分支已删除，验证用 build 树已清理）。
  复验：上游侧 m3 全量 105/105、ASAN/UBSAN 干净（[PR#87 CI](https://github.com/Linductor-alkaid/mira/pull/87) 24/24）；
  消费侧入库复验工件 [minimax-cutoff-sse.cpp](repro/minimax-cutoff-sse.cpp)
  A/B/C 均解码为 Incomplete/MaxOutputTokens 终态，真实抓流 4 条（orphan-0/1、
  toolcut-none-32/64）由修复前的 FEED-ERROR code 18 全部转为解码。协议层即真实
  MiniMax 截断回合字节；产品内截断回合的会话终态随下次在线验收（opt-in 付费）补跑。

<a id="mira-20261012-001"></a>

## MIRA-20261012-001：网络阶段超时被标记不可重试，单次网络停滞判死整轮

- 状态：Resolved（上游 PR#88 合并 57c3062，授权消费并复验）；日期：2026-10-12；负责人：Mirage 维护者，处理方：Mira 上游。
  关联 DeepSeek 修复包验收会话（docs/compatibility/deepseek-package-20261012.md）与 [DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)。
- 现象与复现：2026-10-12 09:56 真实会话（本地状态 `~/.local/state/mirage/session-state.json`
  会话 `80612890…` turn `ab1ef22…`）中，桌面任务第 6 次模型调用在前 5 次全部成功后
  约 10.2s 失败，turn 落盘为 `model call failed: exchange stage deadline exceeded`
  （错误串出自 pinned c75378b 的 `adapters/net/socket_transport.cpp` poll_wait）。
  时间与 TLS/write 阶段 10s deadline 吻合，为 `api.deepseek.com` 路径上的单次瞬时停滞；
  同会话 10 秒前 4 次带图请求（单图供给，体积恒定）全部成功，排除负载增长与桌面工具因素。
- 首次反馈核对版本：`c75378ba1374450ae667af64561f86db1f90f4c4`。已核对
  `adapters/net/socket_transport.cpp`（阶段 deadline 一律 `retryable=false`）、
  `src/model/loop_support.hpp`（恢复白名单仅 RateLimited/ProviderOverloaded/TransportFailed）、
  `src/model/model_supervisor.cpp`（PreWrite 分支要求 `failure.retryable`）、
  `docs/design/llm-api-protocol-design.md` §10.2（设计已允许 request bytes 前
  DNS/connect/TLS 失败在预算内重试）——实现与设计相悖；网关重试决策表与幂等策略本身无缺口。
- 应用职责排除：Mirage 侧 deadline 覆盖、循环配置、代理链路均属应用层；"阶段超时不可恢复"
  是 pinned 传输与循环的分类语义，应用层无法在不绕过 ModelGateway 重试设施的情况下恢复。
- 影响：一次瞬时网络停滞（代理抖动、供应商瞬时卡顿）击穿两层有界重试预算，整轮 turn 判死；
  与主流 SDK 默认对超时类错误做有界重试的行为相悖。
- 最小期望能力：网络阶段 deadline 错误携带 `retryable=true` 机制提示（total 预算约束的等待
  保持不可重试），循环恢复白名单承认该提示；重试决策表、幂等策略、预算与 circuit 不变。
- 延期影响：不修复则图像化桌面任务在网络抖动下持续出现整轮失败，用户可感知且无法自愈。
- 上游修复：[PR#88](https://github.com/Linductor-alkaid/mira/pull/88)（分支
  `fix/net-stage-deadline-retryable`，提交 1d19dee，merge 57c3062）：poll-wait 链路携带
  调用方计算的 `deadline_retryable`（`stage_deadline < total_deadline`），total 约束的等待
  仍报不可重试（header 等待回卷为既有 total 消息）；`loop_support` 白名单加入 retryable 的
  `ModelDeadlineExceeded`；设计 §10.1/§11 同步。CI 24/24 通过。
- 消费与复验（2026-10-12）：维护者授权合并并升级 pin 至 57c3062（dependencies.lock.json
  同步，工作分支已删除）。复验：上游侧 m3 全量 105/105（Debug 与 ASAN-only，含新增
  transport/supervisor/conversation/agent loop 的 deadline 正反向用例）；消费侧
  `integration/mira/src/model_layer.cpp` 放宽 profile `tls=20s`/`write=30s` 并按 RULE-08
  信封启用阈值化循环恢复预算（capacity ≥ 10 时 recoveries=2 且 turns ≥ 7，稀缺预算保持
  原状），`tests/runtime/native_agent_integration_test.cpp` 新增 deadline 恢复行为用例
  （retryable 单次失败经真实 gateway/supervisor/loop 重试成功、不可重试立即失败、post-write
  场景恰好消耗 2 次恢复预算），356 checks 0 failures，Mirage 全量 71/71，ASAN/UBSAN
  通过（[兼容性文档](../compatibility/model-network-retry-20261012.md)）。
- 临时措施：无（维护者指示直接上游修复；Mirage 不旁路网关重试、不自建重试层）。

<a id="mira-20261012-002"></a>

## MIRA-20261012-002：ProviderCircuit 开启后无时间恢复路径，cooldown 配置未实现

- 状态：Open；日期：2026-10-12；负责人：Mirage 维护者，处理方：Mira 上游。
- 首次反馈核对版本：pinned `57c3062`（MIRA-20261012-001 消费版本）。已核对
  `include/mira/model_supervisor.hpp`（`CircuitConfig::cooldown{5'000}` 仅有配置声明）、
  `src/model/model_supervisor.cpp`（`record_success` 在 `OpenCircuit` 态为空操作；
  `record_failure` 仅在 `Probing` 态回开；`CircuitState::Probing` 全库无赋值点）、
  `src/model/model_gateway.cpp`（open 电路上 `admits_requests()` 直接拒绝并 `continue`，
  无任何定时器或延迟后转入 Probing 的路径；`submit_delayed` 仅用于重试间隔）——
  熔断一旦 `OpenCircuit`，在本 ModelGateway 实例生命周期内永久拒绝，时间恢复语义缺失。
- 应用职责排除：Mirage 通过公开 `ModelGateway`/`ModelLayer` 承载模型调用，不旁路网关、
  不自建熔断或重试层；应用层既无 `circuits_` 的写入口，也无重建网关之外的恢复手段。
- 现象与复现：`tests/runtime/native_agent_integration_test.cpp` 的脚本化失败场景
  （适配层拒绝未声明工具名、取消在途调用）累计 3 次 provider 失败后熔断开启，
  后续全部模型调用立即以 "provider circuit is open" 失败；间隔 1 秒的 30 次探针
  （约 30 秒 ≫ 5 秒 cooldown）无一放行。同一二进制在失败计数未达 3 的运行中表现正常，
  证明开启与否取决于场景时序，且开启后不可自愈。复现命令：
  `ctest --test-dir build/native-debug -R '^native_agent_integration_test$' --output-on-failure`。
- 影响：长生命周期宿主（产品 Runtime Service）一次 3 连失败即永久失去该 profile 的
  模型能力，直到模型配置变更重建 ModelLayer；与 `CircuitConfig::cooldown` 声明的
  半开恢复语义相悖，用户可感知且无法自愈。
- 最小期望能力：`OpenCircuit` 经 cooldown 后进入 `Probing`（放行一次探测请求，
  成功按 `consecutive_successes_to_close` 闭合，失败回开），cooldown 语义与现有
  `CircuitConfig` 字段一致；或在该版本明确文档化"熔断为网关生命周期内永久态"，
  由消费方按重建网关处理。
- 延期影响：测试以 `model.set` 重建 ModelLayer 规避（见该测试熔断探针段注释）；
  产品侧行为维持"配置变更前该 profile 持续不可用"，不放宽。
- 验收：上游交付定时半开恢复或明确语义后，测试移除 `model.set` 重建并以真实
  cooldown 恢复路径验证探针放行；Mirage 侧不新增并行熔断设施。

## MIRA-20261013-001：`Result<T>::value()` 对 error 态未定义行为解引用

- 状态：Open；日期：2026-10-13；负责人：Mirage 维护者，处理方：Mira 上游；上游：
  未提交（待维护者评估提交时机与形态）。
- 公开API核对：`include/mira/core_contracts.hpp` 的 `Result<T>::value()`（约 260-262 行）
  对 error 态（`value_` 为空 `std::optional`）直接 `return *value_;`，无断言、无防线；
  `error()` 同理返回空 optional 的解引用。属公开 API 形状，不是使用错误。
- 现场证据：2026-10-13 DEC-085 续写特性测试中，测试侧
  `parse_json(非法JSON).value()` 把 optional 原始存储重解释为 `JsonValue`（variant
  index 为垃圾字节 0xff），析构经 GCC variant 跳表分发跳入未映射内存——SIGSEGV 且
  无符号栈（gdb/unwind 全部失效），排查成本极高。布局敏感：ASAN 与部分运行下垃圾
  字节恰好落在合法备选项上，问题呈随机隐匿形态（详见 mirage 侧排查记录：
  docs/compatibility/session-resilience-fix-20261013.md 与上游 PR #93 排查段）。
- 影响范围：仓库内测试与示例大量使用 `parse_json(...).value()` 惯用法；任何一处
  输入非法即触发静默 UB，且崩溃点远离缺陷点。
- 最小期望能力：`value()`/`error()` 在错误/空态显式失败（断言 + domain 标注的
  abort，或返回带 diagnostics 的 Error 语义），使误用成为确定性问题。
- 延期影响：Mirage 侧测试规范补充"`.value()` 只能用于字面量已验证合法的常量"
  约定并自查；产品代码不受影响（error 路径均检查）。
- 验收：上游交付防线后，本条归档；Mirage 测试中的 `.value()` 惯用法按新语义复核。
- Mirage 侧临时措施落地（2026-10-16）：测试支撑头以静态断言/注释约定固化
  "`.value()` 只能用于字面量已验证合法的常量"，见 `tests/support` 引用本编号的说明。

<a id="mira-20261016-001"></a>

## MIRA-20261016-001：语义相似度计算能力（M10 记忆系统前瞻确认）

- 状态：Accepted（公开契约部分交付，产品接线与质量未验收）；日期：2026-10-16；负责人：Mirage 维护者。
- 来源：[M10 预备清单](../plans/m11-pre-work-checklist.md)第 1.5 节"依赖能力确认"预留
  本编号：需与 Mira 维护者确认 pinned mira 是否计划提供语义相似度计算（版本、API 形式），
  若无则确认是否接受本反馈条目。编号按"一经引用不得复用"规则先行占位。
- 预期需求：记忆系统（M10+）需要查询-记忆相关性评分；候选 fallback 为关键词 Jaccard
  相似度（召回率待标注测试集验证）或本地 embedding 模型（M12 评估）。
- 登记时的后续条件：M10 预备清单"依赖能力确认"完成后，本条转 Open（登记正式缺口与最小能力）
  或 Rejected（确认由宿主/其他承载方解决）；在此之前不向 Mira 上游提交、不引入
  embedding 运行时。

2026-10-25 公开能力复核：pin `68a8bf1` 的上游 PR#103 / DEC-060 已提供
`IEmbeddingProvider`、`IMemoryEmbeddingIndex` 与 `MemoryEmbeddingIndexer`，
SqliteMemoryStore 支持向量查询、缺失向量降级、内容变更失效与有界修复。
生产者实现及 Executor 路由由宿主提供；Mira Core 不包含语义模型推理实现。
因此接口确认已完成，后续应复用该契约，不再自建记忆向量索引或以 Jaccard
代替上游召回面。此次不引入 embedding 运行时、不启用产品记忆；真实模型的
召回质量/资源预算与 M10 接线仍未验收，不能标为 Resolved。
负责人 Mirage 维护者；补跑条件：M10 设计冻结、选定获准模型与标注数据集。

<a id="mira-20261016-002"></a>

## MIRA-20261016-002：会话循环对 provider error 与输出截断的韧性/续写语义缺失（DEC-085 系列，追记）

- 状态：Resolved（追记登记）；日期：2026-10-16（缺口与修复实际发生于 2026-10-13）；负责人：Mirage 维护者，处理方：Mira 上游。
- 追记说明：三项上游交付在 DEC-085 实施期间经维护者授权合并并消费，但未按 9.4 登记
  台账条目；本条为 2026-10-16 台账一致性整改（REUSE-18）的补登记，证据沿用既有审计
  与兼容性记录，不新增声明。
- 缺口（当时核对 pinned `57c3062`）：Messages SSE 解析器把 provider `error` 事件一律
  判为协议违规（整轮失败，无恢复价值）；对容忍的 max_tokens 截断，恢复反馈不区分
  形态（generic answer-with-text 复现同样截断并烧毁恢复预算）；截断续写只能整轮重
  生成（部分文本丢弃、上下文重复计费）。
- 上游交付（均已授权合并、Mirage 随 pin 消费并复验）：
  - [PR#91](https://github.com/Linductor-alkaid/mira/pull/91)（merge `bb39738`，closes
    upstream issue #90）：provider error 事件按 overloaded/rate-limit/timeout/transport
    分类进循环恢复白名单（retryable），client 错误按自身 code fail-closed，未知类型
    fail-closed；容忍尾随 ping 与 message_stop 后重复 message_stop，post-terminal
    垃圾仍拒绝。
  - [PR#92](https://github.com/Linductor-alkaid/mira/pull/92)（merge `5791d2d`）：被容忍
    的 max_tokens 截断（Incomplete 无可用文本）的恢复反馈改为"只请求简洁纯文本"，
    不再诱导工具调用与长思考。
  - [PR#93](https://github.com/Linductor-alkaid/mira/pull/93)（merge `1befa45`）：
    `max_output_continuations`（默认 2，独立于 recoveries）——部分文本截断保留为
    assistant 轮并从切点继续，截断在工具调用中的回复丢弃不执行，纯思考截断请求
    简洁纯文本；耗尽以显式输出上限摘要结算。
- 验收证据：[DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md)、
  [会话韧性修复记录](../compatibility/session-resilience-fix-20261013.md)、
  dependencies.lock.json notes（DEC-085 系列）。Mirage 侧消费为 ConversationLoop 恢复
  预算与有界非流式重试的最后兜底层。

<a id="mira-20261016-003"></a>

## MIRA-20261016-003：kairo 0.7.0 延期能力（QoS 标注与 AdaptiveScheduler）触发条件跟踪

- 状态：Open（分层采用延期，非缺陷）；日期：2026-10-16（实际升级发生于 2026-10-13/15）；负责人：Mirage 维护者。
- 背景：kairo pin 经上游 [PR#89](https://github.com/Linductor-alkaid/mira/pull/89)
  （merge `99461b3`）自 v0.6.0 升级至 v0.7.0（`32027c2`，additive release，零代码迁移，
  上游 DEC-054）。按上游分层采用策略：`SchedulingMetrics`（`get_scheduling_metrics()`）
  现已可用；**QoS 标注与 `AdaptiveScheduler` 注入延期**，由上游记录触发条件；
  `FeedbackAggregator` 绑定 `wants_feedback()` 调度器；CPU/GPU 选择不适用。
- 为什么登记：延期的能力是"未来条件触发后 Mirage 应跟进消费"的依赖能力项，此前仅有
  供应链审计文档承接，无台账跟踪；按 9.4 补登记（REUSE-18）。
- 跟进：触发条件与采用评估见
  [kairo 0.7.0 升级审计](../supply-chain/mira-kairo-070-20261013.md)与 lock notes
  （MNT-2026-10-14/15 段）；触发条件满足时由维护者评估 QoS 标注/AdaptiveScheduler
  的授权消费，本条随之收口。
- 影响：当前无行为缺口——Mirage 任务面继续以 tier-0（默认调度 + SchedulingMetrics）
  运行；不预建 QoS/自适应调度消费代码。

<a id="mira-20261016-004"></a>

## MIRA-20261016-004：观察管线无 RGBA→PNG 转码缝，宿主需自维护 DEFLATE/PNG 编码器

- 状态：Open（临时方案在合法 Adapter 边界内运行）；日期：2026-10-16；负责人：Mirage 维护者，处理方：Mira/Mirador 上游（待评估提交）。
- 版本与核对：pinned mira `a29374a`。`include/mira/observation.hpp`（约 181 行）明示
  "RGBA → PNG"转码由 host 负责；核对 mira/mirador 公开头与其 third_party（kairo、
  mbedtls、sqlite、googletest），均不提供 PNG/DEFLATE 编码能力。已排除 API 选型与
  配置问题：桌面截图工具需要把 RGBA 帧编码为 PNG artifact 供下一轮 ImagePart 供给
  （M7-04/05 交付链），该步骤无承载方。
- 影响：`integration/mira/src/desktop_png.cpp`（约 340 行）自研 PNG 编码器（固定
  Huffman + LZ77 哈希链、Adler-32、CRC-32、行滤波）是仓库内最重的自维护编解码器，
  承担压缩正确性与跨平台行为责任；无上游对齐基准。
- 最小期望能力：mira 观察管线或 mirador 图像模块提供有界的 RGBA→PNG 编码（预算、
  错误契约与既有 artifact 语义一致），或明示推荐的宿主侧最小实现契约。
- 临时方案与边界：维持 `desktop_png.cpp` 于 integration/mira Adapter 边界内；不创建
  线程/队列/调度器；编码经 Executor 管理的任务执行。移除条件：上游交付等价编码能力
  并经授权消费、复验后删除自研实现。
- 验收：替换后现有 PNG artifact/识图链路 fixture 与三方言供给回归不回退；编码输出
  经独立解码器校验。未运行验证的负责人：Mirage 维护者；补跑条件：上游能力交付。
- 关联：REUSE-04（[整改计划](../plans/dependency-reuse-remediation.md)）。

<a id="mira-20261014-004"></a>

## MIRA-20261014-004：无公开实时思考分类预览入口

- 状态：Resolved（公开能力和 fixture 范围）；日期：2026-10-14；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[PR#94](https://github.com/Linductor-alkaid/mira/pull/94)，已授权合并；当前消费merge 22d9577，历史fixture head为ea720a6。关联 [M8-11](../plans/m8-mira-pet.md)、
  [DEC-089](../decisions/DEC-089-mira-pet-bubbles.md) 和气泡设计第 6 节。
- 缺口复现基线：`1befa451713e0eb635da6077410e284f7b8ea88b`（升级前）。
  公开 `include/mira/model_sse.hpp` 的 `UnvalidatedModelPreview` 只有正文 `text`、
  `dropped_updates`、`truncated`；`ModelPreviewSink` 无思考类别/内容字段。
  `model_gateway.hpp` 与 `model_provider.hpp` 沿同一 sink；
  `model_contracts.hpp` 的 `ThinkingPart` 仅规范响应内容。
  `docs/api/model-agent-loop.md` 明确 Chat reasoning 与 Messages thinking 不进入正文预览。
- 可复现证据：运行
  `python3 docs/dependency_feedback/repro/mira-thinking-preview-contract.py`；它只读公开头、
  API 和相关测试断言，输出当前能力形状。上游 `m3_sse_test.cpp` 的
  `chat_reasoning_stream_maps_to_thinking()` 断言纯 reasoning 时 preview.text 为空、
  终态才有 ThinkingPart；`m3_anthropic_test.cpp` 的 `thinking()` 对所有双片切点断言
  思考只进入终态且 preview.text 为空。本轮只静态核对这些测试，未运行上游测试二进制，
  不宣称供应商在线流或其他协议实测通过。
- API 选型/配置/应用职责排除：ModelGateway 的 stream 与 preview_sink 已由 Mirage
  ModelLayer 使用；打开 stream 无法让该 sink 返回思考类别。历史 DialogProcessSink
  和完成后的 ThinkingPart 不能反映正在进行的流。不是未开启模型思考或平台窗口权限
  问题；气泡形状、滚动、摘要和桌面避让仍属于 Mirage。此反馈请求区分正文与可展示
  思考，不要求把 thinking 混入 answer preview，不请求 redacted/签名内容。
- 影响：M8-11 能用正文生成输出概括，也能用模型活动事实滚动“正在思考…”提示，
  但无法按公开契约实时渲染模型思考文字。旧正文预览语义正确，不把本条当解析器缺陷。
- 最小期望能力：增加可选、分类且明确 unvalidated 的思考预览 sink/字段，提供请求/
  尝试清空语义、至多有界完整文本快照、截断/drop 统计；取消停止、规范结果独立结算，
  redacted/签名永不泄漏。回调维持传输上下文只投递的纪律，旧正文 sink 语义不变。
- 延期影响：真实思考文字门禁保持未完成，不以历史终态回放充当实时流。思考状态提示
  可先验收，角色/气泡几何契约可供 M8-06 使用，不因此阻塞全部避让设计。
- 临时措施：仅在既有 Integration/呈现 Adapter 边界根据当前模型活动投影状态提示，
  不解析 SSE、不读取私有实现、不创建线程/队列/调度器。差异为没有模型思考文字；
  风险是被误解为正文，因此载荷标明 status_hint。公开能力经授权升级并复验后移除
  “能力缺席才用提示”的限制；Provider 本身不供思考时仍保留明确提示降级。
- 可验收结果：Chat/Messages 分片、纯思考与思考/正文混合、不支持供应商、UTF-8、
  超限、重试清空、取消及迟到回调均有公开 fixture；正文预览不混入思考，规范历史与
  工具执行语义不变。Mirage 经授权消费后补真实模型流→IPC→气泡与安全清除验收。
  未运行验证的负责人：Mirage 维护者；补跑条件：上游公开入口及授权 pin 到位、完成
  M8-11 实现并获得真实供应商/目标平台环境。


### 2026-10-14 解决与消费

独立 Mira worktree 增加 opt-in `ModelThinkingPreviewSink`，Chat reasoning_content、Messages thinking_delta、Responses reasoning_text.delta 生成最新有界 UTF-8 快照；redacted/signature 不暴露，原正文 sink 不变。Mira Debug 106/106 与专项 ASAN/UBSAN 均通过。Mirage 通过 ModelLayer 接入，Runtime 双 LatestMailbox 按请求隔离，端到端 fixture 验证推送与终态安全清除。没有 Mirage SSE 解析旁路。供应商在线流和实桌面门禁独立，未把 fixture 记成在线结果。详见 [消费审计](../supply-chain/mira-thinking-preview-20261014.md) 和 [M8-11 证据](../compatibility/mira-pet-bubbles-runtime-20261014.md)。

<a id="mira-20261014-005"></a>

## MIRA-20261014-005：ConversationLoop 供给缝无跨轮输入复用身份标注入口

- 状态：Resolved；日期：2026-10-14；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[PR#95](https://github.com/Linductor-alkaid/mira/pull/95)，已授权合并
  （squash `9e7b729d0861577b470d45385bdd08301bd1226d`，合并前 checks 全绿）；
  Mirage 已随 pin 消费并复验。关联
  [DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
  能力 1 与 [实施计划](../plans/context-compaction-implementation.md) Phase 1。
- 核对版本：缺口核对于 pinned `1befa451713e0eb635da6077410e284f7b8ea88b`；交付与消费在
  `9e7b729d0861577b470d45385bdd08301bd1226d`。
  公开 `include/mira/conversation_loop.hpp` 的 `ModelInputSupplier` 每轮返回
  `Result<std::vector<ModelInputItem>>`，`resolve_supplied_input` 只做校验/限额/展平；
  无任何跨轮身份或复用元数据入口。DEC-053 明确历史回放不复制供给内容，宿主逐轮重发，
  因此同一截图逐轮重现是契约内行为，模型与循环都无法得知"第 N 次看到同一内容"。
- 可复现证据：Mirage 桌面任务会话诊断显示模型在相同 `artifact://` 引用上反复执行
  "让我分析屏幕内容"思考；`m3_conversation_input_supply_test` 全部请求形状不含任何
  复用提示。`ConversationLoop` 在 Mirage 侧逐 dialog turn 重建
  （`integration/mira/src/model_layer.cpp` 的 `invoke_loop`），跨轮计数只能由宿主持有。
- API 选型/配置/应用职责排除：打开 stream、调整 `ModelInputSupplyOptions`、宿主在
  goal 文本内自行拼接提示均不满足——goal 属任务描述、宿主自拼会绕开 envelope
  纪律与事件面；该能力必须由循环在受控 envelope 内渲染并计入事件/结果统计。
- 影响：图片密集多轮任务中重复识图浪费思考 token（DEC-088 估计 10-20%）；
  无标注时模型无法区分"同图"与"新拍恰好相同"，也不得不重复分析。
- 最小期望能力：新增可选的宿主身份回调（digest/连续复用次数/语义标签），循环在
  `reuse_count >= 2` 且本轮供给含图片时，把一条 Internal 权限的 System note 文本
  作为 User envelope 最后一个 part 追加；`ModelInputSupplied` 事件与
  `ConversationLoopResult` 增加可见计数；无回调时请求字节不变。
- 延期影响：已消除——上游交付 `set_model_input_identity` 身份缝（兼容性约束下走独立
  可选回调，不改变 supplier 返回类型），Mirage 侧由持久 `DesktopImageSource` 持有跨轮
  计数、`DesktopImageTurn::input_reuse_identity` 在 supply 同门控下解析、model_layer
  接线，消费记录见[同日验收](../compatibility/dec088-input-reuse-20261014.md)。
- 可验收结果：mira 单测覆盖首次/第二次不标注、第三次标注、标签渲染、非图片供给不
  标注、事件与结果计数、无回调字节不变；Mirage 侧在授权消费新 pin 后接跨轮计数并
  做真实会话对照验收。未运行验证的负责人：Mirage 维护者；补跑条件：上游 PR 合并、
  pin 授权消费、真实任务对照实验。

### 2026-10-14 解决与消费

PR#95 经授权 squash 合并为 `9e7b729`（兼容性约束下身份元数据走独立的可选
`ModelInputIdentityProvider` 回调，`ModelInputSupplier` 返回类型不变），mira Debug
全量 107/107。Mirage pin 随授权消费（gitlink + lock 同步），`DesktopImageTurn`
持有跨轮 digest/计数并经 `set_model_input_identity` 接线，
`DialogCompletion.supplied_input_reuse_annotations` 透传计数。fixture 验证：
连续 3 轮相同截图仅第 3 次渲染 Internal System note、计数与 digest 链语义、
退役/降级轮暂停、端到端 `supplied_input_reuse_annotations == 1`；Mirage Debug
81/81、ASAN 995 checks 无报告。真实会话对照实验（思考 token −10%~−20%）待
真实 provider 环境补跑，见
[消费验证](../compatibility/context-reuse-degradation-20261014.md) 与
[消费审计](../supply-chain/mira-input-reuse-20261014.md)。

<a id="mira-20261014-006"></a>

## MIRA-20261014-006：ConversationLoop 工具历史回放无宿主可插入的压缩缝，Layer 0 无法接入

- 状态：Resolved；日期：2026-10-14；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[PR#96](https://github.com/Linductor-alkaid/mira/pull/96)，经授权 squash
  合并为 `93e2eae6ceb6d2d0ce99e41601fcaf7ed0d91e04`（合并前 24 项 checks 全绿）；
  Mirage 已随 pin 消费并复验。关联
  [DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
  能力 2/3 与 [实施计划](../plans/context-compaction-implementation.md) Phase 1 的
  Layer 0 集成项。
- 核对版本：pinned `9e7b729d0861577b470d45385bdd08301bd1226d`（PR#95 合并提交），
  锁文件一致。公开 `include/mira/conversation_loop.hpp` 的全部扩展面只有
  `set_tool_registry` / `set_model_input_supplier` / `set_model_input_identity` /
  `run`，无任何历史变换或压缩入口。`src/model/conversation_loop.cpp` 的 `run()`
  以局部 `std::vector<ModelInputItem> history` 整轮累积 assistant 轮与
  ToolCall/ToolResult 对，`build_request` 将其原样 insert 进每个请求（"Canonical
  tool round-trip history rides after the request block"）；回放对宿主不可见。
- 可复现证据：`third_party/mira/docs/api/model-agent-loop.md` 明确供给内容"只进入
  当轮请求、不进入 tool-history 回放"（DEC-053 冻结契约），宿主既读不到也改不了
  回放；`docs/api/context-memory.md` 的 `StandardContextManager::prepare` 要求调用方
  提交全部候选 `ContextItem`，因此只对"持有全部内容的一方"可用——该方是循环而非宿主。
- API 选型/配置/应用职责排除：`ModelInputSupplyOptions` 限额只约束当轮追加内容；
  调小每轮输出预算或轮数不改变输入累积；DEC-087 preflight 只缩输出预算。Layer 0
  组件本身无缺口（水位、引用替换、审计、确定性装配均已交付），缺的是把它接入
  ConversationLoop 回放的缝；Mirage 不得自建平行压缩（仓库纪律），故登记本条。
- 影响：DEC-088 能力 2/3（工具历史压缩 + 桌面水位特化）无法按修订版路径落地；
  15+ 轮图片密集任务输入线性累积直至超窗（现状只能被动续写或显式失败）；
  `DroppedByWatermark` / `SelectedByReference` 审计与确定性 selection digest 在
  Mirage 不可用。
- 最小期望能力：ConversationLoop 新增可选的宿主历史压缩缝——每轮装配时把循环持有
  的规范化 history 交宿主提供的变换（宿主内部路由 Layer 0 `prepare`），以压缩结果
  替代原始回放；无缝时请求逐字节不变；压缩结果经既有事件面可见（复用
  `ModelInputSupplied` 或等价事件 + 结果计数）；回调异常按 DEC-053 纪律降级为可见
  事件、取消安全，不阻塞循环。
- 缝设计约束（9e7b729 渲染器核对）：`src/context/context_manager.cpp` 的
  `role_for()` 只把 `ContextItemKind::ToolCall` 渲染为 `ModelRole::Assistant`，
  其余 kind 一律渲染为 User——Layer 0 的装配目标是从结构化 ContextItem 组装请求，
  `PreparedModelContext.input` 虽与循环回放同型，但 assistant 文本轮没有保真的
  ContextItemKind，history 整体往返会把模型自身回复变形为 User 内容。因此缝的
  候选形态有二：(a) 上游扩展契约（角色保真的会话 kind 或 ContextItem 显式 role）；
  (b) 缝只覆盖工具往返段（ToolCall/ToolResult 对 + 大载荷），assistant 文本保持
  循环原样回放。需上游按其契约纪律定夺，Mirage 不在集成层绕开。
- 延期影响：DEC-088 Phase 1 的 Layer 0 集成与桌面水位两项保持未验收；超窗路径维持
  DEC-087 现状；不阻塞能力 1/5（PR#95 已合并、Mirage 已消费）。
- 临时措施：无——Mirage 侧不建平行压缩、不缓存回放副本，等待上游缝。
- 可验收结果：mira 侧测试覆盖无缝字节不变、缝内压缩生效且审计可见、回调异常降级、
  取消与关闭安全；Mirage 侧 `DesktopContextBuilder`（Goal pinned / CurrentObservation
  / ToolHistory P5 + replaceable_by_reference、桌面水位 0.45/0.70/0.90）接入后，
  15 轮任务 input tokens 相比基线下降可测且任务成功率不降。未运行验证的负责人：
  Mirage 维护者；补跑条件：上游缝 PR 合并、授权消费、真实任务回归。

### 2026-10-14 解决与消费

缝形态采用登记的候选 (b)（通用 history 变换，不改 Layer 0 契约），规避了
`role_for` 角色保真约束。PR#96 经授权合并（mira Debug 108/108、全部门禁、
24 项 checks 绿），Mirage pin 随授权消费（gitlink + lock 同步）。Mirage 侧
`DesktopContextBuilder` 只把工具往返段映射 Layer 0（consumed、
replaceable_by_reference=false 防引用标记破坏工具协议、配对键 provider_call_id），
桌面水位 0.45/0.70/0.90，checkpoint 拆对按 audit 整对收缩，assistant 文本透传；
`model_layer.cpp` 在声明窗口时接线（per-call 输出为 reserved，整 run 包络会在
一切现实窗口触发 limits 拒绝——独立验证发现并已修复），`DialogCompletion`
透传 history_transforms / history_transform_degradations。独立验证
（两轮）：1205 项断言，覆盖透传逐字节一致、压缩真实触发（15 轮 capture、
4096 窗口，早期配对整对消失且配对合法）、余量窗口全量保留、四类降级、
取消安全（上游 m3）；Mirage Debug 82/82、ASAN 无报告。真实会话 token 验收项
（-30%~40%）待真实 provider 环境补跑；登记时的精度缺口
[MIRA-20261014-007](#mira-20261014-007) 已由上游 PR#97 解决并消费（见其解决
与消费小节），水位精度恢复。见
[消费审计](../supply-chain/mira-history-compaction-20261014.md) 与
[消费验证](../compatibility/context-history-compaction-20261014.md)。

<a id="mira-20261014-007"></a>

## MIRA-20261014-007：ConservativeTokenCounter 不计工具载荷字节，工具密集请求水位系统性低估

- 状态：Resolved；日期：2026-10-14；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[PR#97](https://github.com/Linductor-alkaid/mira/pull/97)，已授权合并
  （squash `7391df70036cbdb035dea5ebaec99c46cbabe5ae`，合并前 24 项 checks 全绿）。
  关联
  [DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
  能力 2/3（[MIRA-20261014-006](#mira-20261014-006) 缝的消费验证发现）。
- 核对版本：pinned `93e2eae6ceb6d2d0ce99e41601fcaf7ed0d91e04`。
  `src/context/context_manager.cpp` 的 `ConservativeTokenCounter::estimate_item`
  只累计 TextPart/ImagePart/FilePart 的字节；`ToolCallPart.arguments`（JSON 参数）
  与 `ToolResultPart.result`（工具回执 JSON，Mirage 桌面 OCR 回执可达 16KiB 级）
  完全不计——单 part 工具项恒为 per-part + per-item 固定开销（实测 20 token），
  与载荷大小无关。
- 可复现证据：独立验证探针——400 字节 payload 的 ToolResult 与空 result 估算同为
  20 token；24 对工具往返（含大回执）合计仅 1152 token。消费验证报告
  （[历史压缩消费验证](../compatibility/context-history-compaction-20261014.md)）
  附完整用例。`ITokenCounter` 契约要求 upper bound "safe"（失败可降级但绝不记零/，
  低估破坏该约定）。
- API 选型/应用职责排除：未开启压缩/水位无关；Mirage 无法在 `ContextItem.content`
  侧镜像载荷来绕过——把工具回执复制成 TextPart 会双份进入模型请求（Layer 0 渲染
  原样透传 content），反而放大真实成本； DEC-087 的 provider 锚点是整请求级的，
  不能替分项水位估算。
- 影响：DEC-088 桌面水位的触发点系统性偏晚——工具密集请求的真实输入被低估一个
  数量级级，trim/checkpoint 在真实超窗压力到来之前不触发；Layer 0 的
  `ContextBudgetReport` / selection digest 与真实 wire 用量脱节。
- 最小期望能力：`ConservativeTokenCounter::estimate_item` 把 ToolCallPart 与
  ToolResultPart 的序列化字节（arguments / result 的 JSON 文本）纳入保守上界
  （沿用 bytes_per_token 密度与既有校准机制），并补跨载荷规模的估算测试；
  `estimate_assembly_overhead` 语义不变。
- 延期影响：能力 2/3 的水位精度打折（压缩仍正确——配对收缩语义不受影响，只是
  触发偏晚）；DEC-087 preflight（provider 锚点 + 整请求估计）不受影响，仍守住
  线上不超窗。真实会话 token 验收项在缺口修复前不具精度意义。
- 临时措施：无——Mirage 不自建第二套计数器（仓库纪律）。
- 可验收结果：mira 侧估算测试覆盖空/小/大载荷（ToolCallPart.arguments 与
  ToolResultPart.result 序列化字节线性进入 upper bound）；Mirage 消费后
  [消费验证](../compatibility/context-history-compaction-20261014.md) 的压缩触发
  用例改为按真实载荷规模工作。未运行验证的负责人：Mirage 维护者；补跑条件：
  上游修复 PR 合并、授权消费、真实会话回归。

### 2026-10-14 解决与消费

上游 PR#97 把 `ConservativeTokenCounter::estimate_item` 的计数范围扩展到全部
回放 part 的 wire 载荷字节（沿用 bytes_per_token=4 密度与既有 margin 校准）：
`ToolCallPart` 计 provider_call_id + wire_name + `arguments` 序列化字节；
`ToolResultPart` 按 tool bridge 的 wire 形态计（inline result 计序列化 JSON、
failed 只计 `safe_error_summary`、`large_payload` 只计工件引用——存储字节不上
wire 不计入）；`ThinkingPart` 计 text + signature（同一缺陷类，一并修复）。
`estimate_assembly_overhead` 语义不变。mira Debug 全量 108/108（新增 3 个
估算用例：arguments 载荷线性进入上界、result 三形态镜像 wire、thinking 线性
计入），format/docs/sbom/platform-boundary/architecture 门禁全过，PR#97
24 项 checks 绿后授权合并 `7391df7`，API 文档 `docs/api/context-memory.md`
同步计数器契约。Mirage pin 随授权消费（gitlink + lock 同步）：独立验证
PASS——新增永久回归测试 `large_tool_receipts_drive_checkpoint_compaction`
（5 对 8KiB 回执，新计数估算 10585 token 越过 checkpoint 水位 10035 必然
触发整对收缩、配对合法、回执逐字节保真；旧地板算术同夹具仅 240 token、
零触发的反证以真实 `StandardContextManager` 执行取证），二进制 1004 项
断言全过，Debug 82/82、ASAN 无报告。压缩触发自此按真实载荷规模工作
（约 44 倍于旧地板算术）。见
[消费验证](../compatibility/context-history-compaction-20261014.md) 补跑
章节与 [消费审计](../supply-chain/mira-history-compaction-20261014.md)。

<a id="mira-20261015-001"></a>

## MIRA-20261015-001：部分文本续写锚点按字节切取，多字节脚本下产生非法 UTF-8 feedback

- 状态：Resolved；日期：2026-10-15；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[PR#98](https://github.com/Linductor-alkaid/mira/pull/98)，已授权合并
  （squash `560da5ec7cab3ae4a0129c047b86ed57ae18501f`，合并前 24 项 checks 全绿）。
  关联 [DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
  能力 4（真实会话验收发现）。
- 核对版本：pinned `7391df70036cbdb035dea5ebaec99c46cbabe5ae`。`conversation_loop.cpp`
  部分文本续写锚点按原始字节切取末 50 字节；中文等多字节脚本下切点落在码点内部，
  feedback 携带悬空 continuation 字节进入下一请求。
- 可复现证据：DEC-088 真实会话验收（deepseek-flash，600 字中文任务，max_output
  64/128/256/512 各档强制截断）中续写请求异常；离线模拟确认各档切出的锚点均以
  continuation 字节开头（非法 UTF-8）。
- 排除：`Result::value()` 契约、请求装配、dialect 编码均无涉；误用方为上游切片实现。
- 影响：续写场景（能力 4）在非 ASCII 输出下的反馈质量与请求合法性。
- 最小期望能力：切后丢弃 lead 字节之前的悬空 continuation 字节，锚点保持合法 UTF-8。
- 修复与消费：PR#98 在切片后丢弃至多 3 个悬空 continuation 字节；`m3_conversation_loop_test`
  新增 `multibyte_anchor_stays_valid_utf8`（CJK 长文本切点落于码点内，断言锚点首字节
  非 continuation、尾部码点完好、续写到达 Answered）。mira Debug 108/108、门禁全过、
  24 项 checks 绿后授权合并；Mirage pin 随授权消费（gitlink + lock 同步）。


2026-10-15：MIRA-20261015-002 上游 PR #99 已授权合并，Mirage pin 与锁文件
升级至 a29374a；验收及许可证审计见
[持续循环 pin 升级](../supply-chain/mira-continuous-loop-20261015.md)。本次明确保留
Finite 产品行为，M9-03 仍需接线，不能把 pin 前滚等同于原长任务问题修复。


2026-10-15：a29374a 消费验收完成：干净升级工作树 Debug 82/82，当前工作区
84/84，ASAN/UBSAN 各 5/5；消费侧链接新库的持续循环契约与完整 native 产品
构建通过。MIRA-20261015-002 标记 Resolved（公开能力范围），M9-02 完成；
产品持续策略与 16/32 替换仍由 M9-03 实施。升级交付为
[Mirage PR #97](https://github.com/Linductor-alkaid/mirage/pull/97)，审计记录含平台限制。


2026-10-17：Mirage 消费完成（M9-03）：harness 循环切换 Continuous，16/32 累计
上限、15/16 轮收尾与 22 倍包络删除，动作/视觉/历史容量清理落地；产品接线交付为
[Mirage PR #103](https://github.com/Linductor-alkaid/mirage/pull/103)。脚本验收
261 模型步/260 工具（无窗口与大窗口）、40 动作归档、字节截断 2MiB 精确生效；
debug 全量 91/91、asan 聚焦 9/9。原长任务问题（2026-10-15 09:56 会话）的
产品侧修复至此关闭。

<a id="mira-20261022-001"></a>

## MIRA-20261022-001：ConversationLoop 终态不携带输出截断结构化原因，事件面不可观测

- 状态：Resolved（2026-10-25，Linux pin 消费范围）；日期：2026-10-22；负责人：Mirage 维护者，处理方：Mira 上游。
  上游：[mira#100](https://github.com/Linductor-alkaid/mira/issues/100)（2026-10-22
  提报）。
  关联 [MIRA-20261009-002](#mira-20261009-002)、
  [MIRA-20261016-002](#mira-20261016-002)（DEC-085 系列）、
  [DEC-112](../decisions/DEC-112-output-limit-failure-category.md)。
- 核对版本：pinned `a29374a`（mira 0.1.0）。公开头文件核对：
  `include/mira/conversation_loop.hpp` 的 `ConversationLoopResult`（:72-105）带有
  `output_continuations` 计数（:96-98），但没有任何终态截断原因字段；
  `ConversationSettled` 事件（`src/model/conversation_loop.cpp:831-839`）只携带
  outcome/turns/total_turns/recoveries/tool_executions/turn_records_dropped，不含
  `incomplete_reason` 与 `output_continuations`。续写预算耗尽以
  `ConversationOutcome::Failed` + `safe_summary` 文本
  "output-limit continuation budget exhausted; …"（`conversation_loop.cpp:662`，
  上游 `m3_conversation_loop_test::continuation_budget_exhaustion_settles_output_limit`
  固化）结算。
- 现象与复现：宿主在 loop 结果上无法从公开结构区分"输出上限耗尽"与其他
  `Failed`（恢复耗尽、工具预算等）——唯一信号是 `safe_summary` 文本；
  仅订阅事件面（IEventStore）的消费者则完全无法感知截断语义。复现：以固定返回
  `Incomplete(MaxOutputTokens)` 的 provider 驱动 `ConversationLoop` 超过
  `max_output_continuations`，检查 `ConversationLoopResult` 公开字段与
  `ConversationSettled` 事件载荷。
- 应用职责排除：直连 `ModelGateway::infer` 可得
  `ModelResponse.incomplete_reason`，但 loop 消费者不经过该面；Mirage 已在自有
  边界内为 dialog/harness 两条路径补了结构化类别与 IPC `error_kind`
  （[DEC-112](../decisions/DEC-112-output-limit-failure-category.md)），属消费侧
  表达面，不能替代上游结构化事实源。续写提示文案/锚窗/次数不可注入是既有边界，
  不属本条诉求。
- 影响：Mirage 的 IPC `error_kind: "output_limit"` 在 harness 路径依赖
  `safe_summary` 文本锚点（临时方案，见下），pinned 升级若改 summary 文案会静默
  失配；事件订阅方对截断不可观测，与仓库"失败须转化为明确结果与事件"纪律相悖。
- 最小期望能力：`ConversationLoopResult` 增加终态截断结构化字段（如
  `std::optional<IncompleteReason> terminal_incomplete_reason` 或等价
  `output_limit_exhausted` 标记）；`ConversationSettled` 事件摘要增加
  `output_continuations`（或在发生截断时附 `incomplete_reason`）。
- 延期影响：Mirage 维持文本锚点临时方案——限定在 `integration/mira` 适配边界内
  （`model_layer.cpp` harness settle 处，含注释与移除条件），行为差异是该锚点对
  summary 文案敏感；事件面截断观测继续缺失。
- 可验收结果：mira 测试固化新字段语义（耗尽 settle 时字段为真/事件携带计数）；
  Mirage 消费后删除文本锚点、以结构化字段驱动 `error_kind` 并回归
  `harness_desktop_tools_test` 续写耗尽场景。未运行验证的负责人：Mirage 维护者；
  补跑条件：上游 PR 合并、授权升级 pin、复验。

## 2026-10-25：上游同步复核与消费

MIRA-20261022-001：上游 PR#108（`729c5e8`）增加结果字段
`terminal_incomplete_reason`、公开 `incomplete_reason_name()` 与终态事件的
`output_continuations`/可选 `incomplete_reason`；PR#109（`5c40aa2`）扩展为
供应商报告的终态不完整原因，并保证不完整文本不成为答案。授权同步 pin 到
`68a8bf17b30af246b722fbb548c033de82efe26e`，Mirage 改为精确匹配
Failed + MaxOutputTokens，删除 summary 锚点；其它原因不误报 output_limit。
上游 PR#110 同时修正 Responses/Messages 的过滤/拒绝终态。
[升级审计](../supply-chain/mira-upstream-sync-20261025.md)记录结果/事件、消费侧
回归和 sanitizer，替代本条原补跑条件；真实 provider/Windows 不在本地验收声明内。

复核未解决项：MIRA-20260922-001、20261008-002、20261012-002、20261013-001、
20261016-003/004 在 `a29374a..68a8bf1` 无对应接口或嵌套依赖变更；维持 Open，
编译告警抑制、熔断测试重建及 Result 使用纪律保留。不能因其他上游修复关闭。
