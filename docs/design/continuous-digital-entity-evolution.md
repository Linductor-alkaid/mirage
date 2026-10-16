# Mira / Mirage 持续性数字个体演进路线图

> **状态**: Proposed  
> **日期**: 2026-10-15  
> **负责人**: Mirage 维护者  
> **相关设计**: [桌面端设计方案](Mirage：Linux%20-%20Windows%20桌面端设计方案.md)、[mira_pet 设计](mira-pet.md)  

## 执行摘要

本文档提出将 Mira/Mirage 从传统的**会话驱动 Agent 工具**演进为**持续性数字个体**的六阶段路线图。核心构想：建立以**统一身份为主体、单一注意力为核心、统一事件时间线组织经历、动态工作记忆维持认知连续性、持久化任务状态保证执行可靠性，并以 MiraPet 作为主要外部表现形式**的新型架构。

**关键演进方向**：
- 多个独立会话 → 统一身份主体
- 静态上下文历史 → 动态工作记忆  
- 并行认知实例 → 单一注意力 + 受控并发
- 会话边界定义存在 → 持续事件时间线
- 任务进度指示器 → 个体状态外显（MiraPet）

演进遵循渐进式原则：保持向后兼容、增量验证、复用已有能力。预计 M10-M13 完成核心能力，M14+ 扩展自主行为与多模态。

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

### 4.2 Phase 1: 统一身份与全局事件时间线（M10 提案）

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

### 4.3 Phase 2: 注意力管理器（M11 提案）

**目标**：从多会话并行转向"单一主注意力 + 受控并发执行"。

**P2.1 AttentionManager 核心**
```cpp
// runtime/attention/attention_manager.hpp
class AttentionManager {
public:
  struct Focus {
    FocusTarget target;  // Task / Workflow / Idle
    std::string target_id;
    std::chrono::steady_clock::time_point since;
    bool interruptible;
  };
  
  enum class SwitchDecision { Accept, Defer, Reject };
  SwitchDecision request_focus(FocusRequest request);
  
  std::optional<Focus> current_focus() const;
  void subscribe_focus_changes(kairo::comm::MailboxHandle<Focus> sink);
};
```

**P2.2 焦点决策策略**
- 优先级：安全状态（接管/断连）> 用户明确请求 > 后台完成通知
- 可中断性判断：
  - `Observing`/`Verifying`：可中断
  - 持有桌面输入租约：不可中断，等待释放
  - 等待外部响应：可中断

**P2.3 与 ActionCoordinator 集成**
- ActionCoordinator 授予租约时通知 AttentionManager
- AttentionManager 优先为焦点任务授予租约
- 非焦点任务请求租约 → 可能触发焦点切换或返回 `desktop_busy`

**P2.4 MiraPet 状态更新**
- mira_pet 显示状态跟随 AttentionManager focus
- 焦点切换时触发 `pet.updated` 事件

**P2.5 后台任务执行模型**
- AttentionManager 不阻止后台任务（编译、网络、文件处理）
- kairo Executor 按现有策略调度
- AttentionManager 只决定"当前主要关注什么"

**验收标准**：
- [ ] 多个活动任务时，MiraPet 显示焦点任务状态
- [ ] 用户新消息触发焦点切换，旧任务进入等待
- [ ] 后台编译完成时，焦点任务未被打断（只收到通知）
- [ ] 桌面输入租约始终由焦点任务持有

**依赖能力**：✅ ActionCoordinator lease、kairo::comm::LatestMailbox、pinned TaskState

**预计工期**：4 周

---

### 4.4 Phase 3: 动态工作记忆（M12 提案）

**目标**：从静态会话历史转向动态构建的工作记忆。

**P3.1 记忆分层模型**
```
层次          内容                    激活机制
────────────────────────────────────────────────
工作记忆      当前任务上下文          始终激活
情景记忆      具体事件、交互历史      按相关性/时间检索
语义记忆      抽象知识、技能          按主题检索
身份记忆      长期偏好、承诺          始终加载
```

**P3.2 记忆激活模型**
```cpp
struct MemoryEntry {
  std::string id;
  MemoryType type;  // Episodic / Semantic / Identity
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_recalled;
  int recall_count;
  float base_importance;
  
  float activation_level() const;  // 动态计算
};
```

**激活度计算**（参考人类记忆模型）：
```
activation = importance × recency × frequency × relevance

recency  = exp(-decay_rate × time_since_last_recall)
frequency = log(1 + recall_count)
relevance = semantic_similarity(memory, current_goal)
```

**P3.3 Context Builder 重构**
- 原 `session.history` 返回完整历史
- 新增 `agent.working_memory`：
  - identity_facts（始终加载）
  - current_task_context（工作记忆）
  - relevant_episodic（高激活度情景）
  - semantic_knowledge（相关语义知识）
  - token_usage（allocated/used）

**P3.4 记忆巩固任务**
- 空闲时（AttentionManager focus = Idle）触发
- kairo::Executor `submit_delayed()` 周期任务
- 将情景记忆抽象为语义知识：
  ```
  多次 Python 调试情景 → "Python常见错误模式"语义知识
  ```

