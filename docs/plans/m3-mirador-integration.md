# M3：Mirador 视觉集成

> 状态：In Progress（`M3-01`、`M3-02`、`M3-03`、`M3-04` 完成）
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M2](m2-desktop-environment.md)（已完成：Desktop Environment 核心 Provider、
> Linux Backend、Semantic Snapshot、ElementTarget 解析顺序契约、Observation 组装
> 与 runtime 接线）
> 建议发布点：`release-gamma`（tag 待维护者授权后创建）
> 更新日期：2026-09-20（`M3-04` 完成）

## 目标

接入 pinned mirador（v0.3.0）的视觉能力——OCR、目标检测、几何结构、Visual
Cache——使 Desktop Observation 形成 Accessibility 与视觉结合的桌面感知；实现
Visual Reference（`@vN`）的签发与解析闭环，补齐 DEC-005 解析顺序中
"Mirador cache / OCR / geometry" 一环；同步完成 DEC-006 壳选型 PoC 与冻结
（设计文档第 18 节第三阶段）。

## 范围与非目标

范围：

- 视觉集成契约与决策记录（`DEC-016`，`M3-01` 产出）：Visual Reference 生命周期、
  `visual_snapshot_ref` 组件语义与 schema 演进、mirador 结果到 Mirage 感知面的
  映射边界、Executor 承载方式。
- `integration/mirador` 集成层：mirador `PerceptionSession`（变化分析 → 按需
  OCR / 检测 → 坐标恢复 → 能力缓存 → 融合发布）的承载与适配，ScreenProvider
  采集帧到 mirador `Frame` 的转换，取消 / deadline 到 mirador
  `ExecutionContext` 的映射，视觉后端 SPI（`OcrBackend` / `DetectorBackend`）
  注册与 identity 校验。
- Visual Observation 组件接入 Observation 组装（`visual_snapshot_ref`）、
  Visual Reference 注册表（`@vN` 签发与失效语义）与 ElementTarget visual 提示
  解析器（`ocr_text` / `template_id` → 坐标 → 输入动作执行路径）。
- Visual Cache（`visual_index` 三层证据匹配）与几何提议（`geometric_proposal`，
  上游 `DEC-018` 阶段 1 已转正）接入：模板 / 缓存标识解析、闭合区域 →
  Tight / Context ROI。
- runtime 接线：observe 请求视觉组件的映射与能力如实上报扩展，增强 Observation
  进入 Mira 观察面；headless X 拓扑端到端闭环（fake / identity backend）。
- [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md) 壳选型 PoC 与
  冻结评审（CEF 暂定默认值 vs Tauri / Electron），更新通道签名 / 差分策略复核。

非目标：

- Windows Backend（M4）。
- 真实模型权重的 OCR / 检测后端交付与质量评测：mirador 的模型后端由集成方提供
  （参考 ncnn integration 默认关闭且需网络获取），M3 以 fake / identity backend
  验证集成层（总计划拆分顺序"先 SPI / 假实现后真实依赖"），真实权重接入面保留
  SPI；不声明未经真实后端验证的识别质量或性能（`RULE-08`）。
- 桌面 GUI 产品界面（M5）：M3 仅完成壳选型冻结，不实现产品 UI；视觉状态经 IPC
  向 UI 的呈现与订阅演进属 M5。
- mirador 上游 `DEC-018` 阶段 2 能力（`temporal_stability`、融合 / 输出模型
  集成）：上游另行立项交付后再评估引入。
- VLM 显式提示路径的消费：Agent 侧 VLM 调用属 Mira 职责，Mirage 只在
  DEC-005 解析顺序中保留其位置；SoM 标注渲染面（mirador `set_of_mark`）的
  对外暴露随 VLM 路径启用时另行评估，M3 不强制交付。
- mirador 自带 capture adapter 的引入：设计文档第 8 节约定采集由 Mirage
  ScreenProvider 承担，Mirador 只消费帧。
- Wayland 原生（非 XWayland）采集的一等支持（延续 M2 非目标；视觉管线以既有
  ScreenProvider 能力为边界，缺失 fail closed 且 capabilities 如实收缩）。

## 设计与决策依据

- [设计文档](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md) 第 6、7、
  8、9、17、18 节（第 8 节为视觉集成主设计：采集 → Mirador 分析 → 增强
  Observation → Visual Reference → 坐标与输入行为）。
