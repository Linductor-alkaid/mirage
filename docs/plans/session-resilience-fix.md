# 会话韧性与工具失败处理修复计划

> 状态：Phase 1/2 已实施（2026-10-13）；Phase 3 验证项待真实桌面会话
> 负责人：Mirage 维护者
> 关联决策：DEC-085
> 依据：gui-session-failure-20261011.md、model-network-retry-20261012.md、
> session-failure-final-report-20261013.md
> 更新日期：2026-10-13

## 实施状态（2026-10-13 第二轮，彻底方案全部落地）

**定性修正**：最终分析报告以实际会话证据证明，SSE 错误（`invalid Messages SSE
lifecycle or budget`）发生在**运行中**会话（session 5 Turn 6，无重启），是单请求内
流协议违规，不是恢复路径问题。原 Phase 1.1"恢复首轮降级"取消；根治在上游完成。

**已落地**（DEC-085 实施记录为权威描述，验收证据见
[session-resilience-fix-20261013](../compatibility/session-resilience-fix-20261013.md)）：

- **上游根治** ✅ mira PR#91（issue #90，CI 24/24）：Messages SSE 解析器 provider
  error 事件分类到 loop 恢复白名单 + 终止后良性尾随容忍；pin `bb39738`。
- Phase 1.1 → SSE 协议违规有界非流式重试（dialog/harness 双路径，第一轮验证修复了
  harness 分支不可达的死代码缺陷），作为上游恢复之后的最后一层。
- Phase 1.2 ✅ Wayland 判定（`WAYLAND_DISPLAY` ∨ `XDG_SESSION_TYPE`）、Portal 观察
  优先（X11 继续服务窗口/输入/剪贴板）、`screen_capability()`（Full/PortalOnly/
  Unavailable）；显式 display 保持纯 X11 语义。
- Phase 1.3 ✅（专用事件 + 卡片）`session.tool_error` IPC 事件（golden v21）+
  EventHub 直发 + runtime 结算推送 + native 前端 runtime_notice 与失败工具卡
  error 色/分类标签。
- Phase 2.1 ✅ 三级分类（None/Transient/Conditional/Permanent）+ 封闭错误码词表 +
  canonical 回执。
- Phase 2.2 ✅ epoch 感知阻止：同工具同 epoch 3 次非瞬时失败阻止，新 epoch 重置
  per-epoch 计数，block 跨 epoch 保留（完整语义，非简化版）。
- Phase 2.3 ✅ 预算优雅降级：轮次计数提示 + 最终轮关闭工具 + 失败错误附
  `（N 个模型步骤、M 次工具调用）（工具失败摘要：…）`。
- 方案 2.3 注册前置 ✅ Unavailable 不注册截图类工具；goal 能力事实段（PortalOnly
  授权语义、Unavailable 禁用声明）。

**未做/待验证**（Phase 3）：Xorg 对照实验、Portal 授权路径实测、Windows 平台验证；
环境限制未执行项按工程规范记录补跑条件（见验收证据文档）。

## 执行摘要

最近两次会话暴露了 Mirage 的两个关键韧性问题：

1. **会话恢复后的 SSE 流初始化失败**：用户重启后无法继续历史会话
2. **工具失败导致会话终止**：确定性平台限制（Wayland 截图不可用）导致工具反复
   失败，耗尽预算后会话以笼统错误终止，未给出可操作原因

本计划分三阶段修复：
- **Phase 1（紧急）**：恢复会话降级方案 + 平台能力判定 + 工具错误可见性
- **Phase 2（核心）**：工具失败三级分类 + 反复失败阻止 + 预算优雅降级
- **Phase 3（根本）**：SSE 流重建 + M7 修复验证 + 回归测试

## 问题分析

### 问题 1：会话恢复 SSE 流错误

**症状**：程序重启后恢复会话，首次对话出现模型 SSE 流错误。

**根因**（待验证）：
- 会话恢复路径未重建 ModelLayer 流式订阅
- 持久化上下文与运行身份不一致
- UI 订阅回调失效

**影响范围**：所有恢复的历史会话，首次对话失败率未知（需遥测）。

**当前缓解措施**：无（用户只能重新创建会话）。

### 问题 2：工具失败反复调用与会话终止

**症状**（2026-10-11 bilibili 任务）：
- 16 步预算中，7 个工具调用失败（5 个截图 `io_error`）
- 模型在同类失败下反复 wait → observe → screenshot → 失败
- 最终以 `turn budget exhausted before a final answer` 终止
- 用户看不到失败是因为"Wayland 环境不支持 X11 截图"

