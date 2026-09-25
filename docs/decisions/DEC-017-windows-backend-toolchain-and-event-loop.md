# DEC-017：Windows Backend 工具链、依赖与事件循环接入

> 状态：Accepted
> 日期：2026-09-22
> 负责人：Mirage 维护者
> 冻结里程碑：M4（`M4-01` 落地第 1-3、5-9 条；第 4 条 COM 模型在 `M4-02`
> UIA 兑现，事件型升级路径保留）
> 替代/被替代：无（Linux 侧先例见
> [DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)，本文为其
> Windows 同型决策）

## 背景与问题

`M4-01` 开始实现 Windows Backend 骨架（Win32 窗口 / 采集 / 输入闭环），随之
而来五个必须先定案的问题：

1. 开发与 CI 环境均为 Linux：Windows Backend 的编译门禁与运行取证拓扑必须
   先定案，且证据等级不得虚报（工程规范第 7 节，`RULE-08`）。
2. 工具链选择：MinGW-w64 与 MSVC 在异常模型、Win32 头面与产品分发
   （DEC-006 Windows exe 安装包）上各有位置，单一工具链有长期风险。
3. Win32 前端并发纪律：`SendInput` / GDI `BitBlt` / `EnumWindows` 的线程
   要求、消息循环亲和（EXEC-03）与 Executor 的关系（`RULE-03`、EXEC-04）。
4. COM 初始化模型是 M4-02 UIA Accessibility 的前提（STA 消息泵 vs MTA），
   影响是否需要承载消息循环的 worker 线程。
5. 编码与采集模型：契约层 UTF-8 与 Win32 `W` API 的转换边界；GDI 与
   Windows.Graphics.Capture / DXGI Desktop Duplication 的取舍。

## 决策

1. **双工具链门禁，证据分级**：MinGW-w64（GCC，x86_64）是 Linux 开发机上的
   交叉编译门禁（本地可复现）；MSVC 是 CI windows runner 上的编译门禁
   （产品主工具链，对齐 DEC-006 分发形态与工程规范第 11 节平台矩阵）。任一
   工具链失败即构建失败。证据分级遵守工程规范第 7 节：交叉构建只证明
   "可以编译（指定工具链）"；"可以运行"必须有真实 Windows 会话的运行或
   集成测试（CI runner 桌面可用性随 `M4-01` 首个 PR 验证，否则由维护者
   Windows 机器补跑并记录）。跨平台支持声明需两级证据齐备。
2. **无 root 引导**：MinGW-w64 以 `apt-get download` + `dpkg -x` 用户前缀
   提取引导（DEC-015 第 4 条同一策略；`g++-mingw-w64-x86-64` 已实测可下载），
   定位顺序 `$MIRAGE_MINGW_PREFIX` → `$PATH`；工具链文件
   `cmake/toolchains/mingw-w64-x86_64.cmake` 承载交叉配置，不 vendored。
3. **Win32 前端并发纪律（对齐 DEC-015 第 2 条）**：Provider 方法同步、有界
   （预算在副作用前检查、取消在副作用前观察），跨调用共享状态由一把互斥锁
   串行化；不建线程、不建消息循环、不初始化 COM（骨架面 `RULE-03`）。骨架
   所用 API（`EnumWindows` / `GetForegroundWindow` / `SetForegroundWindow`、
   GDI `BitBlt`、`SendInput`、`GetCursorPos`、`GetSystemMetrics`）均不要求
   调用线程持有消息循环或窗口。调用上下文 = 消费方选择的 Executor blocking
   worker（EXEC-04）。平台事实如实记录：`SendInput` 要求调用线程位于交互
   桌面会话（session 0 服务上下文失败为 `io_error`，不静默）；单次注入无
   内在超时，强制收敛依赖调用方执行上下文（同 DEC-015 `M2-06` 结论 2）。
4. **COM 初始化模型（M4-02 前提，留待兑现）**：UIA 客户端的调用型用法
   （枚举树、模式调用，无事件回调）在 MTA（`CoInitializeEx(COINIT_
   MULTITHREADED)`）上按调用承载，由 UIA 前端在每调用线程初始化语境中管理，
   不新增线程；UIA 事件流若立项则需 STA + 消息泵，经 EXEC-03 外部事件循环
   边界接入 Executor blocking worker（DEC-015 第 3 条同型），该升级随触发
   工作项修订本条。M4-01 骨架不初始化 COM。
5. **编码纪律**：全部 Win32 字符串 API 使用 `W` 变体；契约边界（UTF-8）与
   平台边界（UTF-16）的转换集中在 Win32 前端内，公共头零 Win32 类型
   （`RULE-01`）。窗口标题经 `GetWindowTextW` + `WideCharToMultiByte(CP_UTF8)`；
   `type_text` 经 `KEYEVENTF_UNICODE`（UTF-8 → UTF-16 码元序列，代理对按
   UTF-16 自然承载）——Windows 不受 X11 键位表限制，任意 UTF-16 可表达文本
   可注入，此能力差异如实写入前端注释，不改契约。