- [DEC-005](../decisions/DEC-005-desktop-observation-contract.md)：
  DesktopObservation schema 1.0（`visual_snapshot_ref` 预留位）、ElementTarget
  解析顺序契约（reference → accessibility → Mirador cache / OCR / geometry →
  VLM 显式）、加法演进升 minor 版本。
- [DEC-003](../decisions/DEC-003-repository-layout.md)：分层依赖方向；
  `integration` 是 pinned 依赖唯一边界层，mirador 类型不出 Mirage 公共头
  （`RULE-01`）。
- [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)：壳选型与更新
  通道暂定默认值最迟 M3 冻结；PoC 验证窗口嵌入、本地资产加载、IPC 桥延迟基线。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)：
  范围 / 预算 / 取消语义（视觉管线的缓存预算、取消先于副作用沿用同一纪律）。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)：视觉分析为只读
  感知，不新增副作用 Capability；解析产出的输入动作复用既有词表（`M3-03`
  复核确认）。
- 总计划 `EXEC-04`：截图、OCR 请求等阻塞或耗时操作使用 blocking worker 承载；
  取消路径必须可解除阻塞。
- pinned mirador v0.3.0：[`docs/api/README.md`](../../third_party/mirador/docs/api/README.md)
  模块索引（`perception_session` / `evidence` / `fusion` / `visual_index` /
  `capability_cache` / `set_of_mark` / `geometric_proposal`）；上游
  `DEC-018` 阶段 1 转正记录（几何契约计入上游兼容性承诺）；
  `PerceptionSession` 同步 API、非线程安全、一 session 一调度上下文的并发
  契约。

## 工作项

- [x] `M3-01` 视觉集成契约与决策记录 `DEC-016`：Visual Reference 生命周期
      （`@vN` 签发、注册表容量与换代失效语义，对齐 `@eN` 整体替换先例）、
      `visual_snapshot_ref` 组件的负载形态与 schema 1.0 → 1.x 加法演进、
      mirador 融合快照 / OCR / 检测 / 几何结果到 Mirage 感知面的映射边界
      （哪些进 observation、哪些只在解析时消费）、视觉会话的 Executor 承载
      方式（blocking worker + 每图像源一 session 的串行化模型，`EXEC-04`）、
      fake / identity backend 作为默认验证形态。契约头与 fake 负向测试先行。
- [x] `M3-02` 集成层适配器（`integration/mirador`）：`PerceptionSession`
      创建 / `analyze_change` / `run_ocr` / `run_detector` / `fuse` 的封装；
      按消费面为集成层显式链接所需 mirador 模块目标（当前 Mirage 仅链
      `mirador::core`，`image` / `cache` / `geometry` / `fusion` / `render`
      均不在默认构建依赖图内，见 2026-09-20 依赖升级审计）；ScreenProvider
      Bgra8 采集帧到 mirador `Frame` 的转换（经 mirador
      `pixel_format` / `color_convert`，不自研转换）；取消与 deadline 到
      mirador `ExecutionContext` 的映射；`OcrBackend` / `DetectorBackend` SPI
      注册、`VisualBackendIdentity` 校验接线与确定性 fake / identity backend
      实现（正 / 负向：格式不支持、后端不可用、取消、预算）。
- [x] `M3-03` Visual Observation 组件与解析闭环：ObservationAssembler 增
      visual 组件（`visual_snapshot_ref` 从预留位转正，schema 1.x）；Visual
      Reference 注册表（`@vN` 签发、预算、快照换代失效）；ElementTarget
      visual 提示解析器（`ocr_text` 匹配 OCR 区域、`template_id` 走缓存索引，
      DEC-005 顺序中位于 accessibility 之后、VLM 之前，降级产生可追溯事件）；
      visual 解析 → 全局坐标 → InputProvider 执行路径（`click(@vN)` 闭环）；
      Permission 面复核（视觉分析只读、无新增 Capability 词表）。
- [x] `M3-04` Visual Cache 与几何提议接入：`visual_index` 三层证据（精确哈希 /
      感知哈希 / 模板 NCC）匹配服务于 `template_id` 解析与跨帧元素再定位；
      `capability_cache` / `frame_cache` 预算与 Mirage 侧会话生命周期的接线
      （`RULE-07` 容量上限、背压显式）；`geometric_proposal` 闭合区域 →
      Tight / Context ROI 供给检测请求与采集裁剪。