**根因**：
1. **平台能力误报**：`LinuxDesktopEnvironment` 在 Wayland 下误报完整 X11 能力
   - 代码位置：`platform/linux/src/linux_environment.cpp`
   - 检测逻辑：`XOpenDisplay()` 成功即认为 screen Provider 可用
   - 实际限制：Wayland 下 XGetImage 根窗口返回 BadMatch（error=8）

2. **工具失败未分类**：XGetImage 错误被笼统标记为 `io_error`，模型无法区分
   "暂时不可用"与"永久不可用"
   - 代码位置：`platform/linux/src/x11_backend.cpp:capture_region`
   - 返回路径：`desktop_image_supply.cpp` → `model_layer.cpp` 工具 handler
   - 模型视角：每次都是新的 `io_error`，无历史失败计数

3. **无效重试未阻止**：相同 epoch/scope 下的截图请求被允许重复执行
   - 代码位置：`desktop_action_verification.cpp`
   - 当前逻辑：仅检查权限和参数有效性，不检查历史失败
   - 结果：5 次截图请求全部执行并失败，消耗 5 个工具 quota

4. **预算耗尽即终止**：`max_turns=16` 触发后，循环立即返回 Incomplete
   - 代码位置：`integration/mira/src/model_layer.cpp:676`
   - 终止时机：第 16 次 turn 开始前，ConversationLoop 拒绝新请求
   - 返回内容：`completion.error = "model layer did not complete..."`，
     无已执行动作摘要或失败原因

**影响范围**：
- 所有 Wayland 桌面用户的截图任务必然失败
- 其他确定性工具失败（无权限应用、不支持的输入法）同样导致预算耗尽
- 失败原因对用户不透明，无法自助解决（如切换到 Xorg 会话）

**M7-14 修复未覆盖的原因**：
- DEC-068 的"可恢复工具拒绝"仅覆盖权限弹窗等瞬时失败
- Wayland 能力判定在 M7-10（DEC-073/074）范围，2026-10-11 时未完成
- `max_recoveries=2`（MIRA-20261012-001）仅用于网络层重试，不影响工具预算

## 修复方案

### Phase 1：紧急修复（目标 1-2 天）

#### 1.1 会话恢复首轮降级为非流式

**目标**：规避 SSE 流重建风险，确保恢复会话可用。

**实施**：
```cpp
// runtime/service/src/runtime_service.cpp
// 在恢复会话的首次 complete_harness_turn 调用中
bool is_restored_session_first_turn = /* 从会话状态判定 */;
if (is_restored_session_first_turn) {
    preview_sink = nullptr;  // 禁用流式预览
    // 在 UI 显示"加载中..."占位，完成后一次性显示
}
```

**验收**：
- 创建会话，发送消息 M1（流式显示）
- 退出 Mirage，重启，恢复会话
- 发送消息 M2，首轮非流式（整条显示），后续轮次恢复流式

**回退风险**：低（仅影响恢复会话首轮体验，不影响功能）

#### 1.2 Wayland 环境能力判定修复

**目标**：在 Wayland 会话中不误报完整 X11 屏幕能力。

**实施**：
```cpp
// platform/linux/src/linux_environment.cpp
bool detect_wayland_session() {
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    return (wayland_display && *wayland_display) ||
           (session_type && std::string(session_type) == "wayland");
}

// 在 LinuxDesktopEnvironment::initialize() 中
if (x11_display_ && detect_wayland_session()) {
    screen_capability_ = ScreenCapability::PortalOnly;
    // X11 连接仅用于窗口枚举，根窗口采集不可用
} else if (x11_display_) {
    screen_capability_ = ScreenCapability::Full;
} else {
    screen_capability_ = ScreenCapability::Unavailable;
}
```

**实施位置**：
- `platform/linux/include/mirage/platform/linux_environment.hpp`：增加
  `screen_capability_` 成员
- `desktop/environment/include/mirage/desktop/desktop_environment.hpp`：
  增加 `get_screen_capability()` 方法
- `integration/mira/src/desktop_image_supply.cpp`：检查能力后再调用底层采集

**验收**：
- Xorg 会话：`get_screen_capability()` 返回 `Full`，全屏截图成功
- Wayland 会话：返回 `PortalOnly`，全屏截图返回明确错误"需要授权"
- 无 DISPLAY：返回 `Unavailable`，截图工具不注册

**回退风险**：中（环境检测逻辑可能误判，需要多环境测试）

#### 1.3 工具错误 UI 可见性

