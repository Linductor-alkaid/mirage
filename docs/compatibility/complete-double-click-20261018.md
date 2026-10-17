# 完整点击序列验证

> 日期：2026-10-18
> 状态：Completed（Linux X11 与 Portal 事件边界首增量）
> 负责人：Mirage 维护者
> 工作项：M7-20 / DEC-099；基线：`9444b29`，本地 `fix/desktop-click-sequence`

## 原因与实现

原工具和共享输入 action 没有点击次数，X11/Portal 每次只发送一组按下/释放。
两次工具调用之间的观察和模型往返无法保证系统双击时限。维护者进一步明确要求
泛化为自定义次数，单击、双击和更多次点击共用函数。

共享、区域和编号点击增加可选整数 `click_count=1..32`，默认 1。
`click_sequence_at(..., count)` 在单次动作的既有权限、目标重验和 lease 内完成
指定数量的按下/释放，之后统一观察和核验。32 为单次连续输入容量上限；既有动作
期限和取消仍可提前终止。未实现序列的 InputProvider 不把多次降级为单击；语义
Invoke 仍为一次激活。未修改 pinned 依赖或新增并发设施。公开 C++ ABI 需重编译，
IPC 和持久化格式不变；安全契约、设计、DEC-099 和 M7 计划已同步。

## 已执行

- native-release 产品及相关测试构建成功。
- `provider_contract_test`、`x11_backend_test`、`desktop_image_supply_test`、
  `visual_marks_test`、`gui_fallback_test`、`portal_input_test` 通过：
  CTest 8/8（含两个隔离 fixture），23.14 秒。
  X11 对 1/2/3/5/32 次实际收到同一点交替的 press/release；双击两次按下间隔小于
  250ms。物理按钮占用、非法次数和提前取消均拒绝。三个工具分别覆盖次数透传至
  单次 provider 调用及越界拒绝；共享工具另覆盖负数、小数、字符串，非法参数不触发
  输入前采集或派发。Portal 覆盖第 3/32 次按下失败、异常、取消、超时、释放失败、
  前次释放后取消和会话停止，始终尝试释放已拥有按钮，停止后续按下。
- GTK3 私有 Xvfb 真实列表：1/2/3/5/32 次分别收到相等的正常按下与释放数量；
  单击 `row_activations=0`，双击为 1，三击信号为 1，无按钮遗留。
  29 项检查、0 失败。5/32 次的列表激活分别为 2/16，说明点击数量不代表业务操作
  次数，具体解释由应用决定。执行入口：
  `build/native-release/tests/gtk_pointer_effects_test /usr/bin/python3 tests/manual/gtk_pointer_fixture.py --click-sequence-only`。
  启用 `MIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE` 后也注册为 `gtk_click_sequence_test`。
- ASAN/UBSAN 覆盖 provider、X11、共享工具、Portal 四个实际测试，
  均 CTest 6/6（含 fixture），15.29/14.91 秒，无 sanitizer 报告。
- 全仓 C++ 格式、公共头边界和 `git diff --check` 通过；61 个公共头零新增违规。
  新私有头格式与 Python fixture 语法检查通过。

本轮泛化后的日志：`/tmp/mirage-click-sequence-{build,tests,gtk,static-checks}.log`、
`/tmp/mirage-click-sequence-{asan,ubsan}-{build,tests}.log`。
首次仅支持 1/2 的历史验证保留于 `/tmp/mirage-double-click-*.log`，不代替本轮复验。

## 运行实例

在 `active_work=0` 时经 `product.control quit` 正常退出旧 owner 和子进程，使用
已登记桌面入口的显式 tray/native 路径启动 native-release。新 tray/native/pet
PID 分别为 900685/900704/900705；frontend_ready=true、pet_state=running。
三者 `/proc/PID/exe` 摘要均与当前构建文件一致。8 个历史会话、19 轮记录与重启前
持久化数据完全相同。证据：`/tmp/mirage-click-sequence-live-validation.json`。
原 master 中的预算扩充和过程诊断继续包含在本次 native-release 中。

## 未执行范围

没有调用真实供应商模型重跑用户任务；不能据此保证模型在所有场景都选对目标。
Portal 使用边界事件和调用链 fixture，未验证真实 Wayland compositor 的多击识别；
Windows 完整点击序列没有实现，默认入口继续显式不可用。由 Mirage 维护者在目标桌面
授权后，用全访问任务打开受控 GTK 列表条目，检查一次 `click_count=2` 调用与一次
条目打开，并验证三击事件；Windows 后端交付后补跑相同语义。本轮没有新增跨上下文
状态，未运行 TSAN。M7-20 的这些后续平台验收保持开放；本地分支尚未提交 PR。
