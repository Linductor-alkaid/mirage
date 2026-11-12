# Mira 上游同步审计（2026-11-11）

> 状态：Completed（Linux 消费范围；未验平台见"验证"节）
> 日期：2026-11-11
> 负责人：Mirage 维护者
> 授权：维护者要求"更新 mira pin，以及根据 mira 更新修复的部分以及更新的功能，
> 看看 mirage 需要的对应的调整"（范围 A：pin 同步 + 必要接线 + 评估报告）。
> 工作项：[mira-upstream-sync-20261111](../plans/mira-upstream-sync-20261111.md)

## 锁定与来源

旧 pin `68a8bf17b30af246b722fbb548c033de82efe26e`（2026-10-25 同步）→ 新 pin
`be44e36044eaf8e5ab09bc0a22f134a3c8545111`（origin/master tip；无指向该提交的
release tag，沿用 master tip 锁定惯例）。升级区间包含上游 PR#111（issue 跟踪）、
#112～#127 共 21 个提交；全部变更经 gitlink 消费，无依赖源码补丁。
嵌套 kairo pin `32027c2`（v0.7.0）→ `10e870bcb94b109a1a1f32eab8ef0d3d9f01055b`
（v0.7.4 release tag，由上游 PR#127/DEC-110 收敛，中间经 PR#112/115 的 master
pin 轮次）。工作分支 `deps/mira-upstream-sync-20261111` 基于当时 master
`faa72b3`（工作树干净，无并行未提交改动）。

## 消费面兼容性结论

对 Mirage 全部 mira 消费点逐一核对（Explore 全仓扫描 + 关键头文件 diff）：

| 消费点 | 结论 |
| --- | --- |
| `ConversationLoop` 配置与 `ConversationLoopResult` 字段（outcome、`terminal_incomplete_reason`、supplied_input/history_transform 计数器、safe_summary、answer、last_usage） | 完全兼容；新字段全部纯增量（`max_pending_user_messages`、`system_prompt`、`set_tool_selector`、`set_continuation_source`、`enqueue_user_message`） |
| `MiraRuntime`（mira_host 唯一宿主；`submit_task(TaskSpec{goal})`、`request_shutdown`→`finish_shutdown` 单次序、workflow 面） | 兼容。`TaskSpec::user_constraints` 为新增可缺省字段；`finish_shutdown` 新语义（最多一次 shutdown admission retry、重复调用幂等）与 mira_host 现有"单次 request_shutdown → 等待 → 单次 finish_shutdown、Failed 终态不复生"序列吻合；`RuntimeState` 增加 `Failed→Stopping` 转移（DEC-080）无影响——Mirage 不消费 `RuntimeState`，自有 `HostStatus` 投影不变 |
| `ModelProfile`（model_layer.cpp `build_profile` 唯一构造点） | 兼容；新增 `decoding`/`extra_body` 缺省为空，`validate()` 通过。`allow_private_endpoint` 见下节接线 |
| `ModelRouter` / `RouteQuery` | Mirage 不构造 RouteQuery（单 profile）；`required_tools` 缺省 0 无影响 |
| `AgentLoop` | Mirage 不实例化（统一走 ConversationLoop，pinned PR#76 起）；其新增 `set_action_dispatcher` 强制缝不触及 |
| `action_journal` / `task_checkpoint` / `task_recovery` | 0 消费；PR#114 的 journaled execution 加固在上游 runtime 内部，经 MiraRuntime 命令面间接受益，无 Mirage 代码迁移 |
| `MemoryEventStore` / `workflow` 面 / `AgentLoopSpec` / `digest_*` 工具 | 兼容 |
| kairo v0.7.0→v0.7.4 | 零代码迁移（与上游 DEC-110 暴露审计一致：Mirage 无 `submit_task_result`/`TaskResult`/`CpuGpuTask`/显式 `cpu_affinity` 使用）。行为变化一项：空 `cpu_affinity` 默认 OS 自由调度（正面，big.LITTLE 主机受益）；v0.7.3 deadline-drop 竞态修复顺带获得 |

Wire 层行为变化（无代码迁移、回归覆盖）：Chat Completions 纯文本 parts 以单换行
连接（DEC-103，goal/注入指令/供给内容的 part 边界在 wire 上可读）；ConversationLoop
每轮发出 `AssistantSpeech` 事件（DEC-104）——Mirage 的会话重建/捕获路径不解析该
事件类型，上游对未识别类型跳过、对已识别类型 fail closed，无干扰。

## 本轮接线：用户配置模型 endpoint 的私有/回环同意

上游 PR#118 给 `ModelProfile` 增加 host 同意缝 `allow_private_endpoint`：传输层
SSRF 防线（回环/私有/链路本地/元数据地址）此前一律拒绝，宿主无产品化 opt-in
路径（仅测试可设 `TransportLimits`）。Mirage 自初始版本即接受用户配置
`http://` 自定义 endpoint（`model_layer.cpp` origin 校验），因此本地/内网模型
服务（Ollama、LM Studio 等 `http://127.0.0.1:*` 类）一直不可达——这是本轮确认
的真实功能缺口，非本轮引入的回归。

