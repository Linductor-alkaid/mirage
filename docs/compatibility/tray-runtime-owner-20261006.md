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
首次验收时旧用户窗口和 headless 服务保留，防止丢弃未确认的 API Key/输入草稿；新入口
遇到旧服务会明确要求关闭旧实例再启动，没有自动杀进程或读取实际 Key。

## 后续目标平台验证

M6-25 的 Linux 私有实际产品验收完成；M6-04 / M6 保持 In Progress。Windows
CreateProcess/通知区、原生 Wayland/高 DPI、真实通知区像素和安装包未执行，负责人
为 Mirage 维护者，补跑条件为目标机器及对应 SDK/桌面宿主；复用本次 gate/关闭重开/
活动退出矩阵并核对图标资源。依赖 pin 和设置/凭据/会话磁盘格式不变。


## BUG-20261006-006：应用条目不能恢复最小化窗口

维护者确认失败入口为应用列表/Dock。在当前 GNOME/XWayland 会话复现：产品
frontend_ready=true、窗口仍映射，但 _NET_WM_STATE_HIDDEN=true，重复启动 epoch
增长而窗口不恢复。EUI GLFW runner 在 iconified 时跳过 compose；原恢复位于
compose 的 IPC 消费内，窗口暂停绘制后无法执行。此为 Mirage 产品打开职责，
不需要修改 EUI，也不属于 Mira/Mirador 能力缺口。

FrontendProcess.open 的既有子进程分支现在直接在 Runtime 串行任务内请求平台
恢复。Linux 查询有界 EWMH 客户端列表，以 _NET_WM_PID 匹配所属 child；明确用户
打开用 _NET_ACTIVE_WINDOW 的 pager source，WM-less 使用所属顶层窗口 map/raise。
Windows 对所属 PID 请求 ShowWindowAsync。候选预算 256，超限拒绝且有诊断；
不选择其他 PID 的窗口，不扫描/终止别的进程。初次尚未映射时继续既有启动就绪等待。
公开 open 契约和 DEC-045 同步，窗口自身的 IPC/UI show 保留为可绘制时的补充。

本机实际操作：已关闭旧窗口、无活动任务/Workflow；启动产品后人工协议最小化当前
所属前端，再通过 Gio 的 org.mirage.native.desktop 打开，hidden 标记清除且相同
前端 PID 复用。托盘/前端新实例已运行，未捕获用户会话正文/Key。系统可能拒绝前台
焦点，所以只承诺此次窗口恢复，不把 active-window=false 表述为焦点验收通过。
公开布尔取证为 desktop-restore-results.json。

```bash
python3 tests/manual/tray_runtime_acceptance.py --build build/native-release   --window-manager --output /tmp/mirage-tray-restore-private   --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
cmake --build build/native-release --target frontend_activation_test mirage-format-check -j3
ctest --test-dir build/native-release -R '^frontend_activation_test$' --output-on-failure
ctest --test-dir build/native-release   -R '^(tray_runtime_test|native_agent_integration_test|native_conversation_view_test|tray_backend_test)$'   --output-on-failure
```

私有 WM 为外部 ICCCM/EWMH 测试对端，接受真正 minimize/unmap/map/activate 请求，
不会代替产品消费队列。21/21 全部通过，新增窗口暂停绘制、启动入口恢复、托盘恢复
三项；restore-results.json 保留结果。平台单测验证 WM-less map/focus、精确 PID
选择、非所属 PID 无动作、257 候选拒绝且不发恢复请求。Release 平台 1/1 和相关
4/4 通过；ASAN/UBSAN build 中相关各 2/2，新增实际平台各 1/1，无诊断。命令为在
各 build/asan、build/ubsan 构建 frontend_activation_test 后执行同名 CTest；实际
开窗夹具仍使用 Release。构建/格式/51 个公共头边界复验通过。

首个私有脚本编辑尝试匹配旧行失败（未运行产品测试），改正 fixture 插入位置后
完整执行退出码 0；不将脚本编辑失败计为产品通过。当前用户窗口已更新并恢复。
首个 PR head 4c76e5b 的 CI Linux debug/release/ASAN/UBSAN、格式/边界、native
均通过；TSAN 的 event_subscription_test 末尾 Completed 计时断言失败，Windows
MSVC 全树 getenv/C4244 门禁失败，均记录为未完成，不声称全 CI 通过。此次修复不
将 M6-04 的 Windows/包装验收关闭，后续按目标门禁单独处理。


## 左键两项菜单增量

对应 bc4bcf6 后本记录所在提交的工作树，环境及依赖 pin 同前。按 DEC-045 左键
显示“打开应用”“退出应用”，右键保留相同菜单。Linux 返回一个 id=0 的 DBusMenu
根和两个 variant 子节点，声明 ItemIsMenu，并补齐菜单属性与签名；Windows 保留
现有左键/右键弹出路径，布局同步为两项。旧暂停/恢复 id 不派发，Open 灰显时不派发，
Quit 始终进入 Runtime 既有退出检查。没有新增并发设施或修改依赖。

```bash
cmake --build build/native-release --target mirage mirage-native mirage-service mirage-tray \
  tray_backend_test tray_runtime_test frontend_activation_test mirage-format-check mirage-boundary-check -j6
ctest --test-dir build/native-release \
  -R '^(tray_backend_test|tray_runtime_test|frontend_activation_test)$' --output-on-failure
python3 tests/manual/tray_runtime_acceptance.py --build build/native-release \
  --window-manager --output /tmp/mirage-tray-menu-private-final \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
cmake --build build/asan --target tray_backend_test -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan -R '^tray_backend_test$' --output-on-failure
cmake --build build/ubsan --target tray_backend_test -j4
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan -R '^tray_backend_test$' --output-on-failure
cmake --build build/tsan --target tray_backend_test -j4
setarch x86_64 -R ctest --test-dir build/tsan -R '^tray_backend_test$' --output-on-failure
```

