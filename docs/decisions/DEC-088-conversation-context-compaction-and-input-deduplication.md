# DEC-088：会话上下文压缩与输入去重

> 状态：Proposed
> 日期：2026-10-14
> 负责人：Mira/Mirage 维护者
> 前置：DEC-087（输出预算与 preflight）、DEC-085（会话韧性）、DEC-053（图片供给缝）
> 工作项：待分配（建议归入 M9 多步任务稳定性）

## 问题陈述

DEC-087 已对齐 ZCode 的输出预算管理（声明值优先 + preflight 收缩 + 续写深度 3），
但**输入侧仍无主动压缩机制**，导致以下问题：

### 问题 1：图片重复分析的 token 浪费

**现象**（从会话诊断观察）：
- Mirage 桌面任务中，模型反复分析"截图身份重复"问题
- 实际是图片供给去重的预期行为（DEC-055：只保留最新一张，内容相同复用 artifact）
- 但模型在每轮都看到**相同的 `artifact://abc123`**，无法区分是"上轮同图"还是"新拍恰好相同"
- 导致重复的"让我分析屏幕内容"思考，浪费 10-20% 思考 token

**根因**：
`ModelInputSupplier` 回调（DEC-053）只提供 `ImagePart` + ArtifactRef，
**无跨轮身份连续性标记**——模型无法知道这是第 N 次看到同一张截图。

### 问题 2：工具历史的线性累积

**现象**：
- 桌面任务 15 轮后，完整回放 30+ 个 ToolCall/ToolResult pairs
- 失败场景更严重：7 次截图失败 × 完整 error 回执，占用大量上下文
- 早期轮次的工具细节对后续决策价值递减，但仍完整保留

**根因**：
ConversationLoop 的 tool-history 回放策略是**完整序列化所有工具往返**，
无分级摘要或淘汰机制。

### 问题 3：接近窗口上限时的被动失败

**现象**：
- Preflight 收缩只能延缓截断，余量 < 1000 后每轮必截断
- 续写 3 次耗尽后显式失败，无压缩自救
- 10-15 轮图片密集任务必然失败（DEC-087 明确"超窗输入在 Mirage 仍显式失败"）

**根因**：
无 ZCode 的三级 compact（micro/auto/reactive），只能被动续写或放弃。

### 问题 4：模型输出截断时的质量螺旋

**现象**（DEC-085 会话证据）：
- 思考档模型输出超过 32768 被截断
- 续写时模型尝试"继续思考"，再次截断
- 3 次续写耗尽，整轮失败，用户看不到任何有用输出

**根因**：
续写提示词是通用的"从截断处继续"，模型不理解**应该切换到简洁模式**。

## 决策

在 **pinned mira** 的 `ConversationLoop` 层实现三级上下文管理，不在 Mirage 适配层
临时绕过（避免与 mira 预算语义双轨并行）：

### 1. 输入去重标注（Input Deduplication Annotation）

**目标**：让模型识别"这是第 N 次看到同一输入"，避免重复分析。

**实现**：
- `ModelInputSupplier` 回调返回值扩展为 `struct SuppliedInput`：
  ```cpp
  struct SuppliedInput {
      std::vector<ModelInputItem> items;  // 既有
      std::optional<InputReuseMeta> reuse_meta;  // 新增
  };
  
  struct InputReuseMeta {
      std::string content_digest;   // 本轮供给内容的摘要
      uint32_t reuse_count;         // 连续复用次数（0=首次）
      std::string semantic_label;   // "same-screenshot-3x" / "new-screenshot"
  };
  ```

- ConversationLoop 在 build_request 时：
  - 比对本轮 `content_digest` 与上轮记录
  - 若相同：`reuse_count++`；若不同：`reuse_count=0`
  - `reuse_count >= 2` 时，在 User envelope 末尾追加一条 **TextPart 提示**：
    ```
    [System note: The screenshot above is the same as in the previous 3 turns. 
     No need to re-analyze unless the task explicitly requires it.]
    ```

**边界**：
- 提示词不进入 System role（保持 RULE-09：外部内容不提升权限）
- 作为 User envelope 的**最后一个 part**，模型可选择性使用
- 只标注图片复用，不标注文本复用（文本本身 token 成本低）

**验收**：
- 相同截图连续 3 轮供给，第 3 轮请求包含 reuse 提示
- 模型在后续 turn 中不再输出"让我先分析屏幕内容"
- 思考 token 降低 10-20%（通过对照实验验证）

### 2. 工具历史的微压缩（Micro-Compaction）

**目标**：即时摘要工具结果，保留决策连续性，降低上下文基线。

