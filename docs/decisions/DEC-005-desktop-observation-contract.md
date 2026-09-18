# DEC-005：DesktopObservation 契约 schema v1.0 与 Semantic Snapshot / Element Reference 类型

> 状态：Accepted
> 日期：2026-09-18
> 负责人：Mirage 维护者
> 冻结里程碑：M2（`M2-01` 落地）
> 替代/被替代：无（取代 0.1 骨架 schema 的"冻结前过渡形态"地位）

## 背景与问题

设计文档第 6、7 节要求 Mirage 向 Mira 提供结构化 `DesktopObservation`，其中
Semantic Snapshot 来自 Accessibility，每个可交互对象携带临时 Element Reference。
总计划把"DesktopObservation 契约 schema 冻结"列为 M2 的未决决策（原 DEC-005 待建）。
`M1` 交付的是 0.1 骨架：`semantic_snapshot` 为非结构化字符串、无指针/焦点元素字段、
`ElementReference` 仅含 `id`，仅支撑最小观察链路。

`M2-01` 在任何 Backend 实现之前冻结核心 Provider 契约面（Application / Window /
Accessibility / Screen / Input / Clipboard / Notification，配合 M1 已有的 Filesystem /
Process），需要一并定案：

1. Observation 的 v1.0 字段集与各字段类型；
2. Semantic Snapshot 的结构化表示及其与 Element Reference 的关系；
3. 桌面动作的目标寻址形态（多提示组合）与解析顺序（调研第 5.1 节的设计输入：
   "当前不改造成本最低、事后改造成本最高"）。

## 决策

1. **DesktopObservation schema 1.0**：字段集为
   `active_application` / `active_window` / `window_geometry`（全局坐标）/ 
   `window_focused` / `focused_element`（快照内 Element Reference id，未知为空）/
   `semantic_snapshot`（结构化）/ `visual_snapshot_ref`（M3 起填充）/ `pointer_state`
   （全局坐标）/ `environment_state`（backend 摘要字符串）。schema tag
   （`kObservationSchemaVersion`）升为 `"1.0"`：加字段升 minor，破坏性变更升 major。
   自 0.1 的破坏性变更（`semantic_snapshot` 字符串 → `SemanticSnapshot` 结构、新增
   `focused_element` / `pointer_state`）随本决策一并生效；0.1 无外部消费者。
2. **SemanticSnapshot 结构化类型**：`application` / `window_title` /
   `nodes[]`；每节点含 `ref`（快照内稳定句柄 `"@eN"`）、`role`（稳定小写词表，backend
   负责平台角色映射）、`name` / `description`、`parent`（`nodes` 内索引，根为
   `kNoParent`）、`geometry`（全局坐标，可为空）、`focused` / `enabled`。生成受
   `SemanticSnapshotLimits.max_nodes`（默认 4096）约束：超限 fail closed
   （`snapshot_too_large`），不静默截断（RULE-07）。快照生成策略为调用方按需触发；
   增量更新留作后续演进（接口按全量快照设计，不承诺增量语义）。
3. **确定性文本渲染**：`render_semantic_snapshot()` 把快照渲染为设计文档第 7 节的
   紧凑行式文本（`@e5 button "Run" [focused]`），同一快照渲染结果逐字节稳定——它
   是给 Agent 看的表示，结构化类型才是给解析器用的表示，二者同源。
4. **多提示 ElementTarget 与解析顺序**：桌面动作以 `ElementTarget` 寻址，允许以下
   提示组自由组合（至少一组，`has_any_hint()` 判定）：快照 reference（`@eN`）、
   semantic（role/name）、structural（accessibility path）、visual（OCR anchor /
   template id，M3 起解析）、spatial（相对另一元素偏移）、raw（全局坐标点）。
   契约解析顺序固定为：**reference → accessibility（semantic，然后 structural）→
   Mirador 缓存 / OCR / 几何（M3）→ VLM（仅显式开启，永不作为默认 resolver）**；
   每次降级解析产生可追溯事件；raw 点提示最后尝试。解析器实体随 `M2-03` 落地；
   本决策冻结的是顺序与提示组形状。
