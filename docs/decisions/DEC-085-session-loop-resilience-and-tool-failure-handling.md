# DEC-085：会话循环韧性与工具失败处理优化

> 状态：Accepted（2026-10-13 维护者授权实施；实现记录见文末）
> 日期：2026-10-13
> 负责人：Mirage 维护者
> 工作项：待分配（关联 M7 桌面工具闭环与会话韧性）
> 前置：DEC-054（桌面动作契约）、DEC-068（可恢复工具拒绝）、MIRA-20261012-001（网络重试）

## 问题陈述

根据最近会话诊断（2026-10-11 bilibili 任务失败、2026-10-12 DeepSeek 网络超时），
Mirage 暴露两类韧性缺陷：

### 问题 1：会话恢复后的模型 SSE 流初始化失败

**现象**：用户在程序退出重启后恢复历史会话继续对话时，出现模型 SSE 流错误，导致
会话无法继续。具体错误形式待确认（当前 session-state.json 为空会话，无实际案例）。

**根因假设**（待验证）：
1. 会话恢复路径未正确重建 ModelLayer 的流式订阅状态
2. 持久化的会话上下文与 Mira ConversationLoop 的运行身份不一致
3. SSE 预览回调在恢复会话时指向已失效的 UI 订阅者

**影响**：用户无法在重启后继续使用历史会话，体验断裂。

### 问题 2：工具执行失败导致会话直接终止

**现象**：2026-10-11 会话（gui-session-failure-20261011.md）显示：
- 16 个模型步骤、17 个工具调用，7 个调用失败（2 个应用启动、5 个截图）
- 最终以 `turn budget exhausted before a final answer` 失败终止
- 截图失败为 Wayland 环境下 X11 根窗口不可采集（`io_error`，XGetImage BadMatch）
- 模型在同类确定性平台失败下反复 wait → 截图 → 失败，耗尽预算

**根因分析**：
1. **工具失败反复调用**：确定性平台限制（Wayland XGetImage 失败）未被识别为
   不可恢复失败，模型在相同观察/epoch 下反复请求同类工具，无新信息输入
2. **预算耗尽即终止**：`max_turns=16`（model_layer.cpp:676）硬限制触发后，会话
   以笼统"预算耗尽"失败，未保留已派发动作证据或给出可操作失败原因
3. **平台能力误报**：LinuxDesktopEnvironment 以 X11 连接成功判定 screen/input
   Provider 存在，未区分 Wayland 下 XWayland 的局部能力与完整 X11
4. **不确定副作用重发**：`application.open_uri` 成功派发后返回
   `application_running_only`，但 Firefox 窗口未出现在观察中；模型用旧 epoch
   观察重新启动，被拒绝为 `stale_observation`，再次启动得到 `already_running`——
   确定性拒绝未阻止无效重试

**影响**：
- 确定性平台限制下会话必然耗尽预算失败，用户无法完成任务
- 失败原因不可操作（"预算耗尽"不告知是截图不可用还是其他原因）
- 工具执行失败被模型层吞掉，未向用户呈现"当前环境不支持截图"等明确信息

### 关联问题：M7 阶段的修复是否生效

**M7-14 已验收内容**（bundled-vision-and-tool-recovery-20261011.md）：
- DEC-068：512 字节 canonical 回执、拒绝→新观察→已验证纠正
- 工具拒绝恢复：无副作用工具（如截图权限拒绝）可在新观察后重试

**最新会话未体现修复的原因**：
1. **Wayland 能力判定不完整**：DEC-073/074（M7-10）的原生 Wayland Portal 观察
   在 2026-10-11 会话时未完成，环境仍误报为完整 X11 能力
2. **确定性失败未归类**：XGetImage BadMatch 是平台限制，不是瞬时权限拒绝；
   M7-14 的"拒绝→重试"语义不覆盖此类永久不可用场景
3. **预算内无恢复空间**：`max_turns=16` 在图像化任务中已是紧预算；7 次工具失败
   + 3 次 wait（13 秒）+ 反复观察耗尽所有步骤，无空间给模型调整策略

**MIRA-20261012-001 修复范围**：
- 上游 PR#88 已合并，网络阶段超时标记为 `retryable=true`
- Mirage 侧 `max_recoveries` 按信封阈值启用：turn_capacity ≥ 10 时为 2
- **仅覆盖网络层瞬时故障**，不覆盖工具执行失败或平台能力缺失

