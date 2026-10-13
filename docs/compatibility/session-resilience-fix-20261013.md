# 会话韧性与工具失败处理修复验收证据（2026-10-13）

> 状态：Completed（Phase 1/2 全部落地 + 上游根治合入；真实 Wayland/Xorg 对照与 Portal
> 授权路径实测仍属 Phase 3 待办，见文末补跑条件）
> 决策：[DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md)
> 计划：[session-resilience-fix.md](../plans/session-resilience-fix.md)
> 依赖升级：[mira kairo 0.7.0 与 SSE 根治审计](../supply-chain/mira-kairo-070-20261013.md)

## 实现范围（彻底方案，无简化残留）

1. **上游根治**（mira `bb39738`，PR #91 / issue #90，CI 24/24 绿）：
   Messages SSE 解析器把 provider `error` 事件按文档化类型映射到 loop 恢复白名单
   （瞬时类 retryable，客户端错误类 fatal 且域码准确，未知类型 fail closed），终止后
   容忍 ping 与重复 terminal。Mirage 侧重试降为最后一层纵深防御。
2. **epoch 感知工具失败跟踪**（model_layer.cpp）：同工具同一环境 epoch 内 3 次非瞬时
   失败后阻止；新 epoch 重置 per-epoch 计数，block 跨 epoch 保留；分类码封闭词表
   （platform_limitation / permanent_failure / invalid_arguments / permission_denied /
   conditional_failure / transient_failure）。
3. **规范化回执 + 结构化字段**：被阻止调用返回 canonical `permanent_failure` 回执；
   `DialogCompletion.tool_failures` 携带 {tool, code, message(中文前缀+最后错误),
   count(当前 epoch 次数)}。
4. **平台能力如实声明**：`DesktopEnvironment::screen_capability()`（Full/PortalOnly/
   Unavailable）；Linux 以 `WAYLAND_DISPLAY` ∨ `XDG_SESSION_TYPE` 识别 Wayland（含
   XWayland），自动选择会话时 Portal 观察优先、X11 继续服务窗口/输入/剪贴板；显式
   display 保持纯 X11。X11 采集失败在能力非 Full 时映射为 `platform_not_supported`
   canonical 回执。
5. **工具注册能力前置**：Unavailable 时不注册截图类工具；goal 文本注入能力事实段
   （PortalOnly 的授权语义与"不要重复尝试被拒采集"指引；Unavailable 的禁用声明）。
6. **预算优雅降级**：供给接缝轮次计数；倒数第二轮/最终轮预算提示；最终轮关闭全部工具
   （`final_turn_tools_closed`）；整轮失败错误附 `（N 个模型步骤、M 次工具调用）
   （工具失败摘要：…）`。
7. **SSE 有界降级重试**（model_layer.cpp）：流协议违规且无工具副作用时，dialog 与
   harness 双路径各非流式重试一次（第一轮验证发现 harness 分支不可达的死代码缺陷，
   已修复：改为按 loop 的 Failed outcome 值结果判定）。
8. **`session.tool_error` IPC 事件**：协议 v1 加法扩展（golden 向量 v21，含正/负
   用例）；EventHub 直发；settle_dialog_turn 结算后逐类发布；native 前端事件驱动的
   runtime_notice + 失败工具卡按 canonical 前缀显示"环境限制/需要授权/已阻止/预算
   收尾/参数无效"并以 error 色呈现。

## 测试证据

（独立验证代理执行，本节为结论摘要；详细断言证据见验证报告）

### 第一轮（基线 + 缺陷修复后复跑）

- `harness_desktop_tools_test`：614 checks / 0 failures——阻止机制（3 次后阻止、
  handler 计数==3）、Transient 不阻止（5 次全执行）、失败摘要、预算降级
  （max_turns=4：请求 3 含还剩一轮提示、请求 4 含 FINAL 提示、第 4 轮工具被拒）、
  dialog SSE 重试（stream 标志 [true,false]、恰 2 次调用）、harness SSE 重试。
- `provider_contract_test`（446 checks）、`x11_backend_test`（271 checks，真实 Xvfb +
  环境标记 RAII）、`desktop_image_supply_test`（910 checks，canonical 回执映射）、
  `desktop_atom_toolset_test`、`post_action_verification_test`、`tray_runtime_test`、
  `portal_screen_fixture` 全部通过。

### 第二轮（彻底方案增量）