6. **采集模型：GDI `BitBlt` 为首版**：显示 / 窗口 / ROI 采集统一走
   `GetDC(NULL)` / `GetWindowDC` + `BitBlt` → Bgra8，语义对齐 M2-02 契约
   澄清（`capture_window` = 窗口边界处的屏幕内容，含重叠内容；不可见窗口
   fail closed）。Windows.Graphics.Capture / DXGI Desktop Duplication 为
   升级路径（帧率 / 独占全屏 / HDR），触发条件：性能基准不达标或 M5 Overlay
   需要帧序列；在此之前不引入 WinRT / COM 依赖面。
7. **标识词表**：window id = `HWND` 的十进制字符串（对齐 X11 后端
   `std::to_string(Window)` 先例）；display id = `EnumDisplayMonitors` 枚举
   序配合 `MONITORINFO` 的 `szDevice`（会话内稳定）；二者均为不透明 id，
   消费方不得解析其结构。
8. **权限默认策略沿用 M2 冻结词表**：骨架动作面全部落在既有能力
   （`window.activate` / `screen.capture` / `input.inject`，默认 allow，
   DEC-015 第 6 条），不新增词表、不改判定语义（DEC-010）。
9. **CI windows 作业**：GitHub windows runner（MSVC）configure + build +
   可运行测试子集（纯逻辑单测）；win32 前端集成测试的 runner 桌面可用性随
   `M4-01` 首个 PR 验证——不可运行时按 skip 纪律显式标注 gate 并记录补跑
   条件，不以编译通过冒充运行证据（DOD-03、`RULE-08`）。

## 备选方案

- **仅 MSVC（放弃 Linux 本地门禁）**：否决——开发闭环需要本地可复现的编译
  门禁；MSVC 保留为 CI 必过门禁。
- **仅 MinGW-w64**：否决——产品分发与 UIA / WinRT 生态以 MSVC 为主工具链，
  MinGW-only 会积累 MSVC 不兼容风险（异常模型、头面差异）。
- **wine 作为运行取证**：否决——wine 非 Windows 目标平台，其 Win32 实现
  差异会污染证据等级（`RULE-08`）；不作为验收证据，不进 CI 门禁。
- **Windows.Graphics.Capture / DXGI Duplication 首版采集**：否决——WinRT /
  COM 依赖面与骨架闭环无关；GDI 足以承载契约的同步、有界、Bgra8 语义
  （升级路径见决策 6）。
- **骨架引入 STA 消息泵 worker（预建 UIA 事件能力）**：否决——无事件消费
  方先行建设线程与消息循环边界违反 `RULE-03` 的最小化纪律；按决策 4 触发
  制演进。

## 影响与风险

- `platform/windows` 新增公共头 `windows_desktop_environment.hpp` 与私有
  Win32 前端（`src/win32_backend.{hpp,cpp}`）；公共头零 Win32 类型
  （`RULE-01`，`mirage-boundary-check` 计入新头）。
- CMake：platform 库 Windows 分支接线 `user32` / `gdi32`；win32 集成测试
  注册 gate 为 Windows 平台；交叉构建只 configure + build（ctest 需 Windows
  运行环境，由 CI windows 作业承载）。
- MinGW 与 MSVC 的警告面与 `WIN32_LEAN_AND_MEAN` / `NOMINMAX` 等宏纪律需要
  在前端统一；遗漏会在双工具链门禁暴露（设计意图即如此）。
- `SetForegroundWindow` 前台锁定与 UIPI 是系统安全边界：非前台进程的激活
  可能被系统拒绝，Provider 如实返回失败并携带 Win32 错误，不绕过、不静默。
- 全树 Windows 构建尚不可达（IPC `stream_posix`、persistence `store_posix`
  为 POSIX 专用）：CI windows 作业在 `M4-06` 前只构建 desktop / platform /
  integration 子集并运行可运行测试子集；全树 Windows 构建是 `M4-06` 退出
  条件，不是 `M4-01` 的。

## 验证方式

- MinGW-w64 交叉 configure + build（含 win32 前端与其测试的编译 + 链接）。
- MSVC CI 作业 configure + build + 可运行测试子集（首个 PR 验证 runner
  桌面能力并留证）。
- Linux 主机矩阵不回归（debug / release / asan / ubsan / tsan + format +
  boundary）。
- win32 前端集成测试在真实 Windows 会话的运行证据（CI runner 或维护者
  Windows 机器；不可运行时按规范记录）。

## 变更记录

- 2026-09-22（`M4-03`）：决策 4 的升级路径裁决记录——UIA client 迁入
  Executor blocking worker 的结构性升级**已获维护者授权立项**，实施窗口由
  维护者指定（当前证据：live-tree 解析的三级兜底工作正常、structural 环可
  用，紧迫性低）。另记录 `M4-03` 过程的平台事实：Windows 控制台会话上，
  `cmd.exe` 的隐藏控制台载体（conhost）会复制管道写端且比命令长寿，管道
  EOF 不可作为命令完成信号——进程承载使用"直接子进程退出 + 捕获管道排
  空"完成谓词 + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 精确继承 + Job Object
  整组拆除（`M4-03` 验证记录）。
