# DEC-019：CEF 产品壳骨架与受控 bridge 装配

> 状态：Accepted
> 日期：2026-09-26
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-02` 落地）
> 替代/被替代：无（DEC-006 决策 1 壳选型与决策 6 供应链锁定的骨架兑现）

## 背景与问题

`M5-02` 要求把 DEC-006 冻结的 CEF 路线落成产品壳骨架（`apps/desktop`）：壳进程
模型、`ui/app` 构建产物承载、壳内 IPC 产品传输路径，验收为壳内 UI 对真实
`mirage-service` 完成 hello / 订阅 / 任务往返。设计约束（设计文档第 12、17 节，
DEC-006，AGENTS.md）：

1. UI 不进 C++ 目标依赖图，IPC 契约是 UI 与 C++ 的唯一耦合面（`RULE-01`）；
2. 壳形态与浏览器形态同构（DEC-013 §3.2），UI 侧 `MirageTransport` 接口缝
   不变，devbridge 维持开发工具定位；
3. 并发全部经 pinned Executor（AGENTS.md 并发纪律），第三方回调只做有界校验
   与投递（规则 11）；
4. CEF 二进制必须经 `dependencies.lock.json` 锁定消费（DEC-006 决策 6、
   shell-binary-locking §2.1），沙箱默认启用、GPU 默认启用（M5-01 正式方案）。

实现前需定案：工件获取方式、Windows 进程形态、CRT 一致性、renderer 受控
bridge 的机制与载荷格式、长连接会话客户端的归属与失败语义、构建门禁形态。

## 决策

1. **工件获取（configure 期、fail closed）**：`cmake/MirageCef.cmake`
   `mirage_acquire_locked_cef()` 从锁文件解析平台 pin
   （`mirage_require_locked_artifact_platform()`，新增于
   `cmake/MirageDependencies.cmake`：URL 模板按平台填充 + 40-hex sha1 + 正整数
   size 校验）。缓存 tarball 存在即 configure 期复核 sha1 与 size，失配即
   FATAL_ERROR（"configure 失败于摘要不匹配"）；冷缓存经 `file(DOWNLOAD)`
   `EXPECTED_HASH SHA1=` 下载，服务端摘要不符同为 configure 失败；解包后校验
   发行包布局。缓存目录 `build/artifact-cache`（gitignore，仓库不收二进制），
   tar 文件名对 CDN URL 的 `%2B` 做规范化解码。
2. **壳目标 opt-in**：`MIRAGE_ENABLE_DESKTOP_SHELL`（默认 OFF）。默认构建图不
   下载 360–675 MB 工件、保持密闭；启用即绑定锁定工件（未注册 pin 即
   configure 失败），"未锁定二进制不进默认构建"语义不减弱。CI 在 Windows
   全树作业显式启用并以锁文件哈希缓存 tarball；Linux 侧启用待 CI 矩阵补齐
   X11 构建依赖（M5-02 验证记录挂账）。
3. **Windows 进程形态（CEF 152 bootstrap 模型）**：`USE_SANDBOX=ON`（CEF
   默认）下 client 代码构建为 `mirage-desktop.dll`，发行包 `bootstrap.exe`
   复制为 `mirage-desktop.exe` 承载沙箱装配并经 `RunWinMain` 进入；Linux 为
   单可执行（chrome-sandbox setuid 随 M5-11 打包交付）。两平台共用同一组
   client 源（browser/renderer 进程同体，按 `--type` 分派）。
4. **CRT 一致性**：CEF 的 `CEF_RUNTIME_LIBRARY_FLAG`（默认 `/MT`）在
   `find_package(CEF)` 之前设为 `/MD`——CEF 目标（wrapper 与壳）与
   `mirage_ipc`、pinned executor 共用 `/MD[d]`，消除 LNK2038。`_HAS_EXCEPTIONS=0`
   等 CEF 定义照常生效；壳 TUs 经 VS 生成器保留 CMake 默认 `/EHsc`，行为与
   cefsimple 的 VS 构建一致。
5. **受控 bridge（renderer ↔ browser）**：CefMessageRouter（浏览器侧
   `DesktopClient::BridgeHandler` + 渲染侧 `DesktopRendererApp`，查询函数
   `mirageQuery` / `mirageQueryCancel`）。载荷格式 = 协议 v1 请求/响应封装
   JSON 原文——一条 query 一个请求封装、应答一个响应封装，browser 进程把
   会话侧关联 id 重写为渲染器侧 echo，wire 契约与 golden vectors 不变
   （DEC-012：bridge 是传输内部形态，不是协议扩展）。服务事件以
   `CefProcessMessage`（`mirage:desktop-event`，载荷为 `encode_event` 产物）
   推送到渲染器 `window.__mirageOnEvent`；会话丢失推
   `mirage:desktop-connection-lost`。UI 侧
   `DesktopBridgeTransport implements MirageTransport`（`ui/contracts`），
   壳内自动选择（`window.mirageQuery` 存在）或显式 `?transport=desktop`。
6. **UI 资产承载**：自定义 scheme `mirage://app/`（STANDARD+SECURE+CORS，
   两进程一致注册），`UiSchemeFactory` 从 `MIRAGE_UI_APP_DIST`（vite 构建产物，
   configure 期要求存在）按白名单 MIME 供文件；路径百分号解码、`..`/绝对逃逸
   fail closed。不用 `file://`（ES module 的 CORS 限制），打包资产布局随
   M5-11 复核。
