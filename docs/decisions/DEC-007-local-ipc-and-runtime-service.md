# DEC-007：Local IPC 机制与 Runtime Service 进程形态

> 状态：Accepted
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1（传输与帧格式自 M1-04 起冻结；M1 请求面随里程碑演进）
> 替代/被替代：无

## 背景与问题

`M1-04` 要求 Runtime Service 独立于 GUI 生命周期运行，CLI 经 IPC 完成
`task list` / `task submit` / `task inspect`（设计文档第 12 节）。Local IPC 机制在此前
只有暂定候选（Unix domain socket / 命名管道），未定案前 `M1-04` 不进入实现。需要决定：

1. 每个平台的本地传输载体，以及跨平台如何在同一契约下演进（Linux 先行，Windows 属
   M4）。
2. 帧格式与编码：长度前缀、容量上限、错误如何呈现。
3. M1 的请求面：CLI 需要的最小命令集，以及在没有模型驱动 Agent 循环的前提下
   （[DEC-008](DEC-008-m1-environment-binding-and-reference-providers.md) 第 1 条），
   提交的任务如何被 Service 内的宿主侧驱动循环确定性推进。
4. Service 的进程形态：谁托管 `MiraRuntime`、socket 文件的所有权与陈旧恢复、
   GUI/CLI 与 Service 的生命周期解耦边界。

## 决策

1. **传输载体**：Linux 使用 Unix domain socket（`AF_UNIX`、`SOCK_STREAM`），默认路径
   `$XDG_RUNTIME_DIR/mirage/mirage-service.sock`，`XDG_RUNTIME_DIR` 未设置时回退
   `/tmp/mirage-<uid>/mirage-service.sock`；socket 所在目录以 `0700` 创建，传输的访问
   控制依赖目录权限（同 uid 才可连接）。Windows（M4）使用同名命名管道
   （`\\.\pipe\mirage-service`）实现同一契约；两平台共享
   `runtime/ipc` 的帧格式、协议与消息语义，传输实现按平台分编译单元。
2. **帧格式**：每条消息为 4 字节小端无符号长度前缀 + UTF-8 JSON 载荷；载荷上限
   1 MiB，超限视为协议错误并关闭连接。一条连接同一时刻至多一个未决请求：客户端发出
   请求后必须等到响应才能发下一条，服务端在此期间不读取该连接。连接可以按此方式
   顺序复用多条请求。
3. **编码**：JSON，使用 pinned mira 自带的 `mira::JsonValue` /
   `parse_json` / `to_json_string`（严格 RFC 8259 子集，带深度与尺寸限制）。JSON 只
   出现在 `runtime/ipc`、`runtime/service` 的实现文件内；协议消息以 pinned-free 的
   C++ 结构定义在 `runtime/ipc` 公共头，公共 API 不暴露第三方类型。协议携带
   `protocol` 版本号（当前为 1），不兼容升级时递增。
4. **M1 请求面**：`hello`（服务与版本信息）、`task.submit`（goal + 可选有序
   steps）、`task.list`、`task.inspect`、`service.shutdown`。每个请求带客户端相关性
   `id`，响应原样回带；错误以稳定错误码
   （`protocol_error` / `unsupported` / `invalid_argument` / `not_found` /
   `invalid_state` / `unavailable` / `internal`）+ 安全消息呈现。
5. **M1 任务推进（DEC-008 过渡边界的 Service 形态）**：`task.submit` 除自由文本
   goal 外接受有序 `steps`，每个 step 是 M1 桌面能力之一——
   `filesystem.read`（`path`）或 `process.execute`（`command`）。Service 内的宿主侧
   驱动循环逐个执行 step：每个动作以 `MiraHost::begin_operation` /
   `admit_operation_completion` 括起进入 pinned 控制面，Provider 返回值即结构化结果，
   记录 step 的 operation id（Trace 关联）；全部 step 成功则 `complete_task(true)`，
   任一 step 失败即 fail-fast `complete_task(false)`。steps 缺省为空时任务仅入控制面、
   由提交方驱动。这是无模型循环阶段的确定性推进方式；pinned 上游提供宿主环境工具表面
   后随 DEC-008 第 2 条一起迁移，不新增稳定脚本契约承诺。
6. **Service 进程形态**：Runtime Service 由独立可执行文件 `apps/service`
   （`mirage-service`）托管，前台运行直至被要求关闭（`service.shutdown` 请求或
   SIGINT/SIGTERM）；`mirage service start` 通过 fork + exec 兄弟二进制方式拉起并
   等待就绪。socket 文件由 Service 创建与删除：启动时若路径已存在，先探测连接，
   确认无活服务（`ECONNREFUSED`）才允许接管（删除后重建），有活服务则启动失败；
   关闭时删除 socket 文件。GUI、Tray、CLI 只经 Local IPC 与 Service 交互
   （DEC-006），不进入 Service 进程依赖。
