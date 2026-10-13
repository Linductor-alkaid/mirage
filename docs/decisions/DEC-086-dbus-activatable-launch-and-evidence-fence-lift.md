# DEC-086：D-Bus 激活型应用的确定性启动与机器证据解除围栏

> 状态：Accepted
> 日期：2026-10-14
> 负责人：Mirage 维护者
> 扩展：DEC-056/068、DEC-015
> 实证：会话 `db6b8942…`（2026-10-14 00:12，「查看电脑储存空间占用情况」失败）

## 失败实证

真实 Wayland 会话中，模型对 `org.gnome.baobab.desktop`（`DBusActivatable=true`）执行
`desktop.application.launch`：GIO 在存在会话总线时把启动委派给 D-Bus 激活，子进程由
`systemd --user` 孵化（实证父进程 pid 1491，非 Mirage 进程），进程内无 fork，
CLOEXEC pid 管道恒空。后端据此把**完全成功的启动**判为
`uncertain / "spawned process pid was not reported"`，叠加 DEC-056 的围栏语义后：

1. `application_running` 再核验把 `r.uncertain` 一票置为 `uncertain`，即使
   `running=true` 的机器证据已经在手；
2. `validate_request` 对 canonical 历史中该失败回执置粘性 `canonical_failure_`；
3. 变更门禁拒绝本轮后续全部修改动作，模型用 OCR 数据作答仍被 settle 否决，
   轮次以 failed 收场——对启动类应用形成必败死锁。

受影响面：本机 10 个 `DBusActivatable=true` 的 GNOME 应用（baobab、Nautilus、
gnome-disks、gnome-logs 等）。此前 bilibili 任务通过仅因该轮未调用 launch。

## 选型

**平台层（根因）**：`DBusActivatable=true` 且 `Terminal=false` 的条目，按 Desktop
Entry 规范把 `Exec` 行展开为 argv（字段码 %u/%U/%f/%F/%i/%c/%k，弃用码丢弃，argv
≤32 项、≤8 KiB）后用 `g_spawn_async_with_pipes` 直接派生，沿用既有 pid 管道、
进程组与注册表模型；`Path=` 作为工作目录。解析失败或超出上限回落 GIO 路径。
GIO 激活路径对无 `Exec` 的纯 D-Bus 条目仍可用，但保持 uncertain 语义。

**验证层（证据解除围栏）**：修订 DEC-056 的"不确定副作用等待用户检查"——围栏针对
**不可证实**的副作用，不针对证据。`application_running` 条件事后取到
`running=true` + 实例 id 时，记录落 `verified`（reason
`application_running_confirmed`），围栏解除；不可证实的记录维持人工复核语义。
`canonical_failure_` 改为每次请求按记录当前状态派生重算（接受谓词与 settle 一致：
verified 或派发前干净拒绝），未知失败始终不合格。点击/窗口类条件的 uncertain
仍不允许自证清除（输入注入无法因果证实），维持人工复核。

## 边界

- 直接派生的单实例应用第二次启动时由应用自身委托既有实例后退出；`running_state`
  经 /proc 名称扫描（既有回退）仍能报告运行中实例。
- `terminate` 仅对本后端跟踪的实例投递信号，外部/委托实例的停止不在契约内（不变）。
- 无 `Exec` 的纯 D-Bus 条目与 `Terminal=true` 条目回落 GIO；后者可能仍产出
  uncertain，由验证层证据解除兜底。

## 验证

平台层：XDG_DATA_HOME fixture 桌面条目（`DBusActivatable=true`）验证 launch 直接
派生并上报 pid、running_state 可见、terminate 可停。集成层：uncertain launch →
verify 确认运行 → 记录 verified、围栏解除、下一修改放行、settle 通过；运行证据
缺失时维持 uncertain 封锁。命令与结果见
[验收记录](../compatibility/dbus-activatable-launch-20261014.md)。