**策略**：
- **最近 2 轮**：完整保留 `ToolCallPart` + `ToolResultPart`
- **第 3-5 轮**：保留 `ToolCallPart` + **摘要 ToolResultPart**
  ```json
  {
    "call_id": "...",
    "output": "[Compacted] screenshot succeeded, 3200x2000, artifact:abc123 (full result in turn N-3)",
    "compacted": true,
    "original_size": 1234
  }
  ```
- **第 6+ 轮**：**折叠为单条文本**
  ```
  [Earlier tool executions: Turn 1-5: 3 screenshots succeeded, 2 clicks executed]
  ```

**实现位置**：
`ConversationLoop::build_request()` 在序列化 tool-history 时：
- 维护 per-turn 的工具执行摘要
- 根据 turn 距离当前轮的偏移应用不同策略
- 摘要长度上限 200 字符/工具

**失败场景特殊处理**：
同类工具连续失败 ≥3 次（DEC-085 已有计数器），后续轮次只保留：
```
[Tool blocked] screenshot failed 3x in turns N-2 to N (platform_not_supported), 
subsequent calls rejected.
```

**验收**：
- 15 轮桌面任务，第 15 轮请求的 tool-history 长度 < 5 轮完整回放的 40%
- 模型仍能引用早期轮次的关键动作（"你在 turn 3 打开了 Firefox"）
- Micro-compact 开销 < 10ms/轮

### 3. 自动压缩阈值（Auto-Compaction Threshold）

**目标**：在达到上下文窗口 40-55% 时主动压缩，避免接近上限时的被动失败。

**触发条件**：
```cpp
bool should_compact = 
    context_window > 0 &&  // 已声明窗口
    (estimated_input_tokens / context_window) >= kAutoCompactThreshold;  // 默认 0.45
```

**压缩策略**：
1. **保留区**（不压缩）：
   - 最近 3 轮完整内容
   - Goal 与最新的 recovery feedback

2. **摘要区**（深度压缩）：
   - 第 4-10 轮：每轮压缩为 ≤300 字符摘要
   - 第 11+ 轮：整体折叠为单段（≤500 字符）

3. **摘要生成**：
   - 优先保留：turn 结果（Answered/Failed）、关键工具名称、epoch 变化
   - 丢弃：thinking 详细过程、工具参数细节、长错误栈
   - 模板：
     ```
     [Turn N] User: {first_50_chars}... → {3 tools executed} → Assistant: {outcome_summary}
     ```

**实现**：
- `ConversationLoop` 新增 `ContextCompactor` 接口（可选依赖，默认实现为上述策略）
- 压缩后发布 `ContextCompacted` 事件（before/after token count, compacted_turns）
- 压缩状态不持久化：每次 run() 基于当前 message history 重新计算

**边界**：
- 压缩只影响**本轮请求的序列化形式**，不修改 message history 存储
- 下一轮仍从完整 history 重新压缩（幂等）
- 压缩后无法利用 provider cache hit（trade-off：续航 > 缓存）

**验收**：
- 30 轮图片任务，第 20 轮触发 auto-compact
- 压缩后 input_tokens 降至窗口 50% 以下
- 任务成功完成，不因上下文耗尽失败

### 4. 续写提示的差异化（Continuation Feedback Specialization）

**目标**：截断后的续写提示根据**截断类型**差异化，提高收敛概率。

**当前问题**（DEC-085 已修复部分）：
Mira PR #93 已区分三类截断形态（部分文本 / 工具截断 / 仅思考截断），
但恢复提示仍是通用的"continue from where you left off"。

**优化**：
- **仅思考截断**（thinking 超预算，无 text/tool_use）：
  ```
  Your reasoning was cut off due to length. Please provide a concise plain-text 
  response now, without further extended thinking.
  ```

- **部分文本截断**（有部分 text，无 tool_use）：
  ```
  Your response was cut off. Continue from: "{last_50_chars}..."
  ```

- **工具调用截断**（ToolCallPart 不完整）：
  ```
  Your tool call was cut off and discarded. Please retry with a complete, 
  valid tool call, or provide a text response instead.
  ```

**实现位置**：
`ConversationLoop` 在构造 recovery feedback 时，根据 `IncompleteReason::MaxOutputTokens`
的响应结构选择模板。

**验收**：
- 思考截断后，模型在续写时直接输出简短文本，不再尝试继续思考
- 续写成功率从当前 30% 提升至 60%+

### 5. 极端收缩时的图片降级（Image Degradation on Extreme Preflight）

**目标**：preflight 余量 < 阈值时，自动降级图片为文本描述，避免必然截断。

**触发条件**：
```cpp
int64_t remaining = context_window - estimated_input - 1000;
if (remaining < kImageDegradationThreshold) {  // 默认 10000
    degrade_images_to_text = true;
}
```

**降级策略**：
- **余量 5000-10000**：保留图片，但在其后追加警告
  ```
  [Context budget low: if analysis is not essential, you may skip detailed 
   image inspection]
  ```