7. **并发承载（Executor 路由）**：Service 进程内由 RuntimeService 持有本进程唯一的
   `executor::Executor` 实例（总计划 `EXEC-01`）。连接事件循环以 Executor 的专属
   blocking I/O worker 承载（poll + wakeup 可中断）；请求 handler 与全部
   `MiraHost` 操作经 `SerialExecutionContext` 串行化（宿主单线程所有权纪律，M1-02）
   ；任务驱动循环以 `submit_cancellable` 承载，step 间检查停止令牌；事件回投使用
   `executor::comm::MpscChannel`。不引入 `std::thread` / 自建队列。

## 备选方案

- **TCP localhost 端口**：否决。端口占用与协商复杂，访问控制弱于文件系统权限，
  且与本机 IPC 的语义不符。
- **抽象 socket / autolaunch（DBus 式）**：否决。引入总线依赖与生命周期魔法，
  M1 需要的是显式、可测试的直连通道。
- **Protobuf / Flatbuffer 等独立编码**：否决。新增直接依赖违反依赖锁定基线
  （工程规范第 9.1 节），pinned mira 的 JSON 模型已满足带限制的严格解析与紧凑序列化。
- ** 自由文本 goal 由 Service 解释执行**：否决。没有模型循环时对自然语言做隐式解释
  既不确定也不可测试；显式 steps 把"计划"作为输入，与未来模型循环产出的计划同构。
- **Service 内嵌于 CLI 进程（`mirage service start` 同进程服务）**：否决。任务必须
  脱离发起进程生命周期运行（设计文档第 12 节），独立进程是唯一满足形态。

## 影响与风险

- 新增 `runtime/ipc` 目标与 `apps/service` 可执行文件；设计文档第 12、17 节同步。
- 帧格式与协议 v1 自 M1-04 起对 CLI/Tray/GUI 是兼容性承诺；演进走 `protocol`
  版本号。
- steps 是过渡性输入形态：它让任务可确定性验收，但不承诺成为产品脚本语言；M1 结束
  时若 pinned 工具表面仍缺位，随 DEC-008 复核去留。
- socket 目录权限是 M1 的全部访问控制；peer credentials 校验（`SO_PEERCRED`）与
  多用户隔离留待产品化里程碑（M5）复核。
- Service 关闭时在途 step 以 Provider 预算为界（`ProcessLimits.timeout`，默认 30s），
  排空上限受其约束；取消路径硬化随 M1-05 收紧。

## 验证方式

- `tests/runtime` 协议测试：帧编解码（含超限拒绝、半包/粘包）、请求/响应编解码
  round-trip、非法 JSON / 未知 op / 版本不匹配的稳定错误码。
- `tests/runtime` 服务测试：hello / submit / list / inspect 全链路、steps 驱动的
  结构化结果与 operation id 关联、fail-fast、并发多连接顺序复用、
  `service.shutdown` 有序关闭、陈旧 socket 接管。
- 真实进程端到端冒烟：`mirage-service` + `mirage task submit/list/inspect`（M1 退出
  条款中的 Filesystem + Shell 任务）。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 构建与 `ctest` 全绿；
  公共头 pinned-free 边界零命中。

## 变更记录

- 2026-09-16（`M1-05`，[DEC-009](DEC-009-provider-scope-budget-cancellation.md)
  落地）：协议 v1 请求面新增 `task.cancel`（`CancelTaskRequest` 请求与
  `TaskCancelled` 响应载荷），`task.inspect` 的 step 状态集新增 `cancelled`。
  传输、帧格式、载荷上限与单连接单请求纪律不变；任务取消的语义（desktop
  cancel token → 驱动停止令牌 → pinned cancel 的顺序、终态幂等）见 DEC-009
  第 4、5 条。
- 2026-09-16（`M1-06`，[DEC-010](DEC-010-m1-permission-framework.md) 落地）：
  `task.inspect` 的 `StepView` 新增附加字段 `permission`（稳定决策字符串
  `allowed` / `confirmed` / `denied` / `confirmation_rejected`，未判定为空），
  编码端总是写出、解码端缺省为空（向后兼容）。传输、帧格式、其余请求面与
  单连接单请求纪律不变；权限判定发生在动作副作用前，被拒步不产生
  operation id。

## 关联文档和工作项

- 设计文档第 12（后台运行）、17（推荐代码结构）节；
  [DEC-006](DEC-006-ui-web-frontend-packaging.md)（UI 仅经 Local IPC 耦合）。
- 工作项：`M1-04`（本决策）；`M1-05`（step 预算与取消路径收紧）、`M1-06`
  （Permission 判定接入请求面）在此边界内收紧。
- pinned 依据：`third_party/mira/include/mira/json.hpp`（解析限制与严格子集）、
  `third_party/mira/third_party/executor/include/executor/blocking_io.hpp` 与
  `serial_execution_context.hpp`（事件循环与串行化承载）。
