# M10 启动前必须完成的工作清单

> **关联文档**: [持续性数字个体演进路线图 v1.1](../design/continuous-digital-entity-evolution.md)  
> **截止日期**: 2026-11-01（预计 M10 启动日期）  
> **负责人**: Mirage 维护者

## 状态说明

本清单来源于 2026-10-16 架构评审修订版路线图第 7.3 节。所有 P0 级别工作必须在 M10 开始前完成，否则延期 M10 启动。

---

## P0 级别工作（阻塞 M10）

### 1. 架构设计文档（2 周内完成）

#### 1.1 核心不变量文档
- [ ] **文件**: `docs/design/continuous-digital-entity-invariants.md`
- [ ] **内容**:
  - INV-1 到 INV-7 详细定义（每个不变量 1-2 页）
  - 每个不变量的验证方法（单元测试、集成测试、运行时监控）
  - 不变量违反时的系统行为（崩溃？报警？降级？）
  - 代码中哪些位置必须维护这些不变量（写入点、读取点）
- [ ] **评审**: 至少 2 名 Maintainer 批准
- **预计工时**: 3 天

#### 1.2 AttentionCoordinator 切换协议详细设计
- [ ] **文件**: `docs/design/focus-switching-protocol.md`
- [ ] **内容**:
  - Prepare / Suspend / Release / Commit / Resume 五阶段详细语义
  - 每个阶段的前置条件、后置条件、不变量维护
  - 超时处理：每个阶段最长等待时间（如 Prepare 3s, Suspend 10s）
  - 回滚机制：哪个阶段失败回滚到哪里
  - 崩溃恢复：进程重启后如何识别中断的切换
  - 竞态条件处理：切换过程中新请求如何排队
  - DurableTaskState 检查点格式
- [ ] **代码示例**: 完整的 `AttentionCoordinator::request_switch()` 伪代码
- [ ] **状态机图**: 绘制状态转换图（Graphviz / Mermaid）
- [ ] **评审**: 至少 2 名 Maintainer 批准
- **预计工时**: 5 天

#### 1.3 MemoryStore 激活模型设计
- [ ] **文件**: `docs/design/memory-activation-model.md`
- [ ] **内容**:
  - 基础激活度公式（与查询无关）
  - 查询相关性评分公式
  - 最终排序策略（加权 vs 乘法 vs 两阶段）
  - 时间衰退曲线（指数 vs 线性 vs 分段）
  - 首次回忆保护机制（recall_count=0 时如何处理）
  - 重要记忆保留策略（importance >0.7 不受时间衰退）
  - 回忆强化机制（如何更新 last_recalled / recall_count）
  - 证据可信度（如何避免错误记忆自我强化）
- [ ] **标注测试集**: 100 条查询 + 期望结果（召回率 >80%）
- [ ] **评审**: 至少 2 名 Maintainer 批准
- **预计工时**: 4 天

#### 1.4 SQLite Schema 最终确定
- [ ] **文件**: `docs/design/sqlite-schema-v1.md`
- [ ] **内容**:
  - events / episodic_memories / semantic_memories / memory_associations 表结构
  - 索引策略（时间、主题、重要性、激活度）
  - 迁移计划（JSON → SQLite 双写 → 只读 JSON → 移除 JSON）
  - 事务边界（哪些操作必须原子）
  - WAL 模式配置（Write-Ahead Logging）
  - 备份与恢复策略
  - 性能基线目标（10K 事件查询 <10ms）
- [ ] **性能验证**: 创建 10K 条测试数据，验证查询延迟
- [ ] **评审**: 至少 2 名 Maintainer 批准
- **预计工时**: 3 天