**目标**：工具执行失败时，向用户显示明确错误，不被循环吞掉。

**实施**：
```cpp
// runtime/ipc/include/mirage/runtime/ipc/protocol.hpp
struct ToolErrorEvent {
    std::string session_id;
    std::string tool_name;
    std::string error_code;      // "platform_limitation" | "permission_denied" | ...
    std::string error_message;   // "当前环境不支持全屏截图，请使用窗口截图"
    int failure_count;           // 本 epoch 已失败次数
};

// runtime/service/src/runtime_service.cpp
// 在工具 handler 返回错误后
if (tool_result.failed && is_deterministic_failure(tool_result.error_code)) {
    ToolErrorEvent event;
    event.session_id = session_id;
    event.tool_name = tool_name;
    event.error_code = classify_error(tool_result.error_code);
    event.error_message = user_friendly_message(tool_result.error);
    event.failure_count = count_failures_in_epoch(session_id, tool_name);
    send_tool_error_to_ui(event);
}
```

**UI 呈现**（Native/Web 双前端）：
- 工具过程卡片下方显示红色错误条
- 图标：⚠️（警告）或 ⛔（永久限制）
- 文本：`截图失败（平台限制）：当前环境不支持全屏截图，请使用窗口截图`
- 点击展开：显示技术细节（错误码、失败次数、建议操作）

**验收**：
- Wayland 会话中请求全屏截图
- UI 显示工具错误卡片，文本为"平台限制：当前环境不支持..."
- 用户明确知道失败原因，不是"程序出错"

**回退风险**：低（仅增加 UI 显示，不影响功能）

### Phase 2：核心韧性（目标 3-5 天）

#### 2.1 工具失败三级分类

**目标**：区分瞬时、条件、永久失败，给出不同恢复策略。

**分类定义**：

```cpp
// desktop/environment/include/mirage/desktop/tool_failure.hpp
enum class ToolFailureLevel {
    Transient,      // 瞬时可恢复（权限弹窗、短暂遮挡）
    Conditional,    // 条件可恢复（需授权、需启动应用）
    Permanent       // 永久不可恢复（平台限制、非法参数）
};

struct ToolFailureInfo {
    ToolFailureLevel level;
    std::string error_code;          // "platform_not_supported"
    std::string canonical_message;   // ≤512B，给模型的结构化描述
    std::string user_message;        // 给用户的友好描述
    std::optional<std::string> recovery_hint;  // "请在系统设置中授权屏幕共享"
};
```

**分类实施**：

```cpp
// integration/mira/src/desktop_action_verification.cpp
ToolFailureInfo classify_capture_error(const desktop::CaptureError& error,
                                       const desktop::DesktopEnvironment& env) {
    ToolFailureInfo info;

    if (error.code == "x11_badmatch" && env.get_screen_capability() == PortalOnly) {
        info.level = ToolFailureLevel::Permanent;
        info.error_code = "platform_not_supported";
        info.canonical_message =
            "Full-screen capture unavailable in Wayland session. "
            "Suggestion: use window-scoped capture or switch to X11 session.";
        info.user_message = "当前 Wayland 环境不支持全屏截图，请使用窗口截图";
        info.recovery_hint = "或在登录时选择 'Ubuntu on Xorg' 会话";
        return info;
    }

    if (error.code == "permission_denied" && error.retriable) {
        info.level = ToolFailureLevel::Transient;
        info.error_code = "permission_pending";
        info.canonical_message = "User authorization required. Retry after authorization.";
        info.user_message = "需要屏幕共享权限";
        info.recovery_hint = "请在弹出窗口中点击'共享'";
        return info;
    }

    // ... 其他分类
}
```

**模型回执格式**：

Level 1（Transient）：
```json
{
    "tool_call_id": "call_abc123",
    "result": {
        "error": "permission_pending",
        "retriable": true,
        "message": "User authorization required. Retry after new observation."
    }
}
```

Level 3（Permanent）：
```json
{
    "tool_call_id": "call_abc123",
    "result": {
        "error": "platform_not_supported",
        "retriable": false,
        "message": "Full-screen capture unavailable in Wayland. Use window capture.",
        "alternative": "desktop.capture_window"
    }
}
```

#### 2.2 反复失败阻止机制

**目标**：同类工具在同 epoch 下失败 3 次后，阻止进一步调用。

**实施**：

