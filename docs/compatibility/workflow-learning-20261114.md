# Workflow 默认学习、长期元数据与在线验收（2026-11-14）

> 状态：Completed（Linux 元数据与受控在线链路）；回归收口：2026-11-16
> 负责人：Mirage/Mira 维护者
> 工作项：M15-16/17；决策：DEC-145/146、Mira DEC-111/112

## 基线与产品行为

Mirage 分支 feat/supervised-rpa-experience，基线 8f05a5b 之后的 M15-16～18 增量。
维护者明确要求默认学习开启，并授权补齐 Mira 与升级 pin。Mira 从 be44e36 升至
2c42627a23da83c6d04825c28163836f9f747ec3，功能分支已推送；嵌套 kairo 与其他依赖不变。

Runtime Service 默认打开 state/workflow-learning/memory.sqlite，开启 Workflow
Episode 学习；成功恢复 Lesson 仍由受信任宿主显式记录。使用当前 OS 用户的私有目录
和固定 Agent scope mirage.workflow.local-user.v1，允许同主体跨 Session 召回。直接
Host 省略 binding 仍不装配学习，默认 binding 仍是 primary Session scope。

数据库、WAL、SHM 和 rollback journal 文件长度合计硬限额 64 MiB，其中 4 MiB 维护
预留；普通新写入不能消耗预留，幂等重放先读原账本。超限拒绝不删除旧库、不悄悄改用
内存库。关闭先排空 Workflow，再释放实际 Store，最后关闭 Executor。服务启动失败、
显式关闭与保留用户关闭设置均有测试。配置关闭方式：

```json
{"schema":1,"runtime":{"workflow_learning_enabled":false}}
```

这个限额不是文件系统块/目录元数据的物理配额，也不包含 Event/ArtifactStore。当前
默认记录元数据；视觉工件长期采集、跨层回收、自动归档与完整 M31 继续开放。

## 在线证据

使用当前配置的 deepseek-flash、anthropic.messages.v1，凭据仅在传输边界从钥匙环
读取。专用 Workflow ModelLayer 模式复用原 Gateway/provider/传输，拒绝普通聊天并用；
采用显式 Text 协议、同源本地 JSON schema/决策/补丁校验、无工具授权、关闭思考、单次
30s 期限与 2048 输出 token。只投影合成任务的明确白名单，用户文件和桌面不参与。

| 最终轮次 | 真实结果 | 输入/输出 token | Lesson |
| --- | --- | --- | --- |
| 1 | patched-and-resumed，实际文件 atom 成功、Run 完成 | 742 / 163 | 显式记录成功 |
| 2 | patched-and-resumed，实际文件 atom 成功、Run 完成 | 1076 / 200 | 模型上下文携带 1 条历史 Lesson，再记录成功 |

两次总计输入 1818 / 输出 363 token，约 3319ms；是两条受控文件任务的链路数据，不是
模型泛化、成功率提升、产品成本收益或真实桌面性能基准。另一独立进程随后打开同库，
按确定性 ID 读回并核对 2 条 Episode、2 条 Lesson；当时文件长度为 110592 字节，运行
期间快照为 337848 字节，均小于 64 MiB。长度变化不作为物理块回收证明。

正式数据：[脱敏 JSON](data/workflow-learning-online-20261114.json)。无 API key、请求/
回复正文或用户文件内容。程序见
[在线驱动](../../tests/manual/workflow_learning_online_acceptance.cpp)，仅手动开启
MIRAGE_BUILD_MANUAL_ACCEPTANCE 构建，不加入 CTest，不让 CI 发送付费请求。

## 保留的失败轮次

开发与最终验收共 6 次恢复尝试，最终 2 次成功；未将中途失败改写为通过。

| 开发轮次 | 结果与处置 |
| --- | --- |
| gpt-6.1-sol 首轮 | 约 5s 后 model-unavailable；验收入口误用 Host 5s 默认，改为显式 30s。该轮未记录 provider 调用数/usage，不能补作零值 |
| gpt-6.1-sol 第二轮 | 收到 headers，返回 InvalidModelRequest（domain_code=4）；具体远端字段原因未确认，未宣称修复了远端严格 schema 支持 |
| deepseek-flash 首轮 Text | 收到真实回复；新接线未显式调用本地 schema parser，返回 decision-invalid。补齐同源 parser 并通过离线合法/坏 JSON 矩阵 |
| deepseek-flash 第二轮 Text | decision-invalid；未保留原始回复，无法逐字归因。补明确的原始 step ID 路径和实参对象契约后，最终两轮通过 |

已仪表化的 provider 调用计数为 5；首轮未仪表化，不推算缺失 token 或费用。没有自动
切换协议或盲重试；每个 Run 保留单次恢复限制，线上每次尝试内不修复。

## 回归与范围

Ubuntu 24.04.5 x86_64、GCC 13.3.0、CMake 3.28.3/Ninja、clang-format 18.1.8；
fetch OFF、无 ccache，非 native/OCR。新 pin 最终构建/测试命令和逐配置结果在
[M15 计划](../plans/m15-supervised-rpa-experience.md) 记录；最终 Debug/ASAN/UBSAN/TSAN 各 **15/15 通过、0 skip**，无 sanitizer
报告，恢复额外连续 20 次通过。格式、实际架构扫描、边界、文档与 SBOM 检查通过。Mira 最终 pin 的
[分支 CI](https://github.com/Linductor-alkaid/mira/actions/runs/43784628663) 12/12 job 通过
（Linux GCC/Clang Debug/Release、Windows Debug/Release、Android 两 ABI、三个 sanitizer
及 quality），不等同于 Mirage Windows 产品设备验收。

保留首次 CI 的 Windows open 失败及第二次 CI 的架构检查失败：前者由 232c9d1
统一原生 VFS 文件名规范化修正，后者由 2c42627 修正按绝对路径过滤嵌入式仓库的问题。
本地原先“架构通过/全部 stale”的结果为漏扫，已撤销作为有效证据；修正后实际扫描
16 项既有债务、0 stale、0 新违规，恢复原有 sqlite_memory_store 文件规模基线，
没有提高预算或新增豁免。

在线请求发生于 2026-11-14，之后只改动 VFS 路径/检查器及恢复准入结算边界；模型协议
与生成代码未改变。最终 pin 重建验收入口并在另一进程离线重开同库核对通过，未追加
付费模型请求。完整 Mirage 四配置结果和开发期回归失败见 M15-17。

本轮在线范围为受控 Host + 生产 ModelLayer/真实模型，不包含原生界面自动触发、真实
桌面布局变化/接管、视觉截图长期存储、真实断电/磁盘故障、Windows 设备验收或完整
跨 store 配额。上述项由 Mira/Mirage 维护者在公开接口、生命周期与目标设备就绪后补跑。
