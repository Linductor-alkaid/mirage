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

## Mira 现有能力分析（2026-10-14 补充）

验证发现 Mira 已交付完整的五层上下文智能架构（M4-M21），但 Mirage 当前**未集成**：

### Layer 0：StandardContextManager（M4，已交付）
- **P0-P5 优先级分区**：Essential/Important/Useful/Supplemental/Background
- **三级水位**：trim (70%) / checkpoint (85%) / hard (95%)，自动裁剪低优先级项
- **引用替换**：大载荷转 `ArtifactRef`，降低实际传输 token
- **确定性装配**：相同 `ContextRequest` 产生相同 selection digest
- **API**：`IContextManager::prepare(ContextRequest) -> PreparedModelContext`

### Layer 1-3：检索、重排、固化（M17-M21，已交付）
- **IContextRetriever**：Cold History 召回，三腿混合检索
- **IContextReranker**：候选重排，token overlap F1 scoring
- **ISemanticConsolidator**：ConversationCheckpoint 固化
- **WorkingContext**：跨会话记忆快照，模型介导维护

### ConversationLoop 的职责边界

根据代码分析（`third_party/mira/src/model/conversation_loop.cpp:217`）：

```cpp
Result<ModelRequest> ConversationLoop::build_request(...) {
    // 1. System 指令
    // 2. User envelope (goal + feedback + supplied)
    // 3. 直接拼接 history（完整线性回放）
    request.input.insert(request.input.end(), history.begin(), history.end());
    return request;
}
```

**关键发现**：
- ❌ ConversationLoop **不调用** ContextManager
- ❌ 工具历史**线性拼接**，无压缩
- ❌ 图片每轮**全量复制**，无引用替换
- ✅ 提供 `ModelInputSupplier` 回调扩展点

**结论**：ConversationLoop 是**轻量对话循环**，只管工具调度和轮次控制。上下文管理是
**宿主职责**，需要 Mirage 在 `supply_input` 回调外部主动集成 ContextManager。

详见：`docs/analysis/context-management-capability-gap.md`。

---

## 决策（修订版）

基于上述分析，修正实施路径：**优先集成 Mira 已有能力**，只对真正缺口补充实现。

### 能力分类

| 能力 | DEC-088 原设计 | Mira 已有 | 实施路径 |
|------|---------------|----------|---------|
| 1. 输入去重标注 | ✅ 新增 | ❌ | **Mira 侧实现**（PR#95） |
| 2. 工具历史压缩 | ✅ 微压缩 | ✅ P5 分区 + 水位 | **集成 Layer 0** |
| 3. 自动压缩阈值 | ✅ 45% 触发 | ✅ 70%/85%/95% 水位 | **调整水位配置** |
| 4. 续写提示差异化 | ✅ 截断类型适配 | ❌ | **Mira 侧实现** |
| 5. 图片降级 | ✅ preflight 降级 | ✅ 引用替换 + 图片预算 | **配置 + 可选扩展** |

在 **pinned mira** 实现能力 1、4（真正缺口）；在 **Mirage 集成层**正确使用 Layer 0
（能力 2、3、5）：

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

### 2. 集成 Mira Layer 0：工具历史自动压缩（利用已有能力）

**目标**：利用 Mira 的 `StandardContextManager` 实现工具历史压缩。

**Mira 已有能力**：
- **P5 分区**：`ContextItemKind::ToolResult` 默认映射到 P5（Background）
- **水位裁剪**：超过 trim 水位（默认 70%）时，自动裁剪低优先级项
- **引用替换**：`replaceable_by_reference = true` 的 ToolResult 转为 `ArtifactRef`
- **消费标记**：`consumed = true` 的 ToolResult 可被优先裁剪

**Mirage 集成实现**：

在 `integration/mira/src/model_layer.cpp` 创建 `DesktopContextBuilder`：

