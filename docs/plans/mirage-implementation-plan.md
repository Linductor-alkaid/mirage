# Mirage 实施总计划

> 状态：Planned
> 版本：0.1
> 负责人：Mirage 维护者
> 依据：[《Mirage：Linux - Windows 桌面端设计方案》](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)（下称"设计文档"）
> 更新日期：2026-10-06（M6-21 模型服务编辑）

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
计划已建立，见 [M3：Mirador 视觉集成](m3-mirador-integration.md)；pinned
mirador 已升级至 v0.3.0（`6fa92ec`，几何区域提议契约经上游 DEC-018 阶段 1
转正），审计记录见[依赖升级审计](../supply-chain/dependency-upgrade-audit.md)；
`M3-01`（视觉集成契约 DEC-016）、`M3-02`（集成层适配器）、`M3-03`（Visual
Observation 与解析闭环）、`M3-04`（Visual Cache 与几何提议）、`M3-05`
（runtime 接线与端到端闭环）与 `M3-06`（DEC-006 壳选型 PoC 与冻结：CEF 冻结，
取证见[壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)）已完成；
2026-09-21 M3 里程碑退出条件复核通过（8/8，验证记录见
[M3：Mirador 视觉集成](m3-mirador-integration.md)），M3 Completed。
UI 并行轨道
[M1.5](m1.5-ui-parallel-track.md) 已完成（wire schema 事实源
[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、事件订阅、
dev bridge、harness 前端壳）。M4（Windows Backend）里程碑计划已建立，见
[M4：Windows Backend](m4-windows-backend.md)；`M4-01`（Windows Backend 骨架闭环：
Win32 窗口 / 采集 / 输入，工具链双门禁决策见
[DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)）、
`M4-02`（AccessibilityProvider：UIA 语义树 → SemanticSnapshot 与
ElementTarget 解析 / 语义动作）、`M4-03`（ClipboardProvider（Win32）与
ProcessProvider 的 CreateProcess 有界承载）、`M4-04`（ApplicationProvider：
开始菜单快捷方式发现、启动 / 运行态 / TERM 等价协作式终止）与 `M4-05`
（NotificationProvider：承载决策
[DEC-018](../decisions/DEC-018-windows-notification-carrier.md)——
`Shell_NotifyIcon` 气球/横幅 + open() 探测 fail closed 降级，toast 留 M5
重议）、`M4-06`（产品进程 Windows 化：命名管道 IPC、Windows 持久化、
service / CLI 进程形态、全树 MSVC 构建）与 `M4-07`（Windows 端到端闭环：
observe → action → observe 经真实 Win32 / UIA 前端与
`MiraEnvironmentBinding` 绑定取证；里程碑退出条件逐项复核）已完成；
M4 里程碑 Completed，退出复核通过（全树 MinGW 交叉复验随台账
`MIRA-20260922-001` 缺口关闭挂账）。M5（Desktop Product）里程碑计划已建立，
见 [M5：Desktop Product](m5-desktop-product.md)，状态 In Progress；`M5-01`
（前端工具链定案与 CEF 二进制锁定：npm / Vite / React 19 / vitest 复核确认、
ESLint 新定选、`dependencies.lock.json` schema v2 工件 pin 与 npm 树哈希门禁、
沙箱与 GPU 正式方案，DEC-006 2026-09-26 修订）、`M5-02`（CEF 产品壳骨架与
壳内 IPC 传输路径：锁定工件 configure 期消费门禁、`apps/desktop` bootstrap
进程形态壳骨架、`runtime/ipc` SessionClient、壳内 UI 对真实 `mirage-service`
的 hello / 订阅 / 任务往返验收，DEC-019）、`M5-03`（权限异步确认面：DEC-020
替换 M1 同步确认挂点——`AsyncConfirmationHub` 有界等待 + 超时/容量/取消
fail closed，协议 v1 附加扩展 `permission.request` 事件与
`permission.respond` / `permission.list` 请求面，golden vectors 双端门禁
meta.version 3，任务驱动器取消探测接线）与 `M5-04`（会话与消息契约面：
DEC-021——`session.list` / `session.open` / `session.history` 请求面与
`session.updated` / `session.message` / `session.turn` / `session.output`
事件集，`task.submit` 会话绑定，pinned 会话投影经 `SessionJournal` 承载，
`MiraHost` 会话面开启 DEC-008 迁移路径第一步，golden vectors 双端门禁
meta.version 4）已完成。2026-09-26 双依赖前滚（mira `1348515`、mirador
`fb0dc3f`，[升级审计](../supply-chain/dependency-upgrade-audit.md)）交付上
游工具引用 / Skill 发布 / MCP 接纳与 mirador 目标跟踪 Experimental 等能力，
消费路由见 [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)：
`M5-05` 绑定升级后的 pinned 工作流 / 工具契约面，Tools / MCP 产品面与
mirador 跟踪消费分别挂账 POST-05 / POST-04。`M5-05` 第一轮（工作流契约面，
[DEC-023](../decisions/DEC-023-workflow-contract-face.md)）已完成：
`workflow.list` / `workflow.save` / `workflow.publish` / `workflow.delete` /
`workflow.atom.catalog` / `workflow.runs` / `workflow.run` / `workflow.cancel`
请求面与 `workflow.run_updated` 事件（golden vectors 双端门禁 meta.version 5），
pinned `WorkflowRuntime` 经 `MiraHost` 工作流承载面服务化托管，运行监控事件经
`WorkflowEventBridge` 从 pinned 事件词表转译；编辑器完整版接真实面与桌面原子
工具注册为该工作项第二轮（均已完成）。`M5-06` 第一轮（会话页接真实面，
[DEC-025](../decisions/DEC-025-session-page-productization.md)）已完成：
UI 侧消费 `M5-04` 的 `session.*` 契约面（wire 零变更），会话列表 / 消息流 /
观察台全部接契约路径，模拟域退出会话页，对话模式在模型循环接入前如实降级；
第二轮观察面协议扩展批次（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）
已完成：`desktop.observe` 按需观察请求面（语义快照投影 / 视觉状态 /
`visual_snapshot_ref` 进 UI 观察面，M3 非目标兑现）与 `workflow.get` 定义读
取面（编辑器跨会话编辑）随 golden vectors v5 → v6 上线，步级运行事件与会话
管理面评估后挂账（PR #58 CI 全绿取证已补录里程碑计划）；第三增量会话管理面
`session.close`（DEC-026 挂账②兑现）已完成：pinned `close_session` 承载 +
服务注册表条目移除 + 主会话 `invalid_state` 守卫 + 会话页删除入口，
`session.updated` 增补关闭发布点，golden vectors v6 → v7；第四增量对话模式
真实化（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)，DEC-025
挂账③兑现，DEC-008 迁移路径第二步纯对话形态）已完成：模型层装配（pinned
ModelProfile/Router/Provider/SocketTransport/Gateway + SecretRef 环境变量
解析 + TLS 通道按需挂接）与 `session.chat` 对话面（异步 turn +
session.chat_updated 生命周期 + session.chat.history 快照），Composer 双模
式信息架构完整兑现，golden vectors v7 → v8——`M5-06` 工作项收口；设置-模
型类目（Profile 管理面、凭据承载升级）属 M5-08 联动范围。`M5-07` 批准中
心与权限管理产品化（[DEC-028](../decisions/DEC-028-permission-policy-face.md)）
已完成：批准中心（Overlays / WallDisplay）接 DEC-020 异步确认面
（permission.list/respond/permission.request），设置页权限矩阵替换为
DEC-010 十一能力真实矩阵（policy.get 事实源 / policy.set 全量应用立即生
效 + 合并文档持久化），hello `policy` 能力位，golden vectors v8 → v9，
DEC-011 默认拾取与写回翻转兑现，默认策略收紧留观 M5-08。`M5-08` 设置全
量与产品化规模复核已完成：LocalSettings 扩展 model / runtime 两块（设置
-模型类目配置输入与队列容量、连接规模可调），会话历史与对话线程跨重启
持久化落地（session-state.json，DEC-021 挂账①兑现），POSIX 端点对端凭
据校验（SO_PEERCRED）与 M1 遗留守护纪律修复（BUG-20260916-001）落地，
会话重命名产品别名（DEC-026 挂账⑤）落地。`M5-09` Desktop Overlay 已完成
（[DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md) 承载机制随实现
定案并登记）：Overlay 由 `mirage-service` 进程承载（与 GUI 生命周期解耦）、
平台承载落 Platform Backend 私有前端（Windows 双分层窗口：置顶 / 每像素
alpha / 跨进程点击穿透 + 确认入口交互窗；Linux X11 shape 覆盖层先例，无合
成器依赖，Wayland 原生会话如实降级为无载体）；UI 面兑现目标高亮、即将执行
操作提示、确认入口与 Observation 调试观察面（`--overlay on|debug`，默认
off）；与任务执行流的事件联动经服务内部事件织物（事件订阅横幅 +
`AtomOverlayFeed` 动作缝 + 确认面镜像，wire 零变更，DEC-020 应答路径扩展
为 IPC 客户端或 Overlay 确认入口 first-response-wins）。`M5-10` Tray 进程
形态已完成（[DEC-030](../decisions/DEC-030-tray-carrier-and-task-pause-face.md)
承载机制与任务暂停/恢复面随实现定案并登记）：`apps/tray` 单 Executor owner
经 Local IPC 订阅运行状态（EXEC-02），任务暂停/恢复经 `task.pause` /
`task.resume` 附加扩展（v10；pinned pause 家族投影 + 驱动操作边界驻留），
快速进入 Mirage 经兄弟壳二进制发现拉起；Windows 承载沿用 DEC-018 通知区面
的产品化菜单扩展，Linux 以 StatusNotifierItem + 最小 dbusmenu（纯 gio）并
在无指示器宿主会话如实降级为无载体。`M5-11` 打包与更新通道已完成
（[DEC-031](../decisions/DEC-031-windows-installer-generator.md) NSIS
定案、DEC-018 toast 重议不重开）：Linux .deb（CPack DEB）+ GPG 签名 apt
仓库脚本（fail closed）+ Windows NSIS 安装器 + Authenticode 签名脚本
（证书注入 fail closed）+ 更新清单 ed25519 工具链与五步演练。`M5-12`
Windows 应用内更新器已完成
（[DEC-032](../decisions/DEC-032-update-client-fail-closed.md)）：清单
严格解析 + ed25519 验签（libcrypto EVP，OpenSSL 缺席构建整体 fail
closed）+ 原子切换与逆序回滚 + HTTP fetch（信任锚为清单签名与
Authenticode 双层，非 TLS）+ `mirage update check/apply` CLI；差分维持
全量优先不承诺；更新器零私钥（keyring 隔离既定）。`M5-11` 打包与更新通道已完成
（[DEC-031](../decisions/DEC-031-windows-installer-generator.md) 安装器
生成器定案 NSIS、DEC-018 toast 重议结论不重开）：Linux `.deb` 由 CPack
DEB 产出（四产品进程 + postinst/prerm 尽力而为脚本），apt 仓库 GPG
签名/验签脚本 fail closed（签名 key 发布机隔离，本机临时演练 key 演练
全过）；Windows NSIS 安装器脚本 + Authenticode 签名脚本（证书注入 fail
closed，发布机取证补跑）；更新清单 ed25519 签名生成与 fail-closed 验签
脚本（本地五步演练：签验/篡改拒/恢复/回滚收敛）。

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
| [M3](m3-mirador-integration.md) | Mirador 集成：OCR / 检测 / 几何 / Visual Cache、Visual Reference | `release-gamma` | Completed | M2 |
| [M4](m4-windows-backend.md) | Windows Backend（UIA / Win32 / Capture / Input） | `release-delta` | Completed | M2 |
| [M5](m5-desktop-product.md) | Desktop Product：Workspace、Workflow UI、Execution Trace、Overlay、权限 | `release-epsilon` | In Progress | M3、M4 |

