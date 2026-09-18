# M2：Desktop Environment 核心 Provider 与 Linux Backend

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：[M1](m1-mira-host.md)（已完成：Desktop Environment 抽象、Filesystem/Process
> Provider、绑定适配器、Runtime Service + IPC、Permission 框架、持久化骨架）
> 建议发布点：`release-beta`（tag 待维护者授权后创建）
> 更新日期：2026-09-18

## 目标

完成 Desktop Environment 的核心接口，实现 Application、Window、Accessibility、Screen
Capture 和 Input 的 Linux Backend，完成 Semantic Snapshot、Element Reference 与基础
Desktop Observation，使 Mira 能够稳定操作标准桌面应用（设计文档第 18 节第二阶段）。

## 范围与非目标

范围：

- Desktop Environment 核心 Provider 契约：Application / Window / Accessibility /
  Screen / Input / Clipboard / Notification（Filesystem / Process 已随 M1 交付），
  连同结构化 SemanticSnapshot、多提示 ElementTarget 与 DesktopObservation schema
  v1.0（[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)）。
- Linux Backend（`platform/linux`）：X11 窗口与采集、XTest 输入注入、AT-SPI2
  Accessibility、剪贴板、应用发现/启动、通知；按"先骨架后功能"先以最小动作打通
  Observation -> Action -> Observation 闭环。
- 按需 Observation 组装与 `integration/mira` 能力如实上报扩展，使 Semantic
  Snapshot 进入 Agent 观察面。
- 系统开发依赖的引导策略（本机/CI 缺失平台开发包且无 root 的缓解）与 Linux
  Backend 依赖/事件循环接入决策（D-Bus 栈选型，EXEC-03）。

非目标：

- Mirador 视觉集成与 Visual Reference（M3；`VisualHint` 契约字段保留、解析器缺位
  fail closed）。
- Windows Backend（M4）。
- Wayland 原生（非 XWayland）截图与输入的一等支持：能力缺失时 fail closed 并如实
  上报 capabilities，Portal 路径按需逐项评估。
- Snapshot 增量更新的完整事件驱动实现（首版为全量快照 + 行为结果驱动刷新；事件驱动
  增量作为后续演进，接口保留余地）。
- 桌面 GUI 产品界面（M5）。

## 设计与决策依据

- [设计文档](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md) 第 5、6、7、
  9、10、17、18 节。
- [DEC-002](../decisions/DEC-002-build-test-baseline.md)：构建与测试基线；Mbed TLS
  适配器暂定默认值于 M2 复核（挂 `M2-06`）。
- [DEC-003](../decisions/DEC-003-repository-layout.md)：分层依赖方向
  （`runtime -> desktop -> platform`，Adapter 依赖 Core 接口）。
- [DEC-005](../decisions/DEC-005-desktop-observation-contract.md)：DesktopObservation
  契约 schema v1.0、结构化 SemanticSnapshot、多提示 ElementTarget 与解析顺序契约
  （M2 冻结）。
- [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)：Local IPC 与
  Service 进程形态（协议演进按其变更记录约束）。
- [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)：
  绑定适配器形态与 Provider 暴露路径（M2 沿用，能力面如实扩展）。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)：范围/预算/
  取消语义（M2 新 Provider 全部沿用 Limit/Outcome/CancelToken 纪律，`RULE-07`）。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)：Permission 判定挂点；
  新副作用动作的 Capability 词表随对应 Provider 落地扩展。
- 调研输入：[RPA / Agentic Automation 架构调研](../research/2026-09-16-rpa-agentic-automation-architecture-survey.md)
  第 5.1 节（多提示 ElementTarget、解析顺序写进契约、语义调用优先于键鼠、canonical
  types 统一）。

## 工作项

- [x] `M2-01` Desktop Environment 核心 Provider 契约：Application / Window /
      Accessibility / Screen / Input / Clipboard / Notification 七个 Provider 纯虚
      接口、结构化 SemanticSnapshot（预算内节点集 + 确定性文本渲染）、多提示
      ElementTarget 与解析顺序契约、DesktopObservation schema v1.0（DEC-005 冻结）；
      fake backend 契约测试覆盖正/负向。无平台依赖；现有 Linux 参考后端不实现新
      Provider（访问器缺位 fail closed）。
