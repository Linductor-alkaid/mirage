# 会话上下文压缩与输入去重：实施计划（修订版）

> 状态：Superseded（2026-10-15；后续实施与验收由 M9 跟踪）
> 后继：[M9 持续任务与本地语义感知](m9-continuous-tasks-and-local-perception.md)，
> 决策为 DEC-090/091。下文保留历史设计、fixture 结果和未执行项，不作为当前
> 并行实施清单；请求级预算与真实会话门禁分别由 M9-04/12 承接。
>
> 日期：2026-10-14（初版）/ 2026-10-14（修订：集成 Mira Layer 0）
> 决策依据：[DEC-088](../decisions/DEC-088-conversation-context-compaction-and-input-deduplication.md)
> 负责人：Mira/Mirage 维护者
> 目标：M9 多步任务稳定性（20+ 轮图片密集任务不因上下文耗尽失败）

## 修订说明

**2026-10-14 验证发现**：Mira 已交付完整的五层上下文智能架构（M4-M21），但 Mirage
当前未集成。修正实施路径为：

1. **优先集成 Mira 已有能力**（Layer 0: StandardContextManager）
2. **只对真正缺口补充实现**（输入去重标注、续写提示差异化）

详见：`docs/analysis/context-management-capability-gap.md`。

---

## 实施优先级与依赖（修订版）

```
Phase 1 (P0, 1周)              Phase 2 (P1, 3天)           Phase 3 (P2, 2天)
┌─────────────────────────┐    ┌──────────────────┐       ┌─────────────────┐
│ 1. 集成 Layer 0         │───>│ 4. 图片复用标注   │ ───>  │ 7. 端到端验证    │
│    - ContextBuilder     │    │    (等PR#95)     │       │ 8. 性能调优      │
│    - 水位配置           │    │                  │       │ 9. 文档更新      │
│ 2. 续写提示差异化(并行)  │    │ 5. 图片降级(并行) │       │                 │
│    (Mira PR)           │    │    (Mirage侧)    │       │                 │
└─────────────────────────┘    └──────────────────┘       └─────────────────┘
     ↓ 3天                          ↓ 3天                      ↓ 2天
   Layer 0集成                   等待PR#95合并              真实任务验证
```

**总工期**：3 周 → 1.5 周（集成已有能力，减少重复实现）

---

## Phase 1：Mira 能力集成（P0，Day 1-5）

### 1.1 集成 StandardContextManager（Mirage 侧）

**工作量**：3 天  
**位置**：`integration/mira/src/model_layer.cpp`

#### Step 1.1.1：创建 DesktopContextBuilder（1 天）

```cpp
// integration/mira/src/desktop_context_builder.hpp

#include <mira/context_manager.hpp>
#include <mira/context_contracts.hpp>

namespace mirage {

class DesktopContextBuilder {
public:
    explicit DesktopContextBuilder(
        std::shared_ptr<mira::IContextManager> context_manager)
        : context_manager_(std::move(context_manager)) {}
    
    mira::ContextRequest build_context_request(
        const DesktopTaskState& state);
    
private:
    mira::ContextLimits make_desktop_limits(
        const mira::ModelProfile& profile);
    
    std::vector<mira::ContextItem> make_goal_items(
        const std::string& goal);
    
    std::vector<mira::ContextItem> make_observation_items(
        const DesktopObservation& obs,
        uint64_t task_epoch,
        uint64_t env_epoch);
    
    std::vector<mira::ContextItem> make_tool_history_items(
        const std::vector<std::pair<ToolCall, std::optional<ToolResult>>>& history);
    
    std::shared_ptr<mira::IContextManager> context_manager_;
};

} // namespace mirage
```

**实现**：