## 决策

### 1. 会话恢复 SSE 流初始化（高优先级，阻塞用户使用）

**目标**：确保恢复会话后的首次模型调用能正确建立流式订阅，不因重启导致流错误。

**方案**（待技术验证后定案）：

#### 方案 A：显式重建 ModelLayer 流式上下文
- 会话恢复时检测 ModelLayer 的 `impl_->running` 状态和流式订阅有效性
- 若会话持久化时启用了流式预览，恢复时重新注册 UI 订阅回调
- 在首次 `complete_harness_turn` 前验证 `preview` sink 可达性

**优点**：
- 直接修复根因，恢复会话与新会话行为一致
- 不改变 Mira 循环语义，仅在 Mirage 适配层处理

**缺点**：
- 需要在会话持久化中记录流式订阅状态（或恢复时统一启用）
- 增加 Runtime Service 会话恢复路径复杂度

#### 方案 B：恢复会话首轮降级为非流式
- 会话恢复后首次调用强制 `config.inference.stream = false`
- 在首轮成功完成后，后续轮次恢复流式（若配置启用）
- 用户首轮看到"等待完整响应"，后续轮次恢复增量显示

**优点**：
- 实现简单，规避流式订阅重建风险
- 首轮成功后自动恢复正常体验

**缺点**：
- 首轮体验降级（无打字效果）
- 未真正修复根因，只是规避

**推荐**：先实施方案 B（快速修复），同步调查方案 A（根本解决）。

### 2. 工具失败处理与会话韧性（高优先级，影响任务成功率）

**目标**：
1. 确定性工具失败不导致无效重试循环
2. 预算耗尽前给出可操作的失败原因
3. 工具执行错误对用户可见，不被循环吞掉

**方案 2.1：工具失败分类与恢复策略**

引入三级工具失败分类（扩展 DEC-068）：

**Level 1 - 瞬时可恢复**（已有 DEC-068 支持）：
- 权限弹窗待授权、截图授权拒绝（用户可重新授权）
- 临时资源不可用（窗口焦点丢失、短暂遮挡）
- **策略**：返回明确回执，模型在新观察后可重试

**Level 2 - 条件可恢复**（新增）：
- 平台能力缺失但有替代路径（Wayland 截图失败 → Portal 授权路径）
- 目标不存在但可通过其他动作创建（窗口未启动 → 需先启动应用）
- **策略**：返回 512B canonical 回执 + 恢复条件说明；若 3 轮内条件未满足，
  升级为 Level 3

**Level 3 - 确定性不可恢复**（新增）：
- 平台永久限制（Wayland 无 X11 根窗口、Windows UIPI 阻止低权限注入）
- 工具参数非法（无效坐标、不支持的字符集）
- 环境缺失（无屏幕 Provider、OCR 模型未加载）
- **策略**：立即向模型返回明确失败 + 建议（"当前环境不支持屏幕截图，请使用
  应用窗口截图"），并在 UI 呈现工具错误卡片；**同类工具在同 epoch 下最多失败
  3 次后，阻止进一步调用**

**实施边界**：
- `desktop_action_verification.cpp` 在动作执行前检查平台能力，返回分级回执
- `model_layer.cpp` 维护每 epoch 的工具失败计数器，Level 3 失败 ≥3 次后，
  同类工具调用返回 `permanent_failure`（不消耗 turn 预算）
- Runtime Service 在收到 Level 3 失败时，向 UI 推送工具错误事件（新增 IPC 消息）

**方案 2.2：预算耗尽前的优雅降级**

当 `remaining_turns <= 2` 且尚未生成用户可见回复时：
1. 模型层在 ConversationLoop 结果中附加 `budget_warning` 标志
2. Runtime Service 检测到该标志后，向模型发送系统提示：
   "You have 2 turns remaining. Please summarize progress and explain what 
   prevented task completion, rather than retrying failed actions."
3. 最后一轮强制 `tools_allowed = false`，确保模型必须生成文本回复

**实施**：
- `model_layer.cpp` 在 `complete_harness_turn` 返回前检查 turn 计数
- Runtime Service 在 turn 结果中检查 `budget_warning`，注入系统提示后继续循环
- 最终失败时，completion.error 包含已执行动作摘要（"attempted 3 screenshots, 
  all failed due to platform limitation"）

