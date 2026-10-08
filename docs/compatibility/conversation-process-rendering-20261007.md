# 思考与工具过程渲染验收

日期：2026-10-07。负责人：Codex。工作项：M6-31。依据：DEC-049。

## 范围与来源

维护者要求参考本机 ZCode 直接实现。参考 `/home/linductor/ZCode` 的固定提交
`29628c9acdb81b703bbd4080c207a0e7ce5e276e`：ConversationRowView 的 ReasoningRowView
默认折叠，ToolCallBlocks/ToolLayout 以稳定工具ID保留展开状态并分出参数与结果。
Mirage 保持原生 EUI，使用28px摘要行、13px/20px详情、8px统一文字内边距及240px独立滚动。
展开后可复制；工具状态由真实 handler 产生，过程顺序、展开状态和详情滚动位置保留。

Mira 承载核对：公开 IModelProvider、ModelResponse/ThinkingPart/ToolCallOutput、
BuiltinToolHandler、ConversationLoop 及 model-agent-loop API 文档；Executor 使用 integration
skill 的 by-api/communication 路由。Adapter 装饰原 provider 并委托原 wait handler，
不另建循环/线程/队列，不修改 pinned 依赖。服务复用现有任务/Topic及历史恢复面。

新 Mirage::conversation 契约只含产品DTO，JSON编码由 Mira 支持、隐藏在实现层。
每轮96段，每个内容字段16KiB，总内容64KiB，UTF-8安全截断并明确标记。
历史响应按实际JSON编码大小保留最新轮并维持顺序，显式标记省略旧轮，保证1MiB帧预算。
会话文件及JSON解析使用固定8MiB上限；存储失败保留既有可观察错误路径。
IPC/持久化共享校验；终态幂等，过期过程快照与迟到pending被拒绝。
思考签名和redacted数据不进入IPC、持久化或复制。旧历史继续读取；旧版严格持久化
读取器不支持新增字段，不能用旧版覆盖新记录。

## 验证

```bash
cmake --build --preset native-release -j 2
ctest --test-dir build/native-release --output-on-failure -j 2
(cd build/native-release/apps/native && ../../tests/native_conversation_view_test /tmp/mirage-process-render)
python3 .impeccable/review/conversation-process-20261007/instrument.py asan
cmake -S . -B /tmp/mirage-process-full-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMIRAGE_ENABLE_NATIVE_FRONTEND=ON '-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer -O1 -g0'
cmake --build /tmp/mirage-process-full-tsan --target native_agent_integration_test -j 2
setarch x86_64 -R /tmp/mirage-process-full-tsan/tests/native_agent_integration_test
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check
git diff --check
```

Release全部52/52通过。原生渲染14,019 checks，UI模型388 checks，集成298 checks，均0失败。
集成使用脚本IModelProvider和真实Mira ConversationLoop/wait执行边界，通过真实Executor、
IPC及前端RuntimeBridge验证 pending/running/complete、执行中取消、取消后续聊、非法工具
失败、异常、超时、预算、拒绝、关闭、溢出恢复和重启历史；未请求外部付费模型。
UI验证默认折叠、实际指针点击展开、内容复制、更新保持展开、长文本独立滚动位置，
1180×800和860×620明暗主题及既有控件边距/会话/设置回归。
解码验证错误类型、重复ID、未知状态/种类及数量/单字段/总量容量限制，旧记录回归通过。
容量回归以12轮含JSON转义控制字符的长思考覆盖64KiB投影与1MiB历史响应裁剪，
最新轮序号12及顺序保留、truncated为真；大于4MiB的会话文件在重启后正常恢复。
合成大文件压力场景的删除请求等待预算为8秒；不作为产品性能指标。

ASAN/UBSAN对三个目标的全部自研依赖翻译单元插桩通过（388/298/14,019 checks），
复用Release pinned库，因EUI Release的-fno-rtti排除vptr；不宣称依赖库和vptr覆盖。
TSAN完整插桩自研和Mira/Executor依赖通过（298 checks、无诊断、不使用抑制规则）。
首次混合Release依赖的TSAN结果不能作为验收：未插桩future同步不可见；已改用完整构建。
本机默认ASLR导致TSAN unexpected memory mapping，使用setarch仅关闭测试进程ASLR。
机械设计扫描无发现，格式/公开头文件边界/diff及受影响文档本地链接检查通过。

