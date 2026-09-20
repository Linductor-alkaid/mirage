# DEC-016：Mirador 视觉集成契约（Visual Reference、visual_snapshot 组件与视觉会话承载）

> 状态：Accepted
> 日期：2026-09-20
> 负责人：Mirage 维护者
> 冻结里程碑：M3（`M3-01` 落地）
> 替代/被替代：无；细化 [DEC-005](DEC-005-desktop-observation-contract.md)
> 预留的 `visual_snapshot_ref` 组件与解析顺序第三环；遵循
> [DEC-009](DEC-009-provider-scope-budget-cancellation.md) 预算/取消纪律。
> 注意：上游 mirador 自身的 `DEC-016`（kDisplay 融合契约冻结）是 pinned 依赖的
> 决策编号，与本文件同号不同库，引用时冠以"上游"字样。

## 背景与问题

M3 将 pinned mirador（v0.3.0-2）的视觉能力接入 Desktop Observation：OCR、目标
检测、几何结构与能力缓存在设计文档第 8 节定位为"Accessibility 无法表达的桌面
信息"的感知面。落地前必须定案五件事，否则集成层（`M3-02`）与桌面接线
（`M3-03`）没有可验收的契约：

1. `@vN` Visual Reference 的签发与失效生命周期（设计文档第 8 节只给了引用形态
   示例，未给语义）；
2. `visual_snapshot_ref` 的负载形态——DEC-005 schema 1.0 把它留作
   "M3 起填充"的 `std::string`，直接塞整段视觉结果会重蹈 0.1 骨架
   "非结构化字符串"的否决路；
3. mirador 融合快照 / OCR / 检测 / 几何结果中，哪些进入 Observation、哪些只在
   解析时消费——边界不清会把像素、缓存内部结构与诊断痕迹泄漏进感知面；
4. mirador `PerceptionSession` 同步、非线程安全、一 session 一调度上下文的并发
   契约（见其公共头注释与上游 skill `session-and-caching` 卡）如何映射到
   Executor 承载（EXEC-04、RULE-03：不得绕过 Executor，不得自建线程/队列）；
5. 真实 OCR / 检测模型后端缺位时的默认验证形态，以及真实权重接入的触发条件
   （总计划拆分纪律："先 SPI / 假实现后真实依赖"；RULE-08 禁止未经真实后端
   验证的质量声明）。

上游 skill（`docs/skill/mirador-integration/`，随 pin `fff7f15` 交付）提供了
契约输入：`mirador::fusion` 是会话管线的最高层链接目标；任何 kDisplay 空间
证据参与融合时 `FusionOptions::display_transform`（kOriented → kDisplay）必填
（上游 DEC-016），窗口放置属平台知识、由调用方持有；stable id 仅 session 内
唯一，代际失效经 `is_generation_current` 判定；融合/索引/几何预算全部为显式
`kBudgetExceeded` 错误而非截断。

## 决策

1. **Visual Reference 生命周期对齐 `@eN` 整体替换先例**。`@vN` 是**单个视觉
   快照内**的稳定临时句柄（`"@v1"`、`"@v2"`…，按融合区域确定性顺序线性编号，
   与 `@eN` 的 DFS 编号同风格）。Mirage 侧 `VisualReferenceRegistry` 在每次
   视觉快照发布时**整体替换**活动集合（发布原子、读者要么见旧集要么见新集，
   不见混合），旧引用解析即 `not_found` 并产生可追溯事件——不做跨代生存期，
   不暴露 mirador generation 概念。注册表容量受 `VisualSnapshotLimits.
   max_regions`（默认 1024，与 mirador 融合 `max_regions` 预算对齐）约束，
   超限**整体拒绝该次发布**（fail closed，RULE-07 不截断；mirador 侧融合已
   在同预算上先失败，此处为纵深防御）。注册表操作内部互斥（与 `@eN` 注册表
   在 AT-SPI Backend 内的 mutex + map 先例一致）；发布方（视觉会话的串行
   执行上下文）与解析方（动作执行路径）跨上下文时，一致性由"不可变整体快照
   交换"保证。跨帧再定位所需的 mirador stable_id 由集成层内部保留，不出
   Mirage 契约面。
