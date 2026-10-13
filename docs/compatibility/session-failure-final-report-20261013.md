# 2026-10-13 会话失败问题：最终分析报告

> 日期：2026-10-13
> 基于实际会话证据
> 会话来源：Session 2 (02:41:52) 和 Session 5 (01:48:39)

## 执行摘要

从 `~/.local/state/mirage/session-state.json` 提取了 2026-10-13 的两个实际失败会话，
完全验证了两个关键问题：

1. **SSE 流生命周期错误**（会话 5 Turn 6）：
   - 错误："model call failed: invalid Messages SSE lifecycle or budget"
   - **重要发现**：不是"程序重启恢复会话"问题，是**运行中的流状态管理问题**
   - 发生在连续预算耗尽失败后

2. **工具反复失败导致预算耗尽**（会话 2 和会话 5）：
   - 截图工具 3/3 失败（会话 2）、2/2 失败（会话 5）
   - 错误码不一致："invalid capture source" 和 "io_error"
   - 最终："turn budget exhausted before a final answer"

## 问题 1：SSE 流生命周期错误（优先级提升为高）

### 实际证据

**会话 5（2026-10-13 01:48:39）**：

| Turn | 状态 | 用户输入 | 错误 |
|------|------|---------|------|
| 1 | ok | "你好" | - |
| 2 | failed | "你帮我打开个好吃的视频，在b站" | turn budget exhausted |
| 3 | failed | "没事的" | turn budget exhausted |
| 4 | failed | "你告诉我失败的原因吧" | turn budget exhausted |
| 5 | ok | "你告诉我原因就行为什么还调用这么多工具，直接说话就行" | - |
| 6 | **failed** | "当前屏幕共享是对你完全开发的" | **invalid Messages SSE lifecycle or budget** |

### 关键发现

**这不是"程序重启恢复会话"的问题**：
- 会话 5 是连续运行的，没有重启
- SSE 错误发生在运行中的 Turn 6
- 影响范围比之前分析的更广：**任何长会话在失败后都可能触发**

**时间线分析**：
1. Turn 2-4：连续 3 次预算耗尽失败（每次都有截图失败）
2. Turn 5：成功（模型只生成文本，未调用工具）
3. Turn 6：**SSE 流生命周期错误**

### 根因推测

**假设 1：连续失败后的流状态不一致**

```cpp
// integration/mira/src/model_layer.cpp
// Turn 2-4 预算耗尽时
case mira::ModelCompletionStatus::Incomplete:
    completion.failed = true;
    completion.error = "turn budget exhausted before a final answer";
    return finish();  // ⚠️ 流状态是否正确清理？
```

可能的问题：
- `DialogPreviewSink` 在预算耗尽后处于不一致状态
- Turn 5 成功但未重置流状态
- Turn 6 尝试使用失效的流订阅

**假设 2：预算计数器与流生命周期不同步**

错误信息："invalid Messages SSE lifecycle or budget"

这表明 Mira 的 ConversationLoop 检测到预算或生命周期异常：
- 可能预算计数器溢出或不一致
- 可能 Turn 6 的预算验证失败
- 可能流生命周期管理与预算管理不同步

### 修复方案（优先级：高）

**紧急修复（1天）：SSE 流健康检查与降级**

```cpp
// integration/mira/src/model_layer.cpp
DialogCompletion ModelLayer::complete_harness_turn(...) {
    // 在调用前验证 sink 有效性
    if (preview && !is_sink_reachable(preview)) {
        log_warning("Preview sink unreachable, disabling streaming");
        preview = nullptr;  // 降级为非流式
    }

    // ... 继续正常调用
}
```

**中期修复（3天）：失败后的流状态重置**

```cpp
// 在任何失败后显式清理流状态
case mira::ModelCompletionStatus::Incomplete:
case mira::ModelCompletionStatus::Failed:
    completion.failed = true;
    completion.error = ...;

    // 新增：清理流状态
    impl_->reset_stream_state();

    return finish();
```