**方案 2.3：平台能力如实声明**

修复 `LinuxDesktopEnvironment` 的能力误报（关联 M7-10/14）：
1. **Wayland 环境判定**：检测 `WAYLAND_DISPLAY` + `XDG_SESSION_TYPE`，
   X11 连接成功但在 Wayland 会话中，screen Provider 降级为"仅 Portal"
2. **能力细化**：Screen Provider 返回 `capability_level`：
   - `full`：X11 完整 framebuffer（Xorg 会话）
   - `portal_only`：需用户授权的 Portal（Wayland）
   - `unavailable`：无屏幕能力
3. **工具注册时声明前置**：`desktop_ocr_tool.cpp` 等工具在注册时检查
   `capability_level`，`portal_only` 时工具描述标注"需要授权"

**实施**：
- `platform/linux/src/linux_environment.cpp` 增加 Wayland 检测
- `desktop/environment` 接口增加 `get_screen_capability()` 方法
- 工具注册时（`model_layer.cpp` 装配阶段）根据能力调整工具可用性

### 3. M7 修复生效性验证（中优先级）

**目标**：确认 M7-14 的工具拒绝恢复在实际会话中生效，找出未覆盖的失败模式。

**行动**：
1. 在 Xorg 会话中重跑 2026-10-11 失败任务（bilibili 视频查找），验证
   完整 X11 能力下是否成功
2. 在 Wayland 会话中触发 Portal 截图授权，验证"拒绝→授权→重试"路径
3. 构造 Level 2/3 失败场景的回归测试（无权限、平台限制、参数非法）
4. 确认 `max_recoveries` 在图像任务中的实际消耗（网络重试 vs 工具重试）

## 验收标准

### 会话恢复 SSE
1. 创建会话 A，发送消息 M1，收到流式回复 R1
2. 退出 Mirage，恢复会话 A，发送消息 M2
3. **验收**：M2 的回复 R2 正常流式显示，无 SSE 错误

### 工具失败分类
1. Wayland 会话中请求全屏截图（Level 3 失败）
2. **验收**：
   - 第 1 次失败返回"平台不支持，请使用窗口截图"
   - 第 2-3 次失败返回相同提示
   - 第 4 次调用被阻止，模型收到"permanent_failure，已尝试 3 次"
   - UI 显示工具错误卡片（红色，带"环境限制"标签）

### 预算优雅降级
1. 构造 14 轮无效工具调用的会话（剩余 2 轮）
2. **验收**：
   - 第 15 轮模型收到预算警告系统提示
   - 第 16 轮禁用工具，模型生成总结文本
   - 最终 completion.error 包含"attempted X actions, failed due to Y"

### 平台能力声明
1. Xorg 会话：`get_screen_capability()` 返回 `full`，全屏截图工具可用
2. Wayland 会话：返回 `portal_only`，工具描述含"需要授权"
3. 无屏幕环境：返回 `unavailable`，截图工具不注册

## 实施计划

### Phase 1：紧急修复（1-2 天）
- [ ] 实施方案 B（恢复会话首轮禁用流式）
- [ ] 平台能力判定修复（Wayland 检测）
- [ ] 工具失败 UI 错误卡片（让用户看到失败原因）

### Phase 2：工具韧性（3-5 天）
- [ ] 工具失败三级分类实现
- [ ] 同类工具失败计数器与阻止逻辑
- [ ] 512B canonical 回执扩展（Level 2/3 说明）
- [ ] 预算优雅降级（系统提示注入）

### Phase 3：根本解决（5-7 天）
- [ ] 方案 A 调查与实施（SSE 流重建）
- [ ] 会话恢复回归测试（SSE、工具状态、UI 订阅）
- [ ] M7-14 修复生效性验证（Xorg/Wayland 对照）
- [ ] 更新 DEC-068 工具拒绝分类文档

### Phase 4：长期优化（后续）
- [ ] 工具执行遥测（失败率、分类分布、恢复成功率）
- [ ] 自适应预算分配（图像任务 +4 turns，文本任务 -2 turns）
- [ ] 用户可配置的"任务失败时总是给出总结"选项

## 依赖与风险

**依赖**：
- Mira ConversationLoop 的错误分类扩展能力（当前仅区分 Cancelled/Failed/Incomplete）
- Runtime Service IPC 协议扩展（工具错误事件推送）
- UI 层工具错误卡片渲染（Native/Web 双前端）