[验证元数据](../../.impeccable/review/conversation-process-20261007/verification.json)
记录工作树与参考版本。最终原生截图来自合成测试会话：
[折叠](../../.impeccable/review/conversation-process-20261007/process-collapsed-light.png)、
[展开](../../.impeccable/review/conversation-process-20261007/process-expanded-light.png)、
[暗色](../../.impeccable/review/conversation-process-20261007/process-expanded-dark.png)、
[最小暗色窗口](../../.impeccable/review/conversation-process-20261007/process-expanded-small-dark.png)。
本机日志为/tmp/mirage-process-final-budget-{tests,asan,tsan-test}.log、
/tmp/mirage-process-final-checks.log及/tmp/mirage-process-verified-render.log。
首次容量回归发现truncated标记缺失，已修复并由最终52/52与消毒器复跑覆盖；
并行压力中托盘属性等待曾超时，额外插桩构建结束后完整52/52回归通过。

## 平台与能力限制

当前公开ModelPreviewSink只有正文：思考在每次完整模型步骤响应后出现，不是逐token流式思考。
当前会话工具仍仅wait，未接入桌面/RPA工具。历史中的无过程旧轮不会补造过程。
Windows/Wayland/高DPI、全UI依赖ASAN/UBSAN及vptr未执行；负责人Codex，补跑条件为对应
平台或完整可插桩依赖构建环境。Linux X11验收不关闭整体M6。
新可执行文件已构建；当前常驻托盘服务和前端须完整退出后重新启动，才能加载过程推送。

## PR CI总时限补验（2026-10-07）

提交16c8bc5的[首次CI](https://github.com/Linductor-alkaid/mirage/actions/runs/37636001827)
中，Debug、Release、UBSAN、格式及原生前端通过；ASAN和TSAN均为43/44通过，
唯一失败是native_agent_integration_test触及40秒CTest总时限，无消毒器诊断。
该目标新增了12轮转义长思考及大于4MiB文件持久化/恢复，完整Debug插桩的工作量扩大。
测试总时限调整到120秒；保留原有操作期限、全部断言、取消和故障注入，不跳过测试。

本机Ubuntu 24.04 / GCC 13.3的完整Debug ASAN（自研及pinned依赖）复验：

```bash
cmake --preset asan -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset asan --target native_agent_integration_test -j 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --preset asan -R '^native_agent_integration_test$' --output-on-failure -V
```

298 checks、0失败，26.28秒，无ASAN诊断；不扩大到全UI依赖或UBSAN vptr覆盖。
原生Release相同目标复验1/1通过（8.36秒），格式/52个公开头边界检查通过。
本机日志：/tmp/mirage-pr75-full-asan-{configure,build,test}.log、
/tmp/mirage-pr75-targeted-release-test.log及/tmp/mirage-pr75-reconfigure.log。
最终提交的CI结论、run链接及合并状态记录在[PR#75](https://github.com/Linductor-alkaid/mirage/pull/75)。

## 容量夹具隔离补验（2026-10-08）

提交61cce6b的[第二轮CI](https://github.com/Linductor-alkaid/mirage/actions/runs/37637754707)
中7/8个job通过（含Windows MSVC及原生前端）；TSAN在49.32秒遇到bad_variant_access，
无竞态诊断。多MiB合成状态混入正常密钥/删除/恢复夹具，造成2秒响应超时后读取非成功payload。
容量场景改为独立RuntimeService、状态目录及脚本Provider，仍执行12轮转义长过程、
完整文件恢复和1MiB最新历史验证；响应改为get_if防护，异常时打印合成轮次诊断。
容量I/O及模型期限明确为8秒，等待只查询最新一轮以减少重复编码；正常场景2秒期限保持。

UI收件箱溢出回归改为有界等待16个在途任务的准入/完成后继续填充128个收件槽，
并有界等待gap及恢复响应；不再假定固定300ms足以完成一批请求。仍验证168个请求、
溢出可观察、恢复可用及shutdown，不改变产品队列/期限，不跳过断言。

本机完整Debug TSAN（自研及Mira/Executor）303 checks、0失败，24.80秒，无诊断或抑制规则：

```bash
cmake --build build/tsan --target native_agent_integration_test -j 2
setarch x86_64 -R ctest --preset tsan -R '^native_agent_integration_test$' --output-on-failure -V
```

日志：/tmp/mirage-final-debug-tsan-{build,test}.log。
