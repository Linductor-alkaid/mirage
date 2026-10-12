# M7-01/02/03 作用域观察与完整输入验证

> 状态：Completed（M7-01/02/03 Linux X11 首交付；其他平台/视觉/业务验收未完成）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作树：feat/m7-conversation-desktop-tools，父提交 a722f383b004236017a7e5460ce61a9002f5dfe8，未提交
> 依据：[M7](../plans/m7-visual-tools-and-input.md)、[DEC-054](../decisions/DEC-054-scoped-complete-desktop-actions.md)

## 实现与适用范围

沿用 Mira ConversationLoop/BuiltinToolRegistry 和 Runtime 唯一 Executor。Context 注入
共享 ActionCoordinator，workflow 的窗口激活/文本输入与会话修改使用同一 lease。
每轮只保留最新会话观察，窗口/语义 scope、epoch、前台与几何明确核验；AT-SPI2/UIA
在现有锁内验证 backend scope。视觉 registry 同锁校验/执行的边界已交付，当前会话
尚不开放 @v 或坐标输入。输入 Provider 对完整 key/click/type 清理声明支持后才暴露
key/type；语义 click 还需 scoped_references_supported。

| 工具 | 会话版本/注册与权限 |
| --- | --- |
| desktop.window.list / front | 2.0.0；WindowProvider；观察 scope/epoch/time；front 无焦点仍返回 found=false |
| desktop.application.list | 1.0.0；ApplicationProvider；只读应用清单，不签发修改观察 |
| desktop.accessibility.semantic_snapshot | 2.0.0；AccessibilityProvider；512 nodes、64 KiB 渲染上限，截断明确标记 |
| desktop.window.activate | 2.0.0；WindowProvider + owner + 门禁 + default；window.activate；最新 list 身份 |
| desktop.input.type_text / key | 2.0.0；Window/InputProvider + 完整动作声明 + owner/门禁/default；input.inject；当前窗口观察 |
| desktop.input.click | 2.0.0；Window/AccessibilityProvider + scope 声明 + owner/门禁/default；input.inject；当前语义 @e ref |
| wait | Mira 原有工具；仅 default |

完整环境 default 目录为八个桌面工具加 wait，read_only 为四个观察工具。缺 Provider/
声明/owner/门禁时相应修改工具不注册。普通动作 1 秒、文本 64 KiB、观察有效期 5 秒，
均不代表系统调用的强制终止保证。示例、回执和恢复流程见[安全契约](../security/desktop-actions.md)。

环境：Linux x86_64 / GCC 13.3.0 / C++20；X11 和 AT-SPI2 开启，系统 Xvfb 独立目标。
native-debug/release 开启原生前端；asan/tsan 非原生前端，仍编译真实平台 Backend。
Mira bfcb8e77、其 kairo ef821dcc v0.6.0、Mirador fb0dc3f8、EUI a7e625fe 与锁一致。
无 third_party 源码、submodule 指针或锁文件变更，无新线程/队列/定时设施。

## 测试及故障证据

确定性 Script：list → activate(w2) → front → type(probe) → semantic(w2) → click(@e1)
→ front → key(ctrl+a) → front → Answered。fixture 显式 max_output_tokens=512、轮次
预算 3 秒，容纳该测试链路；产品仍按原 Profile 输出预算收紧推理数（默认 7），没有
因 fixture 扩大默认预算。后观察是提示和 fixture 行为，强制业务验证仍属于 M7-08。

action_contract_test：schema v2 required golden、会话身份不符、64 KiB 越界、5 秒观察过期、几何
改变、无前台、Executor 上并发 lease 拒绝、workflow 同 owner 拒绝、接管取消、lease
异常结算、释放失败 fence；PhaseGate 控制视觉解析/执行与发布竞争，同名旧 @v1 拒绝
且不回退 raw。AT-SPI2 fixture 重新发布后旧 scope 的同名 @e2 激活/写入均拒绝。

