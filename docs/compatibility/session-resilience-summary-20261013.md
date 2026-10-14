# 会话韧性问题分析总结

> 日期：2026-10-13
> 负责人：Mirage 维护者
> 类型：问题分析与修复建议

## 问题概述

最近两次 Mirage 会话暴露了关键的韧性问题：

### 问题 1：程序重启后恢复会话的 Model SSE 流错误

**症状**：用户重启程序后点击历史会话，发送消息时出现 SSE 流错误。

**根因**（待实际复现验证）：
- 会话恢复路径未重建 `DialogPreviewSink` 流式订阅
- 持久化上下文与 Mira `TaskId` 运行身份不一致
- UI 订阅回调在进程重启后失效

**影响**：用户无法继续使用历史会话，体验断裂。

### 问题 2：工具执行失败导致会话直接终止

**案例**：2026-10-11 bilibili 任务（gui-session-failure-20261011.md）
- Wayland 环境下，5 次全屏截图请求全部失败（`io_error`）
- 模型反复 wait → observe → screenshot → 失败，消耗 16 步预算
- 最终以 `turn budget exhausted before a final answer` 终止
- 用户看不到失败原因是"Wayland 不支持 X11 根窗口截图"

**根因**：
1. **平台能力误判**：`LinuxDesktopEnvironment` 在 Wayland 下误报完整 X11 能力
   - 检测逻辑：`XOpenDisplay()` 成功即认为 screen Provider 可用
   - 实际限制：Wayland 下 `XGetImage` 根窗口返回 BadMatch（error=8）

2. **工具失败未分类**：所有 XGetImage 错误都是 `io_error`，模型无法区分瞬时失败与永久限制

3. **无反复失败阻止**：同类工具在同 epoch 下允许无限重试，无历史失败计数

4. **预算耗尽即终止**：`max_turns=16` 触发后立即返回 Incomplete，无"最后一轮总结"机会

**影响**：
- Wayland 用户的截图任务必然失败
- 失败原因不透明（"预算耗尽"不告知根本原因）
- 其他确定性工具失败（无权限、不支持输入法）同样导致预算耗尽

## M7 阶段修复的覆盖范围

### M7-14 已验收内容

DEC-068（bundled-vision-and-tool-recovery-20261011.md）：
- ✅ 512 字节 canonical 回执
- ✅ 拒绝→新观察→已验证纠正（权限弹窗场景）
- ✅ 无副作用工具可在新观察后重试

### 未覆盖的场景

M7-14 的"可恢复工具拒绝"仅定义了**瞬时失败**（权限弹窗、窗口焦点丢失），
未定义**永久失败**（平台限制、非法参数）的分类和处理策略。

**为什么最新会话未体现修复**：
1. Wayland 能力判定在 M7-10（DEC-073/074），2026-10-11 时未完成
2. XGetImage BadMatch 是平台限制，不是权限拒绝，M7-14 逻辑未触发
3. `max_turns=16` 在图像任务中是紧预算，无空间给模型调整策略

**MIRA-20261012-001 网络重试修复范围**：
- ✅ 网络阶段超时标记为 `retryable=true`
- ✅ `max_recoveries` 按信封阈值启用（turn_capacity ≥ 10 时为 2）
- ❌ 仅覆盖网络层瞬时故障，不覆盖工具执行失败或平台能力缺失

## 修复方案概要

### 短期方案（1-2 天，紧急修复）

**1. 会话恢复首轮降级为非流式**
```cpp
// 恢复会话时设置标志
session.first_turn_after_restore = true;

// 首次调用时禁用流式
DialogPreviewSink preview = session.first_turn_after_restore 
    ? nullptr 
    : session.preview_sink;
```
- 优点：实施简单，零风险，可立即解决
- 缺点：首轮无打字效果（用户需等待完整响应）

**2. Wayland 环境能力判定**
```cpp
bool is_wayland = std::getenv("WAYLAND_DISPLAY") || 
                  (std::getenv("XDG_SESSION_TYPE") == "wayland");

if (XOpenDisplay() && is_wayland) {
    screen_capability_ = ScreenCapability::PortalOnly;
} else if (XOpenDisplay()) {
    screen_capability_ = ScreenCapability::Full;
}
```

**3. 工具错误 UI 可见性**
- 工具失败时向 UI 推送 `ToolErrorEvent`
- 显示红色错误卡片："截图失败（平台限制）：当前环境不支持全屏截图，请使用窗口截图"

### 中期方案（3-5 天，核心韧性）

