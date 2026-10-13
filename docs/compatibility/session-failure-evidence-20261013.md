# 2026-10-13 实际会话失败证据

> 日期：2026-10-13
> 来源：~/.local/state/mirage/session-state.json
> 会话：Session 2 (02:41:52) 和 Session 5 (01:48:39)

## 执行摘要

从 session-state.json 提取了 2026-10-13 的两个实际失败会话：
- **会话 5**：出现了 SSE 流错误（"invalid Messages SSE lifecycle or budget"）
- **会话 2**：截图工具反复失败，预算耗尽终止

这两个会话完全验证了之前分析的两个核心问题。

## 会话 5：SSE 流错误（01:48:39）

### 会话概要

| Turn | 状态 | 用户输入 | 错误 |
|------|------|---------|------|
| 1 | ok | "你好" | - |
| 2 | failed | "你帮我打开个好吃的视频，在b站" | turn budget exhausted |
| 3 | failed | "没事的" | turn budget exhausted |
| 4 | failed | "你告诉我失败的原因吧" | turn budget exhausted |
| 5 | ok | "你告诉我原因就行为什么还调用这么多工具，直接说话就行" | - |
| 6 | **failed** | "当前屏幕共享是对你完全开发的" | **model call failed: invalid Messages SSE lifecycle or budget** |

### 关键发现

**Turn 6 的 SSE 错误**：
```
"error": "model call failed: invalid Messages SSE lifecycle or budget"
```

这是一个**明确的 SSE 流生命周期错误**，发生在用户继续对话时。

**上下文**：
- Turn 1-4：用户请求打开 bilibili 视频，失败（截图问题）
- Turn 5：用户让模型只说话不调用工具，成功
- Turn 6：用户说"屏幕共享是对你完全开放的"，**SSE 错误**

**根因推断**：
1. 可能不是"程序重启恢复会话"的问题（没有重启）
2. 更可能是**连续失败后的 SSE 流状态损坏**
3. 或者是**预算/生命周期管理的边界条件**

### Turn 2 的工具调用统计

| 工具 | 总调用 | 失败 |
|------|--------|------|
| desktop.application.list | 1 | 0 |
| desktop.application.open_uri | 1 | 0 |
| **desktop.screen.capture** | **2** | **2** |
| desktop.screen.list_displays | 1 | 0 |
| desktop.window.front | 1 | 0 |
| desktop.window.list | 2 | 0 |
| wait | 1 | 0 |

**截图失败 2/2**，导致预算耗尽。

## 会话 2：截图工具反复失败（02:41:52）

### 会话概要

| Turn | 状态 | 用户输入 | 错误 |
|------|------|---------|------|
| 1 | ok | （首轮，无用户输入） | - |
| 2 | failed | "我想让你用鼠标键盘工具，打开终端后，在mirage目录下打开我的vscode" | turn budget exhausted |
| 3 | failed | （继续） | turn budget exhausted |

### Turn 2 的工具调用统计

| 工具 | 总调用 | 失败 |
|------|--------|------|
| desktop.application.launch | 1 | 0 |
| desktop.application.list | 2 | 0 |
| **desktop.screen.capture** | **3** | **3** |
| desktop.screen.list_displays | 1 | 0 |
| desktop.window.front | 1 | 0 |
| desktop.window.list | 3 | 0 |
| wait | 1 | 0 |

**截图失败 3/3**，全部失败。

### 截图失败的详细错误

**第 1 次失败**：
```json
{
  "name": "desktop.screen.capture",
  "input": "{\"source\":\"desktop\",\"source_id\":\"eDP-1\"}",
  "output": "invalid capture source"
}
```

**第 2 次失败**：
```json
{
  "name": "desktop.screen.capture",
  "input": "{\"source\":\"display\",\"source_id\":\"eDP-1\"}",
  "output": "io_error"
}
```

**第 3 次失败**：
```json
{
  "name": "desktop.screen.capture",
  "input": "{\"source\":\"desktop\",\"source_id\":\"desktop\"}",
  "output": "invalid capture source"
}
```

### 关键发现

1. **错误码不一致**：
   - 第 1/3 次："invalid capture source"
   - 第 2 次："io_error"

   模型看到的是**不同的错误**，无法识别这是"同一类永久失败"。

2. **模型尝试了不同的参数**：
   - `source: "desktop"` + `source_id: "eDP-1"`
   - `source: "display"` + `source_id: "eDP-1"`
   - `source: "desktop"` + `source_id: "desktop"`

   这是**合理的尝试**，但平台根本不支持，应该在第一次失败时就明确告知。

3. **预算耗尽**：
   - 12 个工具调用（3 次截图失败）
   - 最终："turn budget exhausted before a final answer"
   - 用户看不到失败原因

## 问题验证

### 问题 1：SSE 流错误 ✅ 已证实

**证据**：会话 5 的 Turn 6
```
"error": "model call failed: invalid Messages SSE lifecycle or budget"
```

**新发现**：
- 不是"程序重启恢复会话"导致（会话 5 没有重启）
- 更可能是**连续失败后的流状态损坏**
- 或者是**预算管理的边界条件**（Turn 5 成功，Turn 6 立即 SSE 错误）

**修正分析**：
- 原分析假设是"恢复会话首轮"的问题
- 实际是**运行中的 SSE 流生命周期管理问题**
- 可能与前序失败、预算重置、或流状态不一致有关

