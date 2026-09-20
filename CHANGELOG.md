# CHANGELOG

本文件按工程规范第 10.5 节维护，按发布点记录功能、修复、兼容性影响和依赖变化。
发布点与里程碑计划的"建议发布点"一一对应（见 `docs/plans/`）。

## release-beta（2026-09-20）

首个打点发布。对应里程碑 [M1](docs/plans/m1-mira-host.md)、[M1.5](docs/plans/m1.5-ui-parallel-track.md)
与 [M2](docs/plans/m2-desktop-environment.md) 全部完成并通过退出条件复核（M2 复核记录
见里程碑文档验证记录，2026-09-20）。`release-alpha` 未单独打点，其内容随本发布一并
覆盖。

### 功能

- Mira Host 与 Runtime（M1）：Host 生命周期状态机（状态集冻结于 DEC-004）、任务提交 /
  取消 / 排空关闭、Runtime Service 进程形态与 Local IPC（Unix domain socket +
  长度前缀 JSON 帧，协议 v1 冻结于 DEC-007）、事件订阅与 wire schema 事实源
  （DEC-012，golden vectors 双端门禁）、Filesystem / Process Provider（范围 / 预算 /
  取消语义见 DEC-009）、Permission 框架（DEC-010）、本地状态持久化与 Recovery
  （DEC-011）、`mirage` CLI。
- UI 并行轨道（M1.5，DEC-013 harness 优先统一壳）：dev bridge、harness Web 前端壳
  （会话 / 工作流 / 设置），产品化 UI 属 M5。
- Desktop Environment 契约（M2-01，DEC-005 冻结）：Application / Window /
  Accessibility / Screen / Input / Clipboard / Notification 七个 Provider 接口
  （Filesystem / Process 随 M1）、结构化 SemanticSnapshot（节点预算 fail closed +
  确定性文本渲染）、多提示 ElementTarget 与解析顺序契约（reference → semantic →
  structural，visual 保留 M3、raw 显式）、DesktopObservation schema 1.0。
- Linux Backend（M2-02..M2-05，X11 / XWayland 一等路径）：X11 窗口枚举 / 激活 /
  几何与 RandR 显示器、根帧采集（display / window / ROI）、XTest 输入注入、AT-SPI2
  语义树采集与语义动作（activate / set_text，引用注册表随快照整体替换）、剪贴板
  读写（ICCCM / INCR 增量双向）、应用发现 / 启动 / 终止（GIO）、通知投递（GDBus）。
  平台能力缺失时编译 stub、访问器 null fail closed，capabilities 如实收缩（DEC-015）。
- Observation 组装与 Agent 观察面（M2-06）：`ObservationAssembler` 按需组装
  DesktopObservation（失败组件显式不完整、不静默）；`integration/mira` observe 能力
  如实上报，SemanticSnapshot 投影为 pinned `UiTreeSnapshot` 并经上游 validator 复验，
  Semantic Snapshot 进入 Mira 观察面。
- Permission Capability 词表随动作面扩展至 11 项（`window.activate` /
  `screen.capture` / `input.inject` / `clipboard.read` / `clipboard.write` /
  `application.launch` / `application.terminate` / `notification.post` 等）。

### 依赖

- pinned `mira` `cf0af75`（内嵌 executor `2ae4fc8`、mbedtls `068ff08`）与 `mirador`
  `50f5349`，见 `dependencies.lock.json`（M2-07 复核通过负向篡改校验）。
- `MIRA_WITH_MBEDTLS=OFF` 冻结为默认值（DEC-002 变更记录：运行面暂无 TLS 传输；
  接入真实模型网关时按触发条件重开）。
- Linux 系统依赖与事件循环接入决策见 DEC-015（D-Bus 栈选型 libatspi + GDBus、
  剪贴板机会性 pump、可选依赖 stub 策略）。

### 兼容性影响

- 首个发布，无向后兼容义务。IPC 协议 v1 与 DesktopObservation schema 1.0 自本发布
  起为冻结契约，后续变更按 DEC-007 / DEC-005 的变更记录约束执行。

### 已知限制

- `semantic_snapshot` 对真实应用窗口的正路径全链路（标题→AT-SPI 应用映射→DFS→`@eN`）
  已由线协议精确 fixture 覆盖（Xvfb + 私有 D-Bus 拓扑），但未在真实桌面会话执行过
  （复核时会话焦点在 Wayland 原生表面、`:0` 无 X 客户端；补跑条件见 M2 里程碑
  验证记录）。EWMH activate、XTest 注入、剪贴板、屏幕采集等侵入性动作未对真实
  用户会话执行，需专用显示环境取证。
- Wayland 原生（非 XWayland）截图与输入不做一等支持：能力缺失 fail closed 并如实
  上报 capabilities；Portal 路径按需逐项评估。
- Mirador 视觉集成与 Visual Reference（M3）、Windows Backend（M4）、产品化 GUI
  （M5）尚未开始；`VisualHint` 契约字段保留、解析器缺位 fail closed。
- 绑定 `foreground` 组件在 optional structure 降级时 `package_name` 为空，Mira 侧
  跨仓库消费语义未验证。
- 本发布不声明任何未通过目标平台或基准验证的实时性、性能或跨平台保证
  （工程规范第 10.5 节）。
