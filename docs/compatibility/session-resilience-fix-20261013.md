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

## 补跑条件（环境限制，未执行）

- Xorg 会话对照实验（重跑 2026-10-11 bilibili 任务）：需真实 Xorg 登录会话。
- Portal 授权路径实测（拒绝→授权→重试）：需真实 Wayland 桌面会话与用户交互。
- Windows 平台验证：需 Windows 构建环境。
- 真实 provider 流的 SSE 错误复现：依赖 provider 侧故障复现窗口；解析器形态已被
  上游测试锁定。