2. **`visual_snapshot` 组件结构化、`visual_snapshot_ref` 为 scope 句柄，
   schema 1.0 → 1.1 加法演进**。新增 pinned-free 结构类型
   `mirage::desktop::VisualSnapshot`：`regions[]`，每项 `VisualRegionEntry`
   含 `ref`（`@vN`）、`source`（`ocr` / `detector` / `template` / `geometry`
   四值词表，映射 mirador 证据来源；OCR → text 形态、template/cache → icon
   形态、detector/geometry → geometry 形态，对应设计文档第 8 节的三种引用
   行）、`bounds`（**全局桌面坐标** `WindowGeometry`——融合目标空间固定为
   kDisplay，`display_transform` 由 Mirage 从窗口放置供给，见决策 3）、
   `text`（OCR 文本或 detector 标签）、`template_id`（缓存/模板标识，可为
   空）、`confidence`。`DesktopObservation` 新增 `visual_snapshot` 结构字段
   （纯加法，schema tag 升 `"1.1"`）；既有 `visual_snapshot_ref` 字符串字段
   落实其 1.0 声明的用途：承载本条观察的视觉 scope 句柄（`"@vs<代数>"`），
   未请求/未捕获视觉组件时为空。Agent 面的紧凑渲染
   （`@v2 icon cache:settings` 行式）由确定性渲染函数与语义快照渲染同源
   供给。schema 演进走 DEC-005 minor 路径；wire/IPC 面若受影响按 DEC-012
   纪律同步 golden vectors（随 `M3-03` 接线落地）。
3. **映射边界：进 Observation 的与只在解析时消费的**。进入感知面的仅限
   mirador 融合输出 `SemanticSnapshot.regions` 经决策 2 的映射结果；
   **Accessibility 区域不作为融合输入**——语义快照已是感知面第一优先
   （DEC-005 解析顺序），把 a11y 区域再喂给融合会在 Observation 中双重
   暴露同一对象，视觉面只融合视觉证据（OCR / 检测 / 模板命中）。只在解析
   或管线内部消费、不进 Observation 的：OCR 原始区域（`ocr_text` 提示匹配
   消费注册表条目而非独立通道）、模板/缓存标识（`template_id` 解析经视觉
   索引，`M3-04`）、几何闭合区域提议（供 ROI 裁剪与 geometry 形态条目，
   `M3-04`）、变化报告与帧指纹（session 内部）、`FusionTrace`（诊断用，
   归集成层与测试所有，默认不落日志、证据文本默认不入日志——隐私纪律）、
   像素数据（永不离开视觉会话）。坐标边界：融合目标空间固定 kDisplay，
   `FusionOptions::display_transform`（kOriented → kDisplay）由 Mirage 从
   ScreenProvider/WindowProvider 的窗口放置构造并随融合调用传入（即使恒等
   也必传——上游契约要求 kDisplay 快照自描述）；`@vN` bounds 因此与
   `window_geometry` / `pointer_state` 同一全局坐标系，可直接驱动输入动作。
4. **视觉会话经 Executor blocking worker 承载，一图像源一 session 一串行
   上下文**（EXEC-04、RULE-03）。每个活动图像源至多一个
   `PerceptionSession`，其全部调用（`analyze_change` / `run_ocr` /
   `run_detector` / `fuse`）固定在该源的 Executor blocking worker 上串行
   执行（worker 生命周期经 Executor 登记，关闭顺序遵循 EXEC-01：停止生产者
   → 请求取消 → 有界排空 → 非 worker 线程 shutdown）。取消映射：
   `desktop::CancelToken` 适配为 mirador `ExecutionContext::is_cancelled`
   轮询通道；deadline 直接映射 `ExecutionContext::deadline`（steady_clock）；
   mirador 在阶段边界轮询上下文，kCancelled / kTimeout 显式返回。并发模型：
   每源同一时刻至多一个分析请求在执行，新请求到达时**最新者优先**——被取代
   的在途请求经取消通道收敛并以 `cancelled` 显式结算（不静默丢弃、不无限
   排队，RULE-10）；admission 拒绝、执行失败、取消/超时经 Executor 设施
   观察，不建平行监控。产出侧经不可变快照交换进入 `VisualReferenceRegistry`
   （决策 1）。
