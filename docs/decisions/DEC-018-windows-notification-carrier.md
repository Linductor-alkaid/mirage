# DEC-018：Windows NotificationProvider 通知承载机制

> 状态：Accepted
> 日期：2026-09-23
> 负责人：Mirage 维护者
> 冻结里程碑：M4（`M4-05` 落地）
> 替代/被替代：无（toast 承载的 M5 重议触发条件见"备选方案"）

## 背景与问题

`M4-05` 要求为 Windows NotificationProvider 先定承载机制并登记决策记录（DEC-018
起，编号顺延），再落地契约面等价能力（[M4 计划](../plans/m4-windows-backend.md)，
登记时 `docs/decisions/` 实存 DEC-001..DEC-017）。约束面：

1. Provider 契约已在 M2-05 冻结：单方法 `notify(title, body, limits, cancel)`
   同步有界，拒绝（cancelled、空 title、超预算、非 UTF-8）一律发生在副作用前；
   `ok` 表示平台受理而非用户已见，交付语义遵循平台
   （`desktop/environment/include/mirage/desktop/notification_provider.hpp:24-39`，
   `RULE-07`）。Linux 侧以 freedesktop Notifications（GDBus，5 s 内部 deadline）
   承载，无 session bus 时 open() 返回 null fail closed
   （`platform/linux/src/notification_backend.cpp:25`、`:43-56`、`:61-86`）。
2. Windows 侧挂载点已在公共头预留：`notification()` 当前落基类 null 缺省——
   能力缺失而非坏 Provider（`platform/windows/include/mirage/platform/windows/
   windows_desktop_environment.hpp:46-49`）。
3. 候选承载只有两个：WinRT toast（ToastNotificationManager；未打包 Win32 应用
   的前提是 MSIX 包身份或携带 `System.AppUserModel.ID` 的开始菜单快捷方式，
   载体缺失时 toast 静默不显示而 HRESULT 仍可能成功——依据本工作项调研核对的
   Microsoft Learn 桌面 toast 文档）与 `Shell_NotifyIcon` 气球（Win10 起气球
   呈现为横幅：Win10 驻留通知中心、Win11 为瞬态、`uTimeout` 自 Vista 起弃用、
   全任务栏同一时刻单气球——同上来源 Shell_NotifyIconW / NOTIFYICONDATAW 页
   remarks；本记录的运行级确认随 `M4-05` 取证）。
4. M4 边界：无安装包与更新器（[DEC-006](DEC-006-ui-web-frontend-packaging.md)
   `exe` 安装包属 M5）、仅交付契约面等价能力（M4 计划非目标）、双工具链门禁
   （[DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md) 决策 1，
   MinGW-only 代码不被接受）。

## 决策

1. **承载机制 = `Shell_NotifyIcon` 气球/横幅**：通知经 `NOTIFYICONDATAW`
   （`NIF_INFO`）提交，由环境生命周期内的隐藏回调窗口 + 托盘承载图标承载
   （`NIM_ADD` 建图标、`NIM_MODIFY` 发气球、析构 `NIM_DELETE` 后销毁窗口）。
   `Shell_NotifyIconW` 返回 `TRUE` = shell 受理 = 契约 `ok`；呈现形态（气球/
   横幅、Win10 驻留通知中心与 Win11 瞬态、静默时段 / Focus assist、单气球
   排队）由系统决定，全部落入契约"交付语义遵循平台"条款，如实声明、不改契约。
2. **open() 探测 fail closed**：构造时建立隐藏窗口与承载图标，任一步失败
   （无交互桌面会话 / 无 shell 通知区；session 0 服务上下文按 DEC-017 决策 3
   `SendInput` 先例如实失败）则整体清理并令访问器 null——与 Linux 无
   session bus → null 同型。探测失败是能力如实缺失，不是降级成"假 ok"。
3. **行为差异如实声明（实现注释 + 本记录）**：
   - 载荷收窄：`szInfoTitle[64]` / `szInfo[256]` 是含终止符的 UTF-16 硬上限
     （63 / 255 字符），落在契约默认预算（256 / 4096 字节）之内；超平台上限
     在副作用前 `invalid_argument` 拒绝、不静默截断——同载荷 Linux 成功、
     Windows 拒绝是经 stable 错误码可见的平台差异，不是契约分叉。
   - 托盘常驻可见面：承载图标在环境生命周期内驻留通知区（裸图标、无菜单，
     完整托盘交互属 M5 产品化）；这是气球承载的固有代价，写入前端注释。
   - 无泵回调：契约面无点击/激活语义，回调消息（`NIN_BALLOON*`）不设消费方，
     积压于创建线程消息队列（有系统上限）；结构性解决（STA + 消息泵经
     EXEC-03 接入 Executor blocking worker）走 DEC-017 决策 4 的升级路径，
     仅随 M5 交互需求立项，不在 M4。
4. **线程亲和所有权模型**：承载窗口与图标由 open() 所在线程创建、析构在同
   线程销毁（`DestroyWindow` 线程亲和；`NIM_DELETE` 先于窗口销毁）；跨调用
   共享状态一把互斥锁串行化；不建线程、不建消息循环（DEC-017 决策 3；线程
   亲和封装在 Platform Backend 内）。调用同步返回，不等待展示时长（气球/
   横幅生命周期由系统管理）；`Shell_NotifyIconW` 无每调用超时参数，shell 挂死
   的极端情形由调用方执行上下文兜底（DEC-017 决策 3 单调用收敛纪律，M4-02
   三级兜底先例同型）。
5. **不创建任何 AUMID 载体**：M4-05 不写开始菜单快捷方式、不写注册表、不引入
   包身份——载体创建属安装器职责（M5，DEC-006）。
