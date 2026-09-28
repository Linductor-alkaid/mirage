# DEC-028：M5-07 批准中心与权限策略面（policy.get/set 与 DEC-011 扩展）

> 状态：Accepted
> 日期：2026-09-28
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-07`：批准中心与权限管理产品化）
> 替代/被替代：无；本记录是 [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的**附加扩展**（协议面），并按 M5 退出条件完成 [DEC-010](DEC-010-m1-permission-framework.md)
> 与 [DEC-011](DEC-011-m1-local-state-persistence.md) 的演进修订（规则矩阵从
> M1 三能力扩展到全 DEC-010 词表；settings 默认拾取翻转）。wire 契约
> `meta.version` 8 → 9（golden vectors 双端门禁同一文件）

## 背景与问题

`M5-03` 交付了 `permission.*` 异步确认面（契约 + 服务承载 + TS 镜像），
UI 接线明确属 `M5-07`。动工前的结构性事实：

1. **TS 传输缺口**：M5-03 只交付了 types/codec 镜像，`MirageTransport` 接
   口从未暴露 `permission.list` / `permission.respond` ——批准中心没有任何
   消费路径；`permission.request` 事件也无人订阅。
2. **策略面缺位**：DEC-010 规则只在 start() 时从配置/旗标装配，运行中的规
   则集没有读取与变更面；M1 设置文档的 permission 块只有三个具名能力（M1
   Provider 词表），与 DEC-010 全词表（11 能力）不匹配。
3. **持久化默认行为**：DEC-011 明文"M1 仅显式加载、不回写；默认拾取 M5 翻
   转"——M5-07 的策略持久化兑现这两项翻转。

## 决策

1. **批准中心 = 快照 + 请求事件 + respond**（无新 wire 面）：pending 快照
   事实源是 `permission.list`（timeout_ms 为剩余预算），`permission.request`
   事件是通知（触发快照重取），响应走 `permission.respond`（先到先得；
   `not_found` 已决/已超时静默收敛）。UI 呈现能力 / 资源（DEC-020 决策 8
   披露边界）/ 任务 / 剩余秒数与批准 / 拒绝操作。
2. **策略面（协议 v1 附加扩展）**：`policy.get` 返回 `PolicyView`
   {rules（全 DEC-010 集，capability → allow/confirm/deny）, read_roots}；
   `policy.set` 要求全量覆盖（缺失能力 `invalid_argument`），规则**立即生
   效**（PermissionController 与任务驱动、atom toolset 门共享），read_roots
   随下轮启动生效（绑定 provider 的 PathScope 不支持热更——如实声明）；
   合并文档持久化到 settings store（DEC-011 Desktop Permissions 条目）。
   hello 新增 `policy` 能力位（策略面为核心装备，恒 true）。
3. **规则持久化 = settings 读-改-写**：policy.set 加载现存文档（缺席按默
   认；decode 失败或读失败拒绝写入——DEC-011 用户权威输入 fail-closed 不
   对称姿态），写入规则集与 read_roots，保留 socket/confirmation 等其他成
   员。settings 文档 `permission` 块从三个具名可选成员扩展为全 DEC-010 词
   表 map（键序确定编码），schema 保持 1（既有成员零变更，加法演进），
   DEC-011 修订登记。
4. **DEC-011 默认行为翻转（M5 兑现）**：apps/service 无 `--config` 时默认
   拾取 `default_config_directory()/service.json`（存在才读；损坏 fail
   closed）——policy.set 的写回由此跨重启生效。CLI 旗标逐项覆盖的优先级
   不变；测试通过 `settings_directory` 重定向或 `persist_settings=false`
   隔离。
5. **默认策略收紧留观**：DEC-010 的 M1 默认值（read/execute 等 Allow、
   filesystem.write Deny）维持不变——默认值变更会静默改变既有任务流行为，
   其收紧随 M5-08 产品化规模复核与真实使用数据定，本增量只提供变更面。

## 备选方案

- **复用 `permission.list`/`respond` 携带策略**：否决。确认快照与规则集是
  两个正交面（瞬时 vs 持久），混载会让 golden 向量与解码端都变复杂。
- **policy.set 支持部分覆盖**：否决。全量覆盖让"缺 capability"成为显式错
  误（`invalid_argument`），UI 总是持有完整状态（policy.get 回显），部分
  语义只会制造静默默认回落的歧义。
- **read_roots 热更到 provider**：否决。PathScope 绑定在 provider 构造
  （DEC-009），热更需要新的 provider 可变面；资源范围重启生效已如实声明
  于 UI 与 wire 文档。
- **settings schema 递增到 2**：否决。既有成员（schema/socket/read_roots/
  confirmation）零变更，`permission` 块的键集按加法演进扩展——旧文档仍可
  解码（新键缺席 = 默认），不构成 DEC-011 的"不兼容演进"。

## 影响与风险

- golden vectors `meta.version` 8 → 9；既有解码路径不改，零回归。
- `PermissionController` 增 `policy_mutex_`：authorize（驱动线程并发）与
  set_policy（serial context）互斥；authorize 改经加锁快照判定。
- policy.set 的持久化路径在 serial context 做文件 IO（读-改-写，64 KiB 预
  算内）——单次小文档写入，服从 DEC-011 原子保存纪律。
- 默认拾取 service.json 使开发拓扑多了一个行为来源（文档损坏 fail closed
  启动失败，DEC-011 姿态）；CLI 旗标仍逐项覆盖。
- UI：批准中心与设置矩阵依赖 `permissions` / `policy` 能力位，缺席如实降
  级（hub 未配置 / 旧服务）。

## 验证方式

- 协议测试：两请求编解码 round-trip、全量覆盖与词表校验失败路径、
  read_roots 边界、hello `policy` 能力位（有 / 无）。
- Golden vectors 双端门禁：v9 向量（requests +2、request_failures +4、
  responses +2 + hello 能力位、response_failures +3）双端逐字节一致。
- 服务测试：`policy_face_get_set_persists`——get 默认集、set 收紧 +
  read_roots 变更、持久化文档回读、部分覆盖拒绝；persistence_test 规则
  map round-trip 与新词表负向。
- 前端：mock 能力位 / 降级、批准中心空态与响应流、设置矩阵；`npm run
  check` / `npm test` / `npm run lint` / `npm run build` 全绿。

## 关联文档和工作项

- [DEC-010](DEC-010-m1-permission-framework.md)（词表与框架演进）、
  [DEC-011](DEC-011-m1-local-state-persistence.md)（settings 扩展与默认拾
  取翻转）、[DEC-020](DEC-020-permission-async-confirmation.md)（异步确认
  面语义）、[DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
  （附加扩展流程）。
- 工作项：`M5-07`（本决策）；消费方：批准中心（Overlays / WallDisplay）、
  设置页权限矩阵。