**风险**：
- 工具失败计数器状态管理复杂度（epoch 变化、会话恢复、并发任务）
- 预算警告可能被模型忽略（prompt injection 或模型不理解系统提示）
- Wayland Portal 授权流程用户体验断裂（需要跳出会话窗口授权）

**替代方案**：
- 若工具失败分类实施复杂度过高，先实施"同工具连续失败 3 次 → 禁用该工具"
  的简化版本
- 若 SSE 流重建无法在 Phase 3 完成，长期保持"恢复会话首轮非流式"降级方案

## 参考

- [gui-session-failure-20261011.md](../compatibility/gui-session-failure-20261011.md)：
  2026-10-11 bilibili 任务失败诊断
- [model-network-retry-20261012.md](../compatibility/model-network-retry-20261012.md)：
  MIRA-20261012-001 网络重试修复
- [session-failure-final-report-20261013.md](../compatibility/session-failure-final-report-20261013.md)：
  2026-10-13 实际会话证据最终分析（修正本文问题 1 的定性）
- [session-resilience-fix.md](../plans/session-resilience-fix.md)：实施计划
- [DEC-068](DEC-068-bundled-vision-and-recoverable-tool-rejection.md)：
  可恢复工具拒绝（M7-14）
- [DEC-054](DEC-054-scoped-complete-desktop-actions.md)：桌面动作契约
- [M7 计划](../plans/m7-visual-tools-and-input.md)：视觉工具与键鼠输入闭环

## 实施记录（2026-10-13，依最终报告修正后落地）

### 问题 1 定性修正

2026-10-13 实际会话证据（session 5，Turn 6）证明 SSE 错误
（`invalid Messages SSE lifecycle or budget`）**不是重启恢复会话问题**，而是运行中的
单请求内流协议违规：pinned Messages SSE 解析器状态按请求新建，跨轮无残留；连续预算
失败后出现该错误的触发点是 provider 流本身。因此"方案 B（恢复首轮降级）"保留为可选
的恢复体验缓解，不再是主修复。

**落地修复（integration/mira/src/model_layer.cpp）**：当流式调用以该协议违规失败、且
本 harness 轮尚无任何工具执行（无已派发副作用）时，有界降级为非流式重试一次；纯对话
路径无副作用，直接重试一次。不再继续失败的流，也不做跨轮状态重置（无根因可重置）。

### 问题 2 落地（彻底方案，2026-10-13 第二轮实施更新）