第二轮验证结论 PASS WITH RISKS；其探针暴露的边界缺陷 D1 已修复并复验全绿：

- `epoch_reset_scenario`：E1 内 2 次失败不阻止 → epoch 变更后计数清零（第 3、4 次
  整体失败仍触达 handler）→ 同 epoch 第 3 次失败阻止 → 再切 epoch 阻止保持；
  handler 派发恰 5 次。
- `tool_failure_record_scenario`：`tool_failures` 恰 1 条，code=platform_limitation、
  message 以"平台限制："开头、count==3。
- `capability_registration_scenario`：Unavailable 时工具清单不含 capture/displays/ocr
  且 goal 含禁用声明；PortalOnly 时 goal 含 portal_only 授权语义且 capture 在册
  （正控）；Full 时无能力事实段。
- `service_tool_error_scenario`：service 级恰好发布 1 条 `session.tool_error`，seq 在
  权威 `session.chat_updated` 之后，字段与失败匹配。
- `resilience_scenario("budget_counts")`：失败错误含 `（4 个模型步骤、4 次工具调用）
  （工具失败摘要：…连续失败 3 次（已阻止后续调用）…）`。
- 回归：8/9 套件 PASS（golden 1552 checks、provider_contract 446、event_subscription
  446、runtime_service 594 等）；ASAN 复跑无任何 AddressSanitizer 报告。
- **D1 缺陷与修复**：跨 epoch 陈旧失败条目以 `failure_count=0` 发布，被协议自身解码
  契约拒绝。修复：`failures()` 对 epoch 计数取下限 1（`model_layer.cpp`，表示"该类
  在当前 epoch 前已发生"），头文件语义注释同步。修复后 `harness_desktop_tools_test`
  693 checks / 0 failures（含 `tool_failure_stale_count_probe`），golden/runtime 套件
  6/6 复跑通过。

### 上游验证（mira）

- `tests/m3/m3_anthropic_test.cpp` 新增 `provider_error_events_and_post_terminal_tolerance`：
  5 类可重试错误映射、4 类 fatal 映射、未知类型 fail closed、终止后 ping/重复 terminal
  容忍、终止后垃圾仍违规、流中 error 事件分类。
- mira 全量 ctest 105/105（build-debug 与 build-release，GCC 13.3.0 本地）。
- 上游 CI 24/24（linux gcc/clang、windows、android、ASAN/UBSAN/TSAN、quality）。

## 真实 Wayland 手动验证与后续失败诊断（16:22-16:24 会话，16:12 构建）

维护者在真实 Wayland 会话（XDG_SESSION_TYPE=wayland、XWayland、DBUS 可用）手动重跑
bilibili 任务：

**修复生效的证据**：
- `desktop.screen.capture` 两次 **complete**（portal 授权路径出帧，模型真实读到图像，
  supplied_input 5 条、0 丢弃）——上午「截图必然 io_error → 反复重试 → 预算耗尽」的
  故障链已消除。
- 失败摘要机制生效：错误带「（6 个模型步骤、6 次工具调用）」与分类码。

**新瓶颈（与本修复无关的既有预算约束，现被前置障碍掩盖后暴露）**：
- 错误 `model output remained incomplete after recovery budget`。链路：DeepSeek
  （anthropic 兼容端点）+ 自适应 thinking + 多模态桌面上下文 → 输出（含 thinking）
  超过每轮 8192 token 地板 → Messages 解析器按 MIRA-20261009-002 容忍为
  `Incomplete`（stop_reason=max_tokens）→ 既有恢复反馈「请直接以文本回答」无法让
  模型变短 → 两次恢复同样截断 → 恢复预算耗尽整轮失败。
- 直接 API 探测证实 DeepSeek 接受 `max_tokens=16384` 且正常完成（end_turn）——
  服务端无硬上限，约束在我方每轮预算。

**修复（三轮）**：
1. Mirage：thinking 调用的 generation 地板 8192 → 16384，Messages profile 预算
   同比放大（RULE-08 包络改为声明性支出估计）；
2. Mirage：`portal_timeout`（授权框超时/取消）从 Permanent `platform_limitation`
   细化为 Conditional `conditional_failure`——用户仍可授权下一次请求；
3. 上游 mira PR #92：截断（Incomplete）场景的恢复反馈差异化（"仅用简洁纯文本回答，
   不调工具不长思考"），使紧预算下的重试有真实收敛机会。

