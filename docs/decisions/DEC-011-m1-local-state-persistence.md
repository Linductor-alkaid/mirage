# DEC-011：M1 本地状态持久化骨架（配置与 Runtime Recovery State）

> 状态：Accepted
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 冻结里程碑：M1（存储位置解析、文件格式 schema v1 与容量预算自 M1-07 起冻结）
> 替代/被替代：无

## 背景与问题

设计文档第 16 节将 Mirage 自身持久化状态划分为 Application Settings、Runtime
Configuration、Desktop Permissions、Runtime Recovery State 等条目，并要求 Agent
Memory、Workflow 等 Mira 数据模型继续经 Mira 接口访问。`M1-07` 要求落地其中 M1 相关
子集的存取骨架。此前 Runtime Service 的任务注册表纯内存（`task_registry.hpp` 自注
"persistence is M1-07"），服务重启后历史任务不可观察；服务配置只能经命令行旗标表达，
没有本地配置入口。需要决定：

1. M1 子集具体包含哪些状态，各自落在什么文件、什么目录。
2. 文件格式与编码边界（如何复用 pinned mira 的 JSON 模型而不违反 RULE-01）。
3. 原子性与容量纪律（RULE-07）；损坏文件如何呈现。
4. 配置文件与命令行旗标的优先级，以及恢复状态的注水（hydration）语义。

## 决策

1. **M1 子集与文件布局**：只覆盖两类状态，均属 Mirage 自有产品状态（第 16 节划分，
   与 Mira 数据模型零重复）：
   - **本地配置**（`LocalSettings`，对应 Application Settings / Runtime
     Configuration / Desktop Permissions 的 M1 可配置面）：IPC endpoint 覆盖、
     Filesystem 读范围（DEC-009）、逐能力 Permission 规则与确认挂点结果（DEC-010）。
     文件名 `service.json`，目录为 `$XDG_CONFIG_HOME/mirage`（未设置时
     `$HOME/.config/mirage`）。M1 仅显式加载（`mirage-service --config PATH`），
     不做隐式默认拾取；默认路径解析函数随模块提供，M5 产品化时再翻转默认行为。
     配置是只读输入，M1 服务不回写配置文件。
   - **Runtime Recovery State**（`RecoveryState`）：已结算任务记录（id、goal、终态
     progress、success、逐步 status/operation id/permission/结构化结果摘要）。文件名
     `task-recovery.json`，目录为 `$XDG_STATE_HOME/mirage`（未设置时
     `$HOME/.local/state/mirage`）。默认启用，`RuntimeService` 经 `ServiceConfig`
     可换目录或关闭。在途任务不落盘：有序停机路径会先取消结算在途任务，故其终态
     仍被记录；进程被强杀时的在途任务丢失是 M1 已知限制。
2. **格式与编码边界**：两文件均为 JSON 文档，顶层携带 `schema` 版本号（当前为 1，
   不兼容演进递增；解码端对未知 schema fail closed）。编码复用 pinned mira 的
   `mira::JsonValue` / `parse_json` / `to_json_string`（DEC-007 第 3 条同款边界）：
   JSON 只出现在 `runtime/persistence` 的实现文件内，公共 API 保持 pinned-free 纯
   std 类型。解码严格：未知成员、类型错误、枚举值超出冻结词表（step kind/status、
   permission 决策、progress 终态名）均拒绝。
3. **存储原语**：`runtime/persistence::LocalStateStore` 封装"目录 + 单文件 + 容量
   预算"的存取。保存为原子且尽力持久：临时文件（`0600`、`O_EXCL`）写入 → fsync →
   `rename` 覆盖 → 目录 fsync；目录以 `0700` 递归创建。读取按预算截断拒绝（超出
   预算报 `too_large`，不截断内容）。预算：settings 64 KiB，recovery 4 MiB（与
   pinned `parse_json` 默认文档上限一致，recovery 超预算时保存失败并对使用者可见，
   不静默截断）。POSIX 实现位于 `store_posix.cpp`（`runtime/ipc` 的
   `stream_posix.cpp` 同款平台分文件形态），Windows 属 M4。
