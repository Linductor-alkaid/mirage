# Linux 进程 EOF 与退出状态修复

> 状态：Completed
> 工作项：M6-28 CI 验收修复；BUG-20261007-001
> 负责人：Mirage 维护者 / Codex
> 依据：维护者授权修复 CI；沿用 Process Provider 有界执行/取消/整组清理契约。

#69 TSAN 运行中 event_subscription_test 的断开订阅后续任务未达到 Completed；日志无
ThreadSanitizer 数据竞争报告，现有断言缺少步骤错误。核对 Linux Process Provider 发现
管道 EOF 被当作进程结束并立即 SIGKILL，且关闭的 fd 仍参与 poll，可能提前耗尽双流计数。
在 Platform Backend 内以独立 fd 生命周期和公开 waitid(WNOWAIT) 观察实际子进程退出；
保留总预算、协作取消、进程组清理及最终 waitpid 回收。不新增线程、定时器或队列。

- [x] 先用提前关闭双流、单流分阶段关闭的命令复现，再验证退出码/尾部输出和预算。
- [x] 验证超时、执行中取消、整组清理与 IPC 订阅断开后继续接纳任务；补齐失败诊断。
- [x] Debug/ASAN/UBSAN/TSAN 与完整 CI 成功后合并；没有通过的检查保持未完成。

## 复现与首轮修复证据

Release 原实现新增回归 100 checks / 2 failures：双流关闭后退出 7 被终止、双流关闭后
长命令没有报告 deadline_exceeded。修复后 provider_hardening_test 与 event_subscription_test
串行 2/2 成功（额外覆盖正常退出后的背景子进程清理与双流关闭后执行中取消），
format/boundary 成功。原失败日志为 #69 run 37509956033 / job 112428216514；无 TSAN race 报告。
CI 未打印步骤错误，不能仅凭该旧日志断言失败原因；补充任务终态、步骤状态/退出码/错误诊断，
保留原 Completed 断言，不放宽预算或用重试掩盖失败。

Debug/ASAN/UBSAN/TSAN 同两项各重复三次，四种 preset 各六次均成功。
[PR#69](https://github.com/Linductor-alkaid/mirage/pull/69) 精确验收 head 为
`9584cf1b8596026747e0cd6f7484ef6095714bae`，八项 CI 全绿后合并为
`35ffa923adf89f3a740ce8a283229a9619a5ce9b`。
完整组合 [PR#73](https://github.com/Linductor-alkaid/mirage/pull/73) 八项 CI 全绿后进入
master（`2d0e409af12c714627beb6cf0730a38ee03ce629`）。登记入口的 native-release
最终五项回归 5/5 成功，包含进程与事件订阅两项。详见
[验收记录](maintenance-ci-acceptance-20261007.md) 与
[逐项 CI 证据](../compatibility/ci-acceptance-20261007.json)。
