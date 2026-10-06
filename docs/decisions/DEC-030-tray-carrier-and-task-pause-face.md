# DEC-030：Tray 进程形态承载机制与任务暂停/恢复面

> 状态：Accepted
> 日期：2026-09-30
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-10` 落地）
> 替代/被替代：无（Windows 通知区承载沿 [DEC-018](DEC-018-windows-notification-carrier.md)；
> Linux 指示器机制为本记录定案）

## 背景与问题

`M5-10` 要求落地 `apps/tray` 托盘进程形态（运行状态显示、任务暂停/恢复、快速
进入 Mirage），约束面：

1. 进程交互纪律：GUI、Tray、CLI 只经 Local IPC 与 Runtime Service 交互
   （`EXEC-02`，设计文档第 12 节）；"不自建 Executor" 指不构建第二套并发
   设施——进程内并发仍由 pinned Executor 承载（`EXEC-01`：每进程 owner 唯一）。
2. 平台承载：Windows 通知区沿用 DEC-018 既有 Shell_NotifyIcon 承载面（该记录
   明示"完整托盘交互属 M5"）；Linux 状态指示器机制随实现定案并记录——现代
   Linux 的事实标准是 StatusNotifierItem（AppIndicator 协议族），其可用性受
   桌面环境制约（KDE/Unity/ayatana 支持；原生 GNOME 无扩展不支持），按平台
   能力如实降级并响亮声明、不为取证伪造能力（M5 计划风险节）。
3. 任务暂停/恢复：pinned 控制面已交付 pause 家族（pinned `runtime.hpp:78-79`
   `pause_task` / `resume_task`；[core-runtime.md](../../third_party/mira/docs/api/core-runtime.md)：
   pause 递增 epoch、在途操作迟到完成作废、暂停期 `begin_operation` 拒绝；
   resume 回 Observing 再进 epoch、**不支持从暂停点的执行级续跑**），而
   MiraHost 未投影、wire 无对应 op——`task.pause` / `task.resume` 走
   [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md) 附加扩展流程。
4. 双工具链门禁（[DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)）
   与公共头纪律（RULE-01）；Overlay 承载的先例形态
   （[DEC-029](DEC-029-desktop-overlay-carrier.md)）。

## 决策

1. **进程形态 = `apps/tray`（`mirage-tray`）单 Executor owner**：SessionClient
   经 Executor blocking worker 驱动（壳内 ShellSession 消费形态先例），托盘
   呈现循环经同型 blocking worker；跨线程通信按语义选型（状态互斥量保护 +
   `submit_delayed` 自续重连 + `submit_auto` 消费调用 future——AGENTS 规则
   3/5/11）。进程不自建线程；断连自续重连（2 s 有界节拍），快照事实源纪律
   （(重)连后 `task.list` 重同步，DEC-012）。
2. **wire 面 = `task.pause` / `task.resume` 附加扩展（DEC-012，协议版本不
   递增）**：请求/应答与 `task.cancel` 同形（`TaskPaused` / `TaskResumed`，
   progress 为受理时点快照）；守卫镜像 `handle_cancel`（unknown not_found、
   recovery-era invalid_state、pinned 拒绝逐字透传）；受理即进度推进（应答前
   发布 `task.updated`）。golden vectors v9 → v10 双端门禁，wire 契约文档
   §4/§6.9/§10 同步。
3. **Windows 承载 = DEC-018 既有 Shell_NotifyIcon 面的产品化扩展**：常驻图标
   + 活动提示（状态行）+ 运行时弹出菜单（状态头 / 暂停 / 恢复 / 打开 Mirage /
   退出，灰显随状态），`TrackPopupMenuEx(TPM_RETURNCMD)` + `SetForegroundWindow`
   焦点纪律；仍不建 AUMID 载体、不引入 WinRT（DEC-018 决策 5 与其 M5 重议
   条件不动——本决策是气球承载的常驻图标面产品化，不是 toast 承载改选）。
4. **Linux 承载 = 会话总线 StatusNotifierItem + 最小 dbusmenu 服务**：导出
   `org.kde.StatusNotifierItem`（属性 vtable 实时 pull + `PropertiesChanged`
   推送）与 `com.canonical.dbusmenu`（固定布局 GetLayout / Event /
   ItemsPropertiesUpdated），向会话总线的 `org.kde.StatusNotifierWatcher`
   注册——按 SNI 规范，**watcher 的接口名与总线名同为
   `org.kde.StatusNotifierWatcher`**（规范中不存在 "ItemWatcher" 接口），
   注册参数为**本连接的会话总线唯一名**（宿主据此在约定条目路径
   `/StatusNotifierItem` 下内省条目），独立验证第 1 轮（SNI 规范核对 +
   tray_backend_test 规范门）修正了实现与冒烟 fake 中两处错写；
   **只依赖既有 gio-unix-2.0 可选依赖族**（[DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md) 决策 3
   纪律，无 GTK / 无 libayatana——否决理由见备选）。无会话总线或无
   StatusNotifierWatcher 宿主 → `open_tray_carrier()` 返回 null，进程响亮
   退出（能力诚实）；指示器宿主中途消失 → 呈现循环带诊断退出，进程随止。
5. **暂停/恢复的驱动语义 = 操作边界驻留（driver 端配套修改）**：M1 过渡驱动
   在 pinned 暂停期拒绝 `begin_operation` 时，检查 pinned 视图——处于 pause
   家族则**驻留**（取消可中断、100 ms 有界轮询切片、单次序列化 `task_view` /
   切片），`task.resume` 后以新 step id 重新受理并续驱（内存延续）。理由：
   无此驻留，驱动把 pinned 拒绝按既有取消语义收账（余步 skip、任务
   Cancelled），"暂停"对当下唯一存在的任务形态等效于取消——语义谎言。与
   pinned"resume 重新驱动、不支持执行级续跑"的关系如实声明：pinned 语义约束
   的是 pinned 自驱循环的重建行为；宿主侧 M1 驱动的内存延续属驱动形态自治
   （DEC-007/DEC-008 过渡形态），且与 pinned epoch 纪律相容（resume 后新
   step id 落入当前 era，暂停前操作的迟到完成照旧 stale 结算）。
6. **快速进入 Mirage = 兄弟二进制发现 + 分离进程拉起**：默认解析托盘可执行
   同目录的 `mirage-desktop`（产品布局由 M5-11 打包交付），`--shell` 可覆盖；
   解析失败 → 菜单禁用（不伪造入口），拉起失败 → stderr 响亮记录。托盘不
   等待壳就绪（设计文档 §12.1 的"拉起兄弟二进制并等待就绪"属 service start
   语义，壳由用户会话持有）。
7. **状态显示 = 事件折叠 + 快照重同步**：`host.status` + `task.updated` 折叠
   为单行状态（图标 tooltip 与菜单头），跟踪策略 = 最近 Active/Paused 任务
   胜出、终态释放；会话 / 工作流 / 权限事件不驱动指示器（工作区仍是它们的
   呈现面，DEC-013 §3.6）。

## 备选方案

- **libayatana-appindicator（Linux 指示器库）**：否决。appindicator3 依赖
  GTK3 全家（主循环、菜单对象模型），把第二 UI 框架引入 Platform Backend，
  突破 DEC-015 立定的 gio 族依赖边界；纯 GDBus 实现同协议且零新依赖。
- **XEmbed 系统托盘（X11 老协议）**：否决。遗留协议（Xfpanel/MATE 一线），
  需要 X 窗口嵌入与绘制面；SNI 是现行标准，XEmbed 环境通常并存 SNI 支持。
- **CEF / Web 呈现的托盘菜单**：否决。托盘是平台原生面（通知区 / 指示器），
  与 DEC-006 的壳选型无关；CEF 透明窗承载托盘菜单成本与风险无意义。
- **暂停语义 = 仅契约投影、驱动不加驻留**：否决。对当下唯一任务形态（M1
  驱动任务）等效于"暂停即取消"，产品语义谎言（见决策 5）。
- **独立 `apps/overlay` / 托盘共用呈现进程**：否决（DEC-029 已否决 Overlay
  进程形态；托盘按计划明示为 `apps/tray` 独立进程形态，两者不合并——生命周期
  与呈现面完全不同）。

## 影响与风险

- `apps/tray` 新进程形态（M4-06 进程清单增员：`mirage-service` / `mirage` /
  `mirage-desktop` / `mirage-tray` / `mirage-devbridge`）；M5-11 打包需纳入
  `.deb` / 安装器内容清单。
- 托盘图标资产未交付：SNI `IconName` 指向 `mirage-tray`，无已装图标时指示器
  宿主显示占位图（呈现差异如实声明；图标资产随 M5-11 打包交付）。
- 运行级取证分级（DEC-017）：Linux 注册 / 菜单动作 / 干净停机已在真实会话
  总线取证（Fake SNI watcher 宿主 + 真实 mirage-service；见 M5-10 验证记录）；
  真实指示器宿主（KDE/ayatana）的视觉呈现与点击、Windows 通知区图标 / 菜单 /
  穿透行为需维护者机器取证（CI runner 无交互桌面，按 skip 纪律登记补跑条件）。
- M1 驱动任务的暂停语义边界：暂停在操作边界生效（在途动作完成收尾，其完成
  按 pinned epoch 结算 stale——产品步记录仍记 ok，如实声明）；Idle-era 任务
  （已受理未驱动）的暂停呈现与恢复续驱的端到端行为留独立测试验证轮以夹具
  钉住（本轮冒烟未覆盖该组合）。
- 全树 MinGW 交叉构建仍不可达（`MIRA-20260922-001` 维持）：Win32 托盘前端
  仅依赖 user32/shell32（双工具链 SDK 自带），platform 子集交叉验证可行。
- `task.pause` / `task.resume` 为 v1 附加扩展：旧客户端零影响；新客户端对旧
  服务端按未知 op 稳定错误降级（DEC-012 既有纪律）。

## 验证方式

- 契约/状态机测试（无平台依赖）：fake carrier 注入托盘核心；wire 侧 golden
  vectors 双端（v10）。
- Linux 运行级：无会话总线 / 无 StatusNotifierWatcher → null 响亮退出；Fake
  SNI watcher 宿主 → 注册受理、dbusmenu 布局/事件、SIGTERM 有界干净停机
  （teardown 全序在案）。
- Windows 运行级（维护者机器兜底）：通知区图标 / tooltip / 菜单灰显与点击、
  无交互桌面 probe null。
- 双工具链：MSVC CI windows 作业全树 + MinGW platform 子集交叉编译。
- 独立测试验证轮：tray 核心的 fake-carrier 状态机测试 + task.pause/resume
  服务面（Idle-era / Active-era / 暂停期取消 / 驱动驻留恢复）夹具化钉住。

## 关联文档和工作项

- 设计文档第 12 节；[DEC-007](DEC-007-local-ipc-and-runtime-service.md) /
  [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（IPC 与附加扩展
  流程）、[DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)
  （可选依赖族）、[DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)
  （双工具链）、[DEC-018](DEC-018-windows-notification-carrier.md)（Windows
  通知区承载与线程亲和模型）、
  [DEC-024](DEC-024-desktop-atom-toolset.md) / DEC-029（缝与承载先例形态）。
- 工作项：[M5 计划](../plans/m5-desktop-product.md) `M5-10`（本记录即"Linux
  状态指示器机制随实现定案并记录"义务的兑现）。

## 2026-10-06 产品进程所有权修订

[DEC-045](DEC-045-tray-runtime-owner.md) 替代本决策中产品托盘作为独立 Service
客户端的部分：托盘内嵌 RuntimeService，复用其唯一 Executor，并持有独立前端
子进程。本文其他 API/平台/任务语义及历史验收保留；新拓扑以 M6-25 证据为准。
