# DEC-086 验收记录：D-Bus 激活型应用启动与证据解除围栏

日期：2026-10-14
范围：`platform/linux/src/application_backend.cpp`、`integration/mira/src/desktop_action_verification.cpp`
决策：[DEC-086](../decisions/DEC-086-dbus-activatable-launch-and-evidence-fence-lift.md)
验证：Independent-Verification-Agent 独立执行（生产代码未由验证方修改）

## 实证背景

会话 `db6b8942…`（2026-10-14 00:12，「帮我看看我的电脑储存空间占用情况」）：
`desktop.application.launch` 对 `org.gnome.baobab.desktop`（`DBusActivatable=true`）
被 GIO 委派给 D-Bus 激活（baobab 实际父进程 `systemd --user`，pid 1491），Mirage
进程内无 fork、pid 管道为空 → 成功启动被判
`uncertain / "spawned process pid was not reported"` → 围栏 + `canonical_failure_`
死锁本轮全部后续修改，settle 否决已生成的回答，轮次 failed。

## 测试与结果（build/native-release，ctest）

| 套件 | 结果 | 覆盖 |
|---|---|---|
| `application_backend_test` | 188 checks / 0 failures | 新场景 `dbus_activatable_direct_spawn`：direct launch 上报 pid（不再 uncertain）、`running_state` 一致、list running 标志、二次 launch `already_running`、terminate 可停且进程 reap、terminate 后 not running；字段码展开经可观测代理验证（`%U` 替换、无字段码 URI 追加、`%%` 转义、`Path=` 工作目录生效）；回落路径（Exec 超 32 项上限 / 缺 Exec / 引号损坏 → GIO 路径 fail-closed） |
| `post_action_verification_test` | 628 checks passed | 场景 A（端到端）：uncertain launch → verify 取得 running 证据 → 回执 `verified` / `application_running_confirmed`、围栏解除、第二次 handoff 真实派发、turn ok、`settle()` 通过；场景 B（`uncertain_gone`）：无运行证据时维持 `uncertain`/`effect_uncertain`、第二次派发被拒、turn 失败；场景 C（验证器直测）：`validate_request` 对"已 verified 的失败历史"通过、未知 action_id 回执仍不合格、再验证后恢复（锚定派生重算而非粘性）；`desktop.input.key` uncertain 场景（窗口分支，人工复核语义）原样通过 |
| `native_agent_integration_test` | PASS（空闲时 ≈7s） | harness 端到端无回归 |
| 全量 ctest | 84/86 | 仅 `gtk_pointer_effects_test`、`gtk_pointer_scaled_test` 失败——改动前代码（git stash 对照）同样失败，为既有 Xvfb+GTK 环境时序问题，与本次改动无关 |

基线对照：改动前代码上 `post_action_verification_test` 的 `"uncertain"` 模式失败于
`check(result.ok == success)`——原断言锚定旧"必败"语义；修复后该场景转败为胜，
断言按新契约更新（见测试文件内注释与上表）。

## 残余风险与补跑条件

- 真实 session-bus 环境下 GIO 对 DBusActivatable 条目的激活分支无法在无总线测试
  进程内复现；direct-spawn 分支经 pid 管道/注册表/terminate 契约验证，带总线环境
  需真实桌面会话回归（用户下一次真实任务即覆盖：baobab/Nautilus 类应用启动应
  直接 dispatched）。
- `expand_exec_argv` 的 8 KiB 字节上限分支未单独测试（与 32 项上限共用回落路径）。
- 既有问题（非本次引入，建议跟进）：`gtk_pointer_*` 两例环境时序失败；
  `native_agent_integration_test` 的 ctest 120s 超时预算在重负载下偏紧。
- 2026-10-13 遗留补跑项不变：Xorg 对照、Portal 拒绝→授权→重试自动化、Windows
  平台验证、TSAN（vm.mmap_rnd_bits=28）。