7. **长连接会话客户端**：`runtime/ipc` 新增 `SessionClient`（executor-free、
   线程安全；`call()` 排队 + id 关联；单连接单未决纪律内建于发送节流；事件
   sink；读循环 `run()` 由 owner 提交 Executor blocking worker 驱动）。超时即
   fail-closed 关闭会话（冻结纪律：未决请求未答复不得发下一条），重连与
   resync 是 owner 决策（快照为事实源，DEC-012）。壳侧 `ShellSession`
   持 Executor 引用、惰性（重）连接；查询处理按规则 11 跳 CEF UI 线程 →
   Executor → `CefPostTask(TID_UI)` 回投，admission 拒绝经 future 异常显式
   应答（AGENTS.md 规则 10）。
8. **传输差异收敛**：Unix socket / 命名管道差异收敛于 `runtime/ipc` 既有
   编译期选择，壳与 bridge 代码零平台分支（窗口创建除外，属进程边界）。

## 备选方案

- **configure 期下载 vs 构建期自定义 target**：选 configure 期——错误面在
  configure（与锁文件门禁同层）、实现最小（`file(DOWNLOAD)` 原生哈希校验），
  且"构建流水线下载、摘要不符即失败"的承诺不折损；代价是冷缓存首次 configure
  变慢（360 MB ≈ 1-2 min，仅壳启用时）。
- **经典 exe + `no_sandbox`（/MT 自洽）**：放弃——M5-01 冻结沙箱默认启用；
  bootstrap+DLL 模型同时满足沙箱与 Mirage 的 /MD 工具链，无需 CRT 分叉或
  runtime/ipc 源码二次编译。
- **`file://` 直载 dist**：放弃——ES module 在 file:// 下被 CORS 拦截，且
  资产来源不可控；自定义 scheme 同时解决模块加载与产品资产边界。
- **`std::thread` 驱动会话循环**：违反 AGENTS.md 并发纪律，放弃；blocking
  worker 是 pinned Executor 的既定承载（devbridge、Runtime Service 同例）。

## 影响与限制

- 新增公共 API：`runtime/ipc/session_client.hpp`（`SessionClient`）；POSIX
  单测 `session_client_test`、Windows 场景并入 `win32_product_process_test`、
  bridge 核心映射单测 `bridge_core_test`（CEF-free，跨平台）。
- 已知限制（随后续工作项收口）：事件推送经 UI 线程扇出、无背压（服务端每连接
  有界队列 + overflow 事件为第一道闸，产品化规模复核在 `M5-08`）；bridge 载荷
  JSON 直嵌 `ExecuteJavaScript`（`encode_event` 产物为受信 canonical JSON，
  CSP 收紧随 M5-11）；Linux 壳构建与往返回证待 CI 矩阵/维护者机器补齐
  （M5-02 验证记录挂账）；安装器布局与 chrome-sandbox 属主随 `M5-11`。

## 验证方式

- `dependency_lock_gate_test` 既有负例仍绿；configure 门禁对本机缓存的
  windows64 tarball 复核 sha1 `fcefc344…`（与 M5-01 挂账的本地下载-摘要复核
  同值）。
- 本机（Windows 11，MSVC 19.44）：全树 Debug 构建 0 诊断、ctest 23/23
  （含 `bridge_core_test`）；ui `npm run check` / `vitest`（含
  `desktop-transport.test.ts` 9 例）/ lint 全绿。
- 壳内 UI 对真实 `mirage-service`（命名管道）：hello（身份 + events 能力
  通告）、`events.subscribe`、`task.submit` → 步骤执行 → `task.updated`
  事件实时回流 → 任务完成呈现（证据：维护者机器截图与壳/服务日志，
  M5-02 验证记录）。