5. **确定性 fake / identity backend 是 M3 的默认验证形态；真实权重接入面
   保留 SPI**。`integration/mirador` 提供确定性 `FakeOcrBackend` /
   `FakeDetectorBackend`（mirador SPI 实现，仅存在于 pinned 边界层）：输出
   由配置与输入视图尺寸确定性推导（同输入同输出，满足缓存正确性）、调用
   计数可观测（缓存命中以后端调用次数证明，上游 skill 纪律）、逐条履行 SPI
   义务（`min_confidence` 过滤、`context` 轮询、kCancelled/kTimeout 显式、
   格式不支持 kUnsupportedFormat、identity 非法 kBackendUnavailable、输出
   上限 kBudgetExceeded 不截断）。M3 全部测试与 headless e2e 以 fake/identity
   backend 验证集成层与解析闭环；**真实模型权重的获取、`model_revision`
   登记与质量/性能评测属集成方职责，不在 M3 范围**。真实后端接入触发条件：
   (a) 集成方提供 ncnn 权重文件与其修订标识；(b) 权重获取进入锁定/SBOM
   机制（DEC-006 第 6 条同类纪律）；(c) 具备可重复的评测场景集。触发前
   Mirage 不得声明任何识别质量、召回或延迟数字（RULE-08）。
6. **目标寻址与权限面不变**。视觉引用经既有 `ElementTarget.reference` 承载，
   以 `"@v"` 前缀与 `"@e"` 区分；解析顺序维持 DEC-005 冻结：reference →
   accessibility → **Mirador cache / OCR / geometry（本决策落地环）** → VLM
   （仅显式开启）；`ocr_text` / `template_id` 提示组解析位于 accessibility
   之后、VLM 之前，降级产生可追溯事件，visual/spatial/raw 的既有 fail-closed
   桩由 `M3-03` 转正。视觉分析为只读感知：不新增 Permission Capability
   词表，解析产出的输入动作复用既有词表（`M3-03` 复核确认，本决策预登记
   结论）。

## 备选方案

- **`visual_snapshot_ref` 内联完整视觉结果文本**：否决。单字符串承载结构化
  结果正是 DEC-005 对 0.1 骨架 `semantic_snapshot` 的否决理由；解析器与
  Agent 将被迫重新解析文本。
- **`@vN` 直接采用 mirador stable_id 作句柄、跨代生存**：否决。stable_id
  仅 session 内唯一且无界增长，跨快照生存使"引用失效"从显式事件退化为
  坐标漂移的静默错误；`@eN` 先例证明"快照内句柄 + 整体替换 + not_found"
  足以支撑动作闭环。stable_id 保留在集成层内部服务于 `M3-04` 再定位。
- **Accessibility 区域作为融合 external 证据**：否决（决策 3）：双重暴露、
  且融合输出将混入平台语义，违反视觉面"不推断平台语义"的分工
  （上游 RULE-11）。
- **融合目标空间取 kOriented、解析时再做显示变换**：否决。`@vN` bounds 若
  非全局坐标，动作路径需二次变换且 Observation 内坐标不再自洽；
  上游要求 kDisplay 快照自描述 transform，正好由 Mirage 持有窗口放置的
  事实承接。
- **每请求创建/销毁 PerceptionSession**：否决。能力缓存与帧签名是 session
  状态，销毁即丢失；一源一长寿命 session + 串行 worker 才能兑现"变化门控
  + 缓存复用"的管线价值。
- **视觉请求共用 M2 观察管线的并行 source 槽**：暂不采用。Mira Observation
  Pipeline 的 screen source 槽面向组件采集；视觉分析需要每源串行 session
  亲和与最新者优先语义，先以独立 blocking worker 承载，`M3-05` 接线时如与
  observe 管线存在结构性重复再评估归并（避免为归并而提前耦合）。
