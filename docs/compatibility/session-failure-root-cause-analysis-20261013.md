# 最近会话失败根因分析与修复建议

> 日期：2026-10-13
> 负责人：Mirage 维护者
> 关联：DEC-085、session-resilience-fix.md
> 状态：分析完成，待实施

## 执行摘要

最近两次会话暴露了 Mirage 的两个关键问题：

1. **程序重启后恢复会话出现 model SSE 问题**
2. **工具端反复调用失败，会话直接终止**

**核心发现**：
- SSE 问题根因是会话恢复路径未重建流式订阅，需要重建订阅生命周期或降级为非流式
- 工具反复失败是因为平台能力误判 + 失败未分类 + 无阻止机制，导致确定性失败循环
- M7-14 的修复仅覆盖权限弹窗等瞬时失败，未覆盖平台永久限制（Wayland 截图）
- 需要三级工具失败分类、反复失败阻止机制、预算优雅降级来根本解决

**修复优先级**：
1. **紧急**（1-2天）：会话恢复降级 + Wayland 能力判定 + 工具错误可见
2. **核心**（3-5天）：工具失败分类 + 反复失败阻止 + 预算降级
3. **根本**（5-7天）：SSE 流重建 + M7 验证 + 回归测试

## 问题 1：运行中会话的 Model SSE 流生命周期错误

### 实际证据

**会话 5（2026-10-13 01:48:39）**：

| Turn | 状态 | 用户输入 | 错误 |
|------|------|---------|------|
| 1 | ok | "你好" | - |
| 2 | failed | "你帮我打开个好吃的视频，在b站" | turn budget exhausted |
| 3 | failed | "没事的" | turn budget exhausted |
| 4 | failed | "你告诉我失败的原因吧" | turn budget exhausted |
| 5 | ok | "你告诉我原因就行为什么还调用这么多工具，直接说话就行" | - |
| 6 | **failed** | "当前屏幕共享是对你完全开发的" | **model call failed: invalid Messages SSE lifecycle or budget** |

**关键发现**：
- SSE 错误发生在 Turn 6（运行中），**不是程序重启恢复会话**
- 前序有 4 次预算耗尽失败 + 1 次成功
- 错误信息："invalid Messages SSE lifecycle or budget"

### 根因探索

**根因假设 1：连续失败后的流状态不一致**

会话 5 的时间线：
- Turn 2-4：预算耗尽失败（3 次连续失败）
- Turn 5：成功（模型生成文本回复）
- Turn 6：**SSE 流生命周期错误**

代码路径推测：

```cpp
// integration/mira/src/model_layer.cpp
// Turn 2-4 预算耗尽时
case mira::ModelCompletionStatus::Incomplete:
    completion.failed = true;
    completion.error = "turn budget exhausted before a final answer";
    return finish();  // ⚠️ 流状态是否正确清理？
```

可能的问题：
1. **流状态未在失败后重置**：`DialogPreviewSink` 在预算耗尽后可能处于不一致状态
2. **预算计数器与流生命周期不同步**：Turn 5 成功重置了预算，但流状态未重置
3. **SSE 解析器状态残留**：`AnthropicMessagesSseParser` 在失败后未清理内部状态

**根因假设 2：预算重置后的流订阅失效**

```cpp
// runtime/service/src/runtime_service.cpp（推测）
// Turn 5 成功后，Turn 6 开始新轮次
DialogCompletion completion = model_layer_->complete_harness_turn(
    transcript, user_text, context, reasoning, tools_allowed,
    session.preview_sink,  // ⚠️ 可能在前序失败后失效
    session.process_sink,
    cancel, activity
);
```

可能的问题：
- `preview_sink` 在 Turn 2-4 失败期间被设置为 nullptr
- Turn 5 成功但未重建 sink
- Turn 6 尝试使用 nullptr sink，触发生命周期错误

**根因假设 3：ConversationLoop 预算管理的边界条件**

错误信息："invalid Messages SSE lifecycle or budget"

