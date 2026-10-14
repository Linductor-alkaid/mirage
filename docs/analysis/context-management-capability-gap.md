# Mirage 上下文管理能力缺口分析报告

## 执行摘要

**验证日期**：2026-10-14

**核心发现**：Mirage 当前**完全没有使用** Mira 已交付的上下文管理和记忆能力（M4-M21）。
ConversationLoop 内部构建请求时直接操作 `ModelInputItem`，绕过了 Mira 的五层上下文
智能架构。

**影响**：
- 图片、工具历史线性累积，无压缩
- 15 轮后必然接近上下文窗口
- 模型重复分析相同截图，浪费 10-20% 思考 token
- 无跨会话记忆能力

**根因**：ConversationLoop 设计为轻量对话循环，不携带上下文管理职责。上下文管理是
**宿主职责**，需要 Mirage 主动集成。

---

## 1. Mira 已交付的上下文管理能力（M4-M21）

### 五层上下文智能架构

```
Layer 0: StandardContextManager (M4)
  ├─ P0-P5 优先级分区
  ├─ 三级水位 (70% trim / 85% checkpoint / 95% hard)
  ├─ 引用替换 (ArtifactRef)
  ├─ 确定性装配 (selection digest)
  └─ 大载荷拒绝 (MinimumSetTooLarge)

Layer 1: IContextRetriever (M17)
  ├─ Cold History 召回
  ├─ 三腿混合检索 (exact / FTS / embedding)
  └─ 可重建投影 (RULE-07)

Layer 2: IContextReranker (M18)
  ├─ Top-K → 候选收敛
  ├─ TokenOverlapContextReranker (参考实现)
  └─ F1 + exact 加成

Layer 3: ISemanticConsolidator (M19)
  ├─ ConversationCheckpoint 固化
  ├─ ProviderSemanticConsolidator (模型介导)
  ├─ 约束/决策/线索/偏好
  └─ commit_conversation_checkpoint

Layer 3+: WorkingContext (M20-M21)
  ├─ WorkingContextSnapshot (确定性投影)
  ├─ IContextCurator (模型介导维护)
  └─ 增量 merge + supersede
```

### 核心契约

- **`ContextItem`**：带 kind/priority/authority/pinned/replaceable_by_reference
- **`ContextRequest`**：宿主提交给 ContextManager 的请求
- **`PreparedModelContext`**：经压缩、审计后的模型输入
- **`ContextLimits`**：窗口预算 + 三级水位阈值

---

## 2. Mirage 当前实现分析

### 2.1 集成点：ConversationLoop

**位置**：`integration/mira/src/model_layer.cpp:1212`

```cpp
mira::ConversationLoop loop(*impl_->gateway, loop_config);
loop.set_tool_registry(registry);
loop.set_model_input_supplier(supply_input);  // ← 唯一扩展点
return loop.run(spec, turn_context);
```

### 2.2 输入供给路径

**位置**：`integration/mira/src/model_layer.cpp:1004-1039`

```cpp
auto supply_input = [&](const auto &specification) 
    -> mira::Result<std::vector<mira::ModelInputItem>> {
    
    // 1. 验证输入（DesktopActionVerification::supply）
    auto items = verification.supply(specification);
    
    // 2. 图片输入（DesktopImageTurn::supply）
    if (image_turn) {
        auto images = image_turn->supply(specification);
        for (auto &item : images.value())
            items.value().push_back(std::move(item));
    }
    
    // 3. 预算通知（文本 notice）
    if (final_turn) {
        // "SYSTEM BUDGET NOTICE: this is the FINAL model turn..."
    }
    
    return items;
};
```

**关键发现**：
- ✅ 返回 `std::vector<ModelInputItem>`（Loop 内部契约）
- ❌ 没有构建 `ContextRequest`
- ❌ 没有调用 `IContextManager::prepare()`
- ❌ 没有使用 `ContextItem` 的优先级、水位、引用替换能力

### 2.3 ConversationLoop 内部请求构建

**位置**：`third_party/mira/src/model/conversation_loop.cpp:217-275`

