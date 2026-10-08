# Mira 反馈台账

> 状态：Active
> 负责人：Mirage维护者；处理方：Mira上游
> 更新日期：2026-10-08
> 工作项：[M6-10](../plans/m6-native-frontend.md)
> 总入口：[依赖反馈](ledger.md)；维护规则：[工程规范9.4](../project/project-standards.md)

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
| MIRA-20260922-001 | Executor MinGW POSIX native_handle转换 | 构建缺陷 | Open | 最小编译复现同类错误 | [#75](https://github.com/Linductor-alkaid/mira/issues/75)，已提交 |
| MIRA-20260927-001 | 同digest草稿遮蔽已验证workflow记录 | 版本语义缺陷 | Resolved | 公共版本API离线复现 | [#74](https://github.com/Linductor-alkaid/mira/issues/74)，已提交 |
| MIRA-20261004-001 | 无屏幕通用harness与规范工具回填入口 | P2：harness边界结构性缺口 | Resolved | 无屏幕环境在模型路由前失败；核对输入契约 | [#73](https://github.com/Linductor-alkaid/mira/issues/73)，已提交 |
| MIRA-20261004-002 | OpenSSL ClientHello缺少SNI | 传输缺陷 | Resolved | socketpair捕获真实adapter ClientHello，无server_name | [#72](https://github.com/Linductor-alkaid/mira/issues/72)，已提交 |
| MIRA-20261008-001 | Responses/Chat Completions方言丢弃思考输出 | P2：方言能力缺口 | Resolved | 双服务在线对照 + 升级后真机复验（chat-completions 1059字符思考可见，Messages回归正常） | [#83](https://github.com/Linductor-alkaid/mira/issues/83)，PR[#84](https://github.com/Linductor-alkaid/mira/pull/84)已合并 |
| MIRA-20261008-002 | core_contracts纳秒转换触发-Wuseless-cast | 构建摩擦 | Open | GCC/libstdc++全量消费TU复现useless_cast错误 | 未提交 |

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
- **状态/责任人**：Open；维护者与 Mira 上游。延期影响为保留此单一适配边界与受限
  工具回填语义。不修改第三方代码；上游提交回执见索引与跟进记录。

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
- **状态/负责人**：Open；维护者与Mira上游。延期影响为MiniMax无法验收；未修改third_party，
  上游提交回执见索引与跟进记录。测试原始证书在临时目录，仓库只记录非敏感诊断。

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

- 状态：Accepted；负责人：Mirage维护者；范围：M6-17/18、DEC-041。
- 核对：3716dbf的公开InferOptions/ProviderInferOptions无实时sink；take_last_preview仅在infer返回后可用；OpenAiCompatibleProvider流式入口仅Responses，ConversationLoop固定默认选项。
- 复现：配置Chat模型并启用stream被Provider拒绝；Responses回调只供内部parser消费，UI在终态前没有公开文本事件。已排除Mirage的显示/连接配置问题。
- 影响：无法真实增量输出；私自解析协议会重复Mira已有模型职责。
- 最小能力：有界非权威完整快照sink、两种协议SSE归约、ConversationLoop选项透传及最后usage。
- 语义：预览不得进入历史、触发工具或作为成功依据；重试清空、取消停止投递、终态覆盖；断流失败。
- 延期影响：保留等待状态直至最终回复，不模拟打字。2026-10-05维护者授权修复并要求上游PR。
- 验收：分片中文、工具参数、预算/终态拒绝、实时回调、异常隔离/取消和下游晚到预览门禁。
- 移除条件：同步上游修复提交与锁文件、完成集成验收；已提交[上游PR#79](https://github.com/Linductor-alkaid/mira/pull/79)，本地pin 0a099ba集成验证通过；上游尚未合并，保持Accepted。


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

- 状态：Resolved（Linux本地验收范围）；负责人：Mirage维护者；[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已创建未合并。
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

- 获批修复：Mira fbc644be2fabfaa2f8257e579fbc1d368537224c；[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81)，基于 Messages PR#80，尚未合并。Executor 与其他内嵌 pin 未变。
- 收敛：DEC-051 的 ThinkingMode / ThinkingPart、SSE 有界签名与 redacted 回填由 Mira 承载，Mirage 仅负责模型选项投影。7 项上游协议测试、Mirage 52 项 Release 回归与真实 MiniMax 开关/工具循环通过；[完整验收](../compatibility/session-thinking-20261007.md)。没有临时并发或协议实现。其他厂商在线/Windows 与当前 kairo 主干未复验，维护者取得目标环境后补跑。

<a id="mira-20261008-001"></a>

## MIRA-20261008-001：Responses / Chat Completions 方言丢弃思考输出

- 状态：Open；负责人：Linductor-alkaid；关联 Mirage M6-26 / DEC-046、MIRA-20261006-002。
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