- **在 Mirage 侧自研像素格式转换**：否决（计划已定）。转换必须复用
  mirador `color_convert`，Mirage 不自研重采样（设计文档第 8 节、上游
  `frames-and-coordinates` 卡）。

## 影响与风险

- `desktop/environment` 公共头新增 `visual_snapshot.hpp` /
  `visual_reference_registry.hpp`（pinned-free，边界检查覆盖）；
  `DesktopObservation` 加字段 + schema tag 升 1.1（`M3-03` 落线，本决策
  冻结形态）；`integration/mirador` 新增 fake backend 与（`M3-02`）会话
  适配层，其公共头允许出现 pinned 类型（pinned 唯一边界层），desktop /
  runtime / platform 头仍严格 pinned-free。
- 视觉面默认不开启：observe 请求不含视觉组件时零行为变化；能力如实上报
  （`screen_capture` 等视觉位）随 `M3-05` 扩展，required/optional fail
  closed 语义沿用 M2-06 先例。
- 已知限制：真实 OCR / 检测缺位期间，`ocr_text` / `template_id` 解析与
  `@vN` 闭环仅在 fake/identity 语义下验证；headless X 拓扑 e2e（`M3-05`）
  是其端到端证据，不外推为真实桌面识别能力（RULE-08）。
- `display_transform` 的窗口放置事实来自 X11 Backend（M4 Windows 对齐）；
  几何提议（`geometric_proposal`）消费面以 `M3-04` 边界用例先行，缺口按
  工程规范第 9.4 节登记 `MIRADOR-*` 台账。

## 验证方式

- `tests/desktop/visual_contract_test.cpp`：`VisualRegionEntry` 词表与
  渲染确定性、`VisualReferenceRegistry` 确定性 `@vN` 编号、整体替换后旧
  引用 `not_found`、容量超限整体拒绝（发布前后集合一致、无截断）、发布
  原子性（读者不见混合集合）。
- `tests/integration/fake_visual_backend_test.cpp`（SPI 级，决策 5 负向
  先行）：正/负向——identity 非法 → kBackendUnavailable；声明格式集外的
  prepared view → kUnsupportedFormat；进入即取消 → kCancelled 且调用计数
  不变（取消先于副作用）；deadline 已过 → kTimeout；输出上限 →
  kBudgetExceeded 不截断；同输入确定性等输出；`min_confidence` 过滤生效。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` 构建与 `ctest` 全绿
  （`M3-01` 范围）；公共头 pinned-free 边界检查通过。
- `M3-02`（会话适配）与 `M3-03` / `M3-05`（解析与端到端）在本契约上追加
  各自证据；schema 1.1 的 wire 证据随 `M3-03`。

## 关联文档和工作项

- 设计文档第 6、7、8、9 节（第 8 节视觉集成主设计；本决策为其契约注记）。
- [DEC-005](DEC-005-desktop-observation-contract.md)（schema 演进路径与
  解析顺序）、[DEC-009](DEC-009-provider-scope-budget-cancellation.md)
  （CancelToken 与预算纪律）、[DEC-003](DEC-003-repository-layout.md)
  （integration 唯一 pinned 边界）、
  [DEC-006](DEC-006-ui-web-frontend-packaging.md)（二进制锁定纪律，真实
  权重接入参照）。
- pinned mirador v0.3.0-2 公开头（`perception_session` / `fusion` /
  `semantic_snapshot` / `evidence` / `ocr_backend` / `detector_backend` /
  `execution_context`）与其 `docs/skill/mirador-integration/` 路由卡
  （quick-start、frames-and-coordinates、backends、session-and-caching、
  fusion-and-snapshots）；上游 DEC-016（kDisplay 融合契约）与上游
  DEC-018 阶段 1（几何契约转正）。
- 工作项：[M3 计划](../plans/m3-mirador-integration.md) `M3-01`（本决策）；
  `M3-02` 集成层适配；`M3-03` 组件转正与解析闭环；`M3-04` 缓存与几何；
  `M3-05` runtime 接线与 e2e。
