# M4：Windows Backend（UIA / Win32 / Capture / Input）

> 状态：Completed（`M4-01` … `M4-07` 全部完成，退出复核通过）
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M2](m2-desktop-environment.md)（已完成：Desktop Environment 九个 Provider
> 契约、SemanticSnapshot、ElementTarget 解析顺序契约；Linux Backend 同型骨架先例）
> 建议发布点：`release-delta`（tag 待维护者授权后创建）
> 更新日期：2026-09-25（`M4-07` 退出复核通过，里程碑完成）

## 目标

在同一套 Desktop Environment API、Observation 与 Action 语义上交付 Windows
Backend（设计文档第 10、18 节第四阶段）：UI Automation 承载语义树，Win32 承载
窗口 / 进程 / 采集 / 输入 / 剪贴板，使 DesktopObservation、ElementTarget 与桌面
动作在 Linux 与 Windows 上行为一致；产品进程（Runtime Service、IPC、持久化）
在 Windows 上可构建可运行（DEC-007 命名管道传输）。

## 范围与非目标

范围：

- Windows Backend（`platform/windows`）：`WindowsDesktopEnvironment` 与 Win32
  前端——WindowProvider（枚举 / 前台 / 激活 / 几何）、ScreenProvider（显示器 /
  显示 / 窗口 / ROI 采集）、InputProvider（键盘 / 文本 / 指针）、UIA
  AccessibilityProvider（语义树 → SemanticSnapshot、ElementTarget 解析与语义
  动作）、ClipboardProvider、ApplicationProvider、NotificationProvider；能力面
  与 Linux Backend 一一对应，语义对齐 DEC-005 / DEC-009 / DEC-010。
- Windows 工具链与验证门禁（DEC-017）：MinGW-w64 交叉编译门禁（Linux 开发机）
  + MSVC CI 门禁（GitHub windows runner）；win32 前端集成测试在真实 Windows
  会话取证。
- 产品进程 Windows 化：Local IPC 命名管道传输（DEC-007，`stream_windows`）、
  持久化 Windows 路径与存储、ProcessProvider CreateProcess 承载，使
  `apps/service` 等进程形态在 Windows 可构建。
- `integration/mira` 绑定在 Windows 环境上的 observe / execute 面复核与
  Windows 端到端闭环取证（observe → action → observe）。

非目标：

- Windows 安装包与更新器（DEC-006 分发形态属产品化交付，随 M5 落地取证）。
- Windows 通知中心的完整产品化交互（通知 Provider 的承载机制已定案
  [DEC-018](../decisions/DEC-018-windows-notification-carrier.md)——
  `Shell_NotifyIcon` 气球/横幅承载 + open() 探测 fail closed 降级；仅交付
  契约面等价能力）。
- 采集路径的性能升级（Windows.Graphics.Capture / DXGI Duplication 为后续
  升级路径，DEC-017；M4 只交付 GDI 同步采集并如实声明边界）。
- Desktop GUI 产品界面（M5）。
- UIA 事件流驱动的快照增量（与 M2 同一立场：全量快照 + 行为结果驱动刷新，
  事件驱动作为后续演进）。

## 设计与决策依据

- [设计文档](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md) 第 5、
  6、7、9、10、17、18 节（Windows Backend 组成与第四阶段路线）。
- [DEC-005](../decisions/DEC-005-desktop-observation-contract.md)：
  DesktopObservation schema 1.0、SemanticSnapshot、ElementTarget 解析顺序
  （reference → accessibility → Mirador 视觉 → VLM），Windows 后端不得偏离。
- [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)：命名管道
  （Windows）为协议 v1 的既定传输，M4 兑现。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)：范围 /
  预算 / 取消语义全 Provider 沿用（`RULE-07`）。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)：Permission 判定
  挂点；Windows 动作沿用 M2 冻结词表（`window.activate` / `screen.capture` /
  `input.inject` / `clipboard.read` / `clipboard.write` 等），不新增判定语义。
- [DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)：
  Linux Backend 的并发纪律与可选依赖纪律先例（单连接 + 互斥锁串行、stub fail
  closed、用户前缀引导），Windows 侧同型（DEC-017）。
- [DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)：
  Windows 工具链双门禁（MinGW-w64 + MSVC）、Win32 前端并发纪律、COM 初始化
  模型、编码纪律、采集模型（M4-01 登记）。
- [DEC-018](../decisions/DEC-018-windows-notification-carrier.md)：Windows
  NotificationProvider 承载机制——`Shell_NotifyIcon` 气球/横幅 + open() 探测
  fail closed 降级，toast 否决并留 M5 重议触发（M4-05 登记）。
- [M2 计划](m2-desktop-environment.md)：Windows Backend 的工作项拆分、测试拓扑
  与验证记录形态以 Linux Backend 先例为模板。

## 工作项

- [x] `M4-01` Windows Backend 骨架闭环（Win32）：`WindowsDesktopEnvironment`
      （公共头零 Win32 类型）+ 私有 Win32 前端——WindowProvider（`EnumWindows`
      枚举与几何、`GetForegroundWindow` / `SetForegroundWindow` 前台与激活）、
      ScreenProvider（`EnumDisplayMonitors` 显示器、GDI `BitBlt` 显示 / 窗口 /
      ROI 采集，Bgra8）、InputProvider（`SendInput` 键盘 / `KEYEVENTF_UNICODE`
      文本 / 指针移动与按键、`GetCursorPos` 只读查询）；预算前置拒绝、取消先于
      副作用、UTF-8 ↔ UTF-16 转换边界与 Linux 后端语义一致；CMake Windows 分支
      接线（user32 / gdi32）；MinGW-w64 交叉编译门禁与 MSVC CI 作业（DEC-017
      登记）；win32 前端集成测试在真实 Windows 会话取证。
- [x] `M4-02` AccessibilityProvider（UI Automation）：COM 初始化模型兑现
      （DEC-017）、UIA 语义树采集 → SemanticSnapshot（节点预算 fail closed、
      角色词表投影）、ElementTarget 解析（reference / semantic / structural；
      visual/spatial/raw `unsupported_hint` fail closed）与语义动作
      （Invoke / Value 模式；语义调用优先于键鼠，设计文档第 9 节）；UIA 前端
      类型不出公共头（`RULE-01`）。
- [x] `M4-03` ClipboardProvider（Win32）与 ProcessProvider Windows 承载：
      `CF_UNICODETEXT` 读写、预算与 UTF-8 转换（对齐 M2-04 契约语义）；
      ProcessProvider 经 `CreateProcess` 的有界执行与取消（对齐 M1-05 语义）。
- [x] `M4-04` ApplicationProvider（Windows）：应用发现（开始菜单快捷方式 /
      注册表 Uninstall 面，机制随实现定案并记录）、启动 / 运行态 / 终止
      （对齐 M2-05 语义：tracked 实例、TERM 等价的协作式收尾、不强杀）。
- [x] `M4-05` 通知承载决策与 NotificationProvider（Windows）：承载机制已定案
      （[DEC-018](../decisions/DEC-018-windows-notification-carrier.md)——
      `Shell_NotifyIcon` 气球/横幅承载 + open() 探测 fail closed 降级，toast
      否决并留 M5 重议触发）；契约面等价能力已落地（`notify` 冻结拒绝序 +
      平台载荷收窄 63/255 UTF-16 码元 + 嵌入 NUL 拒绝，均副作用前；托盘
      承载图标常驻、无泵回调如实声明）。
- [x] `M4-06` 产品进程 Windows 化：Local IPC 命名管道传输（DEC-007 兑现，
      `stream_windows`；帧格式与协议 v1 不变，golden vectors 复用）、持久化
      Windows 路径与存储（`store_windows`）、`apps/service` / CLI /
      tray 进程形态可构建；全树 MSVC 构建通过。
- [x] `M4-07` Windows 端到端闭环与里程碑退出复核：`integration/mira` 绑定在
      Windows 环境的 observe / execute 面复核（能力如实上报）、真实 Windows
      会话端到端取证（observe → action → observe）；退出条件逐项独立取证
      （同 M2 退出复核形态）。

拆分纪律：与 M2 相同——骨架先于功能（`M4-01` 先以最小动作打通闭环）、语义
Provider（UIA）先于外围 Provider、产品进程化（`M4-06`）不阻塞 Backend 工作项。
契约已随 M2 冻结，Windows 侧不再新建 Provider 契约；发现契约在 Windows 不可
承载时，按工程规范第 9.4 节与决策流程处理，不得静默分叉语义。

## 风险与阻塞

- 开发与 CI 环境均为 Linux，无本机 Windows：编译证据分两级（MinGW 交叉 +
  MSVC CI，DEC-017）；运行取证依赖 GitHub windows runner 的桌面会话能力——
  **已在 `M4-01` 首个 PR 确认可用**（交互输入 / 窗口创建 / 前台激活均真实
  执行，见验证记录 2026-09-22 CI 取证），后续工作项默认可经 CI 取证。仍无法
  取证的工作项保持未勾选并记录补跑条件（工程规范第 4 节）。
- MinGW-w64 与 MSVC 的差异（异常模型、`std::filesystem` 行为、Win32 头宏与
  结构体布局、警告面）：双工具链门禁缓解；MSVC 为产品分发（DEC-006）主工具
  链，MinGW-only 的代码不被接受。
- 前台激活的平台限制（`SetForegroundWindow` 前台锁定、UIPI 完整性级别）：
  与 X11 EWMH 同类的系统安全边界，如实返回失败并记录，不虚报可激活性。