Release 3/3，菜单后端 79 checks；格式与 51 个公共头边界检查通过。真实双进程
23/23，在既有 21 项生命周期矩阵之外，新增标准签名根树/两个启用动作/ItemIsMenu
以及 AboutToShow/GetAll 准备请求。打开、最小化恢复、关闭重开、活动任务退出取消/
确认均通过。公开结果为证据目录 menu-results.json；测试未读取用户 Key 或发出推理。

首次 ASAN（71 checks 版）报告 136 字节泄漏：7 个 Event 字符串副本及 5 个测试
GMainLoop。回调改用 borrowed 字符串，GetProperty 返回值引用平衡，夹具 join 后
释放 loop；补根深度/无效 parent/深度及批量预算测试后 ASAN/UBSAN/TSAN 各 1/1（79 checks），
无 sanitizer 报告。TSAN 仍需关闭 ASLR，未改变此前全树 CI 的独立失败记录。

当前用户托盘/前端实例未强制重启；只读确认 active_work=0，不读取会话正文或草稿。
新菜单随正常重启加载。私有 DBus 宿主验证了菜单协议与产品动作，但未执行真实
GNOME 通知区左键像素/鼠标菜单验收；Windows 构建/通知区亦未执行，负责人 Mirage
维护者，补跑条件为加载新实例后点击通知区和目标 Windows SDK/桌面。此记录不以
协议测试冒充实际通知区呈现，也不将未执行项标为通过。


## BUG-20261006-007：GNOME 应用列表仍调用旧前端命令

维护者再次明确统一入口：冷启动先托盘后窗口，托盘已驻留时只打开窗口。现场
GNOME 46.0 / GIO 2.80.0 / XWayland，磁盘唯一 Mirage 条目已执行 mirage start，
但实际应用列表点击在用户 journal 留下原生程序直接调用和“tray runtime is not
running”诊断。临时 .desktop 启动跟踪未被该点击执行，而新 GIO 对象读到已更新
命令；因此之前 GIO 文件启动验收遗漏了现存桌面缓存的旧调用。

按 DEC-045，Linux 原生入口在已登记 GIO 桌面标记且未指定托盘端点时，在窗口/
Executor 初始化前 exec 同树 mirage start。正常前端由托盘附带明确端点并严格
核验注册和所属 PID，不递归转发。未知桌面标记、显式端点非所属、缺少启动器和
启动器指向同程序都明确拒绝；新桌面文件继续直接执行统一启动器。

维护者随后从真实应用列表点击，明确反馈“窗口和托盘都出现了”；现场只读 IPC
验证 frontend_ready、UI parent 为托盘，再经正式注册条目的 GIO 打开测得相同
托盘与前端 PID 复用。未捕获用户会话、输入或 Key，没有发出模型请求。正式开发
注册目标重新运行；临时诊断入口已恢复，不留 /tmp wrapper 的运行依赖。

```bash
cmake --build build/native-release --target mirage-native mirage mirage-tray \
  mirage-native-register-desktop mirage-format-check mirage-boundary-check -j6
ctest --test-dir build/native-release \
  -R '^(tray_runtime_test|frontend_activation_test|native_agent_integration_test)$' --output-on-failure
python3 -B tests/manual/tray_runtime_acceptance.py --build build/native-release \
  --legacy-desktop --window-manager --output /tmp/mirage-legacy-desktop-final-v2 \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
python3 -B tests/manual/tray_runtime_acceptance.py --build build/native-release \
  --window-manager --output /tmp/mirage-new-desktop-final-v2 \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
cmake --build build/asan --target mirage-native mirage -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 python3 -B tests/manual/tray_runtime_acceptance.py \
  --build build/asan --legacy-desktop --bootstrap-only --output /tmp/mirage-desktop-bootstrap-asan \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
cmake --build build/ubsan --target mirage-native mirage -j4
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 -B tests/manual/tray_runtime_acceptance.py \
  --build build/ubsan --legacy-desktop --bootstrap-only --output /tmp/mirage-desktop-bootstrap-ubsan \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
```

Release 相关 3/3，当前入口 24/24，旧 GIO desktop Exec 入口 28/28。包含冷启动、
先注册后真实 UI、已有托盘/窗口复用、关闭前端后从桌面条目重开而不换托盘、最小化
恢复、菜单与活动退出等既有链路；旧入口额外验证四个拒绝/循环守卫。ASAN/UBSAN
原生入口构建与拒绝路径各 6/6，无诊断；不把 bootstrap-only 结果当成完整 sanitizer
GUI 验收。脚本初次替换旧行匹配失败，文件未写入，其后旧脚本复跑不计为新增
桌面重开验收；改正匹配后 v2 按新矩阵完整通过。

公开结果在证据目录 legacy-entry/current-entry/bootstrap-asan/bootstrap-ubsan-results.json
与 desktop-entry-results.json。此次没有新增跨线程路径，沿用既有 Runtime Executor
和生命周期测试，未修改依赖。Windows 兼容入口、原生 Wayland和安装包仍待维护者
目标 SDK/桌面补验，不以 Linux 结果关闭这些范围。GNOME 旧缓存保留时也可使用
新版兼容入口，当前窗口和托盘已运行，不再强制重启用户实例。