- [ ] `M3-05` runtime 接线与端到端闭环：`integration/mira` observe 能力如实
      上报扩展（视觉组件映射与 required / optional fail closed 语义对齐
      M2-06 先例）；增强 Observation 进入 Mira 观察面；headless X 拓扑 e2e：
      采集 → fake 视觉分析 → `@vN` 签发 → ElementTarget visual 解析 → 坐标
      命中注入 → 再观察更新。
- [ ] `M3-06` DEC-006 壳选型 PoC 与冻结：CEF（暂定默认值）vs Tauri / Electron
      对比取证（窗口嵌入、本地资产加载、IPC 桥延迟基线、包体积与内存基线、
      Linux 发行版兼容性风险）；壳二进制 / 前端依赖进入锁定与 SBOM 的机制
      复核；更新通道签名 / 差分策略复核；结论回写 DEC-006（含否决理由封存）。
- [ ] `M3-07` 里程碑退出复核：退出条件逐项独立取证（同 M2-07 形态）。

拆分纪律：契约先于实现（`M3-01` 先行）；集成层先于桌面接线（`M3-02` →
`M3-03` / `M3-04` → `M3-05`）；fake / identity backend 先于真实模型依赖；
视觉路径全程经 Executor 承载，不自建线程或队列（`RULE-03`）。

## 风险与阻塞

- **真实模型后端缺位**：M3 视觉能力验证止于确定性 fake / identity backend；
  真实 OCR / 检测的识别质量、召回与性能不在 M3 声明范围（`RULE-08`）。真实
  权重获取与评测属集成方职责，触发条件与证据要求在 `DEC-016` 中登记。
- **mirador 同步 API 与线程模型**：`PerceptionSession` 非线程安全、一 session
  一调度上下文，OCR / 检测为同步阻塞调用；承载方式（blocking worker 串行化、
  取消经 `ExecutionContext` 在阶段边界生效、会话与 worker 生命周期归属）必须
  在 `M3-01` / `DEC-016` 定案后实现，避免绕过 Executor（`RULE-03`）或长任务
  不可取消。
- **像素格式与色彩转换**：ScreenProvider 交付 Bgra8；各后端接受格式集由
  identity 声明，转换必须复用 mirador `color_convert`；转换引入的坐标与内容
  一致性（旋转 / 缩放）依赖 mirador 变换契约，Mirage 侧不自研重采样。
- **DesktopObservation schema 演进**：`visual_snapshot_ref` 组件的加法演进须
  走 DEC-005 声明的 minor 版本路径，wire / IPC 面若受影响按 DEC-012 变更纪律
  同步 golden vectors；不得静默修改已冻结字段集。
- **DEC-006 PoC 环境依赖**：CEF 二进制获取与 Tauri 工具链需要网络与磁盘预算，
  沙箱环境受限时 PoC 以可复现的延迟 / 体积 / 内存基线方法学先行，安装级验证
  留 M5；二进制来源未进入锁定机制前不得进入默认构建（DEC-006 第 6 条）。
- **几何提议契约较新**：`geometric_proposal.hpp` 虽经上游 `DEC-018` 阶段 1
  转正（2026-09-20），实际消费面仍以 `M3-04` 的边界用例先行验证；发现缺口按
  工程规范第 9.4 节登记 `MIRADOR-*` 台账条目，不在 Mirage 侧临时补位。

## 测试与退出条件

- [ ] `debug`、`release`、`asan`、`ubsan` 预设构建通过，`ctest` 全绿且无 skip；
      涉及跨上下文状态或关闭路径的变更以 `tsan` 覆盖（按 README 注意事项运行）
      （`DOD-03`）。
- [ ] 视觉契约与集成层经 fake / identity backend 测试覆盖正 / 负向：预算拒绝
      不截断、取消先于副作用、格式不支持 / 后端不可用 fail closed、缓存命中
      与未命中的一致性、注册表换代失效（`DOD-04`）。
