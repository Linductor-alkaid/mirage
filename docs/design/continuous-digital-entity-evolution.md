# Mira / Mirage 持续性数字个体演进路线图

> **状态**: Under Revision (v1.1)  
> **日期**: 2026-10-16（v1.1 修订）  
> **负责人**: Mirage 维护者  
> **相关设计**: [桌面端设计方案](Mirage：Linux%20-%20Windows%20桌面端设计方案.md)、[mira_pet 设计](mira-pet.md)  
> **评审**: 2026-10-15 架构评审通过，需完成修订后实施

## 执行摘要

本文档提出将 Mira/Mirage 从传统的**会话驱动 Agent 工具**演进为**持续性数字个体**的六阶段路线图。核心构想：建立以**统一身份为主体、单一注意力为核心、统一事件时间线组织经历、动态工作记忆维持认知连续性、持久化任务状态保证执行可靠性，并以 MiraPet 作为主要外部表现形式**的新型架构。

**关键演进方向**：
- 多个独立会话 → 统一身份主体
- 静态上下文历史 → 动态工作记忆  
- 并行认知实例 → 单一注意力 + 受控并发
- 会话边界定义存在 → 持续事件时间线
- 任务进度指示器 → 个体状态外显（MiraPet）

**v1.1 架构修订重点**（基于 2026-10-15 评审）：
- 明确 AttentionManager 职责边界，拆分为 Policy/Coordinator/Projection 三层
- 建立安全的任务抢占与恢复协议（Prepare→Suspend→Release→Commit→Resume）
- 重构记忆激活模型，分离基础激活度与查询相关性
- Phase 1 即确定 SQLite 作为长期存储，避免 JSON 分片复杂化
- MiraPet 单向投影，不反向影响认知决策
- 持续性认知架构与自主行为系统明确分层
- 并行开发策略：AgentIdentity/EventTimeline 作为公共基础

演进遵循渐进式原则：保持向后兼容、增量验证、复用已有能力。预计 M11-M14 完成核心能力，M15+ 扩展自主行为与多模态。

---

## 1. 愿景概述

### 1.1 核心问题

当前 Mira/Mirage 以及主流 Agent 系统仍主要沿用**会话驱动的交互范式**。这种设计对文本生成与独立任务有效，但当 Agent 具备完整计算机操作能力并以持续存在的数字个体形态与用户交互时，会话机制的局限性显现：

**问题1：桌面资源独占冲突**  
鼠标、键盘、窗口焦点构成高度耦合的共享资源。多个独立会话同时操作桌面时，即使底层支持并发，也易产生冲突：点击落错位置、键入进错窗口、视觉定位失效。根源在于多任务对同一组有状态交互资源缺乏统一协调。

**问题2：注意力碎片化**  
传统多会话模型允许多个独立认知实例并行，但桌面操作需要连续视觉反馈与精确控制。人类在此类任务中表现出集中的主要注意力。当前架构缺乏统一的全局协调机制来决定"当前应关注哪个任务"。

**问题3：记忆与身份割裂**  
不同会话具有相对独立的上下文，但用户期望 Agent 作为**一个**持续存在的个体：记住长期目标、维持稳定偏好、在不同交互中保持连续性。一个几分钟前的无意义闲聊不值得占据工作记忆，而几个月前的重要承诺却应始终影响行为。

**问题4：MiraPet 定位模糊**  
如果 MiraPet 只展示某个任务的执行状态，本质上仍是任务进度指示器。当多个任务同时存在时，展示哪个任务产生歧义。

### 1.2 演进目标

**目标架构特征**：
1. **统一身份主体**：所有交互、任务、经历关联到同一 AgentIdentity
2. **单一注意力焦点**：任意时刻只有一个主要关注对象，其他任务后台执行或等待
3. **持续事件时间线**：用户消息、任务创建、工具调用、系统事件统一组织为连续经历
4. **动态工作记忆**：按相关性、时间、重要性动态构建上下文，而非静态历史堆积
5. **MiraPet 个体表达**：不展示"某个任务状态"，而展示"Mira 这个个体当前在做什么"

**保持能力**：
- kairo 并发调度继续支持后台任务（编译、网络、文件处理）
- ActionCoordinator 桌面租约机制保持独占控制
- 会话概念作为兼容层保留，支持传统模式切换

---

## 2. 当前架构分析

### 2.1 现有会话与任务模型

**会话管理（M5，DEC-021）**：
- 协议：`session.list`、`session.open`、`session.history`
- 每个会话绑定到 pinned MiraRuntime
- 任务通过 `task.submit` 可选绑定 `session_id`，缺席时落主会话
- 历史由 `MemoryEventStore` 持久化，每次重建对话视图

**任务状态机（M1，DEC-007）**：
```
Idle → Active (Observing/Reasoning/Planning/Acting/Verifying/Recovering)
     → Paused / Cancelling
     → 终态 (Completed/Failed/Cancelled)
```

**桌面资源协调（M7，DEC-054）**：
- `ActionCoordinator` 管理桌面输入租约
- 共享 lease 无队列，另一会话/workflow 输入返回 `desktop_busy`
- 窗口ID、observation_scope、environment_epoch 绑定观察与动作

**并发承载（强制，AGENTS.md）**：
- RuntimeService 持有唯一 `kairo::Executor` owner
- 所有异步任务、定时、取消必须通过 Executor 管理
- 跨上下文通信使用 `executor::comm`（MpscChannel/LatestMailbox/Topic）

**分析**：
- ✅ 已有统一 Runtime owner，提供全局任务协调基础
- ❌ 会话仍是主要组织单位，任务/历史按会话边界组织
- ❌ 无统一注意力管理，ActionCoordinator 只管桌面租约
- ❌ 上下文 = 会话历史，未实现动态记忆激活

### 2.2 MiraPet 当前实现

**显示状态投影（M8，mira-pet.md）**：
```
RuntimeService → 统一投影 → IPC (Topic + 快照) → mira_pet 播放器
```

**九态模型**：
- 运行阶段：`starting`/`idle`/`thinking`/`looking`/`mouse`/`typing`
- 终态展示：`task_success`/`task_failure`
- 安全状态：`waiting`/`paused`/`takeover`/`cancelled`/`disconnected`

**多任务选择策略（§3）**：
1. 全局安全状态优先
2. 其次当前桌面输入租约持有者
3. 其余维持当前仍活动的显示对象

**关键洞察**：MiraPet 已在**呈现层**实现"统一个体状态"投影，但：
- 投影源仍是多个独立会话/任务的集合
- 选择逻辑基于技术状态（租约、安全态），非"Agent 主观决策"
- 还没有反向影响：MiraPet 状态不驱动任务调度

### 2.3 可复用能力核对

| 能力域 | 已有基础 | 可复用入口 |
|--------|---------|-----------|
| 统一执行 owner | ✅ RuntimeService 唯一 Executor | 全局任务调度基础 |
| 跨任务通信 | ✅ kairo::comm (Topic/Mailbox/Channel) | 注意力切换、状态同步 |
| 持久化 | ✅ MemoryEventStore (DEC-021) | 长期记忆存储 |
| 状态机 | ✅ pinned TaskState + MiraHost 五态 | 任务生命周期管理 |
| 统一投影 | ✅ mira_pet 显示状态选择器 | 外部表现基础 |
| 桌面独占协调 | ✅ ActionCoordinator lease | 资源仲裁 |
| 事件驱动 | ✅ IPC event subscription (DEC-012) | 事件时间线基础 |
| 动态记忆 | ❌ 未实现 | 需新建 |
| 注意力管理 | ❌ 未实现 | 需新建 |
| 统一身份 | ⚠️ 部分（MiraHost owner） | 需强化 |

---

## 3. 研究与业界实践

### 3.1 持续记忆架构