拆分与合并顺序：先契约后实现（Desktop Environment 接口先于任何 Backend）、先骨架后
功能（每个 Backend 先以最小动作打通 Observation -> Action -> Observation 闭环）、
先 SPI/假实现后真实依赖（视觉后端先用 fake/identity backend 验证集成层）。

## 尚未冻结的决策（暂定默认值）

| 编号 | 主题 | 暂定默认值 | 负责人 | 最迟冻结 |
| --- | --- | --- | --- | --- |
| DEC-002 | Mira TLS 通道适配器 | 已定案（[DEC-002](../decisions/DEC-002-build-test-baseline.md) 变更记录）：`MIRA_WITH_MBEDTLS=OFF` 于 M2 复核冻结（运行面无模型网关/TLS 传输，mbedtls pinned 子模块在位可随时重开）；接入真实模型网关且通道要求 Mbed TLS 时按记录触发条件重开 | Mirage 维护者 | M2（已冻结） |
| DEC-004 | Mira Host 状态集 | 已定案（[DEC-004](../decisions/DEC-004-mira-host-status-set.md)）：五态 `Stopped/Starting/Running/Stopping/Failed`，M1 冻结并写入设计文档第 11.1 节 | Mirage 维护者 | M1（已冻结） |
| DEC-005 | DesktopObservation 契约 | 已定案（[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）：schema 1.0 字段集、结构化 SemanticSnapshot（预算 + 确定性渲染）、多提示 ElementTarget 与解析顺序契约（reference → accessibility → Mirador 视觉（M3）→ VLM 显式），M2-01 冻结 | Mirage 维护者 | M2（已冻结） |
| DEC-006 | UI 技术路线与分发打包 | 已定案并冻结（[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md) 2026-09-21 修订节）：Web 前端 + 嵌入式渲染壳冻结为 CEF（PoC 取证见[壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)，Electron / Tauri 否决理由封存）；`.deb` / Windows `exe` 安装包；更新通道冻结为 Linux GPG 签名 apt 仓库唯一路径、Windows 双层签名应用内更新器（全量优先，差分 M5 评估） | Mirage 维护者 | M3（已冻结） |
| DEC-007 | Local IPC 机制 | 已定案（[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)）：Unix domain socket（Linux）/ 命名管道（Windows，M4），长度前缀 + JSON 帧格式，协议 v1 请求面，`apps/service` 进程形态；传输与帧格式自 M1-04 冻结 | Mirage 维护者 | M1（已冻结） |
| DEC-012 | IPC 事件订阅与 wire schema 事实源 | 已接受（2026-09-16 评审通过，[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)）：协议 v1 附加事件帧 + 订阅 op（版本号不递增），[mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md) 为契约事实源（M1.5-01 落地，golden vectors 双端门禁），事件分发经 `executor::comm` 承载 | Mirage 维护者 | M1.5 |
| DEC-013 | 前端信息架构与设计规范 | 已定案（[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)）：harness 优先统一壳（会话默认落地、workflow 一级入口、设置八类），workflow 编辑器为对齐 mira Workflow IR v1 的结构化步骤序列；规范见 [《Mirage 前端设计规范与信息架构》](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)，M1.5 落地骨架、M5 验收基线 | Mirage 维护者 | M1.5 |
| DEC-016 | Mirador 视觉集成契约 | 已定案（[DEC-016](../decisions/DEC-016-mirador-visual-integration-contract.md)，M3-01）：Visual Reference `@vN` 生命周期对齐 `@eN` 整体替换、`visual_snapshot(_ref)` schema 1.0 → 1.1 加法演进、mirador 结果到感知面的映射边界、一图像源一 session 一 blocking worker 串行承载（EXEC-04）、fake/identity backend 默认验证形态（真实模型后端属集成方） | Mirage 维护者 | M3（已冻结） |
| DEC-022 | 上游能力消费路由（2026-09-26 升级批） | 已定案（[DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)）：`M5-05` 绑定升级后 pinned 工作流 / 工具契约面（TR0/TR2、Skill 执行、Degraded 事件；`atom.catalog` 经工具暴露投影；MCP 连接配置以 DEC-039 为承载边界）；Skill / 工具管理产品面挂账 POST-05；mira 记忆 / 上下文 / 时间策略经 `Mira::core` 隐式消费、无 Mirage 直接面；mirador M7 目标跟踪转正前观察、转正后按 POST-04 立项（DEC-016 加法通道） | Mirage 维护者 | M5-05（绑定面）；POST-04 / POST-05 按各自触发条件 |
| DEC-023 | M5 工作流契约面（协议 v1 扩展）与 WorkflowRuntime 服务承载 | 已定案（[DEC-023](../decisions/DEC-023-workflow-contract-face.md)）：协议 v1 附加扩展 `workflow.*` 八请求 + `workflow.run_updated` 事件 + hello `workflows` 能力位；草稿 = pinned `not_validated` 版本、发布 = `publish_validated` DryRun 门禁、删除 = 产品目录条目移除（pinned 追加式历史不动）；运行监控事件经 `WorkflowEventBridge` 从 pinned 事件转译、快照事实源 `workflow.runs`；TR2 工具引用挂载 / Skill 执行注册 / Degraded 呈现与桌面原子工具注册挂账至第二轮及 POST-05；同内容草稿遮蔽可运行版本的 pinned 语义经台账 `MIRA-20260927-001` 登记 | Mirage 维护者 | `M5-05` 第一轮（已完成）；第二轮（编辑器真实化 + atom 目录人口） |
| DEC-024 | M5 桌面原子工具集（atom.catalog 人口与 ToolCall 执行路径） | 已定案（[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)）：integration/mira `DesktopAtomToolset` 在绑定环境非空 Provider 上构建 pinned `BuiltinToolRegistry`（初始 13 原子，缺席不注册、目录如实反映能力）；宿主 attach 经 `set_tool_registry` 安装，ToolCall 步派发真实桌面动作（副作用原子按 pinned W-02 须带 verification 谓词；DryRun 门禁只规划不派发）；权限经 `AtomPermissionGate` 回调缝接服务共享 `PermissionController`（RULE-05 与任务驱动同门）；`workflow.atom.catalog` 转 exposed view 投影，wire 契约不变（golden 仍 v5）；捕获 / 元素动作 / 指针输入与 TR2 挂载挂账后续轮 | Mirage 维护者 | `M5-05` 第二轮（桌面原子工具注册增量）；编辑器真实化增量随后 |
| DEC-025 | M5 会话页产品化（会话面消费、模拟域退役与对话模式承载） | 已定案（[DEC-025](../decisions/DEC-025-session-page-productization.md)）：会话列表 = `session.list` 快照事实源 + 派生标题（重命名/置顶/删除/导出/fork 无 wire 面不呈现，挂账协议扩展评估）；消息流 = `session.history` 重同步基线 + `session.message` 增量 + 任务快照步骤卡，turn/output 作观察流事件源；对话模式在模型循环接入前如实降级（DEC-008 迁移路径第二步挂账）；模拟域退出会话页；观察台 = 任务快照时间线 + 会话事件观察流（通知面语义）；观察面协议附加扩展（视觉状态呈现，M3 非目标完整兑现，golden v5 → v6）挂账另立增量 | Mirage 维护者 | `M5-06` 第一轮（已完成）；观察面 / 会话管理面 / 对话模式挂账后续增量 |
| DEC-026 | M5 观察面协议附加扩展与工作流定义读取面（golden v6 批次） | 已定案（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）：`desktop.observe` 按需请求 + `ObservationView` 载荷（帧成员恒在、语义快照投影 1024 节点 wire 预算 + `truncated` 显式标记、`visual_snapshot_ref`/`visual_regions` 同现同缺视觉承载，请求即必须 fail closed；无事件形态——M1 驱动形态无观察生产者，驱动器观察投影评估结论）；`workflow.get` + `WorkflowDefinitionView`（head 定义正文由服务侧产品目录保留，pinned 库无正文读取 API 属 W-03 上游设计非缺口），编辑器跨会话编辑解除；hello `observation` 能力位；步级运行事件（无已承诺消费方）与会话管理面（删除=pinned `close_session` 承载挂账 `session.close`、重命名=产品别名 M5-08、导出=客户端投影、fork 不立项）评估后挂账；产品视觉管线接线挂账与 Overlay 联动 | Mirage 维护者 | `M5-06` 第二轮（已完成）；视觉管线点亮 / `session.close` / 步级运行事件挂账后续增量 || DEC-026 | M5 观察面协议附加扩展与工作流定义读取面（golden v6 批次） | 已定案（[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)）：`desktop.observe` 按需请求 + `ObservationView` 载荷（帧成员恒在、语义快照投影 1024 节点 wire 预算 + `truncated` 显式标记、`visual_snapshot_ref`/`visual_regions` 同现同缺视觉承载，请求即必须 fail closed；无事件形态——M1 驱动形态无观察生产者，驱动器观察投影评估结论）；`workflow.get` + `WorkflowDefinitionView`（head 定义正文由服务侧产品目录保留，pinned 库无正文读取 API 属 W-03 上游设计非缺口），编辑器跨会话编辑解除；hello `observation` 能力位；步级运行事件（无已承诺消费方）与会话管理面（删除=pinned `close_session` 承载挂账 `session.close`、重命名=产品别名 M5-08、导出=客户端投影、fork 不立项）评估后挂账；产品视觉管线接线挂账与 Overlay 联动 | Mirage 维护者 | `M5-06` 第二轮（已完成）；视觉管线点亮 / `session.close` / 步级运行事件挂账后续增量 |
| DEC-027 | M5 对话模式真实化（模型层装配与 session.chat 对话面） | 已定案（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)）：纯对话形态 = Text 契约单轮 ModelRequest → ModelGateway 推理（不复用桌面 AgentLoop）；`integration/mira::ModelLayer` 装配 pinned ModelProfile/Router/Provider/SocketTransport/Gateway，SecretRef 经环境变量在传输边界解析（fail closed），TLS 通道按需挂接、https 无通道 fail closed 永不降级；`session.chat` 异步 turn 受理回执 + `session.chat_updated` 生命周期事件 + `session.chat.history` 快照，hello `chat` 能力位；对话线程服务内存易失、不进 pinned 会话投影；测试经 `ModelProviderOverride` 注入（生产 provider SSRF 姿态拒回环为上游安全设计）；真实端点连通性挂账部署态取证；凭据升级/流式/上下文策略挂 M5-08 与后续 | Mirage 维护者 | `M5-06` 第四增量（已完成，工作项收口）；真实端点取证 / 凭据升级 / 流式挂账后续 || DEC-027 | M5 对话模式真实化（模型层装配与 session.chat 对话面） | 已定案（[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)）：纯对话形态 = Text 契约单轮 ModelRequest → ModelGateway 推理（不复用桌面 AgentLoop）；`integration/mira::ModelLayer` 装配 pinned ModelProfile/Router/Provider/SocketTransport/Gateway，SecretRef 经环境变量在传输边界解析（fail closed），TLS 通道按需挂接、https 无通道 fail closed 永不降级；`session.chat` 异步 turn 受理回执 + `session.chat_updated` 生命周期事件 + `session.chat.history` 快照，hello `chat` 能力位；对话线程服务内存易失、不进 pinned 会话投影；测试经 `ModelProviderOverride` 注入（生产 provider SSRF 姿态拒回环为上游安全设计）；真实端点连通性挂账部署态取证；凭据升级/流式/上下文策略挂 M5-08 与后续 | Mirage 维护者 | `M5-06` 第四增量（已完成，工作项收口）；真实端点取证 / 凭据升级 / 流式挂账后续 |
| DEC-028 | M5-07 批准中心与权限策略面（policy.get/set 与 DEC-011 扩展） | 已定案（[DEC-028](../decisions/DEC-028-permission-policy-face.md)）：批准中心接 DEC-020 异步确认面（permission.list 快照 + permission.request 通知 + permission.respond 先到先得，TS 传输缺口补齐）；`policy.get` / `policy.set` wire 面（PolicyView：全 DEC-010 规则集 + read_roots 资源范围；全量覆盖校验、规则立即生效、read roots 重启生效）；`PermissionController` 线程安全 policy/set_policy；settings `permission` 块扩展为全词表 map（加法演进）+ 默认拾取与写回翻转（DEC-011 修订）；默认策略收紧留观 M5-08 | Mirage 维护者 | `M5-07`（已完成）；默认收紧 / read-roots 热更挂账后续 |
| DEC-029 | M5-09 Desktop Overlay 承载机制（服务进程承载 + Platform Backend 私有前端 + 事件织物联动，wire 零变更） | 已定案（[DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md)）：Overlay 由 `mirage-service` 承载（任务执行流宿主，GUI 关闭仍呈现）；`desktop/overlay_carrier.hpp` + `overlay_surface.hpp` 纯 std 公共头为唯一新契约面（不进 DesktopEnvironment 冻结 provider 集、不进 IPC wire）；Windows 双分层窗口（视觉窗 `WS_EX_TRANSPARENT` 跨进程穿透 + 每像素 alpha；交互窗仅确认横幅可点）；Linux X11 shape 覆盖层（override-redirect + XShape 双 shape，无合成器依赖），Wayland 原生 / 无交互桌面 open() 返回 null fail closed；呈现循环 = Executor `IBlockingIoWorker`，流侧 `LatestMailbox` 最新状态语义；`AtomOverlayFeed` 动作缝（DEC-024 先例同型）；确认入口经 `AsyncConfirmationHub::resolve` 同路径应答（first-response-wins 不变），呈现以 hub 快照 `is_pending` 校验；`--overlay on|debug` 默认 off | Mirage 维护者 | `M5-09`（已完成）；Windows 运行级取证 / Xvfb 呈现取证 / 独立测试验证轮挂账后续 |
| DEC-030 | M5-10 Tray 承载机制与任务暂停/恢复面（task.pause/resume 附加扩展 + 双平台指示器承载） | 已定案（[DEC-030](../decisions/DEC-030-tray-carrier-and-task-pause-face.md)）：`apps/tray` 单 Executor owner 经 Local IPC（EXEC-02）；`task.pause`/`task.resume` v1 附加扩展（v10，与 task.cancel 同形守卫），pinned pause 家族投影 + 驱动操作边界驻留（resume 后新 step id 续驱；pinned"不支持执行级续跑"关系如实声明）；Windows 承载 = DEC-018 Shell_NotifyIcon 面产品化菜单扩展（无 AUMID/无 WinRT）；Linux = StatusNotifierItem + 最小 dbusmenu（纯 gio 族，DEC-015 纪律），无指示器宿主 null 响亮退出；快速进入 = 兄弟壳二进制发现 | Mirage 维护者 | `M5-10`（已完成）；Windows 托盘运行级取证 / 真实指示器宿主取证 / 独立测试验证轮挂账后续 |
| DEC-031 | M5-11 Windows 安装器生成器定案（NSIS）与打包/更新通道交付 | 已定案（[DEC-031](../decisions/DEC-031-windows-installer-generator.md)）：生成器 = NSIS（`packaging/windows/mirage.nsi` + build-installer.ps1，fail closed），WiX 否决（.NET 工具链绑定、MSI 特性超需）；载荷 = 四产品进程 + 可选桌面壳 CEF payload；AUMID 注册表键随安装器交付（DEC-018 载体半边成立，快捷方式属性插件留发布机）；Authenticode 签名脚本证书注入 fail closed（证书不入仓库）；Linux .deb = CPack DEB（shlibdeps 自动依赖 + postinst/prerm），apt 仓库 GPG 签名/验签脚本 + 更新清单 ed25519 签名与 fail-closed 验签（本地五步演练全过）| Mirage 维护者 | `M5-11`（已完成）；真实安装/升级/卸载取证 / makensis 构建 + Authenticode 签名取证 / 图标资产挂账后续 |
| DEC-032 | M5-12 Windows 应用内更新器核心与通道 fail-closed 行为（runtime/update 库 + CLI，全量优先） | 已定案（[DEC-032](../decisions/DEC-032-update-client-fail-closed.md)）：runtime/update 库——清单严格解码（schema/字段/预算全 fail closed）、ed25519 验签（libcrypto EVP，OpenSSL 缺席构建整体 fail closed 于 crypto 面）、HTTP fetch（无 TLS，信任锚为清单签名+Authenticode 双层；3xx 不跟随；超预算 fail closed；POSIX/WinSock 双端口）、原子切换（staged 预检→备份→aside+rename→全成删备、败则逆序回滚）；CLI `mirage update check/apply`；托盘/壳触发面接同核（后续接线）；更新器零私钥（信任锚公钥随包分发）| Mirage 维护者 | `M5-12`（已完成）；Windows 运行级全量更新演练 / OpenSSL 运行时随包分发挂账后续 |

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
- `POST-04` mirador M7 跨帧目标跟踪消费（视觉观察增强）：触发条件 = 上游
  M7-10 go/no-go 判定 GO 且 `object_tracker` 契约计入兼容性承诺；消费点在
  `integration/mirador`（PerceptionSession 管线 + 变化门控短路），wire 走
  DEC-016 加法通道；路由见
  [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md) 决策 4。
