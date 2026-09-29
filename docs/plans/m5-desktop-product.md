# M5：Desktop Product（Workspace / Overlay / 权限 / 分发）

> 状态：In Progress（`M5-01`、`M5-02`、`M5-03`、`M5-04` 完成，2026-09-26；
> `M5-05` 全部完成 2026-09-27；`M5-06` 完成 2026-09-28（四增量：会话页接
> 真实面、观察面协议扩展、会话管理面 session.close、对话模式真实化）；
> `M5-07` 批准中心与权限策略面完成 2026-09-28；`M5-08` 设置八类落地与
> 产品化规模复核完成 2026-09-28（CI 取证已由 `M5-09` PR 补录）；
> `M5-09` Desktop Overlay 完成 2026-09-30（DEC-029 承载机制定案））
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M3](m3-mirador-integration.md)（已完成：Mirador 视觉集成与壳选型冻结——
> CEF，PoC 取证与暂定策略在案）、[M4](m4-windows-backend.md)（已完成：Windows
> Backend 与产品进程 Windows 化——命名管道 IPC、`mirage-service` / CLI 双平台
> 可运行）
> 建议发布点：`release-epsilon`（tag 待维护者授权后创建）
> 更新日期：2026-09-30（`M5-09`）

## 目标

使 Mira 已有 Agent Harness 能力在 Mirage 中获得完整桌面产品形态（设计文档第
18 节第五阶段）：CEF 嵌入式壳承载产品 UI（DEC-006 冻结路线），Agent Workspace
（会话 / 工作流 / 执行 / 设置）全部接真实数据面，权限管理演进为 Local IPC
异步确认面（DEC-010 预告的 M5 演进），Desktop Overlay 与 Tray 进程形态落地
（设计文档第 12、14 节），并以 `.deb` / `exe` 安装包与签名更新通道完成分发
形态（DEC-006 分发承诺兑现，`SCOPE-06` 收口）。

## 范围与非目标

范围：

- CEF 产品壳（`apps/desktop`）：壳进程模型、`ui/app` 构建产物承载、壳内 IPC
  产品传输路径（browser 进程经 `runtime/ipc` 直连 Local IPC；IPC 契约是 UI 与
  C++ 的唯一耦合面，`RULE-01` / `EXEC-02`）；壳形态与浏览器形态同构
  （DEC-013 §3.2），devbridge 维持开发工具定位不变。
- 前端工具链与供应链定案（DEC-006 决策 6）：包管理器 / 打包器 / lint 工具链
  定案；CEF 二进制获取与锁定进 `dependencies.lock.json` schema v2（
  [shell 二进制锁定机制](../supply-chain/shell-binary-locking.md)兑现），
  "未锁定二进制不进默认构建"门禁；CEF 沙箱与 GPU 策略转正式方案。
- 协议 v1 附加扩展（DEC-012 扩展流程，传输 / 帧格式 / 版本号不变）：
  `permission.*` 异步确认面、`session.*` 会话面与消息 / 轮次 / 增量输出事件、
  `workflow.*` 工作流面；Runtime Service 承载相应演进（DEC-008 迁移路径）。
- UI 产品化（DEC-013 为验收基线）：会话页对话模式真实化、批准中心与权限
  策略管理、工作流编辑器完整版（mira Workflow IR 对齐）、设置八类全量；
  M1.5 模拟域演示语义退出真实数据面。
- Desktop Overlay（设计文档第 14 节）：目标高亮、即将执行操作提示、确认
  入口与 Observation 调试观察面；平台承载按平台能力如实降级。
- Tray 进程形态（`apps/tray`，设计文档第 12 节）：运行状态、任务暂停 / 恢复、
  快速进入 Mirage；经 Local IPC 交互。
- 分发与更新（DEC-006）：Linux `.deb` + GPG 签名 apt 仓库；Windows `exe`
  安装器 + Authenticode 签名 + 双层签名应用内更新器（全量优先）；安装 /
  升级 / 卸载验证与更新通道演练；toast 通知承载重议评估（DEC-018 触发）。

非目标：

- Agent Harness 内核与模型推理（pinned Mira / mirador 承载，Mirage 只适配）；
  会话 / 任务模型以 pinned mira 已交付公开 API 为承载面，缺口按工程规范
  9.4 节登记，不静默分叉。
- Windows 采集性能升级（Windows.Graphics.Capture / DXGI Duplication）：
  DEC-017 既定后续路径，非产品形态必需，独立评估立项（M4 非目标重申）。
- 全树 MinGW 交叉复验：随台账 `MIRA-20260922-001` 缺口关闭补跑（M4 挂账），
  不占 M5 工作项。
- 差分更新：全量优先，差分（如 courgette / bsdiff、apt delta）按实测流量
  成本评估、不提前承诺（DEC-006 决策 5）。
- 多实例 / 多用户隔离的实现（`POST-02`）：M5 仅做单实例前提下的产品化规模
  复核（DEC-007 / DEC-012 复核条目），隔离形态依触发条件另立项。
- macOS / 移动端（`POST-01`）、远程 Desktop Environment（`POST-03`）。

## 设计与决策依据

- [设计文档](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md) 第
  11-16、18 节（后台运行与 CLI、Agent Workspace、Desktop Overlay、权限与
  桌面安全、本地状态、第五阶段路线）。
- [《Mirage 前端设计规范与信息架构》](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)：
  M5 产品化验收基线（设置八类、workflow 编辑器 IR 对齐、批准中心）；§4 契约
  映射与前瞻依赖是协议扩展工作项的直接输入；§3.2 壳形态与浏览器形态同构。
- [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)：CEF 冻结与
  PoC 暂定策略（沙箱 / GPU）、`.deb` / `exe` 分发与更新通道冻结、决策 6
  （工具链定案属 M5）、Overlay 平台限制与验证方式。
- [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md) /
  [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)：
  协议 v1 附加扩展流程、wire 契约事实源（`mirage-ipc-protocol-v1.md`）、
  事件承载（`executor::comm`）、产品化规模复核条目（队列容量 / 多连接 /
  多用户隔离）。
- [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)：
  M1 过渡驱动形态向 mira 会话 / 任务模型迁移的既定路径（M5-04 兑现）。
- [DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)：2026-09-26
  升级批（[升级审计](../supply-chain/dependency-upgrade-audit.md)，mira
  `1348515` / mirador `fb0dc3f`）能力消费路由——`M5-05` 绑定升级后 pinned
  工作流 / 工具契约面（TR0/TR2、Skill 执行、DEC-039 MCP 接纳为后续连接配
  置承载边界）；Tools / MCP 产品面与 mirador 目标跟踪消费挂账 POST-05 /
  POST-04，不在 M5 范围内。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)：M1 同步确认
  挂点为过渡形态，M5 以 Local IPC 异步确认面替换（判定语义不变），需新
  决策记录；默认策略收紧随产品化。
- [DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)：本地状态
  条目集与格式冻结；默认路径解析翻转默认行为、其余条目随产品设置面定案。
- [DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)：
  [DEC-014](../decisions/DEC-014-ui-component-stack.md)：信息架构、组件框架
  与视觉语言跨形态一致；M5 验收基线条款。
- [DEC-018](../decisions/DEC-018-windows-notification-carrier.md)：toast
  重议触发条件（安装器交付 AUMID 载体 + Action Center 驻留 / 点击交互的
  产品化需求），M5-11 复核。
- [shell 二进制锁定](../supply-chain/shell-binary-locking.md)：CEF 二进制
  锁定机制设计（`dependencies.lock.json` schema v2 与门禁，M5-01 兑现；
  "未锁定二进制不进默认构建"）。
- [M1.5 计划](m1.5-ui-parallel-track.md)：UI 侧 transport 接口 + mock 先行
  模式、`WorkflowBackend` 接口缝、模拟域边界；M5 以真实数据面替换。
- [M4 计划](m4-windows-backend.md)：产品进程 Windows 化先例（命名管道 IPC、
  全树 MSVC 门禁、CI 取证分级）；tray 目标随 M5 落地的既定交接。

## 工作项

- [x] `M5-01` 前端工具链定案与 CEF 二进制锁定（DEC-006 决策 6 兑现）：包
      管理器 / 打包器 / lint 工具链（eslint）复核定案并写入 DEC-006 修订；
      组件框架复核（DEC-014 React 19 已定选；前端规范 §2.2 token 命名映射
      复核）；CEF 二进制获取与锁定落地 `dependencies.lock.json` schema v2
      （shell-binary-locking 机制兑现）与"未锁定二进制不进默认构建"门禁；
      CEF 沙箱与 GPU 策略由 PoC 暂定值转正式方案（M3-06 预告）。
- [x] `M5-02` CEF 产品壳骨架与壳内 IPC 传输路径（`apps/desktop`）：壳进程
      模型（browser / renderer、窗口创建、加载 `ui/app` 构建产物、M5-01
      定案的沙箱 / GPU 策略）；产品 IPC 路径——browser 进程经 `runtime/ipc`
      `IpcClient` 直连 Local IPC（Unix socket / 命名管道，传输差异不外溢），
      renderer 经受控 bridge 访问（UI 不进 C++ 目标依赖图，IPC 契约唯一
      耦合面）；壳形态与浏览器形态同构，transport 装配语义在壳内等价；
      devbridge 维持开发工具定位不变。验收：壳内 UI 对真实 `mirage-service`
      完成 hello / 订阅 / 任务往返。
- [x] `M5-03` 权限异步确认面（DEC-010 M5 演进）：异步确认语义决策记录
      （等待预算、超时收敛、多连接竞争、恢复行为——DEC-010 限制节预告的
      决策输入）；协议 v1 附加扩展 `permission.*`（请求事件 + 应答 op，
      DEC-012 扩展流程，wire 契约与 golden vectors 双端同步）；
      `runtime/permission` 同步确认挂点演进为 Local IPC 异步面（M1
      "必须立即返回"挂点契约的替换路径，判定语义与 Provider 硬边界不变）；
      任务驱动器确认流接线与 trace 记录保持。
- [x] `M5-04` 会话与消息契约面（协议 v1 扩展）：`session.*` 请求面（列表 /
      打开 / 历史摘要，依托 pinned mira `open_session` / `conversation_log`
      投影）与消息 / 轮次 / 增量输出事件集（DEC-012 扩展流程）；Runtime
      Service 由 M1 过渡驱动形态向 mira 会话 / 任务模型演进（DEC-008 迁移
      路径兑现）；golden vectors 双端门禁与 wire 契约同步。
- [x] `M5-05` 工作流契约面与编辑器真实化（第一轮契约面、第二轮桌面原子工具
      注册与编辑器接真实面完成，2026-09-27）：`workflow.*` 请求面（`list` /
      `save` / `publish` / `delete` / `atom.catalog` / `runs` / `run` /
      `cancel`），绑定 2026-09-26 升级后的 pinned 工作流 / 工具契约面
      （[DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)
      决策 1：`workflow_runtime` TR2 工具引用挂载与 Skill 执行注册、
      `workflow_events` 新增事件词表（含工具兼容 Degraded 准入）、
      `atom.catalog` 经 pinned 工具暴露投影承载 Platform Backend 能力上
      报；实现前以 pinned 公开头与 `docs/api/` 复核接口缝，M1.5 mock 的
      `WorkflowBackend` 仅作 UI 侧接口缝）；运行监控事件化（WorkflowRun
      状态；工具兼容状态呈现与否由实现轮定，走 DEC-012 附加扩展）；UI 工
      作流编辑器完整版接真实面（DEC-013 IR 对齐验收）。
- [x] `M5-06` 会话页产品化（对话模式真实化；第一轮"会话页接真实面"完成，
      2026-09-27，[DEC-025](../decisions/DEC-025-session-page-productization.md)：
      签派栏 / 线程流 / 观察台接 `session.*` 与任务快照契约路径，模拟域退出
      会话页，对话模式如实降级，wire 零变更；第二轮"观察面协议扩展批次"
      完成，2026-09-28，
      [DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)：
      观察面协议附加扩展（`desktop.observe` + `ObservationView`，语义快照
      投影 / 视觉状态 / `visual_snapshot_ref` 进 UI 观察面，M3 非目标兑现）
      与工作流定义读取面（`workflow.get`，编辑器跨会话编辑），golden
      v5 → v6，步级运行事件与会话管理面评估后挂账；第三增量"会话管理面
      `session.close`"完成，2026-09-28（DEC-026 挂账②兑现，golden v6 → v7）：
      pinned `close_session` 承载 + 服务注册表条目移除（容量可复用）+ 主会
      话 `invalid_state` 守卫 + 会话页删除入口，`session.updated` 增补关闭
      发布点）；第四增量"对话模式真实化"完成，2026-09-28
      （[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md) 新增，
      Accepted，DEC-025 挂账③兑现，golden v7 → v8）：模型层装配（pinned
      ModelProfile/Router/Provider/SocketTransport/Gateway + SecretRef 环
      境变量解析 + OpenSSL TLS 通道按需挂接）+ `session.chat` /
      `session.chat.history` / `session.chat_updated` 对话面 + Composer 对
      话模式启用；DEC-026 挂账④联动评估维持挂账（纯对话无桌面驱动步），
      M5-08 联动评估以 `ModelLayerConfig` 面为契约输入）：会话列表 / 管理接
      `session.*`；消息流真实渲染（M1.5 模拟域演示语义退出会话页）；观察台
      真实化——运行时间线 / 观察流直连任务快照与事件，视觉状态呈现与订阅
      演进；Composer 双模式信息架构完整兑现，模型层未配置的服务保持如实降
      级。
- [x] `M5-07` 批准中心与权限管理产品化：批准中心接异步确认面（`M5-03`）；
      设置页权限策略配置（每能力 `allow` / `confirm` / `deny`、资源范围、
      默认策略收紧——DEC-010）；权限策略持久化（DEC-011 Desktop
      Permissions 条目）与运行时生效路径。
- [x] `M5-08` 设置全量与产品化规模复核：设置八类持久化条目定案与落地
      （DEC-011 条目集：Application Settings / Runtime Configuration /
      Desktop Permissions / UI Layout / Platform Configuration 等）；默认
      路径解析翻转默认行为（DEC-011）；事件队列容量 / 多连接规模 / 端点
      对端凭据校验的产品化复核（DEC-012 / DEC-007 复核条目）；M1 遗留
      `mirage service start` 守护纪律修复随产品化落地（M1 计划验证记录
      挂账）；CI 取证已随 `M5-09` PR 补录（2026-09-30，工程规范第 4 节
      勾选规则第 1 条）。
- [x] `M5-09` Desktop Overlay（完成 2026-09-30；
      [DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md) 承载机制
      随实现定案并登记）：平台承载（Windows 分层窗口：置顶 / 透明 /
      点击穿透；Linux X11 覆盖层先例 + Wayland 限制如实声明——DEC-006
      风险节）与 UI（目标高亮、即将执行操作提示、确认入口、Observation
      调试观察面）；与任务执行流的事件联动（服务内部事件织物：事件订阅
      横幅 + 桌面原子动作 overlay 缝 + 确认面镜像，wire 零变更）。
- [ ] `M5-10` Tray 进程形态（`apps/tray`）：运行状态显示、任务暂停 / 恢复、
      快速进入 Mirage；经 Local IPC 交互（`EXEC-02`，不自建 Executor）；
      承载机制按平台能力如实降级（Windows 通知区事实沿用 DEC-018 既有
      承载面；Linux 状态指示器机制随实现定案并记录）。
