# 依赖问题反馈台账

> 状态：Active
> 维护规则：[工程规范第 9.4 节](../project/project-standards.md)
> 更新日期：2026-09-27

本台账登记 Mirage 对两个直接 pinned 依赖 —— `third_party/mira` 与
`third_party/mirador` —— 的能力缺口反馈与获批例外。编号按依赖区分：
`MIRA-YYYYMMDD-NNN`、`MIRADOR-YYYYMMDD-NNN`。"依赖不支持"不构成有效记录，条目必须附
可复现证据、影响范围、期望语义与可验收结果。

executor（`third_party/mira/third_party/executor`）由 mira pin 并交付，属于 mira 的
集成面：Mirage 发现的 executor 层缺口以 `MIRA-*` 条目登记，由 Mira 上游经其自身的
executor 反馈流程消化；Mirage 不直接向 executor 反馈，也不在台账中单列 executor 条目。

| 编号 | 日期 | 主题 | 分级 | 状态 | 关联实现/测试 |
| --- | --- | --- | --- | --- | --- |
| MIRA-20260922-001 | 2026-09-22 | executor 的 MinGW-w64 交叉构建失败（`std::thread::native_handle_type` 假设 win32 线程模型） | build 缺口（不阻塞 MSVC 主工具链） | Open | M4-02 交叉构建预检；阻塞 M4-06 的全树 MinGW 交叉门禁 |
| MIRA-20260927-001 | 2026-09-27 | 同内容草稿记录遮蔽可运行版本（`resolve_workflow_version` 按内容 digest 取首条匹配） | 语义缺口（不阻塞 M5-05 第一轮；编辑器自然流受影响） | Open | DEC-023 决策 2；mira_host_test workflow surface 场景；M5-05 第二轮编辑器接线前须复核 |

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
  工具回填语义。不修改第三方代码、不冒充已反馈远端 issue。

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
  未向远端提交issue。测试原始证书在临时目录，仓库只记录非敏感诊断。