```cpp
class DesktopContextBuilder {
public:
    mira::ContextRequest build_context_request(
        const DesktopTaskState& state) {
        
        mira::ContextRequest request;
        request.limits = make_desktop_limits(state.profile);
        request.task_id = state.task_id;
        request.session_id = state.session_id;
        
        // P1: Goal (pinned，永不压缩)
        request.items.push_back(mira::ContextItem{
            .kind = mira::ContextItemKind::Goal,
            .priority = mira::ContextPriority::Essential,
            .content = {mira::TextPart{.text = state.goal}},
            .pinned = true
        });
        
        // P2: CurrentObservation (当前截图)
        if (state.current_screenshot) {
            request.items.push_back(mira::ContextItem{
                .kind = mira::ContextItemKind::CurrentObservation,
                .content = {state.current_screenshot},
                .task_epoch = state.task_epoch,
                .environment_epoch = state.environment_epoch
            });
        }
        
        // P5: ToolHistory (可压缩)
        for (const auto& [call, result] : state.tool_history) {
            // ToolCall
            request.items.push_back(mira::ContextItem{
                .kind = mira::ContextItemKind::ToolCall,
                .content = make_tool_call_parts(call),
                .tool_call_key = call.id,
                .consumed = result.has_value(),
                .sequence = call.sequence
            });
            
            // ToolResult
            if (result) {
                request.items.push_back(mira::ContextItem{
                    .kind = mira::ContextItemKind::ToolResult,
                    .content = {mira::TextPart{.text = result->output}},
                    .payload = result->large_artifact,  // 大截图走 payload
                    .tool_call_key = call.id,
                    .consumed = result->followed_by_model_turn,
                    .replaceable_by_reference = true,  // ← 关键！允许引用替换
                    .sequence = result->sequence
                });
            }
        }
        
        return request;
    }
    
private:
    mira::ContextLimits make_desktop_limits(const mira::ModelProfile& profile) {
        mira::ContextLimits limits;
        limits.context_window_tokens = profile.capabilities.limits.context_window;
        limits.reserved_output_tokens = profile.capabilities.limits.max_output_tokens;
        
        // 桌面任务特化：更激进的压缩阈值
        limits.trim_watermark = 0.45;        // 从 70% 调到 45%
        limits.checkpoint_watermark = 0.70;  // 从 85% 调到 70%
        limits.hard_watermark = 0.90;        // 从 95% 调到 90%
        
        // 图片预算：桌面截图专用
        limits.max_image_tokens = 10000;     // 1张1600px截图 + 余量
        
        return limits;
    }
};
```

在 `supply_input` 回调中：

```cpp
auto supply_input = [&](const auto& spec) 
    -> mira::Result<std::vector<mira::ModelInputItem>> {
    
    // 1. 构建 ContextRequest
    auto request = context_builder.build_context_request(task_state);
    
    // 2. 调用 Mira ContextManager 压缩
    auto prepared = context_manager_->prepare(request);
    if (!prepared) return prepared.error();
    
    // 3. 记录审计日志
    log_context_audit(prepared.value().item_audit);
    
    // 4. 转换为 ModelInputItem（Loop 内部契约）
    return convert_to_model_input_items(prepared.value().input);
};
```

**验收**：
- 15 轮桌面任务，`item_audit` 显示 `DroppedByWatermark` 和 `SelectedByReference`
- Input tokens 相比基线 **-30% ~ -40%**
- 任务成功率不降低

### 3. 调整水位配置（利用已有能力）

**目标**：根据桌面任务特点调整 Mira 的压缩水位。

**Mira 默认水位**：
- `trim_watermark = 0.70`（70% 触发裁剪）
- `checkpoint_watermark = 0.85`（85% 建议 checkpoint）
- `hard_watermark = 0.95`（95% 最低集合检查）

**桌面任务调整**：
```cpp
limits.trim_watermark = 0.45;        // 45% 即触发，更激进
limits.checkpoint_watermark = 0.70;  // 70% 建议固化
limits.hard_watermark = 0.90;        // 90% 拒绝
```

**理由**：
- 桌面任务图片密集，每轮 1600+ tokens
- 早期触发压缩，避免后期被动失败
- 对齐 DEC-088 原设计的 45% 阈值

**验收**：
- 10 轮任务触发 trim（水位 > 45%）
- 压缩后利用率降至 < 60%
- 30 轮任务不因超窗失败

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

### 5. 图片降级策略（配置 + 可选扩展）

**目标**：preflight 余量 < 阈值时，自动降级图片，避免必然截断。

**Mira 已有能力**：
- `ContextLimits::max_image_tokens`：图片预算上限
- 引用替换：大图转 `ArtifactRef`
- 水位拒绝：`MinimumSetTooLarge` 显式拒绝超窗请求