#### 1.5 依赖能力确认
- [x] **公开能力核对**（2026-10-25，pin `68a8bf1`，上游 DEC-060 / PR#103）：
  - Mira 已提供 `IEmbeddingProvider`、`IMemoryEmbeddingIndex`、`MemoryEmbeddingIndexer`。
  - Core 不含 embedding 模型推理；宿主提供生产者并管理 Executor 路由，复用上游向量召回与有界修复。
  - [MIRA-20261016-001](../dependency_feedback/mira.md#mira-20261016-001) 为 Accepted（部分交付）；不重复提报已有 API。
  - 该勾选仅确认接口与职责，不代表 M10 产品接线或真实语义质量通过。
- [ ] **fallback 方案评估**:
  - 基于关键词 Jaccard 相似度的质量如何？
  - 在标注测试集上的召回率是否可接受（>60%）？
  - M12 是否需要切换到本地 embedding 模型（如 sentence-transformers）？
- [ ] **决策记录**: 写入 `docs/decisions/`
- **预计工时**: 2 天（含等待反馈）

---

### 2. 技术验证（1 周内完成）

#### 2.1 SQLite 性能基线
- [ ] **验证目标**: 10K 事件查询延迟 < 10ms (P95)
- [ ] **测试脚本**: `tests/performance/sqlite_baseline.cpp`
- [ ] **测试数据**: 生成 10K 条模拟事件（时间、类型、内容、关联任务）
- [ ] **查询场景**:
  - 按时间范围查询（最近 24 小时）
  - 按任务 ID 查询（某任务的所有事件）
  - 按事件类型查询（所有 tool_call 事件）
  - 全文搜索（LIKE '%关键词%'）
- [ ] **索引优化**: 根据结果调整索引策略
- [ ] **结论记录**: 写入 `docs/performance/sqlite-baseline.md`
- **预计工时**: 2 天

#### 2.2 切换协议原型验证
- [ ] **原型目标**: 验证 Prepare → Resume 流程可行性
- [ ] **实现范围**: 简化版 AttentionCoordinator（无完整 DurableTaskState）
- [ ] **验证场景**:
  - 正常切换（任务 A 完成 → 任务 B）
  - 拒绝切换（任务 A 不可中断）
  - 超时回滚（Suspend 超时 → 恢复原焦点）
- [ ] **代码位置**: `tests/prototypes/focus_switching_prototype.cpp`
- [ ] **结论**: 协议设计是否可行？哪些环节需要调整？
- **预计工时**: 3 天

#### 2.3 记忆激活公式验证
- [ ] **验证目标**: 标注测试集召回率 >80%
- [ ] **测试集**: 100 条查询，每条标注期望的 top-5 记忆
- [ ] **激活公式**: 实现 1.3 中设计的公式
- [ ] **评估指标**:
  - Recall@5（top-5 中命中期望记忆的比例）
  - MRR（Mean Reciprocal Rank）
  - 首次回忆成功率（recall_count=0 的记忆）
- [ ] **结论**: 公式是否达标？需要哪些参数调整？
- **预计工时**: 2 天

---

### 3. 团队对齐（1 周内完成）

#### 3.1 架构评审会
- [ ] **时间**: 预计 2026-10-25
- [ ] **参会者**: Mirage 维护者 + 至少 2 名外部 Reviewer
- [ ] **议程**:
  1. 演示路线图 v1.1 修订内容（30 分钟）
  2. 评审核心不变量文档（30 分钟）
  3. 评审切换协议设计（30 分钟）
  4. 评审记忆激活模型（30 分钟）
  5. Q&A 与风险讨论（30 分钟）
- [ ] **产出**: 会议纪要 + Action Items
- [ ] **决策**: Go / No-Go 决定（是否启动 M10）

#### 3.2 风险评估会
- [ ] **时间**: 架构评审会后 2 天
- [ ] **参会者**: Mirage 核心团队
- [ ] **议程**:
  1. 回顾路线图第 6 节（风险与缓解）
  2. 识别遗漏风险（头脑风暴）
  3. 为每个风险制定缓解措施
  4. 更新风险台账
- [ ] **产出**: `docs/risks/m10-risks.md`

#### 3.3 资源确认会
- [ ] **时间**: 风险评估会后 1 天
- [ ] **参会者**: Mirage 维护者 + 项目经理
- [ ] **议程**:
  1. 确认 M10-M13 人力分配
  2. 识别排期冲突（如其他项目占用人力）
  3. 调整并行工作安排
  4. 明确关键路径上的责任人
- [ ] **产出**: `docs/plans/m10-m13-resource-plan.md`

---

## 完成标准

**架构设计**：
- ✅ 所有 5 份设计文档完成并通过评审
- ✅ 核心不变量可转化为自动化测试

**技术验证**：
- ✅ SQLite 性能达标（10K 事件 <10ms）
- ✅ 切换协议原型可行
- ✅ 记忆激活公式召回率 >80%

**团队对齐**：
- ✅ 架构评审会通过（Go 决定）
- ✅ 风险台账更新
- ✅ 资源计划确定

---

## 阻塞情况处理

**如果无法按时完成**：
1. **延期 M10 启动**（优先选择）
2. **降低 M10 范围**（如只做 AgentIdentity，EventTimeline 推到 M11）
3. **寻求外部支持**（如 Mira 维护者协助设计）

**不可接受的选项**：
- ❌ 跳过设计直接编码（会在 M11/M12 返工）
- ❌ 忽略不变量定义（导致状态一致性问题）
- ❌ 使用未验证的 SQLite schema（后期迁移困难）

---

## 进度跟踪

| 工作项 | 负责人 | 开始日期 | 完成日期 | 状态 | 备注 |
|-------|-------|---------|---------|------|------|
| 核心不变量文档 | | | | 🔴 Not Started | |
| 切换协议设计 | | | | 🔴 Not Started | |
| 激活模型设计 | | | | 🔴 Not Started | |
| SQLite Schema | | | | 🔴 Not Started | |
| 依赖能力确认 | | | | 🔴 Not Started | |
| SQLite 性能验证 | | | | 🔴 Not Started | |
| 切换协议原型 | | | | 🔴 Not Started | |
| 激活公式验证 | | | | 🔴 Not Started | |
| 架构评审会 | | | | 🔴 Not Started | |
| 风险评估会 | | | | 🔴 Not Started | |
| 资源确认会 | | | | 🔴 Not Started | |

**状态说明**：
- 🔴 Not Started：尚未开始
- 🟡 In Progress：进行中
- 🟢 Completed：已完成
- 🔵 Blocked：被阻塞

---

**文档版本**: v1.0  
**创建日期**: 2026-10-16  
**最后更新**: 2026-10-16  
**下次更新**: 每周五更新进度
