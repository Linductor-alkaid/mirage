# M5：Desktop Product（Workspace / Overlay / 权限 / 分发）

> 状态：In Progress（`M5-01` 完成，2026-09-26）
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
- [ ] `M5-02` CEF 产品壳骨架与壳内 IPC 传输路径（`apps/desktop`）：壳进程
      模型（browser / renderer、窗口创建、加载 `ui/app` 构建产物、M5-01
      定案的沙箱 / GPU 策略）；产品 IPC 路径——browser 进程经 `runtime/ipc`
      `IpcClient` 直连 Local IPC（Unix socket / 命名管道，传输差异不外溢），
      renderer 经受控 bridge 访问（UI 不进 C++ 目标依赖图，IPC 契约唯一
      耦合面）；壳形态与浏览器形态同构，transport 装配语义在壳内等价；
      devbridge 维持开发工具定位不变。验收：壳内 UI 对真实 `mirage-service`
      完成 hello / 订阅 / 任务往返。
- [ ] `M5-03` 权限异步确认面（DEC-010 M5 演进）：异步确认语义决策记录
      （等待预算、超时收敛、多连接竞争、恢复行为——DEC-010 限制节预告的
      决策输入）；协议 v1 附加扩展 `permission.*`（请求事件 + 应答 op，
      DEC-012 扩展流程，wire 契约与 golden vectors 双端同步）；
      `runtime/permission` 同步确认挂点演进为 Local IPC 异步面（M1
      "必须立即返回"挂点契约的替换路径，判定语义与 Provider 硬边界不变）；
      任务驱动器确认流接线与 trace 记录保持。
- [ ] `M5-04` 会话与消息契约面（协议 v1 扩展）：`session.*` 请求面（列表 /
      打开 / 历史摘要，依托 pinned mira `open_session` / `conversation_log`
      投影）与消息 / 轮次 / 增量输出事件集（DEC-012 扩展流程）；Runtime
      Service 由 M1 过渡驱动形态向 mira 会话 / 任务模型演进（DEC-008 迁移
      路径兑现）；golden vectors 双端门禁与 wire 契约同步。
- [ ] `M5-05` 工作流契约面与编辑器真实化：`workflow.*` 请求面（`list` /
      `save` / `publish` / `delete` / `atom.catalog` / `runs` / `run` /
      `cancel`，对齐 `WorkflowBackend` 接口缝与 pinned mira Workflow IR
      v1）；`atom.catalog` 承载 Platform Backend 能力上报；运行监控事件化
      （WorkflowRun 状态）；UI 工作流编辑器完整版接真实面（DEC-013 IR
      对齐验收）。
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
  静默分叉或绕过。
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