### 问题 2：工具反复失败，会话终止 ✅ 已证实

**证据**：
- 会话 2：截图 3/3 失败 → 预算耗尽
- 会话 5：截图 2/2 失败 → 预算耗尽

**验证的根因**：
1. ✅ **错误码不一致**：同类失败返回不同错误（"invalid capture source" / "io_error"）
2. ✅ **模型合理尝试**：更换参数尝试不同组合，但都失败
3. ✅ **预算耗尽终止**：没有总结，只有"turn budget exhausted"
4. ✅ **用户无法理解**：失败原因对用户不透明

## 环境信息推断

### Display 信息
```json
{
  "displays": [{
    "id": "eDP-1",
    "origin_x": 0,
    "origin_y": 0,
    "width": 3200,
    "height": 2000,
    "primary": true
  }]
}
```

**推断**：
- 笔记本内置屏幕（eDP-1）
- 3200×2000 分辨率（高分屏）
- 可能是 Wayland 环境（截图全部失败）

### 截图失败的可能原因

基于错误信息推断：

1. **"invalid capture source"**：
   - `source: "desktop"` 不是有效的 source 类型？
   - 或者 Desktop Environment 未正确注册 source 类型

2. **"io_error"**：
   - 底层 X11/Wayland 调用失败
   - 可能是 Wayland XGetImage BadMatch（如之前分析）
   - 或者是权限问题

**需要进一步确认**：
- 当前桌面环境（GNOME Wayland / GNOME Xorg）
- 截图失败的底层日志（platform 层）
- `WAYLAND_DISPLAY` 和 `XDG_SESSION_TYPE` 环境变量

## 与原分析的对比

### 修正 1：SSE 错误不是"恢复会话首轮"

**原假设**：
- 程序重启后恢复会话，首轮 SSE 流订阅未重建

**实际情况**：
- 会话 5 没有重启
- SSE 错误发生在 Turn 6（运行中）
- 前序有 4 次预算耗尽失败 + 1 次成功

**新假设**：
- SSE 流在**连续失败/预算重置后的状态不一致**
- 或者是**预算计数器与流生命周期的边界条件**
- 或者是**取消/恢复机制的状态管理问题**

### 修正 2：截图失败的错误码问题

**原假设**：
- 所有 XGetImage 失败都返回 `io_error`

**实际情况**：
- 第 1/3 次："invalid capture source"
- 第 2 次："io_error"

**分析**：
- 可能 `source: "desktop"` 根本不被支持（参数校验失败）
- `source: "display"` 通过参数校验，但底层 I/O 失败
- 需要检查 `desktop_image_supply.cpp` 的 source 类型判定

## 更新的修复建议

### 紧急修复（不变）

1. ✅ Wayland 环境能力判定
2. ✅ 工具错误 UI 可见性
3. **新增**：SSE 流状态诊断和恢复

### SSE 流问题的新方向

**调查方向**：
1. 检查 `ModelLayer` 在预算耗尽后的流状态清理
2. 检查连续失败后 `ConversationLoop` 的重置逻辑
3. 检查 `DialogPreviewSink` 在失败/恢复时的有效性
4. 增加 SSE 流状态的健康检查（每次调用前验证）

**快速修复**：
```cpp
// 在 ModelLayer::complete_harness_turn 开始时
if (preview_sink && !validate_sink_reachability(preview_sink)) {
    log_warning("Preview sink unreachable, disabling streaming");
    preview_sink = nullptr;  // 降级为非流式
}
```

### 截图错误分类（不变）

工具失败三级分类仍然是核心修复，但需要特别关注：
- ✅ "invalid capture source" → `Permanent`（参数不支持）
- ✅ "io_error"（XGetImage 失败）→ `Permanent`（Wayland 限制）

## 下一步行动

### 立即验证

1. **确认桌面环境**：
   ```bash
   echo $WAYLAND_DISPLAY
   echo $XDG_SESSION_TYPE
   loginctl show-session $(loginctl | grep $(whoami) | awk '{print $1}') -p Type
   ```

2. **复现 SSE 错误**：
   - 尝试在运行中的会话触发多次预算耗尽
   - 观察是否出现 SSE 流错误

3. **查看平台日志**：
   - 截图失败时的底层错误（X11 error code / Wayland protocol error）

### 更新文档

- ✅ 创建本证据文档
- ⚠️ 更新 DEC-085：增加 SSE 运行时错误的分析
- ⚠️ 更新根因分析：修正 SSE 错误的场景

## 总结

两个实际会话完全验证了问题存在：

1. **SSE 流错误**：会话 5 Turn 6 出现"invalid Messages SSE lifecycle or budget"
   - **新发现**：不是恢复会话问题，是运行中的流状态管理问题
   - **影响更广**：任何长会话在失败后都可能触发

2. **工具反复失败**：会话 2/5 截图全部失败，预算耗尽终止
   - **已验证**：错误码不一致、模型合理尝试、预算耗尽无总结
   - **影响确认**：用户完全无法理解失败原因

**优先级调整**：
- SSE 流问题应提升为**高优先级**（影响运行中的会话，不只是恢复）
- 工具失败分类保持**高优先级**（影响所有桌面任务）

修复方案的总体方向仍然正确，但需要调整 SSE 问题的调查范围。