- [ ] headless X 拓扑端到端：采集 → fake 视觉分析 → `@vN` 签发 → ElementTarget
      visual 解析 → 全局坐标命中（输入注入验证）→ 再观察更新；observe 能力
      如实上报扩展经绑定层测试覆盖。
- [ ] 公共头边界检查通过：`integration` / `desktop` / `runtime` / `platform`
      公共头 pinned-free，mirador 类型不出 Mirage 公共接口（`DOD-01`）。
- [ ] 视觉任务路径经 Executor 观察：admission 拒绝、执行失败、取消 / 超时、
      关闭状态可经 Executor 设施观察，无平行监控（`EXEC-04`、`RULE-03`）。
- [ ] `DEC-016` 定案并同步设计文档第 8 节注记；`DEC-006` 壳选型冻结并回写
      结论；（若触发）依赖反馈台账登记；计划状态与验证证据同步（`DOD-05`）。
- [ ] Commit / MR 符合工程规范第 10 节；`dependencies.lock.json` 与 submodule
      指针一致（`DOD-06`）。
- [ ] 不出现未经真实模型后端验证的识别质量、性能或实时性声明（`RULE-08`）；
      如声明缓存命中或延迟目标，按工程规范第 7 节取证。

## 验证记录

2026-09-20：里程碑计划建立。前置依赖升级独立 MR `chore/deps-mirador-v0.3.0`
（mirador `50f5349` → `6fa92ec`，v0.3.0；升级审计见
[依赖升级审计](../supply-chain/dependency-upgrade-audit.md) 2026-09-20 条目）。
后续按工作项追加实施与验收记录。

2026-09-20：`M3-01` 完成（分支 `feat/m3-01-visual-contract`，基于
`chore/deps-mirador-integration-skill`）。前置：mirador pin 前滚至 `fff7f15`
（v0.3.0-2，docs-only：README 重构 + `docs/skill/mirador-integration/` 下游
skill；审计见依赖升级审计同日条目，MR `chore/deps-mirador-integration-skill`），
skill 卡片为契约输入。

- **决策**：[DEC-016](../decisions/DEC-016-mirador-visual-integration-contract.md)
  定案六项——`@vN` 生命周期对齐 `@eN` 整体替换先例（容量 fail closed，默认
  1024 与融合预算对齐）；`visual_snapshot` 结构组件 + `visual_snapshot_ref`
  scope 句柄（schema 1.0 → 1.1 加法演进，`M3-03` 落线）；映射边界（仅融合
  输出进感知面，a11y 区域不作融合输入，trace/指纹/像素不外泄）；一图像源一
  session 一 blocking worker 串行承载（EXEC-04，CancelToken→`ExecutionContext`
  适配，最新者优先取代语义）；fake/identity backend 默认验证形态与真实权重
  触发条件（RULE-08）；解析顺序第三环与只读权限面预登记。设计文档第 8 节已
  加契约注记。
- **契约头（pinned-free）**：`desktop/environment` 新增
  `visual_snapshot.hpp`（`VisualRegionSource`/`VisualRegionEntry`/
  `VisualSnapshot`/`VisualSnapshotLimits`/`VisualPublishOutcome`/
  `render_visual_snapshot`）与 `visual_reference_registry.hpp`
  （`VisualReferenceRegistry`：整体替换、`@vN` 确定性编号、`@vs<gen>` scope、
  容量拒绝、互斥快照交换）。
- **fake backend（pinned 边界层）**：`integration/mirador` 新增
  `FakeOcrBackend` / `FakeDetectorBackend`（确定性身份语义、调用计数、
  kBackendUnavailable / kUnsupportedFormat / kCancelled / kTimeout /
  kBudgetExceeded 负向语义、`min_confidence` 过滤）。
- **测试证据**（Independent-Verification-Agent 独立执行）：新增
  `tests/desktop/visual_contract_test.cpp`（83 checks：渲染确定性、编号/
  scope、换代失效、容量拒绝、clear 语义、1 写 2 读并发原子性压测）与
  `tests/integration/fake_visual_backend_test.cpp`（71 checks：门槛 a–h
  正/负向、门序取消先于 deadline、identity 校验正/负）；
  `debug` / `release` / `asan` / `ubsan` / `tsan`（`setarch -R`）五预设
  configure + build + ctest 26/26 通过、0 skip；asan 报告 0，ubsan verbose
  `runtime error` 0，tsan 并发压测无竞争报告；`mirage-format-check` 通过；
  `mirage-boundary-check` 通过（35 头 0 命中）。