```cpp
// integration/mira/src/desktop_context_builder.cpp

mira::ContextRequest DesktopContextBuilder::build_context_request(
    const DesktopTaskState& state) {
    
    mira::ContextRequest request;
    request.limits = make_desktop_limits(state.profile);
    request.task_id = state.task_id;
    request.session_id = state.session_id;
    request.profile_id = state.profile.id;
    
    // P1: Goal (pinned，永不压缩)
    auto goal_items = make_goal_items(state.goal);
    request.items.insert(request.items.end(), 
                        std::make_move_iterator(goal_items.begin()),
                        std::make_move_iterator(goal_items.end()));
    
    // P2: CurrentObservation (当前截图)
    if (state.current_observation) {
        auto obs_items = make_observation_items(
            *state.current_observation,
            state.task_epoch,
            state.environment_epoch);
        request.items.insert(request.items.end(),
                            std::make_move_iterator(obs_items.begin()),
                            std::make_move_iterator(obs_items.end()));
    }
    
    // P5: ToolHistory (可压缩)
    auto tool_items = make_tool_history_items(state.tool_history);
    request.items.insert(request.items.end(),
                        std::make_move_iterator(tool_items.begin()),
                        std::make_move_iterator(tool_items.end()));
    
    return request;
}

mira::ContextLimits DesktopContextBuilder::make_desktop_limits(
    const mira::ModelProfile& profile) {
    
    mira::ContextLimits limits;
    limits.context_window_tokens = profile.capabilities.limits.context_window;
    limits.reserved_output_tokens = profile.capabilities.limits.max_output_tokens;
    
    // 桌面任务特化：更激进的压缩阈值
    limits.trim_watermark = 0.45;        // 45% 触发裁剪
    limits.checkpoint_watermark = 0.70;  // 70% 建议 checkpoint
    limits.hard_watermark = 0.90;        // 90% 拒绝
    
    // 图片预算：桌面截图专用
    limits.max_image_tokens = 10000;     // 1张1600px截图 + 余量
    
    return limits;
}

std::vector<mira::ContextItem> DesktopContextBuilder::make_goal_items(
    const std::string& goal) {
    
    mira::ContextItem item;
    item.id = mira::ContextItemId::generate();
    item.kind = mira::ContextItemKind::Goal;
    item.authority = mira::ContextAuthority::UserConstraint;
    item.priority = mira::ContextPriority::Essential;
    item.content.emplace_back(mira::TextPart{.text = goal});
    item.pinned = true;  // 永不压缩
    
    return {std::move(item)};
}

std::vector<mira::ContextItem> DesktopContextBuilder::make_observation_items(
    const DesktopObservation& obs,
    uint64_t task_epoch,
    uint64_t env_epoch) {
    
    mira::ContextItem item;
    item.id = mira::ContextItemId::generate();
    item.kind = mira::ContextItemKind::CurrentObservation;
    item.priority = mira::ContextPriority::Important;
    item.task_epoch = task_epoch;
    item.environment_epoch = env_epoch;
    
    // 截图
    if (obs.screenshot) {
        item.content.emplace_back(mira::ImagePart{
            .artifact_ref = obs.screenshot->artifact_id,
            .media_type = "image/png"
        });
    }
    
    // UI Tree（如果有）
    if (obs.ui_tree) {
        item.content.emplace_back(mira::TextPart{
            .text = serialize_ui_tree(*obs.ui_tree)
        });
    }
    
    return {std::move(item)};
}

std::vector<mira::ContextItem> DesktopContextBuilder::make_tool_history_items(
    const std::vector<std::pair<ToolCall, std::optional<ToolResult>>>& history) {
    
    std::vector<mira::ContextItem> items;
    
    for (const auto& [call, result] : history) {
        // ToolCall
        mira::ContextItem call_item;
        call_item.id = mira::ContextItemId::generate();
        call_item.kind = mira::ContextItemKind::ToolCall;
        call_item.priority = mira::ContextPriority::Background;
        call_item.tool_call_key = call.id;
        call_item.consumed = result.has_value();
        call_item.sequence = call.sequence;
        
        // 序列化 ToolCall
        call_item.content.emplace_back(mira::TextPart{
            .text = serialize_tool_call(call)
        });
        
        items.push_back(std::move(call_item));
        
        // ToolResult
        if (result) {
            mira::ContextItem result_item;
            result_item.id = mira::ContextItemId::generate();
            result_item.kind = mira::ContextItemKind::ToolResult;
            result_item.priority = mira::ContextPriority::Background;
            result_item.tool_call_key = call.id;
            result_item.consumed = result->followed_by_model_turn;
            result_item.replaceable_by_reference = true;  // ← 关键！
            result_item.sequence = result->sequence;
            
            // 大截图走 payload，文本走 content
            if (result->large_artifact) {
                result_item.payload = result->large_artifact;
            } else {
                result_item.content.emplace_back(mira::TextPart{
                    .text = result->output
                });
            }
            
            items.push_back(std::move(result_item));
        }
    }
    
    return items;
}
```