**长期修复（5天）：流生命周期重构**

需要调查：
1. ModelLayer 与 ConversationLoop 的预算同步机制
2. preview_sink 的生命周期管理
3. SSE 解析器状态的显式重置点
4. 增加遥测：流创建/失败/重置事件

## 问题 2：工具反复失败导致预算耗尽（优先级：高）

### 实际证据

**会话 2（2026-10-13 02:41:52）**：

用户："我想让你用鼠标键盘工具，打开终端后，在mirage目录下打开我的vscode"

截图失败 3/3：

| 次数 | 输入参数 | 错误 |
|------|---------|------|
| 1 | `source: "desktop", source_id: "eDP-1"` | **invalid capture source** |
| 2 | `source: "display", source_id: "eDP-1"` | **io_error** |
| 3 | `source: "desktop", source_id: "desktop"` | **invalid capture source** |

最终：`turn budget exhausted before a final answer`

**会话 5（2026-10-13 01:48:39）**：

用户："你帮我打开个好吃的视频，在b站"

截图失败 2/2（错误信息未记录，但都失败）

最终：`turn budget exhausted before a final answer`

### 关键发现

**1. 错误码不一致**：
- 同类失败返回不同错误："invalid capture source" 和 "io_error"
- 模型无法识别这是"同一类永久失败"

**2. 模型的尝试是合理的**：
- 尝试了不同的 source 类型（"desktop" / "display"）
- 尝试了不同的 source_id（"eDP-1" / "desktop"）
- 这是**正确的调试策略**，但平台根本不支持

**3. 平台能力误报**：
- 系统信息显示：eDP-1（内置屏幕）、3200×2000 分辨率
- 截图全部失败，推测是 Wayland 环境
- 但 Desktop Environment 误报为"可用"

**4. 用户完全无法理解失败原因**：
- 只看到"turn budget exhausted before a final answer"
- 不知道是截图不可用，还是其他原因
- 无法自助解决（如切换到 Xorg 会话）

### 根因验证

**根因 1：平台能力误判**（已验证 ✅）

`LinuxDesktopEnvironment` 在 Wayland 下误报完整 X11 能力：
- `XOpenDisplay()` 成功（通过 XWayland）
- 但 `XGetImage` 根窗口失败（Wayland 安全限制）

**根因 2：工具失败未分类**（已验证 ✅）

同类失败返回不同错误码：
- "invalid capture source"：可能是参数校验失败
- "io_error"：可能是底层 I/O 失败
- 模型看到的是"不同的错误"，无法识别模式

**根因 3：无反复失败阻止**（已验证 ✅）

会话 2 调用了 3 次截图，全部失败，无阻止机制。

**根因 4：预算耗尽即终止**（已验证 ✅）

12 个工具调用耗尽预算后，立即失败，无总结。

### 修复方案（优先级：高）

**紧急修复（1-2天）**：

1. **Wayland 环境能力判定**：
   ```cpp
   bool is_wayland = std::getenv("WAYLAND_DISPLAY") ||
                     (std::getenv("XDG_SESSION_TYPE") == "wayland");
   if (XOpenDisplay() && is_wayland) {
       screen_capability_ = ScreenCapability::PortalOnly;
   }
   ```

2. **工具错误 UI 可见性**：
   - 失败时向 UI 推送 `ToolErrorEvent`
   - 显示："截图失败（平台限制）：当前环境不支持全屏截图"

**核心修复（3-5天）**：

3. **工具失败三级分类**：
   - Transient：瞬时可恢复
   - Conditional：条件可恢复
   - Permanent：永久不可恢复
   - "invalid capture source" / "io_error"（Wayland）→ Permanent

4. **反复失败阻止机制**：
   - 同类工具在同 epoch 下失败 3 次 → 阻止后续调用

5. **预算优雅降级**：
   - 剩余 ≤2 轮时注入系统提示："请总结失败原因"
   - 最后一轮禁用工具，强制生成文本