- **余量 < 5000**：替换 `ImagePart` 为 `TextPart`
  ```
  [Image degraded due to context limit] Screenshot from turn N, 3200x2000 pixels.
  Previous analysis: {last_ocr_summary_if_any}
  ```

**实现位置**：
`ModelInputSupplier` 在 Mirage 侧实现（图片供给是下游职责），
根据 `ObservedProvider` 传递的 preflight 余量决策。

**验收**：
- 余量 8000 时，图片保留 + 警告
- 余量 3000 时，图片降级为文本
- 降级后任务仍可继续，不立即失败

## 实施边界

1. **Mira 本地修改**（`third_party/mira`）：
   - 能力 1/2/3/4 在 `ConversationLoop` 实现
   - 修改前**必须**先在 `docs/executor_feedback/ledger.md` 登记（即使是 mira 本地，
     也要保持可追溯；待上游合并时作为 PR 描述）
   - 单元测试覆盖：压缩策略、reuse 标注、续写提示选择

2. **Mirage 集成层**（`integration/mira`）：
   - 能力 5 在 `desktop_image_supply.cpp` 实现
   - 依赖 mira 的 preflight 余量暴露（可能需要 mira 侧扩展接口）

3. **不做的**：
   - 不实现 LLM-based 摘要（成本高、延迟大）——只做规则式压缩
   - 不持久化压缩状态——每次从完整 history 重新计算（幂等）
   - 不在 Mirage 层实现通用压缩器——只做图片降级特化

## 验收标准

### 短期验收（能力 1/4/5，1 周内）

1. **图片复用标注**：
   - [ ] 相同截图连续 3 轮，第 3 轮请求包含 reuse 提示
   - [ ] 对照实验：有/无标注的思考 token 差异 ≥10%

2. **续写提示差异化**：
   - [ ] 思考截断场景，续写提示包含"concise plain-text"
   - [ ] 续写成功率 ≥60%（当前基线 30%）

3. **图片降级**：
   - [ ] 余量 8000：图片保留 + 警告
   - [ ] 余量 3000：图片降级为文本，任务继续

### 中期验收（能力 2/3，2-3 周内）

4. **工具历史微压缩**：
   - [ ] 15 轮任务，第 15 轮 tool-history tokens < 完整回放的 40%
   - [ ] 模型仍能引用 turn 3 的关键动作
   - [ ] Micro-compact 开销 < 10ms/轮

5. **自动压缩阈值**：
   - [ ] 30 轮图片任务，input_tokens 曲线在第 15 轮后趋平（压缩生效）
   - [ ] 第 30 轮仍 < 窗口 60%
   - [ ] 任务成功完成，不因上下文耗尽失败

### 端到端验收（全能力，1 月内）

6. **真实桌面任务**：
   - [ ] bilibili 视频查找任务（DEC-085 失败案例）成功完成
   - [ ] 20+ 轮多步任务稳定运行（M9 目标）
   - [ ] 平均 turn input tokens < 当前基线 50%

## 风险与替代方案

### 风险 1：压缩后模型理解下降

**缓解**：
- 保留最近 3 轮完整内容（短期记忆）
- 摘要保留关键要素（工具名称、epoch、结果）
- 对照实验验证任务成功率无显著下降

### 风险 2：压缩开销过高

**缓解**：
- 规则式压缩，无 LLM 调用
- 目标 < 10ms/轮（preflight 本身已有估算开销）
- 只在 auto-compact 触发时深度压缩

### 风险 3：与 provider cache 冲突

**接受**：
- 压缩后前缀不匹配，无法 cache hit
- Trade-off：长会话续航 > 单轮缓存节省
- 前 10 轮（未触发 auto-compact）仍可利用 cache

### 替代方案：不做压缩，提高轮数上限

**落选理由**：
- 输入侧线性累积，最终仍会耗尽
- 图片任务 1600 tokens/图，15 轮 = 24000 tokens（仅图片）
- 加上工具历史、thinking，必然超窗

## 参考

- [DEC-087](DEC-087-zcode-aligned-output-budget-and-context-preflight.md)：
  输出预算与 preflight（本决策的输入侧对偶）
- [DEC-085](DEC-085-session-loop-resilience-and-tool-failure-handling.md)：
  会话韧性（续写、失败分类）
- [DEC-053](../../../third_party/mira/docs/decisions/DEC-053-conversation-image-supply-seam.md)：
  图片供给缝（本决策扩展其 reuse 语义）
- [session-failure-final-report-20261013.md](../compatibility/session-failure-final-report-20261013.md)：
  实际会话证据（问题来源）
- ZCode `compact.ts`：三级压缩参考实现（micro/auto/reactive）