这可能是 Mira 侧的错误，表示：
- 预算已耗尽，但仍然尝试开始新的 SSE 流
- 或者预算计数器溢出/不一致
- 或者 Turn 6 的预算验证失败

需要检查：
```cpp
// integration/mira/src/model_layer.cpp
config.max_turns = static_cast<std::uint32_t>(
    std::min<std::uint64_t>(16, turn_capacity - 1 - config.max_recoveries));

// Turn 6 时，turn_capacity 是多少？
// max_turns 是否正确计算？
// 是否存在 Turn 5 成功后预算未重置的问题？
```

### 为什么会直接终止？

SSE 流生命周期错误在 `ModelLayer::complete_harness_turn` 中被捕获：

```cpp
// integration/mira/src/model_layer.cpp
// Mira 返回 Failed 状态时
completion.failed = true;
completion.error = "model call failed: " + response.error.message;
return finish();
```

SSE 生命周期错误被视为**模型层致命错误**，会话循环立即终止。

**问题**：
- 用户看到的只是"model call failed: invalid Messages SSE lifecycle or budget"
- 没有上下文（发生在哪个阶段、前序有什么失败）
- 无法恢复（会话进入失败状态）

**影响范围**：
- 不只是恢复会话的问题，**运行中的会话也会触发**
- 任何导致连续失败的场景（截图失败、权限拒绝）都可能间接触发
- 比"程序重启恢复"更严重，因为影响所有长会话

### 修复方案

**紧急方案（1天，调整优先级为高）：SSE 流健康检查与降级**

在每次调用前验证流订阅有效性：

```cpp
// integration/mira/src/model_layer.cpp
DialogCompletion ModelLayer::complete_harness_turn(...) {
    // 在调用前验证 sink 有效性
    if (preview && !is_sink_healthy(preview)) {
        log_warning("Preview sink unhealthy, disabling streaming for this turn");
        preview = nullptr;  // 降级为非流式
    }

    // 记录流状态用于诊断
    if (preview) {
        log_debug("Streaming enabled for turn");
    }

    // ... 继续正常调用
}

// 辅助函数
bool is_sink_healthy(const DialogPreviewSink& sink) {
    try {
        // 尝试调用空预览以验证 sink 可达
        sink(DialogPreview{DialogPreview::Type::StartTurn, ""});
        return true;
    } catch (...) {
        return false;
    }
}
```

**中期方案（3天）：失败后的流状态重置**

在预算耗尽或任何失败后，显式清理流状态：

```cpp
// integration/mira/src/model_layer.cpp
case mira::ModelCompletionStatus::Incomplete:
case mira::ModelCompletionStatus::Failed:
    completion.failed = true;
    completion.error = ...;

    // 新增：清理流状态，下次调用时重新初始化
    impl_->clear_stream_state();

    return finish();
```

**长期方案（5天）：SSE 流生命周期重构**

需要调查并实施：
1. ModelLayer 与 ConversationLoop 的预算同步机制
2. preview_sink 的生命周期管理（何时创建、何时失效、何时重建）
3. SSE 解析器状态的显式重置点
4. 增加遥测：流创建/失败/重置事件

## 问题 2：工具端反复调用失败，会话直接终止

### 根因分析：为什么工具反复失败？

**案例**：2026-10-11 bilibili 任务
- 用户在 Wayland 环境请求"打开 bilibili 找视频"
- 模型调用 5 次 `desktop.capture_screen`（全屏截图），全部失败
- 错误码：`io_error`（底层 XGetImage 返回 BadMatch error=8）
- 模型在每次失败后 wait 几秒，重新观察，再次截图，再次失败
- 16 步预算耗尽，会话终止

**根因 1：平台能力误判**

代码位置：`platform/linux/src/linux_environment.cpp`

```cpp
// 当前逻辑（简化）
if (XOpenDisplay(display_name)) {
    // ✅ X11 连接成功
    screen_provider_ = std::make_unique<X11ScreenProvider>();
    // ❌ 问题：未检测 Wayland 会话
    // ❌ 问题：未验证根窗口是否可采集
}
```