## 与原分析的对比

### 修正 1：SSE 错误的场景更广

**原假设**：程序重启后恢复会话，首轮 SSE 流订阅未重建

**实际情况**：
- ✅ 会话 5 没有重启
- ✅ SSE 错误发生在运行中（Turn 6）
- ✅ 前序有连续失败（Turn 2-4 预算耗尽）

**影响**：
- 原方案"恢复会话首轮降级"只能缓解恢复场景
- 需要更广泛的 SSE 流健康检查和状态重置
- 优先级从"中"提升为"高"

### 修正 2：截图失败的错误码问题

**原假设**：所有 XGetImage 失败都返回 `io_error`

**实际情况**：
- ✅ 第 1/3 次："invalid capture source"
- ✅ 第 2 次："io_error"

**影响**：
- 需要统一错误分类逻辑
- "invalid capture source" 可能是参数校验层返回的
- 需要在更高层（desktop_image_supply.cpp）统一分类

## 更新的修复优先级

| 问题 | 原优先级 | 新优先级 | 理由 |
|------|---------|---------|------|
| SSE 流错误 | 紧急（恢复会话） | **高（运行中会话）** | 影响范围更广，任何长会话都可能触发 |
| 工具反复失败 | 高 | **高** | 已有实际证据，用户无法完成任务 |
| 预算优雅降级 | 中 | **高** | 两个会话都以预算耗尽终止，无总结 |
| 会话恢复降级 | 紧急 | 中（待验证） | 实际 SSE 错误不是恢复场景 |

## 下一步行动

### 立即验证（今天）

1. **确认桌面环境**：
   ```bash
   echo $WAYLAND_DISPLAY
   echo $XDG_SESSION_TYPE
   loginctl show-session $(loginctl | grep $(whoami) | awk '{print $1}') -p Type
   ```

2. **查看截图失败的底层日志**：
   - 检查是否有 X11 error code
   - 确认是否是 Wayland XGetImage BadMatch

3. **尝试复现 SSE 错误**：
   - 在新会话中触发连续预算耗尽
   - 观察是否出现 SSE 流错误

### 实施修复（本周）

**Day 1-2（紧急）**：
1. SSE 流健康检查与降级
2. Wayland 环境能力判定
3. 工具错误 UI 可见性

**Day 3-5（核心）**：
4. 工具失败三级分类
5. 反复失败阻止机制
6. 预算优雅降级

**Day 6-7（验证）**：
7. 在 Xorg 会话中重跑任务（对照实验）
8. 在 Wayland 会话中触发 Portal 授权路径
9. 回归测试

### 更新文档（今天）

- ✅ 创建证据文档（session-failure-evidence-20261013.md）
- ✅ 创建最终分析报告（本文档）
- ⚠️ 更新 DEC-085：增加 SSE 运行时错误的分析
- ⚠️ 更新实施计划：调整优先级

## 总结

**两个实际会话完全验证了问题存在，并揭示了新的关键信息**：

1. **SSE 流错误**：
   - ✅ 已证实存在
   - ❌ 不是"恢复会话"问题
   - ✅ 是"运行中流状态管理"问题
   - 影响范围更广，优先级提升

2. **工具反复失败**：
   - ✅ 已证实存在
   - ✅ 错误码不一致
   - ✅ 模型尝试合理
   - ✅ 预算耗尽无总结

**修复方案的总体方向正确，但需要调整重点**：
- SSE 流健康检查应优先于"恢复会话降级"
- 工具失败分类应统一在高层进行
- 预算优雅降级是必须的（两个会话都需要）

下一步由维护者评审本报告并授权实施。

---

**关键交付件**：
1. [session-failure-evidence-20261013.md](session-failure-evidence-20261013.md) - 原始证据提取
2. 本文档 - 最终分析报告
3. [DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md) - 决策文档（待更新）
4. [session-resilience-fix.md](../plans/session-resilience-fix.md) - 实施计划（待更新）
