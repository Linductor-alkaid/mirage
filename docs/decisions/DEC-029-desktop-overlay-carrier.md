# DEC-029：Desktop Overlay 承载机制

> 状态：Accepted
> 日期：2026-09-30
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-09` 落地）
> 替代/被替代：无

## 背景与问题

`M5-09` 要求落地 Desktop Overlay（设计文档第 14 节：目标高亮、即将执行操作提示、
确认入口、Observation 调试观察面），并明确"承载机制随实现定案并记录"（M5 退出
条件第 7 条；与 `M4-05`/DEC-018 同型，不是实施前阻塞）。约束面：

1. 平台承载差距先例在案：Windows 分层窗口可置顶 / 透明 / 点击穿透；Linux 的
   置顶 / 透明 / 点击穿透受合成器与 Wayland 限制（[DEC-006](DEC-006-ui-web-frontend-packaging.md)
   风险节），该职责在 Platform Backend 解决、按平台能力如实降级并响亮声明，
   不为取证伪造能力（[m5 计划风险节](../plans/m5-desktop-product.md)）。
2. 拆分纪律：壳内 IPC、Overlay 与 tray 的平台差异收敛在 Platform Backend 或进程
   边界内，**不渗入协议与契约面**（M5 计划拆分纪律节）。Overlay 不在 M2 冻结的
   DesktopEnvironment Provider 集（Application / Window / Accessibility / Screen /
   Input / Clipboard / Filesystem / Process / Notification）内，本决策不改动该
   冻结面，也不新增 IPC wire 面。
3. Overlay 的内容与任务执行流联动：Agent 即将执行的桌面动作、动作目标几何、待
   确认权限请求（[DEC-020](DEC-020-permission-async-confirmation.md) 异步确认面）
   与 Observation 快照（[DEC-026](DEC-026-observation-face-and-definition-read.md)
   的语义快照投影）都产生于 mirage-service 的任务执行路径。
4. 双工具链门禁（[DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)
   决策 1）：Win32 路径必须同时可被 MinGW-w64 与 MSVC 编译；公共头不得泄漏平台
   与第三方类型（RULE-01）。
5. 并发纪律（仓库 AGENTS.md）：平台 API 要求线程亲和时（Win32 消息循环）封装在
   Platform Backend；长期阻塞循环走 Executor blocking worker 生命周期；跨上下文
   通信按语义选用 `executor::comm`（最新状态用 `LatestMailbox`、多订阅广播用
   `Topic`）；业务 handler 不在平台回调线程执行。

## 决策

1. **承载形态 = Platform Backend 私有前端实现的原生覆盖窗口，由 `mirage-service`
   进程承载**。理由：Overlay 的全部内容源（动作流、目标几何、待确认请求、观察
   快照）都在服务的任务执行流内，服务进程承载使 Overlay 与 GUI 生命周期解耦——
   产品核心承诺是后台任务持续运行（设计文档第 12 节），任务在 GUI 关闭时执行，
   Overlay 同样应当工作。壳进程 / 独立 Overlay 进程形态被否决：前者使 Overlay
   绑定 GUI 生命周期（语义错误），后者为单一消费方新增进程形态与第二个 Executor
   owner（EXEC-01 纪律代价最大，tray 的进程形态属 `M5-10` 且经 Local IPC 交互）。
   Overlay 窗口本体由 service 进程内的 Executor blocking worker 驱动，不新增
   Executor 实例、不自建线程。
2. **桌面核心新增独立抽象，不进 DesktopEnvironment 冻结 provider 集**：
   `desktop/environment/include/mirage/desktop/overlay_surface.hpp`（`OverlayHighlight` /
   `OverlayConfirmation` / `OverlaySurfaceFrame` / `OverlayClick`，纯 std 类型 +
   预算上限）与 `desktop/overlay_carrier.hpp`（抽象 `OverlayCarrier`：长驻 `run()`
   呈现循环 + 线程安全 `wakeup()`；平台无类型过公共头）。平台后端以私有前端实现
   该接口（adapter 依赖 core 接口，DEC-008 既有模式）；每个平台公开头暴露一个
   `open_overlay_carrier()` 工厂，环境绑定与否不影响既有构造点。
3. **Windows 承载 = 双分层窗口**（user32/gdi32，无新依赖面）：
   - 视觉窗：`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
     WS_EX_TOOLWINDOW`，覆盖整个虚拟屏幕，premultiplied ARGB DIB +
     `UpdateLayeredWindow` 呈现每像素 alpha。`WS_EX_TRANSPARENT` 使整窗点击穿透
     跨进程成立（`WM_NCHITTEST` 的 `HTTRANSPARENT` 只在同线程窗口间转发，不能
     作为跨进程穿透机制——以双窗口替代单窗 `HTTRANSPARENT` 方案）。
   - 交互窗：仅在确认入口存在时显示，位置精确覆盖确认横幅；分层但**不设**
     `WS_EX_TRANSPARENT`，layered 像素 alpha 命中测试使透明像素继续穿透、按钮
     像素可点击。两窗同一 pump 线程创建销毁，`SW_SHOWNA` 显隐不抢焦点。
4. **Linux 承载 = X11 shape 覆盖层（X11/XWayland 会话先例）**：override-redirect
   置顶窗口，**XShape 合成透明**——窗口内容区域 shape 为绘制图元（高亮矩形描边、
   提示横幅、按钮）的并集，input shape 仅确认按钮区域：无确认入口时整窗完全
   点击穿透。不依赖合成管理器（Xvfb / CI 可运行取证）；每像素 alpha 属合成器
   依赖演进，本决策**不宣称**。Wayland 原生会话（无 XWayland）、X11 dev 包缺席
   或显示连接失败时 `open_overlay_carrier()` 返回 null——能力如实缺失、fail
   closed（DEC-015 X11 前端先例），不为取证伪造。X11/Xext 编译可选性与
   `x11_backend` 同门禁（`MIRAGE_LINUX_X11`），缺席编译 stub（访问器返回 null）。
5. **与任务执行流的事件联动 = 服务内部事件织物（`executor::comm`），零 wire
   变更**：
   - 任务 / 主机 / 权限事件：presenter 经 `EventHub::subscribe()` 取得 Topic 订阅
     （有界 drop-oldest，与 IPC 连接同机制），由 pump 循环排空——任务横幅
     （goal + progress）随 `task.updated` 演进，终态清空动作高亮与调试观察面；
     服务无关事件忽略。快照事实源纪律不变（overlay 是呈现，不是状态源）。
   - 桌面原子动作（DEC-024）：`DesktopAtomToolset::build()` 新增可选
     `AtomOverlayFeed` 缝（null = 零开销不接线，先例同 `AtomPermissionGate`）：
     `desktop.window.activate` / `desktop.input.type_text` 在权限判定之后、
     Provider 副作用之前发布"即将执行 + 目标高亮"（目标几何经同一 Provider 的
     既有有界查询解析，查询失败如实降级为无高亮、不阻塞动作），动作结算后清除；
     `desktop.accessibility.semantic_snapshot` 在 debug 模式下发布语义快照节点
     几何为 Observation 调试盒（封顶 64 盒，超出部分不呈现——RULE-07 有界）。
   - 权限确认（DEC-020）：hub 的 publish hook 在广播 `permission.request` 事件的
     同一线程把待确认请求推入 presenter（`LatestMailbox`，最新状态语义）；确认
     入口点击经服务串行域调用 `AsyncConfirmationHub::resolve()`——与 IPC 客户端
     同一应答路径，first-response-wins 语义不变；呈现的确认以 hub 快照为事实源
     校验（pump 每 tick 经新增 `AsyncConfirmationHub::is_pending()` 探测，已决 /
     过期请求即刻从呈现移除，不等待预算耗尽）。无 hub（`--confirm allow|deny`）
     时确认入口不存在，overlay 只呈现动作面。
6. **线程与生命周期纪律**：覆盖窗口在 pump 线程创建并在同线程销毁（Win32
   `DestroyWindow` 线程亲和，DEC-018 所有权模型先例）；pump = Executor
   `IBlockingIoWorker`（`wakeup()` 释放外部等待：Win32 消息队列投递 / X11
   self-pipe，等待切片 200 ms 封顶）；流侧（驱动线程、hub hook 线程、IPC loop
   线程）更新一律 `LatestMailbox::try_publish`（满即丢弃旧值，最新状态胜出，
   永不阻塞生产者）；点击回传经串行域 post（admission 拒绝即丢弃——teardown
   窗口期的点击是废弃交互），业务 handler 不在平台回调线程执行（仓库规则 11）。
   `OverlayCarrier` 接口不出现 executor 类型（`std::function` 探针），pump worker
   归 RuntimeService 所有：`start()` 在 IPC loop worker 之后启动，admission 失败
   响亮降级（stderr 记录，服务继续——overlay 是辅助呈现面，DEC-011"响亮降级不
   阻断启动"姿态先例）；`teardown()` 在既定顺序最前（loop 之后、驱动取消之前）
   `worker.stop()` 收敛。
7. **配置面**：`ServiceConfig` 新增 `overlay_carrier`（`shared_ptr`，null = 无
   Overlay，既有行为与全部既有测试零影响）与 `overlay_debug`（默认 false，
   Observation 调试面显式开启）。`mirage-service` CLI 新增 `--overlay on|debug`
   （默认 off：不经用户同意不在桌面绘制任何覆盖内容）。无新 wire、无新设置
   持久化条目（Overlay 呈现形态属产品装配面，不入 DEC-011 条目集）。

## 备选方案

- **CEF / Web 呈现的 Overlay 窗口**（壳进程透明 CEF 窗 + `ui/overlay` 前端）：
  否决。CEF 透明窗 + 跨进程点击穿透在双平台的壳内实现风险与验证成本最高
  （PoC 基线未取证透明合成路径）；且使 Overlay 绑定壳生命周期——GUI 未运行时
  任务执行无 Overlay，语义错误。DEC-006 修订节保留的"原生窗口嵌入"路径服务于
  壳内合成场景，Overlay 不属该场景。
- **独立 `apps/overlay` 进程经 Local IPC 订阅**（tray 同型）：否决。新增进程形态
  与第二个 Executor owner；Overlay 的内容源在服务进程内，经 IPC 绕出再绕入只为
  形态对称，不构成需求。tray（`M5-10`）的 IPC 形态由其常驻交互需求正当化，
  Overlay 不具备。
- **DesktopEnvironment 新 Provider（`overlay()` 访问器）**：否决。M2 冻结 provider
  集是跨平台契约面；Overlay 是产品呈现面而非环境能力，冻结尾上追加 provider
  违反"不渗入契约面"拆分纪律，且环境所有权（服务）之外的消费方并无 Overlay
  需求。
- **服务内直绘（无 carrier 抽象，runtime 直接持有平台窗口代码）**：否决。平台
  类型进 runtime 层违反依赖方向（runtime -> desktop -> platform）与 RULE-01。

## 影响与风险

- `platform/{windows,linux}` 各新增私有前端 `overlay_backend.{hpp,cpp}`（平台类型
  不出 .cpp，RULE-01；Linux 无 X11/Xshape 时编译 stub）；两平台公开头各新增
  `open_overlay_carrier()` 工厂声明；`desktop/environment` 新增两个纯 std 公共头
  （boundary 门禁覆盖，无第三方 include）。
- 运行级取证分级（DEC-017）：Linux 侧可在本机 Xvfb / 真实 X 会话取证（shape
  合成不依赖合成管理器）；每像素 alpha 仅 Windows 承载宣称，Linux 侧不宣称；
  Windows 交互窗点击、分层窗穿透与置顶行为需真实桌面取证（CI runner 无交互
  桌面时按 skip 纪律注记并登记补跑条件，维护者 Windows 机器兜底——M4-06 先例）。
- 确认入口是 DEC-020 应答面的新来源（进程内 overlay 点击）：first-response-wins
  与 fail-closed 收敛语义不变（同一 `resolve()` 路径）；DEC-020 文档语义中
  "任一 IPC 客户端应答"扩展为"任一 IPC 客户端或 Overlay 确认入口应答"，由本
  记录与 DEC-020 关联注记承载。
- Overlay 常驻呈现是用户可见副作用：默认 off + 显式 `--overlay` 开启；服务以
  非 GUI 上下文运行（session 0）时窗口创建失败 → carrier `run()` 返回失败诊断，
  pump worker 退出，stderr 响亮记录（能力诚实，非降级为"假显示"）。
- 全树 MinGW 交叉构建仍不可达（`MIRA-20260922-001`）：Win32 Overlay 路径仅依赖
  user32/gdi32（双工具链 SDK 自带），platform 子集可交叉验证；本决策不宣称全树
  MinGW 证据。

## 验证方式

- 契约/状态机测试（无平台依赖）：fake carrier 注入 `ServiceConfig`，验证任务
  横幅事件联动、动作发布 / 结算清除、确认呈现与 `resolve` 后 tick 清除、overlay
  点击经串行域收敛 `resolve`、teardown 顺序（`worker.stop()` 先于驱动取消）。
- Linux 运行级取证：Xvfb 会话 `open_overlay_carrier()` 非 null、`run()` 建窗、
  帧更新重绘、无确认入口时 input shape 为空（完全穿透）、`wakeup()` 切片内停机；
  无 X 连接（Wayland 原生模拟 / 空 DISPLAY）返回 null。
- Windows 运行级取证（真实桌面，维护者机器兜底）：分层窗置顶 / 每像素 alpha /
  视觉窗穿透、交互窗按钮点击应答 `permission.respond` 同路径收敛、无交互桌面
  probe 失败 null。
- 双工具链门禁：MSVC CI windows 作业全树 + MinGW platform 子集交叉编译
  （DEC-017 决策 1；全树 MinGW 仍挂台账缺口）。
- golden vectors / wire 零变更确认（`ipc_protocol_golden_test` 不动）。

## 关联文档和工作项

- 设计文档第 12、14、17 节；[DEC-006](DEC-006-ui-web-frontend-packaging.md)
  （Overlay 平台限制风险节）、[DEC-007](DEC-007-local-ipc-and-runtime-service.md) /
  [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（事件织物与
  EXEC 纪律）、[DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)
  （Linux 前端可选性与 fail-closed 先例）、
  [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)（双工具链、
  Win32 前端并发纪律）、[DEC-018](DEC-018-windows-notification-carrier.md)
  （平台承载决策形态先例）、[DEC-020](DEC-020-permission-async-confirmation.md)
  （确认面应答路径）、[DEC-024](DEC-024-desktop-atom-toolset.md)（atom 缝模式）。
- 工作项：[M5 计划](../plans/m5-desktop-product.md) `M5-09`（本记录即其"承载
  机制随实现定案并记录"义务的兑现；实施进度由该工作项跟踪）。
