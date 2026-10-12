# 模型网络阶段超时可重试与 deadline 放宽（2026-10-12）

> 状态：Completed（上游 PR#88 合并消费 + Mirage 侧 deadline/恢复预算调整与全量回归）
> 负责人：Mirage 维护者
> 工作项：M6-39 验收跟进
> 依据：[DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)、
> [MIRA-20261012-001](../dependency_feedback/mira.md#mira-20261012-001)

## 背景与失败分析

DeepSeek 修复包验收会话（2026-10-12 09:30，见
[deepseek-package-20261012](deepseek-package-20261012.md)）中，桌面任务在第 6 次模型调用
失败，turn 落盘 `model call failed: exchange stage deadline exceeded`。会话数据
（`~/.local/state/mirage/session-state.json`，会话 `80612890…`）显示：前 5 轮带图请求
全部成功，失败发生在上次工具结果后约 10.2s，与 pinned mira 传输层的 TLS/write 阶段
10s deadline 吻合——即到 `api.deepseek.com` 的单次瞬时网络停滞。

该失败被放大为整轮判死的原因有二：传输层把阶段 deadline 错误标记为 `retryable=false`，
网关 `ProviderSupervisor` 的 PreWrite 重试表与循环恢复预算（Mirage 此前配置
`max_recoveries = 0`）均无法吸收。这与上游设计 §10.2"request bytes 前的 DNS/connect/TLS
失败可在预算内重试"相悖。已登记 [MIRA-20261012-001](../dependency_feedback/mira.md#mira-20261012-001)。

## 交付

- 上游 mira：[PR#88](https://github.com/Linductor-alkaid/mira/pull/88)（merge 57c3062，
  经维护者授权合并，工作分支已删除）。网络阶段 deadline（dns/connect/TLS/write/
  first-byte/idle-read）错误携带 `retryable=true` 机制提示；被 total 预算收紧的等待保持
  不可重试（header 等待回卷为既有 total 消息）；循环恢复白名单承认 retryable 的
  `ModelDeadlineExceeded`；设计 §10.1/§11 同步。重试决策表、幂等策略、预算与 circuit
  行为不变。
- pin 消费：`third_party/mira` c75378b → 57c3062，`dependencies.lock.json` 同步；
  嵌套 pin 不变。
- Mirage 侧（`integration/mira/src/model_layer.cpp`）：profile 传输 deadline 放宽
  `tls` 10s → 20s、`write` 10s → 30s（桌面请求携带多 MB 截图请求体，上游 10s 默认对
  家庭上行与代理链路过紧；首字节 60s、total=request_deadline 不变，dns/connect 5s
  保持快速失败并由上游重试兜底）。会话循环恢复预算按 RULE-08 信封阈值启用：
  turn capacity ≥ 10 时 `max_recoveries = 2`（turns ≥ 7），稀缺预算保持原 fail-fast
  形态——避免为重试牺牲小预算配置的可用轮次。

## 验证

| 验证 | 结果 | 证明范围 |
| --- | --- | --- |
| mira 上游 CI | 24/24 | Linux GCC/Clang × Debug/Release、Windows、Android、ASAN/UBSAN/TSAN、quality |
| mira 本地全量 | 105/105（Debug 与 ASAN-only） | 新增 transport（connect/first-byte retryable、total 不可重试）、supervisor（阶段决策表）、conversation/agent loop（retryable 恢复、不可重试 fail-closed）用例 |
| Mirage 全量 | 71/71（native-debug） | pin 消费 + deadline/恢复预算改动完整回归；`desktop_image_supply_test` 的 7 轮脚本在阈值方案下不经测试修改即保持旧行为 |
| 集成行为用例 | 356 checks 0 failures | profile deadline 断言（20s/30s/60s/90s）；retryable 单次失败经真实 gateway/supervisor/loop 重试成功（恰好 2 次调用）；不可重试立即失败（1 次调用）；post-write 场景恰好消耗 2 次恢复预算 |
| sanitizers | ASAN（detect_leaks=0）、UBSAN（halt_on_error=1）目标用例通过 | 三套配置在阈值方案业务代码上干净 |

精确命令（在仓库根执行）：

```bash
cmake --preset native-debug && cmake --build --preset native-debug -j4
ctest --preset native-debug --output-on-failure
cmake --build --preset asan -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --preset asan -R 'native_agent_integration_test' --output-on-failure
cmake --build --preset ubsan -j4
UBSAN_OPTIONS=halt_on_error=1 ctest --preset ubsan -R 'native_agent_integration_test' --output-on-failure
```

## 范围与残留

真实 socket 传输的 dns/connect/TLS 阶段超时→retryable 语义未做线上验证（测试经真实
gateway/supervisor/loop 全链路覆盖错误分类与重试决策，fixture 无网络路径）；DNS 站点
在 mira 侧无确定性运行时用例（静态确认），TLS/write 与 read 共用同一 poll-wait 链路。
产品内验收：DeepSeek 修复包的原会话测试项在本次网络修复后可重跑，结论回填
[deepseek-package-20261012](deepseek-package-20261012.md)。