**Continuum Memory Architecture (CMA)** - [arxiv 2601.09913](https://arxiv.org/pdf/2601.09913v1)：
- **持久存储**：内部状态跨会话保持
- **选择性保留**：不是所有信息等权保留
- **关联路由**：通过语义链接检索相关记忆
- **时间链**：事件按时间关联
- **抽象巩固**：细节逐步转化为高层知识

**Agent Cognitive Compressor (ACC)** - [arxiv 2601.11653](https://arxiv.org/pdf/2601.11653v1)：
- 用**有界内部状态**替代完整转录本重放
- 每轮更新状态而非追加消息
- 分离 artifact 召回与状态承诺，防止记忆污染

**人类启发式记忆** - [arxiv 2605.08538](https://arxiv.org/pdf/2605.08538v1)：
- **睡眠期巩固**：定期将短期记忆转化为长期表示
- **干扰遗忘**：新信息覆盖不重要的旧信息
- **检索时重巩固**：回忆本身增强记忆
- **实体知识图谱**：结构化知识独立于情景记忆

**启示**：记忆是**动态激活**而非静态存储；遗忘是**激活度降低**而非数据删除；情景记忆、语义知识、工作记忆三者分离。

### 3.2 事件驱动 Agent 架构

**Event-Driven Multi-Agent Orchestration** - [arxiv 2606.20058](https://arxiv.org/abs/2606.20058)：
- 从请求-响应转向**持续事件流**
- 专业 Agent 订阅特定事件类型，响应延迟降低 90%
- Observe-Analyze-Act 循环

**统一架构模式** - [atlan.com](https://atlan.com/know/event-driven-architecture-for-ai-agents/)：
- 将监控 Agent 与后端服务器合并为单一进程
- 消除进程间通信延迟

**Autonomous Agentic Systems** - [confluent.io](https://www.confluent.io/blog/autonomous-agentic-event-driven-systems-architecture/)：
- 持续感知事件 → 推理共享状态 → 采取行动 → 学习结果
- 无需人工在环编排

**启示**：事件时间线替代会话边界；Agent 通过订阅机制响应事件；异步处理比同步请求更符合持续个体特征。

### 3.3 具身 Agent 与持续呈现

**Dual-Embodiment Framework** - [arxiv 2605.03882](https://arxiv.org/pdf/2605.03882v1)：
- 物理对象扩展为持续数字伴侣
- 混合环境存在与校准代理
- 忠实身份、互惠记忆

**Desktop Companion Avatars** - [dev.to](https://dev.to/xadenai/giving-an-ai-a-body-building-a-desktop-companion-avatar-for-macos-5el9)：
- 使用运动响应、空间移动、物理姿态传达状态
- 可见 Agent 使用户更自然提供上下文
- 动画角色信号思考状态时，用户容忍更长处理时间

**Spatially Situated Virtual Embodiment** - [arxiv 2606.28714](https://arxiv.org/html/2606.28714v1)：
- 空间化虚拟具身是**关系升级**的一种形式
- 使 AI 伴侣关系更在场、社交可读、情境敏感

**启示**：可视化角色显著改变交互方式；状态动画比文本通知更自然；持续呈现建立信任与认同。

### 3.4 对 Mirage 的启示

| 研究发现 | Mirage 应用 |
|---------|------------|
| 动态记忆激活 | 实现基于相关性/时间/重要性的上下文动态构建 |
| 睡眠期巩固 | 空闲时将情景记忆抽象为语义知识 |
| 事件订阅机制 | 扩展 IPC event system，支持内部组件订阅 |
| 单一注意力焦点 | 新增 AttentionManager，决定当前主要关注对象 |
| MiraPet 状态驱动 | MiraPet 不仅展示状态，还反映 Agent"主观决策" |
| 混合多线索检索 | 记忆检索结合语义、时间、任务关联、回忆次数 |


---

## 4. 演进路线图

### 4.1 总体策略

**渐进式演进原则**：
1. **保持兼容**：不立即移除会话机制，作为过渡层保留
2. **增量验证**：每个阶段可独立验证，拆分架构调整风险
3. **能力优先**：先建立新能力（注意力、记忆），再调整 UI
4. **用户可选**：早期阶段提供传统模式与新模式切换

**依赖约束遵守**（AGENTS.md）：
- 复用 pinned mira/mirador 已交付能力
- 不自建线程池、调度器、队列
- 所有并发通过 Executor 管理
- 依赖缺口按反馈台账流程登记

### 4.2 Phase 1: 统一身份与全局事件时间线（M11 提案）

**目标**：建立持续身份基础，将不同会话/任务关联到同一主体。

**P1.1 Agent Identity 模型**
- 在 `runtime/` 新增 `agent_identity/`
- 定义 `AgentIdentity`：唯一 ID、创建时间、身份档案
- 所有 Session/Task/Workflow 关联到同一 Identity
- 持久化扩展 M1 `service.json`（DEC-011）

**P1.2 Global Event Timeline**
- 扩展 `MemoryEventStore`（DEC-021）支持跨会话事件
- 事件类型：UserMessage、TaskCreated、ActionExecuted、SystemNotification、ExternalTrigger
- 每个事件携带：timestamp、source、agent_identity
- 通过 `EventTimeline::query(time_range, source_filter, type_filter)` 检索

**P1.3 统一事件订阅扩展**
- 复用 DEC-012 的 `events.subscribe` 机制
- 新增内部事件：`agent.identity_initialized`、`attention.focus_changed`
- RuntimeService 组件可订阅内部事件

**P1.4 兼容层**
- Session 概念保留，内部记录关联到 AgentIdentity
- `session.list` 响应增加 `agent_identity_id` 字段
- `task.submit` 可选指定 session，缺席仍落主会话

**验收标准**：
- [ ] 多个 Session 的事件可按时间线统一查询
- [ ] MiraPet 显示状态关联到 AgentIdentity
- [ ] 重启后 Agent Identity 可恢复

**依赖能力**：✅ MemoryEventStore、executor::comm::Topic、DEC-011 持久化框架

**预计工期**：3 周

---

### 4.3 Phase 2: 注意力管理与安全任务切换（M12 提案）

**目标**：建立单一主注意力模型，实现安全的任务抢占、挂起与恢复。

**架构修订要点**（评审 P0）：
- 职责分离：AttentionPolicy（决策）、AttentionCoordinator（切换协调）、ActionCoordinator（资源管理，已有）、MiraPetProjection（状态表达）
- 安全切换协议：Prepare → Suspend → Release → Commit → Resume
- MiraPet 单向投影：不反向影响认知决策

**P2.1 注意力职责分层**

```cpp
// runtime/attention/attention_policy.hpp
class AttentionPolicy {
public:
  // 评估应关注的任务（决策层，无状态修改）
  struct Evaluation {
    std::string recommended_target_id;
    float urgency_score;  // 0.0-1.0
    std::string rationale;
  };
  
  Evaluation evaluate_focus(
    const std::vector<TaskState>& active_tasks,
    const std::optional<Focus>& current_focus,
    const ExternalEvent& trigger
  );
  
  // 稳定性策略
  struct StabilityPolicy {
    std::chrono::seconds min_focus_duration{30};
    float urgency_threshold_for_interrupt{0.8};
  };
};

// runtime/attention/attention_coordinator.hpp
class AttentionCoordinator {
public:
  // 安全切换状态机
  enum class SwitchPhase {
    Idle,
    Preparing,      // 检查当前任务可中断性
    Suspending,     // 等待安全检查点
    Releasing,      // 释放独占资源
    Committing,     // 原子更新焦点
    Resuming        // 恢复目标任务
  };
  
  // 切换请求返回
  struct SwitchResult {
    bool accepted;
    SwitchPhase current_phase;
    std::optional<std::string> error;
    std::chrono::milliseconds elapsed;
  };
  
  // 发起切换（异步）
  kairo::Future<SwitchResult> request_switch(
    const std::string& target_task_id,
    SwitchPriority priority
  );
  
  // 查询当前已提交焦点
  std::optional<Focus> committed_focus() const;
  
  // 回滚机制（切换失败时）
  void rollback_to(const Focus& previous);
};
```

**P2.2 安全切换协议**

**协议流程**：
```
1. Prepare（准备阶段）
   - 查询当前任务状态（pinned TaskState）
   - 检查是否持有桌面输入租约
   - 评估可中断性：Observing/Verifying 可中断，Acting 需等待
   - 拒绝条件：不可中断且优先级不足

2. Suspend（挂起阶段）
   - 等待当前任务到达安全检查点
   - 持久化任务执行状态（DurableTaskState，见 P2.6）
   - 超时机制：默认 10 秒，超时强制进入 Releasing

3. Release（释放阶段）
   - 释放桌面输入租约（通知 ActionCoordinator）
   - 标记任务为 Paused 状态
   - 清理临时资源（非持久化上下文）

4. Commit（提交阶段）
   - 原子更新全局焦点状态（单一写入点）
   - 发布 `attention.focus_changed` 事件
   - 此时 MiraPetProjection 收到通知并更新显示

5. Resume（恢复阶段）
   - 从 DurableTaskState 恢复目标任务
   - 重新申请所需资源（可能失败 → 回滚）
   - 标记任务为 Active 状态
```

**失败恢复**：
- Prepare/Suspend 失败 → Defer（延迟到当前任务完成）
- Release 失败 → 记录警告，强制释放（资源泄漏风险）
- Commit 失败 → 系统故障，触发 Failed 状态
- Resume 失败 → 回滚到原焦点，目标任务标记为 FailedToResume

**P2.3 与 ActionCoordinator 解耦**

ActionCoordinator 继续独立管理桌面输入租约，不感知注意力焦点。AttentionCoordinator 在 Release 阶段主动释放租约，在 Resume 阶段重新申请。两者通过事件通信，不直接耦合。

**租约与焦点一致性不变量**：
- 任意时刻，桌面输入租约持有者必须是已提交焦点任务（或为空）
- 焦点切换过程中，租约先释放，新焦点提交后再申请
- 验证方式：记录每次租约授予/释放事件，与焦点事件对齐

**P2.4 MiraPet 单向投影**

```cpp
// runtime/pet/mirapets_projection.hpp
class MiraPetProjection {
public:
  // 订阅已提交焦点
  void subscribe_focus(kairo::comm::MailboxHandle<Focus> focus_mailbox);
  
  // 订阅任务状态
  void subscribe_task_states(kairo::comm::TopicHandle<TaskStateUpdate> updates);
  
  // 生成外部表现状态（纯函数）
  PetDisplayState project_state(
    const Focus& focus,
    const TaskState& task_state,
    const ActionCoordinatorStatus& action_status
  );
  
  // 发布到 IPC
  void publish_to_ipc();
};
```

**单向原则**：
- MiraPetProjection 只消费状态，从不修改
- 用户通过 MiraPet 的交互（点击、右键菜单）作为新的 ExternalEvent 进入系统
- AttentionPolicy 可配置"避免频繁切换"策略，但不读取 MiraPet 当前播放状态
- 稳定性通过 `min_focus_duration` 和 `urgency_threshold` 在认知层实现

**P2.5 后台任务执行模型**

焦点切换不影响后台任务：
- 编译、网络请求、文件处理继续由 kairo Executor 调度
- 后台任务完成时产生 ExternalEvent（`BackgroundTaskCompleted`）
- AttentionPolicy 评估是否切换焦点到该任务（通常 urgency 较低）
- 焦点任务可以同时运行多个后台子任务

**P2.6 持久化任务状态（DurableTaskState）**

```cpp
// runtime/task/durable_task_state.hpp
struct DurableTaskState {
  std::string task_id;
  std::string goal;
  TaskPhase current_phase;  // Observing/Acting/...
  
  // 执行检查点
  std::vector<CompletedStep> completed_steps;
  std::optional<InProgressStep> in_progress_step;
  
  // 资源占用
  std::vector<std::string> held_resources;
  
  // 恢复所需信息
  json recovery_context;  // 工具调用结果、observation 引用等
  
  // 持久化到 EventStore
  void save_checkpoint();
  static std::optional<DurableTaskState> load_checkpoint(const std::string& task_id);
};
```

**恢复保证**：
- 任务挂起时必须保存检查点
- 崩溃恢复时从最近检查点恢复
- 不可恢复的任务标记为 FailedToResume，通知用户

**验收标准（修订）**：
- [ ] **焦点一致性**：任意时刻只有一个已提交焦点，日志可追溯
- [ ] **租约一致性**：桌面租约持有者始终是焦点任务或为空
- [ ] **切换成功率**：正常场景 >98%，异常场景回滚成功 >95%
- [ ] **恢复正确率**：挂起任务恢复后状态完整 >99%
- [ ] **切换延迟分层**：
  - Prepare 决策：< 100ms (P95)
  - 完整切换：< 5s (P95，包含等待安全检查点)
  - 紧急切换（安全状态）：< 500ms (P95)
- [ ] **后台任务隔离**：焦点任务响应时间不受后台影响
- [ ] **崩溃恢复**：进程重启后，未完成任务可从检查点恢复

**依赖能力**：
- ✅ ActionCoordinator lease
- ✅ kairo::comm::LatestMailbox / Topic
- ✅ pinned TaskState 查询
- ✅ MemoryEventStore（持久化检查点）

**预计工期**：6 周（原 4 周，增加切换协议与恢复测试）

---

### 4.4 Phase 3: 动态工作记忆（M13 提案）

**目标**：从静态会话历史转向动态构建的工作记忆。

**架构修订要点**（评审 P1）：
- 分离基础记忆激活度与查询相关性评分
- 避免乘法模型导致的归零问题
- 长期承诺/未完成任务/身份事实采用独立保留策略
- 回忆强化考虑证据可信度，防止错误自我强化
- Phase 1 即确定 SQLite 作为长期存储后端

**P3.1 记忆分层模型**
```
层次          内容                    激活机制                保留策略
──────────────────────────────────────────────────────────────────
工作记忆      当前任务上下文          始终激活                会话内内存
情景记忆      具体事件、交互历史      按相关性/时间检索       长期 SQLite
语义记忆      抽象知识、技能          按主题检索              长期 SQLite
身份记忆      长期偏好、承诺          始终加载，独立保留      持久化 JSON
未完成任务    活动与挂起任务          独立跟踪，不受遗忘影响  DurableTaskState
```

**P3.2 记忆激活与检索模型（修订）**

```cpp
// runtime/memory/memory_entry.hpp
struct MemoryEntry {
  std::string id;
  MemoryType type;  // Episodic / Semantic / Identity
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_recalled;
  int recall_count;
  
  // 基础激活度（独立于查询）
  struct BaseActivation {
    float importance;        // 0.0-1.0，创建时评估
    float recency_decay;     // 时间衰退系数
    int effective_recalls;   // 有效回忆次数（考虑可信度）
    
    float compute() const {
      float recency_factor = std::exp(-recency_decay * days_since_last_recall());
      float frequency_factor = std::log(1 + effective_recalls);
      
      // 加法模型，避免归零
      return importance * 0.5 + recency_factor * 0.3 + frequency_factor * 0.2;
    }
  };
  
  BaseActivation base_activation;
  
  // 查询相关性评分（动态计算）
  struct QueryRelevance {
    float semantic_similarity;  // 语义相似度 0.0-1.0
    float task_association;     // 与当前任务关联 0.0-1.0
    float temporal_relevance;   // 时间相关性 0.0-1.0
    
    float compute() const {
      return semantic_similarity * 0.5 + task_association * 0.3 + temporal_relevance * 0.2;
    }
  };
};

// runtime/memory/memory_store.hpp
class MemoryStore {
public:
  // 检索相关记忆（两阶段）
  std::vector<MemoryEntry> retrieve_relevant(
    const std::string& query,
    const std::optional<std::string>& task_context,
    size_t token_budget
  ) {
    // 1. 按基础激活度过滤（阈值 0.3）
    auto candidates = filter_by_base_activation(0.3);
    
    // 2. 计算查询相关性
    for (auto& entry : candidates) {
      entry.query_relevance = compute_relevance(query, task_context, entry);
    }
    
    // 3. 综合排序
    sort_by_combined_score(candidates);  // base * 0.4 + relevance * 0.6
    
    // 4. Token 预算截断
    return take_within_budget(candidates, token_budget);
  }
  
  // 记忆巩固（情景 → 语义）
  void consolidate_episodic_to_semantic();
  
  // 时间衰退（批量更新激活度）
  void decay_unused_memories();
  
  // 回忆强化（考虑可信度）
  void strengthen_recall(
    const std::string& memory_id,
    float credibility  // 0.0-1.0，来自证据验证
  );
};
```

**激活度计算修订说明**：

原模型问题：
```cpp
// ❌ 原模型：乘法导致归零
activation = importance × recency × frequency × relevance
// 首次回忆 frequency=0 → 归零
// 长期未访问 recency→0 → 归零
```

新模型：
```cpp
// ✅ 新模型：基础激活与查询分离
base_activation = importance×0.5 + recency×0.3 + frequency×0.2  // 加法
query_relevance = similarity×0.5 + association×0.3 + temporal×0.2  // 独立评分
final_score = base_activation×0.4 + query_relevance×0.6  // 综合排序
```

优势：
- 首次回忆不会归零（importance 保底）
- 长期未访问的重要记忆仍可检索（importance 高则 base 高）
- 查询无关但重要的记忆可通过 base_activation 保留
- 回忆强化有界（effective_recalls 上限）

**P3.3 独立保留策略**

**身份记忆**（Identity Memory）：
- 类型：长期偏好、用户承诺、重要决策
- 保留：独立持久化到 `agent_identity.json`，始终加载到工作记忆
- 不受时间衰退影响
- 示例：`{"preference": "user_prefers_python", "commitment": "review_pr_daily"}`

**未完成任务**（Incomplete Tasks）：
- 跟踪：通过 DurableTaskState 独立管理
- 不进入情景记忆系统，避免因激活度低被遗忘
- 定期检查：AttentionPolicy 评估是否需要恢复

**长期承诺**（Long-term Commitments）：
- 标记为 `importance=1.0`，recency_decay=0（不衰退）
- 示例："用户要求每天汇报进度"

**P3.4 回忆强化与可信度**

```cpp
void MemoryStore::strengthen_recall(const std::string& memory_id, float credibility) {
  auto& entry = get_entry(memory_id);
  
  // 只有高可信度回忆才增加 effective_recalls
  if (credibility > 0.7) {
    entry.base_activation.effective_recalls += 1;
  }
  
  // 低可信度回忆不强化，甚至标记为"disputed"
  if (credibility < 0.3) {
    entry.metadata["disputed"] = true;
  }
  
  entry.last_recalled = now();
}
```

**可信度来源**：
- 用户明确确认："是的，我说过"→ credibility=1.0
- 事件日志验证：检查 EventTimeline 是否有对应记录 → 0.8
- 模型推断：无直接证据 → 0.5
- 用户否认："我没说过"→ 0.0（标记 disputed）

**P3.5 记忆巩固任务**

- 触发：空闲时（AttentionPolicy focus = Idle）
- 调度：kairo::Executor `submit_delayed()` 周期任务（每 6 小时）
- 逻辑：
  ```cpp
  // 提取重复出现的情景模式
  auto patterns = extract_recurring_patterns(episodic_memories);
  
  // 转换为语义知识
  for (auto& pattern : patterns) {
    if (pattern.occurrences > 3 && pattern.confidence > 0.8) {
      create_semantic_memory(pattern.summary, pattern.evidence);
    }
  }
  
  // 归档低激活情景（保留摘要，原始可检索）
  archive_low_activation_episodic(threshold=0.2, older_than_days=30);
  ```

**P3.6 长期存储架构（确定 SQLite）**

**架构决策**（评审 P1）：
- **Phase 1 即确定** SQLite 作为 EventTimeline 和长期记忆后端
- 现有 MemoryEventStore JSON 作为兼容适配层保留
- 避免在 JSON 分片上叠加复杂索引

**Schema 设计**：
```sql
-- 事件时间线
CREATE TABLE events (
  id TEXT PRIMARY KEY,
  timestamp INTEGER NOT NULL,
  agent_identity_id TEXT NOT NULL,
  event_type TEXT NOT NULL,
  source TEXT,  -- session_id / task_id / system
  payload JSON,
  UNIQUE(timestamp, id)  -- 支持时间范围查询
);
CREATE INDEX idx_events_time ON events(timestamp);
CREATE INDEX idx_events_agent ON events(agent_identity_id);

-- 情景记忆
CREATE TABLE episodic_memories (
  id TEXT PRIMARY KEY,
  created_at INTEGER NOT NULL,
  last_recalled INTEGER,
  recall_count INTEGER DEFAULT 0,
  effective_recalls INTEGER DEFAULT 0,
  importance REAL NOT NULL,
  recency_decay REAL DEFAULT 0.1,
  content TEXT NOT NULL,
  embedding BLOB,  -- 预计算 embedding（可选）
  metadata JSON
);
CREATE INDEX idx_episodic_activation ON episodic_memories(importance, last_recalled);

-- 语义记忆
CREATE TABLE semantic_memories (
  id TEXT PRIMARY KEY,
  topic TEXT NOT NULL,
  knowledge TEXT NOT NULL,
  confidence REAL NOT NULL,
  evidence_count INTEGER DEFAULT 0,
  created_at INTEGER NOT NULL,
  metadata JSON
);
CREATE INDEX idx_semantic_topic ON semantic_memories(topic);

-- 记忆关联
CREATE TABLE memory_associations (
  from_id TEXT NOT NULL,
  to_id TEXT NOT NULL,
  association_type TEXT,  -- derived_from / related_to / contradicts
  strength REAL,
  PRIMARY KEY (from_id, to_id)
);
```

**迁移策略**：
- M11: 引入 SQLite schema，现有 JSON 继续工作
- M12: EventTimeline 写双份（JSON + SQLite）
- M13: 记忆系统读写 SQLite，JSON 只读兼容
- M14: 移除 JSON 写入，保留读取用于历史数据迁移

**验收标准（修订）**：
- [ ] **Token 预算控制**：长期运行后 context < 预算（8K tokens）
- [ ] **重要记忆恢复**：重启后 90% 重要记忆（importance >0.7）可检索
- [ ] **首次回忆正确**：新记忆首次回忆成功率 >95%
- [ ] **长期记忆存活**：30 天前的重要承诺仍在工作记忆（不受时间衰退影响）
- [ ] **检索准确率**：标注测试集（100 条查询）召回率 >80%，准确率 >70%
- [ ] **错误记忆防护**：用户否认的记忆标记 disputed，不再主动加载
- [ ] **巩固正确性**：情景→语义转换后，原始情景仍可通过关联检索
- [ ] **检索延迟**：< 100ms (P95)，大规模数据（10K+ 记忆）< 200ms (P95)

**依赖能力**：
- ✅ MemoryEventStore（扩展为 SQLite 后端）
- ✅ kairo::Executor timer（巩固调度）
- ⚠️ 语义相似度计算
  - **确认缺口**：需与 Mira 维护者确认
  - **fallback**：关键词 Jaccard 相似度（M13 可用）
  - **长期方案**：M14+ 评估本地 embedding 模型或 API

**预计工期**：8 周（原 6 周，增加 SQLite 迁移与测试）

---

### 4.5 Phase 4: UI 交互模式演进（M14 提案）

**目标**：从会话列表转向持续个体交互界面。

**P4.1 新增"与 Mira 对话"模式**
- 主界面不再强制选择会话
- 顶部固定输入框：用户直接与 Agent 对话
- 系统自动决定：创建新任务、切换焦点到已有任务、还是普通交流

**P4.2 时间线视图**
- 替代会话列表
- 按日期组织：`今天`、`昨天`、`本周早些时候`、`更早`
- 每个条目：时间、类型（任务/交流/工具调用）、摘要
- 点击展开完整上下文

**P4.3 焦点任务指示器**
- 界面顶部显示当前焦点：
  ```
  🔵 正在关注：整理 mirage 文档
  📊 后台进行中：编译 native-vision-debug (3个)
  ```

**P4.4 MiraPet 常驻交互**
- 点击 MiraPet 打开主窗口
- 右键菜单：暂停当前任务、切换焦点、查看后台任务

**P4.5 会话兼容层保留**
- 设置中提供"传统会话模式"切换
- API 层继续支持 `session.open`、`session.list`

**验收标准**：
- [ ] 用户无需创建会话即可与 Agent 交互
- [ ] 时间线视图可按时间/任务/类型筛选
- [ ] 切换到传统模式后，会话列表功能正常

**预计工期**：4 周

---

### 4.6 Phase 5: 自主行为能力（M15+ 长期）

**目标**：在持续性认知架构基础上，引入有限自主行为能力。

**架构修订要点**（评审 P1）：
- **持续性认知架构**与**自主行为系统**明确分层
- Phase 1-4 构建持续运行的 Agent（身份、注意力、记忆、交互）
- Phase 5 作为可独立启用的上层能力，不依赖 Mirage UI
- 自主行为受明确权限和资源约束

**分层关系**：
```
┌─────────────────────────────────────┐
│  自主行为系统（Phase 5，可选）        │
│  - Motivation（内在动机）            │
│  - Goal Selection（目标选择）        │
│  - Resource Budget（资源预算）       │
│  - Behavior Evaluation（行为评估）   │
└─────────────────────────────────────┘
                ↓ 基于
┌─────────────────────────────────────┐
│  持续性认知架构（Phase 1-4，基础）    │
│  - AgentIdentity（统一身份）         │
│  - AttentionCoordinator（注意力）    │
│  - MemoryStore（动态记忆）           │
│  - DurableTaskState（任务恢复）      │
└─────────────────────────────────────┘
```

**Phase 1-4 vs Phase 5**：
- **Phase 1-4**：解决"如何作为一个持续个体运行"
  - 统一身份：跨会话保持同一主体
  - 注意力管理：协调多任务，安全切换
  - 动态记忆：长期运行不丢失重要信息
  - 交互模式：无需显式创建会话
- **Phase 5**：解决"为什么选择某项活动"
  - 内在动机：Agent 有自己的"想做的事"
  - 目标选择：在多个可能目标中优先级排序
  - 资源预算：有限资源下如何分配
  - 行为评估：自主行为的价值与风险

**自主行为边界**：
- **允许**（只读，无副作用）：
  - 整理记忆、学习新知识、分析项目文档
  - 准备常用工具建议、检查定时任务状态
- **需确认**（有副作用，用户可见）：
  - 修改文件、发送消息、调用付费服务
  - 创建任务、安装依赖、执行系统命令
- **禁止**（高风险，无论用户是否在线）：
  - 删除数据、修改权限、访问凭据
  - 超出授权范围的系统操作

**P5.1 自主目标系统**

```cpp
// runtime/autonomy/autonomous_goal.hpp
enum class GoalSource {
  UserExplicit,      // 用户明确指示："每天提醒我"
  SelfGenerated,     // Agent 自主产生："我注意到文档过期了"
  Scheduled,         // 定时触发："每周整理记忆"
  EnvironmentTrigger // 环境事件："检测到新 PR"
};

enum class GoalPriority {
  Background,  // 空闲时执行，随时可中断
  Normal,      // 正常优先级，与用户任务竞争
  Urgent       // 紧急（如主动提醒），打断当前任务
};

struct AutonomousGoal {
  std::string goal_id;
  std::string description;
  GoalSource source;
  GoalPriority priority;
  
  // 资源需求
  std::vector<std::string> required_capabilities;
  ResourceBudget estimated_cost;
  
  // 执行约束
  std::chrono::system_clock::time_point earliest_start;
  std::chrono::system_clock::time_point deadline;
  
  // 价值评估
  float expected_value;  // 预期收益（0.0-1.0）
  float risk_level;      // 风险程度（0.0-1.0）
};

// runtime/autonomy/motivation_system.hpp
class MotivationSystem {
public:
  // 生成自主目标
  std::vector<AutonomousGoal> generate_goals(
    const AgentState& state,
    const std::vector<EnvironmentEvent>& recent_events
  );
  
  // 评估目标价值（为什么要做这件事）
  float evaluate_goal_value(
    const AutonomousGoal& goal,
    const AgentState& state
  );
  
  // 示例动机源
  // - 信息完整性：发现记忆缺口、文档过期
  // - 效率优化：准备常用工具、预测用户需求
  // - 社交维护：定期汇报、主动提醒承诺
  // - 自我完善：学习新知识、改进技能
};
```

**P5.2 资源预算管理**

```cpp
// runtime/autonomy/resource_budget.hpp
struct ResourceBudget {
  int max_api_calls_per_hour{10};
  int max_tasks_per_day{5};
  std::chrono::minutes max_task_duration{5};
  size_t max_file_reads{20};
  
  // 当前已使用
  int used_api_calls{0};
  int used_tasks_today{0};
};

class ResourceBudgetManager {
public:
  // 检查是否可执行
  bool can_afford(const AutonomousGoal& goal);
  
  // 扣除资源
  void consume(const ResourceBudget& cost);
  
  // 定期重置
  void reset_hourly();
  void reset_daily();
  
  // 优先级豁免（紧急任务可超预算，需记录）
  bool request_override(const AutonomousGoal& goal, const std::string& rationale);
};
```

**默认预算限制**：
- **API 调用**：每小时 < 10 次（避免成本失控）
- **自主任务数**：每天 < 5 个（避免过度打扰）
- **单任务时长**：< 5 分钟（避免长期占用资源）
- **文件读取**：单任务 < 20 个文件（避免隐私风险）

**P5.3 自主活动示例**

**1. 空闲记忆整理**（Background）
- 触发：用户离线 >30 分钟，AttentionManager focus = Idle
- 动作：执行 Phase 3 的记忆巩固（情景→语义）
- 成本：0 API 调用（本地计算）
- 通知：下次用户上线时："我整理了 23 条记忆"

**2. 主动提醒**（Normal）
- 触发：检测到用户承诺 + 接近 deadline
- 动作：产生 ExternalEvent 进入 AttentionPolicy
- 成本：0 API 调用（查询本地记忆）
- 示例："您今天说要 review PR #42，现在还有时间吗？"

**3. 后台学习**（Background）
- 触发：检测到项目新增文档
- 动作：读取文档，提取关键概念，更新语义记忆
- 成本：1-2 API 调用（文档摘要）
- 通知："我读了新的 API 文档，下次可以帮你用"

**4. 预测性准备**（Background）
- 触发：用户历史模式（每天 9 点 review 代码）
- 动作：提前检查 CI 状态、准备相关文件列表
- 成本：2-3 API 调用（GitHub API）
- 通知：静默准备，用户发起任务时直接使用

**P5.4 用户控制机制**

**全局开关**：
- 设置项：`autonomy.enabled`（默认 false，需明确开启）
- 分级控制：
  - `autonomy.allow_background_only`：只允许空闲整理
  - `autonomy.allow_reminders`：允许主动提醒
  - `autonomy.allow_learning`：允许后台学习
  - `autonomy.allow_predictive`：允许预测性准备

**实时监控**：
- UI 显示："🤖 自主活动：整理记忆中..."
- 日志记录：所有自主决策记录到 EventTimeline
- 预算仪表盘：显示已用/剩余资源

**随时中断**：
- 用户发起新任务 → 自主任务立即让出焦点
- 显式中止："停止自主活动"→ 所有 Background 任务取消
- 紧急接管 → 自主任务进入 Paused，资源释放

**P5.5 行为评估与学习**

```cpp
// runtime/autonomy/behavior_evaluator.hpp
class BehaviorEvaluator {
public:
  // 记录自主行为结果
  void record_outcome(
    const AutonomousGoal& goal,
    const TaskResult& result,
    const UserFeedback& feedback
  );
  
  // 评估行为价值
  struct Evaluation {
    float actual_value;    // 实际产生的价值
    float cost;            // 实际消耗的资源
    float user_satisfaction; // 用户反馈（-1.0 到 1.0）
  };
  
  Evaluation evaluate(const std::string& goal_id);
  
  // 调整未来行为
  void update_motivation_weights(const Evaluation& eval);
};
```

**学习机制**：
- 用户明确称赞："很好，继续这样"→ 增加该类目标权重
- 用户忽略通知 → 降低主动提醒频率
- 用户中止任务 → 标记该模式为低价值
- 用户反馈"不要再..."→ 永久禁用该类行为

**P5.6 与持续性认知架构集成**

自主目标进入系统的路径：
```
MotivationSystem.generate_goals()
    ↓
AutonomousGoal → ExternalEvent
    ↓
AttentionPolicy.evaluate_focus()
    ↓
（与用户任务竞争，按优先级决定是否切换焦点）
    ↓
AttentionCoordinator.request_switch()
    ↓
执行（通过 Phase 2 的标准任务执行流程）
```

自主行为不绕过注意力管理：
- Background 任务只在 focus = Idle 时执行
- Normal 任务进入 AttentionPolicy 评估队列
- Urgent 任务可打断当前焦点（需用户授权）

**验收标准**：
- [ ] **权限边界**：自主行为严格遵守允许/需确认/禁止分类
- [ ] **资源预算**：每小时 API 调用 < 10，每天任务 < 5
- [ ] **用户控制**：全局开关、分级控制、随时中断全部有效
- [ ] **透明度**：所有自主决策记录到 EventTimeline，可审计
- [ ] **非侵入性**：用户任务优先级始终 > 自主任务
- [ ] **学习有效性**：3 次负反馈后，该模式自动禁用
- [ ] **崩溃安全**：进程重启后，未完成自主任务不自动恢复（需重新评估）

**依赖能力**：
- ✅ Phase 1-4 持续性认知架构
- ✅ AttentionPolicy（目标竞争）
- ✅ ResourceBudgetManager（资源控制）
- ✅ EventTimeline（行为审计）

**预计工期**：10 周（原 8 周，增加学习机制与安全测试）
---

### 4.7 Phase 6: 多模态感知与表达（M11+ 长期）

**目标**：扩展持续性数字个体的感知与表达通道。

**扩展方向**：
1. **语音交互**：用户通过麦克风与 MiraPet 对话，Agent 通过语音回复
2. **环境感知**：监控系统状态（CPU/内存/网络/电量）作为上下文
3. **主动表达**：MiraPet 通过动画、声音、气泡主动表达情绪与状态
4. **跨设备连续性**：手机/桌面/Web 统一身份，记忆实时同步

**说明**：Phase 6 是长期愿景，不阻塞 Phase 1-5 交付。多模态能力依赖硬件支持（麦克风、扬声器）和平台能力（Android TTS、语音识别），需单独评估可行性。

---

## 4.8 并行开发策略与依赖关系（修订）

**评审要点**（评审 P1）：原路线图 P1→P2→P3→P4→P5 线性依赖过于严格。修订为：以 AgentIdentity 和 EventTimeline 为公共基础，核心模块并行开发。

**公共基础层**（Phase 1，M11）：
- AgentIdentity 模型
- EventTimeline（SQLite）
- 基础持久化框架

**并行开发层**（M12-M13，可并行）：
- **注意力与切换**（AttentionCoordinator + DurableTaskState）
- **动态记忆**（MemoryStore + 激活模型）
- **UI 原型**（时间线视图，基于兼容接口）

**集成层**（M14）：
- 整合注意力、记忆、UI
- Cognitive Runtime 统一接口
- MiraPet 单向投影

**扩展层**（M15+）：
- 自主行为系统（可选）
- 多模态（长期）

**依赖关系**：
```
Phase 1 (AgentIdentity + EventTimeline)
   ├─→ Phase 2 (AttentionCoordinator)  ─┐
   ├─→ Phase 3 (MemoryStore)            ├─→ Phase 4 (UI集成)
   └─→ Phase 4 UI原型（兼容接口）       ─┘
                                         ↓
                                      Phase 5 (自主行为，可选)
                                         ↓
                                      Phase 6 (多模态，长期)
```

**关键解耦点**：
- Phase 2 和 Phase 3 不强依赖：记忆系统可基于现有 TaskState 开发
- Phase 4 UI 可提前准备：通过兼容接口（session.history）先实现时间线视图
- Phase 5 独立于 UI：自主行为系统不依赖 Mirage 存在，可在 Android/服务端复用

---

## 4.9 核心架构不变量与状态一致性（新增）

**评审要求**（评审 P2）：M11 开发前必须明确系统约束。

### 不变量定义

**INV-1：单一焦点**
- 任意时刻最多一个已提交焦点（`committed_focus`）
- 焦点切换过程中，旧焦点先释放，新焦点后提交
- 验证：`committed_focus` 只有一个写入点（AttentionCoordinator::commit）

**INV-2：租约一致性**
- 桌面输入租约持有者 ∈ {已提交焦点任务, null}
- 焦点切换时，租约在 Release 阶段释放，在 Resume 阶段重新申请
- 验证：日志中 lease_granted 事件的 task_id 等于当前焦点，或租约为空

**INV-3：任务状态原子性**
- 任务状态转换（Active/Paused/Completed/Failed）是原子操作
- 焦点切换不能破坏正在执行的原子操作（如工具调用、文件写入）
- 验证：DurableTaskState 保存的检查点必须处于一致状态

**INV-4：记忆持久性**
- 标记为 `importance >0.7` 的记忆不因时间衰退而丢失
- 未完成任务（DurableTaskState）不进入情景记忆遗忘流程
- 身份记忆（Identity Memory）始终加载到工作记忆
- 验证：重启后，高重要性记忆和未完成任务 100% 可恢复

**INV-5：MiraPet 忠实投影**
- MiraPet 显示状态必须追溯到真实运行状态
- MiraPet 不反向影响认知决策（单向投影）
- 验证：MiraPetProjection 只读取状态，never 写入焦点或任务状态

**INV-6：事件时间线完整性**
- 所有外部交互（用户消息、任务创建、工具调用）记录到 EventTimeline
- 事件标识唯一、时间戳单调递增、不可篡改
- 验证：EventTimeline 写入通过事务保证原子性

**INV-7：资源边界**
- 自主行为受资源预算约束（API 调用、任务数、时长）
- 超预算拒绝，豁免需记录理由
- 验证：ResourceBudgetManager 拒绝超预算请求，日志可审计

### 故障恢复策略

**场景1：进程崩溃**
- 恢复：从 DurableTaskState 最近检查点恢复未完成任务
- 策略：用户确认是否继续（不自动恢复，避免错误重复）
- 不变量验证：INV-3、INV-4

**场景2：网络断连**
- 恢复：标记为 `waiting_external`，AttentionPolicy 切换到其他任务
- 策略：网络恢复后，作为 ExternalEvent 通知，重新竞争焦点
- 不变量验证：INV-1、INV-2

**场景3：模型调用失败**
- 恢复：重试 3 次，失败后标记任务为 Failed
- 策略：不静默吞掉失败，通知用户并记录到 EventTimeline
- 不变量验证：INV-6

**场景4：资源租约超时**
- 恢复：强制释放租约，任务进入 Failed 状态
- 策略：记录警告日志，用户可查看失败原因
- 不变量验证：INV-2

**场景5：任务取消**
- 恢复：执行 AttentionCoordinator::rollback_to 恢复原焦点
- 策略：持久化的检查点保留，用户可选择稍后恢复
- 不变量验证：INV-1、INV-3

**场景6：记忆损坏**
- 恢复：从 SQLite WAL（Write-Ahead Log）恢复
- 策略：无法恢复的记忆标记为 `corrupted`，不加载到工作记忆
- 不变量验证：INV-4

### 状态观察与调试

**运行时监控**：
- `/runtime/status`：当前焦点、活动任务、租约持有者、资源预算
- `/runtime/invariants`：实时验证 INV-1 到 INV-7，违反时报警
- `/runtime/events`：EventTimeline 最近 100 条事件

**测试验证**：
- 单元测试：每个不变量对应一个测试套件
- 集成测试：焦点切换、崩溃恢复、并发任务场景
- 混沌测试：随机注入故障（网络断连、进程杀死、资源耗尽）

---

## 5. 技术实现要点

### 5.1 并发模型遵守（AGENTS.md 强制）

**绝对禁止**：
- ❌ 使用 `std::thread`、`std::async`
- ❌ 自建线程池或调度器
- ❌ detached worker 或 fire-and-forget
- ❌ 私有定时调度器
- ❌ 共享可变状态 + mutex + 条件变量

**必须使用**：
- ✅ `kairo::Executor` 的 `submit_auto()`、`submit_cancellable()`
- ✅ `kairo::comm::MpscChannel`（FIFO）
- ✅ `kairo::comm::LatestMailbox`（最新状态）
- ✅ `kairo::comm::Topic`（多订阅广播）
- ✅ `submit_delayed()`、`submit_periodic()`（定时任务）

**AttentionManager 实现示例**：
```cpp
class AttentionManager {
  kairo::Executor& executor_;  // 引用唯一 owner，不持有线程
  kairo::comm::LatestMailbox<Focus> current_focus_;  // 焦点状态广播
  kairo::comm::MpscChannel<FocusRequest> requests_;  // 切换请求队列
  std::optional<kairo::TaskHandle> processing_handle_;
  
public:
  void start() {
    processing_handle_ = executor_.submit_cancellable([this](kairo::StopToken token) {
      while (!token.stop_requested()) {
        auto req = requests_.receive(token);
        if (req) process_request(*req);
      }
    });
  }
  
  void shutdown() {
    if (processing_handle_) {
      processing_handle_->request_cancel();
      processing_handle_->wait();
    }
  }
};
```

### 5.2 依赖能力复用优先

**优先级顺序**：
1. **pinned mira**：Agent runtime、conversation loop、memory
2. **pinned mirador**：视觉感知、OCR、检测
3. **kairo（via mira）**：并发、通信、生命周期
4. **Mirage 自研**：仅在上述依赖明确无对应能力时

**能力路由检查清单**（每个新功能前必查）：
```
□ 核对 third_party/mira/docs/api/
□ 核对 third_party/mirador/docs/
□ 核对 third_party/mira/third_party/kairo/docs/API.md
□ 确认依赖确无对应能力
□ 若有缺口 → 登记 docs/dependency_feedback/ledger.md
```

**示例：Phase 3 语义相似度需求**
- 需求：计算两段文本的语义相似度用于记忆检索
- 检查：pinned mira 未提供公开 embedding 或相似度接口
- 行动：登记 `MIRA-20261015-001` 到反馈台账
- 临时方案：基于关键词重叠的简单相似度 `jaccard(tokenize(t1), tokenize(t2))`
- 影响：Phase 3 记忆检索质量受限，长期需评估独立 embedding 服务

### 5.3 记忆存储策略

**三层存储架构**：

| 层次 | 技术选型 | 生命周期 | 用途 |
|-----|---------|---------|------|
| 短期（会话内） | 内存 `std::deque` | 进程退出丢失 | 快速访问工作记忆 |
| 中期（任务级） | `MemoryEventStore` JSON | 重启后恢复 | 任务历史与会话记录 |
| 长期（Agent 级） | `agent_memory.db` (SQLite或分片JSON) | 持久化 | 情景/语义记忆，支持时间/主题/重要性索引 |

**长期存储权衡**：
- SQLite 优势：成熟索引、事务支持、查询灵活
- SQLite 劣势：引入新依赖、跨平台构建复杂度
- 替代方案：扩展现有 JSON 持久化 + 分片文件（按月/按主题）
- **决策**：M13 阶段先用 JSON，性能瓶颈时再评估 SQLite

**记忆压缩策略**：
- 超过 N 天（如 30 天）且 `activation < threshold`（如 0.1）的情景记忆：
  - 保留摘要，归档原始细节
  - 完整记录仍可检索，但不主动加载到工作记忆
- 巩固：将重复出现的情景模式提取为语义知识

### 5.4 MiraPet 状态驱动机制（修订）

**评审修订**（评审 P1）：取消 MiraPet 反向影响认知决策，改为单向忠实投影。

**修订前模式**（已废弃）：
```
AttentionManager 决定焦点时，考虑当前 MiraPet 展示的任务
→ 问题：表现层反向控制认知层，形成循环依赖
```

**修订后模式**（Phase 2 实施）：
```
                AttentionPolicy（决策层）
                  ↓ 评估焦点
        AttentionCoordinator（切换协调）
                  ↓ commit
            committed_focus（单一真相源）
                  ↓ 订阅
          MiraPetProjection（单向投影）
                  ↓ 生成显示状态
              IPC → mira_pet 播放器
```

**单向原则**：
1. **MiraPetProjection 只读状态**：
   - 订阅 `committed_focus`（LatestMailbox）
   - 订阅 `task_state_updates`（Topic）
   - 订阅 `action_coordinator_status`（租约信息）
   - **Never 写入焦点、任务状态、租约**

2. **注意力稳定性在认知层实现**：
   - AttentionPolicy 配置 `min_focus_duration`（如 30 秒）
   - AttentionPolicy 合并同类通知（5 秒窗口）
   - 用户可启用"勿扰模式"锁定焦点
   - **不读取 MiraPet 当前播放状态**

3. **用户交互作为外部事件**：
   - 用户点击 MiraPet → 产生 `UserInteractionEvent`
   - 用户右键菜单"切换焦点" → 产生 `FocusRequest`
   - 这些事件进入 AttentionPolicy 评估，与其他事件同等对待
   - MiraPet 本身不直接修改焦点

**防抖与过渡**：
- MiraPet 动画层可实现视觉过渡（淡入淡出）
- 快速切换时（< 2 秒），MiraPet 可延迟显示新状态（视觉防抖）
- 但这是**表现层优化**，不影响认知层决策

**验证**：
- 代码审查：MiraPetProjection 类只有 const 方法访问状态
- 集成测试：焦点切换时，MiraPet 状态在 500ms 内同步
- 不变量测试：INV-5（MiraPet 忠实投影）始终成立

---

## 6. 风险与缓解

### 6.1 技术风险

**R1: 记忆检索性能**
- **风险**：全文语义检索可能耗时过长，阻塞交互
- **缓解**：
  - 分层检索：先按时间/任务筛选（O(log n)），再语义匹配（O(k)）
  - 离线索引：空闲时预计算 embedding，存储到索引
  - Token 预算硬限制：检索超时（如 100ms）返回部分结果
  - 异步预取：用户输入时后台准备记忆

**R2: 注意力切换抖动**
- **风险**：频繁切换焦点导致任务都无法完成
- **缓解**：
  - 最小焦点持续时间（30 秒）
  - 同类通知合并（5 秒窗口）
  - 用户可手动锁定焦点（"勿扰模式"）
  - 安全状态例外：接管/断连立即打断

**R3: 依赖能力缺口**
- **风险**：pinned mira 缺少语义相似度计算，影响 Phase 3
- **缓解**：
  - Phase 3 前（M13）完成能力核对
  - 准备 fallback：基于关键词的简单相似度
  - 按 AGENTS.md 流程登记反馈台账
  - 与 Mira 维护者确认计划路线图

### 6.2 产品风险

**R4: 用户不适应新交互模式**
- **风险**：移除会话列表后，用户找不到历史任务
- **缓解**：
  - 保留"传统模式"切换（至少 2 个版本）
  - 渐进式引入：早期版本两种模式并存
  - 充分的用户教育：教程、Tooltip、视频演示
  - 内部用户测试：每个 Phase 至少 5 名试用

**R5: MiraPet 行为预期不符**
- **风险**：用户期望 MiraPet 立即响应，但注意力管理器决定延迟
- **缓解**：
  - MiraPet 明确显示"排队中"状态
  - 紧急任务提供"立即关注"按钮
  - 用户可配置优先级策略（设置中）
  - 首次使用时说明注意力模型

### 6.3 工程风险

**R6: 多阶段协同复杂度**
- **风险**：6 个 Phase 跨越多个里程碑，协调困难
- **缓解**：
  - 每个 Phase 独立可验证（有明确验收标准）
  - 明确接口边界（AttentionManager API、MemoryStore API）
  - 早期 Phase 为后续留扩展点
  - 定期架构评审（每 Phase 结束后）

**R7: 性能回归**
- **风险**：动态记忆检索增加延迟，影响用户体验
- **缓解**：
  - Phase 3 前建立性能基线（context 构建时间、内存占用）
  - 每个 Phase 设置延迟预算（<100ms）
  - 持续监控：记录 P50/P95/P99 延迟
  - 异步预取：输入时后台准备记忆
  - 降级策略：超时返回部分结果，不阻塞

---

## 7. 里程碑与时间线（修订）

### 7.1 建议排期

| Phase | 里程碑 | 预计工期 | 关键依赖 | 累计时间 |
|-------|-------|---------|---------|---------|
| P1: 身份+时间线+SQLite | M11 | 4 周 | MemoryEventStore→SQLite 迁移 | 4 周 |
| P2: 注意力+切换协议 | M12 | 6 周 | ActionCoordinator、DurableTaskState | 10 周 |
| P3: 动态记忆+激活模型 | M13 | 8 周 | 语义相似度确认、SQLite 索引 | 18 周 |
| P4: UI 演进 | M14 | 4 周 | 前端重构 | 22 周 |
| P5: 自主行为 | M15+ | 10 周 | Phase 1-4 完成、权限框架 | 32 周 |
| P6: 多模态 | M11+ | 长期 | 硬件/平台能力 | - |

**工期调整说明**（相比 v1.0）：
- P1: 3周 → 4周（增加 SQLite 初始迁移）
- P2: 4周 → 6周（增加安全切换协议与恢复测试）
- P3: 6周 → 8周（增加 SQLite 完整迁移与记忆模型验证）
- P5: 8周 → 10周（增加学习机制与安全测试）
- 总计：M11-M14 核心能力 **22 周**（原 17 周）

**关键里程碑成果**：
- **M11 结束**：
  - ✅ AgentIdentity 跨会话统一
  - ✅ EventTimeline（SQLite）可按时间/类型查询
  - ✅ MiraPet 展示状态关联到身份
  - ✅ 核心不变量（INV-1 到 INV-7）定义完成

- **M12 结束**：
  - ✅ AttentionCoordinator 安全切换工作
  - ✅ 多任务时只有一个焦点，可追溯
  - ✅ 租约一致性（INV-2）验证通过
  - ✅ DurableTaskState 恢复正确率 >99%

- **M13 结束**：
  - ✅ 工作记忆动态构建，Token 预算可控
  - ✅ 重要记忆 90% 重启后可恢复
  - ✅ 情景→语义巩固机制工作
  - ✅ SQLite 迁移完成，JSON 只读兼容

- **M14 结束**：
  - ✅ 用户可无需创建会话与 Agent 交互
  - ✅ 时间线视图替代会话列表
  - ✅ MiraPet 忠实投影（INV-5）验证通过
  - ✅ 传统模式兼容层保留

### 7.2 并行与依赖（修订）

**可并行工作**：
```
Phase 1 (AgentIdentity + EventTimeline + SQLite)
   ├─→ Phase 2 (AttentionCoordinator + DurableTaskState)  ───┐
   ├─→ Phase 3 (MemoryStore + 激活模型)                    │
   └─→ Phase 4 UI原型（基于兼容接口开始前端工作）          │
                                                            ├─→ Phase 4 集成
                                                            │
                                                            └─→ Phase 5 (自主行为)
```

**关键路径**（决定总工期）：
```
P1 (4周) → P2 (6周) → P3 (8周) → P4 集成 (4周) = 22周
```

**并行收益**：
- P2 和 P3 可部分并行开发（前 2 周接口设计，后 4/6 周各自实现）
- P4 UI 原型可在 P3 完成前 2 周开始（基于 session.history 兼容接口）
- 实际日历时间可缩短至 **~18-20 周**

### 7.3 M11 前必须完成的工作（评审 P0/P1）

**架构设计**（2 周内）：
1. [ ] **核心不变量文档**：INV-1 到 INV-7 详细定义
2. [ ] **AttentionCoordinator 切换协议**：Prepare/Suspend/Release/Commit/Resume 详细设计
3. [ ] **MemoryStore 激活模型**：基础激活度与查询相关性分离
4. [ ] **SQLite Schema 确定**：events/episodic_memories/semantic_memories 表结构
5. [ ] **依赖能力确认**：与 Mira 维护者确认语义相似度计划

**技术验证**（1 周内）：
1. [ ] **SQLite 性能基线**：10K 事件查询延迟 < 10ms
2. [ ] **切换协议原型**：验证 Prepare→Resume 流程可行性
3. [ ] **记忆激活公式**：标注测试集验证召回率

**团队对齐**（1 周内）：
1. [ ] **架构评审会**：Mirage 维护者 + 至少 2 名 Reviewer
2. [ ] **风险评估会**：识别遗漏风险，补充缓解措施
3. [ ] **资源确认**：人力分配、排期冲突解决

### 7.4 验收门禁（每个 Phase）

每个 Phase 必须通过：

1. **单元测试**：核心逻辑覆盖率 >80%
2. **集成测试**：与 Runtime/Desktop/Platform 各层协同
3. **性能测试**：延迟/内存/CPU 不超预算
4. **不变量验证**：INV-1 到 INV-7 自动化测试通过
5. **用户测试**：至少 5 名内部用户试用，收集反馈
6. **文档更新**：设计文档、API 文档、用户指南同步
7. **代码评审**：至少 2 名 Maintainer 批准
8. **兼容性验证**：传统模式功能不受影响
---

## 8. 成功标准（修订）

**评审修订**（评审 P1）：增加行为正确性验证，性能指标建立在正确性基础上。

### 8.1 正确性指标（优先级最高）

| 指标 | 目标值 | 测量方法 | 不变量关联 |
|-----|-------|---------|-----------|
| **任务恢复正确率** | 100% | 崩溃后从检查点恢复，验证状态完整性 | INV-3, INV-4 |
| **焦点与租约一致性** | 100% | 日志审计：租约持有者 = 焦点任务 | INV-1, INV-2 |
| **重要记忆恢复率** | 100% (importance >0.7) | 重启后检索 100 条标注记忆 | INV-4 |
| **首次回忆成功率** | >95% | 新记忆首次检索命中率 | 记忆模型正确性 |
| **跨任务上下文污染率** | 0% | 任务 A 的私有数据不出现在任务 B 上下文 | 状态隔离 |
| **事件时间线完整性** | 100% | 所有外部交互记录，无丢失 | INV-6 |
| **资源预算遵守率** | 100% | 自主行为不超预算（除豁免） | INV-7 |

### 8.2 性能指标（在正确性满足前提下）

| 指标 | 目标值 | 测量方法 | 降级策略 |
|-----|-------|---------|---------|
| 焦点切换延迟 | < 500ms (P95) | Prepare→Resume 全流程时间 | 不宜中断任务可 Defer |
| 记忆检索延迟 | < 100ms (P95) | query → 返回结果时间 | 超时返回部分结果 |
| Token 预算控制 | < 8K tokens | 工作记忆大小 | 降低激活阈值 |
| 后台任务隔离 | 焦点任务响应时间不增加 >10% | 对比有/无后台任务 | 后台任务降优先级 |
| 长期运行稳定性 | 连续 7 天无崩溃 | 自动化压力测试 | - |
| 记忆巩固延迟 | 空闲时 < 30 秒启动 | 监控 submit_delayed 调度 | 用户活跃时跳过 |
| SQLite 查询延迟 | < 10ms (10K 事件) | 时间线查询延迟 | 增加索引 |

### 8.3 用户体验指标

| 指标 | 目标值 | 测量方法 |
|-----|-------|---------|
| 无缝交互 | 80% 用户无需手动创建会话 | 观察 20 名用户，统计会话创建次数 |
| 状态理解 | 70% 用户能通过 MiraPet 判断当前工作 | 问卷："看着 MiraPet，你知道它在做什么吗？" |
| 记忆感知 | 60% 用户感觉"Agent 记得我们的对话" | 问卷：5 分量表，≥4 分视为达标 |
| 注意力合理性 | < 10% 用户报告焦点切换不合理 | 收集使用日志，分析投诉率 |
| 任务完成率 | 与当前持平或提升 | 对比新旧模式下相同任务的完成率 |
| 满意度 | NPS ≥ 40 | Net Promoter Score 调查 |

### 8.4 业务指标

| 指标 | 目标值 | 意义 |
|-----|-------|-----|
| 日活跃用户 | 增长 20% | 更自然的交互吸引更多使用 |
| 会话时长 | 增长 30% | 用户愿意与持续个体更长时间交互 |
| 任务重复率 | 降低 15% | 记忆能力减少重复工作 |
| 用户留存（30天） | 提升 10% | 持续身份增强粘性 |

### 8.5 特殊场景验证（评审 P1 要求）

**焦点切换场景**：
- [ ] **正常完成切换**：任务 A 完成 → 任务 B 获得焦点
- [ ] **不可中断拒绝**：工具调用中 → 新请求 Defer
- [ ] **紧急请求打断**：用户显式请求 → 当前任务 Suspend
- [ ] **后台任务完成通知**：后台编译完成 → 进入焦点队列
- [ ] **连续插入请求**：5 秒内 3 个低优先级 → 合并为 1 个
- [ ] **异常取消回滚**：切换过程中崩溃 → 恢复原焦点

**记忆检索场景**：
- [ ] **首次回忆**：新记忆（recall_count=0）可检索
- [ ] **长期记忆**：30 天前的重要承诺仍可检索
- [ ] **语义检索**：同义词查询命中（"修复" = "fix"）
- [ ] **时间定位**："上周讨论的 API"定位到正确时间
- [ ] **证据否认**：用户纠正错误记忆 → 标记 disputed
- [ ] **巩固正确性**：情景→语义后，原始情景可追溯

**任务恢复场景**：
- [ ] **正常恢复**：进程重启 → 未完成任务状态完整
- [ ] **检查点回滚**：工具调用失败 → 回退到上一检查点
- [ ] **超时保护**：租约超时 → 任务标记 Failed，不占用资源
- [ ] **依赖丢失**：外部服务不可用 → 标记 waiting_external
- [ ] **用户取消**：显式停止 → 检查点保留，可选稍后恢复

---

## 9. 参考文献与相关工作

### 9.1 学术研究

1. **Continuum Memory Architecture for Long-Horizon LLM Agents**  
   [arxiv 2601.09913](https://arxiv.org/pdf/2601.09913v1)  
   提出持久存储、选择性保留、关联路由、时间链与抽象巩固的记忆架构。

2. **AI Agents Need Memory Control Over More Context**  
   [arxiv 2601.11653](https://arxiv.org/pdf/2601.11653v1)  
   Agent Cognitive Compressor (ACC)：用有界内部状态替代完整转录本重放。

3. **Human-Inspired Memory Architecture for LLM Agents**  
   [arxiv 2605.08538](https://arxiv.org/pdf/2605.08538v1)  
   睡眠期巩固、干扰遗忘、检索时重巩固、实体知识图谱等六大认知机制。

4. **Autonomous Event-Driven Multi-Agent Orchestration for Enterprise AI at Scale**  
   [arxiv 2606.20058](https://arxiv.org/abs/2606.20058)  
   事件驱动架构使 Agent 响应延迟降低 90%，持续感知事件流。

5. **Deco: Extending Personal Physical Objects into Pervasive AI Companion through a Dual-Embodiment Framework**  
   [arxiv 2605.03882](https://arxiv.org/pdf/2605.03882v1)  
   混合环境存在与校准代理，忠实身份与互惠记忆。

6. **Understanding Spatially Situated Virtual Embodiment in Close Human–AI Relationships**  
   [arxiv 2606.28714](https://arxiv.org/html/2606.28714v1)  
   空间化虚拟具身作为关系升级形式，增强在场感与情境敏感性。

### 9.2 工业实践

7. **Event-Driven Architecture for AI Agents**  
   [atlan.com](https://atlan.com/know/event-driven-architecture-for-ai-agents/)  
   统一架构模式消除进程间通信延迟。

8. **Agentic Event-Driven Systems Architecture**  
   [confluent.io](https://www.confluent.io/blog/autonomous-agentic-event-driven-systems-architecture/)  
   自主 Agent 系统持续感知-推理-行动-学习循环。

9. **Giving an AI a Body: Building a Desktop Companion Avatar for macOS**  
   [dev.to](https://dev.to/xadenai/giving-an-ai-a-body-building-a-desktop-companion-avatar-for-macos-5el9)  
   使用运动响应、空间移动传达状态，动画角色增加用户容忍度。

### 9.3 Mirage 内部文档

10. [Mirage 桌面端设计方案](Mirage：Linux%20-%20Windows%20桌面端设计方案.md)
11. [mira_pet 设计](mira-pet.md)
12. [mira_pet 思考与对话气泡设计](mira-pet-bubbles.md)
13. [DEC-084: mira_pet 进程与动画](../decisions/DEC-084-mira-pet-process-and-animation.md)
14. [DEC-092: native 产品 pet 交付](../decisions/DEC-092-native-product-pet-delivery.md)
15. [AGENTS.md：项目协作约定](../../AGENTS.md)
16. [项目管理与工程规范](../project/project-standards.md)

---

## 10. 附录

### A. 术语表

- **Agent Identity**: 持续数字个体的唯一身份标识，跨会话/任务/设备保持稳定
- **Attention Manager**: 决定 Agent 当前主要关注对象的组件，协调多任务焦点切换
- **Working Memory**: 动态构建的当前任务上下文，非静态会话历史堆积
- **Episodic Memory**: 具体事件与交互的情景记忆，按时间组织
- **Semantic Memory**: 抽象知识、技能、规律的语义记忆，按主题组织
- **Activation Level**: 记忆的动态激活度（0.0-1.0），决定是否进入工作记忆
- **Focus**: Agent 当前主要注意力所在的任务或活动
- **Memory Consolidation**: 记忆巩固，将情景记忆抽象为语义知识的过程
- **Interruptible**: 可中断性，表示当前任务是否可被新请求打断
- **Event Timeline**: 事件时间线，统一组织用户消息、任务、工具调用、系统事件的时序记录

### B. 依赖反馈台账示例

**MIRA-20261015-001**（假设场景，用于说明格式）：

```yaml
id: MIRA-20261015-001
component: mira
capability: 语义相似度计算
status: 待评估
filed_date: 2026-10-15
filed_by: Mirage 维护者

description: |
  Phase 3 动态记忆需要计算两段文本的语义相似度，用于检索相关记忆。
  当前 pinned mira 未提供公开的 embedding 或相似度计算接口。

current_capability: |
  - mira/json.hpp 提供 JSON 编解码
  - mira 内部可能使用模型 API，但未暴露为可复用能力

requested_capability: |
  一个可复用的语义相似度接口，例如：
  ```cpp
  float compute_semantic_similarity(const std::string& text1, const std::string& text2);
  ```
  或暴露 embedding 计算：
  ```cpp
  std::vector<float> compute_embedding(const std::string& text);
  ```

workaround: |
  暂时使用基于关键词重叠的简单相似度：
  ```cpp
  float similarity = jaccard(tokenize(text1), tokenize(text2));
  ```
  限制：无法捕捉语义，但可先验证架构。

impact: |
  - Phase 3 记忆检索质量受限，可能误判相关性
  - 无法按语义聚类记忆，只能按时间/关键词
  - 长期需依赖模型 API 进行 embedding，增加延迟与成本

timeline: |
  - M13 前（2026-11-30）需明确：pinned mira 是否计划提供？
  - 若否，M13 使用 workaround 先实现架构验证
  - M14+ 评估独立 embedding 服务（需授权）或本地轻量模型

verification: |
  当提供语义相似度能力后：
  1. 验证相似文本得分 > 0.7
  2. 验证不相关文本得分 < 0.3
  3. 性能：< 50ms (P95)，支持批量计算
```

**台账维护规则**：
- 每个缺口唯一编号：`MIRA-YYYYMMDD-NNN` 或 `MIRADOR-YYYYMMDD-NNN`
- kairo 层缺口以 `MIRA-*` 登记（executor 经 mira 交付）
- 必须附可复现证据、影响范围、建议能力、临时方案
- 定期评审（每个里程碑）：已解决、仍开放、已弃用

### C. 下一步行动

**立即行动**（本周内）：

1. [ ] **与 Mira 维护者确认**：
   - pinned mira 是否计划提供语义相似度能力？
   - 时间表如何？是否会在 M13 前交付？
   
2. [ ] **评估 Phase 1 工作量**：
   - 详细拆分 P1.1-P1.4 子任务
   - 评估是否可纳入 M11（3 周工期）
   - 明确人力资源分配

3. [ ] **创建依赖反馈台账**：
   - 建立 `docs/dependency_feedback/ledger.md`
   - 如果确认语义相似度缺口，登记 `MIRA-20261015-001`

4. [ ] **建立性能基线**：
   - 记录当前 context 构建延迟（P50/P95/P99）
   - 记录当前内存占用（Runtime/Executor）
   - 建立监控脚本，后续对比

**短期行动**（2 周内）：

1. [ ] **完成 Phase 1 详细设计**：
   - AgentIdentity 数据模型
   - EventTimeline API 定义
   - 与现有 Session 的兼容层设计
   - 持久化 schema 设计

2. [ ] **原型验证 AttentionManager**：
   - 实现最小可用版本（MVP）
   - 验证焦点决策策略
   - 测试与 ActionCoordinator 集成

3. [ ] **用户研究准备**：
   - 设计问卷（记忆感知、状态理解、注意力合理性）
   - 招募 5 名内部用户作为早期测试者
   - 准备演示视频

4. [ ] **技术栈评估**：
   - 评估 SQLite 作为长期记忆存储的可行性
   - 评估语义相似度 fallback 方案的质量
   - 评估前端时间线视图的技术选型

**中期目标**（M11-M13，17 周内）：

1. [ ] **Phase 1-3 实现完成**：
   - 统一身份与时间线（M11）
   - 注意力管理器（M12）
   - 动态工作记忆（M13）

2. [ ] **持续收集反馈**：
   - 每个 Phase 结束后进行内部用户测试
   - 记录问题、建议、困惑点
   - 调整后续 Phase 设计

3. [ ] **文档与培训**：
   - 编写用户指南：如何使用新交互模式
   - 制作视频教程：注意力模型、时间线视图
   - 内部培训：新架构的技术细节

4. [ ] **准备 Phase 4 UI 设计**：
   - 设计稿：时间线视图、焦点指示器
   - 原型：可点击的交互原型
   - 用户测试：收集 UI 反馈

---

**文档版本**: v1.1  
**最后更新**: 2026-10-16  
**状态**: Under Revision  
**反馈渠道**: Mirage 维护者 / 设计讨论频道  
**下次评审**: M11 开始前（预计 2026-11-01）

---

## 11. 变更日志

### v1.1 (2026-10-16) - 架构评审修订版

**P0 级别修订**（必须完成才能开始 M11）：
- [ ] **明确 AttentionManager 职责边界**：拆分为 AttentionPolicy / AttentionCoordinator / ActionCoordinator / MiraPetProjection 四层，避免认知决策与执行控制耦合（评审建议 #1）
- [ ] **建立安全焦点切换协议**：Prepare → Suspend → Release → Commit → Resume，包含回滚、超时、崩溃恢复机制（评审建议 #2）
- [ ] **Phase 1 确定 SQLite 作为长期存储**：避免 JSON 分片复杂化，支持事务一致性与崩溃恢复（评审建议 #4）
- [ ] **定义核心不变量**：INV-1 到 INV-7，作为系统约束与测试基线（评审建议 #10）

**P1 级别修订**（M11-M12 优先完成）：
- [ ] **重构记忆激活模型**：分离基础激活度与查询相关性，避免首次回忆归零和时间衰退过度抑制（评审建议 #3）
- [ ] **MiraPet 单向投影**：取消反向影响认知决策，改为忠实投影 committed_focus（评审建议 #5）
- [ ] **持续性认知与自主行为分层**：Phase 1-4 专注身份/注意力/记忆，Phase 5 独立实现动机/目标/预算（评审建议 #6）
- [ ] **并行开发策略**：以 AgentIdentity + EventTimeline 为公共基础，P2/P3 可并行，缩短关键路径（评审建议 #7）
- [ ] **重构验收指标**：优先验证正确性（恢复率、一致性、污染率），性能建立在正确性之上（评审建议 #8）

**P2 级别修订**（M13 及后续阶段）：
- [ ] **EventTimeline 状态一致性**：区分不可变事件记录与可更新状态投影，支持重放与版本化（评审建议 #9）
- [ ] **多用户访问边界**：统一时间线不意味所有内容可无条件检索，需保留作用域与权限（评审建议 #9）

**工期调整**：
- Phase 1: 3周 → 4周（增加 SQLite 迁移）
- Phase 2: 4周 → 6周（增加切换协议与恢复测试）
- Phase 3: 6周 → 8周（增加激活模型重构与验证）
- Phase 5: 8周 → 10周（增加学习机制与安全测试）
- **总计 M11-M14**: 17周 → 22周（实际日历时间可并行至 18-20周）

**新增章节**：
- 4.8 并行开发策略与依赖关系
- 4.9 核心架构不变量与状态一致性
- 8.1 正确性指标（优先级最高）
- 8.5 特殊场景验证

**修订章节**：
- 4.3 Phase 2: 注意力管理器（拆分职责、切换协议）
- 4.4 Phase 3: 动态工作记忆（激活模型重构、SQLite 迁移）
- 4.6 Phase 5: 自主行为能力（分层架构、动机系统）
- 5.4 MiraPet 状态驱动机制（单向投影）
- 7.1 建议排期（工期调整）
- 7.3 M11 前必须完成的工作（架构设计清单）
- 8. 成功标准（增加正确性验证）

---

### v1.0 (2026-10-15) - 初版

- 完成六阶段演进路线图（Phase 1-6）
- 整合 2026 年最新研究成果（Continuum Memory、ACC、人类启发记忆）
- 基于当前 Mirage 架构（M8）设计
- 遵守 AGENTS.md 并发与依赖约束
- 提供 AttentionManager 代码示例
- 明确依赖能力缺口与反馈流程
  