5. **共享错误词表**：`ProviderError` 的稳定 code 词表收敛于
   `provider_error.hpp` 文档（含 `result_too_large`：枚举/表格类结果超预算整体拒绝，
   不静默截断；`file_too_large` / `snapshot_too_large` / `capture_too_large` /
   `clipboard_too_large` 为对应 Provider 的既有具体形态）。
6. **契约验证纪律**：全部新 Provider 契约测试经 `tests/support/fake_desktop_
   environment.hpp` 内存实现执行；fake 与真实 Backend 负责相同的契约义务（预算拒绝、
   取消先于副作用、非法参数先于状态变更、缺位 Provider 的访问器返回 null 且消费方
   fail closed）。

## 备选方案

- **快照保持纯文本（0.1 形态）**：否决。文本无法支撑 Element Reference 解析与
  结构化断言，M3 视觉融合也会被迫重新解析文本。
- **树形嵌套节点表示**：否决。扁平数组 + 父索引在预算控制、序列化与渲染上更简单，
  树信息不丢失；嵌套所有权使预算截断语义复杂化。
- **单一 selector 字符串（OpenRPA 风格）**：否决（调研第 5.1 节）。多提示组合 +
  固定解析顺序把"选择器失效"从运行时猜测变成可追溯的降级链。
- **Observation 内嵌完整 Accessibility 树**：否决。设计文档第 7 节明确 Observation
  应为紧凑快照；完整树属 Provider 内部，经预算快照暴露。
- **把解析器放进各 Provider**：否决。解析需跨 Provider（reference → semantic →
  visual）并维护顺序契约，属 desktop 层组合逻辑，`M2-03` 以独立解析器承载。

## 影响与风险

- `desktop/environment` 公共头新增 7 个 Provider 接口、`geometry.hpp` /
  `semantic_snapshot.hpp`、重写 `element_reference.hpp`（`ElementTarget`）与
  `desktop_observation.hpp`（v1.0）；`DesktopEnvironment` 访问器扩至 9 个（缺位
  返回 null，向后兼容）。
- Backend 未实现新 Provider 前所有新访问器 fail closed（null），M1 行为不变；
  `kObservationSchemaVersion` 升级对 M1 集成链路无 wire 影响（observe 契约仍为
  最小观察，能力面扩展随 `M2-06`）。
- `role` 词表、structural path 语法目前只约束"稳定、小写、平台角色映射"，具体词表
  随 `M2-03` AT-SPI2 实现冻结并在设计文档补录；提前冻结跨平台词表缺乏依据。
- Input 的 `KeySym` 词表与 `is_valid_utf8` / `is_valid_key_name` 校验助手为契约
  一部分（backend 与测试共用），Windows Backend（M4）必须复用同一词表映射。

## 验证方式

- `tests/desktop/provider_contract_test.cpp`：9 Provider 访问器 fail closed、
  各 Provider 预算拒绝（`result_too_large` 等）、取消先于副作用、非法参数先于状态
  变更、快照渲染确定性、`ElementTarget` 提示组计数、`KeySym` / UTF-8 校验词表、
  M1 Provider（filesystem/process）在 fake 上的契约保持。
- `tests/desktop/desktop_observation_test.cpp`：v1.0 字段默认值与 schema tag。
- 公共头 pinned-free 边界检查（`mirage-boundary-check`）。

## 关联文档和工作项

- 设计文档第 5、6、7、9 节。
- 总计划：`SCOPE-02` / `SCOPE-03`、`RULE-07`、原"尚未冻结的决策"表 DEC-005 行。
- 调研输入：[RPA / Agentic Automation 架构调研](../research/2026-09-16-rpa-agentic-automation-architecture-survey.md)
  第 5.1 节（多提示 Target、解析顺序、canonical types）。
- 工作项：[M2 计划](../plans/m2-desktop-environment.md) `M2-01`（本决策）；
  `M2-03` 解析器实现；`M2-06` Observation 组装与 runtime 接线；`M4` Windows 词表
  映射。