**Mirage 扩展**（在 Mirage 集成层实现）：

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
        items.value().push_back(make_budget_warning(
            "Context budget low. Skip detailed image analysis if not essential."));
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
    
    std::string text = "[Image degraded due to context limit] Screenshot from turn " +
                       std::to_string(current_turn_) + ", " +
                       std::to_string(screenshot.width) + "x" + 
                       std::to_string(screenshot.height) + " pixels.";
    
    if (last_ocr) {
        text += " Previous OCR: " + *last_ocr;
    }
    
    item.content.emplace_back(mira::TextPart{.text = std::move(text)});
    return {std::move(item)};
}
```

**触发条件**：
- 余量 > 20000：正常供给图片
- 余量 10000-20000：图片 + 警告
- 余量 < 10000：降级为文本描述

**实现位置**：
`integration/mira/src/desktop_image_supply.cpp`（Mirage 侧，图片供给是下游职责）

**验收**：
- 余量 15000 时，图片保留 + 警告
- 余量 8000 时，图片降级为文本
- 降级后任务仍可继续，不立即失败

## 实施边界（修订版）

1. **Mira 本地修改**（`third_party/mira`）：
   - **能力 1**：输入去重标注（PR#95 已提交）
   - **能力 4**：续写提示差异化
   - 修改前**必须**先在 `docs/dependency_feedback/ledger.md` 登记反馈台账
     （MIRA-20261014-005 已登记）
   - 单元测试覆盖：reuse 标注、续写提示选择

2. **Mirage 集成层**（`integration/mira`）：
   - **能力 2/3**：集成 Mira 的 `StandardContextManager`
   - **能力 5**：图片降级策略（`desktop_image_supply.cpp`）
   - 创建 `DesktopContextBuilder`，构建 `ContextRequest`
   - 调整水位配置（`ContextLimits`）

3. **不做的**：
   - ❌ 不在 Mirage 侧重复实现工具历史压缩（Mira Layer 0 已有）
   - ❌ 不实现 LLM-based 摘要（成本高、延迟大）——只做规则式压缩
   - ❌ 不持久化压缩状态——每次从完整 history 重新计算（幂等）

4. **如果 Mira 能力不足**：
   - 登记 `MIRA-20261014-006`：ContextManager 缺少桌面特化能力
   - 说明 Mira 通用策略与 Mirage 桌面场景的具体差异
   - 评估是否需要 `DesktopContextManager` 装饰器

## 验收标准（修订版）

### Phase 1：Mira 能力集成（1 周，P0）

1. **Layer 0 集成**：
   - [ ] `DesktopContextBuilder` 实现并测试
   - [ ] 正确设置 `ContextItem` 属性（kind/priority/replaceable_by_reference）
   - [ ] `item_audit` 显示 `DroppedByWatermark` 和 `SelectedByReference`
   - [ ] 15 轮桌面任务 input tokens **-30% ~ -40%**

2. **水位配置调整**：
   - [ ] `trim_watermark = 0.45` 生效
   - [ ] 10 轮任务触发 trim（水位 > 45%）
   - [ ] 压缩后利用率降至 < 60%

3. **续写提示差异化**（Mira PR，可并行）：
   - [ ] 思考截断场景，续写提示包含"concise plain-text"
   - [ ] 续写成功率 ≥60%（当前基线 30%）

### Phase 2：图片复用标注（3 天，等待 PR#95 合并）

4. **输入去重标注集成**：
   - [ ] PR#95 合并后更新 Mirage 的 pinned mira
   - [ ] `DesktopImageTurn::supply_with_reuse_meta` 实现
   - [ ] 相同截图连续 3 轮，第 3 轮请求包含 reuse 提示
   - [ ] 对照实验：有/无标注的思考 token 差异 ≥10%

### Phase 3：图片降级策略（2 天，可并行）

5. **图片降级**：
   - [ ] `supply_with_budget_awareness` 实现
   - [ ] 余量 15000：图片保留 + 警告
   - [ ] 余量 8000：图片降级为文本，任务继续

### 端到端验收（全部能力，1 天）

6. **真实任务回归**：
   - [ ] Bilibili 视频查找任务（15 轮）成功，input tokens < 基线 60%
   - [ ] 30 轮图片密集任务成功完成（不超窗失败）
   - [ ] 任务成功率不降低（相比基线）
   
7. **性能指标**：
   - [ ] ContextManager::prepare 开销 < 50ms/轮
   - [ ] 压缩决策确定性（相同 request → 相同 selection digest）

## Trade-offs 与接受的限制

1. **压缩 vs Cache**：
   - 压缩后前缀不匹配，无法利用 provider cache hit
   - **接受理由**：长会话续航 > 单轮缓存节省（30 轮任务 vs 1-2 轮缓存收益）

2. **规则 vs LLM**：
   - 使用规则式压缩（水位 + 优先级），不使用 LLM-based 摘要
   - **接受理由**：低延迟（< 50ms）、低成本、确定性；M10+ 再考虑 LLM 增强

3. **幂等 vs 持久化**：
   - 不持久化压缩状态，每次从完整 history 重新计算
   - **接受理由**：简化状态管理、避免压缩漂移；计算开销可接受（< 50ms）

4. **通用 vs 特化**：
   - 优先使用 Mira 的通用 ContextManager，只在确认不足时增加桌面特化
   - **接受理由**：避免与 Mira 预算语义双轨并行；如需特化，先反馈台账登记

## 相关文档

- **DEC-087**：输出预算与 preflight（输出侧已对齐）
- **DEC-085**：会话韧性与工具失败计数
- **DEC-053/055**：图片供给缝与去重
- **验证报告**：`docs/analysis/context-management-capability-gap.md`
- **Mira API 文档**：`third_party/mira/docs/api/context-memory.md`
- **反馈台账**：`docs/dependency_feedback/ledger.md` (MIRA-20261014-005)

## 上游交付状态

- **PR#95**：输入复用标注（能力 1）
  - 状态：Open
  - 地址：mira PR#95
  - 合并后更新 Mirage 的 pinned mira

- **能力 4**：续写提示差异化
  - 状态：待提交 PR
  - 需先登记反馈台账（如果尚未登记）

---

**状态**：Proposed  
**日期**：2026-10-14（初版）/ 2026-10-14（修订：集成 Mira 已有能力）  
**负责人**：Mira/Mirage 维护者  
**修订记录**：
- 2026-10-14：补充"Mira 现有能力分析"，修正能力 2/3/5 为集成已有能力
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