- [x] `M2-02` Linux Backend 骨架闭环（X11 / XWayland）：WindowProvider（枚举 /
      前台 / 激活 / 几何）、ScreenProvider 最小采集、InputProvider 最小注入
      （XTest），以最小动作打通 Observation -> Action -> Observation；系统开发依赖
      引导策略落地；Linux Backend 依赖与事件循环接入决策（D-Bus 栈选型、glib/事件
      循环与 Executor 的 EXEC-03 互操作）登记决策记录；Permission Capability 词表
      扩展随动作落地。
- [ ] `M2-03` AccessibilityProvider（AT-SPI2）：语义树采集与 SemanticSnapshot 生成
      （节点预算、fail closed）、ElementTarget 解析器（reference / semantic /
      structural）与语义动作路径（activate / input_text 语义面优先于键鼠）、快照
      刷新策略首版（行为结果驱动）。
- [ ] `M2-04` Input / Clipboard Provider 完整：键盘 / 文本 / 鼠标完整动作面与
      剪贴板读写；`input.inject` / `clipboard.read` / `clipboard.write` Capability
      接入 Permission 判定链。
- [ ] `M2-05` Application / Notification Provider 与 ScreenProvider 完整：应用发现
      / 启动 / 终止、通知投递、显示器与 ROI 采集；`application.launch` /
      `application.terminate` / `screen.capture` Capability 接入。
- [ ] `M2-06` Observation 组装与 runtime 接线：desktop 层按需 Observation 组装
      （按任务需求取组件，设计文档第 6 节）、`integration/mira` observe 能力如实
      上报扩展（required 组件映射与 fail closed）、Semantic Snapshot 进入 Agent
      观察面；DEC-002 Mbed TLS 暂定默认值复核记录。
- [ ] `M2-07` 里程碑退出复核：退出条件逐项独立取证（同 M1 退出复核形态）。

拆分纪律：契约先于 Backend（`M2-01` 先行）；每个 Backend 能力先以最小闭环打通再
补全功能（`M2-02` 先于 `M2-03`..`M2-05` 的完整面）；Contract 测试全部经 fake
backend，真实后端集成测试单独标注并在有显示环境的前提下运行。

## 风险与阻塞

- 本机与 CI 缺平台开发头文件（`libxtst-dev`、`libatspi2.0-dev`、`libdbus-1-dev`、
  `libwayland-dev`）且沙箱无 root：已实测 `apt-get download` + `dpkg -x` 用户前缀
  提取可行，作为引导策略于 `M2-02` 落地（脚本 + CMake 前缀选项）；备选为维护者
  一次性安装系统包。X11 / Xi / Xrandr / GIO 开发文件本机已可用。
- Wayland 会话下原生协议（非 XWayland）截图与输入受合成器协议与 Portal 授权限制：
  M2 以 X11 / XWayland 为一等路径，原生能力缺失 fail closed 且 capabilities 如实
  收缩；不虚报跨显示服务器能力（`RULE-08`）。
- AT-SPI2 接入形态（raw D-Bus 客户端 vs libatspi 绑定）影响 glib 主循环与
  Executor 的互操作形态（EXEC-03）：`M2-02` 决策记录定案后再实现（`M2-03`）。
- 无 Xvfb 且沙箱不可安装：headless 测试拓扑（嵌套 Xwayland / Xorg dummy / 用户前缀
  提取 xvfb）随 `M2-02` 定案；真实桌面集成测试仅在有显示环境的机器上执行。