接线（`integration/mira/src/model_layer.cpp` `build_profile`，ModelProfile 唯一
构造点）：`record.allow_private_endpoint = true`。依据上游契约"host-only consent，
never derived from model output"：Mirage 的 endpoint origin 全部来自用户本机
LocalSettings（经 IPC 校验后进入 ModelLayerConfig），不存在模型/请求派生路径；
且上游 `limits_from_profile` 将 `allowed_hosts` 钉死为配置的 endpoint host
（每一跳含重定向），同意仅解锁用户显式键入的地址族，不扩大可达面。代理同意
（`ModelProxyConfig::allow_private_endpoint`）Mirage 未配置代理，不涉及。

## 变化与采用总表

| 上游交付 | Mirage 消费 |
| --- | --- |
| PR#118：`ModelProfile::allow_private_endpoint` host 同意缝 | **本轮接线**（见上节）；补行为探针测试 |
| PR#112/113 / DEC-068/069：统一 Interaction Coordinator（M28 冻结 + 主体级输入入口）、有界容量分层模型 | pin 提供；MIRA-20261027-001 转 Accepted（部分交付）；Mirage 未接线，后续评估（见"后续工作项"） |
| PR#124-126：memory agent、偏好提案/批准持久化、交互生命周期与原生角色召回 | pin 提供；M10 产品记忆接线前的公开面确认，本轮不启用 |
| PR#121/122：托管 subagent 委派、自主发现并执行已验证工具 | pin 提供；Mirage 未接线 |
| PR#119/116 / DEC-097/105：per-request 工具选择、profile 解码默认值与 extra_body | pin 提供；列为后续工作项 |
| PR#120 / DEC-098：host-owned 会话 system prompt | pin 提供；Mirage 纯对话路径现有手工 system item 继续工作，迁移列为后续工作项 |
| PR#121 附带 / DEC-099：`set_continuation_source` | pin 提供；列为后续工作项 |
| M28 §4.4 / DEC-068：`ConversationLoop::enqueue_user_message` 有界用户消息队列 | pin 提供；运行中插话注入列为后续工作项 |
| PR#123 / DEC-103/104：Chat 文本分部分隔、AssistantSpeech 事件 | wire 层直接受益（可读 part 边界）；事件供后续会话视图消费 |
| PR#117：`HttpRequest::keep_alive`（header 敏感 SSE endpoint） | pin 提供；Mirage 未设置（缺省 close，行为不变） |
| PR#114：journaled execution/recovery/host 集成加固 | 上游 runtime 内部；经命令面间接受益，0 迁移 |
| PR#115/127 / DEC-110：kairo v0.7.3/v0.7.4 | 嵌套 pin 升级；零代码迁移；空 `cpu_affinity` 默认 OS 自由调度 |
| PR#112 附带 / DEC-101：gateway 完整请求 Layer 0 准入 + `set_context_supplier` | **默认路径无条件执行**（初判"opt-in"经复验推翻）；本轮完成 Mirage 预算装配对齐（见下节）并修复三个 fixture 回归 |

## 网关上下文准入的预算装配对齐

**初判更正**：静态头文件 diff 显示 `set_context_supplier`/`admit_model_request`
为可选缝，但复验（IVA 全量回归 3 个确定性失败 + 网关源码核对）证明新网关在
`infer()` 默认路径对每个匹配 profile **无条件**调用 `admit_model_request`——
完整请求（pinned 输入 + 全部工具 schema + framing）按
`window − reserved_output − 1024` 准入，计数器缺省 bytes/token=1。上游自身
测试依赖 `ProfileLimits` 缺省（window 128000 / output 16384）自然满足；
Mirage 的装配在三点上结构性冲突：

1. profile 输出下限 `max(config, 32768)` 可以等于甚至超过声明窗口（fixture
   32768 窗口 + 32768 输出下限 → 准入输入预算为 0）；
2. 压缩 transformer（DEC-088 DesktopContextBuilder）把历史整形到
   `window − per_call_output − 1024` 的水位，未给工具 schema 留准入份额；
3. 既有预算体系以 pinned byte estimator（bytes/3）表述，准入缺省计数器
   bytes/token=1 过密 4 倍。

本轮对齐（全部在 Mirage 自研层，无依赖改动）：

- **校准计数器**：`ModelGatewayConfig.context_admission.counter` 注入
  `ConservativeTokenCounter` 缺省配置（bytes/4，与 transformer 的 PR#97 wire
  payload 语义同单位）——`ModelContextAdmissionConfig.counter` 即上游为此
  预留的校准缝。
- **profile 窗口一致性钳制**：声明窗口时 `max_output_tokens` 钳制到
  `window − 9696`（1024 margin + 8192 准入预算下限 + 480 slack），下限 1024；
  纯对话与 IME planner 请求输出钳制到 profile 限额。
