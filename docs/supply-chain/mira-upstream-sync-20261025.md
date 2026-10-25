# Mira 上游同步与终态消费审计

> 状态：Completed（Linux 消费范围；基线格式问题已修复，未验平台见下文）
> 日期：2026-10-25
> 负责人：Mirage 维护者
> 授权：维护者要求“上游 mira 近期更新了很多，同步一下，以及顺便利用上 mira 解决的 mirage 的台账问题”。
> 工作项：[MNT-MIRA-01～05](../plans/mira-upstream-sync-20261025.md)

## 锁定与来源

旧 pin `a29374aaf05b956cbd5ceeafc65bba4c6172675e` → 新 pin
`68a8bf17b30af246b722fbb548c033de82efe26e`（2026-10-25 fetch 的 origin/master）。
升级区间包含上游 PR#101～110；所有变更通过 gitlink 消费，无依赖源码补丁。
锁文件同步 commit 和审计说明。工作分支 `build/mira-upstream-sync` 基于当前开发
提交 `2af42c5`，不包含主工作区尚未提交的输入法/连续感知改动。

## 变化与采用

| 上游交付 | Mirage 消费 |
| --- | --- |
| PR#108 / DEC-065：终态不完整原因、ConversationSettled 计数与可选原因名 | ModelLayer 使用 Failed + terminal_incomplete_reason==MaxOutputTokens；移除 summary 文本锚点；解决 MIRA-20261022-001 |
| PR#109 / DEC-066：所有不完整回复均不可作为答案；恢复耗尽携带供应商原因 | 直接复用 ConversationLoop；ContextWindow/Other/无原因片段不进入 reply，不误分类 output_limit |
| PR#110 / DEC-067：Responses content_filter/refusal 与 Messages refusal 分类一致 | 复用方言和循环终态，不在 Mirage 再解析供应商 JSON，不把拒绝/过滤送入恢复重试 |
| PR#101/102/104/105/107：记忆 scope/ledger、kind policy、claim key、retention 与事件 | 新 pin 提供；Mirage 尚未接入产品记忆，不宣称此轮启用这些功能 |
| PR#103 / DEC-060：IEmbeddingProvider / IMemoryEmbeddingIndex / MemoryEmbeddingIndexer | MIRA-20261016-001 转 Accepted（部分交付）；前置清单确认接口与职责，宿主模型供给/质量仍开放 |
| PR#106：M22 eval 脚本 plan 绑定实际调用 | 上游测试设施修复，无 Mirage 产品接线 |

IPC `error_kind` 仍只含 output_limit，不添加其它类别，不改变公开 API、Executor
owner/工作调度、取消与 shutdown 形态。过程中的 output_continuations 计数不等于
本次失败原因；取消优先，只有最终 failed 保留类别。

## 台账边界

本区间未改 core_contracts、model_supervisor、kairo 或 PNG 编码缝。
MIRA-20260922-001、20261008-002、20261012-002、20261013-001、20261016-003/004
保持 Open；GCC 告警缓解、熔断测试重建及 Result 使用纪律继续存在。
MIRA-20261016-001 的公开缝已交付，真实模型推理仍由宿主提供，不以测试 fake
冒充语义质量或产品记忆已启用。

## 供应链与许可证

`git diff a29374a..68a8bf1 -- .gitmodules dependencies.lock.json LICENSE THIRD_PARTY_NOTICES`
无差异；嵌套 pin 不变：kairo `32027c2`（0.7.0）、Mbed TLS `068ff080` / framework
`dde0c4a`。Mira 仍为 AGPL-3.0，SQLite 仍为 vendored 3.53.4 / Public Domain。
Mirador `2a9f483`、EUI `19acab6` 及其锁定不变，无新增依赖、模型权重或二进制下载。

## 验证

Linux x86_64 / GCC 13.3.0 / CMake 3.28.3 / Ninja；pin 校验始终开启，离线配置
`MIRAGE_FETCH_DEPENDENCIES=OFF`；Mbed TLS/native/PPOCR 可选配置未开启。

Debug 与相关 sanitizer 均通过。Windows/MinGW、真实 provider、native/Wayland 桌面和模型召回
质量未执行；负责人 Mirage 维护者，分别在目标 CI/设备/provider 授权及 M10 模型与
标注集就绪后补跑。此轮无新增并发路径，未重跑 consumer TSAN。不以这些未执行项
关闭既有平台或产品门禁。本次不安装、发布或替换正在运行的 native-release。