**P3.5 记忆存储策略**
- **短期**（会话内）：内存 `std::deque<MemoryEntry>`
- **中期**（任务级）：`MemoryEventStore` JSON 持久化
- **长期**（Agent 级）：新增 `agent_memory.db`（考虑 SQLite 或分片 JSON）
  - 索引：时间、主题、重要性、回忆次数
  - 定期压缩：超过 N 天且 activation < threshold 的归档

**验收标准**：
- [ ] 长时间运行后，context 不超过 Token 预算
- [ ] 几天前的重要决策仍可检索（activation_level 高）
- [ ] 无关闲聊逐渐退出工作记忆
- [ ] 空闲时执行记忆巩固，不阻塞交互

**依赖能力**：
- ✅ MemoryEventStore、kairo::Executor timer
- ⚠️ 需评估：pinned mira 是否提供语义相似度计算？
  - 若无 → 登记反馈台账 `MIRA-20261015-001`
  - 临时方案：基于关键词重叠的简单相似度

**预计工期**：6 周

---

### 4.5 Phase 4: UI 交互模式演进（M13 提案）

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

### 4.6 Phase 5: 自主行为能力（M14+ 长期）

**目标**：Agent 在无明确指令时可开展有限自主活动。

**自主行为边界**：
- **允许**：整理记忆、学习新知识、准备常用工具
- **需确认**：修改文件、发送消息、调用付费服务
- **禁止**：超出授权范围的系统操作

**P5.1 自主目标系统**
```cpp
enum class GoalPriority { Background, Normal, Urgent };
enum class GoalSource { UserExplicit, SelfGenerated, Scheduled };

struct AutonomousGoal {
  std::string goal_description;
  GoalPriority priority;
  GoalSource source;
  std::vector<std::string> required_capabilities;
  std::chrono::system_clock::time_point earliest_start;
};
```

**P5.2 资源预算管理**
- 默认限制：
  ```
  每小时自主 API 调用：< 10 次
  每天自主任务数：< 5 个
  单次自主任务时长：< 5 分钟
  ```

**P5.3 自主活动示例**
1. **空闲整理**：用户离开时，整理工作区文件（只读分析）
2. **主动提醒**："您今天说要 review PR，现在还有时间吗？"
3. **后台学习**：阅读项目新文档，更新语义知识

**P5.4 用户控制**
- 设置中明确开启/关闭自主行为
- 实时通知："我正在整理你的笔记..."
- 随时可中断

**预计工期**：8 周

---

### 4.7 Phase 6: 多模态感知与表达（M15+ 长期）

**扩展方向**：
1. **语音交互**：用户通过麦克风与 MiraPet 对话
2. **环境感知**：监控系统状态（CPU/内存/网络）作为上下文
3. **主动表达**：MiraPet 通过动画、声音、气泡主动表达
4. **跨设备连续性**：手机/桌面/Web 统一身份，记忆同步


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
- **决策**：M12 阶段先用 JSON，性能瓶颈时再评估 SQLite

**记忆压缩策略**：
- 超过 N 天（如 30 天）且 `activation < threshold`（如 0.1）的情景记忆：
  - 保留摘要，归档原始细节
  - 完整记录仍可检索，但不主动加载到工作记忆
- 巩固：将重复出现的情景模式提取为语义知识

### 5.4 MiraPet 状态驱动机制

**当前模式**（M8）：
```
Runtime 投影 → IPC → mira_pet 播放器
                ↓
        （单向：只展示）
```

**目标模式**（M11+）：
```
                AttentionManager.focus
                       ↓
Runtime 投影 → IPC → mira_pet 播放器
                ↑
        （双向：状态反映决策）
```

**实现机制**：
1. AttentionManager 决定焦点时，考虑：
   - 任务优先级
   - 用户明确请求
   - **当前 MiraPet 展示的任务**（避免频繁切换导致用户困惑）

2. 焦点切换防抖：
   - 最小焦点持续时间（如 30 秒）
   - 5 秒内的多个低优先级通知合并
   - 安全状态（接管/断连）立即打断

3. MiraPet 不仅是结果，也是决策输入：
   - 展示任务 A 时，新低优先级任务 B 不会立即切换
   - 展示任务 A 时，任务 A 完成后自然切换到任务 B
   - 用户明确操作（点击任务列表）可强制切换

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
  - Phase 3 前（M12）完成能力核对
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

## 7. 里程碑与时间线

### 7.1 建议排期

| Phase | 里程碑 | 预计工期 | 关键依赖 | 累计时间 |
|-------|-------|---------|---------|---------|
| P1: 身份+时间线 | M10 | 3 周 | MemoryEventStore 扩展 | 3 周 |
| P2: 注意力管理 | M11 | 4 周 | ActionCoordinator 集成 | 7 周 |
| P3: 动态记忆 | M12 | 6 周 | 依赖能力核对完成 | 13 周 |
| P4: UI 演进 | M13 | 4 周 | 前端重构 | 17 周 |
| P5: 自主行为 | M14+ | 8 周 | 安全边界设计 | 25 周 |
| P6: 多模态 | M15+ | 长期 | 硬件/平台能力 | - |