- **未覆盖项**：schema 1.1 的 wire/IPC golden vectors 随 `M3-03`；真实模型
  后端行为不在 M3 声明范围（RULE-08，触发条件见 DEC-016 决策 5）。

2026-09-20：`M3-02` 完成（分支 `feat/m3-02-visual-session-adapter`，基于
`M3-01` 合入后的 `master`）。按 DEC-016 决策 3/4/5 落地集成层适配器。

- **集成层适配器**（`integration/mirador`）：`visual_execution_context`
  （`desktop::CancelToken` → `ExecutionContext::is_cancelled` 轮询通道、
  deadline 直映射）；`visual_frame`（ScreenProvider Bgra8 采集帧 →
  mirador `Frame`：像素拷贝入共享 owner、`mirador::validate` 复核、负路径
  显式错误码；`convert_frame` 经 mirador `convert_color` 的预算化格式
  转换，不自研 kernel）；`visual_backend_registry`（SPI 注册：identity 过
  `validate_backend_identity`、且与后端 `info()` 逐字段一致（含
  accepted_formats 有序），容量 8 fail closed）；`visual_session_host`
  （一图像源一 `PerceptionSession` 一 Executor blocking worker 串行承载，
  EXEC-04；请求经 `executor::comm::LatestMailbox` 最新者优先传输，被取代
  /外部取消/停止显式结算为 `cancelled`，deadline 显式 `kTimedOut`，取消
  先于副作用；fusion 固定 kDisplay 目标空间 + 调用方 `display_transform`，
  融合输入仅限 OCR / 检测证据）。CMake 按消费面改链 `mirador::fusion`
  （PUBLIC 携带 `image` / `cache`）+ executor（SYSTEM 头 + 按文件路径
  链接，同 runtime service 惯例）。
- **测试证据**（Independent-Verification-Agent 独立执行）：新增
  `tests/integration/visual_session_test.cpp`（26 场景 214 checks：
  context 映射、帧包装 / owner 存活 / 负路径、convert 通道序 / 预算 /
  格式矩阵、注册表正负向与容量、融合 happy path（kDisplay 坐标经
  display_transform 平移验证）、缓存命中以后端调用计数不变证明、null
  后端 / 不可达格式 / 预算 / 缺 display_transform / 过期 deadline /
  预取消各负路径、admission 拒绝、最新者优先取代在途分析、stop 对在途
  与 queued 请求的结算、首帧 kGlobal / kFirstFrame、非法预算 start 失败
  闭合）；`debug` / `release` / `asan` / `ubsan` / `tsan`（`setarch -R`）
  五预设 configure + build + ctest 27/27 通过、0 skip；asan 0 报告，
  ubsan verbose 无 `runtime error`，tsan 并发场景无竞争报告；
  `mirage-format-check` 通过；`mirage-boundary-check` 通过（35 头
  0 命中）。
- **过程记录**：首轮独立验证发现 `to_mirador_frame` 在 `view.data` 赋值
  前调用 `mirador::validate`、合法帧被一律拒绝的实现缺陷；主循环修复
  （owner 创建与 data 赋值前移）后复验通过，五预设全绿。
- **未覆盖项**：真实模型后端行为不在 M3 声明范围（RULE-08）；`@vN`
  注册表接线、schema 1.1 wire 证据与解析闭环随 `M3-03`。

2026-09-20：`M3-03` 完成（分支 `feat/m3-03-visual-resolution-loop`，基于
`M3-02` 合入后的 `master`）。schema 1.1 与解析闭环落地。