上游契约复验（直接链接本次 Mirage Debug 产出的 Mira 静态库，全部 exit=0）：

```bash
for name in conversation_truncation continuous_conversation dialect anthropic; do
  c++ -std=c++20 -g -Ithird_party/mira/include -Ithird_party/mira/tests \
    -isystem third_party/mira/third_party/kairo/include \
    -Ibuild/debug/third_party/mira/third_party/kairo \
    third_party/mira/tests/m3/m3_${name}_test.cpp \
    -o /tmp/mirage-mira-sync-${name} -Wl,--start-group \
    build/debug/third_party/mira/libmira_core.a \
    build/debug/third_party/mira/libmira_simulator_adapter.a \
    build/debug/third_party/mira/libmira_net_transport.a \
    build/debug/third_party/mira/third_party/kairo/src/libkairo.a \
    -Wl,--end-group -latomic -lrt -pthread
  /tmp/mirage-mira-sync-${name}
done
```

终态探针检查结果/ConversationSettled 的原因存在性与计数；截断后成功、非输出
上限失败、Finite/Continuous、取消/期限/拒绝等负例不误报；方言 fixture 检查
Responses content_filter/refusal 及 Messages refusal 的规范终态。无真实在线请求。

静态检查：公开头文件边界 66 个头、0 违规；变更 C++ 的
`clang-format --dry-run --Werror` 与 `git diff --check` 通过。
`cmake --build build/debug --target mirage-format-check mirage-boundary-check`
的全仓格式门禁失败：基线 `runtime/ipc/src/protocol.cpp:3121/3123` 两处既有换行。
`git show 2af42c5:runtime/ipc/src/protocol.cpp` 导出到 /tmp 后使用同一仓库
`.clang-format` 独立复现相同错误；本次不加入无关格式化。
负责人 Mirage 维护者；补跑条件：基线格式问题在所属工作项修正后重跑全仓门禁。


Mirage Debug：

```bash
cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset debug -j6
ctest --preset debug --output-on-failure -j4
```

**110/110 通过**（116.56 s，0 failed，无 skipped）。新增消费用例覆盖六种
终态形态（MaxOutputTokens / ContextWindow / Other / 无原因 / Refused /
ContentFiltered），断言不完整正文不成为答案、过滤/拒绝只调用一次、其它原因不
携带 output_limit；另有“前轮截断后遇到含旧锚点文案的无关错误”反例，验证分类
不再受错误字符串污染。既有成功续写、取消、Runtime/IPC 传播等回归同时通过。


Mirage ASAN+UBSAN（同一二进制启用两种 instrumentation，CMakeCache 与实际编译
命令均为 `-fsanitize=address,undefined -fno-omit-frame-pointer`）：

```bash
cmake --preset asan -DMIRAGE_FETCH_DEPENDENCIES=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build --preset asan --target \
  harness_desktop_tools_test runtime_service_test ipc_protocol_test -j3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --preset asan \
  -R '^(harness_desktop_tools_test|runtime_service_test|ipc_protocol_test)$'
```

**5/5 通过**（三个行为测试及两个隔离 setup/cleanup；110.07 s），无 sanitizer
错误；harness 包含新增分类反例及既有长程循环、取消与供给回归，94.96 s 完成。
变更文档相对链接/围栏与全部直接/嵌套依赖 pin 一致性校验通过。


## 2026-10-25：基线格式门禁修复

按维护者后续授权，在同一升级分支修复 `protocol.cpp` 两处换行问题；全仓检查
继续发现 `tests/runtime/ipc_protocol_test.cpp:1362/1427/1493` 三处既有格式问题，
一并按仓库 clang-format 修复。两个文件与 HEAD 去空白后完全一致，无行为变化。

`cmake --build build/debug --target mirage-format-check mirage-boundary-check`
退出 0，全仓格式通过，66 个公开头文件层界零违规；`git diff --check` 通过。
此前格式门禁阻塞已解除，历史失败记录保留。此次仅格式修复，不新增或重跑行为
测试，前述 Debug 和 sanitizer 记录仍对应升级实现的行为验证。