harness_desktop_tools_test：只读/缺 Provider/门禁、跨轮新 registry 与相同 call ID，
旧 scope/epoch、缺 schema 字段、权限等待 epoch 变化、backend scope 变化、焦点
变化、权限拒绝/异常、运行中取消/期限，Part 文本脱敏与 dispatched/verified=false。
Runtime IPC 验证初始模型、model.set 替换、Deny；阻塞 Provider 验证 chat.cancel、
shutdown 和 supervisor 接管后释放。释放失败、清理后异常、ok=true 但释放失败三类
故障均产生 uncertain/fence，普通 resume 拒绝，shutdown 报告失败。异常详情不外泄。

真实 X11：独立目标收到 probe 和逐键 release；ctrl+shift+t 后 XQueryKeymap 全部
释放；Ctrl 按下后触发取消/1 ms 超时均释放；用户已持有 Ctrl 时拒绝且全部物理状态
不变；ASCII中 整批拒绝且没有任何按键事件；a\nb 产生 a/Return/b；click 派发后取消
仍释放按钮，以 XQueryPointer 验证 Button1Mask 清零。不存在用户真实桌面输入。

| 门禁 | 最终结果 |
| --- | --- |
| native-debug 全量 | 54/54 通过，退出 0，无 skip |
| native-release 全量 | 54/54 通过，退出 0，无 skip |
| 联合 ASAN/UBSAN 六测试 | 6/6 通过，退出 0；detect_leaks=0、UBSAN halt_on_error=1 |
| TSAN 新契约及 harness | 55 / 422 checks，各 0 failures、退出 0，无竞态报告；setarch -R |
| 公共头/格式/文档 | 53 headers / 0 violations；格式、git diff --check 通过；文档复验见 M7 记录 |

准确命令：

```bash
cmake --build --preset native-debug -j 3
ctest --preset native-debug -j 3
cmake --build --preset native-release -j 3
ctest --preset native-release -j 3
cmake --preset asan -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build --preset asan --target harness_desktop_tools_test action_contract_test atspi_backend_test element_target_executor_test desktop_atom_toolset_test native_agent_integration_test -j 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan -R '^(harness_desktop_tools|action_contract|atspi_backend|element_target_executor|desktop_atom_toolset|native_agent_integration)_test$'
cmake --build --preset tsan --target harness_desktop_tools_test action_contract_test -j 2
setarch x86_64 -R build/tsan/tests/action_contract_test
setarch x86_64 -R build/tsan/tests/harness_desktop_tools_test
cmake --build --preset native-debug --target mirage-boundary-check mirage-format-check
git diff --check
```

## 尚未验证的范围

不宣称泄漏检查：联合消毒器关闭 leak detection。TSAN 默认 ASLR 曾因 unexpected
memory mapping 启动失败，使用同宿主 setarch -R；Xvfb 探测占用 display 后重选，
xkbcomp 扩展 keysym 警告非致命，不构成失败或 skip。没有性能/实时性基准。

Windows UIA scope 核验有源码变更，但没有 Windows 编译/运行；完整 Windows key/type
未 opt in，Wayland 无新增实现。负责人 Mirage 维护者，目标环境/授权可用后按 M7-10
执行独立构建、Unicode/UIPI/多屏 DPI、授权拒绝/撤销验收。Linux X11 不代替这些门禁。

未运行在线模型工具互操作、业务表单提交成功、真实 OCR/Canvas、图片方言/在线识图；
分别保留 M7-08、M7-04～07/10。逐轮 ImagePart 仍受 [Mira #85 / MIRA-20261009-001](../dependency_feedback/mira.md)
阻塞。前一轮六工具证据保留在[首增量记录](conversation-desktop-tools-20261009.md)。

2026-10-09 迭代记录：workflow 的 busy 测试最初把失败 ToolExecutionRecord 误当外层
Result 失败，修正为检查 failed/safe_error_summary 后通过。最后追加的无前台测试
误用 JsonValue::as_bool 导致编译失败，核对公开头后改为 as_boolean；四套配置最终
重建和以上全部门禁均通过。额外复核保留聚合初始化兼容性、去除适配器私建 action owner，
拒绝 ok=true/input_released=false 的矛盾回执。没有删除前一轮失败/环境限制证据。