- `POST-05` Tools / MCP 产品面真实化（Skill 列表 / 发布 / 吊销、工具兼容状
  态、MCP 服务器连接配置）：触发条件 = `M5-05` 工作流契约面落地且出现产品需
  求；经 IPC 镜像 pinned DEC-040 发布生命周期与 DEC-039 接纳契约，不自建
  skill 存储；路由见
  [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md) 决策 2。

## 2026-10-03 原生前端接手

维护者要求轻量原生窗口与 ZCode 参考的会话页，按 [DEC-033](../decisions/DEC-033-native-agent-frontend.md) 新建 [M6](m6-native-frontend.md)（In Progress）。先实施 EUI dev 锁定和本地预览；真实 IPC 与整体托盘退出分别归 M6-03/04。既有 M5/CEF 验收保留，不作为原生端功能等价证据。


2026-10-04 / M6-03：按维护者澄清，先交付通用Agent harness，RPA workflow暂不接入。
DEC-034在单一integration Adapter中复用Mira模型/工具/运行控制面；依赖缺口
MIRA-20261004-001（通用入口/工具输入）与002（TLS SNI）保留台账。本机SiliconFlow
实际问答与工具回填通过，MiniMax未通过；M6-04入口/托盘整体退出仍按原计划。
具体状态和验证见[M6](m6-native-frontend.md)及[验收](../compatibility/native-harness-20261004.md)。

