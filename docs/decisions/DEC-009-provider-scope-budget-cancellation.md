# DEC-009：M1 Provider 路径范围、执行预算与取消路径

> 状态：Accepted
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1
> 替代/被替代：无；收紧 [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
> 第 4 条过渡边界中的"范围约束与预算"部分

## 背景与问题

`M1-05` 要求为 Filesystem / Process Provider 落地路径范围约束、命令执行预算与取消
路径，并以负向用例覆盖越界访问与拒绝执行。`M1-03` 首版 Provider 的边界是：文件只读
但无范围约束（读取整个文件系统），命令执行有超时与输出预算但无命令长度预算、无取消
路径（任务取消只能等到步预算耗尽或步间边界）。三个缺口：

1. 无范围约束的读取让开发/测试拓扑也能读任意用户文件，且读取本身按文件大小无上界
   （超大文件会无界占用内存，违反 RULE-07）。
2. 命令行本身没有长度预算，超长命令会进入 fork/exec 之后才由环境失败。
3. Provider 调用是同步阻塞的，任务取消（`MiraHost::cancel_task`）无法中断一个
   在途桌面动作，只能等它的预算到期；服务停机排空同理受最长步预算拖住。

## 决策

1. **读范围（PathScope）**：desktop 层新增 pinned-free 的 `desktop::PathScope`
   值类型：构造时对声明的 roots 做 `weakly_canonical` 归一，`contains()` 对请求路径
   同样归一后做逐组件前缀匹配（symlink 感知：范围内 symlink 解析到范围外即拒绝；
   root `/a/b` 不匹配 `/a/bc`；root `/` 包含全部绝对路径；非绝对 root 不匹配绝对
   路径）。**空 scope 拒绝一切读取（fail closed）**：未声明范围的后端不暴露任何
   文件系统表面。`LinuxDesktopEnvironment` 构造函数以
   `std::vector<std::filesystem::path>` 接收读范围，默认为空。范围检查先于存在性
   检查，越界一律 `permission_denied`，消息只回显请求路径、不泄露解析目标。
   `mirage-service` 以可重复的 `--read-root PATH` 声明范围；没有 `--read-root`
   时服务禁止一切读取。
2. **读取预算**：desktop 层新增 `desktop::FileReadLimits{max_bytes}`（默认 1 MiB）。
   超预算读取 fail closed（`file_too_large`），**永不静默截断**（与 Provider 契约
   一致）；预算在读取前按 file_size 预检，并在分块读取中再检查（防止 size 检查与
   读取之间文件增长）。
3. **命令执行预算**：`ProcessLimits` 新增 `max_command_bytes`（默认 64 KiB）。
   超长命令与空命令、零预算一样在**任何副作用（fork）之前**以 `invalid_argument`
   拒绝——拒绝执行必须可验证为"未执行"。
4. **取消路径**：desktop 层新增 `desktop::CancelToken`——拷贝共享状态的协作取消
   标志（`request_cancel()` 任意线程幂等，`cancelled()` 观测），是 executor
   `StopToken` 的 pinned-free 对应物。这是分层强制的结果（RULE-01：desktop 公共
   头不得暴露 executor 类型），不是 Executor 能力缺口，不登记反馈台账；运行时层
   把自己的停止令牌/取消请求适配到它上面。两个 Provider 的纯虚接口追加
   `const CancelToken&` 参数，基类保留非虚便捷重载（经 `using` 在具体类型上恢复
   可见）。Linux 后端的执行 poll 以 25 ms 切片观察 token，取消时复用超时路径的
   整组 SIGKILL + 阻塞回收收尾，返回 `ProcessOutcome.cancelled = true` 与错误码
   `cancelled`，已捕获输出保留；文件读取按块检查 token，取消返回错误码
   `cancelled`。Runtime Service 的每个任务持有一个 CancelToken：
   `task.cancel` IPC 请求（协议 v1 扩展，见 DEC-007 变更记录）按序触发
   token → driver 停止令牌 → `MiraHost::cancel_task`，被中断步标记为
   `cancelled`（新 step 状态）、后续步 `skipped`，任务终态由 pinned cancel 结算
   （RULE-04：终态幂等，不复活）；有序停机同样先请求全部任务 token，使在途动作
   提前结束、排空有界。
5. **取消请求面的错误形态**：`task.cancel` 对未知 id 返回 `not_found`；对已终态
   任务透传 pinned 拒绝（code `pinned_runtime`、message 前缀 `invalid_state:`，
   与 `task.submit` 的既有透传形态一致），终态不因此改变。

## 备选方案

- **scope 放在 Permission 框架（M1-06）里做**：否决。Permission 是能力判定层，
  可以按任务放行；Provider 层需要一条不依赖任何判定的硬包含边界，两者叠加而非
  互替（设计文档第 15 节"资源范围"的 Provider 侧基座）。
- **desktop 接口直接接受 executor `StopToken`**：否决，违反 RULE-01 公共头
  pinned-free 约束（边界检查会失败）。
- **取消经自管道唤醒 poll**：可行但要求 CancelToken 携带 fd 或注册回调，把
  平台细节引入 desktop 原语；改为有界观察切片（25 ms），取消延迟有界且实现保持
  平台无关。完整 Linux Backend（M2）可按平台升级为即时唤醒。
- **越界读取返回 `not_found` 以隐藏存在性**：否决。调用方需要区分"范围外"
  （策略拒绝）与"不存在"（资源缺失）；消息只回显请求路径已满足脱敏要求。
- **读超预算做截断**：否决，违反 Provider"内容永不静默截断"契约；截断仅允许
  出现在服务层结构化结果的显式 `result_truncated` 标记处（DEC-007）。

## 影响与风险

- **破坏性契约变化**：`LinuxDesktopEnvironment` 默认构造从"可读全盘"变为
  "拒绝一切读取"；所有绑定它的拓扑必须显式声明读范围。M1 拓扑（服务与测试）已
  随本工作项迁移。
- Provider 纯虚签名变化是 M1 内的接口演进（`DesktopEnvironment` 访问器形态不变，
  [DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md) 形态不被
  推翻）。
- 已知限制：范围检查（canonicalize）与 open 之间存在 TOCTOU 窗口，参考后端不
  防御并发路径替换；完整 Linux Backend（M2）应使用 `openat2(RESOLVE_BENEATH)`
  类机制收敛。取消延迟以 poll 切片为上界（约 25 ms 级），非即时中断。
- 命令执行仍以 `/bin/sh -c` 运行，读范围不约束 Shell 内部访问；命令的系统级
  约束属 M1-06 Permission 判定与后续沙箱化的范围，本决策不宣称 Shell 级隔离。

## 验证方式

- `tests/desktop/provider_hardening_test.cpp`：越界（空 scope、兄弟目录、`..`
  逃逸、symlink 逃逸）、范围边界（root 即文件、逐组件前缀）、读预算边界与无
  截断、读取消、命令预算 fork 前拒绝（可验证无副作用）、执行取消的整组清理与
  输出保留、CancelToken / PathScope 单元语义。
- `tests/runtime/task_cancel_test.cpp`：运行中任务取消的有界收敛（step
  cancelled/skipped、canary 未执行、无进程残留）、未知 id `not_found`、终态任务
  拒绝且不变、取消后停机有界且 clean。
- `tests/runtime/ipc_protocol_test.cpp`：`task.cancel` / `TaskCancelled`
  round-trip 与非法变体拒绝。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 构建与 `ctest` 全绿
  （tsan 按本机注意事项以 `setarch` 运行）；`mirage-format-check` 通过；
  desktop / runtime 公共头 pinned-free 边界零命中。

## 关联文档和工作项

- 设计文档第 5、12.1、15 节；[DEC-007](DEC-007-local-ipc-and-runtime-service.md)
  （协议 v1 请求面扩展）、[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md)
  （过渡边界的收紧）。
- 工作项：`M1-05`（本决策）；`M1-06` Permission 判定将在此范围基座上叠加能力
  判定与用户确认挂点。