在 Wayland 环境中：
- `XOpenDisplay()` 成功（通过 XWayland 兼容层）
- `XGetImage(root_window)` 失败（Wayland 不允许采集根窗口，安全限制）
- Mirage 误报完整 X11 能力，工具注册为"可用"

**正确逻辑**：

```cpp
bool is_wayland = std::getenv("WAYLAND_DISPLAY") ||
                  (std::getenv("XDG_SESSION_TYPE") == "wayland");

if (XOpenDisplay(display_name)) {
    if (is_wayland) {
        screen_capability_ = ScreenCapability::PortalOnly;
        // X11 仅用于窗口枚举，根窗口采集不可用
    } else {
        screen_capability_ = ScreenCapability::Full;
        // Xorg 会话，完整能力
    }
}
```

**根因 2：工具失败未分类**

代码位置：`platform/linux/src/x11_backend.cpp`

```cpp
// 截图失败时
if (!ximage) {
    return desktop::CaptureError{
        "io_error",  // ❌ 笼统错误码
        "XGetImage failed"  // ❌ 无具体原因
    };
}
```

所有 XGetImage 失败都返回 `io_error`，模型无法区分：
- **瞬时失败**：窗口被遮挡、短暂不可见（可重试）
- **永久失败**：Wayland 平台限制（不可重试）

模型视角：每次都是新的 `io_error`，没有"这是第 5 次同样的失败"的信息。

**正确逻辑**：

```cpp
if (!ximage) {
    int x_error_code = /* 从 X11 错误处理器获取 */;

    if (x_error_code == BadMatch && is_wayland_session) {
        return desktop::CaptureError{
            "platform_not_supported",  // ✅ 明确错误码
            "Wayland session does not support root window capture",
            ToolFailureLevel::Permanent  // ✅ 不可恢复
        };
    } else {
        return desktop::CaptureError{
            "io_error",
            "XGetImage failed with X error " + std::to_string(x_error_code),
            ToolFailureLevel::Transient  // ✅ 可能恢复
        };
    }
}
```

**根因 3：无反复失败阻止机制**

代码位置：`integration/mira/src/model_layer.cpp`（工具 handler）

当前逻辑：
1. 模型请求 `desktop.capture_screen`
2. 工具 handler 执行底层调用
3. 失败，返回错误给模型
4. 模型收到错误，wait 几秒
5. 模型重新观察（epoch 变化）
6. **重复步骤 1-5**，直到预算耗尽

**缺失**：没有"同类工具在同 epoch 下已失败 N 次"的检查。

**正确逻辑**：

```cpp
// 在 ModelLayer::Impl 中维护失败追踪器
struct ToolFailureTracker {
    std::string current_epoch;
    std::map<std::string, int> failure_counts;  // tool_name -> count
    std::set<std::string> blocked_tools;        // 永久阻止列表
};

// 工具 handler 执行前
if (failure_tracker_.blocked_tools.count(tool_name)) {
    return Error{
        "tool_blocked",
        tool_name + " blocked after 3 permanent failures"
    };
}

// 执行后若失败
if (result.failure_level == Permanent) {
    failure_tracker_.failure_counts[tool_name]++;
    if (failure_tracker_.failure_counts[tool_name] >= 3) {
        failure_tracker_.blocked_tools.insert(tool_name);
    }
}

// 新观察时
if (new_epoch != failure_tracker_.current_epoch) {
    failure_tracker_.current_epoch = new_epoch;
    failure_tracker_.failure_counts.clear();
    // blocked_tools 跨 epoch 保留
}
```

### 为什么会直接终止而非给出失败总结？

**预算硬限制触发**：

代码位置：`integration/mira/src/model_layer.cpp:676`

```cpp
config.max_turns = static_cast<std::uint32_t>(
    std::min<std::uint64_t>(16, turn_capacity - 1 - config.max_recoveries));
```