```cpp
// integration/mira/src/model_layer.cpp
struct ToolFailureTracker {
    std::string current_epoch;
    std::unordered_map<std::string, int> failure_counts;  // tool_name -> count
    std::unordered_set<std::string> blocked_tools;        // permanently blocked

    void on_new_observation(const std::string& new_epoch) {
        if (new_epoch != current_epoch) {
            current_epoch = new_epoch;
            failure_counts.clear();
            // blocked_tools 跨 epoch 保留
        }
    }

    bool should_block(const std::string& tool_name, ToolFailureLevel last_level) {
        if (blocked_tools.count(tool_name))
            return true;

        if (last_level == ToolFailureLevel::Permanent &&
            failure_counts[tool_name] >= 3) {
            blocked_tools.insert(tool_name);
            return true;
        }

        return false;
    }

    void record_failure(const std::string& tool_name) {
        failure_counts[tool_name]++;
    }
};

// 在 ModelLayer::Impl 中增加成员
ToolFailureTracker failure_tracker_;

// 在工具 handler 中
if (impl_->failure_tracker_.should_block(tool_name, last_failure_level)) {
    return mira::Error{
        mira::ErrorCode::PermanentFailure,
        "mirage.tool",
        "Tool '" + tool_name + "' blocked after 3 permanent failures. " +
        "Last error: " + last_error_message
    };
}
```

**验收**：
- Wayland 会话，请求 4 次全屏截图
- 前 3 次返回"平台不支持"错误
- 第 4 次工具 handler 立即返回"已阻止，尝试 3 次"，不执行底层调用
- 发送新观察后，failure_counts 清零，但 blocked_tools 保留

#### 2.3 预算优雅降级

**目标**：预算剩余 ≤2 轮时，提示模型总结并禁用工具。

**实施**：

```cpp
// integration/mira/src/model_layer.cpp
// 在 complete_harness_turn 返回时
if (response.status == mira::ModelCompletionStatus::Completed) {
    const int remaining_turns = config.max_turns - current_turn_count;

    if (remaining_turns <= 2 && !has_user_visible_reply) {
        completion.budget_warning = true;
        completion.remaining_turns = remaining_turns;
    }
}

// runtime/service/src/runtime_service.cpp
// 在收到 completion 后
if (completion.budget_warning) {
    const std::string system_hint =
        "SYSTEM: You have " + std::to_string(completion.remaining_turns) +
        " turns remaining. Please summarize what you've attempted and "
        "explain what prevented task completion, rather than retrying "
        "failed actions. On your last turn, tools will be disabled.";

    // 将 system_hint 附加到下次 transcript
    append_to_next_transcript(session_id, system_hint);
}

if (completion.remaining_turns == 1) {
    // 最后一轮禁用工具
    tools_allowed = false;
}
```

**验收**：
- 构造 14 轮无效工具调用（模拟平台限制失败）
- 第 15 轮：模型收到预算警告，生成总结文本（而非继续调用工具）
- 第 16 轮：工具禁用，模型必须生成文本回复
- 最终失败消息包含"尝试了 X 次截图，均因平台限制失败"

### Phase 3：根本解决（目标 5-7 天）

#### 3.1 SSE 流重建（方案 A）

**目标**：会话恢复后正确重建流式订阅，首轮即可流式显示。

**调查任务**：
1. 确认当前 SSE 流失效的具体错误（需实际复现）
2. 定位 ModelLayer 流式订阅的生命周期管理代码
3. 确认会话持久化是否保存流式配置状态

**实施方案**（待调查后细化）：

```cpp
// runtime/service/src/runtime_service.cpp
// 会话恢复时
void restore_session(const std::string& session_id) {
    auto& session = sessions_[session_id];

    // 重建流式订阅
    if (session.stream_enabled) {
        session.preview_sink = create_preview_sink(session_id);
        session.process_sink = create_process_sink(session_id);
    }

    // 验证订阅有效性
    if (session.preview_sink && !test_sink_reachability(session.preview_sink)) {
        log_warning("Preview sink unreachable for session " + session_id +
                   ", falling back to non-streaming");
        session.preview_sink = nullptr;
    }
}
```

**验收**：
- 创建会话，发送消息 M1（流式显示）
- 退出 Mirage，重启，恢复会话
- 发送消息 M2，首轮即流式显示（无降级）
- 检查日志无 SSE 相关错误

**回退风险**：高（涉及核心订阅生命周期，失败影响所有会话）

#### 3.2 M7-14 修复验证

**目标**：确认 DEC-068 的工具拒绝恢复在实际场景中生效。

**验证任务**：
1. **Xorg 对照实验**：
   - 注销当前 Wayland 会话
   - 登录时选择"Ubuntu on Xorg"
   - 重新启动 Mirage 和 Firefox
   - 重跑 2026-10-11 bilibili 任务
   - **预期**：完整 X11 能力下，截图成功，任务完成