**关键里程碑成果**：
- **M10 结束**：可按时间线查询跨会话事件，MiraPet 展示统一身份
- **M11 结束**：注意力管理器工作，多任务时只有一个获得焦点
- **M12 结束**：工作记忆动态构建，长期运行不超 Token 预算
- **M13 结束**：用户可无需创建会话与 Agent 交互

### 7.2 并行与依赖

**可并行工作**：
- P1.1（AgentIdentity 模型）与 P1.2（EventTimeline）可并行开发
- P3.2（记忆激活模型）可在 P3.1 完成后立即开始，不等 P2 完成
- P4（UI 演进）前端工作可在 P3 完成前开始准备

**阻塞依赖**：
- P2 依赖 P1：AttentionManager 需要 AgentIdentity 关联任务
- P3 依赖 P2：记忆检索需要知道当前焦点任务
- P4 依赖 P3：新 UI 需要工作记忆 API
- P5 依赖 P4：自主行为需要新交互模式支持

### 7.3 验收门禁

每个 Phase 必须通过：

1. **单元测试**：核心逻辑覆盖率 >80%
2. **集成测试**：与 Runtime/Desktop/Platform 各层协同
3. **性能测试**：延迟/内存/CPU 不超预算
4. **用户测试**：至少 5 名内部用户试用，收集反馈
5. **文档更新**：设计文档、API 文档、用户指南同步
6. **代码评审**：至少 2 名 Maintainer 批准
7. **兼容性验证**：传统模式功能不受影响

---

## 8. 成功标准

### 8.1 技术指标

| 指标 | 目标值 | 测量方法 |
|-----|-------|---------|
| 记忆连续性 | 重启后 90% 重要记忆可恢复 | 标注 100 条重要记忆，重启后检索 |
| 焦点切换延迟 | < 500ms (P95) | 记录 100 次切换请求到 MiraPet 更新时间 |
| 记忆检索延迟 | < 100ms (P95) | 记录 1000 次检索调用时间 |
| 后台任务隔离 | 焦点任务响应时间不受影响 | 对比有/无后台任务的响应时间 |
| 长期运行稳定性 | 连续运行 7 天无崩溃 | 自动化压力测试 |
| Token 预算控制 | 长期运行后 context < 预算 | 记录每轮 context token 数 |
| 记忆巩固延迟 | 空闲时 < 30 秒启动 | 监控巩固任务调度时间 |

### 8.2 用户体验指标

| 指标 | 目标值 | 测量方法 |
|-----|-------|---------|
| 无缝交互 | 80% 用户无需手动创建会话 | 观察 20 名用户使用，统计会话创建次数 |
| 状态理解 | 70% 用户能通过 MiraPet 判断当前工作 | 问卷："看着 MiraPet，你知道它在做什么吗？" |
| 记忆感知 | 60% 用户感觉"Agent 记得我们的对话" | 问卷：5 分量表，≥4 分视为达标 |
| 注意力合理性 | < 10% 用户报告焦点切换不合理 | 收集使用日志，分析投诉率 |
| 任务完成率 | 与当前持平或提升 | 对比新旧模式下相同任务的完成率 |
| 满意度 | NPS ≥ 40 | Net Promoter Score 调查 |

### 8.3 业务指标

| 指标 | 目标值 | 意义 |
|-----|-------|-----|
| 日活跃用户 | 增长 20% | 更自然的交互吸引更多使用 |
| 会话时长 | 增长 30% | 用户愿意与持续个体更长时间交互 |
| 任务重复率 | 降低 15% | 记忆能力减少重复工作 |
| 用户留存（30天） | 提升 10% | 持续身份增强粘性 |


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
  - M12 前（2026-11-30）需明确：pinned mira 是否计划提供？
  - 若否，M12 使用 workaround 先实现架构验证
  - M13+ 评估独立 embedding 服务（需授权）或本地轻量模型

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
   - 时间表如何？是否会在 M12 前交付？
   
2. [ ] **评估 Phase 1 工作量**：
   - 详细拆分 P1.1-P1.4 子任务
   - 评估是否可纳入 M10（3 周工期）
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

**中期目标**（M10-M12，17 周内）：

1. [ ] **Phase 1-3 实现完成**：
   - 统一身份与时间线（M10）
   - 注意力管理器（M11）
   - 动态工作记忆（M12）

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

**文档版本**: v1.0  
**最后更新**: 2026-10-15  
**状态**: Proposed  
**反馈渠道**: Mirage 维护者 / 设计讨论频道  
**下次评审**: M10 开始前（预计 2026-11-01）

---

## 变更日志

- **2026-10-15 v1.0**：初版发布
  - 完成六阶段演进路线图
  - 整合 2026 年最新研究成果
  - 基于当前 Mirage 架构（M8）设计
  - 遵守 AGENTS.md 并发与依赖约束
  