**4. 工具失败三级分类**
```cpp
enum class ToolFailureLevel {
    Transient,    // 瞬时可恢复（权限弹窗）
    Conditional,  // 条件可恢复（需授权、需启动应用）
    Permanent     // 永久不可恢复（平台限制、非法参数）
};
```

**5. 反复失败阻止机制**
- 维护 `ToolFailureTracker`，记录每 epoch 的工具失败次数
- `Permanent` 失败 ≥3 次 → 阻止该工具后续调用
- 新 epoch 时清零计数，但保留阻止列表

**6. 预算优雅降级**
- `remaining_turns <= 2` 时，向模型注入系统提示："请总结尝试了什么、为何失败"
- 最后一轮禁用工具，强制模型生成文本回复
- 失败时 `completion.error` 包含已执行动作摘要

### 长期方案（5-7 天，根本解决）

**7. SSE 流重建**（需调查后实施）
- 会话恢复时重建 `preview_sink` 和 `process_sink`
- 验证订阅可达性，失败则降级
- 确保 `TaskId` 正确传递

**8. M7-14 修复验证**
- Xorg 对照实验（重跑 bilibili 任务）
- Portal 授权路径验证
- Level 2/3 失败回归测试

## 成功指标

| 指标 | 当前 | 目标 |
|------|------|------|
| 会话恢复成功率 | 未知 | 95%+ |
| 工具失败后任务完成率 | 0%（必然耗尽预算） | 60%+（给出总结） |
| 确定性失败的无效重试次数 | 5+ 次 | ≤3 次 |
| 用户可见失败原因比例 | 0%（"预算耗尽"） | 100%（明确错误） |

## 实施时间估算

| 阶段 | 开发 | 测试 | 评审 | 总计 |
|------|------|------|------|------|
| 短期（紧急） | 1 天 | 0.5 天 | 0.5 天 | 2 天 |
| 中期（核心） | 3 天 | 1.5 天 | 0.5 天 | 5 天 |
| 长期（根本） | 4 天 | 2 天 | 1 天 | 7 天 |
| **总计** | **8 天** | **4 天** | **2 天** | **14 天** |

## 关键交付件

已创建以下文档：

1. **[DEC-085](../decisions/DEC-085-session-loop-resilience-and-tool-failure-handling.md)**
   - 决策文档，定义工具失败三级分类和恢复策略
   - 状态：Proposed，待维护者评审

2. **[session-resilience-fix.md](../plans/session-resilience-fix.md)**
   - 详细实施计划，包含代码示例和验收标准
   - 状态：Proposed

3. **[session-failure-root-cause-analysis-20261013.md](../compatibility/session-failure-root-cause-analysis-20261013.md)**
   - 根因分析，回答"为什么工具反复失败"和"M7 修复为何未生效"
   - 状态：完成

## 推荐行动

**立即行动**（Day 1-2）：
1. 实施会话恢复首轮降级（方案 1）
2. 修复 Wayland 能力判定（方案 2）
3. 增加工具错误 UI 可见性（方案 3）

**后续推进**（Day 3-7）：
4. 实施工具失败三级分类（方案 4）
5. 实施反复失败阻止机制（方案 5）
6. 实施预算优雅降级（方案 6）

**根本解决**（Day 8-14）：
7. 调查并实施 SSE 流重建（方案 7）
8. 验证 M7-14 修复在扩展场景中的有效性（方案 8）
9. 构建会话恢复回归测试套件

## 风险与依赖

**技术风险**：
- SSE 流重建失败可能影响所有会话（缓解：短期降级方案作为回退）
- 工具失败计数器状态管理复杂（缓解：先实施简化版）
- Wayland 环境检测可能误判（缓解：多环境测试）

**依赖**：
- Mira：确认是否支持 `PermanentFailure` 错误码（可能需要上游扩展）
- Runtime Service：IPC 协议扩展（工具错误事件）
- UI 层：工具错误卡片渲染（Native + Web 双前端）

## 总结

两个问题的根源都是**韧性不足**：

1. **SSE 流重建缺失** → 会话恢复失败 → 用户无法继续使用
2. **工具失败处理不完善** → 确定性失败循环 → 预算耗尽终止

M7-14 的修复在其定义范围内（瞬时权限失败）是有效的，但未覆盖永久平台限制。
需要扩展工具失败分类，增加反复失败阻止和预算优雅降级机制。

**优先级建议**：短期方案是紧急修复，可在 1-2 天内解决用户痛点；中期方案是核心韧性，
应在 1 周内完成；长期方案是根本解决，可与其他 M7 工作并行推进。

下一步由 Mirage 维护者评审 DEC-085 决策文档并授权实施。
