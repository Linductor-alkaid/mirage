# DEC-010：M1 Desktop Permission 框架雏形

> 状态：Accepted
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1
> 替代/被替代：无；在 [DEC-009](DEC-009-provider-scope-budget-cancellation.md)
> 的 Provider 硬边界之上叠加能力判定层（两者叠加而非互替）

## 背景与问题

`M1-06` 要求落地 Desktop Permission 框架雏形：`filesystem.read` /
`filesystem.write` / `process.execute` Capability 判定与用户确认挂点（确认 UI
可延后到 M5）。设计文档第 15 节规定了判定流（Allowed → 执行；Confirmation
required → 用户 → Approve/Reject；Denied → 拒绝），`RULE-05` 要求有真实副作用
的动作必须经过 Permission 判定并与 Agent Trace 关联。`M1-05` 之前的任务驱动
路径上没有任何策略判定层：只要在 Provider 读范围内、预算之内，动作一律执行；
也没有为 M2+ 的写 Provider 预留冻结的能力词表与判定语义。

## 决策

1. **落点在 runtime 层**：新增 `runtime/permission` 目标
   （`mirage_runtime_permission`，pinned-free，纯 std），对应设计文档第 17 节
   的 `runtime/permission/` 目录。判定发生在任务驱动器执行任何副作用之前，
   且在 pinned 操作准入（`begin_operation`）之前——被拒绝的动作不进入控制
   平面，不产生 OperationRecord。desktop 层 Provider 保持 permission-agnostic，
   其 PathScope / 预算 / 取消硬边界不受判定结果影响、始终生效（DEC-009 第 1
   条备选方案的否决理由在此落实为"叠加"）。
2. **能力词表**：`Capability` 枚举 + 稳定字符串名 `filesystem.read` /
   `filesystem.write` / `process.execute`（设计文档第 15 节词表的 M1 子集；
   其余能力随对应 Provider 在 M2+ 进入词表）。`filesystem.write` 在 M1 没有
   Provider，但判定语义从框架落地起存在且默认拒绝（fail closed）。
3. **策略与判定**：每能力一条 `Rule`（`allow` / `confirm` / `deny`），由
   `PermissionController::authorize()` 判定为 `allowed` / `confirmed` /
   `denied` / `confirmation_rejected` 四种稳定决策。`confirm` 规则恰好调用
   一次确认挂点；`allow` / `deny` 不触碰挂点。
4. **用户确认挂点**：`ConfirmationHandler` 纯虚接口（同步、进程内、必须
   立即返回——M1 驱动线程无交互上下文）。默认实现 `DenyAllConfirmation`
   （headless 拓扑 fail closed）；`AllowAllConfirmation` 仅供开发/测试显式
   选择。M5 产品 UI 以 Local IPC 上的异步确认面替换该挂点，判定语义不变。
5. **默认策略**：`filesystem.read=allow`、`process.execute=allow`、
   `filesystem.write=deny`，确认挂点默认拒绝。默认保持 M1-05 既有的开发/
   测试拓扑行为（读取本就被 PathScope 硬约束、执行本就有预算与取消路径），
   收紧经由显式配置进行而非默认破坏拓扑。
6. **Trace 关联（RULE-05 的 M1 形态）**：每个步的权限决策以稳定字符串记录
   进 StepRecord / `task.inspect` 的 StepView（新 `permission` 字段，协议 v1
   附加字段，[DEC-007](DEC-007-local-ipc-and-runtime-service.md) 变更记录），
   允许的步与既有 operation id 并列；拒绝的步以 `permission_denied` 错误
   结算（fail-fast，DEC-007 第 5 条）且无 operation id（无桌面动作发生）。
   被拒绝的步不上报 pinned 控制平面，其 trace 落在 Mirage 侧步记录中。
7. **服务配置与 CLI**：`ServiceConfig` 增加 `permission_policy` 与
   `confirmation`（空 = fail closed 默认）。`mirage-service` 增加可重复
   `--perm CAPABILITY=allow|confirm|deny` 与 `--confirm allow|deny`；
   `mirage service start` 透传两者并在本地预校验。服务启动时打印生效策略。

## 备选方案

- **判定放 desktop Provider 内部**：否决。Provider 是平台能力面，策略判定
  属运行时产品层（设计文档第 15 节的 Mirage Permission 位于 Mira Action 与
  Desktop Environment 之间）；放 Provider 会把策略下沉进平台边界，违反分层。
- **判定放 integration 绑定适配器**：否决。适配器只做 pinned 契约桥接，
  M1 的动作面（脚本化步）在服务驱动器上，绑定层没有任务身份可关联 trace。
- **拒绝也先 `begin_operation` 以进入 pinned trace**：否决。被拒绝的请求
  没有桌面动作，为其制造控制平面记录会把"未执行"伪装成"已调度"；Mirage
  侧步记录 + `permission_denied` 错误已可观察。
- **默认全 deny（M1 即安全默认）**：否决。会破坏 M1-05 冻结的拓扑与全部
  既有冒烟路径（CLI 任务无一步可跑），而 M1 服务本身就是用户显式以
  `--read-root` 声明范围后启动的开发拓扑；默认策略的收紧随 M5 产品化
  （Workspace 级策略）另行决策。
- **确认挂点带超时预算**：否决。同步进程内调用无法可靠超时（需要线程），
  M1 契约改为"必须立即返回"；异步确认面（M5）自带等待预算。

## 影响与风险

- `task.inspect` StepView 增加附加字段：解码端缺省为空字符串，旧客户端不
  受影响；协议版本保持 v1（DEC-007 变更记录）。
- 权限判定是策略层，不防御 Provider 边界（TOCTOU、Shell 内部访问）——这些
  仍按 DEC-009 的已知限制处理；判定层被绕过的前提是绕过任务驱动器，M1
  唯一动作入口就是驱动器。
- `filesystem.write=allow` 在 M1 无效果（无 Provider），不构成能力承诺。
- M1 确认挂点是进程内同步面，产品确认流（持久化待确认请求、UI 交互、
  超时收敛）在 M5 以 Local IPC 异步面重新落地时需要新的决策记录。

## 验证方式

- `tests/runtime/permission_test.cpp`：能力/规则/决策稳定名与解析负向、
  判定器语义（allow/deny 不触碰挂点、confirm 恰好一次调用、两向结果与
  稳定 reason）、默认策略值、两个内建 handler。
- `tests/runtime/task_permission_test.cpp`：经真实 RuntimeService + IPC 覆盖
  默认策略通过、deny 拒绝（步 failed + `permission_denied` + 后续 skipped +
  任务 Failed + 无副作用残留 + 无 operation id）、confirm + 默认拒绝、
  confirm + AllowAll 注入后 confirmed 且任务 Completed、停机 clean。
- `tests/runtime/ipc_protocol_test.cpp`：StepView `permission` 字段
  round-trip 与缺省解码。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 构建与 `ctest`
  全绿（tsan 按本机注意事项以 `setarch` 运行）；`mirage-format-check` 通过；
  `runtime/permission` 公共头 pinned-free 边界零命中。

## 关联文档和工作项

- 设计文档第 5、12.1、15 节；[DEC-007](DEC-007-local-ipc-and-runtime-service.md)
  （协议附加字段变更记录）、[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
  （过渡边界收敛）、[DEC-009](DEC-009-provider-scope-budget-cancellation.md)
  （Provider 硬边界，与判定层叠加）。
- 工作项：`M1-06`（本决策）；确认 UI 与权限管理产品化在 M5。