- **desktop 层**：`DesktopObservation` 新增结构字段 `visual_snapshot`，
  schema tag 升 `"1.1"`（DEC-016 决策 2 声明的加法演进；
  `visual_snapshot_ref` 落实 scope 句柄语义）；`ObservationAssembler` 增
  visual 组件（默认关、不请求零行为变化；fail closed：未绑定 registry →
  `unsupported_platform`，registry 无已发布代际 → `not_found`；捕获序
  active_window → semantic → visual → pointer → environment）。新增
  `element_target_executor`（DEC-005 顺序 + DEC-016 决策 6 的
  visual/spatial/raw 桩转正）：accessibility（masked target，仅交出该环
  提示）→ visual reference（`@vN`）→ visual hint（`ocr_text` /
  `template_id` 精确匹配活动快照条目，ocr 先于 template）→ spatial
  （`@v` 锚走 registry、`@e` 锚走调用方 semantic context，加偏移）→ raw；
  环未解析记 `ResolutionStep` 可追溯降级事件并 fall-through，环命中即决定
  不再降级，accessibility 命中但动作失败（如 unsupported_element）直接
  报错不降级。`click(@vN)` 闭环 = 解析 bounds 中心 → `pointer_move` →
  left press → left release，逐步观察取消；release 阶段被取消时以全新
  token best-effort release 收敛输入状态（设计文档第 13 节不得留按钮
  按住）。依赖可空，各自环 fail closed。
- **integration 层**：`visual_observation_mapper`（pinned 唯一边界层）——
  mirador 融合快照到 `desktop::VisualSnapshot` 的全量确定性映射：来源位
  优先级 kOcr → kTemplate（`template_id` = label，label 空则顺延）→
  kDetector → 其余（含纯 kExternal，DEC-016 管线不产生）→ kGeometry；
  bounds 逐分量 lround（half away from zero）、confidence 转 double；
  ref/scope 留空由注册表发布赋值。`publish_visual_snapshot` 一步完成
  映射 + 注册表发布（重编号、scope、整体替换、超预算整体拒绝）。
- **Permission 面复核（DEC-016 决策 6 确认）**：视觉分析为只读感知，未
  新增 Capability 词表（`runtime/permission/capability.hpp` 枚举与词表
  不变）；解析产出的指针动作复用既有 `input.inject` 判定；执行器位于
  runtime 权限门之后，调用面随 `M3-05` 接线复核。
- **wire 面处置**：mirage 本地 IPC 协议 v1 不承载 DesktopObservation
  载荷（UI 事件/请求面），本次无 golden vectors 变更；pinned mira 绑定层
  observe 对 visual 组件的投影随 `M3-05` 接线落地并按 DEC-012 纪律取证。
- **测试证据**（Independent-Verification-Agent 独立执行）：更新
  `tests/desktop/desktop_observation_test.cpp`（schema 1.1 与 visual 默认
  空，15 checks）、扩展 `tests/desktop/observation_assembler_test.cpp`
  （visual 组件六场景，199 checks）、新增
  `tests/desktop/element_target_executor_test.cpp`（22 场景 228 checks：
  `@vN` click 闭环与奇数尺寸中心、陈旧 `@v` 零注入、ocr/template 精确
  匹配与优先序、降级 steps 可追溯、accessibility 动作失败不降级、
  spatial `@v`/`@e` 锚、raw、无提示副作用前拒绝、三段取消含 release
  收敛、依赖缺失 fail closed、ring 名稳定）与
  `tests/integration/visual_observation_loop_test.cpp`（5 场景 114
  checks：来源位映射矩阵与复合位优先、.5 half-away-from-zero 取整、
  确定性等映射、发布编号/scope/超预算整体拒绝、真 executor + fake
  backend 会话融合 → 发布 → observe 捕获 → `click(@vN)` /
  `click(ocr_text)` 命中融合区域中心的端到端闭环、换代后 observe 见新
  scope）；
  `debug` / `release` / `asan` / `ubsan` / `tsan`（`setarch -R`）五预设
  configure + build + ctest 29/29 通过、0 skip；asan / ubsan / tsan
  报告 0；`mirage-format-check` 通过；`mirage-boundary-check` 通过
  （36 头 0 命中）。
- **未覆盖项**：执行器 structural 提示与 set_text 组合路径、raw `{0,0}`
  与未设置的歧义（契约按未设置处理）未单独测试；headless X 拓扑 e2e
  （采集 → 分析 → `@vN` → 解析 → 注入 → 再观察）随 `M3-05`；真实模型
  后端行为不在 M3 声明范围（RULE-08）。

2026-09-20：`M3-04` 完成（分支 `feat/m3-04-visual-cache-geometry`，基于
`M3-03` 合入后的 `master`）。Visual Cache 与几何提议按 DEC-016 修订
（同日）落线。

