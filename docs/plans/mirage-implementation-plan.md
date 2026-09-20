# Mirage 实施总计划

> 状态：Planned
> 版本：0.1
> 负责人：Mirage 维护者
> 依据：[《Mirage：Linux - Windows 桌面端设计方案》](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)（下称"设计文档"）
> 更新日期：2026-09-20

## 当前状态

项目骨架与依赖接线已初始化（pinned `mira` / `mirador`，见
[DEC-001](../decisions/DEC-001-dependency-pinning.md)）；M1 里程碑计划已建立，见
[M1：Mira Host 与基础 Runtime](m1-mira-host.md)。M1 进行中：`M1-01` 项目初始化、
`M1-02` Mira Host 生命周期（状态集冻结见
[DEC-004](../decisions/DEC-004-mira-host-status-set.md)）、`M1-03` Desktop Environment
绑定适配器（绑定与参考 Provider 边界见
[DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)）、
`M1-04` Runtime Service 与 Local IPC（IPC 机制定案见
[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)）、`M1-05`
Filesystem / Process Provider 收紧（范围/预算/取消语义见
[DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)）、`M1-06`
Desktop Permission 框架雏形（Capability 判定与确认挂点见
[DEC-010](../decisions/DEC-010-m1-permission-framework.md)）与 `M1-07` 持久化骨架
（本地配置与 Runtime Recovery State，见
[DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)）已完成；
2026-09-16 里程碑退出条件复核通过（5/5，验证记录见
[M1：Mira Host 与基础 Runtime](m1-mira-host.md)），M1 Completed。M2 里程碑计划已
建立，见 [M2：Desktop Environment 核心 Provider 与 Linux
Backend](m2-desktop-environment.md)；`M2-01`（核心 Provider 契约与
DesktopObservation schema v1.0，
[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）、`M2-02`（X11
骨架闭环，[DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)）、
`M2-03`（AT-SPI2 AccessibilityProvider）、`M2-04`（Input / Clipboard 完整 +
Permission 词表扩展）、`M2-05`（Application / Notification Provider 与
ScreenProvider 完整）与 `M2-06`（Observation 组装与 runtime 接线：
DesktopObservation 按需组装器、绑定 observe 面如实上报与 Semantic Snapshot
进入 Agent 观察面、DEC-002 Mbed TLS 默认值复核冻结）已完成；2026-09-20 M2 里程碑
退出条件复核通过（6/6，验证记录见
[M2：Desktop Environment 核心 Provider 与 Linux
Backend](m2-desktop-environment.md)），M2 Completed。M3（Mirador 视觉集成）里程碑
计划待建立。UI 并行轨道
[M1.5](m1.5-ui-parallel-track.md) 已完成（wire schema 事实源
[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、事件订阅、
dev bridge、harness 前端壳）。

## 交付边界

- [ ] `SCOPE-01` Mira Host：在 Mirage Runtime 中承载 Mira 实例，绑定 Desktop
      Environment，向产品层转发 Agent 事件（设计文档第 11 节）。
- [ ] `SCOPE-02` Desktop Environment：Application / Window / Accessibility / Screen /
      Input / Clipboard / Filesystem / Process / Notification 的统一 Provider 接口
      （设计文档第 5 节）。
- [ ] `SCOPE-03` Desktop Observation 与 Semantic Snapshot：结构化桌面状态、Element
      Reference 与按需感知（设计文档第 6、7 节）。
- [ ] `SCOPE-04` Mirador 视觉集成：截图采集对接 Mirador OCR / 检测 / 几何 / Visual
      Cache，形成 Visual Reference（设计文档第 8 节）。
- [ ] `SCOPE-05` Windows Backend：UI Automation + Win32 + 系统截图输入的完整平台实现，
      与 Linux 后端共享同一 Desktop Environment API（设计文档第 10 节）。
- [ ] `SCOPE-06` Desktop Product：Agent Workspace、Task / Workflow / Execution 视图、
      Desktop Overlay、权限管理、后台 Runtime Service 与 CLI（设计文档第 12-15 节）。
- [ ] `SCOPE-07` Linux Backend：AT-SPI2 + X11 + Wayland + XDG Desktop Portal
      （设计文档第 10 节）。

明确不包含（由 pinned Mira 提供，Mirage 仅适配）：

- Agent Harness 内核：Observe -> Reason -> Plan -> Act -> Verify 循环、Context、
  Memory、Tool/MCP 框架、Workflow / Subagent 执行模型。
- Agent Memory、Workflow 数据模型的持久化（Mirage 只持久化自身产品状态，设计文档
  第 16 节）。
- 模型推理与视觉模型运行时（由 mirador 的 backend 契约和其 integrations 承载）。

## 不可破坏的架构约束

- [ ] `RULE-01` 依赖方向固定为 `apps/ui -> runtime -> desktop -> platform`；
      `integration` 是 pinned 依赖的唯一边界层，其余层不得直接包含 mira / mirador /
      executor 头文件（公共接口不暴露第三方类型）。
- [ ] `RULE-02` 平台细节（UIA、AT-SPI2、X11、Wayland、Win32、Portal）只存在于
      `platform/`，Desktop Environment 接口与 Observation / Action 语义在两个平台上
      保持一致。
- [ ] `RULE-03` 所有并发任务、定时、取消和生命周期管理必须经 Executor（随 pinned mira
      引入）；自研代码禁止 `std::thread`、`std::async`、自建线程池与 detached worker。
- [ ] `RULE-04` Task / Session / Desktop Action 必须有稳定 ID、取消上下文和生命周期
      所有者；终态幂等，迟到结果不得复活已取消任务。
- [ ] `RULE-05` 键鼠输入、进程执行、文件写入、剪贴板等有真实副作用的动作必须经过
      Desktop Permission 判定（含用户确认路径），并与 Agent Trace 关联（设计文档
      第 15 节）。
- [ ] `RULE-06` `third_party/` 只以 pinned submodule 指针变更，必须与
      `dependencies.lock.json` 同步；configure 阶段校验失败即构建失败。
- [ ] `RULE-07` 队列、缓存和并发 operation 必须有容量或预算上限；背压、拒绝、超时和
      取消必须转化为明确结果与事件。
- [ ] `RULE-08` 声明性能、实时性或跨平台支持前必须留下符合工程规范第 7 节的验证证据。

## Executor 并发边界

Executor 由 pinned `third_party/mira/third_party/executor` 提供，能力路由见根
`AGENTS.md` 与工程规范第 9.3 节。

- [ ] `EXEC-01` Runtime Service 是每进程唯一的 Executor owner：初始化、任务准入、
      排空与 `shutdown(true)` 顺序由其负责；GUI、CLI、Tray 均经 IPC 与之交互，不得
      自行创建 Executor。
- [ ] `EXEC-02` Mira Host 的任务提交、Agent 事件流转发使用 Executor 公开能力与
      `executor::comm` 组件；事件到 UI 的分发映射为 `Topic` / `LatestMailbox`，不得
      自建广播队列。
- [ ] `EXEC-03` Platform Backend 的平台事件循环（UIA/AT-SPI2/Win32 消息循环）按
      Executor 外部事件循环互操作边界接入；线程亲和的平台调用封装在 Backend 内。
- [ ] `EXEC-04` 桌面输入注入、截图、OCR 请求等阻塞或耗时操作使用 blocking worker /
      timer 能力承载；输入注入的取消路径必须可解除阻塞。
- [ ] `EXEC-05` 依赖（mira / mirador，含其内嵌 executor）能力缺口按工程规范第 9.4 节
      登记 `docs/dependency_feedback/ledger.md` 并在实现中引用编号。

## 里程碑索引

| 里程碑 | 内容 | 建议发布点 | 状态 | 依赖 |
| --- | --- | --- | --- | --- |
| [M1](m1-mira-host.md) | Mira Host、Runtime Service + IPC、Filesystem/Process Provider、CLI | `release-alpha` | Completed | - |
| [M1.5](m1.5-ui-parallel-track.md) | UI 并行轨道：wire schema 事实源、IPC 事件订阅、dev bridge、前端（浏览器形态，按 DEC-013 harness 优先统一壳组织：会话/工作流/设置 + 统一壳骨架） | -（随开发线交付，产品化 UI 属 M5） | Completed | M1 |
| [M2](m2-desktop-environment.md) | Desktop Environment 核心 Provider + Linux Backend、Semantic Snapshot、Element Reference | `release-beta` | Completed | M1 |
| M3 | Mirador 集成：OCR / 检测 / 几何 / Visual Cache、Visual Reference | `release-gamma` | Planned | M2 |
| M4 | Windows Backend（UIA / Win32 / Capture / Input） | `release-delta` | Planned | M2 |
| M5 | Desktop Product：Workspace、Workflow UI、Execution Trace、Overlay、权限 | `release-epsilon` | Planned | M3、M4 |

拆分与合并顺序：先契约后实现（Desktop Environment 接口先于任何 Backend）、先骨架后
功能（每个 Backend 先以最小动作打通 Observation -> Action -> Observation 闭环）、
先 SPI/假实现后真实依赖（视觉后端先用 fake/identity backend 验证集成层）。

## 尚未冻结的决策（暂定默认值）

| 编号 | 主题 | 暂定默认值 | 负责人 | 最迟冻结 |
| --- | --- | --- | --- | --- |
| DEC-002 | Mira TLS 通道适配器 | 已定案（[DEC-002](../decisions/DEC-002-build-test-baseline.md) 变更记录）：`MIRA_WITH_MBEDTLS=OFF` 于 M2 复核冻结（运行面无模型网关/TLS 传输，mbedtls pinned 子模块在位可随时重开）；接入真实模型网关且通道要求 Mbed TLS 时按记录触发条件重开 | Mirage 维护者 | M2（已冻结） |
| DEC-004 | Mira Host 状态集 | 已定案（[DEC-004](../decisions/DEC-004-mira-host-status-set.md)）：五态 `Stopped/Starting/Running/Stopping/Failed`，M1 冻结并写入设计文档第 11.1 节 | Mirage 维护者 | M1（已冻结） |
| DEC-005 | DesktopObservation 契约 | 已定案（[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）：schema 1.0 字段集、结构化 SemanticSnapshot（预算 + 确定性渲染）、多提示 ElementTarget 与解析顺序契约（reference → accessibility → Mirador 视觉（M3）→ VLM 显式），M2-01 冻结 | Mirage 维护者 | M2（已冻结） |
| DEC-006 | UI 技术路线与分发打包 | 已定案（[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)）：Web 前端 + 嵌入式渲染壳（暂定 CEF）独立进程；`.deb` / Windows `exe` 安装包。壳选型与更新通道为暂定默认值，M3 冻结 | Mirage 维护者 | M3 |
| DEC-007 | Local IPC 机制 | 已定案（[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)）：Unix domain socket（Linux）/ 命名管道（Windows，M4），长度前缀 + JSON 帧格式，协议 v1 请求面，`apps/service` 进程形态；传输与帧格式自 M1-04 冻结 | Mirage 维护者 | M1（已冻结） |
| DEC-012 | IPC 事件订阅与 wire schema 事实源 | 已接受（2026-09-16 评审通过，[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)）：协议 v1 附加事件帧 + 订阅 op（版本号不递增），[mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md) 为契约事实源（M1.5-01 落地，golden vectors 双端门禁），事件分发经 `executor::comm` 承载 | Mirage 维护者 | M1.5 |
| DEC-013 | 前端信息架构与设计规范 | 已定案（[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)）：harness 优先统一壳（会话默认落地、workflow 一级入口、设置八类），workflow 编辑器为对齐 mira Workflow IR v1 的结构化步骤序列；规范见 [《Mirage 前端设计规范与信息架构》](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)，M1.5 落地骨架、M5 验收基线 | Mirage 维护者 | M1.5 |

## 跨里程碑通用完成定义

- [ ] `DOD-01` 实现落在 `RULE-01` 约定的层内；公开头文件不含第三方类型。
- [ ] `DOD-02` 新增并发路径受 Executor 管理，取消与 shutdown 闭合，失败可见。
- [ ] `DOD-03` `debug`、`release`、`asan`、`ubsan` 预设构建通过；涉及跨上下文状态时
      `tsan` 通过；`ctest` 无 skip 冒充成功。
- [ ] `DOD-04` 新契约有对应单元/集成测试与负向用例（拒绝、越界、取消、超时）。
- [ ] `DOD-05` 计划状态、设计文档、决策记录与验证证据同步更新（工程规范第 8 节矩阵）。
- [ ] `DOD-06` Commit / MR 符合工程规范第 10 节；`dependencies.lock.json` 与
      submodule 指针一致。

## 延后项与触发条件

- `POST-01` macOS / 移动端支持：仅当出现明确的桌面端之外的宿主需求时立项；设计文档
  范围限定 Linux / Windows。
- `POST-02` 多实例 Runtime Service（多用户 / 多会话隔离）：待单实例形态在 M1-M2 验证
  后，依据真实隔离需求立项。
- `POST-03` 远程 Desktop Environment（SSH / 容器内桌面）：待本地 Backend 稳定且出现
  真实远程使用场景时立项；接口设计需提前保留 Provider 远程化的可能。
