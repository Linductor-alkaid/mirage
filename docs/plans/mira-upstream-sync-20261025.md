# Mira 上游同步与台账消费（2026-10-25）

> 状态：Completed（Linux 消费范围；未验平台见审计）
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-25

## 目标、范围与依据

按维护者要求同步 Mira 最新 master `68a8bf1`，基线 `a29374a`。
依据 [DEC-112](../decisions/DEC-112-output-limit-failure-category.md)、上游
DEC-065/066/067 与公开 ConversationLoop / memory_embedding 契约；不修改依赖源码。
复用结构化终态原因，保留既有 IPC 类别与 Executor owner/任务路径。
记忆能力只做公开 API 与职责确认，不在此次开启 M10 产品记忆或引入 embedding 模型。

## 工作项

- [x] MNT-MIRA-01：gitlink/lock 同步，审计增量、嵌套 pin 与许可证。
- [x] MNT-MIRA-02：harness 按终态原因精确识别 MaxOutputTokens，移除 summary 锚点。
- [x] MNT-MIRA-03：消费侧验证截断耗尽、恢复成功、其它 incomplete、过滤/拒绝与取消；执行 Linux 回归和 sanitizer。
- [x] MNT-MIRA-04：同步台账、DEC-112、记忆前置清单与供应链证据，未交付缺口保持开放。
- [x] MNT-MIRA-05：按维护者后续要求修复基线 IPC 格式问题，全仓格式与层界门禁通过。

## 验收与限制

构建保持 pin 校验开启；全量 Debug、相关 ASAN/UBSAN、变更文件格式/层界检查与上游
终态契约探针通过。Windows、真实 provider 在线与 native 桌面验收由 Mirage 维护者
在相应环境补跑，不以 Linux fixture 代替。此次不新增并发路径，无需新增 TSAN 路径。

## 验证记录

2026-10-25：从当前开发分支提交 `2af42c5` 创建独立工作树及
`build/mira-upstream-sync`，不包含主工作区尚未提交的输入法/连续感知变更。
核对已 fetch 的上游 master；本区间没有 core_contracts、model_supervisor、kairo 或
PNG 编码缝变更，因此相应未解决条目不关闭。


2026-10-25 收口：Debug 全量 110/110（116.56 s）、ASAN+UBSAN 定向 5/5
（含三项行为测试；110.07 s）、链接本次新库的四组上游终态/持续循环/方言契约
探针全部通过；公开层界 66 个头、0 违规，变更文件格式/文档链接与 pin 校验通过。
MIRA-20261022-001 转 Resolved，MIRA-20261016-001 为 Accepted（部分交付）。
全仓格式检查受基线 protocol.cpp:3121/3123 既有换行阻塞，未当作通过；
未验平台与负责人/补跑条件见[升级审计](../supply-chain/mira-upstream-sync-20261025.md)。


2026-10-25 后续（MNT-MIRA-05）：按维护者“修复基线问题”要求，对
`runtime/ipc/src/protocol.cpp` 两处及全仓门禁继续发现的
`tests/runtime/ipc_protocol_test.cpp` 三处进行 clang-format 调整。
逐文件与 HEAD 去空白比较一致，无行为变化，未新增或重跑行为测试。
`cmake --build build/debug --target mirage-format-check mirage-boundary-check`
退出 0：全仓格式通过，66 个公开头文件层界零违规；`git diff --check` 通过。
前述全仓格式阻塞至此解除，原失败记录保留。Windows/真实模型等限制不变。