- UIA 线程模型（MTA 调用型 vs STA 消息泵事件型）影响 Executor 互操作形态
  （EXEC-03）：已按 DEC-017 决策 4 兑现（`M4-02`）——调用型 MTA 每调用承载
  + 进程级 MTA 锚；事件型 STA + 消息泵升级路径保留。新事实（`M4-02` 取证）：
  UIA 跨进程调用不可取消、桌面可能存在挂死 provider，live-tree 解析以
  wall-clock 预算 + 事务超时 + 调用方执行上下文三级兜底（见 M4-02 验证记录
  限制节）；worker 隔离的结构性升级是否立项由维护者裁决。
- GitHub windows runner 桌面能力不足时 win32 集成测试不可运行的风险已消除
  （`M4-01` 首跑确认 runner 有真实交互桌面，测试常驻 CI 门禁）；后续风险仅
  剩 runner 环境演进（如镜像更换影响桌面能力），由门禁持续暴露。
- 沙箱无 root：MinGW-w64 以 `apt-get download` + `dpkg -x` 用户前缀提取引导
  （DEC-015 第 4 条同一策略，`M4-01` 已实测可下载）。

## 测试与退出条件

- [x] Linux 主机矩阵不回归：`debug`、`release`、`asan`、`ubsan` 预设构建 +
      `ctest` 全绿无 skip，`tsan` 按本机注意事项运行；`mirage-format-check`
      与 `mirage-boundary-check`（含新增公共头）通过（`DOD-01` / `DOD-03`）。
- [x] Windows 编译门禁：MinGW-w64 x86_64 交叉 configure + build 通过；MSVC
      （CI windows runner）configure + build 通过，可运行的纯逻辑测试在
      runner 上实跑（`DOD-03` 的跨平台形式，DEC-017 证据分级）。
- [x] Windows Provider 契约不偏离 DEC-005 / DEC-009：预算拒绝、取消先于
      副作用、fail closed 语义经既有 fake 契约测试与 Windows 前端集成测试
      双面覆盖；win32 前端测试在真实 Windows 会话取证（CI runner 或维护者
      机器；不可运行时记录原因与补跑条件）（`DOD-04`）。
- [x] Windows 端到端闭环：observe 返回含 SemanticSnapshot 的 Observation，
      ElementTarget 经解析执行后 Observation 更新（`M4-07`）。
- [x] 产品进程 Windows 化：`apps/service` 形态在 Windows 上完成 IPC 往返
      （命名管道传输 + golden vectors 一致）（`M4-06`）。
- [x] 决策与文档同步：DEC-017 及后续决策、设计文档注记、总计划状态与验证
      记录同步（`DOD-05`）；Commit / MR 符合工程规范第 10 节（`DOD-06`）。

## 验证记录

2026-09-22：`M4-01` Windows Backend 骨架实现与编译级验证完成；运行级取证随本
分支 PR 的 CI windows 作业执行（见限制与补跑条件）。

- 范围：`platform/windows` 新增公共头 `windows_desktop_environment.hpp`
  （`WindowsDesktopEnvironment` + `Win32Options`，公共头零 Win32 类型）与私有
  Win32 前端 `win32_backend.{hpp,cpp}`（WindowProvider：`EnumWindows` 枚举 /
  `GetForegroundWindow` / `SetForegroundWindow` 激活（还原最小化窗口）；
  ScreenProvider：`MONITORINFOEXW` 显示器 + GDI `BitBlt` → 负高 DIBSection 的
  Bgra8 显示 / 窗口 / ROI 采集；InputProvider：`SendInput` chord（词表序
  ctrl/alt/shift/meta）+ `KEYEVENTF_UNICODE` 逐 UTF-16 码元文本 + 绝对虚拟桌面
  指针 + `GetCursorPos` 只读查询）。校验序对齐 M2 fake 契约（cancelled →
  timeout → 预算/UTF-8 → 名称解析）；部分注入 best-effort 释放（不漏键）；
  `parse_window_id` 19 位上限保证无异常逃逸；window id = HWND 十进制串、
  display id = `szDevice` UTF-8（DEC-017 决策 7）。`WindowsDesktopEnvironment`
  open() 以虚拟屏尺寸探测交互桌面，失败访问器 null fail closed。CMake：平台库
  Windows 分支接线 user32/gdi32（无 stub 变体——系统库必然在位，唯一诚实失败
  是 open() 探测）；`cmake/toolchains/mingw-w64-x86_64.cmake` 交叉工具链
  （`$MIRAGE_MINGW_PREFIX` → `bin`|`usr/bin` → `$PATH`）；CI windows 作业
  （MSVC，platform 子集 + 可运行测试子集）；`CheckFormat.cmake` 补
  `platform/*/include/*.hpp` glob（既有缺口，新增公共头随之被格式门禁覆盖）。
  新增 `tests/platform/win32_backend_test.cpp`（7 场景 131 断言，不变量式：
  报告结果必须与独立观测的桌面状态一致；前台锁 / session 0 拒绝为响亮注记的
  合法结果，契约拒绝路径严格断言）。决策记录
  [DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)
  登记。
- 依据：设计文档第 10、18 节；`DEC-005` / `DEC-007` / `DEC-009` / `DEC-010` /
  `DEC-015`（先例）/ `DEC-017`；`RULE-01` / `RULE-03` / `RULE-07` / `RULE-08`。
- 验证（Independent-Verification-Agent，两轮；Linux x64，Ubuntu 24.04，GCC
  13.3.0 + MinGW-w64 GCC 13.2.0 posix）：
  - Linux 主机回归：`debug` / `release` / `asan` / `ubsan` 四预设 configure +
    build + ctest 均 **32/32 通过、0 skip**（M3 后基线，非 M2 的 24；Linux 上
    未新增注册任何测试）；`tsan` 经 `setarch x86_64 -R ctest` 32/32。修复轮
    仅触及 Windows 分支专属编译单元（Linux 构建图 `ninja: no work to do`），
    四预设结论现势。
  - 门禁：`mirage-format-check` 通过（含新 glob 扫描 `platform/*/include`，
    既有 `linux_desktop_environment.hpp` 首扫通过）；`mirage-boundary-check`
    **0 violations in 37 headers**（含新增 windows 公共头）。
  - MinGW 交叉门禁：无 root 用户前缀引导（`apt-cache depends --recurse` 闭包
    11 包 `apt-get download` + `dpkg -x` 至 `~/.local/mirage-mingw`，GCC
    13.2.0 posix，前缀内补 alternatives 符号链）；交叉 configure +
    **7/7 目标构建成功 0 诊断**（mirage_desktop / mirage_platform /
    win32_backend_test / 4 个可移植测试），`file` 认证 `win32_backend_test.exe`
    等 5 个 exe 为 `PE32+ executable (console) x86-64, for MS Windows`、平台库
    成员为 amd64 COFF；`win32_backend.cpp` 单 TU 以完整仓库警告集 + `-Werror`
    零诊断。
  - 独立验证发现并回归主循环修复 5 处生产缺陷（首轮交叉编译失败暴露）：
    `MONITORINFOW` 应为 `MONITORINFOEXW`（szDevice 仅在 EX 变体）；
    `SendInput` 形参不可 const；`normalize` lambda 返回类型推导冲突（改显式
    `-> WORD` + `std::clamp`）；冗余 cast（`-Wuseless-cast`）；未使用
    `modifier_vk`（删除）。另有 `parse_window_id` 20→19 位防
    `std::stoull` 溢出抛出。测试侧独立修复 3 处（缺公共头 include、UNICODE
    定义、格式化）。
- 限制与补跑条件：`win32_backend_test` 的**运行级证据**本机不可产生（无
  Windows 会话）——由本分支 PR 的 CI windows 作业首跑取证（同时即 DEC-017
  决策 9 对 runner 交互桌面能力的确认；焦点相关断言（activate 成功分支、键盘
  送达）在 runner 拒绝取焦时按设计输出注记而非失败）；MSVC 工具链行为随同一
  作业首跑确认（本机仅 MinGW 证据）；Windows 侧无 sanitizer 矩阵（Linux 侧
  成立）；负责人：维护者（合并裁决）。
- 同步：[M4 计划](m4-windows-backend.md)（本记录）、
  [DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)
  （新）、总计划当前状态与里程碑索引、README（交叉构建指引）。

2026-09-22：`M4-01` CI 门禁全绿与 Windows 运行级取证完成，工作项完成。

