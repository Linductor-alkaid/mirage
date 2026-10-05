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
当前退出0表示三项现象均复现；若依赖修复、初始化失败或协议结构不同，则退出非0要求重新
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
