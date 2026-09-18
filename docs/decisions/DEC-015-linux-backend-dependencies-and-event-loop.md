# DEC-015：Linux Backend 依赖与事件循环接入（X11 骨架、AT-SPI2 栈、headless 测试拓扑）

> 状态：Accepted
> 日期：2026-09-18
> 负责人：Mirage 维护者
> 冻结里程碑：M2（`M2-02` 落地；第 3 条在 `M2-03` 首次兑现；第 5 条随 `M2-03` 修订）
> 替代/被替代：无（细化 [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
> 的 M2 演进路径）

## 背景与问题

`M2-02` 开始实现 Linux Backend 骨架（X11 / XWayland 窗口、采集、XTest 输入），随之
而来四个必须先定案的问题：

1. X11 栈的并发纪律：Xlib 非线程安全，而 Provider 由消费方在 Executor blocking
   worker 上调用（EXEC-04），不能引入线程或脱离 Executor 的生命周期（RULE-03）。
2. M2-03 的 AT-SPI2 Accessibility 与 M2-05 的通知 / Portal 需要 D-Bus 栈；glib
   主循环是第三方事件循环，必须按 EXEC-03 的互操作边界接入 Executor。
3. 开发机与 CI 的平台依赖可获得性：沙箱环境无 root、早期缺 `libxtst-dev` 等开发
   头文件；X11 依赖必须保持可选（工程约束：平台相关编译单元可选）。
4. 无桌面环境（尤其 CI）下如何真实地测试 X11 Backend：跳过测试违反 DOD-03
   （无 skip 冒充成功）。

## 决策

1. **X11 / XWayland 为第一目标，能力如实收缩**：`LinuxDesktopEnvironment` 以
   `X11Options`（默认关闭）显式 opt-in X11 面；连接失败（如 Wayland 原生会话无
   XWayland）时 window / screen / input 访问器返回 null，消费方 fail closed——
   绝不静默降级为假实现。窗口激活在有 EWMH 窗口管理器时走 `_NET_ACTIVE_WINDOW`
   客户端消息，无 WM 时回退 `XSetInputFocus` + `XRaiseWindow`；`front_window` /
   `list_windows` 的焦点判定按同一优先级（EWMH 属性 → X input focus），两条路径
   的 focused 标志保持一致。
2. **Xlib 并发纪律**：一个进程内 `Display` 连接 + `XInitThreads()` + 一把互斥锁
   串行化全部 Xlib 调用；Provider 方法保持同步、有界（预算在副作用前检查，取消在
   副作用前观察）。不建线程、不建队列（RULE-03）；调用上下文是消费方选择的
   Executor blocking worker（EXEC-04）。异步 X 错误经静默 handler 抑制 stderr
   噪音，结果经 X 调用返回码与 `XSync` 呈现。
3. **M2-03+ 的 D-Bus 栈选 glib 家族**：Accessibility 用 `libatspi`（AT-SPI2 官方
   客户端，系统包 `libatspi2.0-dev`），通知 / Portal 用 GIO `GDBus`；两者共享
   glib 主循环，由一个长寿命 Executor blocking I/O worker 承载
   （`IBlockingIoWorker::run(stop_token)` + `wakeup()` 唤醒边界，devbridge 事件
   循环先例；EXEC-03），停止经 stop token + glib 主循环退出闭合。主循环事件不得
   直接执行业务 handler（root AGENTS.md 第 11 条）。
4. **平台开发依赖保持可选**：构建经 CMake `find_package(X11)`（含 Xtst / Xrandr
   组件）探测；缺失时编译 `x11_backend_stub.cpp`（open 恒 null），平台库照常
   构建。系统开发包（维护者已安装）为主路径；无 root 环境的回退是发行包用户前缀
   提取（`apt-get download` + `dpkg -x`，已实测），不走 vendored 第三方代码。
5. **Headless 测试拓扑：私有 Xvfb 实例**：`tests/support/xvfb_display.hpp` 以
   `-displayfd`（高端 fd，避免 pipe fd 碰撞）在独立显示号上 spawn 一次性 Xvfb，
   测试结束回收；二进制定位顺序 `$MIRAGE_XVFB` → `$PATH` →
   `~/.local/mirage-sysroot/usr/bin/Xvfb`（用户前缀提取产物）。找不到 Xvfb 时
   测试响亮失败并给出指引，不 skip（DOD-03）；CI 显式安装 `xvfb`。
6. **新桌面动作的 Permission 默认策略**：`window.activate` / `screen.capture` /
   `input.inject` 默认 allow，与 M1 的 `process.execute=allow` 同一理由——M2 的
   目的即 Observation -> Action -> Observation 闭环，默认拒绝会使里程碑闭环依赖
   隐式配置；判定链（RULE-05）与确认挂点保持可用，产品级策略收紧随 M5 的持久化
   设置面落地（[DEC-010](DEC-010-m1-permission-framework.md) 词表延续）。

## 备选方案

- **raw D-Bus / libdbus 直连 AT-SPI2 协议**：否决。AT-SPI2 D-Bus 协议面
  （cache、树遍历、事件信号）体量大，手工编解码的维护成本换不来收益；libatspi
  引入的 glib 反正是 D-Bus 栈的公共底座。
- **sd-bus（libsystemd）**：否决。目标环境不保证 libsystemd 开发文件。
- **嵌套 Xwayland 作测试显示器**：否决。它依赖会话合成器并把测试窗口画到用户
  真实桌面上；Xvfb 完全离屏、无外部依赖。
- **每 Provider 一个 Display 连接**：否决。Xlib 连接是进程级资源，多连接放大
  错误处理面且无收益；单连接 + 互斥锁已满足同步 Provider 的吞吐需求。

## 影响与风险

- `platform/linux` 新增私有 X11 前端（`src/x11_backend.{hpp,cpp}` + stub）；公共
  头不出现任何 X11 类型（RULE-01，`mirage-boundary-check` 保持零命中）。
- `LinuxDesktopEnvironment` 构造签名扩展（`X11Options` 追加参数，默认关闭——既有
  M1 拓扑与测试不受影响）；环境身份 `mirage-linux-reference` 更名
  `mirage-linux`（参考后端已演进为 M2 真实后端的骨架）。
- Xvfb 与真实合成器（XWayland + WM）存在行为差：EWMH 路径在 CI 上无 WM 可验证，
  只测回退路径；有 WM 拓扑的验证随 M2-06 在真实桌面冒烟补齐。
- XTest 文本注入按"逐码点解析 + level 0/1 位移弦"实现，无 per-window 键盘组态
  处理；完整文本注入语义随 `M2-04` 加固。
- `type_text`/`inject_key` 的超时预算校验为正即接受；Xlib 单次调用无内在超时
  机制，超时强制收敛依赖调用方的执行上下文（blocking worker 取消路径），随
  runtime 接线（M2-06）验证。

## 验证方式

- `tests/platform/x11_backend_test.cpp`（私有 Xvfb，真实 X server）：连接失败
  fail closed、窗口枚举与预算拒绝、激活/焦点一致性（回退路径）、ROI / 窗口 /
  整屏采集字节与预算拒绝、XTest 指针与键盘事件真实送达监听窗口、位移弦文本、
  取消与非法参数负向。
- 预设矩阵 + format + boundary 门禁（见 M2 计划验证记录）。

## 变更记录

- 2026-09-18（`M2-03`）：第 5 条测试拓扑修订——Ubuntu 24.04 的 at-spi2-registryd
  （at-spi2-core 2.52.0）在处理 `Socket.Embed` 时于 libdbus 派发路径段错误
  （core dump 取证：`dbus_connection_dispatch` 内崩溃，与客户端实现无关）；
  AT-SPI2 测试拓扑改为 fixture 在私有 D-Bus session 上直接持有
  `org.a11y.atspi.Registry` 名字并导出 desktop 树（backend 只经
  `atspi_get_desktop` 读取，不依赖 registryd 的应用注册面），libatspi 经
  `AT_SPI_BUS_ADDRESS` 指向该私有总线。真实桌面（registryd 正常运行）的产品
  路径不受影响；有 WM/真实 AT-SPI 应用的验证随 `M2-06` 冒烟补齐。同时落地：
  SemanticSnapshot 角色词表冻结（`snapshot_role` 显式映射表 + 其余角色小写
  连字符化 fallback）、structural path 语法冻结（自 application 根起的
  `role/name` 成对段）、libatspi 包装对象所有权模型（全局弱表 → backend 进程级
  对象池，容量 8192 为 `RULE-07` 上限）、AccessibilityProvider 契约扩展
  （`activate_element` / `set_text`，visual/spatial/raw 提示以
  `unsupported_hint` fail closed，DEC-005 解析顺序不变）；上游
  `atspi-2.pc` 漏声明 gobject-2.0 依赖（打包缺陷），构建显式补链。

## 关联文档和工作项

- 设计文档第 5、10、18 节；[DEC-005](DEC-005-desktop-observation-contract.md)、
  [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)、
  [DEC-009](DEC-009-provider-scope-budget-cancellation.md)、
  [DEC-010](DEC-010-m1-permission-framework.md)。
- Executor 依据：`third_party/mira/third_party/executor/docs/skill/
  executor-integration/references/blocking-io.md`（外部事件循环承载）。
- 工作项：[M2 计划](../plans/m2-desktop-environment.md) `M2-02`（本决策）；
  `M2-03`（第 3 条兑现）、`M2-04`（文本注入加固）、`M2-06`（真实桌面冒烟）。