```cpp
Result<ModelRequest> ConversationLoop::build_request(
    const AgentLoopSpec &spec,
    const std::vector<ModelInputItem> &history,
    const std::string &feedback,
    const std::vector<ModelContentPart> &supplied) {
    
    ModelRequest request = loop_support::begin_model_request(spec, tools_.get());
    
    // 1. System 指令
    ModelInputItem system_item;
    system_item.role = ModelRole::System;
    system_item.content.emplace_back(TextPart{.text = "You are Mira..."});
    
    // 2. User envelope（goal + feedback + supplied）
    ModelInputItem user_item;
    user_item.role = ModelRole::User;
    user_item.content.emplace_back(TextPart{.text = "Request: " + spec.goal});
    user_item.content.insert(user_item.content.end(), supplied.begin(), supplied.end());
    
    // 3. 直接拼接历史
    request.input.push_back(std::move(system_item));
    request.input.push_back(std::move(user_item));
    request.input.insert(request.input.end(), history.begin(), history.end());
    
    return request;
}
```

**关键发现**：
- ❌ 历史 `history` 完整线性拼接，无压缩
- ❌ `supplied` 内容（图片）每轮全量复制
- ❌ 没有调用 ContextManager
- ❌ 没有水位检查、引用替换、优先级排序

---

## 3. 能力缺口总结

### 3.1 ConversationLoop 的职责边界

根据代码分析和 API 文档：

| 职责 | ConversationLoop | ContextManager | 宿主（Mirage） |
|------|-----------------|----------------|---------------|
| 请求构建 | ✅ System + User envelope | ❌ | ❌ |
| 工具历史管理 | ✅ 线性拼接 | ❌ | ❌ |
| 上下文压缩 | ❌ | ✅ P0-P5 + 水位 | 需集成 |
| 引用替换 | ❌ | ✅ ArtifactRef | 需集成 |
| 检索召回 | ❌ | ✅ Layer 1 | 需集成 |
| 语义固化 | ❌ | ✅ Layer 3 | 需集成 |
| 输入供给 | ✅ 回调接口 | ❌ | ✅ 已实现 |

**结论**：ConversationLoop 是**轻量对话循环**，不承载上下文管理职责。上下文管理是
**宿主职责**，需要 Mirage 在 `supply_input` 回调外部主动集成 ContextManager。

### 3.2 当前架构的后果

```
Turn 1:  System + User(goal + 图1) + history(0)           → 正常
Turn 5:  System + User(goal + 图5) + history(4轮工具)      → 开始累积
Turn 10: System + User(goal + 图10) + history(9轮工具)     → 接近水位
Turn 15: System + User(goal + 图15) + history(14轮工具)    → 超窗失败
```

**问题**：
1. **图片重复分析**：每轮 `图N` 都是新 `ImagePart`，模型不知道是否复用
2. **工具历史膨胀**：14 轮 × (ToolCall + ToolResult) 完整保留
3. **无压缩机制**：只有 DEC-087 的输出预算收缩，输入侧无压缩
4. **无跨会话记忆**：每个 Task 从零开始

---

## 4. 为何 DEC-088 设计重复？

### 4.1 DEC-088 的五个能力

| 能力 | DEC-088 设计 | Mira 已有 |
|------|-------------|----------|
| 1. 输入去重标注 | ModelInputSupplier 扩展 | ❌ 缺失（PR#95 补充） |
| 2. 工具历史微压缩 | 最近2轮完整 + 3-5摘要 + 6+折叠 | ✅ **P5分区 + 水位** |
| 3. 自动压缩阈值 | 45% 窗口触发 | ✅ **70%/85%/95% 水位** |
| 4. 续写提示差异化 | 截断类型 → 提示模板 | ❌ 缺失 |
| 5. 图片降级策略 | preflight < 20k → 缩略图 | ✅ **引用替换 + 图片预算** |

**重复部分**：能力 2、3、5 的核心语义 Mira 已实现，DEC-088 在 Mirage 侧重新设计。

**不重复部分**：能力 1（PR#95）和能力 4（续写提示）是真正的缺口。

### 4.2 根本原因

**DEC-088 的隐含假设**：以为 ConversationLoop 内部包含上下文管理，所以设计了
"在 Mirage 侧实现压缩"的方案。