**修改文件**：
- 新增：`integration/mira/src/desktop_context_builder.hpp`
- 新增：`integration/mira/src/desktop_context_builder.cpp`
- 修改：`integration/mira/CMakeLists.txt`（添加编译单元）

#### Step 1.1.2：集成到 ModelLayer（1 天）

```cpp
// integration/mira/src/model_layer.cpp

Result<AgentLoopResult> ModelLayer::run_agent_loop(...) {
    // 1. 创建 ContextManager（使用 Mira 的 StandardContextManager）
    auto context_manager = mira::create_standard_context_manager(
        token_counter_,
        artifact_store_);
    
    // 2. 创建 DesktopContextBuilder
    DesktopContextBuilder context_builder(context_manager);
    
    // 3. 修改 supply_input 回调
    auto supply_input = [&](const mira::AgentLoopSpec& spec) 
        -> mira::Result<std::vector<mira::ModelInputItem>> {
        
        // 3.1 构建 ContextRequest
        DesktopTaskState task_state = build_task_state(spec);
        auto request = context_builder.build_context_request(task_state);
        
        // 3.2 调用 ContextManager 压缩
        auto prepared = context_manager->prepare(request);
        if (!prepared) {
            LOG(ERROR) << "Context preparation failed: " << prepared.error();
            return prepared.error();
        }
        
        // 3.3 记录审计日志
        log_context_audit(prepared.value().item_audit);
        
        // 3.4 转换为 ModelInputItem（Loop 内部契约）
        return convert_to_model_input_items(prepared.value().input);
    };
    
    // 4. 启动 ConversationLoop
    mira::ConversationLoop loop(*impl_->gateway, loop_config);
    loop.set_tool_registry(registry);
    loop.set_model_input_supplier(supply_input);
    return loop.run(spec, turn_context);
}

// 转换函数：PreparedModelContext -> ModelInputItem
std::vector<mira::ModelInputItem> convert_to_model_input_items(
    const std::vector<mira::ModelInputPart>& prepared_input) {
    
    std::vector<mira::ModelInputItem> items;
    
    // PreparedModelContext.input 已经是压缩后、按优先级排序的 Parts
    // 需要转换为 ConversationLoop 期望的 ModelInputItem 格式
    
    // 简化版：假设 prepared_input 已经是正确的顺序
    // 实际可能需要更复杂的转换逻辑
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    item.content = prepared_input;
    
    items.push_back(std::move(item));
    return items;
}

// 审计日志记录
void log_context_audit(const std::vector<mira::ContextItemAudit>& audit) {
    for (const auto& entry : audit) {
        LOG(INFO) << "Context item " << entry.item_id 
                  << ": " << to_string(entry.disposition)
                  << " (reason: " << to_string(entry.reason) << ")";
    }
}
```

**修改文件**：
- `integration/mira/src/model_layer.cpp`（主要集成点）
- `integration/mira/src/model_layer_impl.hpp`（添加成员变量）

#### Step 1.1.3：编写单元测试（1 天）

