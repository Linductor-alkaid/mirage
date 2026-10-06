# 托盘 Runtime 所有权与双进程生命周期验收

> 状态：Completed（Linux X11 本机范围）
> 日期：2026-10-06
> 负责人：Mirage 维护者 / Codex
> 工作项：M6-25；依据 DEC-045（产品所有权替代 DEC-007/030 部分）
> 对应状态：codex/tray-runtime-owner，父提交 e9c8946 后本记录所在提交的工作树

## 实现与边界

产品启动器只启动/复用 mirage-tray，由托盘内嵌的 RuntimeService 持有唯一 Runtime
Executor，复用 Mira、持久化、凭据、IPC 与现有取消/关闭路径。独立 mirage-native
由托盘创建和回收；启动时先作有界同步 IPC 准入，验证注册状态与所属 PID，之后才
初始化窗口。前端自己的 IPC Executor 边界保持 DEC-033，不拥有 Agent 服务。
headless mirage-service 仍为 CLI/开发入口，其 hello 不提供产品 tray 准入。

平台注册泵、动作派发泵、500ms 子进程回收/状态维护均由同一 Runtime Executor
管理；Topic 订阅容量 128、有界动作 MpscChannel 容量 8，最新活动计数用
LatestMailbox，注册 PhaseGate 等待最多 6s。拒绝/异常进入现有 Executor 失败事实
或明确诊断；未引入线程池、私有队列或第二份 Executor。

关闭窗口保留 Runtime；托盘退出由 Runtime 检查活动任务、Agent 轮次、非终态
Workflow，确认携带独立 exit_epoch，过期确认拒绝。确认/系统信号按既有取消、排空、
shutdown 顺序关闭；IPC 断开允许前端正常退出，平台回收最多先等待 500ms，再请求
终止等待 1s，Linux 必要时强制终止再等待 1s，失败显式进入 run_report。

## 实际构建与回归

Linux 7.0.0-34-generic x86_64，GCC 13.3.0，CMake 3.28.3；native-release 为
Release、MIRAGE_ENABLE_NATIVE_FRONTEND=ON、bundled pinned 依赖不变。私有 Xvfb /
DBus / SNI fixture / XTest 使用真实产品二进制；没有读取用户凭据或运行真实模型请求。

```bash
cmake --build build/native-release --target mirage mirage-tray mirage-service mirage-native \
  tray_runtime_test native_conversation_view_test runtime_service_test ipc_protocol_test \
  ipc_protocol_golden_test tray_backend_test native_agent_integration_test persistence_test \
  mirage-boundary-check mirage-format-check -j4
ctest --test-dir build/native-release \
  -R '^(tray_runtime_test|ipc_protocol_test|ipc_protocol_golden_test|runtime_service_test|tray_backend_test|native_conversation_view_test|native_agent_integration_test|persistence_test)$' \
  --output-on-failure
```

构建成功，相关 CTest 8/8；公共头 51 个 / 0 违规，格式门禁通过。tray_runtime_test
81 checks，覆盖已有 PID 复用/ready 准入、活动 Agent 取消与旧 epoch 拒绝、等待用户的
Workflow 退出、注册拒绝、子进程启动拒绝/异常、carrier 丢失及回收失败。renderer
371 checks，新增 860×620 明暗退出卡、256 项计数与快捷键不穿透测试。

```bash
cmake --build build/asan --target tray_runtime_test native_agent_integration_test runtime_service_test -j3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan \
  -R '^(tray_runtime_test|native_agent_integration_test|runtime_service_test)$' --output-on-failure
cmake --build build/ubsan --target tray_runtime_test native_agent_integration_test runtime_service_test -j3
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan \
  -R '^(tray_runtime_test|native_agent_integration_test|runtime_service_test)$' --output-on-failure
cmake --build build/tsan --target tray_runtime_test -j3
setarch x86_64 -R ctest --test-dir build/tsan -R '^tray_runtime_test$' --output-on-failure
```

ASAN / UBSAN 各 3/3，无 sanitizer 诊断。TSAN 默认 ASLR 下启动失败
`unexpected memory mapping`，关闭 ASLR 后重跑 1/1（0.40s），无竞态报告；不将初次
环境失败写为通过。正常测试命令、关闭 ASLR 条件须一同保留。

## 真实窗口与产品入口

```bash
python3 tests/manual/tray_runtime_acceptance.py --build build/native-release \
  --output /tmp/mirage-tray-lifecycle-v13 \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
python3 tests/manual/native_conversation_acceptance.py --build build/native-release \
  --provider /tmp/mirage-key-fixture-provider --model-settings --preset-minimax \
  --settings-only --desktop-launch --output /tmp/mirage-tray-key-v13 \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
```

第一条 18/18：无托盘先于显示初始化拒绝、no-tray 前端拒绝、headless 拒绝且不杀
既有服务、无通知区拒绝、真实 GIO 双进程冷启动与 parent PID 核对、重复启动复用、
关闭前端后托盘保留且子进程回收、DBusMenu 打开新窗口、活动任务计数、托盘退出
重开确认、scrim 无副作用、实际鼠标取消保留任务、新 epoch、实际鼠标确认整体退出、
重启、SIGTERM 回收前端/删除端点。外部测试使用私有 sleep 30 桌面任务作为退出
夹具；未新增 RPA 产品能力。初次夹具用零步骤任务立即完成，无法取活动证据，改为
有界长任务后复验通过；不计入通过样本。

第二条从空配置输入 MiniMax 预设 Key，粘贴保存、键入替换保存、持久化不含明文均
通过，推理请求 0。相同 settings-only 命令去掉 --desktop-launch，以直接托盘宿主
启动再跑一次，同样通过；旧离线前端/替换 headless 宿主的手动 audit 按新准入契约
明确退役，不静默绕过产品 gate。外部 subordinate UI 仅观察消失，-999 不是 OS
退出码；托盘由测试 owner 收养后实际 waitpid 退出码为 0。

公开结果与三张退出卡截图保存在
[证据目录](../../.impeccable/review/tray-runtime-owner-20261006/)。正常 1180×800 实际
窗口及最小 860×620 明暗 renderer 已逐图检查，无裁切、缺字或按钮溢出。renderer
需在 build/native-release/apps/native 运行，使用 ../../tests/native_conversation_view_test
附捕获目录；首次从仓库根目录调用缺字体，退出 1，该错误截图已废弃。

开发注册目标 mirage-native-register-desktop 已执行，启动条目仍匹配原 WM_CLASS/Mira
图标并改用托盘入口。用户当前 DBus 只读探测：StatusNotifierWatcher 名称存在，
IsStatusNotifierHostRegistered=true；未将此探测当作真实 GNOME 托盘像素验收。
当前旧用户窗口和 headless 服务保留，防止丢弃未确认的 API Key/输入草稿；新入口
遇到旧服务会明确要求关闭旧实例再启动，没有自动杀进程或读取实际 Key。

## 后续目标平台验证

M6-25 的 Linux 私有实际产品验收完成；M6-04 / M6 保持 In Progress。Windows
CreateProcess/通知区、原生 Wayland/高 DPI、真实通知区像素和安装包未执行，负责人
为 Mirage 维护者，补跑条件为目标机器及对应 SDK/桌面宿主；复用本次 gate/关闭重开/
活动退出矩阵并核对图标资源。依赖 pin 和设置/凭据/会话磁盘格式不变。