- 范围：分支 `feat/m4-win32-backend-skeleton` PR [#39](https://github.com/Linductor-alkaid/mirage/pull/39)
  CI 迭代中修复三处 Windows 门禁问题并取证：
  1. `MirageWarnings.cmake` 为 MSVC 给出独立警告旗标（`/W4 /permissive-
     /Zc:__cplusplus` + 精选 C4xxx 提示 + `/WX`）——GCC 旗标（`-Wextra` 等）
     在 MSVC 上是 D8021 硬错误，此为 windows 门禁的首个真实产出；
  2. `win32_backend.cpp` 命名键表 `std::pair` 花括号初始化经模板转发触发
     C4242（int→WORD，`/WX` 下致命）——改普通聚合类型，窄化发生在常量可见
     的聚合初始化点；
  3. windows 作业增加 win32 测试详细输出步骤（通过时 ctest 隐藏 stdout/
     stderr，断言计数与环境注记需显式保留）。
- 验证（CI run 35634388282 与 35634853372，windows-latest，MSVC 19.51
  （VS 18 2026））：全树 configure 成功（pinned mira/mirador 均在 Windows
  校验通过，"Mira target platform: Windows"）；**windows 作业 8 步全绿**：
  MSVC 构建 mirage_desktop / mirage_platform / 5 个测试目标成功，ctest
  **5/5 通过**，其中 `win32_backend_test` 在 runner 真实交互桌面（虚拟屏
  1024x768）上 **128 checks, 0 failures**——无环境受限注记输出，即前台激活
  成功、键盘送达（WM_CHAR / WM_KEYDOWN）与指针往返等运行级场景全部真实执行
  （DEC-017 决策 9 的 runner 桌面能力就此确认）；MSVC 编译级证据就此闭合
  DEC-017 决策 1 的"双工具链门禁"。Linux 矩阵（debug/release/asan/ubsan/
  tsan）、format、boundary、frontend 作业同轮全绿。
- 限制与挂账：同轮 Linux `debug` 作业一次 `event_subscription_test` 失败
  （`wait_terminal` 30 s 活跃性预算耗尽，`event_subscription_test.cpp:1283`
  `settled.has_value()`），同一提交复跑即绿——与本分支改动无关（该测试与
  Windows 路径零交集，前一轮同代码 run 35634388282 全绿），属 M1.5-02 起
  记录的 service serial backlog 病理在活跃性断言上的再次显形（此前
  `runtime_service_test` 同类问题已于 M2-06 定性修复；本例预算已是放宽后的
  30 s 仍被击穿，指向单次 inspect 调用占满剩余预算的病理）。挂账：由维护者
  在 runtime 域单独排查定案（负责人：维护者；触发条件：再次复现即立项），
  不阻塞本工作项。
- 同步：本验证记录、`DEC-017` 变更记录、PR #39 CI 取证。

2026-09-22：`M4-02` AccessibilityProvider（UI Automation）完成。

- 范围：`platform/windows` 新增私有 UIA 前端 `uia_backend.{hpp,cpp}`
  （`UiaBackend` 实现 M2-03 冻结的 `AccessibilityProvider`，AT-SPI2 先例同
  型）——`semantic_snapshot`（window_id = HWND 十进制串经 `ElementFromHandle`
  直达，控制视图 DFS 采集；超 `max_nodes` 预算 `snapshot_too_large` 拒绝不
  截断；`@e1..@eN` 引用注册表随成功快照整体替换（registry-miss 如实
  `not_found` 而非 `invalid_argument`）；application = 窗口进程 exe 名、
  window_title = `GetWindowTextW` 与 WindowProvider 同源；角色词表按 UIA
  ControlType 投影到 DEC-005 冻结词表（MinGW 宏常量 / MSVC 枚举成员同名
  `UIA_*ControlTypeId`，本地化 `LocalizedControlType` 不入词表，未知类型如实
  "unknown"））、`activate_element` / `set_text`（解析序 reference →
  semantic（桌面根 BFS，4096 访问预算 + 30 s wall-clock 预算）→
  structural（`/role/name` 成对段，AT-SPI 冻结语法同型）；visual/spatial/raw
  `unsupported_hint` fail closed；语义动作经 InvokePattern / ValuePattern，
  只读元素 `unsupported_element`）。COM 模型兑现 DEC-017 决策 4：每方法调用
  在调用线程 `CoInitializeEx(COINIT_MULTITHREADED)`，首个成功 scope 永久
  保留为进程级 MTA 锚（恰好一个有界引用，M2-03 对象池先例同型）保注册表
  元素指针跨调用有效；`RPC_E_CHANGED_MODE` 如实 `io_error`。共享 Win32 助手
  提取至 `win32_util.hpp`；`WindowsDesktopEnvironment` 增 `UiaOptions`（默认
  关）与 `accessibility()`，open 失败 null fail closed；CMake 接线
  `ole32`/`oleaut32`（GUID 经 `__uuidof` 解析，双工具链同声明）。
- 依据：设计文档第 9、10 节；`DEC-005` / `DEC-009` / `DEC-010` / `DEC-015`
  （先例）/ `DEC-017`；`RULE-01` / `RULE-03` / `RULE-05` / `RULE-07`。
- 验证（Independent-Verification-Agent，两轮 + 修复复验 + 看门狗改造轮；
  Linux x64 Ubuntu 24.04 GCC 13.3.0 + MinGW-w64 GCC 13.2.0 posix）：
  - Linux 主机矩阵不回归：debug / release / asan / ubsan / tsan 五预设
    ctest **32/32 通过 0 skip**（基线不变，Linux 不编译 Windows TU）；
    `mirage-format-check` 通过；`mirage-boundary-check` 0 violations in 37
    headers（公共头仅扩展 `windows_desktop_environment.hpp`）。
  - MinGW 交叉门禁：8 目标（mirage_desktop / mirage_platform /
    uia_backend_test / win32_backend_test / 4 个可移植测试）构建成功，
    `MIRAGE_WARNINGS_AS_ERRORS=ON` 0 诊断；`uia_backend_test.exe` 认证
    PE32+ x86-64。
  - 独立验证发现并回归主循环修复 3 处生产缺陷：① `resolve_locked`
    registry-miss 分支丢失 found 标记（stale ref 报 `invalid_argument` 而非
    契约 `not_found`）；② 重写时遗漏注册表回填循环（快照成功后引用全部
    not_found——AT-SPI 模板的 fill loop 在重写中丢失，CI 首轮暴露）；
    ③ 上述补丁引入 use-after-move（元素先 move 进对齐数组、子枚举再用空
    指针，快照塌缩为根节点）。另有 CI 首轮暴露 MSVC/MinGW 控制类型命名
    假设错误（两 SDK 实为同名 `UIA_*ControlTypeId` + `CONTROLTYPEID`，归一
    化 shim 撤销）。
  - 运行级取证（CI run 35685654480，windows-latest，MSVC 14.51，真实交互
    桌面）：windows 作业 8 步全绿，ctest 6/6；`uia_backend_test` **108
    checks, 0 failures**（45.5 s）——in-proc 场景全部真实执行：快照结构
    （fixture 树 9 节点、ref 连续、parent 不变量、button/text 节点角色与
    几何）、预算 fail-closed 不清注册表、注册表整体替换、**reference 解析
    经 InvokePattern 真实点击送达**（泵内确认 BN_CLICKED 恰好 +1）、
    **ValuePattern 文本写入经 GetWindowTextW 逐字符回读**、visual/spatial/
    raw 提示 `unsupported_hint`、取消先于全部校验、隐藏窗口语义探针。
  - live-tree 桌面扫描（semantic / structural 解析环）经 `--scan-probe`
    子进程隔离 + 45 s 看门狗取证：**structural 解析环真实命中并点击成功**
    （跨进程路径）；semantic 解析环撞上 runner 桌面的挂死 provider（wedged
    UWP 宿主类窗口），**30 s wall-clock 预算 + 1 s 事务超时被证实生效**
    （30.09 s 诚实返回 `not_found`，而非挂死）；负向全桌面扫描同样挂死、
    被看门狗终止并按 skip 纪律响亮注记环境受限。
- 限制与补跑条件：① semantic live-tree 解析的**命中**场景在 runner 桌面
  因 wedged provider 耗尽预算返回 `not_found`（30 s 预算内无法保证命中——
  解析语义契约经 fake `provider_contract_test` 覆盖，真实命中取证挂账）；
  补跑条件：维护者 Windows 机器或 runner 桌面不再携带挂死 provider 窗口
  （负责人：维护者）。② 深度诊断定案（根因 = UIA 跨进程 COM 调用不可取消
  × 桌面存在挂死 provider；`IUIAutomation2` 事务超时是客户端提示而非传输
  层硬界，AT-SPI 的 D-Bus 每调用超时在 UIA 无等价物）：有界机制只能约束
  "慢 provider"，单个 wedged 调用仅由调用方执行上下文（Executor 调用超时）
  兜底——与 DEC-017 决策 3 的单调用收敛纪律同型；共享互斥锁下 wedged 调用
  会使其后的调用（含取消检查）排队，取消是副作用前观察而非调用中打断。
  生产侧结构性修复（UIA client 迁入 Executor blocking worker + 调用方有界
  等待，DEC-017 决策 4 预留的升级路径）是否立项由维护者裁决：当前证据为
  预算生效、不挂死、structural 环可用，紧迫性下降。③ 依赖反馈台账
  `MIRA-20260922-001`（pinned executor 在 MinGW posix 模型下无法编译，
  MSVC 不受影响；M4-06 全树 MinGW 交叉构建时需复核）。
- 同步：[M4 计划](m4-windows-backend.md)（本记录）、
  [DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)
  变更记录、依赖反馈台账、PR #40 CI 取证。

2026-09-22：`M4-03` ClipboardProvider（Win32）与 ProcessProvider Windows 承载
完成。

- 范围：`platform/windows` 的 `Win32Backend` 增实现 `ClipboardProvider`
  （M2-04 冻结契约，X11 先例同型）——CF_UNICODETEXT 读写；读侧以格式计数
  区分空剪贴板（`not_found`）与非文本内容（`unsupported_content`），超读
  预算 `clipboard_too_large` 不截断；写侧保持副作用前冻结拒绝序（取消 →
  零预算 → 载荷预算 → UTF-8）；`OpenClipboard` 5 s 有界重试（25 ms 切片
  观察取消）；`EmptyClipboard` 之后的失败如实 `io_error`（不可回滚副作用
  如实声明）。`WindowsDesktopEnvironment` 自身实现 `ProcessProvider`
  （M1-05 语义，无条件可用）：命令经 `cmd.exe /c`；整树置于
  `KILL_ON_JOB_CLOSE` Job Object，`TerminateJobObject` 承载超时/取消整组
  拆除（`kill(-pid, SIGKILL)` 等价物）；取消 25 ms 切片观察；
  `PeekNamedPipe` 非阻塞排水（无线程，`RULE-03`）；每流预算超限
  `output_truncated` 而非失败；管道字节原样透传（编码属于子程序，注释与
  本记录声明）。`win32_util.hpp` 增 `UniqueHandle`。
- 依据：设计文档第 5、10、18 节；`DEC-005` / `DEC-009` / `DEC-010` /
  `DEC-015`（先例）/ `DEC-017`；M2-04 / M1-05 冻结语义；`RULE-01` /
  `RULE-03` / `RULE-05` / `RULE-07`。
- 验证（Independent-Verification-Agent；Linux x64 GCC 13.3.0 + MinGW-w64
  GCC 13.2.0 posix + CI windows runner MSVC）：
  - Linux 主机矩阵不回归：debug / release / asan / ubsan / tsan 五预设
    32/32 通过 0 skip；`mirage-format-check` / `mirage-boundary-check`
    （37 headers）通过；MinGW 交叉 8 目标 `MIRAGE_WARNINGS_AS_ERRORS=ON`
    0 诊断，7 个 exe 认证 PE32+。
  - 新增 `tests/platform/win32_clipboard_process_test.cpp`（12 场景）：
    剪贴板制造状态（raw 清空 → `not_found`；raw 私有格式 →
    `unsupported_content`）、多字节 UTF-8 往返 + 独立 `GetClipboardData`
    对照、冻结拒绝序 + 读回不变量、同线程持有探测的有界性；进程拒绝序 +
    越预算金丝雀文件证明（拒绝的命令从未产生进程）、`exit 7` 结构化退出
    码、stderr 捕获、1 s 预算超时（`pre_kill_exit=259` 证实命令真活）、
    定时器线程触发的中途取消（SIGALRM 探针类比）、10k 行发射器的 1024
    字节截断与 512 KB 全量对照。
  - 运行级取证（CI run 35709166819，windows-latest，MSVC，真实交互桌面）：
    windows 作业全绿，ctest 7/7；`win32_clipboard_process_test`
    **129 checks, 0 failures（4.94 s）**；`win32_backend_test` 128/0 与
    `uia_backend_test` 108/0 同轮回归通过。
- 调试过程与平台事实（对后续 Windows 工作有复用价值）：
  - CI 迭代 6 轮定位到 execute 在真实桌面全量烧预算的根因：因子矩阵探针
    （裸启动 17 ms、v1-v6 全部 3 s 内退出、v5 逐行复刻 1 个 25 ms 切片即
    退出）与 provider 交错同进程对比，verdict 证据
    （`pre_kill_exit` 携带真实退出码、`GetProcessTimes` 有 exit time、
    进程句柄已 signaled 而两捕获管道仍 open）裁决——**命令毫秒级正常退
    出，是管道 EOF 被扣**：隐藏控制台的 conhost 载体（由 cmd 侧创建，
    `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 约束不到该层）复制了管道写端且
    比 cmd 长寿。**平台事实：Windows 控制台会话上管道 EOF 不可作为命令完
    成信号**。修复：完成谓词改为"直接子进程退出 + 两捕获管道排空至空"；
    精确句柄继承（`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` +
    `EXTENDED_STARTUPINFO_PRESENT`，libuv/Python 子进程同款）与
    `CREATE_SUSPENDED` 先预算后执行（拒绝的赋值不留运行中的无预算命令）
    一并落地。超时路径 verdict 证实 `pre_kill_exit=259`（命令真活）。
  - 派生语义澄清：`start /b` 分离后代后主 cmd 立即退出 → 调用 ok 完成，
    分离后代由整树拆除（`KILL_ON_JOB_CLOSE`）收割——契约"no descendant
    survives the call"由 job 保证，不依赖 EOF。timed_out 路径由前台长命
    令场景独立覆盖。
- 限制与补跑条件：① 同线程持有探测在 runner 上探测不成立（raw
  `OpenClipboard` 二次打开未失败），held-peer 有界性场景环境受限响亮跳
  过，补跑条件 = 维护者 Windows 机器（负责人：维护者）；② 管道输出编码
  为子程序自身编码（cmd 为 OEM 代码页），契约 UTF-8 假设仅对输出 UTF-8 的
  程序成立，断言已按 ASCII 子串降敏；③ 后代收割依赖 `KILL_ON_JOB_CLOSE`
  语义，共享 runner 上不可独立观测（注记声明，不硬断言）。
- 同步：[M4 计划](m4-windows-backend.md)（本记录）、PR #41 CI 取证。

2026-09-22：`M4-04` ApplicationProvider（Windows）完成。

- 范围：`platform/windows` 新增私有应用前端
  `application_backend.{hpp,cpp}`（实现 M2-05 冻结的 `ApplicationProvider`，
  Linux `application_backend` 先例同型；COM / shell 类型不出 .cpp，`RULE-01`）。
  **发现机制在实现中定案：开始菜单快捷方式（.lnk）面**，注册表 Uninstall 面
  否决——Uninstall 键的 `UninstallString` 是卸载器入口而非启动器（launch 契约
  执行它会触发卸载），`DisplayIcon` 推导启动目标不可靠；.lnk 是 Windows 上与
  Linux desktop entry 同构的用户可见启动入口（携带目标路径 / 参数 / 工作目录 /
  窗口状态，用户根对机器根按相对 id 优先级遮蔽——XDG 先例同型），可承载
  list / launch / running 全语义面。发现 = 遍历机器与用户两个 Start Menu
  Programs 树（`SHGetKnownFolderPath`；隐藏属性 = NoDisplay 同型过滤；遍历
  4096 项上限，`RULE-07`），id = Programs 根下相对路径（'/' 分隔，含 .lnk），
  id 校验拒绝 `..` / 反斜杠 / 空段 / 前导与尾随分隔符（路径逃逸 fail closed）；
  .lnk 解析经 IShellLink + IPersistFile（调用域 COM，无需跨调用锚——本前端
  无跨调用 COM 状态，与 UIA 锚的区别注明）；三个冻结系统 GUID 本地 constexpr
  定义 + `__uuidof` 解析接口（M4-02 纪律，双工具链同声明，无 uuid.lib 依赖）。
  `launch` 拒绝序对齐 fake 契约（cancelled → timeout → 空 id → 超长 id →
  未知 id `not_found` 不建进程 → `already_running`（tracked 优先 + 按映像名
  扫描的外部实例同样拦截）→ 注册表满 `result_too_large`（容量 256）→
  `CreateProcessW` 直接启动解析目标（无 job object——应用必须活过调用，与
  M4-03 预算执行相反；不动 job 即无 KILL_ON_JOB_CLOSE 副作用）），快捷方式
  "Start in" 缺省回退目标目录，窗口状态（"Run:" 框）经
  `STARTF_USESHOWWINDOW` 如实传递；`requireAdministrator` 目标
  `CreateProcess` 不提权 → 诚实 `io_error`（前台锁定同类的系统安全边界，
  不绕过）。`running_state` / `list` 运行判定 = tracked 注册表（句柄
  signaled 即退出，机会性 reap 无后台线程，`RULE-03`）+ 有界
  `CreateToolhelp32Snapshot` 映像名扫描（4096 进程上限；`GetProcessTimes`
  创建时间排序"最老存活者胜"——M2-05 决定性同型）。`terminate` = 向实例
  pid 的全部顶层窗口 `PostMessageW(WM_CLOSE)`（TERM 等价协作式收尾；
  PostMessage 不阻塞，挂死窗口不会卡住 provider；conhost 载体窗口属
  conhost pid，控制台实例如实不可达），25 ms 切片有界等待（tracked 等自身
  句柄，外部 pid `OpenProcess(SYNCHRONIZE)`——打开失败时快照复查，已死即
  ok（ESRCH 先例），存活则诚实 `io_error`）；**全程无 TerminateProcess**，
  忽略关闭请求的实例 → `deadline_exceeded` 且实例存活可见；取消 → 实例保持
  运行。`WindowsDesktopEnvironment` 增 `ApplicationOptions`（默认关，带默认
  值追加，既有构造点不受影响）与 `application()` 访问器；应用前端 open()
  无条件可用（发现 / CreateProcess / 快照 / WM_CLOSE 均不要求交互桌面，
  ProcessProvider 可用性模型同型）。CMake：平台库增源并链接 `shell32`（已知
  文件夹 API 的唯一新增导入；shell-link COM 对象无导入库需求）。
- 依据：设计文档第 5、10、18 节；`DEC-005` / `DEC-009` / `DEC-010` /
  `DEC-015`（先例）/ `DEC-017`；M2-05 冻结语义与 fake 契约；
  `RULE-01` / `RULE-03` / `RULE-05` / `RULE-07`。
- 验证（本分支实现轮，主循环编译级取证；运行级取证随本 PR 的 CI windows
  作业执行，结论按仓库先例由下一个工作项的 PR 补录）：
  - MinGW 交叉门禁（Linux x64 主机，MinGW-w64 GCC 13.2.0 posix 用户前缀）：
    `mirage_desktop` / `mirage_platform` / `win32_application_test` /
    `m404_process_helper` / 既有三个 win32 测试 / 4 个可移植测试共 10 目标
    `MIRAGE_WARNINGS_AS_ERRORS=ON` 构建成功 **0 诊断**；
    `win32_application_test.exe` / `m404_process_helper.exe` 经 `file` 认证
    PE32+ x86-64；测试 exe 导出面含 SHELL32.dll（经平台库的已知文件夹
    API），平台库成员含 `application_backend.cpp.obj`；全树 `all` 目标的
    失败点仍为已登记的 pinned executor MinGW 缺口（`MIRA-20260922-001`，
    third_party 不可动，与本工作项无关）。
  - Linux 主机（debug 预设）不回归：configure + build + ctest **32/32 通过
    0 skip**（Linux 不编译 Windows TU；release / asan / ubsan / tsan 四预设
    由独立验证轮按 DOD-03 补齐）。
  - 门禁：`mirage-format-check` 通过（clang-format 18.1.3）；
    `mirage-boundary-check` **0 violations in 37 headers**（公共头仅扩展
    `windows_desktop_environment.hpp`，零新增 Win32 类型）。
  - 新增 `tests/platform/win32_application_test.cpp`（10 场景）+
    `tests/platform/m404_process_helper.cpp`（窗口型应用替身：hold =
    WM_CLOSE 协作退出（SIGTERM 响应型孪生）/ ignore-close = 吞掉 WM_CLOSE
    （stuck 孪生）/ exit = 自退；ready-file 在窗口存在后才落盘；自带安全
    定时器）。场景：option 关闭时访问器 null、发现列出 fixture 且跳过隐藏
    项（独立快照对照）、预算 `result_too_large` 不截断、冻结拒绝序 +
    快照金丝雀（拒绝的 launch 从未产生进程）、未知与逃逸 id（`..` / 反斜杠
    / 空段 / 尾斜杠）fail closed、launch → running_state / list / 独立快照
    三方一致 → 重复 launch `already_running` 且无重复进程 → 协作 terminate
    （WM_CLOSE 真实送达窗口泵）→ 三处归零 → 再 terminate `not_found`、
    自退实例机会性 reap 无残留、外部同名实例按名可见并可被协作终止、
    ignore-close 实例 → `deadline_exceeded` 且 running 可见（测试侧强制
    清理，provider 全程不强杀）、等待中途定时器取消 → `cancelled` 且实例
    保持 tracked 存活、非 ASCII id（UTF-16 转义构造，双工具链字节一致）
    端到端解析。fixture 纪律沿用 m205：助手按运行唯一名复制 + 结束时
    前缀清扫，开始菜单 fixture 写入真实用户 Programs 树并全程 RAII 还原。
- 限制与补跑条件：① `win32_application_test` 的运行级证据本机不可产生
  （无 Windows 会话）——由本分支 PR 的 CI windows 作业取证；② 控制台应用
  无窗口可投递 WM_CLOSE（conhost 载体窗口不属于实例 pid），Windows 对任意
  进程无 TERM 等价物且契约禁止强杀——控制台实例的协作终止只能
  `deadline_exceeded`（GUI 实例不受影响）；记录为平台事实而非缺陷；
  ③ `requireAdministrator` 清单目标不提权（UAC 提示不属于无头 agent 面），
  `io_error` 如实报告；UWP / ms-resource / 纯 IDList 快捷方式无可解析
  目标——照常列出但 launch 失败；④ 按映像名匹配无法区分共享二进制名的
  不同快捷方式、启动后换名的实例对扫描不可见（tracked 实例不受影响，
  M2-05 同型限制）；⑤ CI windows 作业的 MSVC 编译与运行取证随本 PR 执行，
  结论由下一个工作项的 PR 补录（负责人：维护者）。
- 同步：[M4 计划](m4-windows-backend.md)（本记录）、
  [总计划](mirage-implementation-plan.md) 当前状态叙述（`M4-01`..`M4-04`
  滞后叙述一并修正）、PR CI 取证（本 PR 作业执行后）。

2026-09-23：`M4-04` CI 门禁全绿与 Windows 运行级取证完成（挂账补录）。

- 范围：上一条记录的挂账项（⑤ CI windows 作业的 MSVC 编译与运行取证）
  就此闭合，无代码变更。
- 验证（CI run 35754958118，headSha = ab51335 已核实，全部作业 success）：
  windows msvc 作业 `win32_application_test` 在真实 Windows runner（MSVC
  Debug）**250 checks, 0 failures**（14 场景）——第 1 轮失败的两处
  （launch 生命周期的 list running 断言 = D1 症状、stuck 场景的
  instance_id 断言 = D2 症状）随 `27c251e` 修复转绿；rooted_ids_cannot_
  escape 回归与第 2 轮新增断言（ADS 冒号 id、cancelled 结局携带存活
  instance_id、同映像别名 list 级 running 一致）全部通过；
  `win32_backend_test` 128/0、`uia_backend_test` 108/0、
  `win32_clipboard_process_test` 129/0 同轮回归通过；同轮 Linux
  debug / release / asan / ubsan / tsan、format & boundaries、frontend
  作业全绿。check 计数对账：第 1 轮 223 + rooted 回归场景 + 第 2 轮新增
  断言 = 250。
- 残留观察（非缺陷，仅记录）：mid_wait_cancellation 场景存在 250 ms 取消
  定时器与 helper 协作退出之间的既有竞态（helper 先退出则结局为 ok），
  两轮真实运行均绿；属场景设计固有的可接受形态，不挂账。
- 同步：本计划（M4-04 记录的挂账闭合）、[总计划](mirage-implementation-plan.md)
  （已随 M4-04 叙述更新）。

2026-09-23：`M4-05` 通知承载决策登记
（[DEC-018](../decisions/DEC-018-windows-notification-carrier.md)）；实现未
开始，工作项保持未勾选。

- 范围：决策记录新登记，无代码变更。承载机制定案为 `Shell_NotifyIcon`
  气球/横幅 + open() 探测 fail closed 降级（行为差异——载荷收窄至
  63/255 UTF-16 字符、托盘常驻可见面、无泵回调——逐条声明）；toast 否决
  （AUMID 前提在 M4 无安装包形态下不成立，DEC-006 安装器属 M5；仓库工具链
  无 toast ABI 头，需手写 WinRT 声明），M5 安装器交付 AUMID 载体且产品化
  需要 Action Center 驻留/点击交互时凭新证据重议。
- 依据：`DEC-006` / `DEC-009` / `DEC-010` / `DEC-015`（Linux 通知先例）/
  `DEC-017`（决策 1 / 3 / 4）；M2-05 冻结契约
  （`notification_provider.hpp:24-39`）与 Linux 前端先例
  （`platform/linux/src/notification_backend.cpp`）。
- 验证：决策编号空缺核实（`ls docs/decisions/` 仅 DEC-001..DEC-017）；工具链
  事实复核（MinGW 用户前缀 `shellapi.h:563-564` 声明 `Shell_NotifyIconW`、
  `:465-486` `NOTIFYICONDATAW`（`szInfoTitle[64]` / `szInfo[256]`）；
  `find` / `grep` 证实前缀内无 toast ABI 头；`platform/CMakeLists.txt:105`
  Windows 分支已链 `shell32`）。本机无 Windows 会话，未运行任何 Windows
  构建/测试——运行级取证随 `M4-05` 实现（CI runner 通知区可用性为新增
  取证点）。
- 限制：本记录仅为决策登记；`M4-05` 实现与全部适用退出条件未执行（负责人：
  Mirage 维护者；补跑条件 = 实现 PR 按 M4 门禁执行并取证）。
- 同步：本计划（更新日期、非目标、设计与决策依据、`M4-05` 工作项表述、
  本记录）、[DEC-018](../decisions/DEC-018-windows-notification-carrier.md)
  （新）。

2026-09-23：`M4-05` 通知承载决策与 NotificationProvider（Windows）完成。

- 范围：`platform/windows` 新增私有通知前端 `notification_backend.{hpp,cpp}`
  （M2-05 冻结契约，Linux `notification_backend` 先例同型；Win32 类型不出
  .cpp，`RULE-01`），承载机制按
  [DEC-018](../decisions/DEC-018-windows-notification-carrier.md) 兑现：
  open() 建隐藏回调窗口（`RegisterClassW` 容忍进程内 `ERROR_CLASS_ALREADY_
  EXISTS` 复用 + 不可见顶层窗口）+ 托盘承载图标（`NIM_ADD`，V3 尺寸
  `NOTIFYICONDATAW`、`NIF_MESSAGE|NIF_ICON|NIF_TIP`、共享 stock 图标、
  tooltip "Mirage"），探测失败（无交互会话 / 无通知区，session 0 同
  `SendInput` 先例）整体清理并访问器 null——Linux 无 session bus 同型
  fail closed。`notify` 保持冻结拒绝序（cancelled → 空 title → title
  字节预算 → body 字节预算 → UTF-8 → 均副作用前 `invalid_argument`），其后
  兑现平台载荷收窄（`szInfoTitle[64]` / `szInfo[256]` 含终止符 → 63/255
  UTF-16 码元，超限 `invalid_argument` 不截断）与嵌入 NUL 拒绝（NUL 终止
  定长数组会静默截断——比 Linux 字面更严，经 stable 错误码可见的平台差异）；
  `NIM_MODIFY` + `NIF_INFO` + `NIIF_RESPECT_QUIET_TIME` 承载气球，`TRUE` =
  shell 受理 = 契约 `ok`（≠用户已见）；无泵回调（`NIN_BALLOON*` 积压队列，
  DEC-018 决策 3）、单调用收敛纪律（`Shell_NotifyIcon` 无每调用超时，
  DEC-017 决策 3）。析构先 `NIM_DELETE` 后 `DestroyWindow`（线程亲和，
  跨线程收尾响亮注记延后销毁）。`WindowsDesktopEnvironment` 增
  `NotificationsOptions`（第 4 参默认值追加，既有构造点不受影响）与
  `notification()` 访问器；CMake 零新增导入库（`shell32` 已随 M4-04）。
  权限词表零新增（`notification.post` 沿用 M2-05 冻结默认 allow，
  DEC-010）。
- 依据：设计文档第 5、10、18 节；[DEC-018](../decisions/DEC-018-windows-
  notification-carrier.md)（本项决策）、`DEC-006`（toast 重议触发）/ 
  `DEC-009` / `DEC-010` / `DEC-015`（Linux 通知先例）/ `DEC-017`（决策
  1 / 3 / 5 / 7）；M2-05 冻结语义；`RULE-01` / `RULE-03` / `RULE-05` /
  `RULE-07`。
- 验证（实现轮主循环编译级取证；运行级随本 PR CI windows 作业执行，结论
  由下一个工作项的 PR 补录）：
  - MinGW 交叉门禁（Linux x64 主机，MinGW-w64 GCC 13.2.0 posix 用户前缀，
    `MIRAGE_WARNINGS_AS_ERRORS=ON`）：`mirage_desktop` / `mirage_platform` /
    `win32_notification_test` / `m404_process_helper` / 既有三个 win32
    测试 / 4 个可移植测试共 11 目标 **0 诊断**；`win32_notification_test.exe`
    `file` 认证 PE32+ x86-64。实现中发现并当场修正一处工具链事实：
    `IDI_APPLICATION` 经 TCHAR `MAKEINTRESOURCE` 展开，UNICODE 中性 TU
    （win32_util.hpp 宏纪律）下窄化为窄串——按 winuser.h 定义直书
    `MAKEINTRESOURCEW(32512)`，双工具链同声明。
  - Linux 主机（debug 预设）不回归：configure + build（`ninja: no work to
    do`，Linux 不编译 Windows TU）+ ctest **32/32 通过 0 skip**；
    `mirage-format-check` 通过（clang-format 18.1.3，含
    `--dry-run --Werror` 逐文件复核）；`mirage-boundary-check` **0
    violations in 37 headers**（公共头仅扩展 `windows_desktop_environment.hpp`，
    新增 `NotificationsOptions` 为纯值类型）。
  - 新增 `tests/platform/win32_notification_test.cpp`（3 场景组）：
    选项关闭时访问器 null（fail closed）；真实托盘上的受理（普通 + 63/255
    恰边界 + 63 码元 CJK 边界孪生）；冻结拒绝序全矩阵（cancelled / 空
    title / 收紧 title 字节预算 / 收紧 body 字节预算 / 坏 UTF-8）；
    平台载荷收窄（64 码元 ASCII title = 64 字节 < 256 字节契约预算仍拒绝、
    64 码元 CJK title = 192 字节 < 预算仍拒绝——码元计数而非字节计数的
    直接证据、256 码元 ASCII body）；嵌入 NUL 双位置拒绝；第二环境进程内
    复用承载类 + 双图标共存。气球可见性只注记不声称（契约 `ok` = 受理）；
    无通知区会话响亮跳过正向场景而非伪造证据（DEC-018 验证方式条款）。
- 限制与补跑条件：① CI runner 通知区（Explorer 托盘）可用性是新增取证点
  （M4-01..04 未证过托盘）：正向场景若在 runner 上探测失败，测试按 skip
  纪律响亮注记并挂账（负责人：维护者；补跑条件 = 维护者 Windows 机器或
  runner 桌面携带通知区）；② 气球的实际呈现（横幅 / 静默时段 / Win10 驻留
  / Win11 瞬态）无独立观测通道，按契约 `ok` = 受理≠已见如实声明（DEC-018
  决策 3）；③ MSVC 编译与运行级证据随本 PR CI 作业执行，结论由下一个工作
  项的 PR 补录；④ Windows 侧无 sanitizer 矩阵（Linux 侧成立）。
- 同步：[M4 计划](m4-windows-backend.md)（本记录 + `M4-05` 勾选）、
  [总计划](mirage-implementation-plan.md) 当前状态叙述、PR CI 取证（本 PR
  作业执行后）。

2026-09-23：`M4-05` CI 门禁全绿与 Windows 运行级取证完成（挂账补录）。

- 范围：上一条记录的挂账项（③ MSVC 编译与运行级证据；限制① runner 通知区
  可用性取证点）就此闭合，无代码变更（验证轮增补的测试断言随 `3e9e93d` /
  `56066c5` 在列）。
- 验证（CI run 35765344178，headSha = 56066c5 已核实，全部 8 作业 success）：
  windows msvc 作业 MSVC Debug 编译 0 错误，`win32_notification_test`
  **42 checks, 0 failures**（0.10 s，无无托盘 skip 注记）——**runner 通知区
  可用性成立**（验证记录挂账的新增取证点转绿），正向场景全部真实执行
  （含空 body 受理与销毁重开生命周期）；`win32_backend_test` 128/0、
  `uia_backend_test` 108/0、`win32_clipboard_process_test` 与
  `win32_application_test`（250/0）同轮回归通过；同轮 Linux
  debug / release / asan / ubsan / tsan、format & boundaries、frontend
  作业全绿。check 计数对账：实现者原套件 ~26 + 验证轮新增断言 ~16 = 42。
  过程记录：首跑 run 35764698599 的 2 failures 定位为实现者 tight-body
  用例差一错误（`max_body_bytes=4` 配 4 字节载荷恰在冻结 `>` 检查内），
  `56066c5` 改为真实超限载荷——实现本身无缺陷。
- 同步：本计划（`M4-05` 记录的挂账闭合）。
  残留观察（非缺陷，仅记录）：mid_wait 取消定时器与 helper 协作退出之间的
  既有竞态形态两轮运行均绿（M4-04 记录已注明）。

2026-09-23：`M4-06` 产品进程 Windows 化完成。

- 范围：
  - **Local IPC 命名管道传输（DEC-007 兑现）**：`stream_windows.cpp` +
    `endpoint_windows.cpp` 实现与 POSIX 完全相同的 `IpcStream` /
    `IpcListener` 契约——重叠 I/O 命名管道（`CreateNamedPipeW` /
    `ConnectNamedPipe` / `CreateFileW`），读侧 `PeekNamedPipe` 门禁零等待
    （不留下任何在途读操作），写侧至多一个在途重叠写、其字节由流自持
    （跨调用绝不引用调用方缓冲）、部分完成按已写字节数如实上报；监听端
    首实例带 `FILE_FLAG_FIRST_PIPE_INSTANCE` 承载遗留端点接管纪律（第二次
    绑定同名 → access-denied → "another service is already listening"），
    每次 accept 后立即回收新监听实例；`endpoint_has_listener` 按错误码
    三态（不存在=可接管 / busy=存活 / 其他=保守保留）。`stream.hpp` 公共
    契约中立化：不透明 transport token 取代裸 fd（POSIX fd / Windows
    HANDLE 位型），`adopt_native` 公共工厂供自持 accept 环的传输
    （devbridge）使用。帧格式、协议 v1、golden vectors **零变更**——
    framing/protocol 两 TU 与 golden 数据文件一行未动。
  - **client**：有界等待在 Windows 上为有界切片（传输本身零等待，由
    read/write 的 WouldBlock 决定就绪）。
  - **服务环**：帧处理、outbound 队列、事件扇出全部共享（单 TU 平台分叉）；
    POSIX 保持 poll 驱动环（accept 改经 `IpcListener`、读/帧提取抽取为
    `ingest_connection`、迭代末冲刷抽取为 `flush_and_settle`——行为等价
    重构，Linux 32/32 实测不回归）；Windows 为水平驱动环——零等待流上每
    轮读/写/排水/结算全部连接，`auto-reset wake event` + 同 poll_timeout
    有界等待（唤醒与停机时延保持 POSIX 界）。RuntimeService 以对象传递
    监听端；停机路径 POSIX = 信号自泵 + `register_shutdown_fd`，Windows =
    console ctrl handler → `request_shutdown()`（其线程模型恰为该 API 的
    契约）。
  - **持久化（DEC-011）**：`store_windows.cpp`（`CREATE_NEW` 临时文件 =
    O_EXCL 同型、`FlushFileBuffers` = fsync、`MoveFileEx(REPLACE_EXISTING|
    WRITE_THROUGH)` = 原子持久发布；上限拒绝不截断同型）与
    `paths_windows.cpp`（APPDATA / LOCALAPPDATA 映射，工作目录回退同型）。
    已记录平台差异：目录硬化依赖用户 profile 默认 ACL（无 0700 对应物）、
    无逐目录刷盘。
  - **进程形态**：`mirage-service` Windows 化（绑定 `WindowsDesktopEnvironment`
    默认面——服务上下文桌面能力如实 null fail closed；`--read-root` 为
    Linux backend 面而拒绝而非静默忽略；console ctrl → `request_shutdown`）；
    `mirage` CLI Windows 化（`service start` 经 `CreateProcessA` 拉起兄弟
    mirage-service.exe + 共享就绪探针，余下命令走中立 IpcClient）。
    devbridge 按传输保持 POSIX-only 条件排除（开发工具，非产品进程形态，
    不为其分叉命名管道变体——已在 CMake 注明）；apps/tray 维持 M5 里程碑
    既有范围（无 stub 目标，"tray 可构建"随该目标落地）。
  - **CI**：windows 作业扩为全树构建 + 12 项测试 + **产品进程往返取证步**
    （真实 `mirage-service.exe` 服务命名管道 + `mirage.exe` CLI
    `service status` / `service shutdown`，退出码与停机均断言）。
- 依据：设计文档第 12 节；`DEC-007` / `DEC-011` / `DEC-012` / `DEC-017` /
  `DEC-006`（devbridge/tray 边界）；ledger `MIRA-20260922-001`（引用见
  ci.yml windows 作业注释与本记录限制节）；`RULE-01` / `RULE-02` /
  `RULE-03` / `RULE-07`；`EXEC-01` / `EXEC-02`。
- 验证（实现轮主循环本地取证；MSVC 全树编译与运行级随本 PR CI windows
  作业执行，结论由下一个工作项的 PR 补录）：
  - **MinGW 交叉门禁（受限，如实声明）**：既有平台/桌面子集 11 目标
    `MIRAGE_WARNINGS_AS_ERRORS=ON` 构建 **0 诊断**（基线不回归）；全树
    MinGW 交叉门禁因 **`MIRA-20260922-001`**（pinned executor 的
    `std::thread::native_handle_type` 假设 win32 线程模型，third_party
    不可动）本轮仍不可达——作为受限验证按工程规范第 4 节记录：新 Windows
    TU（`stream_windows` / `endpoint_windows` / `store_windows` /
    `paths_windows` / `service_loop` Windows 路径 / apps Windows 分支 /
    `win32_product_process_test`）全部以 MinGW-w64 g++ `-fsyntax-only
    -Wall -Wextra -Werror` 通过（逐 TU 编译级检查），MSVC 编译由 CI
    windows 作业覆盖；补跑条件 = 台账缺口关闭后全树交叉复验（负责人：
    维护者）。
  - **Linux 主机不回归**：debug 预设全树构建 + ctest **32/32 通过
    0 skip**——service loop 行为等价重构后 `runtime_service_test` /
    `event_subscription_test`（真实传输环）保持全绿；`mirage-format-check`
    通过；`mirage-boundary-check` **0 violations in 37 headers**。
  - 新增 `tests/platform/win32_product_process_test.cpp`：真实
    `RuntimeService` 服务命名管道 + 真实 `IpcClient` 的 hello / list-tasks
    / 协议 shutdown（run() 干净收尾）往返——M4-06 退出条件的库级形态；
    共享 golden framing 在本平台 codec 上的字节一致性；DEC-011 store 的
    absent / 往返 / 原子重发布 / 拒绝不截断 / TooLarge 五态。
- 限制与补跑条件：① 全树 MinGW 交叉挂账 `MIRA-20260922-001`（负责人：
    维护者；补跑 = 缺口关闭）；② MSVC 全树编译、12 项测试与产品进程往返
    取证随本 PR CI windows 作业执行，结论由下一个工作项的 PR 补录；
  ③ POSIX-coupled 测试家族（ipc_protocol / persistence / runtime_service /
    event_subscription / task_cancel / mira_binding /
    desktop_provider_boundary / devbridge 两项）在 Windows 上按 CMake 条件
    排除（各附原因），Windows 侧对应行为由 `win32_product_process_test`
    与后续 M4-07 端到端取证覆盖，完整移植不属本项；
  ④ devbridge 的 Windows 传输未立项（开发工具，需要时按后续工作项评估）；
  ⑤ tray 目标随 M5 落地，"tray 可构建"由该目标兑现；
  ⑥ 命名管道访问控制为创建者默认 DACL（POSIX 0700 目录的对应面），已
    在 endpoint/store 记录为平台差异。
- 同步：[M4 计划](m4-windows-backend.md)（本记录 + `M4-06` 勾选）、
  [总计划](mirage-implementation-plan.md) 当前状态叙述、
  [台账](../dependency_feedback/ledger.md)（`MIRA-20260922-001` 引用落于
  ci.yml 与本记录）、PR CI 取证（本 PR 作业执行后）。

2026-09-25：`M4-06` 维护者 Windows 机器运行级取证完成；发现并修复两处
门禁缺陷（源码字符集区域敏感、命名管道写路径背压死滞），一处测试断言
按不变量纪律修正。

- 背景：开发环境首次落在本机 Windows 会话（MSVC 19.44 BuildTools +
  Windows SDK，真实交互桌面），`M4-06` 的 MSVC 全树编译、win32 测试套件
  与产品进程往返第一次可以在本机直接取证，不再单点依赖 CI runner。
- 发现 1（build，全树 MSVC 门禁区域敏感）：仓库全部源码（含 pinned
  mira / mirador）为 UTF-8，而 MSVC 未声明源码字符集时按系统代码页读取
  ——CI runner（cp1252）侥幸通过，本机（cp936）在 `/WX` 下对
  `mira_core` 与 Mirage 自有 TU 同时报 C4819 硬错误。修复：根
  `CMakeLists.txt` 对 MSVC 全局 `add_compile_options(/utf-8)`（pinned
  依赖从源码经本构建图编译，属 Mirage 构建配置而非 pinned 代码变更；
  DEC-017 编码纪律的工具链入口兑现）。
- 发现 2（fix，命名管道写路径背压死滞——CI 上 `win32_product_process_test`
  192 KiB 背压探针 5 连败的真正根因）：`710e539` 以"调用方游标双重发送"
  为由把写侧从流自持 write-behind 队列（`93067c3`，亦即本记录所述设计）
  改为"调用内零等待 + 立即 `CancelIoEx`"。本机带逐周期诊断的独立探针
  证实：排队写在下发后数微秒内即被取消，内核从未获得把任何字节拷入管道
  的机会——每周期 reaped=0 → WouldBlock，64 KiB 空闲缓冲完全用不上，
  传输只能靠"恰好同步完成"的调用推进；2 核 CI 的调度下同步完成不再发生
  → 服务端 20 s 零接收 + 客户端 20 s 零进展（与 CI 失败签名逐项吻合）。
  `e21905c` 的"取消竞争完成时字节数仍有效"修复是必要的次要缺陷（恢复
  队列后持续背压必然撞上取消竞争），但不是根因。修复：写侧恢复流自持
  采纳语义——排队即把字节复制进流自有缓冲并按 POSIX `write()` 受理语义
  上报 Ok（在途至多一个、`kMaxAdoptedWriteBytes` 1 MiB 上限
  `RULE-07`），调用方游标一次性前进、无重发无乱序；重叠写 OVERLAPPED
  移入流状态（不再引用调用方缓冲，`710e539` D2 的双重发送担忧由此
  结构性消除）；`issue_write` / `issue_read` 下发前 `ResetEvent`（内核对
  同步完成同样置位事件，陈旧信号会把在途操作误判为完成 →
  `ERROR_IO_INCOMPLETE` 虚假 Error——读取排队分支的同型隐患一并消除）；
  `close()` 在释放流状态前对在途采纳写给 2 s 有界排水窗口后取消回收
  （OVERLAPPED 不得比流状态长寿）。计划记录"写侧至多一个在途重叠写、
  其字节由流自持"的表述自 `710e539` 起曾与实现背离，现恢复一致。
- 发现 3（test，`win32_application_test` 断言机器清单敏感）：发现场景的
  `all_lnk` 断言要求全部被发现的 id 以小写 `.lnk` 结尾——本机真实开始
  菜单含 `.url` 项（Git / Java，非隐藏、按 M4-04 设计照常列出）与
  `Image-Line/More....lnk`（文件名含 `....`），断言必然失败；CI 的干净
  runner 掩盖了这一点。按不变量式测试纪律改为镜像后端自身 `valid_
  application_id` 的 id 规则（相对 '/' 路径、无空段 / 点段 / 反斜杠 /
  冒号），机器无关。
- 验证（本机 Windows 11 x64，MSVC 19.44 Debug，真实交互桌面）：
  - 全树 configure + build **0 诊断**（含 pinned mira / mirador）。
  - CI windows 作业的 12 项测试集 ctest **12/12 通过 0 skip**；
    `win32_product_process_test` **50 checks, 0 failures**（0.78 s，修复前
    同机同测试复现 CI 同款 3 failures）。
  - 2 核亲和（`start /affinity 3`，模拟 CI runner 调度）：
    `win32_product_process_test` 50/0；192 KiB 背压独立探针 sent=got=
    196608 完整往返（修复前同探针零进展死滞，逐周期日志在案）。
  - 产品进程往返（真实 `mirage-service.exe` 服务命名管道 + `mirage.exe`
    CLI `service status` / `service shutdown`）：hello 往返、协议停机、
    服务退出码 0、"stopped cleanly"——`M4-06` 退出条件的进程形态取证。
- 限制与补跑条件：① 本机构建依赖全局
  `/utf-8`（cp936 主机）；② 采纳写未被对端读走即关闭时，未入管道缓冲
  的尾部字节随连接丢弃（对端见截断）——POSIX close 的既有关闭语义
  差异已在代码注释与本记录声明，`M4-07` 端到端取证复核；③
  `MIRA-20260922-001`（全树 MinGW 交叉）维持挂账不变。
- 同步：本记录、[DEC-017](../decisions/DEC-017-windows-backend-toolchain-
  and-event-loop.md) 变更记录、PR [#45](https://github.com/Linductor-alkaid/mirage/pull/45)
  CI 取证（下条记录）。

2026-09-25：`M4-06` CI 门禁全绿（挂账补录）。

- 范围：上一条记录的挂账项（CI 证据）就此闭合，无代码变更。
- 验证（CI run 36148338709，headSha = ef83d95 已核实，全部 8 作业
  success）：windows msvc (full tree) 作业 MSVC 编译 0 错误，ctest
  **12/12 通过**——`win32_product_process_test` **50 checks, 0 failures**
  （runner 上 1.11 s / 复跑 0.85 s，此前五连败的背压探针就此转绿），
  `win32_backend_test` 128/0、`uia_backend_test` 108/0、
  `win32_clipboard_process_test` 129/0、`win32_application_test` 250/0
  （id 规则断言在 runner 与真实机器双面成立）、`win32_notification_test`
  42/0 同轮回归通过；**产品进程往返取证步真实执行**（`mirage-service.exe`
  服务命名管道 + `mirage.exe` CLI `service status` / `service shutdown`，
  "product-process round trip over the named pipe: OK"）；同轮 Linux
  debug / release / asan / ubsan / tsan、format & boundaries、frontend
  作业全绿。`M4-06` 的退出条件（命名管道 IPC 往返、Windows 持久化、
  进程形态可构建、全树 MSVC 构建）全部取证成立。
- 同步：本计划（挂账闭合）、PR [#45](https://github.com/Linductor-alkaid/mirage/pull/45)
  CI 取证。

2026-09-25：`M4-07` Windows 端到端闭环与里程碑退出复核完成。

- 范围：新增 `tests/integration/win32_observation_e2e_test.cpp`（CI windows
  作业 Test 与 evidence 两步纳入）——完整 observe → action → observe 闭环
  在真实交互桌面无模拟器运行：真实 fixture 窗口（push button + edit）+
  `WindowsDesktopEnvironment` 真实 Win32 / UIA 前端 + 桌面层
  `ObservationAssembler` / `ElementTargetExecutor` + 全栈经
  `MiraEnvironmentBinding` 绑定。闭环取证：observe() 将 fixture 的真实
  UIA 树投影为 validator-clean pinned 快照（@eN refs + 诚实前台
  AppContext）→ reference ElementTarget 经 accessibility reference ring
  解析、语义激活**真实点击** fixture 按钮（点击落入 fixture 自有窗口
  过程，独立于 provider 的自述）→ Value 模式 set_text 写入 edit（独立
  `GetWindowTextW` 回读）→ 重新观察且新 refs 可继续执行（循环可持续；
  平台事实：Win32 EDIT 的 UIA Name 恒为空，内容在 Value 模式不入
  Name——断言按不变量式设计，不依赖该行为）。绑定面在真实后端复核：
  capabilities 如实上报（foreground_app / ui_tree 声明，
  screen_capture / discrete_input / input_release 不声明）、required
  screen 无视觉管线 fail closed 拒绝整请求、pinned input dispatch 副作用
  前 `Rejected` 且无副作用、interrupt 幂等。前台锁前置：fixture 先经
  `SendInput` 注入一个惰性合成 SHIFT 键满足 OS 的 last-input 前台权前置
  （真实用户交互的等价替身），激活本身仍由 provider 执行；仍被拒绝的
  会话按响亮注记跳过（`win32_backend_test` 纪律）。
- 依据：设计文档第 6、9、11 节；`DEC-005` / `DEC-007` / `DEC-009` /
  `DEC-017`；M2-06（`observation_e2e_test`）同型先例；`RULE-01` /
  `RULE-03` / `RULE-07`。
- 验证（维护者 Windows 机器 + CI 双面）：
  - 本机（Windows 11 x64，MSVC 19.44 Debug，真实桌面）：`win32_
    observation_e2e_test` **74 checks, 0 failures**（重复 4 轮 + 两核
    亲和 `start /affinity 3` 均绿）；windows 作业 13 项测试集 **13/13**；
    新 TU MinGW-w64 `-fsyntax-only -Wall -Wextra -Werror` 0 诊断；
    `mirage-boundary-check` 0 violations in 37 headers。
  - CI（run 36154470581，headSha = caf622b 已核实，全部 8 作业
    success）：windows msvc (full tree) 13/13，e2e 在 runner 真实桌面
    **74 checks, 0 failures**（Test 步 3.99 s / evidence 步 1.99 s，
    无 skip 注记——runner 前台激活在合成键前置下成立）；产品进程往返
    步 OK；Linux debug / release / asan / ubsan / tsan、format &
    boundaries、frontend 全绿（Linux 侧零变更，行为等价不回归）。
- 同步：本记录 + `M4-07` 勾选、退出条件勾选、
  [总计划](mirage-implementation-plan.md) 状态叙述与里程碑索引、
  PR [#46](https://github.com/Linductor-alkaid/mirage/pull/46) CI 取证。

2026-09-25：M4 里程碑退出条件复核通过，里程碑 Completed。

- 范围：对 6 项退出条件逐项独立取证；M4 范围未修改任何 pinned 代码，
  submodule 指针零变更。
- 逐项取证：
  1. **Linux 主机矩阵不回归**（`DOD-01` / `DOD-03`）：PR #45 与 #46 的
     CI Linux 矩阵（debug / release / asan / ubsan / tsan）全绿
     （run 36148338709 / 36154470581）；`mirage-format-check`
     （clang-format-18）与 `mirage-boundary-check` 在两 PR 的 format &
     boundaries 作业通过；本机复核 boundary 0 violations / 37 headers。
  2. **Windows 编译门禁**（`DOD-03` 跨平台形式，`DEC-017` 证据分级）：
     MSVC（产品工具链，`DEC-006`）全树编译在 CI windows 作业两轮
     0 错误，13 项测试 runner 实跑；MinGW-w64 交叉：既有 11+ 目标
     `MIRAGE_WARNINGS_AS_ERRORS=ON` 0 诊断（M4-06 记录），新 Windows TU
     逐 TU `-fsyntax-only -Werror` 0 诊断；全树 MinGW 交叉因台账
     `MIRA-20260922-001`（pinned executor 的线程模型假设）受限，按工程
     规范第 4 节记录补跑条件 = 台账缺口关闭后全树交叉复验（负责人：
     维护者）。
  3. **Windows Provider 契约不偏离**（`DOD-04`）：fake 契约测试
     （`provider_contract_test` 等）在 Linux 矩阵与 windows 作业双面
     绿；win32 前端套件在真实 Windows 会话双点取证（维护者机器 + CI
     runner）：win32_backend 128 / uia_backend 108 / clipboard_process
     129 / application 250 / notification 42 / product_process 50
     checks，均 0 failures；预算拒绝不截断、取消先于副作用、fail
     closed（null 访问器、open() 探测、能力门）语义在各前端测试与
     `M4-01`..`M4-06` 验证记录中逐项在案。
  4. **Windows 端到端闭环**（`M4-07`）：`win32_observation_e2e_test`
     74 checks / 0 failures——observe（含 SemanticSnapshot 的
     Observation，pinned validator-clean 投影）→ ElementTarget 经
     accessibility reference ring 解析并真实执行（独立窗口过程点击
     证据 + Value 写入独立回读）→ 重新观察（fresh capture，refs 重发
     且可执行）。本机与 CI runner 双点取证。
  5. **产品进程 Windows 化**（`M4-06`）：真实 `mirage-service.exe` 服务
     命名管道 + `mirage.exe` CLI `service status` / `service shutdown`
     往返在本机与 CI windows 作业双点取证（退出码 0、干净收尾、
     "product-process round trip over the named pipe: OK"）；golden
     vectors 复用（`ipc_protocol_golden_test` 双侧绿）。
  6. **决策与文档同步**（`DOD-05` / `DOD-06`）：`DEC-017`（M4-01..M4-06
     工具链事实五轮变更记录）与 `DEC-018`（M4-05 承载决策）在案；本计划
     七条验证记录 + 本复核；总计划状态与里程碑索引同步；submodule 指针
     与 `dependencies.lock.json` 一致（mira `cf0af75` / mirador
     `fff7f15`，configure 锁校验通过，M4 范围零指针变更）；`RULE-03`
     审计：M4 新增 Windows TU（stream / endpoint / store / paths /
     service_loop / apps / e2e 测试）`std::thread` / `std::jthread` /
     `std::async` grep 0 命中；Commit / MR 符合工程规范第 10 节
     （`feat(ipc)` / `feat(platform)` / `test(tests)` / `build` /
     `docs(plans)`，PR #39-#46 经 CI 门禁合入）。
- 残留挂账（不阻塞里程碑，均已登记）：① 全树 MinGW 交叉复验 =
  `MIRA-20260922-001` 缺口关闭（负责人：维护者）；② devbridge 的
  Windows 传输未立项（开发工具，M4-06 记录）；③ toast 通知承载留 M5
  重议触发（`DEC-018`）；④ Windows 采集性能升级（DXGI /
  Windows.Graphics.Capture）为 DEC-017 既定后续路径；⑤ `release-delta`
  tag 与发布流程待维护者授权（工程规范第 10.5 节）。
- 同步：本里程碑状态（Completed）、退出条件勾选、
  [总计划](mirage-implementation-plan.md) 当前状态叙述与里程碑索引。
