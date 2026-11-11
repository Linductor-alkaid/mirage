# Mira 上游同步与私有 endpoint 同意接线（2026-11-11）

> 状态：Completed（Linux 消费范围；未验平台见供应链审计）
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-11-11

## 目标、范围与依据

按维护者要求同步 Mira 最新 master `be44e36`，基线 `68a8bf1`（2026-10-25 同步）。
升级区间 PR#112～#127：统一 Interaction Coordinator（M28/DEC-068/069）、memory
agent（PR#124-126）、subagent 委派（PR#121/122）、DEC-097/098/099/105 等 loop 与
profile 增量缝、PR#114 runtime 加固，以及嵌套 kairo v0.7.0 → v0.7.4（DEC-110）。
不修改依赖源码。

范围（维护者确认"范围 A"）：pin/lock 同步 + 兼容验证 + 接线
`ModelProfile::allow_private_endpoint`（上游 PR#118）使用户配置的本地/内网模型
endpoint 可达 + 其余新 API 出评估报告不接线。消费面兼容性论证、私有 endpoint
接线依据与网关准入预算对齐推导见
[升级审计](../supply-chain/mira-upstream-sync-20261111.md)。

## 工作项

- [x] SYNC-01：gitlink/lock 同步（mira `be44e36`、kairo `10e870b` v0.7.4），审计增量、嵌套 pin 与许可证。
- [x] SYNC-02：修复 pin 升级暴露的唯一编译破坏——`TaskSpec` 新增 `user_constraints` 字段使 `mira_host.cpp` 聚合初始化触发 `-Werror=missing-field-initializers`；改默认构造 + 赋值，Mirage 暂不填充约束（后续经 loop 输入缝交付，不改写 goal）。
- [x] SYNC-03：`build_profile` 设置 `allow_private_endpoint = true`（endpoint origin 为用户本机配置，非模型派生；`allowed_hosts` 已钉死每一跳）；附行为探针。
- [x] SYNC-04：网关默认路径 Layer 0 完整请求准入（DEC-101）的预算装配对齐——首轮独立验证发现"准入 opt-in"初判有误（网关无条件调用 `admit_model_request`），三个确定性回归（harness 紧窗口、native 对话/多轮、ctx-builder 压缩）根因是 Mirage 预算装配与新准入契约结构性冲突。修复：校准计数器（bytes/4）、profile 输出窗口一致性钳制（window−9696，floor 1024）、harness 逐轮 required_budget（测量 prefix + 2.64×工具集，推导见审计）、transformer 工具余量、对话/IME 输出钳制；fixture 按新算术重推（preflight cap 算术、compaction 32768/100 轮、tiny-window 正向断言）。
- [x] SYNC-05：消费侧验证——全量 Debug 回归、定向 ASAN/UBSAN、loopback endpoint 探针与公网 endpoint 不变性探针；由 Independent-Verification-Agent 独立执行并回报证据（首轮发现 SYNC-04 回归，修复后复验）。
- [x] SYNC-06：同步台账（MIRA-20261016-003 保持 Open 并补上游进展；MIRA-20261027-001 转 Accepted（部分交付）——上游 M28 交付 coordinator，Mirage 未接线）、供应链证据与后续工作项清单。

## 验收与限制

构建保持 pin 校验开启；全量 Debug、定向 ASAN/UBSAN、格式/层界门禁与新增探针
通过。Windows/MinGW、真实 provider 在线（含真实本地 endpoint 端到端连通）与
native 桌面验收由 Mirage 维护者在相应环境补跑，不以 Linux fixture 代替。
本轮 Mirage 侧无新增并发路径，无需新增 TSAN 路径；kairo v0.7.4 并发面证据沿用
上游 release 证据。未验项不以"已验证"名义关闭任何既有门禁。

## 验证记录

2026-11-11：基于干净 master `faa72b3` 建分支 `deps/mira-upstream-sync-20261111`；
核对已 fetch 的上游 master 与 kairo v0.7.4 tag。首轮 Debug 构建在
`mira_host.cpp` 失败（TaskSpec 聚合初始化缺新字段初始化器，`-Werror`），
即 SYNC-02 修复；修复后全量重建通过。

首轮独立验证（Independent-Verification-Agent）发现 SYNC 范围外的确定性回归：
新网关默认路径无条件执行 Layer 0 完整请求准入（推翻静态 diff 的"opt-in"初判），
harness_desktop_tools_test 15/2136、native_agent_integration_test 4/557、
desktop_context_builder_test 1 项检查失败，根因是 Mirage 预算装配与准入契约
结构性冲突（初判"opt-in"已在审计文档更正）。SYNC-04 修复后复验：

- 全量 Debug：**122/122 通过**（98.21 s，0 failed）。
- 定向 ASAN+UBSAN（组合插桩，`halt_on_error`/`detect_leaks=1`）：
  harness_desktop_tools_test（176.11 s）、native_agent_integration_test、
  desktop_context_builder_test、model_profile_endpoint_test、
  runtime_service_test、ipc_protocol_test、mira_binding_test、
  input_method_state_fixture_test、ime_text_input_test 全过，日志零
  sanitizer 匹配；插桩活性以 `nm | grep -c __asan` = 48 核验。
- 行为探针：loopback endpoint 同意（consent off → EndpointPolicyDenied，
  on → 连接级错误）与公网 endpoint 不变性断言通过（43 checks）。
- 静态门禁：`mirage-format-check`（主循环改动 3 文件空白/注释级违例由 IVA
  按仓库 .clang-format 修复，其中 model_layer.cpp 一处为注释粘连的实质缺陷）、
  `mirage-boundary-check`（67 头 0 违规）、`git diff --check` 全部退出 0。
- 交叉核验：准入常量/算术与声明一致；fixture 数独立验算吻合。

未验平台（Windows/MinGW）、真实 provider 在线（含真实本地 endpoint 端到端
连通）与 native 桌面验收由 Mirage 维护者在相应环境补跑，不以 Linux fixture
代替；本轮 Mirage 侧无新增并发路径（共享计数器为 pinned 组件自带互斥），
未新增 TSAN 范围。