- **模板索引承载**（`integration/mirador`）：`visual_template_index`
  （`VisualTemplateIndexConfig` / `VisualTemplateHit` /
  `VisualTemplateIndex`）——每源一实例包着 pinned `mirador::VisualIndex`
  三层证据（kExactContent / kPerceptualHash / kTemplate）；`enroll` 经
  mirador `make_visual_patch_fingerprint`（与索引共享 thumb_side，预算
  显式 kBudgetExceeded 不截断），identity 容量 fail closed（超限拒绝，
  不驱逐），索引字节预算驱逐后 identity 表逐 enroll 对账（探测命中必可
  映射回身份）；`probe` 执行调用方"清晰胜者"复用策略（首候选达阈值且无
  并列才命中，弱/歧义命中交还真实识别，上游 icon tour 纪律）；probe 非
  const（mirador query 层级晋升），与会话同 blocking worker 串行上下文。
- **会话管线扩展**（`VisualSessionHost`）：请求新增模板登记（帧内裁剪）、
  模板探测（检测区域裁剪 → 指纹 → 索引 → 命中作 `add_template` 证据）、
  几何阶段（`convert_frame` 转 kGray8（预算显式）→ 一方
  `SegmentGrowingLineDetector` → `filter_segments` → `merge_collinear` →
  `propose_regions`，提议入 `geometry_proposals`）与 `cache_stats`（frame
  / result cache 与模板索引占用，worker 上读取）。阶段序：登记 → 变化门控
  → 几何 → OCR → 检测（`detector_roi_from_geometry` 时逐提议
  context_bounds∩frame 作为 ROI，各 ROI 独立能力缓存键）→ 探测 → 融合。
  融合后富集：kTemplate 区域 label 盖写登记身份（最低 evidence id 决定；
  身份缺失清 label 降级 geometry 形态）——template_id 经 DEC-016 映射契约
  到达感知面的唯一通道（mirador `set_label` 不认模板证据）。几何提议经
  无语义 `ExternalRegion`（tight_bounds + closure_score，role/text/
  interactive 全空）进融合，映射为 geometry 形态条目（DEC-016 修订第 2
  条：kExternal 位仅可来自几何管线，accessibility 路径仍不产生）。像素级
  阶段（登记/探测/几何）限 adapter k0 采集形态，越界提交准入即拒
  （kRejected）；取消先于登记副作用；失败结算保留已完成阶段产物。
- **Permission 面不变**：几何/模板均为只读感知与解析提示，未新增
  Capability 词表；`template_id` 解析仍复用 `M3-03` visual hint 环的精确
  匹配（本工作项使其有源可匹配）。
- **测试证据**（Independent-Verification-Agent 独立执行）：新增
  `tests/integration/visual_template_index_test.cpp`（15 场景 146 checks：
  create 正负向、指纹确定性、清晰胜者策略与歧义、阈值边界、erase/替换、
  容量 fail closed、字节预算驱逐对账——条目字节精确等于 pinned 契约
  `32²+64=1088`）与 `tests/integration/visual_cache_geometry_test.cpp`
  （11 场景 218 checks：登记→探测→富集→映射→publish→`click(template_id)`
  命中 kVisualHint 的完整闭环、几何提议确定性 + closure_score、
  ROI-split 检测调用数 == 提议数且坐标全帧正确、kExternal 位融合条目
  bounds/confidence 精确、cache_stats 与预算一致、七类准入拒绝零副作用、
  登记/指纹预算负路径、预取消先于登记、跨帧再定位 template_id 不变且
  中心随检测区域移动）；`debug` / `release` / `asan` / `ubsan` / `tsan`
  （`setarch -R`）五预设 configure + build + ctest 31/31 通过、0 skip；
  `mirage-format-check` 通过；`mirage-boundary-check` 通过（36 头
  0 命中）。
- **未覆盖项**：真实模型后端行为不在 M3 声明范围（RULE-08）；几何提议的
  采集裁剪消费面（`ScreenProvider::capture_roi` 由调用方以
  `geometry_proposals` 驱动）未引入新 e2e（管线侧已验证提议输出）；上游
  `DEC-018` 阶段 2（temporal stability / 融合集成）落地后再评估 External
  载体替换。几何契约消费中未发现需登记 `MIRADOR-*` 台账的缺口。