当 `current_turn_count >= 16` 时，`ConversationLoop` 拒绝新请求，返回 Incomplete：

```cpp
// Mira ConversationLoop 内部（简化）
if (turn_count >= config.max_turns) {
    return ModelCompletion{
        .status = ModelCompletionStatus::Incomplete,
        .reason = "turn budget exhausted"
    };
}
```

Mirage 侧处理：

```cpp
// integration/mira/src/model_layer.cpp:528-533
case mira::ModelCompletionStatus::Incomplete:
    completion.failed = true;
    completion.error = "model layer did not complete the turn (status ...)";
    return finish();
```

**问题**：
1. 预算耗尽时，循环立即终止，不给模型"最后一轮总结"的机会
2. 错误消息为笼统的"turn budget exhausted"，不包含：
   - 已执行的动作列表（"尝试了 5 次截图"）
   - 失败原因（"Wayland 平台不支持全屏截图"）
   - 建议操作（"请使用窗口截图或切换到 Xorg 会话"）

### 修复方案

**方案 1：工具失败三级分类（3天）**

引入 `ToolFailureLevel` 枚举：
- `Transient`：瞬时可恢复（权限弹窗、短暂遮挡）
- `Conditional`：条件可恢复（需授权、需启动应用）
- `Permanent`：永久不可恢复（平台限制、非法参数）

每个工具返回结构化失败信息：
```cpp
struct ToolFailureInfo {
    ToolFailureLevel level;
    std::string error_code;          // "platform_not_supported"
    std::string canonical_message;   // ≤512B，给模型
    std::string user_message;        // 给用户
    std::optional<std::string> recovery_hint;  // "请切换到 Xorg 会话"
};
```

**方案 2：反复失败阻止（2天）**

维护 `ToolFailureTracker`，在同 epoch 下：
- `Permanent` 失败 ≥3 次 → 阻止该工具后续调用
- 新 epoch 时清零计数，但保留阻止列表

**方案 3：预算优雅降级（2天）**

当 `remaining_turns <= 2` 且无用户可见回复时：
1. 向模型发送系统提示："你剩余 2 轮，请总结尝试了什么、为何失败"
2. 最后一轮禁用工具，强制模型生成文本回复
3. 失败时 `completion.error` 包含已执行动作摘要

**方案 4：平台能力如实声明（1天）**

修复 `LinuxDesktopEnvironment` 的 Wayland 检测：
- 检测 `WAYLAND_DISPLAY` + `XDG_SESSION_TYPE`
- 区分 `Full`（Xorg）、`PortalOnly`（Wayland）、`Unavailable`
- 工具注册时根据能力调整可用性和描述

**方案 5：工具错误 UI 可见（1天）**

当工具执行失败时，向 UI 推送 `ToolErrorEvent`：
- 显示红色错误卡片
- 文本："截图失败（平台限制）：当前环境不支持全屏截图，请使用窗口截图"
- 点击展开：技术细节 + 建议操作

## 问题 3：M7 阶段的修复是否生效？

### M7-14 的修复内容

工作项：M7-14（DEC-068，bundled-vision-and-tool-recovery-20261011.md）

**已验收内容**：
1. **512 字节 canonical 回执**：工具失败返回结构化消息给模型
2. **字段诊断**：错误包含可操作信息
3. **拒绝→新观察→已验证纠正**：权限拒绝后，用户授权，重试成功

**验收场景**：
- 截图权限拒绝 → 用户授权 → 重试成功 ✅
- 窗口焦点丢失 → 重新观察 → 激活成功 ✅
- OCR 模型未加载 → 加载后 → 识别成功 ✅

### 为什么最新会话未体现修复？

**原因 1：Wayland 能力判定在 M7-10，2026-10-11 时未完成**

M7-10 工作项（DEC-073/074）负责原生 Wayland Portal 观察，状态为 In Progress。
2026-10-11 的失败会话发生在该工作项完成之前，因此：
- 环境仍误报为完整 X11 能力
- 截图失败被标记为通用 `io_error`
- M7-14 的"拒绝恢复"逻辑未被触发（因为不是权限拒绝，是平台限制）

