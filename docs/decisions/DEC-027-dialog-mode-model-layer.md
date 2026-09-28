# DEC-027：M5 对话模式真实化（模型层装配与 session.chat 对话面）

> 状态：Accepted
> 日期：2026-09-28
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-06` 第四增量：对话模式真实化）
> 替代/被替代：无；本记录是 [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的**附加扩展**（传输载体、帧格式、载荷上限、单连接单未决请求纪律与一致性模型
> 全部不变），兑现 [DEC-025](DEC-025-session-page-productization.md) 挂账③（对
> 话模式真实化，DEC-008 迁移路径第二步的纯对话形态），并完成
> [DEC-026](DEC-026-observation-face-and-definition-read.md) 挂账④（驱动器步
> 级观察事件）的联动评估。wire 契约 `meta.version` 7 → 8（golden vectors 双端
> 门禁同一文件）

## 背景与问题

`M5-06` 第三增量（session.close，PR #59）后，工作项唯一剩余范围是对话模式真
实化：Composer 的对话入口自 DEC-025 第一轮起如实降级（"模型循环尚未接入"）。
动工前接口缝复核确认的结构性事实：

1. **pinned 模型栈已交付但未被 Mirage 接触**：`ModelProfile`（固定端点 /
   方言 / SecretRef 凭据引用 / 能力证据）/ `ModelRouter` / `ModelGateway`
   （预算、重试、熔断）/ `OpenAiCompatibleProvider` + `SocketHttpTransport`
   （POSIX + Winsock 便携适配器，Executor blocking worker 承载）与
   OpenSSL/Mbed TLS 通道适配器全部就绪；宿主装配路径由 pinned M14/M15 评估
   夹具示范。
2. **纯对话 ≠ 桌面 AgentLoop**：pinned `AgentLoop` 是
   Observe→Reason→Plan→Act→Verify 的桌面循环（必需环境与验证器）；纯对话
   形态是 Text 输出契约的单轮 ModelRequest → ModelGateway 推理 → 助手文
   本，无工具、无桌面观察。DEC-008 第 2 条迁移路径的"宿主侧驱动循环"在纯
   对话形态下即宿主侧装配的模型调用。
3. **pinned 会话投影无助手词条**：`build_conversation_view` 只投影
   `UserMessage` / `LoopOutcome`（会话历史 `user` / `outcome` 两类）——助手
   回复没有 pinned 投影承载，把它伪装成 user/outcome 条目是语义伪造。
4. **生产 provider 的 SSRF 姿态**：pinned provider 硬编码
   `allow_private_endpoints = false`——回环/私网端点被拒是上游安全设计，
   意味着对话面在 Mirage 的测试与开发拓扑中必须以注入式 provider 承载
   （脚本化响应），真实端点连通性属部署态取证。
5. **凭据安全（DoR §5.1）**：SecretRef 只在传输边界解析（pinned 纪律），
   明文不进事件、不进摘要、不进日志；Mirage 需要定一个具体的解析源。

## 决策

1. **模型层装配（宿主侧，pinned-free 公共面）**：`integration/mira::ModelLayer`
   按 `ModelLayerConfig`（`ServiceConfig::model` 镜像：dialect / endpoint_origin
   / api_prefix / model_selector / credential_env / request_deadline /
   max_output_tokens / max_input_bytes）装配 pinned `ModelProfile` +
   `ModelRouter` + `OpenAiCompatibleProvider` + `SocketHttpTransport` +
   `ModelGateway`。TLS 通道：pinned OpenSSL 适配器目标存在时挂接；不存在时
   https 端点在传输边界 fail closed，永不降级为明文（pinned 纪律；Windows
   端 Mbed TLS 适配器维持 `MIRAGE_WITH_MIRA_MBEDTLS` 门控，默认关闭）。
   配置旗标（`--model-endpoint` 等）是本增量的配置面；设置-模型类目的持久
   化条目与 Profile 管理面属 `M5-08` 联动范围。配置缺省禁用：模型层未配置
   的服务 `session.chat` 回 `unavailable`，hello 不带 `chat` 能力位。
2. **对话面 = 异步 turn + 独立快照/事件**（协议 v1 附加扩展，golden
   v7 → v8）：请求 `session.chat`（session_id + text）受理即回执
   `DialogTurnAccepted {turn_id}`——模型推理是有界长任务（Executor 可取消
   任务承载，OperationContext 带 deadline 与停止探针），不占用单连接未决请
   求等待；新事件 `session.chat_updated`（pending → ok / failed 生命周期，
   `reply_text` / `error` encode-when-set 恰在 ok / failed）；请求
   `session.chat.history`（每会话有界 turn 日志快照，pending/ok/failed 封
   闭词表，成对可选成员校验）。hello 新增 `chat` 能力位（模型层已配置即
   true，`permissions` 位的装备依赖先例）。一致性模型沿用 DEC-012：快照是
   事实源，事件是通知。
3. **对话线程的承载边界**：turn 日志是服务内存易失状态（workflow 注册表
   同款纪律，持久化挂 DEC-011 条目 / M5-08），**不进 pinned 会话投影**——
   `build_conversation_view` 无助手词条，伪造 user/outcome 携带助手文本会
   破坏 DEC-021 的历史语义；session.history（脚本任务对话投影）零变更。
   UI 把对话轮渲染进同一时间线（用户行 + 助手行），数据面与脚本任务对话
   分子面并存。每会话同时至多一个在途 turn（`invalid_state`），文本预算
   16 KiB（`invalid_argument`）；会话关闭时其对话线程随之移除。
4. **SecretRef 解析源 = 进程环境变量**：`credential_env` 命名环境变量，
   `EnvSecretResolver` 在传输边界（SocketHttpTransport 持有的 resolver）
   读取；未设置变量 fail closed（PermissionDenied），凭据明文不离开传输、
   不进事件/摘要/日志（pinned SecretRef 纪律）。环境变量是 M5 增量内的最
   小可信源；系统 keyring 等更强承载随 M5-08 / 打包轮评估。
5. **测试承载 = 注入式 provider（ModelProviderOverride）**：生产 provider
   的 SSRF 姿态（决策 4 的上游事实）使回环 canned-server 端到端不可达；
   `ServiceConfig::model_provider_override` 以 Mirage 自有的不透明载体
   （integration 层类型，pinned 类型不穿越公共头）注入脚本化
   `IModelProvider`——与 `confirmation_hub` 注入同款缝。被注入时跳过
   socket 栈装配，ModelRequest 装配、gateway 预算/路由与服务侧对话流程全
   部真实执行。真实端点连通性属部署态取证（维护者机器 / CI 出网能力），
   本轮不宣称已验证。
6. **DEC-026 挂账④联动评估结论（维持挂账）**：纯对话形态无桌面驱动步、
   无步级可观察事件流；驱动器步级观察事件的触发条件仍是"桌面模型循环
   （Act 步）进入任务路径"，即 DEC-008 第二步的后续阶段。纯对话形态的轮
   级生命周期已由 `session.chat_updated` 承载，不再叠加步级事件。
7. **M5-08 联动评估结论**：本增量交付服务级配置旗标 + hello 能力位 +
   注入缝；设置-模型类目（Profile 管理面、凭据承载升级、持久化）在 M5-08
   以既有 `ModelLayerConfig` 面为契约输入。

## 备选方案

- **复用 pinned 桌面 `AgentLoop` 跑纯对话**：否决。该循环必需环境观察与
  验证器，纯对话没有桌面语义；强行跑会伪造 Observation 或退化为空转。
  Text 契约的单轮 gateway 推理才是"纯对话形态"的忠实承载。
- **助手回复写入 pinned 会话投影（伪装 user/outcome）**：否决。投影词条
  是 pinned 设计（对话语义封闭）；以 outcome 句式携带助手文本会让
  session.history 说出从未发生的任务结算。
- **同步等待推理完成的 session.chat 回执**：否决。推理是有界长任务（秒到
  分钟级），阻塞单连接未决请求违背有界工作单元纪律（DEC-007）；异步
  turn + 事件收敛才是产品形态，也天然支持后续多轮/流式扩展。
- **本地 canned HTTP 服务器做端到端测试**：否决（尝试后放弃）。pinned
  provider 的 SSRF 姿态使回环地址永不被拨号——这是上游安全设计而非配置
  缺口；以注入式 provider 承载测试（决策 5）同时避免伪造安全边界。
- **凭据存配置文件明文**：否决。环境变量是进程级注入、不落盘；文件承载
  引入新的敏感信息面，留给 M5-08 与打包轮的 keyring/secret 服务评估。

## 影响与风险

- `runtime/ipc` 协议面新增两请求两载荷一事件一能力位；既有解码路径不改，
  既有测试零回归；golden vectors `meta.version` 7 → 8，双端门禁同步。
- `runtime/service` 新增两个处理器、DialogRegistry（容量 = max_sessions，
  每线程 turn 上限 200、最旧裁剪 + truncated 显式）与模型层生命周期（start
  装配 fail closed、teardown 在 Executor 关停前收敛传输——pinned 关闭顺序）。
- **真实模型连通性未被 CI 覆盖**：CI 矩阵以注入 provider 取证对话流程；
  真实端点（公网 https + TLS + 凭据）连通性属部署态取证，不在本 PR 声明。
- **Windows**：`Mira::net_transport` 为 POSIX + Winsock 便携适配器（MinGW/
  MSVC 同源）；https 在 Windows 默认构建 fail closed（Mbed TLS 适配器
  `MIRAGE_WITH_MIRA_MBEDTLS` 门控，DEC-017 双工具链下行为一致）。
- TS 镜像（types / codec / 三传输 / mock `chatCapability`）与 golden 消费
  映射同步；mock 以确定性模拟回复承载对话面（mock 文档角色既定）。

## 验证方式

- 协议测试（`tests/runtime`）：两请求两载荷一事件编解码 round-trip、可选
  成员 encode-when-set 与成对校验失败路径、状态词表拒绝、hello `chat`
  能力位（有 / 无两形态）。
- Golden vectors 双端门禁：v8 向量（requests +2、request_failures +4、
  responses +3 含 hello 能力位、response_failures +4、events +2、
  event_failures +2）双端消费同一文件，逐字节一致。
- 服务测试：`session_chat_dialog_face`（注入 provider）——hello 能力位、
  unknown `not_found`、turn 受理 → 收敛 ok → 回执文本、in-flight 闩锁清
  除、第二轮受理；无模型层拓扑 `session.chat` → `unavailable`。
- 前端：mock 对话面生命周期（pending → ok）、`chatCapability=false` 降级、
  store 对话线程收敛与稳定错误呈现；`npm run check` / `npm test` /
  `npm run lint` / `npm run build` 全绿。

## 挂账

1. **真实端点连通性取证**：公网 https + 真实凭据的端到端对话（TLS 通道、
   计费、限流）属部署态验证，随打包轮 / 维护者机器取证。
2. **凭据承载升级**：环境变量之外的系统 keyring / secret 服务承载随
   M5-08 / M5-11 评估。
3. **对话流式输出**：InferOptions.stream + SSE 分块沿
   `session.chat_updated` 增量投递，随真实端点取证后评估。
4. **多轮上下文策略**：当前对话线程渲染最近 20 轮为转录块；上下文裁剪/
   摘要策略随 pinned context 面接入评估。

## 关联文档和工作项

- [DEC-025](DEC-025-session-page-productization.md)（挂账③兑现）、
  [DEC-026](DEC-026-observation-face-and-definition-read.md)（挂账④联动评
  估结论）、[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
  （迁移路径第二步）、[DEC-021](DEC-021-session-message-contract-face.md)
  （会话投影边界）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
  （扩展流程与一致性模型）。
- pinned 依据：`model_profile.hpp` / `model_gateway.hpp` / `model_provider.hpp`
  / `model_transport.hpp`（IHttpTransport / ISecretResolver / TLS 通道）、
  `agent_loop.hpp`（ModelRequest 装配先例）、`adapters/net/`（socket +
  OpenSSL 适配器）；`docs/api/` 模型面文档。
- 工作项：`M5-06` 第四增量（本决策）；消费方：Composer 对话模式、
  M5-08 设置-模型类目（`ModelLayerConfig` 为契约输入）。