```cpp
// tests/integration/context_builder_test.cpp

TEST(DesktopContextBuilderTest, BuildsCorrectContextRequest) {
    auto context_manager = create_mock_context_manager();
    DesktopContextBuilder builder(context_manager);
    
    DesktopTaskState state = make_test_task_state();
    auto request = builder.build_context_request(state);
    
    // 验证：Goal 是 P1 + pinned
    auto goal_item = find_item_by_kind(request.items, 
                                       mira::ContextItemKind::Goal);
    ASSERT_TRUE(goal_item);
    EXPECT_EQ(goal_item->priority, mira::ContextPriority::Essential);
    EXPECT_TRUE(goal_item->pinned);
    
    // 验证：ToolResult 可被引用替换
    auto tool_result = find_item_by_kind(request.items,
                                         mira::ContextItemKind::ToolResult);
    ASSERT_TRUE(tool_result);
    EXPECT_TRUE(tool_result->replaceable_by_reference);
}

TEST(DesktopContextBuilderTest, DesktopLimitsAreCorrect) {
    auto context_manager = create_mock_context_manager();
    DesktopContextBuilder builder(context_manager);
    
    mira::ModelProfile profile = make_test_profile();
    auto request = builder.build_context_request(make_test_state(profile));
    
    // 验证：桌面特化的水位
    EXPECT_DOUBLE_EQ(request.limits.trim_watermark, 0.45);
    EXPECT_DOUBLE_EQ(request.limits.checkpoint_watermark, 0.70);
    EXPECT_DOUBLE_EQ(request.limits.hard_watermark, 0.90);
    EXPECT_EQ(request.limits.max_image_tokens, 10000);
}

TEST(ContextIntegrationTest, CompactsToolHistoryAt15Turns) {
    // 构建 15 轮工具历史
    DesktopTaskState state = make_15_turn_task_state();
    
    auto context_manager = mira::create_standard_context_manager(...);
    DesktopContextBuilder builder(context_manager);
    
    auto request = builder.build_context_request(state);
    auto prepared = context_manager->prepare(request);
    
    ASSERT_TRUE(prepared);
    
    // 验证：有东西被压缩了
    auto audit = prepared.value().item_audit;
    size_t dropped = count_disposition(audit, 
                                       mira::ContextItemDisposition::DroppedByWatermark);
    size_t replaced = count_disposition(audit,
                                        mira::ContextItemDisposition::SelectedByReference);
    
    EXPECT_GT(dropped + replaced, 0) << "No compaction happened";
    
    // 验证：input tokens 降低
    size_t input_tokens = estimate_tokens(prepared.value().input);
    size_t baseline_tokens = estimate_tokens_without_compaction(state);
    
    EXPECT_LT(input_tokens, baseline_tokens * 0.70) 
        << "Expected at least 30% reduction";
}
```

**修改文件**：
- 新增：`tests/integration/context_builder_test.cpp`
- 修改：`tests/integration/CMakeLists.txt`

**验收标准**：
- [ ] 编译通过
- [ ] 单元测试全部通过
- [ ] 15 轮任务 input tokens < 基线 70%

---

### 1.2 续写提示差异化（Mira 侧，可并行）——已交付

