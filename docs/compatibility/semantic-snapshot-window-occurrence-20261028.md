# 语义快照同标题窗口按出现序号解析

> 状态：Completed（Linux Backend 限定范围）
> 日期：2026-10-28
> 负责人：Mirage 维护者
> 依据：[DEC-118](../decisions/DEC-118-significance-gated-perception-and-scoped-snapshots.md)、
> [DEC-067](../decisions/DEC-067-local-semantic-workflow-grounding.md)
> 修正：[native-provider-discovery-20261022](native-provider-discovery-20261022.md)
> 的"同标题多 ID 拒绝快照"规则

2026-10-28 会话（QQ 群发消息）暴露 `desktop.accessibility.semantic_snapshot`
在多窗口同标题应用上必然失败：QQ 的多个窗口全部标题为 "QQ"，2026-10-22 引入的
v1 映射在标题不唯一时整体拒绝（`unsupported_window: window title mapping is
ambiguous`），随后 DEC-085 熔断把该工具对本会话封锁。同时 harness 观察守卫把
"目标窗口从未在前台"也报成 "focused window changed during capture"，调用方无从
知道应先聚焦窗口，只能原样重试直至熔断。

## 变更

- AT-SPI 语义快照的 window_id 解析不再要求标题唯一：window_id 中的 `#N` 后缀
  是 WindowProvider 枚举时对同 `(application, title)` 窗口的出现序号，解析时
  在同一枚举顺序上按该序号选取目标窗口，并沿用清单侧的"有正尺寸屏幕 extents"
  过滤保持序号对齐。跨应用同标题窗口因此也能各自解析（旧逻辑按标题全局计数，
  误伤不同应用）。
- 序号越界（列表后窗口集变化）报 `not_found`，提示重新 `desktop.window.list`；
  DEC-118 的改名回退（application 段唯一时按现标题解析）不变。
- harness 观察守卫拆分错误语义：前台窗口仍存在但 id 与目标不符时，报
  "target window is not the foreground window; focus it first, then re-observe"；
  前台窗口消失/查询失败/epoch 变化仍报原 "focused window changed during
  capture"。错误码保持 `stale_observation`。
- 工具描述写明前台窗口约束（DEC-067 的锚定语义不变：快照仍只对前台窗口生效，
  Wayland 激活仍按 AT-SPI v1 fail-closed，聚焦需借助图像层点击）。

## 影响与边界

同标题多窗口应用（QQ 等 Electron 壳）的每个窗口 id 现在都可快照；id 与窗口的
绑定仍依赖枚举顺序，两次调用间窗口集变化可能使 `#N` 指向同应用的另一窗口，
此风险与 window.list 的既有语义一致。不改变 Windows UIA 后端、节点预算、
DEC-085 熔断与快照作用域契约。

## 验证（2026-10-28，独立验证代理执行）

- `tests/platform/atspi_backend_test.cpp`：新增 ShelledApp 双同标题窗口 +
  ShelledPeer 跨应用同标题夹具，逐 id 快照互不串树；DuplicateWindows 幽灵
  "#1" 报 not_found 不串树。debug 138 checks 0 failures；ASAN 同样 138/0，
  无 sanitizer 报告。
- `tests/platform/atspi_scoped_snapshot_test.cpp`：跨应用同标题与精确新 id
  均解析成功；改名回退/双不同标题 not_found 保留。54 checks 0 failures。
- `tests/runtime/harness_desktop_tools_test.cpp`：focus_changed 场景 call-5
  断言新消息（foreground window）且错误码 stale_observation；新增 focus_lost
  场景断言旧消息保留。harness 2275、local_semantic_workflow 125、
  observation_e2e 90 checks 全部 0 failures。
- 范围限定：仅 Linux AT-SPI 路径；Windows UIA 后端未验证（不在此变更范围）。

## 生产门禁补验（2026-10-28）

完整 TSAN 矩阵发现夹具在 GLib 属性读取期间直接修改节点名称，触发真实数据竞争。
修正 `FakeAtspiDesktop::rename_node()`，用独立名称锁保护写入和 Name 属性读取；
树拓扑仍在启动前固定。首次复用观察锁导致拒绝路径的同步 RPC 等待相互阻塞，
已撤回并改为独立锁，不屏蔽 TSAN。最终两项 AT-SPI 用例各连续运行5次通过，
CTest 含 setup/cleanup 为4/4，无 sanitizer 报告。

日志：`/tmp/mirage-production-tsan-test.log`（原竞争）、
`/tmp/mirage-production-tsan-fixture-test.log`（被撤回的观察锁尝试）、
`/tmp/mirage-production-tsan-fixture-verified-test.log`（修正后重复验证）。
当前源的综合生产矩阵另见[生产验收记录](layered-perception-production-20261028.md)。