- **上游根治先行**：pinned `AnthropicMessagesSseParser` 的两类过严判定已在 mira 仓库
  内修复（[issue #90](https://github.com/Linductor-alkaid/mira/issues/90)、
  [PR #91](https://github.com/Linductor-alkaid/mira/pull/91)，merge `bb39738`，上游
  CI 24/24 绿）：provider `error` 事件按文档化类型映射到 loop 恢复白名单（瞬时类
  retryable 原地恢复，客户端错误 fatal 且域码准确，未知类型 fail closed）；message_stop
  之后的 ping 与重复 terminal 容忍。Mirage 的有界非流式重试保留为最后一层。
- **三级分类 + epoch 感知阻止**（model_layer.cpp 工具装饰器拦截）：
  `classify_tool_failure` 给出 {级别, 封闭错误码}；同工具**同一环境 epoch** 内 3 次
  非瞬时失败后阻止后续调用（canonical `permanent_failure` 回执，含失败次数与最后
  错误）；新观察（epoch 前进）重置 per-epoch 计数，block 跨 epoch 保留——完整实现
  本决策的 Level 1/2/3 语义，不再使用简化版本。
- **平台能力如实声明 + 注册前置**：`DesktopEnvironment::screen_capability()`
  （Full/PortalOnly/Unavailable）；Linux 识别 Wayland（含 XWayland），自动选择会话时
  Portal 观察优先；Unavailable 时不注册截图类工具；goal 注入能力事实段（PortalOnly
  的授权语义、拒绝后不重复尝试；Unavailable 的禁用声明）。X11 采集失败在能力非 Full
  时映射为 `platform_not_supported` canonical 回执。
- **预算优雅降级**：供给接缝轮次计数注入提示、最终轮关闭全部工具、整轮失败错误附
  `（N 个模型步骤、M 次工具调用）（工具失败摘要：…）`。
- **工具错误对用户可见（专用事件 + 卡片）**：`session.tool_error` IPC 事件（协议 v1
  加法扩展，golden 向量 v21）；settle_dialog_turn 逐类发布；native 前端 runtime_notice
  + 失败工具卡按 canonical 前缀显示"环境限制/需要授权/已阻止/预算收尾/参数无效"并
  以 error 色呈现。

### 不做的与延后的

- pinned ConversationLoop 内部的"剩余轮数感知"未改动（上游契约不变）；降级完全在
  Mirage 装配层实现，上游仅按 PR #91 扩展了错误分类与终止后容忍。
- Xorg/Wayland 对照实验与 Portal 授权路径实测需真实桌面会话，见实施计划 Phase 3 验证
  项，环境限制未执行项按工程规范记录补跑条件
  （[验收证据](../compatibility/session-resilience-fix-20261013.md)）。

## 真实验证与后续修复（2026-10-13 16:xx，维护者手动任务后）

维护者在真实 Wayland 会话手动重跑任务：**采集与授权路径证实已修复**（两次 portal 截图
complete、图像入模、失败分类与摘要生效），暴露出一个与本决策并行的既有预算约束并当场
修复：

1. **输出预算地板**：DeepSeek + 自适应 thinking + 多模态桌面任务的实际输出（含
   thinking 通道）超过每轮 8192 地板，被按 MIRA-20261009-002 容忍为 Incomplete，
   既有恢复反馈无法让模型变短，恢复预算耗尽整轮失败。修复：thinking 调用的
   generation 地板提升至 16384，Messages profile 预算同比放大（RULE-08 包络下
   max_turns 维持 14）。直接 API 探测证实 DeepSeek 接受 16384（end_turn，无硬上限）。
2. **portal_timeout 分类细化**：授权框超时/取消从 Permanent `platform_limitation`
   细化为 Conditional `conditional_failure`——用户稍后仍可授权。
3. **上游恢复反馈差异化**（mira PR #92）：截断（Incomplete）场景的恢复反馈改为
   "仅用简洁纯文本回答"，替代无法成立的"请直接以文本回答"，使紧预算重试有真实收敛
   机会。
4. **环境发现（备案，非 Mirage 代码）**：`xdg-desktop-portal-gnome` 在无人应答的
   截图请求期间段错误崩溃（journal 16:43:22 SEGV，自动重启）；GNOME portal 授权框
   不可被 AT-SPI 自动化点击。建议向 GNOME 上游报告；授权点击由维护者人工完成。

## 完整修复（2026-10-13 晚，按维护者"不计代价收口"指令）

维护者评审指出 16384 地板只是缓解（截断证明需要更多预算，不证明 16384 就够），并给出
ZCode 的两层处理作为参照（能力声明预算 + 剩余上下文收缩 + 截断识别 + 有界续写）与
DeepSeek 的解耦事实（Anthropic 兼容端点忽略 `thinking.budget_tokens`）。据此收口：

1. **预算模型重构（Mirage）**：每轮输出预算 = `generation_budget`（thinking 地板
   16384；声明 context window 时按窗口收缩）与 **profile 准入上限取小，不再人为对半
   砍**——旧包络下 DeepSeek（非 M3 名单模型）实际 wire 上限只有 2048，这才是现场
   截断的真正约束。轮数 16 / 恢复 2 / 续写 2 改为独立声明；config 上限放宽至 65536；
   思考强度与输出预算解耦（wire 只带模式标志，从不发送 thinking token 预算）。
2. **续写策略（上游 mira PR #93，merge `1befa45`，CI 24/24）**：ConversationLoop 新增
   `max_output_continuations`（默认 2，独立于恢复预算），对 `MaxOutputTokens` 截断
   按三类形态处理——部分文本保留为 assistant 轮并要求"从截断处继续"；工具调用截断
   丢弃且不执行（Incomplete 响应结构性不产生 proposals）并明确告知；仅思考截断要求
   简短文本。耗尽后以 `output-limit continuation budget exhausted` settle。通用恢复
   块保留对未上报 reason 截断的截断感知反馈。
3. **排查备案（MIRA-20261013-001）**：`Result<T>::value()` 对 error 态的 UB 解引用
   （本次排查中把测试侧非法 JSON 变成无符号栈段错误的根源）记入依赖反馈台账，建议
   上游为 `value()`/`error()` 加显式防线。