M6-03 Linux通用harness首步已完成，真实模型/取消/背压/关闭与界面验收见上述证据；
M6整体仍In Progress，M6-04统一入口/托盘与Windows运行取证未完成。

2026-10-04 / M6-05：Linux原生会话页按ZCode源码对齐完成，包含Agent Markdown、
增长输入、引用原文/来源检查及真实上下文入口；测试与独立修正评分见
[会话验收](../compatibility/native-zcode-conversation-20261004.md)。
M6整体与M6-04状态不变，未将Wayland截图限制或Windows/IME待验收标记完成。


2026-10-04：M6-06 Linux首步上下文占用圆环完成，真实最后请求输入Token/显式窗口预算
经现有IPC展示；Debug/Release、ASAN/UBSAN、TSAN、真实模型和上下文扩展视觉复核通过。
详见[M6](m6-native-frontend.md)及[验收](../compatibility/native-context-usage-20261004.md)。
窗口未知和重启旧历史明确展示未知；M6整体与Windows/统一入口进程任务状态不变。

2026-10-05：维护者要求 ZCode 模型配置/指定输入栏顺序并清除旧 TS 前端，按 [DEC-037](../decisions/DEC-037-native-model-composer-and-web-retirement.md) 新增 M6-07/08。旧 CEF/TS 技术路线已退役，M5 历史验收保留；当前前端为 EUI 原生。完整入口/托盘退出确认仍在 M6-04。

