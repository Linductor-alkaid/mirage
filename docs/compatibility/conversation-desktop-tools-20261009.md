# M7-02 会话桌面工具首增量验证

> 状态：In Progress（M7-02 完整范围）；首增量 Linux 验证通过
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-02](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-053](../decisions/DEC-053-conversation-desktop-tool-registration.md)
> 工作树：`feat/m7-conversation-desktop-tools`，父提交 `a722f383b004236017a7e5460ce61a9002f5dfe8`，未提交

## 交付边界

Runtime Service 初始装配和 model.set 替换均注入共享环境与权限控制器。ModelLayer
每轮从 DesktopAtomToolset 的既有构造函数装配新 registry，交 Mira ConversationLoop
执行和回填 ToolResultPart；未新增模型循环、异步任务、线程或队列。

| 工具 | 注册条件 | 权限/结果 |
| --- | --- | --- |
| desktop.window.list / front | WindowProvider | 观察；支持只读轮次 |
| desktop.application.list | ApplicationProvider | 观察；支持只读轮次 |
| desktop.accessibility.semantic_snapshot | AccessibilityProvider | 观察；支持只读轮次；最多 512 nodes |
| desktop.window.activate | WindowProvider + 门禁 + default 轮次 | window.activate；目标 ID 不超过 256 bytes |
| desktop.input.type_text | InputProvider + WindowProvider + 门禁 + default 轮次 | input.inject；显式 window_id；64 KiB UTF-8；派发前核对焦点 |
| wait | default 轮次 | Mira 既有工具 |

工具缺 Provider 时不注册；门禁存在不表示已经允许动作。每次请求带当前任务与操作 ID，
输入授权 resource 使用窗口 ID。文本输入 success 返回 dispatched / verified=false，
Part.input 仅投影窗口和字节数；输入内容留在规范模型往返中。四个观察原子沿用原有
权限词表规则，不凭空增加 read capability。其他工作流原子的目录/schema 保持原形。

## 自动验证与真实 X11 输入

环境：Linux x86_64、GCC 13.3.0、C++20；X11/AT-SPI2 backend 启用，系统 Xvfb。
native-debug / native-release 开启原生前端；asan / tsan 为非原生前端构建，仍开启 X11。
Mira `bfcb8e77`、其 kairo `ef821dcc` v0.6.0、Mirador `fb0dc3f8`、EUI `a7e625fe`
与 dependencies.lock.json 一致；未修改第三方源码或指针。

| 验证 | 实际结果 |
| --- | --- |
| native-debug 原子工具 / 新 harness / 原生 Agent | 3/3 通过 |
| native-release 全量 | 53/53 通过，无 skip |
| 联合 ASAN + UBSAN，同上三个测试 | 3/3 通过；detect_leaks=0，UBSAN halt_on_error=1；不宣称泄漏验证 |
| TSAN 新 harness | 默认 ASLR 首次启动报 unexpected memory mapping；setarch x86_64 -R 后 212 checks / 0 failures，无竞态报告 |
| 公共头边界 / C++ 格式 | 0 violations / 52 headers；format-check 通过 |

新测试的确定性模型脚本执行：window.list → activate(w2) → type_text(w2) →
semantic_snapshot(w2) → window.front。覆盖只读清单、空环境、缺门禁、缺窗口 Provider、
跨轮新 tool_id、同名 call ID 跨轮再次执行、动态拒绝、焦点不符、权限等待期间取消与
deadline、handler 异常的安全 ToolResultPart、工具过程状态和文本摘要。
Runtime IPC 覆盖初始模型、禁用后 model.set 启用、input.inject=Deny，并以仅观察
CancelToken 的阻塞 Provider 验证运行中 session.chat.cancel 和 shutdown 的退出。
提交拒绝、工具超时及其他既有生命周期路径由原生 Agent/Runtime 回归继续覆盖。

真实平台用私有 Xvfb 的独立 X window 接收输入：工具先发现真实窗口 ID，再激活、
输入 probe、读取前台；目标实际收到 probe 及 5 个 KeyRelease 事件。native-debug
新 harness 的一次详细运行为 214 checks / 0 failures。该取证不涉及用户桌面，
也不是完整业务表单验收；模型使用 fixture，未调用在线供应商。

复现命令（asan preset 此轮显式开启联合消毒器）：

```bash
cmake --build --preset native-debug --target harness_desktop_tools_test desktop_atom_toolset_test native_agent_integration_test -j 3
ctest --preset native-debug -R '^(harness_desktop_tools|desktop_atom_toolset|native_agent_integration)_test$'
cmake --build --preset native-release -j 3
ctest --preset native-release --output-on-failure -j 3
cmake --preset asan -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build --preset asan --target harness_desktop_tools_test desktop_atom_toolset_test native_agent_integration_test -j 3
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan -R '^(harness_desktop_tools|desktop_atom_toolset|native_agent_integration)_test$'
cmake --preset tsan
cmake --build --preset tsan --target harness_desktop_tools_test -j 3
setarch x86_64 -R build/tsan/tests/harness_desktop_tools_test
cmake --build --preset native-debug --target mirage-boundary-check mirage-format-check
git diff --check
```

## 剩余验收与下一步

M7-02 保持 In Progress。完整 observe → activate/click → key/type → observe 的
点击/组合键路径依赖 M7-01/03；负责人 Mirage 维护者，补跑条件为引用/动作租约与
取消释放契约冻结且实现可注册。此增量不开放裸指针或持有按键，未证明部分输入失败、
用户接管、与 workflow 并发输入的单写者/释放语义；焦点核对与 OS 输入间仍可能变化。

后观察目前是模型提示，强制验证门禁和业务证据由 M7-08 交付。中文/换行、真实语义
表单、Windows/Wayland 和在线模型工具互操作仍需各自环境补跑，不用本次 X11/fixture
结果替代。图片与 OCR/SoM 仍按 M7-04～07 推进，M7-05 依赖
[MIRA-20261009-001 / Mira #85](../dependency_feedback/mira.md)。

2026-10-09 后续：首增量限制保留为历史证据；作用域/点击/组合键与释放故障的当前实现和验证见
[完整输入记录](scoped-complete-input-20261009.md)。当前任务状态以 M7 工作项表为准。