**交付说明**：本节为计划期的实现草案；实际交付早于草案且未走独立台账登记
（草案占用的编号 MIRA-20261014-007 已被计数器缺口
[登记](../dependency_feedback/mira.md#mira-20261014-007)使用）。落地形态：

- PR#93（合并 `1befa45`）：`IncompleteReason::MaxOutputTokens` 的有界续写按
  响应结构区分三种反馈——部分文本截断（部分文本保留进 history 后继续）、
  工具调用截断（废弃未执行调用，要求重发完整调用或改输出文本）、仅思考
  截断（要求跳过冗长思考直接给出简洁文本）；续写预算耗尽以显式
  output-limit 终态收口。
- PR#95（合并 `9e7b729`）：部分文本分支增加截断锚点（末 50 字符
  "Continue from: \"...\""），模型从精确尾部续写而非重推；Mirage 侧
  `harness_desktop_tools_test` 按锚定格式断言。

实现位于 `third_party/mira/src/model/conversation_loop.cpp`（有界续写分支），
非草案假设的 `provider_continuation.cpp` 独立函数。

**验收标准**：
- [x] 单元测试通过（上游 `m3_conversation_loop_test` 覆盖三分支反馈与
  预算耗尽终态）
- [ ] 思考截断场景的续写成功率 ≥60%（真实会话指标，待真实 provider 环境；
  负责人 Mirage 维护者）
- [ ] 不再出现"继续思考 → 截断"循环（同上，待真实会话回归）

---

## Phase 2：等待上游与扩展（P1，Day 6-10）

### 2.1 图片复用标注集成（等待 PR#95 合并）

**工作量**：1 天  
**前置**：Mira PR#95 合并

#### Step 2.1.1：更新 pinned mira（0.5 天）

```bash
cd ~/mirage
git submodule update --remote third_party/mira
# 验证 PR#95 已合并
git add third_party/mira
git commit -m "deps(mira): update to include PR#95 (input reuse annotation)"
```

#### Step 2.1.2：Mirage 侧实现（0.5 天）

```cpp
// integration/mira/src/desktop_image_supply.cpp

class DesktopImageTurn {
public:
    mira::SuppliedInput supply_with_reuse_meta(
        const mira::AgentLoopSpec& spec) {
        
        mira::SuppliedInput input;
        
        // 图片内容
        input.items = make_image_items(current_screenshot_);
        
        // 复用元数据（PR#95 新增）
        if (current_screenshot_id_ == last_supplied_id_) {
            input.reuse_meta = mira::InputReuseMeta{
                .content_digest = current_screenshot_id_,
                .reuse_count = ++consecutive_reuse_,
                .semantic_label = "desktop-screenshot"
            };
        } else {
            consecutive_reuse_ = 0;
            last_supplied_id_ = current_screenshot_id_;
        }
        
        return input;
    }

private:
    std::string last_supplied_id_;
    uint32_t consecutive_reuse_ = 0;
};
```

**验收标准**：
- [ ] 相同截图连续 3 轮，第 3 轮请求包含 reuse 提示
- [ ] 对照实验：思考 token -10% ~ -20%

---

### 2.2 图片降级策略（Mirage 侧，可并行）

**工作量**：2 天  
**位置**：`integration/mira/src/desktop_image_supply.cpp`

#### Step 2.2.1：实现降级逻辑（1 天）

```cpp
// integration/mira/src/desktop_image_supply.cpp

mira::Result<std::vector<mira::ModelInputItem>>
DesktopImageTurn::supply_with_budget_awareness(
    const mira::AgentLoopSpec& spec,
    int64_t preflight_remaining) {
    
    // 策略 1: 余量充足，正常供给
    if (preflight_remaining > 20000) {
        return supply_full_image(current_screenshot_);
    }
    
    // 策略 2: 余量紧张，追加警告
    if (preflight_remaining > 10000) {
        auto items = supply_full_image(current_screenshot_);
        items.value().push_back(make_budget_warning());
        return items;
    }
    
    // 策略 3: 余量极低，降级为文本
    return supply_image_as_text_description(
        current_screenshot_, 
        last_ocr_summary_);
}

std::vector<mira::ModelInputItem> supply_image_as_text_description(
    const Screenshot& screenshot,
    const std::optional<std::string>& last_ocr) {
    
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    item.provenance.source = "mirage.image-supply.degraded.v1";
    
    std::string text = std::format(
        "[Image degraded due to context limit] Screenshot from turn {}, "
        "{}x{} pixels.",
        current_turn_,
        screenshot.width,
        screenshot.height);
    
    if (last_ocr) {
        text += " Previous OCR: " + *last_ocr;
    }
    
    item.content.emplace_back(mira::TextPart{.text = std::move(text)});
    return {std::move(item)};
}
```

**注意**：需要 `preflight_remaining` 从 Mira 暴露。如果 Mira 未暴露，需先登记反馈台账。

#### Step 2.2.2：单元测试（1 天）

```cpp
// tests/integration/image_degradation_test.cpp

TEST(ImageDegradationTest, NormalSupplyWhenBudgetSufficient) {
    DesktopImageTurn turn(screenshot);
    auto items = turn.supply_with_budget_awareness(spec, 25000);
    
    ASSERT_TRUE(items);
    EXPECT_EQ(count_image_parts(items.value()), 1);
    EXPECT_EQ(count_warning_parts(items.value()), 0);
}

TEST(ImageDegradationTest, WarningWhenBudgetTight) {
    DesktopImageTurn turn(screenshot);
    auto items = turn.supply_with_budget_awareness(spec, 15000);
    
    ASSERT_TRUE(items);
    EXPECT_EQ(count_image_parts(items.value()), 1);
    EXPECT_EQ(count_warning_parts(items.value()), 1);
}

TEST(ImageDegradationTest, DegradationWhenBudgetLow) {
    DesktopImageTurn turn(screenshot);
    auto items = turn.supply_with_budget_awareness(spec, 8000);
    
    ASSERT_TRUE(items);
    EXPECT_EQ(count_image_parts(items.value()), 0);
    EXPECT_EQ(count_text_parts(items.value()), 1);
    EXPECT_THAT(get_text_content(items.value()[0]),
                HasSubstr("Image degraded"));
}
```

**验收标准**：
- [ ] 余量 15000：图片 + 警告
- [ ] 余量 8000：降级为文本，任务继续

---

## Phase 3：验证与调优（P2，Day 11-12）

### 3.1 端到端验证（1 天）

#### Test 1：15 轮 Bilibili 视频查找任务

```bash
# 启动 Mirage
./build/apps/native/mirage

# 提交任务
"在 Bilibili 上找到最近一周播放量最高的科技类视频"

# 监控指标
- Input tokens / 轮
- 压缩触发次数
- 任务成功/失败
```

**验收**：
- [ ] 15 轮成功完成
- [ ] 平均 input tokens < 基线 60%
- [ ] 触发 trim（水位 > 45%）

#### Test 2：30 轮图片密集任务

```bash
# 构造 30 轮循环任务
"每次截图并分析窗口标题，持续 30 轮"

# 监控指标
- Input tokens 曲线
- 压缩触发轮次
- 是否超窗失败
```

**验收**：
- [ ] 30 轮成功完成
- [ ] 不因超窗失败
- [ ] 第 30 轮利用率 < 60%

### 3.2 性能调优（0.5 天）

```cpp
// 添加性能监控
auto start = std::chrono::steady_clock::now();
auto prepared = context_manager_->prepare(request);
auto elapsed = std::chrono::steady_clock::now() - start;

LOG(INFO) << "ContextManager::prepare took " 
          << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
          << " ms";

// 目标：< 50ms/轮
EXPECT_LT(elapsed, std::chrono::milliseconds(50));
```

**验收**：
- [ ] ContextManager::prepare < 50ms/轮
- [ ] 压缩决策确定性（相同 request → 相同 digest）

### 3.3 文档更新（0.5 天）

- [ ] 更新 DEC-088 状态：Proposed → Accepted
- [ ] 更新本实施计划状态：完成 Phase 1-3
- [ ] 更新 `docs/dependency_feedback/ledger.md`（关闭已解决条目）
- [ ] 记录验收证据到 M9 计划

---

## 工作量汇总（修订版）

| Phase | 工作项 | 工期 | 人力 |
|-------|--------|------|------|
| Phase 1 | Layer 0 集成 | 3 天 | 1人 |
| Phase 1 | 续写提示（并行） | 2 天 | 1人 |
| Phase 2 | 图片复用标注 | 1 天 | 1人 |
| Phase 2 | 图片降级（并行） | 2 天 | 1人 |
| Phase 3 | 验证调优 | 2 天 | 1人 |
| **总计** |  | **10 天** | **1-2人** |

**相比原计划**：3 周 → 1.5 周（减少 50% 工作量）

---

## 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| Mira 能力不足以压缩桌面任务 | 高 | 低 | 登记 MIRA-20261014-006，实施 DesktopContextManager 装饰器 |
| PR#95 延期 | 中 | 中 | Phase 2 可独立推进图片降级 |
| preflight 余量未暴露 | 低 | 中 | 先登记反馈台账，临时方案：估算余量 |
| 压缩性能不达标 | 中 | 低 | 优化 token 估算，启用 exact counter |

---

## 检查清单

### 开始前
- [ ] 阅读 `docs/analysis/context-management-capability-gap.md`
- [ ] 确认 Mira API 文档：`third_party/mira/docs/api/context-memory.md`
- [ ] 准备测试环境（真实模型 API）

### Phase 1 完成
- [x] DesktopContextBuilder 实现并测试（落地形态：只路由工具往返段经 Layer 0，
  配对整对收缩、assistant 文本透传；上游缝 PR#96 交付后消费）
- [x] Layer 0 集成到 ModelLayer（声明窗口时接线；`DialogCompletion` 计数透传）
- [x] 单元测试全部通过（`desktop_context_builder_test` 1205 项断言；全量 82/82、
  ASAN 干净）
- [ ] 15 轮任务 input tokens < 基线 70%（真实会话，未执行；水位精度缺口
  MIRA-20261014-007 已由上游 PR#97 修复并消费，压缩触发按真实载荷规模工作）

### Phase 2 完成
- [x] PR#95 合并并更新 pinned mira（9e7b729，授权消费）
- [x] 图片复用标注集成（`reuse_identity` + `set_model_input_identity`）
- [x] 图片降级策略实现（`anchored_remaining` 信号 + 三段阈值带）

### Phase 3 完成
- [ ] 真实任务回归通过
- [ ] 性能指标达标
- [ ] 文档更新完成

---

**修订记录**：
- 2026-10-14：初版（自研微压缩 + 自动压缩）
- 2026-10-14：修订版（集成 Mira Layer 0，减少重复实现）

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

> **【作废草稿，未执行】**（2026-10-16 注，[依赖复用整改](dependency-reuse-remediation.md)
> REUSE-18）：本节的 `MIRA-20261014-003` 是未登记入台账的草稿编号，描述的"本地修改
> `third_party/mira`"路径**从未实施**（违反 pinned 纪律的方案在评审阶段被否）。
> 实际走上游缝交付：输入复用标注 = [MIRA-20261014-005](../dependency_feedback/mira.md#mira-20261014-005)
> （PR#95）、工具历史压缩缝 = [MIRA-20261014-006](../dependency_feedback/mira.md#mira-20261014-006)
> （PR#96）、token 计数修复 = [MIRA-20261014-007](../dependency_feedback/mira.md#mira-20261014-007)
> （PR#97）。下方案文中引用的 `context-compaction-validation-20261014.md` 验收文档
> 不存在；实际验收见
> [历史压缩消费验证](../compatibility/context-history-compaction-20261014.md)。

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

### 2026-10-14：PR#95 合并消费，能力 1/5 Mirage 侧落地

- 上游 [PR#95](https://github.com/Linductor-alkaid/mira/pull/95) 经授权合并
  （`9e7b729`），Mirage pin 与 dependencies.lock.json 同步消费（mira Debug
  107/107；消费审计
  [mira-input-reuse-20261014](../supply-chain/mira-input-reuse-20261014.md)）。
- 能力 1 集成：`DesktopImageTurn` 持有跨轮 digest/计数，`reuse_identity()` 经
  `loop.set_model_input_identity` 接线；计数只由真实含图供给推进，退役/降级轮
  暂停，digest 变化清零；`DialogCompletion.supplied_input_reuse_annotations`
  透传循环计数。
- 能力 5 实现：`ObservedProvider::anchored_remaining`（DEC-087 锚点，上界语义）
  供给了预算信号；阈值带 >20000 正常 / ≤20000 图片+警告 / ≤10000 扣图文字描述，
  无窗口或无锚点 fail-open。与本文 Step 2.2.1 草稿的接口差异：mira 未（也无需）
  在 `AgentLoopSpec` 暴露 preflight 余量——preflight 锚点表本来就在 Mirage
  ObservedProvider 内，供给回调直接读取，未登记额外反馈条目。
- 验证（Independent-Verification-Agent 两轮）：`desktop_image_supply_test`
  新增 85 条断言（复用身份链、降级三段阈值带含边界、anchored_remaining 单元、
  脚本化 provider 端到端第 3 轮渲染 System note 且
  `supplied_input_reuse_annotations == 1`），负向对照有效；Mirage Debug 全量
  81/81、ASAN 995 checks 无报告；格式/公共头边界门禁通过。证据：
  [context-reuse-degradation-20261014](../compatibility/context-reuse-degradation-20261014.md)。
- 契约同步：pin 9e7b729 部分文本续写反馈改为锚点式，
  `harness_desktop_tools_test` 两条断言同步（`Continue from: "…"`），复验通过。
- 台账：MIRA-20261014-005 → Resolved（真实会话对照待真实 provider 环境补跑）；
  新增 [MIRA-20261014-006](../dependency_feedback/mira.md#mira-20261014-006)：
  ConversationLoop 工具历史回放无宿主压缩缝，Layer 0 无法接入（含渲染器
  role_for 只保真 ToolCall 的约束证据）——能力 2/3（本计划 Phase 1 的 Layer 0
  集成与桌面水位两项）被此缝阻塞，待上游缝 PR，Mirage 不建平行压缩。
- 构建修复：系统 PipeWire 包切换后干净树测试目标断链
  （`pkg_check_modules` 导入目标改为 GLOBAL，platform/CMakeLists.txt）。
- 未执行：真实会话对照验收（15/30 轮 tokens 曲线、思考 token −10%~−20%）
  需真实模型 API 与桌面环境；负责人 Mirage 维护者，补跑条件见证据文档。

### 2026-10-14：能力 2/3 落地——上游历史压缩缝（PR#96）与 Mirage Layer 0 消费

- 上游 [PR#96](https://github.com/Linductor-alkaid/mira/pull/96) 经授权合并
  （`93e2eae`）：`ConversationLoop::set_history_transformer` 可选宿主历史压缩缝
  （通用 history 变换形态，规避渲染器角色保真约束；协议契约、四类降级、
  `HistoryTransformApplied/Degraded` 事件与结果计数、未接线逐字节不变）。
  mira Debug 108/108、全部门禁、24 项 checks 绿后合并；pin 同步消费。审计：
  [mira-history-compaction-20261014](../supply-chain/mira-history-compaction-20261014.md)。
- Mirage 侧 `DesktopContextBuilder`：工具往返段 → Layer 0（consumed、
  replaceable_by_reference=false、配对键、sequence）；桌面水位 0.45/0.70/0.90；
  checkpoint 拆对按 audit 整对收缩；assistant 文本透传（Layer 0 出决策、原始
  条目出渲染）。`model_layer.cpp` 声明窗口时接线，`DialogCompletion` 透传
  `history_transforms / history_transform_degradations`。
- 独立验证两轮：第一轮抓到高优先级接线缺陷（reserved 误用整 run 包络 ≥720896，
  一切现实窗口下 limits 拒绝、压缩必然降级）——已修复为 per-call 输出基准并
  复验 PASS。最终 1205 项断言：builder 映射/收缩/fail-open 逐字段断言、接线级
  真实触发（15 轮 capture + 4096 窗口，早期配对整对消失、配对合法、观测值与
  Layer 0 算术吻合）、余量窗口全量保留、微小窗口诚实降级；Mirage Debug 82/82、
  ASAN 无报告。证据：
  [context-history-compaction-20261014](../compatibility/context-history-compaction-20261014.md)。
- 台账：MIRA-20261014-006 → Resolved；新登记
  [MIRA-20261014-007](../dependency_feedback/mira.md#mira-20261014-007)（上游
  ConservativeTokenCounter 不计工具载荷字节，水位系统性低估工具密集请求——
  压缩触发偏晚，行为正确，待上游估算修复）。
- 未执行：真实会话 token 验收（-30%~40%）待真实 provider 环境，且精度受
  -007 影响。

### 2026-10-14：上游计数器修复（PR#97）消费——压缩触发按真实载荷规模工作

- 上游 [PR#97](https://github.com/Linductor-alkaid/mira/pull/97) 经授权合并
  （`7391df7`）：`ConservativeTokenCounter::estimate_item` 计入全部回放载荷
  字节（ToolCallPart.arguments 序列化、ToolResultPart 按 tool bridge wire
  形态、ThinkingPart text+signature；沿用 bytes_per_token 密度与 margin
  校准，`estimate_assembly_overhead` 不变）。mira Debug 108/108（新增 3 个
  跨载荷规模估算用例）、全部门禁、24 项 checks 绿；API 文档
  `docs/api/context-memory.md` 同步。pin 与 lock 同步消费，台账
  MIRA-20261014-007 → Resolved。
- Mirage 消费复验（Independent-Verification-Agent，PASS）：新增永久回归
  `large_tool_receipts_drive_checkpoint_compaction`——5 对 8KiB 回执新估算
  10585 token 越过 checkpoint 水位必然整对收缩，旧地板算术同夹具仅 240
  token（1.67% 预算）零触发的反证以真实 `StandardContextManager` 取证；
  既有接线级断言全部保持，本二进制 942 → 1004 项断言，Debug 82/82、
  ASAN 无报告。证据：
  [context-history-compaction-20261014](../compatibility/context-history-compaction-20261014.md)
  复验章节。
- 未执行：真实会话 token 验收待真实 provider 环境（精度缺口已闭合）。
### 2026-10-14：bubbles 线（feat/m8-realtime-bubbles）——能力 1 改为桌面捕获馈送的跨轮计数（#92，经合并进入本分支）

> 该条目记录并行线当时状态；其"能力 5/2/3 未实施"待办已由本文后继条目完成。本次合并统一采纳 `input_reuse_identity` 跨轮计数形态，本分支早先的 `reuse_identity()` turn 本地计数形态随合并移除。

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