- [ ] `M5-11` 打包与更新通道（DEC-006 分发形态兑现）：Linux `.deb` + GPG
      签名 apt 仓库（签名 key 管理与发布机隔离——shell-binary-locking
      既定）；Windows `exe` 安装器（生成器随实现定案并记录，DEC-006 决策 4
      列 NSIS / WiX 为候选）+ Authenticode 签名；安装 / 升级 / 卸载验证与
      更新通道演练（清单验签 fail closed）；toast 通知承载重议评估（DEC-018 M5
      触发条件复核，结论留痕）。
- [ ] `M5-12` Windows 应用内更新器：更新清单（版本 / URL / SHA-256 /
      ed25519 签名）验签 fail closed + 原子切换与回滚（shell-binary-locking
      机制设计兑现；凭据经系统 keyring）；对签名清单完成全量更新演练
      取证（差分按实测评估、不提前承诺）。
- [ ] `M5-13` 里程碑退出复核：退出条件逐项独立取证（同 M2 / M4 退出复核
      形态，独立验证轮）。

拆分纪律：延续"先契约后实现、先骨架后功能"——工具链与二进制锁定先于壳
（`M5-01` → `M5-02`）、协议扩展先于 UI 真实化（`M5-03`..`M5-05` →
`M5-06`..`M5-07`）、UI 侧一律以既有 transport 接口 + mock 先行不阻塞契约
演进（M1.5-04 模式）；协议扩展只走 DEC-012 附加扩展流程，传输 / 帧格式 /
版本号不变。壳内 IPC、Overlay 与 tray 的平台差异收敛在 Platform Backend
或进程边界内，不渗入协议与契约面。

## 风险与阻塞

- CEF 二进制规模与获取（≈560 MB 载荷、下载源依赖）：锁定机制 + 供应链审计
  （`dependency-upgrade-audit`）缓解；二进制来源未进入锁定机制前不得进入
  默认构建（DEC-006 第 6 条）；`shell-binary-locking` 的 schema v2 与门禁
  在 `M5-01` 同变更落地。
- mira 会话 / 消息承载面缺口风险：`M5-04` 动工前先核对 pinned mira 公开
  API（`runtime.hpp` / `conversation_log.hpp` / `agent_loop.hpp`）与
  `docs/api/` 文档；确认缺口即按工程规范 9.4 节登记台账并引用编号，不
  静默分叉或绕过。同一纪律适用于 `M5-05`：pinned `WorkflowRuntime` /
  `workflow_events` 在 2026-09-26 升级批新增工具引用挂载、Skill 执行与
  Degraded 准入事件（DEC-022 决策 1），动工前以现状公开头部复核接口缝，
  不按 M1.5 mock 旧认知实现。
- Linux Overlay / 托盘的平台限制（Wayland 合成器、AppIndicator 可用性、
  点击穿透支持面）：按平台能力如实降级并响亮声明（DEC-006 风险节先例、
  M4 前台激活同类纪律）；不为取证伪造能力。
- CI runner 对 Overlay / tray / 安装包取证的能力边界：维护者 Windows 机器
  取证为兜底（M4-06 先例）；不可取证项保持未勾选并记录补跑条件（工程
  规范第 4 节）。
- 前端工具链改选的迁移成本：M1.5 已交付 Vite + React 19 + vitest 事实；
  `M5-01` 定案原则是"复核确认或以最小迁移成本改选"，不为改选而改选；
  定案前不得写入兼容性声明（DEC-012 限制节既有纪律）。
- 权限异步面的语义回归风险：M1 同步挂点契约（立即返回）与 M5 异步面的
  替换路径必须有决策记录与契约测试双保险；判定语义、词表与 Provider 硬
  边界（DEC-009）不得随实现漂移。

## 测试与退出条件

- [ ] Linux 主机矩阵不回归：`debug`、`release`、`asan`、`ubsan`（+ `tsan`
      按并发路径需要）构建 + `ctest` 全绿无 skip；`mirage-format-check` 与
      `mirage-boundary-check`（含新增公共头）通过（`DOD-01` / `DOD-03`）。
- [ ] 协议演进一致性：`permission.*` / `session.*` / `workflow.*` 附加扩展
      经 golden vectors 双端门禁；传输、帧格式与版本号不变；
      `mirage-ipc-protocol-v1.md` 契约同步（`DOD-04`）。
- [ ] 壳与产品进程形态：`apps/desktop` 壳内 UI 对真实 `mirage-service` 完成
      任务往返（Linux + Windows 双平台取证，证据分级按 DEC-017 形态）；
      `apps/tray` 经 IPC 完成状态订阅与操作往返；`mirage-service` / CLI
      既有行为不回归。
- [ ] 权限异步确认面：批准 / 拒绝 / 超时 fail closed / 取消路径有测试与
      取证；判定语义、Capability 词表与 Provider 硬边界不偏离（DEC-009 /
      DEC-010）（`DOD-04`）。
- [ ] UI 验收基线：DEC-013《前端设计规范与信息架构》为基线（设置八类、
      workflow 编辑器 IR 对齐、批准中心）；会话 / 工作流 / 批准 / 设置
      数据面真实化，模拟域仅保留开发形态并在界面标注（`DOD-04`）。
- [ ] 打包与更新：双平台安装 / 升级 / 卸载验证；更新通道演练（清单验签
      fail closed、回滚路径）；toast 重议评估有结论记录（DEC-006 验证
      方式节 / DEC-018）。
- [ ] 决策与文档同步：新决策记录（权限异步确认面、Overlay / tray 承载
      机制等随实现登记）、DEC-006 / DEC-010 / DEC-011 演进修订、总计划
      状态与验证记录同步（`DOD-05`）；Commit / MR 符合工程规范第 10 节
      （`DOD-06`）。

## 验证记录

2026-09-26：`M5-01` 前端工具链定案与 CEF 二进制锁定完成。

- 范围：
  - **工具链定案**（DEC-006 修订记录 2026-09-26 节）：npm（workspaces，
    `npm ci` 纪律，Node ≥ 22 经 `engines` 声明）/ Vite 7 / React 19（DEC-014
    复核确认）/ vitest 复核确认，无迁移；lint 工具新定选 ESLint 10 +
    typescript-eslint 8 + eslint-plugin-react-hooks 7（flat config
    `ui/eslint.config.js`，CI frontend 作业增 lint 步骤）。react-hooks v7
    新增 `purity` / `set-state-in-effect` 两规则对 M1.5 已交付视图存在 7 处
    既有发现（5 处渲染期 `Date.now`、2 处 effect 内 setState，文件与位置在
    lint 首轮输出在案：Overlays.tsx / WorkflowsPages.tsx / Observer.tsx /
    SessionsSidebar.tsx），修复需视图级重构（时间源注入 / effect→render
    派生），登记为 `M5-06` / `M5-07` 重做对应视图时的清理范围，当前在 lint
    配置内记录性豁免（理由注释在案），其余规则全量生效。
  - **CEF 二进制锁定**（shell-binary-locking §2 机制兑现）：
    `dependencies.lock.json` 升至 schema v2——`artifacts[]` 登记 CEF
    `152.0.8+g1ce985c+chromium-152.0.7977.134` stable standard 双平台 pin：
    linux64（674,894,043 B）sha1 `add0a51f…` 为官方 index 与 PoC 本地
    sha1sum **双源一致**（index 提取 2026-09-26；本地复核 2026-09-21）； 
    windows64（359,844,028 B）sha1 `fcefc344…` 为官方 index 提取（本地下载
    -摘要复核随 `M5-02` 首次消费执行并回填 provenance）。`frontend` 条目登记
    npm 树概要，`ui/package-lock.json` 的 sha256 于每次 configure 重算比对。
  - **门禁**：`cmake/MirageDependencies.cmake` 扩展——schema 版本强制（非 2
    即失败）、工件 pin 结构校验（缺员 / 非 40-hex sha1 / 空值 fail closed）、
    npm 哈希活动门禁（漂移即 configure 失败，无条件执行——本地、零网络）、
    `mirage_require_locked_artifact()` 消费门禁（未注册工件被产品目标消费即
    configure 失败，M5-02 壳目标须经此绑定）。新增
    `tests/cmake/dependency_lock_test.cmake`（ctest
    `dependency_lock_gate_test`：2 正例 + 5 负例——未注册工件、schema 回退、
    非 40-hex sha1、缺员、npm 漂移；夹具经 `MIRAGE_DEPENDENCY_LOCK_DIR`
    重定向，无需真实 CEF 下载）；windows 作业测试列表纳入。实现期事实：CMake
    正则不支持 `{n}` 重复语法（花括号为字面量），摘要校验以长度 + 字符类实现。
  - **沙箱与 GPU 正式方案**（DEC-006 修订节）：沙箱默认启用（Linux `.deb`
    的 chrome-sandbox 属主 / 模式随 `M5-11` 打包交付；任何禁用须显式记录）；
    GPU 保持启用、不设跨平台禁用开关（PoC 基线 §3.4 的 GPU 段错误为 Electron
    特有证据，不外推）。
- 依据：设计文档第 17、18 节；`DEC-006`（决策 5 / 6 与 2026-09-26 修订）、
  `DEC-013` / `DEC-014`、`RULE-06` / `RULE-07` / `RULE-08`、工程规范 §9.1；
  [shell-binary-locking](../supply-chain/shell-binary-locking.md) §2 机制设计；
  本计划 `M5-01` 工作项。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，真实桌面）：
  - 全树 MSVC configure：schema v2 校验在案（双平台工件 pin + npm 树哈希 +
    submodule / nested pin 全部通过）；Debug 全树构建 **0 诊断**；ctest
    **22/22 通过 0 skip**（新增 `dependency_lock_gate_test` 0.35 s，7 场景
    全绿）。
  - ui：`npm run check`（tsc 严格）通过；`npm test` **16 文件 530 测试通过**；
    `npm run lint` 0 问题（ESLint 10.11.0）。
  - 官方 index 事实源：`index.json`（10.4 MB）本机下载解析（2026-09-26），
    双平台 tarball 条目（name / sha1 / size）与锁文件登记逐项一致；linux64
    与 PoC `versions.lock.json` 记录逐字节相同。
  - Linux 矩阵 / format / boundary：本变更零 C++ TU 变更（clang-format 与
    boundary 扫描面无交集），Linux 五预设 + format & boundaries 由本 PR CI
    取证（结论按仓库先例由后续工作项 PR 补录）。
- 限制与补跑条件：① windows64 CEF 包的本地下载-摘要复核随 `M5-02` 首次消费
  执行并回填 provenance（负责人：`M5-02` 实现轮）；② react-hooks 新规则的
  7 处豁免随 `M5-06` / `M5-07` 视图重做清零（负责人：对应工作项）；③ CI
  frontend lint 步骤、windows 作业门禁测试与 Linux 矩阵随本 PR 首跑取证
  （结论见下一条记录——首跑暴露行尾敏感缺陷并修复，复跑全绿）。
- 同步：[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)（修订
  记录 2026-09-26 节 + 决策 6 定案标注）、
  [shell-binary-locking](../supply-chain/shell-binary-locking.md)（状态 →
  Mechanism landed）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §6（token 复核记录）、ui/README（工具链与哈希耦合说明）、
  [总计划](mirage-implementation-plan.md) 状态叙述。

2026-09-26：`M5-01` CI 取证完成；首轮暴露 npm 哈希门禁的行尾敏感缺陷并修复
（`985e45f`），复跑全绿。

