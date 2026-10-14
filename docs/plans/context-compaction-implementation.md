# 会话上下文压缩与输入去重：实施计划

> 日期：2026-10-14
> 决策依据：[DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
> 负责人：Mira/Mirage 维护者
> 目标：M9 多步任务稳定性（20+ 轮图片密集任务不因上下文耗尽失败）

## 实施优先级与依赖

```
Phase 1 (P0, 1周)        Phase 2 (P1, 2周)        Phase 3 (P2, 1周)
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│ 1. 图片复用标注  │ ───> │ 3. 工具历史压缩  │ ───> │ 5. 端到端验证    │
│ 2. 续写提示优化  │      │ 4. 自动压缩阈值  │      │ 6. 性能调优      │
│ 3. 图片降级策略  │      │                 │      │ 7. 文档更新      │
└─────────────────┘      └─────────────────┘      └─────────────────┘
```

## Phase 1：立即修复（P0，Day 1-5）

### 1.1 图片复用标注（mira 本地）

**工作量**：2 天  
**位置**：`third_party/mira/src/conversation_loop.cpp`

**步骤**：

#### Step 1.1.1：扩展 ModelInputSupplier 接口（0.5 天）

```cpp
// third_party/mira/include/mira/conversation_loop.hpp

struct InputReuseMeta {
    std::string content_digest;   // SHA256 of supplied content
    uint32_t reuse_count;         // 0=first time, 1+=reused N times
    std::string semantic_label;   // Human-readable label
};

struct SuppliedInput {
    std::vector<ModelInputItem> items;
    std::optional<InputReuseMeta> reuse_meta;
};

using ModelInputSupplier = std::function<Result<SuppliedInput>(const AgentLoopSpec&)>;
```

**修改文件**：
- `mira/include/mira/conversation_loop.hpp`（接口定义）
- `mira/src/conversation_loop.cpp`（实现）

**兼容性**：
- 保留既有 `std::function<Result<std::vector<ModelInputItem>>(...)>` 签名支持
- 新签名通过 `std::variant` 或重载区分
- 未提供 `reuse_meta` 时行为与现状完全一致

#### Step 1.1.2：实现复用检测逻辑（0.5 天）

```cpp
// third_party/mira/src/conversation_loop.cpp

class ConversationLoop::Impl {
    struct ReuseTracker {
        std::string last_digest;
        uint32_t consecutive_count = 0;
        
        void update(const std::string& current_digest) {
            if (current_digest == last_digest && !current_digest.empty()) {
                consecutive_count++;
            } else {
                consecutive_count = 0;
                last_digest = current_digest;
            }
        }
    };
    
    ReuseTracker input_reuse_;
    
    // In build_request():
    void inject_reuse_annotation(std::vector<ModelInputPart>& user_envelope,
                                  const InputReuseMeta& meta) {
        if (meta.reuse_count >= 2) {  // 第3次及以后标注
            std::string note = std::format(
                "[System note: The {} above is the same as in the previous {} turns. "
                "No need to re-analyze unless the task explicitly requires it.]",
                meta.semantic_label, meta.reuse_count
            );
            user_envelope.emplace_back(TextPart{.text = note});
        }
    }
};
```

**关键点**：
- 摘要算法：SHA256(所有 ImagePart 的 artifact_id 拼接)
- 只标注 `reuse_count >= 2`（第 3 次及以后）
- 提示作为 User envelope 最后一个 part（不进入 System role）

#### Step 1.1.3：Mirage 侧集成（0.5 天）

```cpp
// mirage/integration/mira/src/desktop_image_supply.cpp

class DesktopImageSupply {
    std::string last_artifact_digest_;
    uint32_t reuse_generation_ = 0;
    
    Result<SuppliedInput> supply_for_turn(const AgentLoopSpec& spec) {
        auto current_digest = compute_digest(current_artifacts_);
        
        InputReuseMeta meta;
        meta.content_digest = current_digest;
        
        if (current_digest == last_artifact_digest_ && !current_digest.empty()) {
            reuse_generation_++;
        } else {
            reuse_generation_ = 0;
        }
        
        meta.reuse_count = reuse_generation_;
        meta.semantic_label = reuse_generation_ > 0 
            ? "screenshot" 
            : "new screenshot";
        
        last_artifact_digest_ = current_digest;
        
        return SuppliedInput{
            .items = build_image_parts(),
            .reuse_meta = meta
        };
    }
};
```

**关键点**：
- 摘要算法与 mira 侧一致（SHA256）
- epoch 变化时清零 `reuse_generation_`
- 语义标签："screenshot" / "new screenshot"

#### Step 1.1.4：单元测试（0.5 天）

**测试覆盖**：
- `conversation_loop_input_reuse_test.cpp`：
  - [ ] 首次供给：`reuse_count=0`，无标注
  - [ ] 第 2 次相同：`reuse_count=1`，无标注
  - [ ] 第 3 次相同：`reuse_count=2`，有标注
  - [ ] 内容变化：`reuse_count` 清零
  - [ ] 未提供 `reuse_meta`：行为不变（兼容性）

- `desktop_image_supply_test.cpp`：
  - [ ] epoch 变化时清零计数
  - [ ] 摘要算法正确性

**验收**：
- [ ] 所有测试通过（ASAN/UBSAN）
- [ ] 代码覆盖率 ≥90%

---

### 1.2 续写提示差异化（mira 本地）

**工作量**：1 天  
**位置**：`third_party/mira/src/conversation_loop.cpp`

**步骤**：

#### Step 1.2.1：扩展截断类型识别（0.5 天）

```cpp
// third_party/mira/src/conversation_loop.cpp

enum class TruncationType {
    ThinkingOnly,     // 只有 ThinkingPart，无 text/tool_use
    PartialText,      // 有部分 text，无 tool_use
    ToolCallCutoff,   // ToolCallPart 不完整
    Mixed             // 多种内容混合截断
};

TruncationType classify_truncation(const ModelResponse& response) {
    bool has_thinking = !response.thinking_parts.empty();
    bool has_text = !response.text_parts.empty();
    bool has_tool = !response.tool_call_parts.empty();
    bool tool_incomplete = has_tool && /* check incomplete flag */;
    
    if (has_thinking && !has_text && !has_tool) {
        return TruncationType::ThinkingOnly;
    }
    if (has_text && !has_tool) {
        return TruncationType::PartialText;
    }
    if (tool_incomplete) {
        return TruncationType::ToolCallCutoff;
    }
    return TruncationType::Mixed;
}
```

**关键点**：
- 复用 PR #93 已有的截断检测逻辑
- 只扩展分类，不改变截断处理流程

#### Step 1.2.2：实现差异化提示模板（0.3 天）

```cpp
std::string build_continuation_prompt(TruncationType type, 
                                      const ModelResponse& response) {
    switch (type) {
    case TruncationType::ThinkingOnly:
        return "Your reasoning was cut off due to output length limit. "
               "Please provide a concise plain-text response now, "
               "without further extended thinking.";
    
    case TruncationType::PartialText: {
        auto last_50 = get_last_n_chars(response.text_parts.back().text, 50);
        return std::format(
            "Your response was cut off. Continue from: \"{}...\"",
            last_50
        );
    }
    
    case TruncationType::ToolCallCutoff:
        return "Your tool call was incomplete and discarded. "
               "Please retry with a complete, valid tool call, "
               "or provide a text response instead.";
    
    case TruncationType::Mixed:
        return "Your response was cut off. Please continue or "
               "provide a concise summary to conclude.";
    }
}
```

**关键点**：
- 思考截断：明确要求"concise plain-text"
- 文本截断：提供上下文（最后 50 字符）
- 工具截断：说明已丢弃，需重试

#### Step 1.2.3：集成到恢复流程（0.2 天）

```cpp
// In handle_incomplete_response():
if (reason == IncompleteReason::MaxOutputTokens) {
    auto truncation_type = classify_truncation(response);
    auto prompt = build_continuation_prompt(truncation_type, response);
    
    recovery_feedback.text = prompt;
    recovery_feedback.provenance = "mira.conversation-loop.continuation.v2";
}
```

**验收**：
- [ ] 思考截断场景：提示包含"concise plain-text"
- [ ] 文本截断场景：提示包含最后 50 字符
- [ ] 工具截断场景：提示说明已丢弃

---

### 1.3 图片降级策略（Mirage 集成层）

**工作量**：1.5 天  
**位置**：`mirage/integration/mira/src/desktop_image_supply.cpp`

**步骤**：

#### Step 1.3.1：暴露 preflight 余量（mira 本地，0.5 天）

```cpp
// third_party/mira/include/mira/conversation_loop.hpp

struct AgentLoopSpec {
    // 既有字段...
    
    // 新增：供 supplier 使用的预算提示
    struct BudgetHint {
        int64_t estimated_input_tokens;
        int64_t context_window_tokens;  // 0=未声明
        int64_t preflight_remaining;    // 窗口 - 已用 - 1000
    };
    std::optional<BudgetHint> budget_hint;
};
```

**关键点**：
- 只读提示，不改变预算决策权（仍在 ObservedProvider）
- `preflight_remaining` 计算与 DEC-087 一致

#### Step 1.3.2：Mirage 侧降级实现（0.7 天）

```cpp
// mirage/integration/mira/src/desktop_image_supply.cpp

Result<SuppliedInput> DesktopImageSupply::supply_with_degradation(
    const AgentLoopSpec& spec) {
    
    constexpr int64_t kWarnThreshold = 10000;
    constexpr int64_t kDegradeThreshold = 5000;
    
    int64_t remaining = spec.budget_hint 
        ? spec.budget_hint->preflight_remaining 
        : INT64_MAX;
    
    SuppliedInput result;
    
    if (remaining >= kWarnThreshold) {
        // 正常供给图片
        result.items = build_image_parts();
    } 
    else if (remaining >= kDegradeThreshold) {
        // 保留图片 + 警告
        result.items = build_image_parts();
        result.items.push_back(TextPart{
            .text = "[Context budget low: if detailed image analysis is not "
                    "essential, you may skip it and proceed with text-based actions]"
        });
    } 
    else {
        // 降级为文本描述
        result.items = build_degraded_text_description();
    }
    
    return result;
}

std::vector<ModelInputItem> build_degraded_text_description() {
    std::string desc = std::format(
        "[Image degraded due to context limit] Screenshot from turn {}, "
        "{}x{} pixels. {}",
        current_turn_,
        last_capture_.width, last_capture_.height,
        last_ocr_summary_.empty() ? "No previous OCR available." 
                                  : "Last OCR: " + last_ocr_summary_
    );
    return {ModelInputItem{TextPart{.text = desc}}};
}
```

**关键点**：
- 阈值：警告 10000，降级 5000（可配置）
- 降级时尝试复用上轮 OCR 结果
- 明确标注"Image degraded"，不伪装为正常文本

#### Step 1.3.3：测试（0.3 天）

**测试覆盖**：
- [ ] 余量 15000：正常供给图片
- [ ] 余量 8000：图片 + 警告
- [ ] 余量 3000：文本描述
- [ ] 未声明窗口：正常供给（不降级）

**验收**：
- [ ] 降级后任务继续执行，不立即失败
- [ ] 降级事件正确记录（日志/事件面）

---

### Phase 1 验收里程碑（Day 5）

- [ ] 图片复用标注：相同截图第 3 次出现时有提示
- [ ] 续写提示差异化：思考截断提示包含"concise"
- [ ] 图片降级：余量 < 5000 时降级为文本
- [ ] 所有单元测试通过（ASAN/UBSAN/TSAN）
- [ ] 对照实验：有/无标注的思考 token 差异 ≥10%

---

## Phase 2：核心压缩（P1，Day 6-15）

### 2.1 工具历史微压缩（mira 本地）

**工作量**：3 天  
**位置**：`third_party/mira/src/conversation_loop.cpp`

**步骤**：

#### Step 2.1.1：设计压缩策略数据结构（0.5 天）

```cpp
// third_party/mira/src/conversation_loop.cpp

struct ToolHistoryCompactor {
    struct TurnSummary {
        uint32_t turn_index;
        std::vector<std::string> tool_names;    // ["screenshot", "click"]
        std::vector<std::string> outcomes;      // ["success", "failed"]
        size_t original_token_estimate;
        std::string compacted_text;             // 压缩后的文本
    };
    
    enum class CompactionLevel {
        Full,      // 最近 2 轮：完整保留
        Summary,   // 第 3-5 轮：摘要 result
        Collapsed  // 第 6+ 轮：折叠为单段
    };
    
    std::vector<TurnSummary> turn_summaries_;
    
    CompactionLevel decide_level(uint32_t turn_age) const {
        if (turn_age <= 2) return CompactionLevel::Full;
        if (turn_age <= 5) return CompactionLevel::Summary;
        return CompactionLevel::Collapsed;
    }
};
```

#### Step 2.1.2：实现摘要生成（1 天）

```cpp
std::string compact_tool_result(const ToolResultPart& result, 
                                CompactionLevel level) {
    switch (level) {
    case CompactionLevel::Full:
        return result.output;  // 原样保留
    
    case CompactionLevel::Summary: {
        // 摘要：保留关键信息，限制长度
        std::string summary = std::format(
            "[Compacted] {} ({}), {} bytes",
            extract_outcome(result.output),  // "succeeded" / "failed: reason"
            result.call_id.substr(0, 8),
            result.output.size()
        );
        return truncate_to_length(summary, 200);
    }
    
    case CompactionLevel::Collapsed:
        // 折叠：只统计，不保留细节
        return "";  // 由 collapse_multiple_turns() 统一生成
    }
}

std::string collapse_multiple_turns(
    const std::vector<TurnSummary>& turns) {
    
    std::map<std::string, uint32_t> tool_counts;
    uint32_t success_count = 0;
    uint32_t failed_count = 0;
    
    for (const auto& turn : turns) {
        for (const auto& tool : turn.tool_names) {
            tool_counts[tool]++;
        }
        for (const auto& outcome : turn.outcomes) {
            if (outcome.starts_with("success")) success_count++;
            else failed_count++;
        }
    }
    
    std::vector<std::string> parts;
    for (const auto& [tool, count] : tool_counts) {
        parts.push_back(std::format("{} x", tool, count));
    }
    
    return std::format(
        "[Earlier tool executions, turns {}-{}] {}: {} succeeded, {} failed",
        turns.front().turn_index,
        turns.back().turn_index,
        join(parts, ", "),
        success_count,
        failed_count
    );
}
```

**关键点**：
- 摘要长度上限 200 字符/工具
- 保留 call_id 前 8 字符（便于追溯）
- 折叠时统计成功/失败次数

#### Step 2.1.3：集成到 build_request（0.5 天）

```cpp
// In ConversationLoop::build_request():

std::vector<ModelInputPart> build_tool_history_compacted(
    uint32_t current_turn) {
    
    std::vector<ModelInputPart> result;
    std::vector<TurnSummary> collapsed_turns;
    
    for (uint32_t i = 0; i < tool_history_.size(); ++i) {
        uint32_t turn_age = current_turn - tool_history_[i].turn_index;
        auto level = compactor_.decide_level(turn_age);
        
        switch (level) {
        case CompactionLevel::Full:
            result.push_back(tool_history_[i].call);
            result.push_back(tool_history_[i].result);
            break;
        
        case CompactionLevel::Summary:
            result.push_back(tool_history_[i].call);
            result.push_back(compact_to_summary(tool_history_[i].result));
            break;
        
        case CompactionLevel::Collapsed:
            collapsed_turns.push_back(build_summary(tool_history_[i]));
            break;
        }
    }
    
    // 折叠区域生成单段文本
    if (!collapsed_turns.empty()) {
        auto collapsed_text = collapse_multiple_turns(collapsed_turns);
        result.insert(result.begin(), TextPart{.text = collapsed_text});
    }
    
    return result;
}
```

#### Step 2.1.4：失败场景优化（0.5 天）

复用 DEC-085 的工具失败计数器：

```cpp
// 连续失败 ≥3 次的工具，后续轮次只保留摘要
if (tool_failure_counter_[tool_name] >= 3) {
    std::string blocked_summary = std::format(
        "[Tool blocked] {} failed {}x in turns {} ({}), "
        "subsequent calls rejected.",
        tool_name,
        tool_failure_counter_[tool_name],
        format_turn_range(failure_turns),
        last_failure_reason
    );
    return {TextPart{.text = blocked_summary}};
}
```

#### Step 2.1.5：测试与验收（0.5 天）

**测试覆盖**：
- [ ] 15 轮任务，第 15 轮的 tool-history tokens < 完整回放的 40%
- [ ] 最近 2 轮完整保留
- [ ] 第 3-5 轮结果被摘要
- [ ] 第 6+ 轮折叠为单段
- [ ] 连续失败场景：摘要包含失败次数和原因

**性能要求**：
- [ ] Micro-compact 开销 < 10ms/轮（15 轮历史）
- [ ] 内存增长 < 50KB（摘要缓存）

---

### 2.2 自动压缩阈值（mira 本地）

**工作量**：4 天  
**位置**：`third_party/mira/src/conversation_loop.cpp`

**步骤**：

#### Step 2.2.1：设计 ContextCompactor 接口（1 天）

```cpp
// third_party/mira/include/mira/conversation_loop.hpp

struct ContextCompactionConfig {
    double auto_compact_threshold = 0.45;  // 触发阈值
    uint32_t keep_recent_turns = 3;        // 保留最近 N 轮
    uint32_t summary_window = 7;           // 第 4-10 轮摘要
    size_t summary_max_chars = 300;        // 每轮摘要上限
    size_t collapsed_max_chars = 500;      // 折叠区域上限
};

class IContextCompactor {
public:
    virtual ~IContextCompactor() = default;
    
    struct CompactionResult {
        std::vector<ModelInputCondition> compacted_history;
        uint32_t original_turn_count;
        size_t original_token_estimate;
        size_t compacted_token_estimate;
        std::vector<uint32_t> compacted_turn_indices;
    };
    
    virtual Result<CompactionResult> compact(
        const std::vector<MessageHistoryEntry>& history,
        const ContextCompactionConfig& config) = 0;
};
```

**关键点**：
- 接口设计为可替换（默认实现 + 可选自定义）
- 压缩结果包含 before/after 统计（用于事件面）

#### Step 2.2.2：实现默认压缩器（1.5 天）

```cpp
// third_party/mira/src/context_compactor.cpp

class DefaultContextCompactor : public IContextCompactor {
public:
    Result<CompactionResult> compact(
        const std::vector<MessageHistoryEntry>& history,
        const ContextCompactionConfig& config) override {
        
        CompactionResult result;
        result.original_turn_count = history.size();
        
        // 分区：保留区 / 摘要区 / 折叠区
        auto recent_turns = get_last_n_turns(history, config.keep_recent_turns);
        auto summary_turns = get_turns_in_range(
            history,
            config.keep_recent_turns,
            config.keep_recent_turns + config.summary_window
        );
        auto collapsed_turns = get_turns_before(
            history,
            history.size() - config.keep_recent_turns - config.summary_window
        );
        
        // 保留区：完整保留
        for (const auto& turn : recent_turns) {
            append_full_turn(result.compacted_history, turn);
        }
        
        // 摘要区：每轮压缩
        for (const auto& turn : summary_turns) {
            append_summarized_turn(result.compacted_history, turn, 
                                  config.summary_max_chars);
            result.compacted_turn_indices.push_back(turn.index);
        }
        
        // 折叠区：整体折叠
        if (!collapsed_turns.empty()) {
            auto collapsed_text = collapse_turns(collapsed_turns, 
                                                config.collapsed_max_chars);
            result.compacted_history.insert(
                result.compacted_history.begin(),
                MessageCondition{
                    .role = ModelRole::User,
                    .parts = {TextPart{.text = collapsed_text}}
                }
            );
            for (const auto& turn : collapsed_turns) {
                result.compacted_turn_indices.push_back(turn.index);
            }
        }
        
        // 估算 token 数
        result.original_token_estimate = estimate_tokens(history);
        result.compacted_token_estimate = estimate_tokens(result.compacted_history);
        
        return result;
    }
    
private:
    std::string summarize_turn(const MessageHistoryEntry& turn, 
                               size_t max_chars) {
        // 模板：[Turn N] User: {first_50}... → {tool_summary} → Assistant: {outcome}
        auto user_preview = truncate_to_length(
            turn.user_message.parts[0].text, 50
        );
        auto tool_summary = summarize_tools(turn.tool_executions);
        auto outcome = turn.completion_status == CompletionStatus::Answered
            ? "answered"
            : "failed: " + turn.error_message;
        
        return truncate_to_length(
            std::format(
                "[Turn {}] User: {}... → {} → Assistant: {}",
                turn.index, user_preview, tool_summary, outcome
            ),
            max_chars
        );
    }
    
    std::string collapse_turns(const std::vector<MessageHistoryEntry>& turns,
                              size_t max_chars) {
        // 统计：turn 范围、工具执行次数、成功/失败次数
        uint32_t total_tools = 0;
        uint32_t answered_turns = 0;
        uint32_t failed_turns = 0;
        
        for (const auto& turn : turns) {
            total_tools += turn.tool_executions.size();
            if (turn.completion_status == CompletionStatus::Answered) {
                answered_turns++;
            } else {
                failed_turns++;
            }
        }
        
        return truncate_to_length(
            std::format(
                "[Earlier conversation, turns {}-{}] {} turns: {} tools executed, "
                "{} answered, {} failed",
                turns.front().index,
                turns.back().index,
                turns.size(),
                total_tools,
                answered_turns,
                failed_turns
            ),
            max_chars
        );
    }
};
```

**关键点**：
- 保留 Goal 和最新 recovery feedback（不压缩）
- 摘要保留：turn 索引、用户输入预览、工具摘要、结果
- 折叠保留：turn 范围、统计数据

#### Step 2.2.3：集成触发逻辑（0.5 天）

```cpp
// In ConversationLoop::build_request():

bool should_auto_compact() const {
    if (config_.context_window_tokens == 0) {
        return false;  // 未声明窗口，不压缩
    }
    
    double usage_ratio = static_cast<double>(estimated_input_tokens_) / 
                         config_.context_window_tokens;
    
    return usage_ratio >= config_.compaction.auto_compact_threshold;
}

Result<std::vector<ModelInputCondition>> build_request_with_compaction() {
    if (!should_auto_compact()) {
        return build_request_normal();  // 正常路径
    }
    
    // 触发压缩
    auto compaction_result = context_compactor_->compact(
        message_history_,
        config_.compaction
    );
    
    if (!compaction_result) {
        log_warning("Context compaction failed: {}", 
                   compaction_result.error().safe_message);
        return build_request_normal();  // 降级到正常路径
    }
    
    // 发布事件
    publish_event(ContextCompacted{
        .before_tokens = compaction_result->original_token_estimate,
        .after_tokens = compaction_result->compacted_token_estimate,
        .compacted_turns = compaction_result->compacted_turn_indices
    });
    
    return compaction_result->compacted_history;
}
```

**关键点**：
- 触发条件：`(已用 / 窗口) >= 0.45`
- 压缩失败时降级到正常路径（不阻塞任务）
- 发布 `ContextCompacted` 事件（诊断/遥测）

#### Step 2.2.4：测试与验收（1 天）

**测试覆盖**：
- [ ] 30 轮任务，第 15 轮触发 auto-compact
- [ ] 压缩后 input_tokens < 窗口 50%
- [ ] 最近 3 轮完整保留
- [ ] 第 4-10 轮被摘要
- [ ] 第 11+ 轮被折叠
- [ ] 压缩失败时降级到正常路径
- [ ] `ContextCompacted` 事件正确发布

**性能要求**：
- [ ] 压缩开销 < 50ms/轮（30 轮历史）
- [ ] 内存峰值 < 5MB（压缩临时数据）

---

### Phase 2 验收里程碑（Day 15）

- [ ] 15 轮任务：tool-history tokens < 完整回放 40%
- [ ] 30 轮任务：触发 auto-compact，input_tokens < 窗口 60%
- [ ] 所有单元测试通过（包括压缩场景）
- [ ] 压缩开销 < 50ms/轮
- [ ] 真实桌面任务（bilibili 视频查找）成功完成

---

## Phase 3：端到端验证与调优（P2，Day 16-20）

### 3.1 端到端回归测试（2 天）

**测试场景**：

#### 场景 1：图片密集任务（20 轮）
- [ ] 用户任务：在 Bilibili 找到特定视频并播放
- [ ] 预期：触发 auto-compact，不因上下文耗尽失败
- [ ] 验收：任务成功完成，第 20 轮 input_tokens < 窗口 60%

#### 场景 2：工具失败恢复（10 轮）
- [ ] 用户任务：在 Wayland 环境截图（触发失败）
- [ ] 预期：失败 3 次后阻止，优雅降级
- [ ] 验收：显示清晰失败原因，不耗尽预算

#### 场景 3：续写场景（思考截断）
- [ ] 用户任务：复杂推理任务（触发思考截断）
- [ ] 预期：差异化续写提示，模型切换到简洁模式
- [ ] 验收：续写成功率 ≥60%

#### 场景 4：图片复用（15 轮）
- [ ] 用户任务：等待某个 UI 元素出现（多次截图相同）
- [ ] 预期：第 3 次起出现 reuse 标注
- [ ] 验收：思考 token 降低 ≥10%

### 3.2 性能调优（1 天）

**profiling 重点**：
- [ ] 压缩开销（目标 < 50ms/轮）
- [ ] 内存峰值（目标 < 5MB）
- [ ] token 估算精度（与 provider 报告误差 < 10%）

**优化方向**：
- 摘要生成：预编译正则表达式
- 折叠统计：增量更新（不重复遍历）
- token 估算：缓存中间结果

### 3.3 文档更新（1 天）

**需要更新的文档**：
- [ ] `third_party/mira/docs/api/model-agent-loop.md`：
  - 新增 `ContextCompactor` 接口说明
  - 新增 `SuppliedInput` 结构说明
  - 新增 `ContextCompacted` 事件说明

- [ ] `docs/design/Mirage：Linux - Windows 桌面端设计方案.md`：
  - 更新图片供给章节（reuse 标注）
  - 更新预算管理章节（压缩策略）

- [ ] `docs/compatibility/`：
  - 新增验收记录：`context-compaction-validation-20261014.md`

- [ ] `CHANGELOG.md`（mira 侧）：
  - 记录 breaking change（`ModelInputSupplier` 签名扩展）
  - 记录新增接口（`IContextCompactor`）

### 3.4 Mira 反馈台账登记

按工程规范，即使是本地修改，也要登记可追溯记录：

```bash
# 在 docs/executor_feedback/ledger.md（历史名称，实际是 mira 反馈）登记
```

**登记内容**：
```markdown
## MIRA-20261014-003：ConversationLoop 三级上下文压缩（已在本地实现）

**问题**：
超窗输入在 Mirage 必然失败，无主动压缩机制（DEC-087 明确边界）。

**实现范围**（本地 `third_party/mira`）：
1. 输入去重标注（`SuppliedInput.reuse_meta`）
2. 工具历史微压缩（最近 2 轮完整 + 摘要 + 折叠）
3. 自动压缩阈值（45% 触发，保留 3 轮 + 摘要 7 轮 + 折叠）
4. 续写提示差异化（思考/文本/工具截断分类）
5. 图片降级（Mirage 侧，preflight < 5000 降级）

**验收证据**：
- 30 轮图片任务成功完成，input_tokens < 窗口 60%
- 压缩开销 < 50ms/轮
- 见 [验收记录](../compatibility/context-compaction-validation-20261014.md)

**上游合并计划**：
- 待 Mirage M9 稳定后，提 PR 至 mira 上游
- 需补充：上游 CI 测试、API 文档、兼容性说明
```

---

## 验收清单（最终交付）

### 功能验收
- [ ] P0-1：图片复用标注生效（第 3 次出现有提示）
- [ ] P0-2：续写提示差异化（思考截断 → "concise"）
- [ ] P0-3：图片降级生效（余量 < 5000 降级）
- [ ] P1-1：工具历史压缩（15 轮 < 完整 40%）
- [ ] P1-2：自动压缩触发（30 轮 < 窗口 60%）

### 性能验收
- [ ] 压缩开销 < 50ms/轮（30 轮历史）
- [ ] 内存峰值 < 5MB（压缩临时数据）
- [ ] Token 估算误差 < 10%（与 provider 对比）

### 端到端验收
- [ ] Bilibili 视频查找任务（20 轮）成功
- [ ] Wayland 截图失败场景优雅降级
- [ ] 思考截断续写成功率 ≥60%
- [ ] 图片复用场景思考 token ↓10%

### 文档验收
- [ ] DEC-088 决策文档完整
- [ ] API 文档更新（mira 侧）
- [ ] 设计文档更新（Mirage 侧）
- [ ] 验收记录完整（compatibility/）
- [ ] 反馈台账登记（ledger.md）

---

## 风险与应对

| 风险 | 概率 | 影响 | 应对措施 |
|------|------|------|----------|
| 压缩后模型理解下降 | 中 | 高 | 保留最近 3 轮完整；对照实验验证任务成功率 |
| 压缩开销过高 | 低 | 中 | Profiling 优化；目标 < 50ms 可接受 |
| 与 provider cache 冲突 | 高 | 低 | 接受 trade-off（续航 > 缓存节省） |
| token 估算不准 | 中 | 中 | 基于 provider 报告的锚点增量；误差 < 10% |
| 实现复杂度超预期 | 中 | 高 | Phase 1 优先，Phase 2 可延后至 M9.5 |

---

## 资源需求

- **开发时间**：3 周（1 人全职）
  - Phase 1: 5 天
  - Phase 2: 10 天
  - Phase 3: 5 天

- **测试资源**：
  - 单元测试：随开发同步
  - 端到端测试：需真实桌面环境（Xorg + Wayland）
  - 性能测试：需 profiling 工具（perf / valgrind）

- **环境要求**：
  - Linux 开发机（Ubuntu 22.04+）
  - 真实模型 API（验证 token 统计）
  - Wayland 会话（验证图片降级）

---

## 后续优化方向（M10+）

1. **LLM-based 摘要**（当前是规则式）
   - 用小模型生成高质量摘要（每轮 +100ms 可接受）
   - 成本：每轮 ~500 tokens 输入，~100 tokens 输出

2. **用户可配置压缩策略**
   - UI 设置："激进压缩"/"平衡"/"保守"
   - 对应不同的 `keep_recent_turns` / `summary_window`

3. **压缩状态持久化**
   - 当前：每次从完整 history 重新计算（幂等）
   - 优化：缓存压缩结果，增量更新

4. **与 provider cache 协同**
   - 检测 cache hit rate
   - 低 hit rate 时更激进压缩
   - 高 hit rate 时保守压缩（保留前缀）

---

## 进展记录

### 2026-10-14：能力 1/4 上游交付（PR 待合并）

- 台账 [MIRA-20261014-005](../dependency_feedback/mira.md#mira-20261014-005) 已登记
  （供给缝无跨轮复用身份入口；实际编号与上文草稿模板的 -003 不同，以台账为准）。
  上游 mira [PR#95](https://github.com/Linductor-alkaid/mira/pull/95) 交付
  `set_model_input_identity` 身份缝与 max_tokens 续写锚点：宿主持有跨轮连续复用
  计数，循环在第三次相同图片供给时于 User 信封末尾渲染 Internal 权限的 System
  note；事件与 `ConversationLoopResult` 计数可见；无回调时请求逐字节不变。
  本地 Debug 全量 ctest 106/106、ASAN、architecture/format/docs 门禁通过；
  远端 CI 通过并经维护者合并授权后方可采纳 pin。
- 与本文 Step 1.1 的接口差异：兼容性约束下复用元数据走独立的可选
  `ModelInputIdentityProvider` 回调，不改变 `ModelInputSupplier` 返回类型；
  digest/count 由宿主持有的语义与 Step 1.1.3 一致。Step 1.2 的三类分支在
  pin 1befa45 已存在，本次仅补部分文本续写锚点与 plain-text 措辞。
- 待办：授权消费新 pin 后实施 Mirage 侧跨轮计数（desktop_image_supply 持有
  digest/count）与能力 5 图片降级（preflight 余量），再做真实会话对照验收
  （第 3 轮请求含复用提示、思考 token 下降 ≥10%）。能力 2/3（Phase 2）未实施。

### 2026-10-14：pin 消费 9e7b729 与 Mirage 侧跨轮计数（能力 1 收口）

- 维护者授权合并上游 PR#95（merge 9e7b729）后，`chore(deps)` 采纳新 pin（子模块指针
  与 dependencies.lock.json 同步）。Mirage 侧 Step 1.1.3 落地：持久
  `DesktopImageSource` 以内容 digest + 桌面 epoch 持有跨 dialog turn 的连续复用计数
  （相同像素重拍延续、像素或 epoch 变化清零），`DesktopImageTurn::input_reuse_identity`
  在与 supply() 相同的 owner/取消门控下按请求恰一次解析（纯文本轮不计数），
  model_layer 的 `invoke_loop` 接线；无图配置回调返回 nullopt，请求装配与既往字节一致。
- 自动化验证：desktop_image_supply_test 新增 0/1/2 计数、跨 turn 相同内容重拍延续、
  纯文本跳过、错 owner 拒绝、换 ROI 重置（945 checks）；native_agent_integration_test
  406 checks 保持绿色。证据与边界见[同日验收](../compatibility/dec088-input-reuse-20261014.md)。
- 待办：能力 5 图片降级（preflight 余量暴露）未实施；真实会话对照验收
  （第 3 轮请求含复用提示、思考 token 下降 ≥10%）待维护者在真实桌面任务上补跑。
  能力 2/3（Phase 2）未实施。