- 2026-09-22（`M4-02` CI 取证）：第 4 条（COM 初始化模型）兑现记录。调用型
  MTA 用法按本条落地：每方法调用在调用线程 `CoInitializeEx(
  COINIT_MULTITHREADED)`，首个成功 scope 永久保留为**进程级 MTA 锚**（恰好
  一个有界引用，M2-03 对象池先例同型）——锚使引用注册表的元素指针跨调用
  有效；无事件流、无线程，事件型升级路径继续保留。`M4-02` 运行级取证补充
  的平台事实（写入 `M4-02` 验证记录）：UIA 跨进程 COM 调用不可取消，桌面
  可能存在挂死 provider（runner 桌面已实测）；`IUIAutomation2` 事务/连接
  超时是客户端提示而非传输层硬界，故 live-tree 解析采用三级兜底——
  `IUIAutomation2` 超时（约束慢 provider）+ 每次 live-tree 扫描的 30 s
  wall-clock 预算（预算耗尽如实 `not_found`）+ 调用方执行上下文（Executor
  调用超时，约束单个 wedged 调用，与决策 3 的单调用收敛纪律同型）。测试侧
  桌面扫描以子进程隔离 + 看门狗取证（进程死亡是 wedged 事务的唯一可靠
  收割手段）。UIA client 迁入 Executor blocking worker 的结构性升级是否
  立项由维护者裁决。工具链事实：两 SDK 的控制类型常量同名
  `UIA_*ControlTypeId`（MinGW 宏常量 / MSVC 枚举成员）+ `CONTROLTYPEID`
  typedef，`__uuidof` 双工具链可用，无需 uuid.lib。
- 2026-09-22（`M4-01` CI 取证）：第 1、9 条的首轮兑现记录。MSVC（windows-
  latest，VS 18 2026，19.51）全树 configure 成功（pinned mira / mirador 均在
  Windows 校验通过，"Mira target platform: Windows"）；`mirage_enable_warnings` 为 MSVC 增设独立旗标组
  （`/W4 /permissive- /Zc:__cplusplus` + `/WX`，GCC 旗标在 MSVC 为 D8021
  硬错误）；windows 作业 5/5 测试通过，`win32_backend_test` 在 runner 真实
  交互桌面（1024x768）128 checks 0 failures，前台激活与键盘送达场景真实
  执行——决策 9 预留的"runner 桌面能力"确认成立，windows 集成测试可常驻
  CI 门禁。MinGW 侧同步取证：`MONITORINFOEXW`（非 `MONITORINFOW`）承载
  szDevice、VK_* 宏（int）经聚合初始化窄化为 WORD（std::pair 模板转发会
  触发 MSVC C4242）两处实现纪律记录在案。
- 2026-09-25（`M4-06` 维护者机器取证）：工具链事实两则。① 源码字符集：
  仓库全树（含 pinned 依赖）为 UTF-8，MSVC 不声明字符集时按系统代码页
  读取——cp1252 runner 侥幸通过，cp936 主机在 `/WX` 下 C4819 硬错误；
  根 `CMakeLists.txt` 对 MSVC 全局 `add_compile_options(/utf-8)`（编码
  纪律第 5 条的工具链入口兑现，pinned 依赖经本构建图编译，属构建配置
  而非 pinned 代码变更）。② 重叠 I/O 事件纪律：内核对同步完成同样置位
  auto-reset 事件，跨调用复用的完成事件必须在每次下发前 `ResetEvent`，
  否则陈旧信号会把下一个在途操作误判为完成（`ERROR_IO_INCOMPLETE`
  虚假 Error）；命名管道写侧的背压承载为流自持采纳写（在途至多一个、
  有界上限），调用内零等待 + 立即取消使排队写永零进展（内核无拷贝
  机会），2 核调度下退化为完全死滞——见 M4 计划 2026-09-25 记录。

## 关联文档和工作项

- 设计文档第 10、18 节；[DEC-005](DEC-005-desktop-observation-contract.md)、
  [DEC-006](DEC-006-ui-web-frontend-packaging.md)、
  [DEC-007](DEC-007-local-ipc-and-runtime-service.md)、
  [DEC-009](DEC-009-provider-scope-budget-cancellation.md)、
  [DEC-010](DEC-010-m1-permission-framework.md)、
  [DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)。
- Executor 依据：`third_party/mira/third_party/executor/docs/skill/
  executor-integration/references/blocking-io.md`（外部事件循环承载，UIA
  事件型升级路径）。
- 工作项：[M4 计划](../plans/m4-windows-backend.md) `M4-01`（本决策第
  1-4、6-9 条）；`M4-02`（第 4 条兑现）、`M4-06`（全树 Windows 构建）、
  `M4-07`（端到端取证）。