- pinned mira 0.1.x `IEnvironment` 观察能力面如需随 Semantic Snapshot 扩展，按
  [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  的迁移路径与依赖升级流程处理；能力缺口按工程规范第 9.4 节登记台账。

## 测试与退出条件

- [ ] `debug`、`release`、`asan`、`ubsan` 预设构建通过，`tsan` 按本机注意事项运行；
      `ctest` 全绿且无 skip（`DOD-03`）。
- [ ] 全部新 Provider 契约经 fake backend 测试覆盖正/负向（预算、取消、fail
      closed、非法参数）（`DOD-04`）。
- [ ] headless X 拓扑端到端：observe 返回含 SemanticSnapshot 的 Observation，
      ElementTarget 经解析执行后 Observation 更新；Linux Backend 真实集成测试在
      有显示环境要求下标注并运行。
- [ ] 公共头边界检查通过：`desktop`、`runtime`、`platform` 公共头 pinned-free
      （`DOD-01`）。
- [ ] DEC-005 冻结、Linux Backend 依赖决策记录、（若触发）依赖反馈台账条目同步；
      计划状态与验证证据同步（`DOD-05`）。
- [ ] Commit / MR 符合工程规范第 10 节（`DOD-06`）。

## 验证记录

2026-09-18：`M2-01` Desktop Environment 核心 Provider 契约完成。

- 范围：desktop 层冻结七个核心 Provider 纯虚接口（Window / Accessibility / Screen /
  Input / Clipboard / Application / Notification，各带 Limit/Outcome 与全默认便捷
  重载）；`geometry.hpp` 移出 `WindowGeometry`；`semantic_snapshot.hpp` 定义结构化
  快照（节点预算 4096 fail closed + 确定性文本渲染）；`element_reference.hpp` 重写为
  多提示 `ElementTarget`（reference / semantic / structural / visual / spatial /
  raw）与契约解析顺序；`desktop_observation.hpp` 升 schema 1.0（新增
  `focused_element` / `pointer_state`，`semantic_snapshot` 结构化）；
  `DesktopEnvironment` 访问器扩至 9 个（缺位 null fail closed）；共享助手
  `is_valid_key_name` / `is_valid_utf8` 与 `result_too_large` 错误词表。契约义务
  （预算拒绝不截断、取消先于副作用、非法参数先于状态变更）由内存
  `tests/support/fake_desktop_environment.hpp` 与真实 Backend 共同承担。冻结语义
  登记 [DEC-005](../decisions/DEC-005-desktop-observation-contract.md)。
- 依据：设计文档第 5、6、7、9 节；调研第 5.1 节；`DEC-003` / `DEC-005` / `DEC-009` /
  `DEC-010`；`RULE-01` / `RULE-05` / `RULE-07`。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0）：
  - 新增 `tests/desktop/provider_contract_test.cpp`（独立验证增强后 20 场景 312
    断言）：9 访问器 fail closed、窗口枚举预算 `result_too_large`（恰边界放行 /
    超 1 拒绝）、激活互斥聚焦与未知 id 状态不变、快照预算与空快照成功路径、渲染
    确定性与 `[focused]` / `[disabled]` 格式、ElementTarget 提示计数（含 (0,0)
    raw 与零偏移 spatial 不计、单轴计）、捕获预算/ROI/取消、键名词表与 UTF-8
    边界（越界前导 F5-F7、代理区、超长、截断、坏延续）、剪贴板读预算与写侧
    `invalid_argument` 纪律、应用单实例 / 未知 id 不建实例 / stuck 注入
    `deadline_exceeded`、通知预算、取消后状态不变量逐 Provider 断言、M1
    filesystem / process 契约在 fake 上的保持。
  - `tests/desktop/desktop_observation_test.cpp` 更新至 v1.0（13 断言）。
  - 独立验证修复 fake 一处不一致（`FakeApplication` 空 id / 非正超时改在副作用前
    以 `invalid_argument` 拒绝，与其他 Provider 先例对齐）并补充 8 个边界场景
    （断言 192 → 312）。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` configure + build + ctest 均
    **18/18 通过、0 skip**；`tsan` 直跑复现本机 ASLR 怪癖（README 注意事项），
    `setarch $(uname -m) -R ctest` 18/18 通过。`mirage-format-check` 与
    `mirage-boundary-check`（32 头 0 违规）通过；既有测试无回归。
- 限制：契约语义经内存 fake 验证；真实 X11 / AT-SPI2 backend 行为随 `M2-02` /
  `M2-03` 落地并验证。剪贴板写预算错误码为 `invalid_argument`（读侧
  `clipboard_too_large`）——调用方载荷与环境中内容的有意区分，已在契约头注明。
  `role` 词表与 structural path 语法随 `M2-03` AT-SPI2 实现冻结。独立验证期间
  观察到既有 `runtime_service_test` 在高负载并行下偶发失败（约 9 次全量 2 次，
  隔离复跑稳定；M2-01 未触及 runtime 代码），需单独排查，不阻塞本工作项。
- 同步：设计文档第 5、6、7 节注记、`DEC-005`（新）、总计划当前状态 / 里程碑索引 /
  决策表、本验证记录。


2026-09-18：`M2-02` Linux Backend 骨架闭环（X11）完成。

- 范围：`platform/linux` 新增私有 X11 前端 `x11_backend.{hpp,cpp}`（X11 类型不出
  公共头，`RULE-01`）——单 `Display` + `XInitThreads` + 互斥锁串行化；WindowProvider
  （XQueryTree 枚举 + viewable/override_redirect 过滤、`_NET_WM_NAME` 标题、全局
  坐标几何、EWMH `_NET_ACTIVE_WINDOW` 焦点 → 无 WM 时 X input focus 回退且两条
  路径 focused 一致；activate = EWMH 消息 → 回退 SetInputFocus+Raise）；ScreenProvider
  （RandR monitors → 无 RandR 单一 "screen" 根几何；根 framebuffer XGetImage
  采集 display/window/ROI，Bgra8 行拷贝，预算前置拒绝）；InputProvider（XTest，
  chord 经 desktop 层 `parse_key_chord` 全量预解析，`type_text` 逐码点 level 0/1
  位移弦）。`LinuxDesktopEnvironment` 增 `X11Options` opt-in（默认关闭，连接失败
  访问器 null fail closed），身份更名 `mirage-linux`。`x11_backend_stub.cpp` 使
  X11 开发包缺失时平台库照常构建（fail closed）。desktop 层增量
  `parse_key_chord`；permission 词表追加 `window.activate` / `screen.capture` /
  `input.inject`（默认 allow，DEC-015 第 6 条）。headless 测试拓扑 = 一次性私有
  Xvfb（`-displayfd` 高端 fd；`$MIRAGE_XVFB` → PATH → 用户前缀定位；缺失响亮
  失败）。依赖与事件循环接入决策登记
  [DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)
  （第 3 条 glib 家族 D-Bus 栈在 `M2-03` 兑现）。
- 依据：设计文档第 5、10、18 节；`DEC-005` / `DEC-008` / `DEC-009` / `DEC-010` /
  `DEC-015`；`RULE-01` / `RULE-03` / `RULE-05` / `RULE-07`；executor-integration
  blocking-io 卡（外部事件循环边界）。
- 验证（Independent-Verification-Agent，Linux x64，Ubuntu 24.04，GCC 13.3.0，
  真实 Xvfb 拓扑，两轮）：
  - 新增 `tests/platform/x11_backend_test.cpp`（独立验证增强后 11 场景 140 断言）：
    连接失败 fail closed、枚举与 `result_too_large`（恰边界）、激活后 front_window
    与 list_windows focused 一致、ROI/窗口/整屏采集红通道字节断言、未知显示器
    `not_found`、未映射窗口采集与激活 fail closed 且焦点不变、越界 ROI 经静默
    handler 不杀死进程、XTest MotionNotify/Button1/键码/Shift 弦逐事件真实送达
    （150 ms deadline 证明拒绝注入零泄漏）、type_text 预算与键位表缺口注入前
    拒绝、取消与非法参数负向。
  - 独立验证修复 1 个链接缺陷：stub 分支的 X11Backend 外析构造发出 vtable 引用
    11 个未定义覆写符号，任何链接平台库的目标失败；补齐 stub 覆写（`std::abort()`
    不可达体）后独立目录 `-DCMAKE_DISABLE_FIND_PACKAGE_X11=ON` configure+build
    159/159 目标成功、`build.ninja` 无 `-lX11`、stub 树 ctest 18/18。
  - permission_test 补三新能力的名称/解析往返、11 条近似串负例、默认策略、槽位
    隔离与控制器语义（→95 断言）；provider_contract_test 补 `parse_key_chord`
    与 `is_valid_key_name` 一致性扫描（→351 断言）。
  - 预设矩阵：`debug` / `release` / `asan` / `ubsan` configure + build + ctest 均
    **19/19 通过、0 skip**（x11_backend_test 在各预设实跑）；`tsan` 按 README
    注意事项 `setarch $(uname -m) -R ctest` 19/19。`mirage-format-check` 与
    `mirage-boundary-check`（32 头 0 违规）通过。
  - 契约澄清（独立验证发现）：`capture_window` 对不可见窗口按根帧模型返回像素，
    与原注释"完全遮挡 fail closed"分歧——已修正为：非 viewable fail closed
    （`not_found`），注释如实描述"窗口边界处的屏幕内容（含重叠内容）"（X 无诚实
    的逐窗遮挡查询）。
- 限制：EWMH 激活路径在 Xvfb（无 WM）上不可测，仅回退路径被验证，有 WM 拓扑随
  `M2-06` 真实桌面冒烟补齐；Wayland/XWayland 合成路径、16bpp 拒绝、多显示器
  RandR 变体未覆盖；`InputLimits.timeout` 仅校验为正，Xlib 无单调用超时机制，
  强制收敛依赖调用方执行上下文（`M2-06` 接线验证）；`runtime_service_test` 历史
  高负载偶发失败本轮 5 预设未复现，继续观察。
- 同步：`DEC-015`（新）、`DEC-005` 契约头澄清、permission 词表文档、总计划
  里程碑状态、本验证记录、README Xvfb 说明、CI 依赖安装。