6. **权限与依赖路由不变**：`notification.post` 沿用 M2-05 冻结词表默认 allow
   （[DEC-010](DEC-010-m1-permission-framework.md)；M4 计划"不新增判定语义"）；
   pinned mira / mirador 均无桌面通知能力（include 与 docs 检索核实），自研
   落位 Platform Backend 正确，不构成依赖能力缺口、不进反馈台账。
7. **CMake 零新增导入库**：`Shell_NotifyIconW` / `NOTIFYICONDATAW` 由
   `shellapi.h` 声明（双工具链 SDK 自带；本机 MinGW 用户前缀实测
   `shellapi.h:563-564`、`:465-486`，V1-V3 尺寸宏与 `NIF_INFO` /
   `NIIF_RESPECT_QUIET_TIME` 齐备），`shell32` 已随 `M4-04` 链接
   （`platform/CMakeLists.txt:105`）。

## 备选方案

- **WinRT toast（ToastNotificationManager）**：否决，留 M5 重议触发。理由：
  ① 前提不成立——未打包应用的 toast 需要可解析 AUMID（MSIX 包身份或携带
  `System.AppUserModel.ID` 的开始菜单快捷方式），而 M4 的 Mirage 既无 MSIX
  也无安装器写入的快捷方式（DEC-006 属 M5），唯一路径是测试/进程自写用户
  Start Menu 树——平台外副作用，越出 M4 边界；且载体缺失时 toast 静默不显示
  （HRESULT 仍可能成功），契约面等价能力在 M4 全部验证拓扑实际不可运行取证。
  ② 成本最重——本仓库 MinGW 用户前缀无任何 toast ABI 头（`find -iname
  "*notification*"` 仅命中无关的 `functiondiscoverynotification.h`；
  `grep IToastNotificationManagerStatics|CreateToastNotifierWithId` 零命中，
  本会话实测），需手写冻结 WinRT 接口声明（manager / factory / notifier /
  notification + `Windows.Data.Xml.Dom` XML 面 + HSTRING），是仓库首个 WinRT
  激活面；对比气球承载零新增声明。③ DEC-017 决策 6 已确立"无触发不引入
  WinRT 依赖面"的立场（该条上下文为采集升级路径，本决策将其纪律延伸到通知
  承载：触发条件即下述重议条件）。**重议触发条件**：M5 产品化需要 Action
  Center 驻留、点击激活交互或富内容通知，且 DEC-006 安装器可交付 AUMID 载体
  时，凭新证据重议承载机制并修订本记录。
- **"仅契约面等价 + 无承载即如实收缩"作为独立方案**：不单独立案——该降级
  路径已内建为决策 2（open() 探测 fail closed）；它只定义"无承载"分支，不
  替代正向承载选型。

## 影响与风险

- `platform/windows` 新增私有前端 `notification_backend.{hpp,cpp}`（Linux
  `notification_backend` 先例同型：Win32 类型不出 .cpp，`RULE-01`）；
  `WindowsDesktopEnvironment` 增 `NotificationsOptions`（默认关，带默认值
  追加，既有构造点不受影响）与 `notification()` 访问器（Linux
  `NotificationsOptions` 先例同型）。
- CI runner 通知区（Explorer 托盘）可用性尚无仓库证据——M4-01/02/03 只证
  交互桌面 / 键盘 / 前台 / 剪贴板；若 runner 无通知区，气球正向场景不可运行
  级取证，按 skip 纪律响亮注记并记录补跑条件（维护者 Windows 机器；DEC-017
  证据分级，不以编译通过冒充运行证据）。
- 托盘图标常驻是产品可见副作用；无菜单裸图标在 M4 是声明过的过渡形态，完整
  托盘交互属 M5。
- 载荷收窄与呈现差异是跨平台行为差异：消费方需经 stable 错误码感知（超平台
  上限 `invalid_argument`），对"通知已到达用户"的任何上层假设都不成立
  （契约 `ok` ≠ 用户已见）。
- 全树 MinGW 交叉构建仍不可达（`MIRA-20260922-001`，pinned executor MinGW
  缺口）：与本决策无关（platform 子集不含 executor），本决策不宣称全树
  MinGW 证据。

## 验证方式

- 冻结拒绝序（cancelled → 空 title → 预算（含平台上限收窄）→ UTF-8）副作用
  前，经 fake 契约测试与 Windows 前端集成测试双面覆盖（M4 计划退出条件
  `DOD-04` 形态）。
- MinGW-w64 交叉构建 + MSVC CI 双工具链门禁（DEC-017 决策 1）。
- 真实 Windows 会话运行取证：open() 建立承载后的 notify 受理（`ok` = shell
  受理）、无交互会话 fail closed（访问器 null）、取消与预算拒绝先行；runner
  通知区不可用时按 skip 纪律记录并挂账补跑条件（负责人：维护者）。

## 关联文档和工作项

- 设计文档第 5、10、18 节；[DEC-006](DEC-006-ui-web-frontend-packaging.md)
  （M5 安装包 = AUMID 载体的交付方）、
  [DEC-009](DEC-009-provider-scope-budget-cancellation.md)（`RULE-07`）、
  [DEC-010](DEC-010-m1-permission-framework.md)、
  [DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)（Linux 通知
  前端与可选依赖纪律先例）、
  [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)（决策 1 双
  门禁、决策 3 Win32 前端并发纪律、决策 4 STA + 泵升级路径）。
- 工作项：[M4 计划](../plans/m4-windows-backend.md) `M4-05`（决策落地进度由
  该工作项跟踪，本记录状态不表示实施进度）。