**实际情况**：ConversationLoop 不管上下文压缩，ContextManager 是独立组件，需要
宿主主动集成。

---

## 5. 正确的集成路径

### 5.1 架构修正

```
┌─────────────────────────────────────────────────────────┐
│ Mirage 宿主层 (model_layer.cpp)                          │
├─────────────────────────────────────────────────────────┤
│ 1. 构建 ContextRequest                                   │
│    ├─ P1: Goal (pinned)                                 │
│    ├─ P2: CurrentObservation (当前图片)                  │
│    ├─ P3: CheckpointSummary (WorkingContext, 可选)       │
│    ├─ P4: RetrievedMemory (跨会话召回, 可选)             │
│    └─ P5: ToolHistory (可压缩)                           │
│                                                           │
│ 2. 调用 ContextManager::prepare(request)                 │
│    → PreparedModelContext (已压缩、审计)                  │
│                                                           │
│ 3. 转换为 ModelInputItem 供给 ConversationLoop           │
│    ├─ supply_input 返回转换后的 items                     │
│    └─ Loop 内部只管工具调度和对话控制                      │
└─────────────────────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────────────────────┐
│ Mira ConversationLoop (conversation_loop.cpp)            │
├─────────────────────────────────────────────────────────┤
│ - 接收 supplied items (已压缩)                           │
│ - 构建 System + User envelope                            │
│ - 管理工具调度和轮次控制                                   │
│ - 不关心上下文压缩（宿主已处理）                           │
└─────────────────────────────────────────────────────────┘
```

### 5.2 集成代码示例（伪代码）

```cpp
// integration/mira/src/model_layer.cpp

class DesktopTaskContextBuilder {
public:
    explicit DesktopTaskContextBuilder(
        std::shared_ptr<mira::IContextManager> context_manager)
        : context_manager_(std::move(context_manager)) {}
    
    mira::Result<std::vector<mira::ModelInputItem>> 
    build_context(const DesktopTaskState& state) {
        // 1. 构建 ContextRequest
        mira::ContextRequest request;
        request.limits = make_desktop_limits(state.profile);
        
        // P1: Goal (pinned)
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
            request.items.push_back(make_tool_call_item(call));
            if (result) {
                request.items.push_back(make_tool_result_item(
                    result, 
                    /* replaceable_by_reference = */ true  // ← 关键
                ));
            }
        }
        
        // 2. 压缩
        auto prepared = context_manager_->prepare(request);
        if (!prepared) return prepared.error();
        
        // 3. 审计日志
        log_context_audit(prepared.value().item_audit);
        
        // 4. 转换为 ModelInputItem
        return convert_to_model_input_items(prepared.value().input);
    }

private:
    std::shared_ptr<mira::IContextManager> context_manager_;
};
```

---

## 6. 修正后的实施方案

### Phase 1：Layer 0 集成（1 周，P0）

#### 1.1 创建 DesktopContextManager 适配器
- 封装 Mira 的 `StandardContextManager`
- 实现 `ContextRequest` 构建逻辑
- 转换 `PreparedModelContext` → `ModelInputItem`

#### 1.2 调整水位配置
```cpp
ContextLimits make_desktop_context_limits(const ModelProfile& profile) {
    ContextLimits limits;
    limits.context_window_tokens = profile.context_window;
    limits.reserved_output_tokens = profile.max_output_tokens;
    limits.trim_watermark = 0.45;        // 桌面任务特化
    limits.checkpoint_watermark = 0.70;
    limits.hard_watermark = 0.90;
    limits.max_image_tokens = 10000;     // 1张截图 + 余量
    return limits;
}
```

#### 1.3 正确设置 ContextItem 属性
- `ToolResult` → `replaceable_by_reference = true`
- `consumed = true` 标记已处理结果
- 大截图放在 `payload`，不放 `content`

**验收**：
- 15 轮任务的 input tokens 相比基线 **-30% ~ -40%**
- `item_audit` 显示 `SelectedByReference` 和 `DroppedByWatermark`
- 任务成功率不降低

---

### Phase 2：图片复用标注（3 天，P0）

等待 **PR#95** 合并后集成到 Mirage：