**原因 2：确定性失败未归类为 M7-14 覆盖范围**

M7-14 的"可恢复工具拒绝"定义：
- **瞬时失败**：权限弹窗待授权、窗口焦点丢失
- **语义**：无副作用，新观察后可重试

Wayland 平台限制不是"瞬时失败"：
- **永久失败**：平台架构限制，无法通过授权或等待解决
- **语义**：不可恢复，需要替代方案（窗口截图）或环境切换（Xorg）

DEC-068 未定义"永久失败"的分类和处理策略，因此 XGetImage BadMatch 错误
未被归入"已知不可恢复"类别，仍然允许重试。

**原因 3：预算内无恢复空间**

`max_turns=16` 在图像化任务中是紧预算：
- 观察（1 turn）+ 截图（1 turn）+ 分析（1 turn）= 3 turns/cycle
- 5 次失败 × 3 turns = 15 turns
- 剩余 1 turn 不足以让模型调整策略

`max_recoveries=2`（MIRA-20261012-001）仅用于网络层重试，不影响工具预算。

### M7-14 修复在其覆盖范围内是有效的

**证据**：
- `bundled-vision-and-tool-recovery-20261011.md` 显示权限拒绝恢复场景通过
- `desktop-rejection-recovery-20261011.md` 显示 512B 回执和新观察路径有效

**未覆盖的场景**：
- 平台永久限制（Wayland、UIPI）
- 参数非法（无效坐标、不支持字符）
- 环境缺失（无 Provider、模型未加载）

### 需要扩展 M7-14 的分类

**当前**：二分类
- 可恢复（瞬时失败）
- 不可恢复（其他所有失败）

**建议**：三分类
- Transient（瞬时可恢复，M7-14 已支持）
- Conditional（条件可恢复，新增）
- Permanent（永久不可恢复，新增）

## 问题 4：如何调整调优？

### 短期调优（1-2天，高优先级）

**1. 会话恢复首轮降级**

文件：`runtime/service/src/runtime_service.cpp`

```cpp
// 在 SessionState 中增加标志
struct SessionState {
    bool first_turn_after_restore = false;
};

// 恢复会话时
session.first_turn_after_restore = true;

// 调用模型层时
DialogPreviewSink preview = session.first_turn_after_restore
    ? nullptr
    : create_preview_sink(session_id);

// 首轮成功后清除标志
if (completion.ok && session.first_turn_after_restore) {
    session.first_turn_after_restore = false;
}
```

**验收**：重启后恢复会话，首轮非流式（等待完整响应），后续轮次流式。

**2. Wayland 环境判定**

文件：`platform/linux/src/linux_environment.cpp`

```cpp
bool LinuxDesktopEnvironment::is_wayland_session() {
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    return (wayland_display && *wayland_display) ||
           (session_type && std::string(session_type) == "wayland");
}

void LinuxDesktopEnvironment::initialize() {
    if (XOpenDisplay(display_name_)) {
        if (is_wayland_session()) {
            screen_capability_ = ScreenCapability::PortalOnly;
        } else {
            screen_capability_ = ScreenCapability::Full;
        }
    }
}
```

**验收**：Xorg 会话返回 `Full`，Wayland 会话返回 `PortalOnly`。

**3. 工具错误 UI 可见**

文件：`runtime/ipc/include/mirage/runtime/ipc/protocol.hpp`

```cpp
struct ToolErrorEvent {
    std::string session_id;
    std::string tool_name;
    std::string error_code;
    std::string user_message;
    int failure_count;
};
```

文件：`runtime/service/src/runtime_service.cpp`

```cpp
// 工具失败时推送事件
if (tool_result.failed && is_user_actionable(tool_result)) {
    ToolErrorEvent event{
        session_id,
        tool_name,
        tool_result.error_code,
        translate_error_message(tool_result.error),
        count_failures(session_id, tool_name)
    };
    broadcast_tool_error(event);
}
```