2. **Portal 授权路径**：
   - Wayland 会话中请求全屏截图
   - 触发 Portal 授权弹窗
   - 第一次拒绝授权（点击"取消"）
   - 发送新观察
   - 第二次请求截图并授权（点击"共享"）
   - **预期**：第二次截图成功，符合 DEC-068 "拒绝→授权→重试"

3. **Level 2/3 失败回归**：
   - 构造无权限应用启动（Level 3）
   - 构造窗口焦点丢失（Level 1）
   - 构造需要 IME 的输入（Level 2，当前不支持中文输入法）
   - **预期**：每类失败返回正确分级回执

#### 3.3 会话恢复回归测试

**测试矩阵**：

| 场景 | 验证点 | 预期结果 |
|------|--------|---------|
| 空会话恢复 | 无历史消息 | 直接可用，无错误 |
| 单轮会话恢复 | 1 条用户消息 + 1 条助手回复 | 历史显示正确，新消息流式 |
| 多轮会话恢复 | 10+ 轮对话 | 历史加载正确，新消息流式 |
| 工具会话恢复 | 包含工具调用的会话 | 工具过程卡片显示，新消息流式 |
| 失败会话恢复 | 以错误终止的会话 | 错误状态保留，可继续对话 |
| 流式配置切换 | 恢复前后切换流式开关 | 尊重最新配置 |

**实施**：
- `tests/runtime/session_recovery_test.cpp`：新增回归测试
- 覆盖 SSE 订阅、工具状态、UI 订阅、预算恢复

## 风险与依赖

### 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| SSE 流重建失败影响所有会话 | 中 | 高 | Phase 1 降级方案作为回退 |
| 工具失败计数器状态管理复杂 | 中 | 中 | 先实施简化版（连续失败计数） |
| Wayland 环境检测误判 | 低 | 高 | 多环境测试（Xorg/Wayland/WSL） |
| 预算警告被模型忽略 | 中 | 中 | 最后一轮强制禁用工具兜底 |

### 依赖

**上游依赖**（Mira）：
- ✅ ConversationLoop 错误分类（当前支持 Cancelled/Failed/Incomplete）
- ❓ 扩展错误码支持（PermanentFailure，需确认是否已有）
- ❓ 工具 handler 返回结构化失败信息（当前为字符串）

**横向依赖**（Mirage 内部）：
- Runtime Service IPC 协议扩展（工具错误事件）
- UI 层工具错误卡片渲染（Native + Web）
- Desktop Environment 能力查询接口
- 会话持久化格式扩展（流式订阅状态）

### 时间估算

| Phase | 开发 | 测试 | 评审 | 总计 |
|-------|------|------|------|------|
| Phase 1 | 1 天 | 0.5 天 | 0.5 天 | 2 天 |
| Phase 2 | 3 天 | 1.5 天 | 0.5 天 | 5 天 |
| Phase 3 | 4 天 | 2 天 | 1 天 | 7 天 |
| **总计** | **8 天** | **4 天** | **2 天** | **14 天** |

假设单人全职开发，实际可能因调查、依赖阻塞延长至 3-4 周。

## 成功指标

### 定量指标

- **会话恢复成功率**：从当前未知 → 目标 95%+（Phase 3 后）
- **工具失败后任务完成率**：从 0%（必然耗尽预算）→ 目标 60%+（给出总结）
- **确定性失败的无效重试次数**：从 5+ 次 → 目标 ≤3 次（阻止机制）
- **用户可见失败原因比例**：从 0%（"预算耗尽"）→ 目标 100%（工具错误卡片）

### 定性指标

- 用户在 Wayland 环境能理解"当前不支持全屏截图"，不认为是程序 bug
- 用户在会话失败时能得到明确总结（"尝试了什么"、"为何失败"）
- 开发者能从日志快速定位工具失败原因（错误码、分类、失败计数）

## 参考

- [DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md)：
  决策文档
- [gui-session-failure-20261011.md](../compatibility/gui-session-failure-20261011.md)：
  bilibili 任务失败诊断
- [model-network-retry-20261012.md](../compatibility/model-network-retry-20261012.md)：
  网络重试修复
- [DEC-068](../decisions/DEC-068-bundled-vision-and-recoverable-tool-rejection.md)：
  M7-14 可恢复工具拒绝
- [M7 计划](m7-visual-tools-and-input.md)：视觉工具与键鼠输入闭环