M6-07/08 Linux首步已完成，模型目录、真实文本附件、权限/推理请求与指定工具栏接通，
旧Web源码/构建/打包已清除。Debug空闲回归48/48，原生针对与sanitizers通过，Linux
原生DEB生成和提取启动通过；负载下旧订阅测试失败和Windows等未执行项保留记录。
[模型与输入验收](../compatibility/native-model-composer-20261005.md) /
[退役验收](../compatibility/native-retirement-20261005.md)。M6整体与M6-04状态不变。


2026-10-05 / M6-15至18完成Linux范围：紧凑会话、右侧模型/用量、真实流式与用时，
Linux XIM候选光标跟随及公开Mira ConversationLoop；两项依赖修复分别提交上游PR，
原Executor版本保留，锁与供应链审计同步。[验收](../compatibility/native-conversation-progress-20261005.md)。
M6整体/入口托盘M6-04仍未完成，Windows/Wayland/物理高DPI另验。


M6-19完成Linux依赖收尾：EUI四种窗口/渲染CI组合全部通过，Vulkan生命周期修复与
SDK消费、Mirage原生回归通过。同步最新dev修复并移除库层异常临时覆写；Mira流式PR
12项CI也已成功。未合并依赖PR，完整入口/托盘M6-04保持后续工作。
[验收](../compatibility/eui-vulkan-followup-20261005.md)。


2026-10-06 / M6-20完成Linux会话复验：真实窗口IME/多行、真实流式/用时、右侧工具组，
补齐长回复阅读保位与返回底部、空闲关闭唤醒。EUI公开offset修复更新PR#88/pin与锁，
当前head四项CI及原生Debug/Release/ASAN/UBSAN回归通过；无新增Executor并发路径。
[最终验收](../compatibility/native-conversation-finish-20261006.md)。M6整体仍In Progress，M6-04保持Planned。

2026-10-06 / M6-21完成Linux侧栏与模型服务编辑：统一图标/文字轴、未命名草稿、服务/模型分层、实际Key保存/刷新/调用；来源对照与未覆盖的完整ZCode能力见[验收](../compatibility/native-provider-editor-20261006.md)。M6整体状态和M6-04不变。