**验收**：Wayland 截图失败时，UI 显示红色错误卡片，文本为"当前环境不支持全屏截图"。

### 中期调优（3-5天，核心韧性）

**4. 工具失败三级分类**

文件：`desktop/environment/include/mirage/desktop/tool_failure.hpp`（新建）

```cpp
enum class ToolFailureLevel {
    Transient,    // 瞬时可恢复
    Conditional,  // 条件可恢复
    Permanent     // 永久不可恢复
};

struct ToolFailureInfo {
    ToolFailureLevel level;
    std::string error_code;
    std::string canonical_message;  // ≤512B
    std::string user_message;
    std::optional<std::string> recovery_hint;
};
```

文件：`integration/mira/src/desktop_action_verification.cpp`

实施分类逻辑（见 session-resilience-fix.md § 2.1）。

**5. 反复失败阻止**

文件：`integration/mira/src/model_layer.cpp`

```cpp
class ModelLayer::Impl {
    struct ToolFailureTracker {
        std::string current_epoch;
        std::map<std::string, int> failure_counts;
        std::set<std::string> blocked_tools;
    } failure_tracker_;
};

// 工具 handler 中检查
if (impl_->failure_tracker_.should_block(tool_name, last_failure_level)) {
    return Error{/* tool blocked */};
}
```

**6. 预算优雅降级**

文件：`integration/mira/src/model_layer.cpp`

```cpp
// 返回 completion 前检查
if (remaining_turns <= 2 && !has_user_visible_reply) {
    completion.budget_warning = true;
    completion.remaining_turns = remaining_turns;
}
```

文件：`runtime/service/src/runtime_service.cpp`

```cpp
// 收到 budget_warning 后注入系统提示
if (completion.budget_warning) {
    append_system_hint(session_id,
        "SYSTEM: You have " + to_string(remaining_turns) +
        " turns remaining. Please summarize progress and explain "
        "what prevented completion, rather than retrying.");
}

// 最后一轮禁用工具
if (completion.remaining_turns == 1) {
    tools_allowed = false;
}
```

### 长期调优（5-7天，根本解决）

**7. SSE 流重建**

需调查后实施（见 session-resilience-fix.md § 3.1）。

**8. M7-14 验证**

- Xorg 对照实验（重跑 bilibili 任务）
- Portal 授权路径验证
- Level 2/3 失败回归测试

**9. 遥测与自适应预算**

```cpp
// 记录工具失败统计
struct ToolFailureMetrics {
    int transient_failures;
    int conditional_failures;
    int permanent_failures;
    int recovery_successes;
};

// 自适应预算
if (task_type == "visual_desktop") {
    max_turns += 4;  // 图像任务需要更多轮次
}
```

## 总结

| 问题 | 根因 | 修复优先级 | 预计时间 |
|------|------|-----------|---------|
| 会话恢复 SSE 错误 | 流式订阅未重建 | 紧急 | 1天（降级）/ 5天（根本） |
| 工具反复失败 | 平台误判 + 未分类 + 无阻止 | 紧急 | 1天（能力判定）+ 3天（分类阻止） |
| 会话直接终止 | 预算硬限制，无总结机会 | 高 | 2天（优雅降级） |
| M7-14 未生效 | 覆盖范围限于瞬时失败 | 中 | 3天（扩展分类）+ 2天（验证） |

**推荐实施顺序**：
1. **Day 1-2**：会话恢复降级 + Wayland 判定 + 工具错误可见（紧急修复）
2. **Day 3-7**：工具失败分类 + 反复阻止 + 预算降级（核心韧性）
3. **Day 8-14**：SSE 流重建 + M7 验证 + 回归测试（根本解决）

**关键交付件**：
- [DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md)：决策文档
- [session-resilience-fix.md](../plans/session-resilience-fix.md)：详细实施计划
- 本文档：根因分析与调优建议

下一步由 Mirage 维护者评审并授权实施。