- 背景：PR [#48](https://github.com/Linductor-alkaid/mirage/pull/48) 首轮 CI
  （run 36164073771，headSha = `efddc2e`）frontend 作业绿，但 5 个 Linux
  build & test 作业与 format & boundaries 全部失败——与本变更"零 C++ TU"
  的预期矛盾，指向 configure 期新增门禁。
- 发现（缺陷定性）：npm 哈希门禁的行尾敏感——首轮实现在 Windows 工作树
  （core.autocrlf，CRLF 检出）上以 `file(SHA256)` 直接计算
  `ui/package-lock.json`，登记的是 **CRLF 内容**的摘要；git 索引（repo blob）
  为 LF，Linux CI 检出（LF）在门禁下必然"漂移"→ configure 失败（5 作业 +
  format/boundaries 的 configure 步骤同因失败）。本机验证成立：CRLF 哈希
  `f05f13…`（首轮登记值）≠ LF 规范化哈希 `e45251…`（git blob 内容）。
- 修复（`985e45f`，`fix(deps)`）：摘要语义改为 **LF 规范化内容**（与提交
  blob 一致，检出行尾无关）——cmake 侧 `file(READ)` 后
  `string(REPLACE "\r\n" "\n")` 再 `string(SHA256)`；锁文件登记值更新为
  `e45251…` 并新增 `lockfile_sha256_semantics` 字段声明语义；错误消息、
  头注释、ui/README 同步。`npm_drift_rejected` 负例（追加字节 → 规范化内容
  变化 → fail closed）在修复后语义下仍然成立，门禁强度不减。
- 验证（复跑 run 36165052646，headSha = `985e45f` 已核实，全部 8 作业
  success）：
  - Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿，每预设
    **28/28 测试 0 skip**（含 `dependency_lock_gate_test`，tsan 预设经
    `setarch -R` 0.22 s 通过）；format & public-header boundaries 绿。
  - frontend 作业：**lint 步骤首跑执行**（`npm run lint`，ESLint 10.11.0），
    连同 tsc / 530 测试 / build 全绿。
  - windows msvc (full tree)：MSVC 编译 0 错误，14/14 测试通过（含
    `dependency_lock_gate_test` 0.20 s，windows 作业测试列表新增项）；
    产品进程往返与 windows frontend evidence 步骤照常通过（M4 既有门禁
    不回归）。
  - 本机（Windows 11，CRLF 工作树）复验：configure 以 LF 哈希 `e45251…`
    通过、Debug 全树 0 诊断、ctest 22/22——CRLF 检出与 LF 检出两侧在同一
    登记值下均通过，检出无关性成立。
- 限制与补跑条件：无新增；`M5-01` 既有挂账（windows64 CEF 本地下载复核随
  `M5-02`、react-hooks 豁免随 `M5-06`/`M5-07`）维持不变。
- 同步：本记录；PR [#48](https://github.com/Linductor-alkaid/mirage/pull/48)
  CI 取证（首轮 run 36164073771 / 复跑 run 36165052646）。合并裁决：维护者。

2026-09-26：`M5-02` CEF 产品壳骨架与壳内 IPC 传输路径完成。

- 范围：
  - **锁定工件消费**（shell-binary-locking §2.1 兑现）：`cmake/MirageCef.cmake`
    `mirage_acquire_locked_cef()`——configure 期从锁文件解析平台 pin（URL 按
    `{platform}` 填充 + 40-hex sha1 + 正整数 size 校验，实现于
    `mirage_require_locked_artifact_platform()`）；缓存 tarball configure 期
    复核 sha1/size（失配 FATAL），冷缓存 `file(DOWNLOAD)` 按 `EXPECTED_HASH`
    下载；解包后校验发行包布局。**M5-01 挂账兑现**：windows64 包本地下载
    359,844,028 B、sha1 `fcefc344…` 与锁文件登记一致（2026-09-26 本机
    sha1sum），lock provenance 已回填；configure 门禁以同值复核通过。
  - **壳目标**：`MIRAGE_ENABLE_DESKTOP_SHELL`（默认 OFF，默认构建图不下载
    工件、保持密闭；启用即经锁文件绑定，未注册 pin 即 configure 失败）。
    `apps/desktop/CMakeLists.txt`：Windows 为 `mirage-desktop.dll` +
    发行包 `bootstrap.exe` 复制为 `mirage-desktop.exe`（CEF 152 bootstrap
    沙箱模型，DEC-019 决策 3/4）；Linux 为单可执行 + X11 窗口。
    `CEF_RUNTIME_LIBRARY_FLAG=/MD` 在 `find_package(CEF)` 前设定，CEF 目标与
    `mirage_ipc` / pinned executor 共用 /MD[d] CRT（消除 LNK2038）。
  - **壳骨架**（DEC-019）：browser 进程 `DesktopApp`（scheme 注册 +
    `OnContextInitialized` 建窗）+ `DesktopClient`（message router、浏览器
    集、末窗关闭退出消息循环）+ `ShellSession`（SessionClient 经 Executor
    blocking worker 驱动、惰性重连）+ `BridgeCore`（协议封装映射，
    CEF-free）；renderer 进程 `DesktopRendererApp`（message router 渲染侧
    `mirageQuery` + 事件/失联进程消息分发到 JS 钩子）；`UiSchemeFactory` 以
    `mirage://app/`（STANDARD+SECURE+CORS）供 `MIRAGE_UI_APP_DIST` 资产
    （configure 期要求 dist 存在；路径解码与 `..` 逃逸 fail closed）。
    查询处理按规则 11：CEF UI 线程只投递 → Executor 执行 → `CefPostTask`
    回投；admission 拒绝经 future 异常显式应答（规则 10）。沙箱默认启用
    （bootstrap 承载）、GPU 不禁用（M5-01 正式方案，无跨平台开关）。
  - **runtime/ipc**：新增 `SessionClient`（`session_client.hpp/.cpp`）——
    长连接、`call()` 排队 + id 关联、DEC-012 单未决纪律内建于发送节流、
    事件 sink、超时 fail-closed 关会话；`run()` 由 owner 提交 Executor
    blocking worker 驱动（类本体 executor-free，无自建线程）。
  - **UI**：`ui/contracts` 新增 `desktop-transport.ts`
    （`DesktopBridgeTransport`，CEF 查询对象形式
    `{request, persistent, onSuccess, onFailure}`；关联 id echo 校验；
    `__mirageOnEvent` / `__mirageConnectionLost` 钩子生命周期管理）；
    `app/src/main.tsx` transport 选择扩展——壳内（`window.mirageQuery`
    存在）自动选 DesktopBridgeTransport，devbridge / mock 语义不变
    （M1.5 纪律），devbridge 定位不变。
  - **CI**：windows 全树作业 `-DMIRAGE_ENABLE_DESKTOP_SHELL=ON`，CEF tarball
    按锁文件哈希缓存（`build/artifact-cache/*.tar.bz2`）。
- 依据：设计文档第 12、17 节；`DEC-006`（决策 1/6）、`DEC-007`/`DEC-012`、
  `DEC-013` §3.2、`DEC-017`（/MD 工具链）；[DEC-019](../decisions/DEC-019-cef-shell-skeleton-and-bridge.md)
  （本工作项新决策记录：工件获取、bootstrap 进程形态、CRT 一致性、受控
  bridge 机制与载荷格式、SessionClient 归属与失败语义、构建门禁形态）；
  shell-binary-locking §2.1；本计划 `M5-02` 工作项。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，真实桌面，真实
  `mirage-service` 命名管道 `\.\pipe\mirage-ALKAi-service`）：
  - configure：schema v2 校验 + 工件 pin 结构校验 + 缓存 tarball sha1 复核
    （`fcefc344…`）全部通过；Debug 全树构建 0 诊断；ctest **23/23 通过
    0 skip**（新增 `bridge_core_test` 25 检查；`win32_product_process_test`
    增命名管道 SessionClient 往返场景，60 检查 0 失败）。
  - ui：`npm run check`（tsc 严格）通过；`npm test` **17 文件 539 测试通过**
    （新增 `desktop-transport.test.ts` 9 例）；`npm run lint` 0 问题；
    `npm run build` 产出 dist 供壳加载。
  - **壳内 UI 对真实 mirage-service 完成 hello / 订阅 / 任务往返**（验收）：
    壳窗口加载 `mirage://app/index.html?transport=desktop`；状态栏
    「HOST 运行中 / TRANSPORT Desktop shell / EVENTS 订阅 / PROTOCOL V1」
    （hello 身份含 events 能力通告）；composer 提交目标
    "M5-02 shell roundtrip evidence" + 步骤 `cmd /c echo m502-shell-ok`，
    UI 呈现「已提交协议任务 2958502ae02b474c8614f805f1aed0b3b（事件流实时
    返回中）」→「任务完成：1/1 个步骤成功」→ 命令执行步骤「成功」+ 执行
    结果与观察流（observe → task - Completed）。证据：维护者机器截图
    （`build/m502-shell-14.png` / `m502-final-status.png` 等，gitignore）与
    壳/服务进程日志。
- 限制与补跑条件：① Linux 壳构建与往返：CI Linux 矩阵未启用壳（默认 OFF，
  矩阵不下载 675 MB 工件、无 X11 构建依赖）——Linux 启用待 CI 矩阵补 X11
  依赖后单独评估（负责人：后续 M5 工作项实现轮）；`bridge_core_test` 与
  `session_client_test`（POSIX）已由本 PR CI 在 Linux 取证（30/30）；② react-hooks
  豁免等 M5-01 既有挂账不变；③ 事件推送背压 / CSP 收紧 / 安装器布局挂账
  DEC-019 影响节（`M5-08` / `M5-11`）。
- 同步：[DEC-019](../decisions/DEC-019-cef-shell-skeleton-and-bridge.md)
  （新增）、[dependencies.lock.json](../../dependencies.lock.json)
  （windows64 provenance 回填）、[ui/README](../../ui/README.md)
  （transport 选择补壳内条目）、ci.yml（windows 作业启用壳 + 工件缓存）、
  [总计划](mirage-implementation-plan.md) 状态叙述。

2026-09-26：`M5-02` CI 取证完成；三轮修复后 run 36189032178 全部 8 作业
success。

- 首轮（run 36184741655）：frontend 作业 strict tsc 失败——
  `desktop-transport.test.ts` 的 `noUncheckedIndexedAccess` 索引访问与
  `RequestDecode` 联合未收窄（修复 `f4e1f3f`）；本地 `npm run
  check` 经管道 tail 读取掩盖了退出码，教训留痕：验证命令须直接读退出码。
- 次轮（run 36185289907）：Linux 矩阵编译失败——`session_client_test` 只含
  `test.hpp`，而 `TempDir` / `unique_token` 在 `tests/support/ipc_io.hpp`
  （`5ed2462` 修复）；随后 debug 预设 Test 失败——事件 seq 严格单调校验在
  整表重扫的第二轮重复触发（`4af0650` 改为增量校验新到后缀）。
- 三轮（run 36189032178 前置）：release 预设暴露 timeout 场景设计缺陷——
  `Session::start` 已启动循环，300 ms 请求在快机器上必然被应答，
  fail-closed 断言永不触发（`3f08392` 改为"连接不启动循环 → 超时 → 再启动
  循环验证 ConnectionLost"）。
- 终轮（run 36189032178，headSha = `3f08392` 已核实）：全部 8 作业 success。
  - Linux 矩阵 debug / release / asan / ubsan / tsan 五预设全绿，debug 预设
    **30/30 测试 0 skip**（新增 `bridge_core_test`、`session_client_test`；
    tsan 经 `setarch -R` 正常）。
  - frontend：lint + strict tsc + **539 测试**（含 `desktop-transport` 9 例）
    + build 全绿。
  - format & public-header boundaries 绿。
  - windows msvc (full tree)：**UI 资产构建步骤首跑执行**（npm ci + build →
    壳 configure 的 dist 存在性门禁），锁定的 CEF tarball 按锁哈希缓存后
    configure 门禁 sha1 复核通过，壳目标（mirage-desktop.dll +
    bootstrap.exe 布局）构建成功；门禁测试 14/14（`dependency_lock_gate_test`
    等），`win32_product_process_test` **60 检查 0 失败**（含命名管道
    SessionClient 往返场景），产品进程往返与 windows frontend evidence
    照常通过（M4 门禁不回归）。
- 合并裁决：维护者（PR [#49](https://github.com/Linductor-alkaid/mirage/pull/49)）。

2026-09-26：`M5-03` 权限异步确认面完成。

- 范围：
  - **决策记录**：[DEC-020](../decisions/DEC-020-permission-async-confirmation.md)
    （新增，Accepted）——挂点契约由"同步 bool"演进为 `ConfirmationResult`
    （`Approved` / `Rejected` / `TimedOut` / `Unresolved` / `Cancelled`）+
    `CancelProbe`；等待预算默认 120 s（可配置）、超时收敛 fail closed；
    多连接竞争 first-response-wins（迟到应答稳定 `not_found`）；待确认态纯
    内存不持久化，`permission.list` 为重同步快照事实源；hello 新增
    `permissions` 能力通告；`resource` 上 wire 的披露边界显式化。
  - **协议 v1 附加扩展**（DEC-012 流程，传输 / 帧格式 / 版本号不变）：
    事件 `permission.request`（`request_id` / `capability` / `resource` /
    `task_id` / `timeout_ms`）、请求 `permission.respond`（成功载荷
    `{"request_id"}`）与 `permission.list`（成功载荷 `{"pending":[...]}`）；
    `mirage-ipc-protocol-v1.md` §4 / §6.1 / §6.3 / §7.2 同步，golden vectors
    `meta.version` 2 → 3（requests +2、request_failures +4、responses +3、
    response_failures +3、events +1、event_failures +4，双端门禁同一文件）。
  - **runtime/permission**：`AsyncConfirmationHub`（pinned-free 纯 std：
    mutex + promise + 切片轮询，无线程无 executor 类型）——有界等待、容量
    封顶（默认 64，饱和 fail closed `Unresolved`）、`resolve()` 互斥收敛、
    `pending()` 快照（剩余预算钳制正值）；`PermissionController::authorize()`
    增加探测参数，探测先行。判定语义、决策四值、Capability 词表不变
    （DEC-010 变更记录留痕）。
  - **runtime/service**：`ServiceConfig.confirmation_hub`（与旧 `confirmation`
    互斥，start() 校验双设与非正预算 fail closed）；hub 发布经既有 serial 域
    best-effort 路径（与任务事件同背压语义）；`handle_permission_respond` /
    `handle_permission_list`（确认面未启用 → 稳定 `unavailable`）；hello 能力
    位随 hub 装配。任务驱动器：探测 = `{desktop cancel token, executor stop
    token}`，确认等待期间取消 → 步 `cancelled` 且 permission 字段保持空
    （未判定语义）、后续 `skipped`、任务 `Cancelled`；authorize 返回后取消
    仍赢过已确认批准（无副作用）。
  - **CLI**：`mirage-service --confirm ipc --confirm-wait-ms N`；
    `mirage service start` 同步透传两旗标（本地预校验）。
- 依据：设计文档第 12、15 节；[DEC-010](../decisions/DEC-010-m1-permission-framework.md)
  （被替换条款与不变条款）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、
  [DEC-020](../decisions/DEC-020-permission-async-confirmation.md)（本工作项
  新决策记录）；本计划 `M5-03` 工作项；[前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 批准中心契约行。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，真实桌面 + 命名管道）：
  - 全树 MSVC Debug 构建 0 诊断（仅 M5-02 既有 CEF delayload 警告）；ctest
    **23/23 通过 0 skip**（`permission_test` 更新后 264 检查：新结果集 reason
    稳定串、探测先行、hub 批准/拒绝/超时/取消/容量 9 场景全绿；
    `ipc_protocol_golden_test` 359 检查含 permission 向量逐字节门禁；
    `win32_product_process_test` 等既有门禁零回归）。
  - ui：`npm run check`（tsc 严格）通过；`npm test` **17 文件 556 测试通过**
    （golden-vectors 门禁消费同一 vectors 文件，新增向量两端同绿）；
    `npm run lint` 0 问题；`npm run build` 产出 dist。
  - `mirage-format-check`（clang-format 19）与 `mirage-boundary-check`
    （39 公共头 0 违规，含新 `confirmation_hub.hpp`）本机通过。
  - **Windows 命名管道冒烟**（真实 `mirage-service --confirm ipc
    --confirm-wait-ms 8000`，CLI 往返）：启动横幅
    「confirmations: async ipc (wait 8000 ms)」；`task submit`（read 步）后
    无人应答，8 s 预算耗尽任务 Failed，`task inspect` 呈现
    `perm=confirmation_rejected` + `permission_denied: confirmation timed out
    for filesystem.read`——异步面在 Windows 传输上的超时 fail closed 与 trace
    记录成立（批准 / 拒绝 / 取消 / 多连接路径由 `permission_ipc_test` 在
    Linux CI 取证，Windows respond 路径由 hub 单元与 golden 门禁覆盖）。
- 限制与补跑条件：① Linux 矩阵（debug / release / asan / ubsan / tsan）与
  `permission_ipc_test` 集成取证随本 PR CI 执行（结论由后续记录补录）；②
  批准中心 UI 接线属 `M5-07`（本工作项交付契约面 + 服务承载 + TS 镜像）；
  ③ 确认流在 tsan 预设下的跨上下文验证同随 CI。
- 同步：[DEC-020](../decisions/DEC-020-permission-async-confirmation.md)
  （新增）、[DEC-010](../decisions/DEC-010-m1-permission-framework.md)
  （变更记录：挂点契约替换留痕）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.1 / §6.3 / §7.2 + 变更记录）、设计文档第 15 节（异步确认面
  落地叙述）、[前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 与变更记录、
  [总计划](mirage-implementation-plan.md) 状态叙述。

2026-09-26：`M5-03` CI 取证完成；run 36215388888（headSha = `c72316f`
已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿，每预设
  **31/31 测试 0 skip**（新增 `permission_ipc_test`——批准 / 拒绝 / 超时 /
  取消等待中断 / first-response-wins / `permission.list` 快照 / 面未启用
  `unavailable` / `permissions` 能力位八场景；确认等待的跨上下文收敛路径
  经 tsan `setarch -R` 验证）；format & public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 556 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 3 vectors 文件）。
- windows msvc (full tree)：14/14 测试通过，`win32_product_process_test`
  **60 检查 0 失败**、壳目标与 UI 资产构建照常（M4 / M5-02 门禁零回归）。
- 首轮 CI 修复轮（同 PR 内，提交 `8058ffe` / `706f8e6` / `8c5f54f` /
  `2dbc462` / `defe42d` / `c72316f`）：golden 测试映射补齐 permission 向量、
  clang-format 19 → 18 空花括号风格回归、POSIX 专属测试的 GCC
  `-Werror`（identity 聚合初始化缺新成员、冗余 int64 cast）、测试自身
  秒/毫秒单位比较与取消场景驱动器收敛竞态——六处均为测试/格式修正，
  实现面零改动。
- 合并裁决：维护者（PR [#50](https://github.com/Linductor-alkaid/mirage/pull/50)）。

2026-09-26：`M5-04` 会话与消息契约面完成。

- 范围：
  - **决策记录**：[DEC-021](../decisions/DEC-021-session-message-contract-face.md)
    （新增，Accepted）——`session.*` 请求面语义（状态投影、容量边界、一致性
    模型沿用 DEC-012）、消息 / 轮次 / 增量输出事件集、历史投影承载（pinned
    `MemoryEventStore` + `build_conversation_view`，投影可重建、存储唯一事实
    源）、`task.submit` 会话绑定（缺席落主会话，旧客户端零影响）、内存易失
    持久化挂账（跨重启会话历史留待设置条目定案）、DEC-008 迁移路径第一步。
  - **协议 v1 附加扩展**（DEC-012 流程，传输 / 帧格式 / 版本号不变）：请求
    `session.list` / `session.open` / `session.history`；事件 `session.updated`
    / `session.message` / `session.turn` / `session.output`；`task.submit` 可选
    `session_id` 参数与 `TaskSubmitted.session_id` 回执；hello `sessions` 能力
    通告。`mirage-ipc-protocol-v1.md` §4 / §5 / §6.1 / §6.4（新） / §7.2 / §10
    同步（顺带修正 M5-03 遗留的 §6.4→§6.3 引用错位），golden vectors
    `meta.version` 3 → 4（requests +4、responses +5、events +4 及失败向量，
    双端门禁同一文件）。
  - **integration/mira**：`SessionJournal` 适配器（pinned-free 公共头）——以
    pinned 循环同款事件载荷（`UserMessageInjected` / `LoopSettled` 信封）追加
    任务目标与结算，历史经 `build_conversation_view` 重建取最新窗口；线程安
    全（串行域 + 驱动线程并发追加）；fail closed（空载荷、畸形身份、存储
    容量饱和均显式报错）。
  - **runtime/mira_host**：pinned-free 会话面——`open_session()`（hosted 运
    行时新开会话）、`session_view()`（pinned 快照投影为稳定状态名 +
    environment epoch）、`submit_task(session, goal)` 重载、`primary_session()`；
    pinned 类型不外溢（边界门禁 39 公共头 0 违规）。
  - **runtime/service**：`ServiceConfig.max_sessions`（默认 16）/ 
    `max_history_entries`（默认 200）；会话注册表（主会话随 start() 入册）；
    三个 session 处理器 + 会话绑定提交 + 驱动接线——`session.turn` /
    `session.output` 随步结算发布（含批量 skip 轮次）、任务目标与结算以
    `session.message` 入会话（user 在 serial 域直发、outcome 随驱动结算
    best-effort）、`session.updated` 随 open 发布。EventHub 新增四个发布点，
    承载与背压语义与既有事件同路径。
  - **CLI**：`mirage session list | open | history [--limit N]`；
    `mirage task submit --session <id>`。
  - **TS 镜像**：`ui/contracts` 全量同步（类型 + 编解码 + golden 消费映射）。
- 依据：设计文档第 12.3 节（新）；[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)、
  [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  （变更记录留痕）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、
  [DEC-021](../decisions/DEC-021-session-message-contract-face.md)（本工作项新
  决策记录）；pinned 依据 `runtime.hpp` / `conversation_log.hpp` / `event_store.hpp`
  / `agent_loop.hpp` 与 `docs/api/core-runtime.md` / `model-agent-loop.md`（动工
  前核对，无能力缺口、无台账条目）；本计划 `M5-04` 工作项；
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 会话两行。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，真实桌面 + 命名管道）：
  - 全树 MSVC Debug 构建 0 诊断（仅 M5-02 既有 CEF delayload 警告）；ctest
    **24/24 通过 0 skip**（新增 `session_journal_test` 58 检查：user 往返、
    outcome 投影句式、会话语序与 `truncated` 窗口、会话独立、fail closed 六
    场景；`ipc_protocol_golden_test` **504 检查**含 v4 向量逐字节门禁；
    `mira_host_test` / `permission_test` 等既有门禁零回归）。
  - ui：`npm run check`（tsc 严格）通过；`npm test` **17 文件 581 测试通过**
    （golden-vectors 门禁消费同一 vectors 文件，v4 向量两端同绿）。
  - `mirage-format-check`（clang-format 19）与 `mirage-boundary-check`
    （39 公共头 0 违规，含新 `session_journal.hpp`）本机通过。
  - **Windows 命名管道冒烟**（真实 `mirage-service` + CLI 往返）：
    `session list` 呈现主会话（autonomous）→ `session open` 取得新会话 id →
    `session list` 双会话 → `task submit --session <id> --goal ...` 回执携带
    会话归属 → 任务终态后 `session history` 呈现
    `[1] user: read the smoke fixture` / `[2] outcome: loop settled: Failed
    (steps 1)`——会话对话投影在 Windows 传输上成立（读步因本机无头拓扑
    `filesystem provider unavailable` 结算为 Failed，属既有平台事实，非本工
    作项回归；Completed 路径由 Linux CI 的 `runtime_service_test` 取证）。
- 限制与补跑条件：① Linux 矩阵（debug / release / asan / ubsan / tsan）与
  `runtime_service_test`（session 面 8 场景：hello 能力位、主会话可见、
  open/list、会话内提交与 history 投影、limit 截断、unknown `not_found`、容量
  `unavailable`）及 `event_subscription_test`（会话事件流时序场景）随本 PR
  CI 执行（结论由后续记录补录）；② 会话页 UI 接线属 `M5-06`（本工作项交付
  契约面 + 服务承载 + TS 镜像）；③ 跨重启会话历史持久化挂账 DEC-021（待
  设置条目定案，`M5-08` 复核）。
- 同步：[DEC-021](../decisions/DEC-021-session-message-contract-face.md)
  （新增）、[DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  （变更记录：迁移路径第一步兑现留痕）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §5 / §6.1 / §6.4 / §7.2 / §10）、设计文档第 12.3 节（新增落地叙述）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 与变更记录、
  [总计划](mirage-implementation-plan.md) 状态叙述。

2026-09-26：`M5-04` CI 取证完成；run 36260655094（headSha = `b6d313a`
已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿，每预设
  **32/32 测试 0 skip**（新增 `session_journal_test`；`runtime_service_test`
  增至 228 检查——hello `sessions` 能力位、主会话可见、open/list、会话内
  提交与 history 投影、limit 截断、unknown `not_found`、容量 `unavailable`
  八场景；`event_subscription_test` 增补会话事件流时序场景；会话事件跨上下
  文发布经 tsan `setarch -R` 验证）；format & public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 581 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 4 vectors 文件）。
- windows msvc (full tree)：全树构建 + 既有门禁零回归（本机同轮取证已覆盖
  命名管道冒烟）。
- 首轮 CI 修复轮（同 PR 内，提交 `9f192bb` / `06149ea` / `d8f2d72` /
  `b6d313a`）：测试竞态修复（history 断言改为预算内轮询，等驱动线程落账
  outcome）、GCC `-Werror=missing-field-initializers` 三处（`TaskSubmitted`
  / `ServiceIdentity` 聚合初始化缺新成员，改显式成员赋值）、会话注册表容量
  接线缺失（`max_sessions` 存而未用，容量场景开放成功）——四处均为测试/
  接线修正，契约面零改动。
- 合并裁决：维护者（PR [#52](https://github.com/Linductor-alkaid/mirage/pull/52)）。

2026-09-27：`M5-05` 第一轮——工作流契约面完成（编辑器真实化与桌面原子工具
注册为该工作项第二轮，见下方挂账）。

- 范围：
  - **决策记录**：[DEC-023](../decisions/DEC-023-workflow-contract-face.md)
    （新增，Accepted）——`workflow.*` 八请求语义（草稿 = pinned
    `not_validated` 版本可解析不可运行 W-04；发布 = `publish_validated` DryRun
    门禁 + 内容寻址 + head 同容同证幂等；删除 = 产品目录条目移除、pinned 追加
    式历史不动；运行注册表容量先淘汰终态后拒绝）、`workflow.run_updated` 事件
    从 pinned 事件词表转译（不从服务侧推测）、WorkflowRuntime 服务承载（绑定
    服务进程唯一 Executor / 主会话 / 已绑定环境；teardown 在 Executor 关停前
    先经 `shutdown_workflow_surface()` 收敛，pinned 关闭顺序兑现）、TR2 取舍
    （Degraded 本轮不呈现；工具引用挂载与 Skill 注册接口缝复核后挂账）。
  - **协议 v1 附加扩展**（DEC-012 流程）：请求 `workflow.list` /
    `workflow.save` / `workflow.publish` / `workflow.delete` /
    `workflow.atom.catalog` / `workflow.runs` / `workflow.run` /
    `workflow.cancel`；事件 `workflow.run_updated`；hello `workflows` 能力
    通告。`mirage-ipc-protocol-v1.md` §4 / §6.1 / §6.5（新） / §7.2 / §10 同
    步，golden vectors `meta.version` 4 → 5（requests +8、responses +9、
    events +2 及失败向量，双端门禁同一文件）。
  - **integration/mira**：`WorkflowEventBridge` 适配器——实现 pinned
    `IEventStore`，append 透传内部有界 `MemoryEventStore`（EventStore 权威记
    录纪律），同路解码 `mira.workflow.*` 词表把 Run 级事件经 pinned-free 回调
    交付；线程安全（驱动线程并发 append），回调异常隔离计数不外溢。
  - **runtime/mira_host**：工作流承载面——`attach_workflow_surface(executor,
    bridge)`（Running 门禁 + 二次 attach fail closed）、
    `save_workflow_definition` / `publish_workflow_definition`（pinned-free，
    严格 IR 解码在 pinned 边界内）、`start_workflow_run`（库路径 W-03/W-04 +
    异步驱动；驱动准入失败对已建 run 补发 cancel 后透传拒绝）、
    `workflow_run_view` / `cancel_workflow_run`（幂等，终态不复活）、
    `shutdown_workflow_surface()`（幂等，`host.shutdown()` 兜底调用）；
    executor 仅前向声明，pinned 类型不外溢。
  - **runtime/service**：`ServiceConfig.max_workflow_definitions`（默认 128）
    / `max_workflow_runs`（默认 256）；工作流目录注册表与运行注册表（产品索
    引，进程内易失）；八个 workflow 处理器（字节预算 256 KiB 先于宿主调用、
    容量饱和 `unavailable`、未知工作流 `not_found`、非终态 run 阻断
    delete）；bridge sink 直发 EventHub（驱动线程 append，串行域重入规避），
    `workflow.runs` 为快照事实源。start() 在宿主 Running 后 attach（失败
    fail closed）；teardown 在 `executor.shutdown(true)` 前插入 workflow
    surface 收敛。
  - **CLI**：`mirage workflow list | save --file | publish --file | delete |
    atoms | runs | run <id> [--digest] [--parameters-file] [--policy] |
    cancel <run-id>`。
  - **TS 镜像**：`ui/contracts` 全量同步（类型 + 编解码 + golden 消费映射 +
    codec 边界单测）。
- 依据：设计文档第 12.4 节（新）；[DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)
  决策 1 绑定承诺兑现、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、
  [DEC-021](../decisions/DEC-021-session-message-contract-face.md)（能力位 /
  快照 vs 通知 / passthrough 先例）、[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)
  （IR 对齐验收留第二轮）；pinned 依据 `workflow_runtime.hpp` /
  `workflow_events.hpp` / `workflow_ir.hpp` / `workflow_run.hpp` /
  `workflow_versioning.hpp` / `tool_executor.hpp` 与
  `docs/api/workflow-contracts.md`、`docs/design/workflow_runtime_design.md`
  （动工前接口缝复核，见 DEC-023 背景）。**能力缺口登记**：
  [MIRA-20260927-001](../dependency_feedback/ledger.md)——同内容草稿记录遮蔽
  可运行版本（`resolve_workflow_version` 取首条匹配），受影响流为
  save→publish 同内容后按 digest 运行（fail closed，无静默错误）；本工作项
  测试以"发布内容 ≠ 草稿内容"自然流取证。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，VS 17 2022）：
  - 全树 MSVC Debug 构建 0 诊断；ctest **25/25 通过 0 skip**（新增
    `workflow_event_bridge_test` 40 检查：started/settled 转译、store 权威读
    回、非 workflow 事件静默、sink 异常隔离、批量追加、无 sink 与畸形载荷计
    数；`mira_host_test` 增至 **136 检查**——workflow surface 生命周期：attach
    状态门禁与二次 attach、save→publish→幂等重发布、草稿 W-04 透传、畸形身份
    /参数/策略 fail closed、未知工作流、异步 run + 按运行身份匹配的
    started/completed 事件、终态投影、幂等 cancel、surface 收敛后拒绝；
    `ipc_protocol_golden_test` **691 检查**含 v5 向量逐字节门禁；既有门禁零回
    归）。
  - ui：`npm run check`（tsc 严格）通过；`npm run lint` 通过；`npm test`
    **17 文件 619 测试通过**（golden-vectors 门禁消费同一 vectors 文件，v5
    向量两端同绿）；`npm run build` 通过。
  - `mirage-format-check`（clang-format 19）与 `mirage-boundary-check`
    （39 公共头 0 违规）本机通过。
- 限制与补跑条件：① Linux 矩阵（debug / release / asan / ubsan / tsan）与
  `session_client_test` 新增 `workflow_face_round_trip` 场景（活服务 + 长连
  接：hello 能力位、save/list/发布幂等、草稿 W-04 拒绝、unknown `not_found`、
  run→`workflow.run_updated` started/completed 事件流、`workflow.runs` 终态
  投影、幂等 cancel、空 atom 目录、delete 及二次 delete `not_found`）随本 PR
  CI 执行（结论由后续记录补录）；② 编辑器完整版接真实面（`WorkflowBackend`
  IPC 适配器、DEC-013 IR 对齐验收）与桌面原子工具注册（`atom.catalog` 人口 +
  ToolCall 步执行路径）属 `M5-05` 第二轮；③ 工作流库 / 注册表 / 运行注册表
  跨重启持久化随上游库存储（RISK-2026-038）与 DEC-011 设置条目定案；④
  TR2 `attach_workflow_tool_refs` / `skill_tool_registrations` 接线与工具兼
  容 Degraded wire 呈现随工具引用挂载轮评估（DEC-023 决策 4）。
- 同步：[DEC-023](../decisions/DEC-023-workflow-contract-face.md)（新增）、
  [MIRA-20260927-001](../dependency_feedback/ledger.md)（新增台账条目）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.1 / §6.5 / §7.2 / §10）、设计文档第 12.4 节（新增落地叙述）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 工作流库与运行监控两行及变更记录、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-27：`M5-05` 第一轮 CI 取证完成；run 36310217629（headSha = `66b138d`
已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / tsan / ubsan 五预设全绿，33/33 测试
  通过（含 `session_client_test` 新增 `workflow_face_round_trip` 活服务场景：
  hello `workflows` 能力位、save/list、草稿 W-04 拒绝透传、unknown
  `not_found`、publish 门禁、run → `workflow.run_updated` started/completed
  事件流、`workflow.runs` 终态投影、幂等 cancel、空 atom 目录、delete 及二次
  delete `not_found`）；format & public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 619 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 5 vectors 文件）。
- windows msvc (full tree)：全树构建 + 14/14 测试通过（`win32_product_process_test`
  新增 `workflow_face_over_named_pipe`：workflow 面经命名管道 + SessionClient
  事件流取证）。
- CI 修复轮（同 PR 内）：`bbda681`（GCC `-Werror=missing-field-initializers`
  / `range-loop-construct`，新成员聚合初始化与循环引用）、`446747e`
  （session_client_test 挂起守卫 300s → 600s，套件扩为七场景）、`cafc7a3` /
  `fd865d0`（场景失败诊断与 service 必然关停守卫）、`3cb3abe` / `f5545eb`
  （**根因修复**：pinned run-settled 载荷不含 workflow_id，桥按 run-started
  记录解析并在 settle 后退役，未命中丢弃广播——wire 非空约束不变，见 DEC-023）、
  `ecb7972`（ASAN：workflow sink 捕获 ServiceCore 强引用与 core 持有 bridge
  构成引用环，改 weak 捕获；`0d97156` shadow 改名）、`66b138d`（TSan：
  `request_shutdown` 与 teardown 并发分离/析构 ServiceLoop 的数据竞争，
  `loop` 指针原子化 + 分离/析构对 `request_shutdown` 串行化）。

2026-09-27：`M5-05` 第二轮增量 1——桌面原子工具注册（`atom.catalog` 人口 +
ToolCall 执行路径）完成（[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)
新增，Accepted；编辑器完整版接真实面为该轮剩余增量）。

- 范围：
  - **决策记录**：DEC-024——integration/mira `DesktopAtomToolset` 在绑定环境
    非 null Provider 上构建 pinned `BuiltinToolRegistry`（每 Provider 方法一
    原子，缺席不注册、目录如实反映能力，null 环境 = 空注册表）；初始 13 原
    子（观察读 4 项不经权限门，与 observe 路径同位；`filesystem.read` /
    `clipboard.read` 门禁读；7 项门禁副作用原子），捕获（Artifact 承载须走
    视觉管线）、元素动作（ElementTarget 解析缝）、指针输入（视觉参考/Overlay
    故事）与 TR2 `attach_workflow_tool_refs` / `skill_tool_registrations` 挂
    账后续轮（Degraded 呈现仍不进 wire）。
  - **执行路径**：handler 有界、不抛出；入口查 pinned `OperationContext` 取
    消探针 → `AtomPermissionGate` 回调缝（integration 声明、服务以共享
    `PermissionController` 实现，词表外与 null 控制器一律拒绝）→ Provider 调
    用；Provider 稳定错误映射 pinned 词表（safe_message 保留原 code）；结果为
    紧凑 JSON，文本预算 64 KiB（读超预算拒绝、流式截断显式置位）。进程内取
    消边界如实声明：`process.execute` 由其 1–120 s 超时预算收敛（无宿主线程
    桥接 CancelToken），run 取消在步边界收敛。副作用原子受 pinned W-02 约
    束（create_run 准入要求 verification 谓词）；DryRun 门禁只规划不派发。
  - **接线**：`MiraHost::attach_workflow_surface` 新增非空 `atom_toolset` 参
    数（编译期破坏性变更，调用点仅服务与测试），attach 时
    `set_tool_registry()` 安装；服务 `start()` 以共享 `PermissionController`
    实现权限缝构建工具集，`workflow.atom.catalog` 由空目录返回
    `exposed_atoms()` 投影。wire 契约零变更（golden 仍 meta.version 5）。
  - **执行器并发底线（CI 修复轮实证）**：pinned 驱动结构（驱动 + 步监视 +
    嵌套派发/观察）要求池以固定 min=max ≥ 4 启动——自适应 min 在 2 核机器上
    仅起 2 worker，首个 ToolCall 运行即死锁（本地 min=max=2 复现、固定 4 通
    过）；`ServiceConfig::executor_threads` 默认 2 → 4 并同时落到
    `min_threads` / `max_threads`（此前仅设 max），固定 2 线程的既有测试均
    不跑 ToolCall 运行、不受影响。
- 依据：[DEC-023](../decisions/DEC-023-workflow-contract-face.md) 决策 4 与备
  选"推迟"裁决、[DEC-022](../decisions/DEC-022-upstream-capability-adoption.md)
  决策 1（目录经 pinned exposed view 承载）、[DEC-010](../decisions/DEC-010-m1-permission-framework.md)
  （RULE-05）；pinned 依据 `tool_executor.hpp`（注册边界、schema 子集、至多
  一次派发）、`workflow_runtime.hpp`（`set_tool_registry`、派发通道、
  W-02 准入）、`workflow_ir.hpp`（ToolCall `arguments["tool"]` 绑定、谓词标
  量值、策略副作用门禁）。
- 验证（本机 Windows 11 x64，MSVC 19.44 BuildTools，VS 17 2022）：
  - 全树 MSVC Debug 构建 0 诊断 0 警告；ctest **26/26 通过 0 skip**（新增
    `desktop_atom_toolset_test` **109 检查**：空注册表、13 原子目录/排序/
    schema/副作用分级、裸环境收缩、null 门禁拒绝、门禁能力/资源/探针取证、
    观察原子无门禁、读原子预算与稳定错误、process 结果、取消探针前置拒绝、
    快照渲染、至多一次派发；`mira_host_test` 增至 **154 检查**——attach 新参
    数 null 拒绝 + ToolCall 端到端：发布 DryRun 不派发、Strict 运行副作用落
    于 fake 剪贴板、门禁翻转后同定义运行 fail closed 且剪贴板不被触碰；
    `session_client_test` 活服务目录断言更新为绑定真实能力两项原子；其余门
    禁零回归）。
  - `mirage-format-check`（clang-format 19）与 `mirage-boundary-check`
    （39 公共头 0 违规）本机通过。
- 限制与补跑条件：① Linux 矩阵（debug / release / asan / ubsan / tsan）与
  `session_client_test` / `win32_product_process_test` 更新场景随本 PR CI 执
  行（结论由后续记录补录）；② 编辑器完整版接真实面（`WorkflowBackend` IPC
  适配器 + DEC-013 IR 对齐验收）为 `M5-05` 第二轮剩余增量；③ 进程内取
  消/权限确认在 workflow ToolCall 路径的端到端交互随 M5-07 策略收紧轮复核。
- 同步：[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)（新增）、
  [DEC-023](../decisions/DEC-023-workflow-contract-face.md)（挂账节引用）、
  设计文档 §12.4（桌面原子工具集段落）、
  [总计划](mirage-implementation-plan.md) 决策表（DEC-024 行）。

2026-09-27：`M5-05` 第二轮增量 1 CI 取证完成；run 36322476594
（headSha = `3ccee92` 已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / tsan / ubsan 五预设全绿，34/34 测试
  通过（含新增 `desktop_atom_toolset_test` 与 `mira_host_test` ToolCall 端到
  端场景；`session_client_test` 目录断言更新后随套件通过）；format &
  public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 测试 + build 全绿（wire 未变，golden 仍
  v5）。
- windows msvc (full tree)：全树构建 + 14/14 测试通过。
- CI 修复轮（同 PR 内）：`2a4ecf0`（GCC `-Werror=missing-field-initializers`
  ——SemanticNode 部分花括号初始化改逐字段赋值）、`0be98c7` + `b88c165`
  （**根因修复**：ToolCall 运行需驱动 + 步监视 + 嵌套派发/观察 ≥3 并发
  worker，宿主执行器以自适应 min 启动 2 worker 即死锁——本地 min=max=2 复
  现；`ServiceConfig::executor_threads` 默认 2 → 4 且同时落 min/max 固定
  池，见 DEC-024 决策 7）、`3ccee92`（run-completed 事件先于驱动清算释放
  异步槽位，场景内第二次准入对瞬态容量拒绝做有界重试）。
- 合并裁决：维护者（PR [#55](https://github.com/Linductor-alkaid/mirage/pull/55)）。

2026-09-27：`M5-05` 第二轮增量 2——编辑器完整版接真实面（DEC-013 IR 对齐
验收；wire 契约零变更，golden 仍 meta.version 5）。

- **传输面**：`MirageTransport` 扩展八个 workflow 方法（`listWorkflows` /
  `saveWorkflow` / `publishWorkflow` / `deleteWorkflow` /
  `workflowAtomCatalog` / `listWorkflowRuns` / `startWorkflowRun` /
  `cancelWorkflowRun`）与 `workflowsSupported` 能力位（hello `workflows`）；
  Mock / dev bridge WebSocket / Desktop shell 三个传输同变更实现。mock
  服务新增工作流面（wire 忠实：注册表容量与 256 KiB 字节预算拒绝、
  NotValidated 草稿 + DryRun 门禁发布幂等、W-04 草稿拒跑透传
  `invalid_state`、W-02 未绑定验证参数的运行 fail closed、delete 守卫、
  `workflow.run_updated` 事件经既有事件面分发；目录为 M1 参考绑定两原子，
  与活服务目录断言一致，不虚构能力；`?workflows=off` 构造降级场景）。
- **IR 对齐（DEC-013 验收项）**：新增 `ui/app/src/state/workflow-ir.ts`
  ——编辑器模型 → pinned `workflow_ir.hpp` IR v1 JSON 的唯一换算点。步骤
  kind 词表与 IR v1 同构（`tool_call`/`control`，封闭集合含
  `navigate`/`verify`）；步骤带稳定 32-hex `step_id`（插入时生成，重排不
  变）；参数引用 `{"$name"}` → `{"$param":"name"}` 对象形态；前置条件为
  结构化谓词（signal/op/标量值，编辑器三字段输入）；循环构造 → `control`
  步骤 + 首步 `loop_head` 标注 + `max_iterations`；`default_policy` 恒
  `strict`、`allowed_policies = ["strict","dry_run"]`。
- **W-02 验收（DEC-024 决策 5）**：副作用原子（wire 目录
  `has_side_effects`）步骤自动落 verification 谓词
  `run_parameter:ok_<stepid6> eq true`，并自动派生同名可选 boolean 参数
  （无默认值）——发布 DryRun 空参绑定下谓词 NotEvaluable 计数通过
  （RULE-10），Strict 运行须绑定参数否则该步 fail closed，与 C++ 侧
  `mira_host_test` 已验证形态一致。
- **适配器与 store**：`WorkflowBackend` 接口重写为 `IpcWorkflowBackend`
  （三个传输共用；目录懒取缓存）。store 按 hello `workflows` 能力位接
  契约路径：运行监控事件化（`workflow.run_updated` → 快照刷新，
  `workflow.runs` 为事实源；无事件能力时有未终态运行则轮询兜底）；模拟
  推进（`advanceRun`）、`MockWorkflowBackend`、静态 `ATOM_CATALOG` 与
  `seedWorkflows` 移除（工作流域不再属于模拟域）；运行状态视图新增
  `queued`/`paused` 投影。**已知边界（如实声明）**：wire 无定义读取面，
  `workflow.list` 只投影摘要——编辑器仅可编辑本会话创建/保存过的定义
  （`contentKnown` 标记 + head digest 合并保留会话副本），其余条目只读，
  防止以空内容遮蔽服务端 head（W-03 内容寻址；MIRA-20260927-001 同源
  风险）；定义读取面属后续协议附加扩展挂账（与跨会话编辑、M5-06 观察
  台共同评估）。运行参数绑定入口未随本轮交付（副作用工作流当前以缺参
  运行会按 W-02 fail closed，属如实呈现）；参数化运行对话框挂账
  M5-06/编辑器后续轮。
- 验证（本机 Windows 11，Node 22 / vitest）：eslint 0 告警；contracts +
  app 双包 `tsc --noEmit` 0 诊断；前端全量 **630 测试 / 19 文件通过**
  （新增 contracts `workflow-mock.test.ts` 8 项、ws-transport workflow 面
  6 项（帧形状 + 载荷路由 + 稳定错误）、app `workflow-ir.test.ts` 8 项
  （IR 形态、W-02 谓词派生、loop_head、谓词标量解析）、
  `workflow-backend.test.ts` 7 项（目录投影/缓存、W-04/W-03 语义、运行
  生命周期）、store-dom 工作流组 7 项重写为契约路径（种子投影、
  `contentKnown` 只读守卫、发布翻转、事件化完成、删除路由回退））；
  `vite build` 成功（chunk 体积告警为既有情况）。C++ / wire 侧零变更，
  golden vectors 消费面（TS 侧）随套件通过；Linux/Windows 矩阵随本 PR CI
  执行（结论由后续记录补录）。
- 依据：[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)（编辑器
  IR 对齐约束）、[DEC-023](../decisions/DEC-023-workflow-contract-face.md)
  （wire 面与能力位）、[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)
  （目录 exposed view 承载 + 决策 5 W-02 验收）；pinned 依据
  `workflow_ir.hpp`（IR v1 封闭词汇与谓词形状）、`workflow-contracts.md`
  （`arguments["tool"]` 绑定、W-02 准入）。
- 同步：[DEC-023](../decisions/DEC-023-workflow-contract-face.md)（挂账节：
  编辑器接真实面完成、定义读取面挂账登记）、
  [DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)（决策 5 验收指向
  本增量）、`ui/README.md`（接口缝说明）、设计规范 §3.5 目录来源措辞
  （wire 目录 + 编辑器控制构造）。

2026-09-27：`M5-05` 第二轮增量 2 CI 取证完成；run 36327115131
（headSha = `e55bd10` 已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / tsan / ubsan 五预设全绿；format &
  public-header boundaries 绿（C++ / wire 零变更，零回归）。
- frontend 作业：lint + strict tsc + 测试 + build 全绿（wire 未变，golden 仍
  v5；新增 workflow 传输 / IR 映射 / 契约路径 store 场景随套件通过）。
- windows msvc (full tree)：全树构建 + 测试通过。
- 合并裁决：维护者（PR [#56](https://github.com/Linductor-alkaid/mirage/pull/56)，2026-09-27 授权合并）。

2026-09-27：`M5-06` 第一轮——会话页接真实面（消费 `M5-04` 已交付的
`session.*` 契约面；wire 契约零变更，golden 仍 meta.version 5；
[DEC-025](../decisions/DEC-025-session-page-productization.md) 新增，Accepted）。

- 范围：
  - **决策记录**：DEC-025——会话列表 = `session.list` 快照事实源 +
    `session.updated` 通知（标题为展示层派生：首条 user 消息或会话 id 前
    8 位；重命名 / 置顶 / 删除 / 导出 / fork 无 wire 面，UI 不呈现，挂账
    协议扩展评估）；消息流 = `session.history` 重同步基线 +
    `session.message` 增量 + 任务快照步骤卡，`session.turn` / `session.output`
    不入线程（与步骤卡同源，避免重复呈现）、作为观察流事件源；**对话模式
    如实降级**（模型循环未引入，DEC-008 迁移路径第二步，Composer 保留双模式
    信息架构、对话入口禁用并标注原因，`ChatSimulator` 与回复语料删除）；
    模拟域退役边界（`seedSessions` / 演示审批叠加 / `approve:` 约定 / 上下文
    占用 meter 无契约承载撤除；批准中心真实接线属 `M5-07`）；观察台 =
    时间线（任务快照）+ 观察流（session.turn / output / message 有界环形
    缓冲，通知面语义，丢帧不回补）；观察面协议附加扩展（视觉状态呈现，
    M3 非目标完整兑现）挂账另立增量。
  - **传输面**：`MirageTransport` 扩展 `sessionsSupported` 能力位与
    `listSessions` / `openSession` / `sessionHistory` 三方法，
    `SubmitTaskInput` 增加可选 `session_id`；mock / dev bridge WebSocket /
    Desktop shell 三个传输同变更实现。mock 服务新增 wire 忠实会话面（主
    会话随构造入册、`session.open` 容量 16 饱和 `unavailable`、
    `session.history` 默认 50 窗口 + `truncated`、user / outcome 句式
    `loop settled: <progress> (steps N)` 与单调 sequence、turn / output 每
    步结算发布语义——无结果步骤不发布 output、失败批跳步发布 skipped
    turn、`session.updated` 随 open、`?sessions=off` 降级构造）。
  - **store**：会话 / 消息全部接契约路径——`session.list` 快照 +
    派生字段保留本地记忆；`selectSession` 拉 `session.history` 重建线程
    journal 部分（按 sequence 幂等去重，时间归位，`truncated` 呈现截断）；
    `submitExec` 携带 `session_id` 绑定并在成功后拉历史（事件面缺席时线程
    仍完整）；`session.message` 事件增量入线程 + 首条 user 消息派生标题 +
    按 sequence 去重；`session.turn` / `output` / `message` 进每会话有界
    （40 帧）观察流缓冲，turn 附带触发任务快照刷新。模拟域退役：
    `harness-mock.ts`（种子会话 / `ChatSimulator` / 演示审批 /
    `exportSessionMarkdown`）删除。
  - **视图**：SessionsSidebar 重做（真实会话列表 + 派生标题 + 搜索 +
    今天 / 近 7 天 / 更早分组 + 任务徽标；管理菜单移除）；Composer 对话
    模式降级呈现（禁用 + 原因标注），执行模式保留；Observer 观察流改真实
    事件帧、上下文占用 meter 撤除、trigger-lock 以渲染期状态调整模式重写；
    messages.tsx 渲染器收敛为 user / outcome / step / activity / system
    五类（模拟域卡片随域退役）；Overlays 命令面板会话项接派生标题、批准
    中心转 M5-07 占位面板；WallDisplay 批准灯阵转 M5-07 占位。
  - **react-hooks 豁免清零（M5-01 挂账兑现）**：M1.5 既有视图的 7 处发现
    以注入时间源（`useNow` hook，定时器回调推进状态）与 React 官方渲染期
    状态调整模式修复，`react-hooks/purity` / `set-state-in-effect` 两规则
    移除豁免、全量生效（`eslint.config.js` 留痕注释）。
- 依据：[DEC-021](../decisions/DEC-021-session-message-contract-face.md)
  （被消费契约面）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （一致性模型）、[DEC-025](../decisions/DEC-025-session-page-productization.md)
  （本工作项新决策记录）、[DEC-023](../decisions/DEC-023-workflow-contract-face.md)
  （mock 面纪律先例与挂账批次）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §3.3 / §4；本计划 `M5-06` 工作项。
- 验证（本机 Windows 11，Node 22 / vitest）：
  - contracts：`session-mock.test.ts` 新增 10 项（主会话入册与列表投影、
    容量 `unavailable`、`session.updated` 仅随 open、默认绑定落主会话、
    显式绑定与 unknown `not_found`、history 窗口 / `truncated` / sequence、
    turn/output 每步发布语义、无结果步骤不发布 output、`?sessions=off`）；
    ws-transport 增 4 项（session 三方法帧形状 + `session_id` 映射 +
    稳定错误路由）；desktop-transport 增 2 项（会话三方法 + `session_id`）；
    mock-service 既有 26 项按会话事件流更新（含能力位断言）。
  - app：`store-dom.test.ts` 重写为 23 项——模拟域测试段落退役
    （decideApproval / sendChat / 会话管理本地形态），新增会话面契约路径
    （open 导航与刷新、容量 `unavailable` 显式 toast、无 `sessions` 能力位
    拒绝、history 幂等重建与派生标题、`stopSession` 取消绑定任务）与会话
    事件增量（message 入线程 + 标题派生 + sequence 去重、turn/output 进
    观察流 + 触发快照刷新）；store-connection 10 项随会话面适配全绿。
  - eslint 0 告警（purity / set-state-in-effect 全量生效）；contracts +
    app 双包 `tsc --noEmit` 0 诊断；前端全量 **625 测试 / 18 文件通过**；
    `vite build` 通过。C++ / wire 侧零变更，golden vectors 消费面（TS 侧）
    随套件通过；Linux / Windows 矩阵随本 PR CI 执行（结论由后续记录补录）。
- 限制与补跑条件：① 观察面（视觉状态呈现 / `visual_snapshot_ref` 进观察
  面）需协议附加扩展与驱动器观察投影评估，golden v5 → v6，另立增量
  （DEC-025 挂账①）；② 会话管理面（重命名 / 删除 / 导出 / fork）挂账
  DEC-025 挂账②；③ 对话模式真实化（pinned `AgentLoop` 纯对话形态）挂账
  DEC-025 挂账③（DEC-008 迁移路径第二步，与设置-模型类目联动）；
  ④ Linux 矩阵与 windows 作业随本 PR CI 取证。
- 同步：[DEC-025](../decisions/DEC-025-session-page-productization.md)（新增）、
  [DEC-021](../decisions/DEC-021-session-message-contract-face.md)（变更记录：
  UI 消费留痕）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §3.3 / §4 与变更记录、ui/README（会话面接口缝）、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-27：`M5-06` 第一轮 CI 取证完成；run 36333683533
（headSha = `e69bb5e` 已核实）全部 8 作业 success。

- Linux 矩阵：debug / release / asan / tsan / ubsan 五预设全绿；
  format & public-header boundaries 绿（C++ / wire 零变更，零回归）。
- frontend 作业：lint（含 react-hooks `purity` / `set-state-in-effect`
  全量生效）+ strict tsc + **625 测试**（新增会话面传输 / mock / store
  契约路径场景）+ build 全绿（wire 未变，golden 仍 v5）。
- windows msvc (full tree)：全树构建 + 测试通过。
- 合并裁决：维护者（PR [#57](https://github.com/Linductor-alkaid/mirage/pull/57)）。

2026-09-28：`M5-06` 第二轮——观察面协议扩展批次完成（协议 v1 附加扩展，
[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md) 新增，
Accepted；golden `meta.version` 5 → 6；CI 结论按仓库先例由下一工作项 PR 补录）。

- 范围：
  - **决策记录**：DEC-026——批次构成评估（观察面 / 定义读取面进入，步级运
    行事件与会话管理面评估后挂账）；观察面 = `desktop.observe` 按需请求，
    无事件形态（M1 驱动形态无观察生产者，驱动器观察数据投影评估结论）；
    请求即必须（被请求组件不可交付即整个请求 `unavailable`，不静默残缺）；
    语义快照投影 1024 节点 wire 预算 + `truncated` 显式标记；`workflow.get`
    = 服务侧产品目录保留 head 定义正文（pinned 库无正文读取 API 是 W-03 上
    游设计非缺口）；会话管理面上游核对结论（`session.close` 挂账、重命名
    产品别名 M5-08、导出客户端投影、fork 不立项）。
  - **协议 v1 附加扩展**（DEC-012 流程，传输 / 帧格式 / 版本号不变）：请求
    `desktop.observe` / `workflow.get`；hello `observation` 能力通告；
    `mirage-ipc-protocol-v1.md` §4 / §6.1 / §6.6（新）/ §10 同步，golden
    vectors `meta.version` 5 → 6（requests +3、request_failures +4、
    responses +4、response_failures +5，双端门禁同一文件）。
  - **runtime/service**：观察处理器（serial 域经 `ObservationAssembler` 按
    需捕获，视觉组件自 `ServiceConfig.visual_registry` 显式接线的注册表投
    影，未接线即 fail closed；组件错误映射稳定 `unavailable` 并命名原因）；
    `workflow.get` 处理器（产品目录条目扩展 head 定义正文的保留，
    save/publish 同步写入）；hello `observation = true`。
  - **TS 镜像**：`ui/contracts` 全量同步（类型 + 编解码 + 三个 transport +
    golden 消费映射）；mock 服务新增 wire 忠实观察面（确定性模拟桌面数据，
    `observationCapability` 降级构造 = 旧服务 unknown-op 形态）与定义读取
    面（注册表内容回读 + unknown `not_found`）。
  - **UI 消费**：观察台新增"桌面状态"面板（`desktop.observe` 按需快照，视
    觉组件显式请求、错误如实呈现——M3 非目标"视觉参考进入 UI 观察面"兑
    现）；工作流编辑器 `openWorkflowEditor` 经 `workflow.get` 跨会话回读水
    合（`irToWorkflowDef` 逆映射，摘要投影字段仍以 `workflow.list` 为准，
    回读失败保持只读并显式提示）——DEC-023 挂账①兑现，跨会话编辑限制解
    除；`contentKnown` 守卫文案同步。
- 依据：[DEC-025](../decisions/DEC-025-session-page-productization.md) 挂账
  ①②、[DEC-023](../decisions/DEC-023-workflow-contract-face.md) 挂账①、
  [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （附加扩展流程与一致性模型）、
  [DEC-016](../decisions/DEC-016-mirador-visual-integration-contract.md)
  （视觉组件投影 / 注册表承载 / 不请求不点亮）、pinned 依据 `runtime.hpp`
  （会话面核对：无 rename/fork，有 close_session）、`workflow_versioning.hpp`
  （版本记录无正文核实）、`workflow_events.hpp`（步级事件词表核实）；
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 契约映射；本计划 `M5-06` 工作项。
- 验证（本机 Linux，GCC 13.3 / Node 22）：
  - 全树 debug 构建零告警；ctest **40/40 通过 0 skip**（新增
    `observation_face_test` **47 检查**——Xvfb + AT-SPI 活拓扑上经 IPC 完成
    hello `observation` 能力位、真实语义树投影（fixture 四节点 / focused
    元素 / 标题）、`semantic=false` 跳过投影、visual 无注册表 fail closed
    命名组件原因、注册表接线后 `@vs1` / `@v1` 视觉承载；`runtime_service_test`
    增无头拓扑 fail closed 与 `workflow.get` unknown `not_found` 两场景；
    `ipc_protocol_golden_test` **845 检查**含 v6 向量逐字节门禁；既有门禁零
    回归）。
  - `mirage-format-check`（clang-format）本机通过。
  - ui：contracts + app 双包 `tsc --noEmit` 0 诊断；`npm test` **18 文件
    658 测试通过**（golden-vectors 门禁消费同一 v6 vectors 文件，149 用例
    两端同绿；新增 mock 观察面 / 定义读取面、ws 与 desktop 传输帧形状、
    store 观察状态机与编辑器回读水合、`irToWorkflowDef` 往返场景）；
    `npm run lint` 0 告警；`npm run build` 通过。
  - Linux 五预设矩阵 / windows 作业随本 PR CI 取证（结论由后续记录补录）。
- 限制与补跑条件：① 产品 `mirage-service` 未接线视觉管线，
  `desktop.observe` 的视觉组件在产品拓扑下如实 `unavailable`（wire 面已
  定形，点亮挂账 DEC-026 挂账①，与 Overlay / 采集产品化联动）；② 观察
  捕获在 Windows 传输上的取证随 windows 作业 CI；③ `session.close` wire
  面、步级运行事件、对话模式真实化、会话历史持久化分别挂账 DEC-026 /
  DEC-023 / DEC-025 挂账，不阻塞本工作项收口。
- 同步：[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  （新增）、[DEC-025](../decisions/DEC-025-session-page-productization.md)
  （挂账①②兑现与结论留痕）、
  [DEC-023](../decisions/DEC-023-workflow-contract-face.md)（挂账①兑现留
  痕）、[mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.1 / §6.6 / §10）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 与变更记录、ui/README（观察面与定义读取面接口缝）、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-28：`M5-06` 第二轮 CI 取证完成（补录）；run 36383958688
（headSha = `02984f5`，PR [#58](https://github.com/Linductor-alkaid/mirage/pull/58)）
全部 8 作业 success，2026-09-28 合并（维护者授权）。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿，每预设
  **34/34 测试 0 skip**（含第二轮新增 `observation_face_test` 与
  `runtime_service_test` 观察面 / 定义读取场景；tsan 经 `setarch -R`）；
  format & public-header boundaries 绿（39 公共头 0 违规）。
- frontend 作业：lint + strict tsc + 665 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 6 vectors 文件）。
- windows msvc (full tree)：全树构建 + 测试通过（win32 观察面端到端
  74 检查 0 失败）；**M5-06 第二轮验证记录的补跑条件①②随本 runs 关闭**
  （观察捕获经 Windows 传输的取证由 win32 端到端与全树门禁覆盖）。
- 独立测试验证（第 1 轮）新增用例随本 runs 取证：
  `workflow_get_serves_head_definition_content` /
  `observe_wire_budget_truncates_with_explicit_mark` /
  `session-close` 前置 golden response_failures 六向量 /
  ws-transport desktop.observe 缺省帧形（见 02984f5）。

2026-09-28：`M5-06` 第三增量——会话管理面 `session.close` 完成（协议 v1 附
加扩展，DEC-026 挂账②兑现；golden `meta.version` 6 → 7；本 PR 的 CI 结论按
仓库先例由下一工作项 PR 补录）。

- 范围：
  - **协议 v1 附加扩展**（DEC-012 流程，传输 / 帧格式 / 版本号不变）：请求
    `session.close`（`session_id` 非空）+ 回执 `SessionClosed`
    `{session_id, state}`（沿用 `workflow.cancel` 回执形状先例，信封与
    `session.open` 可判别）；`session.updated` 增补关闭发布点；
    `mirage-ipc-protocol-v1.md` §4 / §6.4 / §7.2 / §10 同步，golden vectors
    `meta.version` 6 → 7（requests +1、request_failures +2、responses +1、
    response_failures +2，双端门禁同一文件）。
  - **runtime/mira_host**：`close_session()`（Running 门禁 + pinned
    `close_session` 命令 outcome 等待，Applied / NoOp 皆视为关闭成功——
    pinned 幂等；pinned 拒绝透传）。
  - **runtime/service**：`handle_session_close`——主会话守卫（`task.submit`
    缺席默认绑定锚点，`invalid_state` "the primary session cannot be
    closed"）；未知 id 稳定 `not_found`；该会话未结算任务的驱动走
    `task.cancel` 同款协作中断（cancel token + executor task cancel）后在
    pinned 边界取消；成功后注册表条目移除（容量可复用）、发布
    `session.updated {state}`、回执携带关闭后状态（投影视图可读取实时名，
    否则取关闭命令自身收敛后条件 `closed`）。
  - **TS 镜像 + mock**：`MirageTransport.closeSession`，dev bridge /
    Desktop shell / mock 三传输同变更；mock 会话面新增 wire 忠实关闭（主
    会话守卫 / unknown `not_found` / 注册表移除 + 关闭通知；构造期主会话
    id 显式锚定）。
  - **UI**：会话页签派栏新增每会话操作菜单（删除 = `session.close`，两段
    确认）；成功后本地线程 / 草稿 / 截断标记 / 观察流 / 提交入参记忆随注
    册表事实收敛清空，当前路由为被删会话时回退默认路由；主会话拒绝以稳定
    错误如实呈现（`invalid_state` 映射说明文案）。
- 依据：[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  挂账②与决策 5 会话管理面评估结论、[DEC-021](../decisions/DEC-021-session-message-contract-face.md)
  （会话面先例）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （附加扩展流程）、pinned 依据 `runtime.hpp` `close_session` 命令语义与
  `core_contracts.hpp` 会话状态机（Opening/Autonomous/TakeoverPending/
  HumanControlled → Closing，Closed 幂等 NoOp，Failed 终态不可关闭）；
  本计划 `M5-06` 工作项。
- 验证（本机 Linux，GCC 13.3 / Node 22）：
  - 全树 debug 构建零告警；ctest **40/40 通过 0 skip**（`runtime_service_test`
    增至 **313 检查**——新场景 `session_close_lifecycle`：主会话守卫、
    unknown `not_found`、open→close→list 收缩、二次 close `not_found`、容
    量复用；`ipc_protocol_golden_test` **875 检查**含 v7 向量逐字节门禁；
    既有门禁零回归）。
  - `mirage-format-check` 与 `mirage-boundary-check`（39 公共头 0 违规）本
    机通过。
  - ui：`npm run check`（tsc 严格）0 诊断；`npm test` **18 文件 678 测试通
    过**（golden-vectors 消费同一 v7 文件 161 用例两端同绿；新增 mock 关闭
    面 3 项、ws-transport 帧形状 2 项、store 删除会话 2 项）；`npm run
    lint` 0 告警；`npm run build` 通过。
  - Linux 五预设矩阵 / windows 作业随本 PR CI 取证（结论由后续记录补录）。
- 限制与补跑条件：① 主会话守卫为产品侧策略（`task.submit` 缺席默认绑定
  锚点，M5-04/DEC-021 契约不变式），pinned 层无此概念——如未来引入"关闭
  主会话后自动重开"的产品语义需另立决策；② 被关闭会话的本地会话历史
  （journal）随 pinned Closed 收敛，跨重启持久化仍属 DEC-021 挂账（M5-08）；
  ③ 重命名（产品别名）/ 导出（客户端投影）/ fork 维持 DEC-026 决策 5 结论；
  对话模式真实化挂账 DEC-025 挂账③。
- 同步：[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  （挂账②兑现留痕）、
  [DEC-025](../decisions/DEC-025-session-page-productization.md)（挂账②删
  除增量兑现留痕）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.4 / §7.2 / §10）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 与变更记录、ui/README（会话管理面接口缝）、
  [总计划](mirage-implementation-plan.md) 状态叙述。

2026-09-28：`M5-06` 第三增量 CI 取证完成（补录）；run 36391609320
（headSha = `32868cb`，PR [#59](https://github.com/Linductor-alkaid/mirage/pull/59)）
全部 8 作业 success，2026-09-28 合并（维护者授权）。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿，每预设
  **34/34 测试 0 skip**（含 `session_close_lifecycle` 场景）；format &
  public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 702 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 7 vectors 文件）。
- windows msvc (full tree)：全树构建 + 测试通过。
- 独立测试验证（第 1 轮）新增用例随本 runs 取证：
  `session_close_cancels_inflight_task` /
  `session_close_publishes_closed_notification` / golden
  `session-closed-closing-state` 与 `session-closed-empty-session-id` /
  desktop-transport session.close 两用例（见 32868cb）。

2026-09-28：`M5-06` 第四增量——对话模式真实化完成（协议 v1 附加扩展，
[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md) 新增，Accepted；
golden `meta.version` 7 → 8；本 PR 的 CI 结论按仓库先例由下一工作项 PR 补
录）。**`M5-06` 工作项至此收口**（四增量：会话页接真实面 / 观察面协议扩
展 / 会话管理面 / 对话模式真实化）。

- 范围：
  - **决策记录**：DEC-027——纯对话形态定位（Text 契约单轮 ModelRequest →
    ModelGateway 推理，不复用桌面 AgentLoop）；对话线程承载边界（服务内
    存易失 turn 日志，不进 pinned 会话投影——`build_conversation_view` 无
    助手词条，伪造 user/outcome 即语义伪造）；SecretRef 解析源 = 进程环境
    变量（传输边界解析，fail closed）；测试承载 = 注入式
    `ModelProviderOverride`（生产 provider 的 SSRF 姿态使回环端点不可达，
    pinned 安全设计非配置缺口）；DEC-026 挂账④联动评估维持挂账（纯对话
    无桌面驱动步）；M5-08 联动评估以 `ModelLayerConfig` 面为契约输入。
  - **模型层装配**：`integration/mira::ModelLayer`——pinned `ModelProfile`
    + `ModelRouter` + `OpenAiCompatibleProvider` + `SocketHttpTransport`
    （Executor blocking worker 承载）+ `ModelGateway`；TLS 通道挂接
    pinned OpenSSL 适配器（目标存在时），https 无通道 fail closed 永不降
    级；`ServiceConfig::model` + `--model-*` CLI 旗标（缺省禁用，启用但
    无效 fail closed）。
  - **协议 v1 附加扩展**（DEC-012 流程，传输 / 帧格式 / 版本号不变）：请求
    `session.chat` / `session.chat.history`；事件 `session.chat_updated`
    （pending → ok / failed，reply_text / error encode-when-set 成对校
    验）；hello `chat` 能力位（模型层装备依赖，`permissions` 位先例）；
    `mirage-ipc-protocol-v1.md` §4 / §6.1 / §6.7（新）/ §7.2 / §10 同步，
    golden `meta.version` 7 → 8。
  - **runtime/service**：`handle_session_chat`（受理即回执 + 可取消任务承
    载推理 + 每会话单在途闩锁 + 16 KiB 文本预算）/
    `handle_session_chat_history`（快照事实源）；DialogRegistry（容量 =
    max_sessions，每线程 200 轮最旧裁剪 + truncated 显式，会话关闭随之移
    除）；teardown 在 Executor 关停前收敛模型层传输（pinned 关闭顺序）。
  - **TS 镜像 + mock**：`MirageTransport.chatSupported` / `sessionChat` /
    `sessionChatHistory`，dev bridge / Desktop shell / mock 三传输同变
    更；mock 对话面（确定性模拟回复、pending → ok 生命周期、
    `chatCapability=false` 降级构造）。
  - **UI**：Composer 对话模式启用（`chat` 能力位 gate；未配置保持如实降
    级并说明）；对话线程投影（用户行 + 助手行：pending 思考中 / ok 回复 /
    failed 稳定错误），随 session.chat_updated 收敛、随
    session.chat.history 重建；在途轮禁再提交。
- 依据：[DEC-025](../decisions/DEC-025-session-page-productization.md) 挂
  账③、[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  挂账④联动评估、[DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  迁移路径第二步、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  附加扩展流程；pinned 依据 `model_profile.hpp` / `model_gateway.hpp` /
  `model_provider.hpp` / `model_transport.hpp` / `adapters/net/`（SSRF 姿
  态与 TLS 门禁核实）、`conversation_log.hpp`（投影无助手词条核实）、
  `agent_loop.cpp`（ModelRequest 装配先例）；本计划 `M5-06` 工作项。
- 验证（本机 Linux，GCC 13.3 / Node 22）：
  - 全树 debug 构建零告警；ctest **40/40 通过 0 skip**（`runtime_service_test`
    增至 **362 检查**——新场景 `session_chat_dialog_face`（注入 provider）：
    hello 能力位、unknown `not_found`、turn 受理 → 收敛 ok → 回执文本、
    in-flight 闩锁清除、第二轮受理、无模型层 `unavailable`；`ipc_protocol_golden_test`
    **999 检查**含 v8 向量逐字节门禁；既有门禁零回归）。
  - `mirage-format-check` 与 `mirage-boundary-check`（39 公共头 0 违规）
    本机通过。
  - ui：`npm run check`（tsc 严格）0 诊断；`npm test` **18 文件 702 测试
    通过**（golden-vectors 消费同一 v8 文件 180 用例两端同绿；新增 mock
    对话面、ws/desktop 传输帧形状、store 对话线程收敛与稳定错误呈现）；
    `npm run lint` 0 告警；`npm run build` 通过。
  - Linux 五预设矩阵 / windows 作业随本 PR CI 取证（结论由后续记录补录）。
- 限制与补跑条件：① 真实模型端点连通性（公网 https + TLS + 真实凭据）未
  被 CI 覆盖——pinned provider 的 SSRF 姿态拒绝回环/私网端点，CI 以注入
  provider 取证对话流程，真实端点属部署态取证（DEC-027 挂账①，维护者机
  器 / 打包轮）；② https 在 Windows 默认构建 fail closed（Mbed TLS 适配
  器 `MIRAGE_WITH_MIRA_MBEDTLS` 门控默认关闭，DEC-017 双工具链行为一致）；
  ③ 凭据承载升级（keyring/secret 服务）、对话流式输出、多轮上下文策略挂
  账 DEC-027 挂账②③④。

2026-09-28：`M5-06` 第四增量独立测试验证（第 1 轮）缺陷修复完成（`fix(runtime)`，
同 PR 补充提交）。

- 独立验证发现两处服务侧实现缺陷（实测取证，未代改实现）：
  1. `settle_dialog_turn` 从未赋值 settled `session.chat_updated` 事件的
     `status` / `user_text`（pending 事件正确），live wire 帧为空串——任
     何订阅端 decode 拒绝，事件驱动收敛失效，只能靠快照重同步；
  2. `handle_session_close` 不移除对话注册表条目——容量（= max_sessions）
     随 open→chat→close 循环永久耗尽，对话面对新会话 `unavailable`，违反
     "会话关闭时其对话线程随之移除" 的 DEC-027 契约文本。
- 修复：settle 路径在注册表锁内补写 `event.status` / `event.user_text` /
  `event.sequence`（自登记 turn 记录取值），未知 turn id 不再发布；close
  路径同步擦除对话注册表条目并取消在飞对话轮任务（其 settle 发现日志已
  移除即吞没——原"死代码"分支由此成为关闭会话竞态的活路径）。
- 回归用例：`event_subscription_test` 恢复独立验证轮注释占位的 settled 断
  言（ok 帧 status / user_text / reply_text / sequence 逐项 + pending 先于
  settled 的流序）；`runtime_service_test` 新增
  `session_close_frees_dialog_registry_slot`（max_sessions=2 下两轮
  open→chat ok→close 后第三会话对话仍可用——泄漏回归在此表现为
  `unavailable: dialog registry capacity exhausted (2)`）。
- 验证：`runtime_service_test` **430 检查 0 失败**、`event_subscription_test`
  **426 检查 0 失败**（均本机实测）；全树 debug 构建 0 错误，ctest 40/40
  通过 0 skip，format / boundary（39 公共头）通过。
- 同步：[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)（新增）、
  [DEC-025](../decisions/DEC-025-session-page-productization.md)（挂账③兑
  现留痕）、[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  （挂账④联动评估结论留痕）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.1 / §6.7 / §7.2 / §10）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §3.3 / §4 与变更记录、ui/README（对话面接口缝）、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-28：`M5-06` 第四增量 CI 取证完成（补录）；run 36418679167
（headSha = `7f8d667`，PR [#60](https://github.com/Linductor-alkaid/mirage/pull/60)）
全部 8 作业 success，2026-09-28 合并（维护者授权）。

- 首轮 CI 暴露三处问题并同 PR 修复（`4d8b92d` / `7f8d667`）：
  windows msvc——MSVC 将 CRT getenv 视为弃用（C4996→C2220），按
  runtime/persistence 平台分 TU 先例拆分 model_layer_env_{posix,windows}.cpp；
  首轮修复引入 C2664（const wstring data() 非 LPWSTR），去 const 后 MinGW
  交叉编译 Built target；tsan/asan——`session_chat_dialog_face` 场景的在飞
  infer 与 teardown 的 ModelLayer::shutdown 竞争（gateway/provider/transport
  UAF + BudgetLedger/ProviderCircuit data race），ModelLayer 增 infer_mutex
  排水锁（shutdown 等待在飞推理完成再释放）。
- 终轮（run 36418679167）：Linux 五预设全绿（34/34），format / boundaries
  绿，frontend 707 测试绿，windows 全树构建 + 测试通过。
- 独立测试验证（第 2 轮）新增用例随本 runs 取证：
  `session_close_swallows_inflight_turn`（GatedDialogProvider 门控 + 静默
  窗口断言）、`session_chat_publishes_turn_lifecycle` settled 断言恢复、
  `session_close_frees_dialog_registry_slot` 复验（见 e899c3d）。

2026-09-28：`M5-07` 批准中心与权限管理产品化完成（协议 v1 附加扩展，
[DEC-028](../decisions/DEC-028-permission-policy-face.md) 新增，Accepted；
DEC-010 / DEC-011 演进修订随实现登记；golden `meta.version` 8 → 9；本 PR
的 CI 结论按仓库先例由下一工作项 PR 补录）。

- 范围：
  - **批准中心接线（DEC-020 面消费）**：Overlays ApprovalsCenter 从占位转
    正——`permission.list` 快照事实源 + `permission.request` 事件触发重
    取，逐条呈现能力 / 资源 / 任务 / 剩余预算，批准 / 拒绝经
    `permission.respond`（先到先得，`not_found` 静默收敛）；WallDisplay 批
    准灯阵接真实挂起计数；TS 传输缺口补齐（`permissionList` /
    `permissionRespond` / `permissionsSupported`——M5-03 只交付了
    types/codec，MirageTransport 从未暴露确认面）。
  - **设置页权限矩阵**：Provider × 占位矩阵（SimTag）替换为 DEC-010 十一
    能力真实矩阵——`policy.get` 事实源、变更经 `policy.set` 全量应用；规
    则立即生效，read roots 资源范围重启生效（如实声明）；策略面缺席如实
    降级。
  - **协议 v1 附加扩展**（DEC-012 流程）：请求 `policy.get` / `policy.set`
    （PolicyView：全 DEC-010 规则集 + read_roots 资源范围）；hello
    `policy` 能力位（核心装备恒真）；`mirage-ipc-protocol-v1.md` §4 /
    §6.1 / §6.8（新）/ §10 同步，golden `meta.version` 8 → 9。
  - **服务承载**：`PermissionController` 增线程安全 `policy()` /
    `set_policy`（authorize 改经加锁快照）；`handle_policy_get` /
    `handle_policy_set`（全量覆盖校验 `invalid_argument`；规则立即生效；
    settings 读-改-写持久化——已存文档不可信拒绝写入，保留非本面成员）；
    `ServiceConfig` 增 `read_roots` 镜像 / `settings_directory` /
    `persist_settings`。
  - **DEC-011 翻转兑现**：apps/service 无 `--config` 时默认拾取
    `default_config_directory()/service.json`（损坏 fail closed）；
    policy.set 读-改-写回写该文件。默认策略收紧留观（DEC-028 决策 5）。
- 依据：[DEC-020](../decisions/DEC-020-permission-async-confirmation.md)
  （异步确认面语义）、[DEC-010](../decisions/DEC-010-m1-permission-framework.md)
  / [DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)（演进修
  订随实现登记）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （附加扩展流程）；本计划 `M5-07` 工作项；M5-06 占位锚点（Overlays /
  WallDisplay 转正）。
- 验证（本机 Linux，GCC 13.3 / Node 22）：
  - 全树 debug 构建零告警；ctest **40/40 通过 0 skip**（`runtime_service_test`
    增至 **453 检查**——新场景 `policy_face_get_set_persists`：get 默认集
    （read/execute allow、filesystem.write deny）、set 收紧 clipboard.write
    + 清空 read_roots、持久化文档回读、部分覆盖拒绝；`persistence_test`
    规则 map round-trip 与全词表负向；`ipc_protocol_golden_test` **1139 检
    查**含 v9 向量逐字节门禁；既有门禁零回归）。
  - `mirage-format-check` 与 `mirage-boundary-check`（39 公共头 0 违规）
    本机通过。
  - ui：`npm run check`（tsc 严格）0 诊断；`npm test` **18 文件 718 测试
    通过**（golden-vectors 消费同一 v9 文件 199 用例两端同绿；新增 mock
    能力位与批准 / 策略面场景）；`npm run lint` 0 告警；`npm run build`
    通过。
  - Linux 五预设矩阵 / windows 作业随本 PR CI 取证（结论由后续记录补录）。
- 限制与补跑条件：① 默认策略收紧（DEC-010 M1 默认值维持）留观至 M5-08
  产品化规模复核（DEC-028 决策 5）；② read roots 资源范围重启后生效
  （PathScope 绑定 provider 构造，热更属新 provider 可变面，未立项）；
  ③ 批准中心超时倒计时依赖快照内 timeout_ms，事件流不逐秒推送——剩余秒
  数随重取刷新。
- 同步：[DEC-028](../decisions/DEC-028-permission-policy-face.md)（新增）、
  [DEC-010](../decisions/DEC-010-m1-permission-framework.md) /
  [DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)（演进修订
  登记）、[mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)
  （§4 / §6.1 / §6.8 / §10）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4 与变更记录、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-28：`M5-07` CI 取证完成（补录）；run 36418679167
（headSha = `7f8d667`，PR [#61](https://github.com/Linductor-alkaid/mirage/pull/61)）
全部 8 作业 success，2026-09-28 合并（维护者授权）。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿；format &
  public-header boundaries 绿（40 公共头 0 违规）。
- frontend 作业：lint + strict tsc + 730 测试 + build 全绿（golden vectors
  双端门禁消费同一 meta.version 9 vectors 文件）。
- windows msvc (full tree)：全树构建 + 测试通过。
- 独立测试验证（第 1 轮）新增用例随本 runs 取证：
  `policy_set_live_effect_and_fail_closed_persist` / golden
  `policy-set-roots-too-many` / `policy-set-root-empty` /
  `policy-view-root-too-long` / ws + desktop + store 传输与策略用例
  （见 cbf9e76）。

2026-09-28：`M5-08` 设置全量与产品化规模复核完成（DEC-011 条目集扩展，
schema 1 加法演进；本 PR 的 CI 结论按仓库先例由下一工作项 PR 补录）。

- 范围：
  - **设置八类持久化条目定案与落地**：LocalSettings 扩展两个新块——
    `model`（设置-模型类目，DEC-027 `ModelLayerConfig` 面的配置输入：
    enabled / dialect / display_name / endpoint / api_prefix / model /
    credential_env）与 `runtime`（Runtime Configuration 类目：
    event_queue_capacity / max_connections）；apps/service 将两块映射进
    `ServiceConfig`（模型层启动期装配；事件队列容量与连接规模经配置可
    调）。UI Layout 类目为前端本地产品状态（会话别名 localStorage +
    观察台有界缓冲），不进 service.json（结论留痕）。Desktop Permissions
    已随 DEC-028 全词表落地（部分兑现转全）。
  - **会话历史跨重启持久化（DEC-021 挂账①兑现）**：新增
    `persistence::session_state` 编解码（registry + journal raw 输入 + 对
    话线程；64 会话 / 1024 journal / 256 轮界，文本 16 KiB 界）；服务持
    久化点 = open / close / dialog settle / journal append / teardown，
    注水 = 会话注册表重建 + journal 按序重放（pinned store 重导出同一序
    号）+ 对话线程重建（next_sequence 续号）；损坏文档响亮降级不阻断启
    动（DEC-011 姿态）。在飞对话轮与未落盘 journal 条目（崩溃窗口）不持
    久化——如实声明。
  - **产品化规模复核（DEC-007 / DEC-012 复核条目）**：事件队列容量与连
    接规模经 `runtime` 块可调（缺省 256 / 16 维持）；复核结论——两界均
    有界可观察（事件队列 drop-oldest + `events.overflow` 标记、超界连接
    显式拒绝），维持既有背压纪律；**端点对端凭据校验**：POSIX accept 循
    环增 SO_PEERCRED 同 uid 校验（不匹配连接关闭并继续），Windows 命名
    管道默认 DACL 同用户边界为既有姿态（结论留痕，热加固挂账后续）。
  - **M1 遗留守护纪律修复（BUG-20260916-001）**：`mirage service start`
    fork 子进程 exec 前 stdio 三描述符重定向 /dev/null——长生命周期服务
    不再持有调用方管道写端，`| grep` 管道读端正常 EOF。
  - **会话重命名产品别名（DEC-026 挂账⑤兑现）**：签派栏每会话菜单增「重
    命名」；标题为展示层产品别名，localStorage 持久化（wire 无标题成
    员，不进契约面）；空名回落派生标题。
- 依据：[DEC-011](../decisions/DEC-011-m1-local-state-persistence.md)
  （条目集与默认拾取翻转，M5-07 已兑现部分）、
  [DEC-021](../decisions/DEC-021-session-message-contract-face.md)（挂账
  ①）、[DEC-026](../decisions/DEC-026-observation-face-and-definition-read.md)
  （挂账⑤）、[DEC-027](../decisions/DEC-027-dialog-mode-model-layer.md)
  （settings-model 契约输入）、
  [DEC-028](../decisions/DEC-028-permission-policy-face.md)（决策 5 留
  观）、[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md) /
  [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （复核条目）；M1 计划 BUG-20260916-001；本计划 `M5-08` 工作项。
- 验证（本机 Linux，GCC 13.3 / Node 22）：
  - 全树 debug 构建零告警；ctest **40/40 通过 0 skip**（`persistence_test`
    增至 **224 检查**——settings 新块 round-trip、缺省文档、model/runtime
    负向；`runtime_service_test` **453 检查**含 `policy_face_get_set_persists`
    与 `session_close_frees_dialog_registry_slot`；`ipc_protocol_golden_test`
    **1139 检查**维持 v9 逐字节门禁——wire 零变更；既有门禁零回归）。
  - `mirage-format-check` 与 `mirage-boundary-check`（40 公共头 0 违规）
    本机通过。
  - ui：`npm run check`（tsc 严格）0 诊断；`npm test` **18 文件 730 测试
    通过**（golden-vectors 维持 v9 199 用例；会话重命名场景随套件）；
    `npm run lint` 0 告警；`npm run build` 通过。
  - Linux 五预设矩阵 / windows 作业随本 PR CI 取证（结论由后续记录补录）。
- 限制与补跑条件：① 会话状态持久化时机为事件驱动 + teardown——崩溃窗口
  内未落盘的 journal / 对话条目丢失（与 recovery 姿态一致，如实声明）；
  ② 设置-模型类目的 UI 实时变更需 wire 面（model.get/set），本增量以配
  置文件 + 启动装配承载，挂账后续；③ 对端凭据校验的 Windows 命名管道热
  加固（安全描述符显式化）挂账后续；④ 默认策略收紧维持留观（DEC-028 决
  策 5），收紧决策随 M5-08 后使用数据另定。
- 同步：[DEC-011](../decisions/DEC-011-m1-local-state-persistence.md) /
  [DEC-028](../decisions/DEC-028-permission-policy-face.md)（演进修订与
  结论留痕）、
  [mirage-ipc-protocol-v1.md](../design/mirage-ipc-protocol-v1.md)（零变更
  确认）、
  [前端规范](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  §4、ui/README、
  [总计划](mirage-implementation-plan.md) 状态叙述与决策表。

2026-09-29：`M5-08` 第二轮独立验证缺陷修复（同 PR 补充提交）——注水会
话可被关闭（`fix(runtime)`）：注水只重建服务侧状态，pinned host 无该会
话，session.close 走 pinned close_session 即拒（not_found），持久化会
话成僵尸。

- 修复：注水时登记 hydrated_sessions；close 对 hydrated 会话跳过 pinned
  调用走纯产品状态移除；同根因下 task.submit 到注水会话无法被 pinned
  受理（『只读历史视图』语义）随代码注释与限制节声明。
- 回归：session_state_round_trip_across_restart 恢复 close-不复活断言
  （第二实例 close ok + 第三实例不复活 + 各面 not_found）。
- 验证：runtime_service_test 539 检查 0 失败（本机实测）。

2026-09-30：`M5-08` CI 取证完成（补录，由 `M5-09` PR 兑现自述义务）；run
36457274467（headSha = `214b53c`，PR
[#62](https://github.com/Linductor-alkaid/mirage/pull/62)）全部 8 作业
success，2026-09-28 合并（维护者授权）。

- Linux 矩阵：debug / release / asan / ubsan / tsan 五预设全绿；format &
  public-header boundaries 绿。
- frontend 作业：lint + strict tsc + 730 测试 + build 全绿。
- windows msvc (full tree)：全树构建 + 测试通过。
- 独立测试验证（第 2 轮）缺陷修复（注水会话可被关闭，`fdc3b00` /
  `042fb31` / `214b53c`）随本 run 取证（runtime_service_test 539 检查）。
- 复选框同步：`M5-08` 工作项复选框随本记录翻转（工程规范第 4 节勾选规则
  第 1 条；先例 c3f1629 补录 `M5-07`）。

2026-09-30：`M5-09` Desktop Overlay 完成（DEC-029 承载机制随实现定案并
登记；本 PR 的 CI 结论按仓库先例由下一工作项 PR 补录）。

- 范围：
  - **承载机制（DEC-029）**：Overlay 由 `mirage-service` 进程承载（与 GUI
    生命周期解耦，后台任务持续呈现），平台承载落 Platform Backend 私有
    前端——Windows 双分层窗口（视觉窗 `WS_EX_LAYERED|WS_EX_TRANSPARENT`
    整面跨进程点击穿透 + 每像素 alpha `UpdateLayeredWindow`；交互窗仅确认
    横幅区域可点，layered alpha 命中测试自然放行透明像素）；Linux X11
    shape 覆盖层（override-redirect 置顶窗 + `XShape` 内容/输入双 shape，
    无确认入口时整窗穿透；不依赖合成管理器，Xvfb 可取证）；Wayland 原生
    会话 / X11 dev 缺席 / 无交互桌面 → `open_overlay_carrier()` 返回
    null（能力如实缺失，DEC-015 先例）。不进 DesktopEnvironment 冻结
    provider 集、不进 IPC wire（`desktop/overlay_carrier.hpp` /
    `overlay_surface.hpp` 两个纯 std 公共头为唯一新契约面，boundary 门禁
    覆盖）。
  - **与任务执行流的事件联动（wire 零变更）**：`OverlayPresenter`（服务
    内部模块）经 `EventHub::subscribe()` 订阅既有事件流驱动任务横幅
    （`task.updated` Active → 终态清面）；`DesktopAtomToolset` 新增
    `AtomOverlayFeed` 缝（null 零开销，`AtomPermissionGate` 先例同型）：
    `desktop.window.activate` / `desktop.input.type_text` 动作前发布
    "即将执行 + 目标高亮"（几何经同 Provider 有界查询，失败如实降级为无
    高亮）、动作后清除；`desktop.accessibility.semantic_snapshot` 在
    debug 模式发布语义快照节点为 Observation 调试盒（封顶 64，RULE-07）。
  - **确认入口（DEC-020 应答面扩展）**：hub publish hook 同步镜像待确认
    请求到 overlay；确认横幅按钮点击经服务串行域调
    `AsyncConfirmationHub::resolve()`（与 IPC 客户端同一路径，
    first-response-wins 不变）；呈现以 hub 快照校验（新增 `is_pending()`
    探测，已决请求即刻清面）。呈现循环 = Executor `IBlockingIoWorker`
    （teardown 于驱动取消前 join；平台回调线程不做业务决策，点击投递串行
    域）。
  - **产品装配**：`ServiceConfig` 增 `overlay_carrier`（null = 既有行为
    零影响）与 `overlay_debug`；`mirage-service` CLI 增 `--overlay
    on|debug`（默认 off，不经同意不在桌面绘制）。UI 工作区零变更
    （DEC-013 §3.6 "独立透明窗，内部布局不规定"形态兑现）。
- 依据：设计文档第 12、14、17 节；[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)
  （风险节先例）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
  （事件织物）、[DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)
  （前端可选性）、[DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)
  （双工具链 + Win32 前端并发纪律）、
  [DEC-018](../decisions/DEC-018-windows-notification-carrier.md)（承载
  决策形态先例）、[DEC-020](../decisions/DEC-020-permission-async-confirmation.md)、
  [DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)（atom 缝模式）、
  [DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md)（本工作项新
  决策记录）；本计划 `M5-09` 工作项。
- 验证（本机 Linux，GCC 13.3，真实 X 会话 `:0`）：
  - 全树 debug 构建零错误（含 platform X11 overlay 前端与 Windows 前端
    同源编译路径外的 Linux TU）；ctest **40/40 通过 0 skip**（既有门禁零
    回归；`ipc_protocol_golden_test` 维持 v9 逐字节门禁——wire 零变更的
    回归证据；`runtime_service_test` 全绿——overlay 缺省路径即既有行为）。
  - `mirage-format-check` 通过（本变更全部 C++ TU）；`mirage-boundary-check`
    通过（**42 公共头 0 违规**，含新增 `overlay_carrier.hpp` /
    `overlay_surface.hpp`——纯 std，无第三方 include）。
  - 运行级冒烟（真实 X 会话）：`mirage-service --overlay on` 启动——X11
    载体建立、overlay pump worker 受理、服务按既有路径服务、SIGTERM 后
    "stopped cleanly"（teardown 先于驱动取消 join pump，关闭序闭合）；
    `DISPLAY=:99`（无服务器）→ `open_overlay_carrier()` 返回 null，stderr
    响亮声明 "overlay carrier unavailable … the service continues without
    the overlay"，启动横幅如实报告 `desktop overlay: off`。
- 限制与补跑条件：① 本 PR 的 CI 结论（Linux 五预设矩阵、format &
  boundaries、frontend、windows msvc 全树）按仓库先例由下一工作项 PR
  补录；② Windows 分层窗口的运行级取证（置顶 / alpha / 穿透 / 交互窗
  点击应答）需真实 Windows 桌面，本机不可达——随 CI windows 作业编译
  门禁 + 维护者 Windows 机器补跑（DEC-017 证据分级，M4-06 先例）；③
  overlay 呈现路径的功能/并发测试（fake carrier 注入的任务横幅联动、
  确认呈现与 resolve 清面、overlay 点击收敛、teardown 顺序）由独立测试
  验证轮交付；④ Xvfb 无合成器环境下的 shape 呈现取证本机未执行（Xvfb
  未安装），随独立验证轮补跑；⑤ MinGW platform 子集交叉编译本机不可达
  （交叉工具链缺席；`MIRA-20260922-001` 全树缺口维持挂账），Win32 前端
  双工具链证据随 CI MSVC + 后续交叉窗口补跑。
- 同步：[DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md)（新增，
  Accepted）、[总计划](mirage-implementation-plan.md) 状态叙述与决策表；
  wire 契约零变更（`mirage-ipc-protocol-v1.md` 无涉）、前端规范 §3.6
  形态兑现无修订。