```cpp
// integration/mira/src/desktop_image_supply.cpp

mira::SuppliedInput DesktopImageTurn::supply_with_reuse_meta(
    const mira::AgentLoopSpec& spec) {
    
    mira::SuppliedInput input;
    
    // 图片内容
    input.items = make_image_items(current_screenshot_);
    
    // 复用元数据（PR#95 新增）
    if (current_screenshot_id_ == last_supplied_id_) {
        input.reuse_meta.reuse_count = ++consecutive_reuse_;
        input.reuse_meta.semantic_label = 
            "desktop-screenshot-reused-" + std::to_string(consecutive_reuse_) + "x";
    } else {
        consecutive_reuse_ = 0;
    }
    last_supplied_id_ = current_screenshot_id_;
    
    return input;
}
```

**验收**：
- 模型看到 "This is the 3rd time seeing this screenshot"
- 思考 token 相比基线 **-10% ~ -20%**

---

### Phase 3：续写提示差异化（2 天，P0）

独立能力，可与 Phase 1/2 并行：

```cpp
// third_party/mira/src/model/provider_continuation.cpp

std::string make_continuation_prompt(
    const ContinuationContext& ctx) {
    
    if (ctx.truncated_in_thinking) {
        return "Continue your internal reasoning from where it was cut off. "
               "Do not restart or summarize — pick up mid-thought.";
    } else {
        return "Complete your answer. The previous response was truncated.";
    }
}
```

**验收**：
- 续写成功率从 30% 提升到 **60%+**
- 思考截断后不再"让我重新思考"

---

### Phase 4：Layer 3 集成（2 周，P1，可选）

如果需要跨会话记忆能力：

```cpp
// 使用 WorkingContextSnapshot
auto snapshot = load_working_context_snapshot(session_id, task_epoch);
if (snapshot) {
    auto items = mira::context_items_from_working_context(*snapshot);
    for (auto& item : items) {
        request.items.push_back(std::move(item));  // P3 优先级
    }
}
```

---

## 7. DEC-088 和实施计划的修订建议

### 7.1 需要修订的内容

1. **DEC-088 第 4.2 节**：增加"Mira 现有能力分析"
   - 列出 Layer 0-3 的已交付能力
   - 说明 ConversationLoop 的职责边界
   - 修正能力 2/3/5 为"利用 Mira Layer 0"

2. **实施计划 Phase 2**：改为"集成 Mira ContextManager"
   - 删除"工具历史微压缩"自研实现
   - 改为"调用 StandardContextManager::prepare"
   - 减少工作量估算（10 天 → 3 天）

3. **如果 Mira 能力确实不足**：
   - 登记 `MIRA-20261014-006`：ContextManager 缺少桌面特化能力
   - 说明 Mira 通用策略与 Mirage 桌面场景的差异
   - 评估是否需要 DesktopContextManager 装饰器

### 7.2 保留的内容

- ✅ 能力 1：输入去重标注（PR#95）
- ✅ 能力 4：续写提示差异化
- ✅ 问题分析和收益预估
- ✅ 验收标准和测试计划

---

## 8. 行动建议

### 立即（今天）
1. ✅ 完成本验证报告
2. ⏭ 修订 DEC-088 和实施计划
3. ⏭ 登记反馈台账（如果需要）

### 本周
1. 实施 Phase 1（Layer 0 集成）
2. 编写集成测试
3. 真实任务验证（Bilibili 视频查找）

### 下周
1. 等待 PR#95 合并
2. 实施 Phase 2（图片复用标注）
3. 实施 Phase 3（续写提示差异化）
4. 端到端回归测试

---

## 附录：关键代码位置

### Mira
- `third_party/mira/include/mira/context_manager.hpp`：IContextManager 接口
- `third_party/mira/src/model/conversation_loop.cpp:217`：build_request
- `third_party/mira/docs/api/context-memory.md`：完整能力文档

### Mirage
- `integration/mira/src/model_layer.cpp:1212`：ConversationLoop 使用点
- `integration/mira/src/model_layer.cpp:1004`：supply_input 回调
- `integration/mira/src/desktop_image_supply.cpp`：图片供给实现

---

**验证人**：Claude Opus 5.5  
**日期**：2026-10-14
