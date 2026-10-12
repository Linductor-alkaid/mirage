# Mira反馈离线复现

工作项M6-10；详见[Mira台账](../mira.md)。这些程序仅用于确认已登记缺陷，
不是生产代码、运行时替代或预期缺陷永远存在的CI门禁。全部编译产物在临时目录自动清理。

## Linux公共API与TLS ClientHello

前置：CMake≥3.25、Ninja、Linux/OpenSSL，以及已构建的native-debug依赖图。

```sh
cmake --preset native-debug
cmake --build --preset native-debug --target mira_host_test -j 4
python3 docs/dependency_feedback/repro/run-probe.py
```

运行器复用Ninja生成的真实编译器/链接依赖，单独编译mira-contract-probe.cpp，不改构建图。
原始模式退出0表示三项现象均复现；若依赖修复、初始化失败或协议结构不同，则退出非0要求重新
核对，不能仅据退出非0判定修复。TLS只用非阻塞socketpair捕获真实adapter发送的第一个
ClientHello，检查server_name扩展；没有DNS、HTTP、凭据或证书验证绕行。此证据证明缺少SNI，
不声称完成一次TLS握手、Minimax互通或证书安全回归；上游修复后应补齐这些验收。

## MinGW-w64 POSIX编译归约

```sh
x86_64-w64-mingw32-g++-posix -std=c++20 -fsyntax-only docs/dependency_feedback/repro/mingw-native-handle.cpp
```

本机编译器路径为`~/.local/mirage-mingw/usr/bin/x86_64-w64-mingw32-g++-posix`。
预期现象是`HANDLE`到整数native_handle_type的static_cast诊断，退出1；不运行Windows程序，
不创建线程。全树构建是历史问题的另一层证据，本轮只执行最小表达式复现。


## PR#76修复后复验（M6-18）

```sh
python3 docs/dependency_feedback/repro/run-probe.py --fixed
```

2026-10-05退出0：同digest可运行版本可解析，真实ClientHello包含server_name，公开
ConversationLoop/规范工具part可用。原设备AgentLoop仍要求screen，这是其设计，不再用于
无屏幕会话；通用Loop的动态工具回填由native_agent_integration_test验证。
历史默认模式预期不同，不把其升级后的失败当新回归。MiniMax实际TLS及工具调用已另验，
Windows MinGW条目仍Open。本机凭据不进入该探针或证据。

## ConversationLoop 图片工具 API 形状探针（2026-10-09）

`conversation-image-api.cpp` 为 `MIRA-20261009-001` 的静态取证，不调用模型、
不采屏、不注入输入，不是产品运行门禁。

原 bfcb8e7 基线（历史证据）从 Mirage 根目录执行：

```bash
c++ -std=c++20 -fsyntax-only -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
```

原 bfcb8e7 下 Linux x86_64/GCC 13.3.0 编译退出 0。证明 handler/ToolResultPart 的 JSON 类型边界
与 ImagePart 模型契约；逐轮入口缺席还须结合完整 `conversation_loop.hpp` 公共声明
核对。需求、限制与验收见 [Mira 台账](../mira.md#mira-20261009-001)。

2026-10-09 PR#86 更新后：探针增加 setter 存在/缺席与签名断言。原 pin 使用默认
负向模式；新 master 的公共头快照使用 -DMIRAGE_EXPECT_MODEL_INPUT_SUPPLIER=1
正向模式，两者均编译通过。导出命令和范围见[更新核对](../../compatibility/mira-image-supply-review-20261009.md)。
实际升级后默认负向模式的失败属预期，复验应切换正向模式；不删除旧基线证据。

授权升级到 698fc77 后，对实际 pin 运行正向模式：

```bash
c++ -std=c++20 -fsyntax-only -DMIRAGE_EXPECT_MODEL_INPUT_SUPPLIER=1 -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
```

2026-10-09 编译退出 0；旧负向模式应使用旧 pin 的导出头，复现命令见更新核对。

## Mirador 常量缩放复现（M7-06）

```bash
c++ -std=c++20 -Ithird_party/mirador/include docs/dependency_feedback/repro/mirador-resize-constant.cpp build/native-debug/third_party/mirador/libmirador_image.a build/native-debug/third_party/mirador/libmirador_geometry.a build/native-debug/third_party/mirador/libmirador_core.a -o /tmp/mirage-resize-constant
/tmp/mirage-resize-constant
/tmp/mirage-resize-constant --expect-fixed
```

2026-10-09 pinned fb0dc3f：两次输出 `128 255 255`；默认模式退出 0（缺陷复现），
固定模式退出 1（未修复）。公开 API 无模型、线程或平台适配，详情见
[Mirador 台账](../mirador.md#mirador-20261009-001)。不把负向探针加入通过型 CTest。

2026-10-09 上游修复分支（[mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35)，
commit `0527166`）复验：两次输出 `255 255 255`；默认模式退出 1（缺陷不再存在），
固定模式退出 0（修复成立）。用修复分支的 `src/image/resize.cpp` 目码置于既有
native-debug 归档之前链接，pin 与 Mirage 构建图未改动。

2026-10-09 合并后正式 pin（ae8cf87，PR #35 已合并、gitlink/lock 同步升级）复验：
native-debug 归档全新重建后按本节原命令编译，两次输出 `255 255 255`，默认模式
退出 1、固定模式退出 0——修复已在正式 pin 生效，无需前置目标码。
