# M5：Desktop Product（Workspace / Overlay / 权限 / 分发）

> 状态：In Progress（`M5-01`、`M5-02`、`M5-03`、`M5-04` 完成，2026-09-26；
> `M5-05` 第一轮完成 2026-09-27；第二轮桌面原子工具注册增量完成，2026-09-27）
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M3](m3-mirador-integration.md)（已完成：Mirador 视觉集成与壳选型冻结——
> CEF，PoC 取证与暂定策略在案）、[M4](m4-windows-backend.md)（已完成：Windows
> Backend 与产品进程 Windows 化——命名管道 IPC、`mirage-service` / CLI 双平台
> 可运行）
> 建议发布点：`release-epsilon`（tag 待维护者授权后创建）
> 更新日期：2026-09-26（计划建立）

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
- [~] `M5-05` 工作流契约面与编辑器真实化（第一轮契约面与第二轮桌面原子工具
      注册已完成，2026-09-27；编辑器完整版接真实面为剩余增量）：`workflow.*` 请求面（`list` /
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
- [ ] `M5-06` 会话页产品化（对话模式真实化）：会话列表 / 管理接 `session.*`；
      消息流真实渲染（M1.5 模拟域演示语义退出会话页）；观察台真实化——
      运行时间线 / 观察流直连任务快照与事件，视觉状态呈现与订阅演进（M3
      非目标兑现：视觉参考进入 UI 观察面）。
- [ ] `M5-07` 批准中心与权限管理产品化：批准中心接异步确认面（`M5-03`）；
      设置页权限策略配置（每能力 `allow` / `confirm` / `deny`、资源范围、
      默认策略收紧——DEC-010）；权限策略持久化（DEC-011 Desktop
      Permissions 条目）与运行时生效路径。
- [ ] `M5-08` 设置全量与产品化规模复核：设置八类持久化条目定案与落地
      （DEC-011 条目集：Application Settings / Runtime Configuration /
      Desktop Permissions / UI Layout / Platform Configuration 等）；默认
      路径解析翻转默认行为（DEC-011）；事件队列容量 / 多连接规模 / 端点
      对端凭据校验的产品化复核（DEC-012 / DEC-007 复核条目）；M1 遗留
      `mirage service start` 守护纪律修复随产品化落地（M1 计划验证记录
      挂账）。
- [ ] `M5-09` Desktop Overlay：平台承载（Windows 分层窗口：置顶 / 透明 /
      点击穿透；Linux X11 覆盖层先例 + Wayland 限制如实声明——DEC-006
      风险节）与 UI（目标高亮、即将执行操作提示、确认入口、Observation
      调试观察面）；与任务执行流的事件联动（承载机制随实现定案并记录）。
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