4. **恢复语义**：任务驱动结算（`mark_driver_done`）与有序停机排空后各写一次全量
   快照（快照在注册表锁内拷贝、写盘在专用 writer 互斥锁下串行，最后一次写入者持有
   最新完整状态）。启动注水：仅终态任务以 `driver_done` + 记录内 progress 注入注册
   表；注水后的任务不再有 pinned 实例对应物，`task list` / `task.inspect` 直接呈现
   记录内状态，`task.cancel` 以 `invalid_state` 明确拒绝（任务属先前服务纪元且已终
   态）。注水数量受注册表容量约束，超出的最后写入条目放弃并计数报告。损坏的
   recovery 文件不阻断服务启动：记录显式错误（stderr）后按无恢复继续，绝不自动删除
   用户状态；损坏的配置文件则 fail closed（启动失败）——配置是用户权威输入，恢复状
   态是服务自有缓存，两者错误呈现不对称是刻意选择。
5. **配置优先级**：`--config` 文件提供基线，命令行旗标逐项覆盖（socket 覆盖、
   `--read-root` 出现任意一次即整体覆盖文件读范围、`--perm` 按能力覆盖、
   `--confirm` 覆盖）。文件加载失败（不存在按默认、解析/校验失败报错退出）。

## 备选方案

- **SQLite / 二进制格式**：否决。新增直接依赖违反依赖锁定基线（工程规范第 9.1 节）；
  M1 状态面小且整体快照即可，pinned mira 的 JSON 模型已带限制的严格解析与紧凑序列化。
- **隐式加载默认配置文件**：否决（M1）。开发拓扑中静默改变服务行为的全局文件是意外
  源；DEC-007 拉起链路（fork + exec 转发旗标）已为显式配置留出通道，默认拾取延后到
  M5 与产品设置面一起定案。
- **逐条追加任务日志（append log + 重放）**：否决。恢复语义只需终态记录，全量快照
  写路径单一、损坏面小、无需重放协议；追加日志的崩溃一致性复杂度与 M1 收益不匹配。
- **恢复文件中记录在途任务并标记 interrupted**：否决（M1）。无模型循环阶段任务由
  提交方 steps 完全定义，服务重启后在途任务不可续驱，记录"interrupted"只增加呈现
  负担；有序停机已把在途任务结算为 Cancelled 后落盘。
- **配置加载下沉进 RuntimeService**：否决。配置到 `ServiceConfig` 的映射是产品装配
  关注点，留在 `apps/service`；RuntimeService 只消费已解析的配置值，保持库面与装配
  面分离。

## 影响与风险

- 新增 `runtime/persistence` 目标；`runtime/service`、`apps/service`、`apps/cli`
  接线；设计文档第 16 节同步 M1 落地形态；`DEC-007` 请求面不变，仅新增
  `step_kind_from_name` 解析辅助（协议面无 wire 变化）。
- `ServiceConfig` 新增 `recovery_directory` / `persist_recovery_state`：默认开启使
  测试必须显式隔离恢复目录，否则用例间经默认状态目录串扰。
- recovery 文件预算（4 MiB）在病态大结果负载下可被写满：保存失败显式可见
  （`last_error` + stderr），不截断不静默丢弃；预算调优随真实负载在 M2+ 复核。
- XDG 目录解析依赖环境约定：`HOME` 与 `XDG_*` 未设置时的回退次序在本决策冻结；
  Windows 目录策略属 M4。
- peer 泄露面：recovery 记录含任务 goal 与结果摘要，文件由 `0700` 目录与 `0600`
  文件权限保护；结果摘要本身已在协议面按 UI 安全裁剪，无新增敏感面。

## 验证方式

- `tests/runtime/persistence_test.cpp`：store 原子保存/加载/absent/too_large、
  目录权限、settings 与 recovery 的编解码 round-trip 与负向（schema 不符、未知成
  员、越界枚举、超预算）。
- `tests/runtime/recovery_service_test.cpp`（集成）：结算落盘、重启注水、
  list/inspect/cancel 对注水任务的语义、损坏文件降级路径、关闭持久化的隔离性。
- 真实进程端到端：`service start` → 提交 → 完成 → `service shutdown` → 重启 →
  `task list` / `task inspect` 仍可见历史任务。
- 预设矩阵 `debug` / `release` / `asan` / `ubsan` / `tsan` 全绿；公共头
  pinned-free 边界零命中。

## 关联文档和工作项

- 设计文档第 16（本地状态与持久化）、12.1（M1 落地形态）节；
  [DEC-007](DEC-007-local-ipc-and-runtime-service.md)（编码与平台分文件先例）、
  [DEC-009](DEC-009-provider-scope-budget-cancellation.md)（读范围）、
  [DEC-010](DEC-010-m1-permission-framework.md)（Permission 词表与确认挂点）。
- 工作项：`M1-07`（本决策）。
- pinned 依据：`third_party/mira/include/mira/json.hpp`（解析限制与严格子集）。