- **harness 逐轮预算**：goal 文本定稿后测量 pinned 前缀（`goal/4+24`）与全部
  暴露工具的 `estimate_tool` 之和 T，`per_call = min(baseline, profile,
  window − 1024 − slack − required_budget)`，其中
  `required_budget = max(8192, ceil(1.82×(prefix+500)) + ceil(2.64×T))`——
  系数推导：压缩后重放钉在 transformer 自身预算的 0.45 checkpoint 水位，而
  该预算还需容纳工具集（`0.55×budget ≥ prefix + 1.45×T + supply_overhead`）。
- **transformer 工具余量**：DesktopContextBuilder 的 reserved 输出改为
  `per_call + T`，使水位形历史与工具 schema 同时落入准入预算。

fixture 影响与更新：三个确定性回归场景（harness 紧窗口、native 对话/多轮、
ctx-builder 压缩）按新算术重推；preflight 场景套件的"usable==0/1 装饰器边界"
在完整请求准入下结构性不可达，改钉 cap 算术；compaction 场景窗口升至 32768
（4096/13312 无法同时容纳工具集与压缩水位），rounds 升至 100 保证水位穿越；
tiny-window 场景的"降级"旧契约被钳制消除，改为正向断言（transform 有效、
零降级）。生产含义：小窗口本地模型（如 8k）的可用输出预算收窄到准入可供给
的值——这是新准入契约的直接结果，fail-closed 语义保留。

## 台账边界

- MIRA-20261016-003（kairo 延期能力）**保持 Open**：v0.7.4 交付 keyed admission、
  `TaskResult<T>`/`submit_task_result`、valued `CpuGpuTask` 与空 `cpu_affinity`
  自由调度，但 QoS 标注与 AdaptiveScheduler 仍未交付；台账补上游进展备注。
- MIRA-20261027-001（统一输入编排）Proposed → **Accepted（部分交付）**：上游
  M28（PR#112/113，源自该条目引用的 mira#111）交付主体级输入接纳与最小路由
  闭环；M29/M30/M31 仅范围边界；Mirage 未接线。
- 其余 Open 条目（MIRA-20260922-001、20261008-002、20261012-002、20261013-001、
  20261016-004）本区间未触及，状态不变。

## 供应链与许可证

mira 仍为 AGPL-3.0；嵌套 kairo `10e870b`（v0.7.4 release tag）仍为 MIT；Mbed TLS
`068ff080` / framework `dde0c4a` 与 vendored SQLite 3.53.4（Public Domain）不变；
`git diff 68a8bf1..be44e36 -- .gitmodules LICENSE THIRD_PARTY_NOTICES` 在 mira
侧无许可证/子模块清单变化。Mirador `2a9f483`、EUI-NEO `19acab6` 锁定不变。
无新增依赖、模型权重或二进制下载。

## 验证

Linux x86_64 / GCC / Ninja；pin 校验开启。详细命令与结果见
[计划文档](../plans/mira-upstream-sync-20261111.md)验证记录（证据由
Independent-Verification-Agent 独立产出）。

未执行项与补跑条件：Windows/MSVC 与 MinGW、真实 provider 在线（含真实本地
endpoint 端到端连通）、native/Wayland 桌面验收——负责人 Mirage 维护者，分别在
目标 CI、provider 授权及桌面环境就绪后补跑。本轮 Mirage 侧无新增并发路径
（接线为 profile 字段赋值），未新增 TSAN 范围；kairo v0.7.4 的并发面证据沿用
上游 release 证据（上游 Debug 182/182）。

## 后续工作项（本轮评估结论，未接线）

- P2（低风险增量，建议后续轮次）：
  1. `set_tool_selector`（DEC-097）：Mirage 满配暴露 30+ 工具，贴近
     `max_tools_per_turn=32` 上限；按任务阶段/能力裁剪每请求工具集，降低
     prompt 噪声并留出余量。
  2. `system_prompt`（DEC-098）：纯对话路径 system item 从手工构造
     （model_layer.cpp `"mirage.dialog.system.v1"`）迁移到
     `ConversationLoopConfig::system_prompt`，获得内置校验与 provenance 固定。
  3. 解码默认值（DEC-105）：temperature/top_p/seed 用户可配置化（当前 Mirage
     不设置，全部依赖 provider 默认）。
  4. `enqueue_user_message`（M28 §4.4）：任务运行中用户插话注入（当前运行中
     输入仅有暂停/接管路径）。
- P3（需设计评估）：
  5. `set_continuation_source`（DEC-099）：owner 续接源。
  6. gateway `set_context_supplier`/`admit_model_request`（DEC-101）与现有
     `set_history_transformer` + DesktopContextBuilder 的职责关系。
  7. subagent 委派（PR#121/122）与 Mirage 任务面的关系。
  8. memory_agent / verified_fact / interaction_coordinator（M10+ 记忆与统一
     输入入口；MIRA-20261027-001 的 Mirage 接线设计）。
  9. `task_state_name`/`task_state_from_name`（DEC-079）与 Mirage 自有
     `project_task_state`/IPC 命名的去重——注意 IPC 用首字母大写风格，与 pinned
     小写风格不同，非简单替换。
