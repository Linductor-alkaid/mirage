# Native 会话密度、流式与输入法验收

> 状态：Completed（下述Linux范围）；负责人：Linductor-alkaid；日期：2026-10-05。
> 工作项：M6-15/16/17/18；依据：[DEC-041](../decisions/DEC-041-native-conversation-density-and-progress.md)。

## 交付与依赖

正文/输入14EM、22px行距，顶部14EM、历史40px与较小品牌/设置标题；工具栏左侧附件/权限，
右侧上下文/模型/思考/发送。真正流式快照到达时更新Markdown，等待显示三点和单调时钟
用时。预览非权威、不存盘、不触发工具、终态覆盖；迟到/旧序列/取消预览拒绝。
活动100ms刷新仅用既有Executor timer，空闲取消；关闭回收worker/future再shutdown。

Mira采用PR#76的3716dbf加0a099ba流式修复，公开ConversationLoop执行规范工具往返；
上游[PR#79](https://github.com/Linductor-alkaid/mira/pull/79)已提交未合并。
EUI dev123f0c5加df8ab1c，平台层协商XIM位置并从framebuffer换算窗口内光标行底；
[PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)已提交未合并，锁定可获取的维护者fork。
依赖提交/锁一致，嵌套Executor2ae4fc8与其他嵌套依赖不变，不迁移kairo。
Mira新增AGPL-3.0许可记录，详见[升级审计](../supply-chain/dependency-upgrade-audit.md)。

## 实际验证

Linux x86_64 / GCC、Ninja，当前pinned源码；以下是本轮实际执行结果，不代表整个平台矩阵。

| 验证 | 实际结果 |
| --- | --- |
| Debug针对 | 5/5通过：chat model、native conversation view、native agent integration、IPC protocol、event subscription |
| Release针对 | 同上前四项4/4通过；原生UI、Service、Tray、CLI构建通过 |
| ASAN / UBSAN | 各四项4/4通过；ASAN detect_leaks=0，不声明LSAN或全C图形栈覆盖 |
| TSAN | 普通启动映射错误；setarch -R后集成156 checks、协议459 checks通过，无race诊断 |
| Mira standalone Debug | SSE/conversation/gateway/canonical/dialect/tool_loop六项6/6；架构检查通过 |
| EUI standalone Debug | bundled eui_neo和可选Linux IME探针构建通过 |
| 边界/格式/差异 | 公共头边界49个零违规；CheckFormat与diff --check通过 |
| 原生渲染 | 一次批量正常/最小、明暗、长名称、等待/真实预览夹具，布局无重叠 |

```sh
cmake --build build/native-release --target mirage-native mirage-service mirage-tray mirage -j2
cmake --build build/native-debug --target native_chat_model_test native_conversation_view_test native_agent_integration_test ipc_protocol_test mirage-native -j2
ctest --test-dir build/native-debug -R '^(native_chat_model_test|native_conversation_view_test|native_agent_integration_test|ipc_protocol_test|event_subscription_test)$' --output-on-failure
# native-release / asan / ubsan对应构建；各运行前四项。ASAN关闭系统图形库leak检测。
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --test-dir build/asan -R '^(native_chat_model_test|native_conversation_view_test|native_agent_integration_test|ipc_protocol_test)$' --output-on-failure
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan -R '^(native_chat_model_test|native_conversation_view_test|native_agent_integration_test|ipc_protocol_test)$' --output-on-failure
setarch $(uname -m) -R build/tsan/tests/native_agent_integration_test
setarch $(uname -m) -R build/tsan/tests/ipc_protocol_test
python3 third_party/mira/tools/check_architecture.py third_party/mira
python3 docs/dependency_feedback/repro/run-probe.py --fixed
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
```

测试覆盖分片中文、Chat SSE工具拼接/usage/type:null、缺终态/超预算/回调抛异常、取消
抑制、旧订阅兼容、预览终态隔离、timer唤醒/关闭、请求拒绝/失败/超时、队列溢出恢复。
真实Mira请求用规范ToolResultPart往返，不再把工具结果包装成私有用户文字。
依赖探针--fixed退出0，workflow同digest可运行版本与ClientHello SNI复验通过。
原设备AgentLoop仍要求screen，通用会话已换公开ConversationLoop；不是修改其桌面语义。

## 上游CI现状

EUI PR#88的GLFW/OpenGL和SDL2/OpenGL通过；Vulkan两项失败于既有未修改的
lifecycle probe缺少glad/glad.h（该探针明确选择OpenGL），不是完整矩阵通过。
Mira PR#79首轮-Wmissing-field-initializers在旧interop probe失败，本轮以新增成员
显式默认初始化修正并本机开启同一警告验证源码兼容；后续CI仍以PR当前状态为准。
不将本地通过冒充远程完整CI通过。

## 真实供应商与IME

只在内存读取维护者已授权的本机Mira模型配置/凭据，不进入仓库、参数或证据日志。
SiliconFlow Qwen/Qwen3.5-4B：实际文字请求10个预览、最大19bytes，最终2+3=5；
wait工具请求2步/1工具、19预览、最大73bytes，均在infer返回前收到。
MiniMax MiniMax-M3：实际文字请求收到提前预览；工具请求实测2步/1工具、6预览、
最大75bytes。模型有时未调用wait却声称已使用，此类尝试不计作工具验证。
另一次SiliconFlow文字探针遇网络deadline，明确失败，保留事实，不承诺网络可用性。

私有Xvfb、DBus、IBus1.5.29/libpinyin；不操作用户桌面或输入。
候选窗口从(220,440)移动到(400,280)，分别等于窗口原点(100,80)加光标
(120,360)/(300,200)，选择后真实提交U+4F60 U+597D（你好）。重现：

```sh
cmake -S third_party/eui-neo -B /tmp/eui-ime-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEUI_DEPS_MODE=bundled -DEUI_BUILD_APPS=OFF -DEUI_BUILD_USER_APPS=OFF -DEUI_ENABLE_MODULES=OFF -DEUI_BUILD_LINUX_IME_PROBE=ON
cmake --build /tmp/eui-ime-build --target eui_linux_ime_cursor_probe eui_neo -j2
python3 third_party/eui-neo/tests/platform/run_linux_ime_cursor_probe.py /tmp/eui-ime-build/eui_linux_ime_cursor_probe
```

本机Xvfb使用用户sysroot路径，通过--xvfb参数指定。运行器要求真实IM组件，缺组件/超时
失败而非伪成功。原生应用渲染验证不等于物理高DPI或所有输入法互操作验证。

## 视觉证据与限制

[正常浅色](../../.impeccable/review/conversation-progress-20261005/normal-light.png)、
[等待](../../.impeccable/review/conversation-progress-20261005/waiting-light.png)、
[流式深色](../../.impeccable/review/conversation-progress-20261005/streaming-dark.png)、
[最小长模型](../../.impeccable/review/conversation-progress-20261005/model-long-860-light.png)、
[IME首位置](../../.impeccable/review/conversation-progress-20261005/eui-ime-first.png)、
[IME移动后](../../.impeccable/review/conversation-progress-20261005/eui-ime-second.png)。
会话截图是公开EUI原生渲染夹具，不冒充live模型截图或ZCode像素对照。
[内联降级复核](../../.impeccable/review/conversation-progress-20261005/finish-verdict.md)；
四份DESIGN/JSON分别同步。不会将已存在的其他未提交截图带入本轮提交。

Windows、原生Wayland、物理高DPI/其他IM由维护者在对应环境补跑；本轮不作跨平台保证。
上游当前kairo配置的Mira CI另验，本轮保持Executor约定。M6-04统一入口/托盘退出确认
尚未完成，M6里程碑仍In Progress。无新增桌面/RPA工具或workflow执行能力。

Git交付：当前codex/native-agent-workbench保留范围化依赖锁与会话实现提交；
依赖修复已普通push并分别建PR，Mirage当前为本地提交，不合并master、不创建release。
原有未提交截图/交互结果仍留工作树，未纳入本轮。
