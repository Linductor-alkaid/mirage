# M4：Windows Backend（UIA / Win32 / Capture / Input）

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M2](m2-desktop-environment.md)（已完成：Desktop Environment 九个 Provider
> 契约、SemanticSnapshot、ElementTarget 解析顺序契约；Linux Backend 同型骨架先例）
> 建议发布点：`release-delta`（tag 待维护者授权后创建）
> 更新日期：2026-09-22（M4-01 骨架开工）

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
- Windows 通知中心的完整产品化交互（通知 Provider 的承载机制在 M4-05 决策，
  仅交付契约面等价能力）。
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
- [ ] `M4-02` AccessibilityProvider（UI Automation）：COM 初始化模型兑现
      （DEC-017）、UIA 语义树采集 → SemanticSnapshot（节点预算 fail closed、
      角色词表投影）、ElementTarget 解析（reference / semantic / structural；
      visual/spatial/raw `unsupported_hint` fail closed）与语义动作
      （Invoke / Value 模式；语义调用优先于键鼠，设计文档第 9 节）；UIA 前端
      类型不出公共头（`RULE-01`）。
- [ ] `M4-03` ClipboardProvider（Win32）与 ProcessProvider Windows 承载：
      `CF_UNICODETEXT` 读写、预算与 UTF-8 转换（对齐 M2-04 契约语义）；
      ProcessProvider 经 `CreateProcess` 的有界执行与取消（对齐 M1-05 语义）。
- [ ] `M4-04` ApplicationProvider（Windows）：应用发现（开始菜单快捷方式 /
      注册表 Uninstall 面，机制随实现定案并记录）、启动 / 运行态 / 终止
      （对齐 M2-05 语义：tracked 实例、TERM 等价的协作式收尾、不强杀）。
- [ ] `M4-05` 通知承载决策与 NotificationProvider（Windows）：toast（COM /
      WinRT，需 MSIX 或开始菜单快捷方式前提）vs `Shell_NotifyIcon` 气球等承载
      机制的决策记录（DEC-018 起，编号顺延）后落地契约面等价能力。
- [ ] `M4-06` 产品进程 Windows 化：Local IPC 命名管道传输（DEC-007 兑现，
      `stream_windows`；帧格式与协议 v1 不变，golden vectors 复用）、持久化
      Windows 路径与存储（`store_windows`）、`apps/service` / CLI /
      tray 进程形态可构建；全树 MSVC 构建通过。
- [ ] `M4-07` Windows 端到端闭环与里程碑退出复核：`integration/mira` 绑定在
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
  （EXEC-03）：`M4-02` 前按 DEC-017 兑现或修订。
- GitHub windows runner 桌面能力不足时 win32 集成测试不可运行的风险已消除
  （`M4-01` 首跑确认 runner 有真实交互桌面，测试常驻 CI 门禁）；后续风险仅
  剩 runner 环境演进（如镜像更换影响桌面能力），由门禁持续暴露。
- 沙箱无 root：MinGW-w64 以 `apt-get download` + `dpkg -x` 用户前缀提取引导
  （DEC-015 第 4 条同一策略，`M4-01` 已实测可下载）。

## 测试与退出条件

- [ ] Linux 主机矩阵不回归：`debug`、`release`、`asan`、`ubsan` 预设构建 +
      `ctest` 全绿无 skip，`tsan` 按本机注意事项运行；`mirage-format-check`
      与 `mirage-boundary-check`（含新增公共头）通过（`DOD-01` / `DOD-03`）。
- [ ] Windows 编译门禁：MinGW-w64 x86_64 交叉 configure + build 通过；MSVC
      （CI windows runner）configure + build 通过，可运行的纯逻辑测试在
      runner 上实跑（`DOD-03` 的跨平台形式，DEC-017 证据分级）。
- [ ] Windows Provider 契约不偏离 DEC-005 / DEC-009：预算拒绝、取消先于
      副作用、fail closed 语义经既有 fake 契约测试与 Windows 前端集成测试
      双面覆盖；win32 前端测试在真实 Windows 会话取证（CI runner 或维护者
      机器；不可运行时记录原因与补跑条件）（`DOD-04`）。
- [ ] Windows 端到端闭环：observe 返回含 SemanticSnapshot 的 Observation，
      ElementTarget 经解析执行后 Observation 更新（`M4-07`）。
- [ ] 产品进程 Windows 化：`apps/service` 形态在 Windows 上完成 IPC 往返
      （命名管道传输 + golden vectors 一致）（`M4-06`）。
- [ ] 决策与文档同步：DEC-017 及后续决策、设计文档注记、总计划状态与验证
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
