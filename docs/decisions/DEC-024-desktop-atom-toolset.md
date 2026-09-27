# DEC-024：M5 桌面原子工具集（atom.catalog 人口与 ToolCall 执行路径）

> 状态：Accepted
> 日期：2026-09-27
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-05` 第二轮：桌面原子工具注册）
> 替代/被替代：无；本记录兑现
> [DEC-023](DEC-023-workflow-contract-face.md) 留给第二轮的挂账（`workflow.atom.catalog`
> 人口 + ToolCall 步执行路径），wire 契约不变（`meta.version` 仍为 5，无新请求 /
> 事件 / 成员）

## 背景与问题

`M5-05` 第一轮交付了 `workflow.*` 请求面与 pinned `WorkflowRuntime` 承载，但
`workflow.atom.catalog` 为空目录、ToolCall 步无派发路径（`set_tool_registry` 缺席，
含 ToolCall 步的 Strict 定义在 `create_run` 准入即被 pinned fail closed 拒绝）。第二轮
需要定案：

1. **atom 目录的真实承载**：DEC-022 决策 1 已定目录经 pinned
   `BuiltinToolRegistry::exposed_tools()` 投影承载，不自建第二套目录模型；缺的是
   "桌面 Provider 能力 → BuiltIn 工具注册"的路径与工具集清单。
2. **执行路径**：pinned 工作流运行时把 ToolCall 步经注册表派发（`arguments` 携带保留
   成员 `"tool"`，其余成员为经 schema 校验的工具输入，handler 在驱动线程同步执行、
   必须有界、不得抛出）。Mirage 侧需要把 handler 接到桌面 Provider 上，并补齐
   RULE-05 权限判定、操作边界（预算）与取消语义。
3. **呈现**：`workflow.atom.catalog` 的 wire 形状已冻结（`ExposedTool`），编辑器侧
   按真实目录呈现；UI mock 静态目录（M1.5 `ATOM_CATALOG`）不作为真实能力依据。

## 决策

1. **落点：integration/mira 新增 `DesktopAtomToolset`**。它是 pinned 边界层（公共面
   可含 pinned 类型，`WorkflowEventBridge` 先例），以
   `build(environment*, gate)` 在绑定的 `mirage::desktop::DesktopEnvironment` 上构建
   并人口一个 `mira::BuiltinToolRegistry`：每个 Provider 方法一个原子工具；**Provider
   缺席（null）即对应原子不注册**——目录如实反映绑定环境的能力，不静态宣称。null
   环境产出空注册表（诚实为零，不是降级）。服务在 `start()` 构建工具集并传入
   `MiraHost::attach_workflow_surface(executor, bridge, tools)`（新第三参数，非空强
   制）；宿主在 attach 时经 pinned `set_tool_registry()` 安装。
2. **初始原子清单（13 项，版本 1.0.0）**，按"每个 handler = 一次有界 Provider 调
   用"裁剪：
   - 观察读（无权限词表条目，与 observe 路径同位，不经门禁）：
     `desktop.window.list` / `desktop.window.front` / `desktop.application.list` /
     `desktop.accessibility.semantic_snapshot`（确定性渲染文本 + 节点预算 512）。
   - 门禁读（词表有条目即判定，与任务驱动一致，RULE-05 覆盖读）：
     `desktop.filesystem.read_text`（`filesystem.read`）、
     `desktop.clipboard.read_text`（`clipboard.read`）。
   - 门禁副作用：`desktop.process.execute`（`process.execute`）、
     `desktop.window.activate`（`window.activate`）、
     `desktop.application.launch` / `desktop.application.terminate`
     （`application.launch` / `application.terminate`）、
     `desktop.clipboard.write_text`（`clipboard.write`）、
     `desktop.input.type_text`（`input.inject`）、
     `desktop.notification.post`（`notification.post`）。
   - **挂账（不注册，后续轮评估）**：屏幕捕获三方法（结果是以 ArtifactRef 承载的
     帧载荷，须走 artifact 发布管线而非工具 JSON 结果，M3-05 视觉管线承担）；
     `accessibility.activate_element` / `set_text`（需 ElementTarget 解析缝与视觉/
     语义引用落地，M5-09/编辑器轮）；指针类输入（依赖视觉参考/Overlay 的坐标故
     事）；`desktop.wait` 类延时（pinned IR Control/谓词面已覆盖工作流内等待）。
3. **执行路径（有界 + fail closed）**：handler 入口先查驱动取消探针（pinned
   `OperationContext::cancelled()`，即 run 的步取消旗标），再经权限门，最后调用
   Provider；Provider 结果映射到 pinned 错误词表（`cancelled` → Cancelled、
   `invalid_argument` / `not_found` / `permission_denied` 原样、`*_too_large` →
   ResourceExhausted、`unsupported_*` → UnsupportedCapability，其余 →
   PlatformError），稳定 Provider code 保留在 safe_message 中。工具结果为紧凑 JSON
   对象，文本载荷预算 64 KiB（`filesystem.read_text` 的读预算即此值，超预算拒绝而
   非截断；`process.execute` 流式输出截断并置 `output_truncated`）。
   **进程内取消边界**：handler 在 Provider 调用期间无法把 pinned 探针桥接进
   `desktop::CancelToken`（无宿主线程序纪律禁止为此建线程），故进行中的
   `process.execute` 由其自身超时预算收敛（参数 1–120 s，缺省 30 s），run 取消在
   步边界收敛——与任务驱动"Provider 自身预算 + 步间检查"的粒度一致，如实声明。
4. **RULE-05 权限缝（依赖倒置）**：integration 层不依赖 runtime，故工具集以
   pinned-free 回调 `AtomPermissionGate(capability, resource, cancelled_probe) -> bool`
   注入门禁；服务用共享 `PermissionController` 实现该缝（词表名解析、null 控制器与
   未知能力一律拒绝）。Confirm 规则的等待经 cancelled 探针与 run 取消对齐
   （DEC-020 纪律）。无权限词表条目的观察原子不经门禁（与 M2 事实一致：权限框架
   只覆盖 11 项能力，观察读不在其中）。
5. **服务面**：`workflow.atom.catalog` 返回工具集 `exposed_atoms()` 投影（注册表
   wire-name 排序的确定性 exposed view → `ExposedTool`）；`workflow.run` 的 Strict
   运行经注册表派发 ToolCall 步，DryRun 门禁（发布）只规划不派发（pinned 语义，
   发布含 ToolCall 步的定义安全）。**pinned W-02 约束（实现期核实）**：派发策略下
   `create_run` 准入要求副作用 ToolCall 步必须声明 `verification` 谓词（且谓词值
   限标量）；发布 DryRun 以空参绑定，故该谓词在门禁下必须缺席可容（可选参数无默
   认值 → `run_parameter` 缺席 → NotEvaluable 计数，RULE-10）或以运行参数满足。编
   辑器为副作用原子生成步骤时必须落此谓词，属编辑器增量（DEC-013 IR 对齐）的验
   收项。
6. **TR2 取舍（DEC-023 决策 4 的实现轮复核）**：`attach_workflow_tool_refs` /
   `skill_tool_registrations` 本轮仍不消费——编辑器不产生 v1.1 工具引用形态、
   Skill 发布产品面未落地（POST-05）；Degraded 兼容呈现随之仍不进 wire。宿主
   verbatim 承载升级后 `WorkflowRuntime`，后续轮按公开 API 接线。

## 备选方案

- **工具集放 runtime/mira_host**：否决。registry 构建/投影需要 pinned 类型（spec、
  schema、JsonValue），host 公共头不得直含 pinned 头；integration 是 designated
  pinned 边界层，且 handler 只依赖 desktop + pinned，无需 runtime。
- **权限门直接依赖 `PermissionController`**：否决，integration → runtime 反向依
  赖；回调缝保持依赖方向（runtime 实现 integration 声明的缝）。
- **`workflow.atom.catalog` 由服务自建目录模型**：否决，违背 DEC-022 决策 1（经
  pinned exposed view 承载，单一事实源）。
- **第一轮同时落地工具注册**（DEC-023 备选重申）：维持推迟裁决；本轮独立交付执
  行面，契约面零变更。
- **所有 Provider 方法一次性全量注册（含捕获/元素动作）**：否决。捕获结果是
  Artifact 载荷不是工具 JSON；元素动作缺解析缝。目录宣称超出执行路径的能力即虚
  报（DEC-006/M4"如实降级"纪律同源）。

## 影响与风险

- `MiraHost::attach_workflow_surface` 签名新增第三参数（非空强制）：编译期破坏性
  变更，仅服务与测试两个调用点，随本变更加线；wire 契约与 golden vectors 零变更。
- `ServiceCore` 新增 `workflow_tools` 持有（start() 构建，服务生命周期）；工具集
  handler 捕获 Provider 裸指针，环境由 ServiceCore 持有且生命周期覆盖工作流面
  （teardown 先 `shutdown_workflow_surface()`），无悬垂窗口。
- 权限缺省策略下（M5 产品策略面未收紧，DEC-010/015），门禁读/副作用原子当前
  Allow 直通，确认面沿 M5-03 异步确认；策略收紧（M5-07）后 workflow ToolCall 与
  任务驱动同受其约束，无第二执行面。
- 注册表容量（pinned 注册表无上限）由静态清单有界化（≤13 项），不接受运行期注
  册，无饱和路径。

## 验证方式

- `desktop_atom_toolset_test`（新增）：null 环境 → 空注册表；完整 fake 环境目录
  13 项、确定性排序、schema 可解码、副作用分级正确；裸环境 → 零原子；null 门禁
  拒绝门禁原子；门禁收到能力/资源/探针；观察原子无门禁可用；读原子映射 Provider
  预算与稳定错误；process 原子携带命令结果；取消驱动在 Provider 前拒绝；快照原
  子渲染语义树并透传 `unsupported_window`；pinned 至多一次派发不变量。
- `mira_host_test`：attach 签名变更接线 + null 工具集拒绝；新增端到端场景——
  含 `desktop.clipboard.write_text` ToolCall 步的定义发布（DryRun 不派发）、
  Strict 运行落成副作用（fake 剪贴板内容）、门禁翻转后同定义运行 fail closed。
- `session_client_test`：活服务目录断言从"空目录"更新为"测试绑定的真实能力"
  （仅 M1 文件系统 + 进程两项原子）。
- 既有门禁零回归：全树构建、`mira-host` / `runtime_service` / golden vectors
  （wire 未变）、format 与 public-header boundary 检查。

## 关联文档和工作项

- [DEC-023](DEC-023-workflow-contract-face.md)（契约面与第二轮挂账）、
  [DEC-022](DEC-022-upstream-capability-adoption.md)（决策 1：目录经 pinned 投影
  承载）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)（wire 契约
  冻结）、[DEC-010](DEC-010-m1-permission-framework.md)（RULE-05 权限面）。
- pinned 依据：`tool_executor.hpp`（`BuiltinToolRegistry` / `BuiltinToolSpec` /
  schema 子集与至多一次派发）、`workflow_ir.hpp`（ToolCall `arguments["tool"]` 绑
  定、`workflow_policy_dispatches_side_effects`）、`workflow_runtime.hpp`
  （`set_tool_registry` / 派发通道）。
- 工作项：`M5-05` 第二轮（桌面原子工具注册；编辑器完整版接真实面为第二轮另一
  增量，随后交付）。