**维护者评审与完整收口（第四轮）**：维护者指出 16384 只是缓解（截断在 8192 只证明
需要更多预算），并给出 ZCode 的两层参照与 DeepSeek 的解耦事实。诊断复核确认更深一层的
约束：DeepSeek（非 M3 名单模型）不走 17x profile 分支，实际 wire 上限是
`config.max_output_tokens`（2048）与 `budget/2` 的较小值——现场截断的真实约束是
**2048**。收口修复：

4. **预算模型重构**：每轮输出 = generation_budget（thinking 地板 16384，声明
   context window 时按窗口收缩）与 profile 准入上限取小（不再对半砍）；轮数 16 /
   恢复 2 / 续写 2 独立声明；config 上限 65536；思考强度与输出预算解耦。
5. **上游续写策略**（mira PR #93，merge `1befa45`，CI 全绿）：`max_output_continuations`
   （默认 2）——部分文本保留续写、工具调用截断丢弃不执行、仅思考要求简短文本、耗尽
   后显式输出上限 settle。三类形态各有场景测试。
6. **MIRA-20261013-001**：`Result<T>::value()` 对 error 态的 UB 解引用记入依赖台账
   （排查中把测试侧非法 JSON 变成无符号栈段错误的根源；建议上游加显式防线）。
7. **测试基础设施**：ConversationLoop 测试的共享 fixture（scripted providers、
   gateway fixture、请求/事件助手）拆至 `tests/support/conversation_loop_support.hpp`
   （1200 行架构预算），主文件与续写场景共用。

**第三轮增量验证（PASS）**：
- `reasoning_scenario`（扩展）：adaptive/"" → wire 16384 + thinking Adaptive；
  none → 512 直传 + Disabled；profile 包络 278528（17×16384）四种 reasoning 一致。
- `thinking_budget_scenario`：地板语义（config=8192 + adaptive → 仍 16384）；端到端
  14 轮包络（第 13 请求含还剩一轮提示、第 14 请求含 FINAL 提示，13 次工具调用后
  Answered 收敛）。
- `portal_timeout_scenario`（生产 capture 路径注入）：portal 变体 →
  `conditional_failure` + `暂不可用：portal_timeout:` 前缀 + 同 epoch 3 次阻止保持；
  io_error 对照 → 仍 `platform_limitation`。
- 回归：harness 764 checks / 0（native-debug + ASAN + UBSAN），provider_contract
  446/0，image supply 910，golden 1552/0；mira（PR #92 后含截断修复的 pin
  `5791d2d`）重建后关键套件 9/9。
- TSAN 本机不可运行（内核 ASLR 与 TSAN shadow 映射冲突，先于本次改动存在；涉面已由
  ASAN+UBSAN 覆盖；补跑条件 `vm.mmap_rnd_bits=28`）。

**环境发现（非 Mirage 代码问题，备案）**：
- 16:43 探针期间 `xdg-desktop-portal-gnome` 段错误崩溃（SEGV core dump，journal
  16:43:22），随后自动重启；崩溃前有多次 `InteractiveScreenshot didn't return a
  file`（无人应答的授权请求）。疑似与并发/无人应答的截图请求相关，建议向 GNOME
  上游报告。服务自身的采集（16:23）期间 portal 健康。
- GNOME 的 portal 授权对话框不可被 AT-SPI 自动化点击（安全设计）；computer-use
  在本执行环境不可用。授权点击需真人完成。

### 真实 Wayland 已验证 / 未验证边界（更新）

- 已验证（真实会话）：环境判定（生产形态 → PortalOnly + portal 路径，含
  XDG_SESSION_TYPE 缺失的故障会话形态）、portal 授权与采集出帧（维护者手动点击）、
  图像供给入模、失败分类与摘要、预算计数。
- 未验证：拒绝→恢复路径的完整自动化（需交互点击）、真实 Xorg 会话对照、Windows。

## 补跑条件（环境限制，未执行）

- Xorg 会话对照实验（重跑 2026-10-11 bilibili 任务）：需真实 Xorg 登录会话。
- Portal 授权路径实测（拒绝→授权→重试）：需真实 Wayland 桌面会话与用户交互。
- Windows 平台验证：需 Windows 构建环境。
- 真实 provider 流的 SSE 错误复现：依赖 provider 侧故障复现窗口；解析器形态已被
  上游测试锁定。
