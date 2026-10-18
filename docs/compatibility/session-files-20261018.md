# M6-44：取消默认会话数量上限与逐会话存储

> 日期：2026-10-18
> 依据：[DEC-101](../decisions/DEC-101-session-files-and-unlimited-count.md)
> 工作树：`feat/session-file-storage`，基线 `7a27472`，未提交修改
> 环境：Linux x86_64，Ubuntu GCC 13.3.0，CMake/Ninja

## 范围与结果

原生界面的 24 个会话限制已移除，服务 `max_sessions` 默认从 25 改为 0（不限制
数量）；非零显式容量仍拒绝超额准入和不完整恢复。旧解码器的 64 个会话限制移除。
每个会话保存在状态目录的 `sessions/<id>.json`，每文件 8 MiB，沿用 schema 1。
`session-state.json` 在全部会话文件迁移完成后原子改为 schema 2 存储标记。
正常保存不重写内容相同的会话文件；删除原子写空数组墓碑；坏文件隔离保留。

恢复时同步重建 `journal_raw`：首轮回归揭示旧实现只恢复查询投影，没有恢复下一次
保存使用的日志源，导致纯任务日志会话再次保存时丢失。本轮修复后 80 个日志会话在
迁移及第二次重启后均保留。原生高度/布局缓存只保留当前会话，切换释放旧线程 scope，
滚动位置仍由会话模型保存。

已覆盖：

- 原生模型创建 101 个会话；旧文件恢复 80 个会话后继续打开 30 个新会话，IPC 列表
  返回 111 项（含内部主会话）。显式容量 25 下拒绝启动且旧文件保持原样。
- 80 个会话分别落盘，文件 0600、目录 0700，未变化文件修改时间保持不变。
- 三个独立会话总 JSON 超过旧 8 MiB 全局预算仍保存/恢复；单文件超预算拒绝并保留旧值。
- 更新与删除临时文件冲突保留旧值；迁移最后提交失败时仍读取旧合并文件，重试遗漏的
  暂存会话不会复活；坏旧文件拒绝写回。
- 坏 JSON、文件名/ID 不匹配与符号链接隔离；其他历史继续恢复，未知/临时文件忽略。
- Agent 编辑失败回滚、活动删除拒绝、删除后重启、上下文用量恢复及正常关闭回归。
- 原生 EUI/Xvfb 合成界面切换会话后旧缓存 scope 被释放，标题、正文、滚动等原回归通过。

## 实际命令与验证

Native Release 使用现有 `native-release` 配置，原生前端、PP-OCR、视觉打包均开启。
ASAN/UBSAN 为 Debug、原生前端开启、PP-OCR/视觉打包关闭。TSAN 为 Debug，原生前端
构建开关关闭，独立会话模型测试仍可构建；其余依赖沿用仓库锁定。

```sh
cmake --build build/native-release --target persistence_test runtime_service_test native_chat_model_test native_agent_integration_test mirage-native mirage-service -j 6
ctest --test-dir build/native-release -R '^(persistence_test|runtime_service_test|native_chat_model_test|native_agent_integration_test)$' --output-on-failure
cmake --build build/native-release --target native_conversation_view_test -j 4
ctest --test-dir build/native-release -R '^native_conversation_view_test$' --output-on-failure

cmake --build build/asan --target persistence_test runtime_service_test native_chat_model_test native_agent_integration_test -j 4
ctest --test-dir build/asan -R '^(persistence_test|runtime_service_test|native_chat_model_test|native_agent_integration_test)$' --output-on-failure
cmake --build build/asan --target persistence_test native_conversation_view_test -j 3
ctest --test-dir build/asan -R '^(persistence_test|native_conversation_view_test)$' --output-on-failure

cmake --build build/ubsan --target persistence_test runtime_service_test native_chat_model_test native_agent_integration_test -j 4
ctest --test-dir build/ubsan -R '^(persistence_test|runtime_service_test|native_chat_model_test|native_agent_integration_test)$' --output-on-failure
cmake --build build/ubsan --target persistence_test native_conversation_view_test -j 3
ctest --test-dir build/ubsan -R '^(persistence_test|native_conversation_view_test)$' --output-on-failure

cmake --build build/tsan --target persistence_test runtime_service_test native_chat_model_test -j 3
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/tsan -R '^(persistence_test|runtime_service_test|native_chat_model_test)$' --output-on-failure
git diff --check
```

Release、ASAN、UBSAN 的五类测试均通过：会话模型 500、持久化最终 578、Agent 集成
522、原生界面 27839 项检查，无失败。服务测试最近一次 Release 为 640 项，消毒器
和 TSAN 某次为 652 项（轮询中的检查数随调度变化），均无失败。包含 fixture 的首次四测试组为 6/6；
界面单独组为 3/3；消毒器最终持久化/界面组为 4/4。测试在隔离目录运行。

首轮 Release 服务测试有 2 项失败，定位为上述纯日志恢复遗漏；修复后定向组 6/6。
TSAN 首次普通启动三测试均在执行前报 `unexpected memory mapping`；按仓库 CI
既有 `setarch -R` 方式重跑，3 个业务测试及 fixture 5/5（1730 项检查）通过，未报告 race。

## 验证边界与剩余项

未修改 pinned 依赖，未引入异步任务或并发设施。单文件原子写入沿用既有 POSIX/Windows
实现；本轮只验证 Linux，不宣称跨会话事务或大量会话的性能保证。

M6-44 保持 In Progress：Windows 构建/文件迁移/删除，以及真实用户桌面多会话重启验收
尚未执行；负责人 Mirage 维护者，补跑条件为提供 Windows 构建运行环境和可重启的产品
验收实例。当前运行中的应用未重启，用户真实历史未做迁移；本机新二进制已构建。


## 2026-10-18：真实 Wayland 桌面启动与重启补验

维护者授权“重启测试一下效果”。检查时没有运行中的 Mirage；先构建完整产品目标，
确保托盘内嵌 Runtime、launcher、native 和 pet 均更新，再从既有桌面条目对应的命令
启动。不能只更新独立 mirage-service 就推断产品托盘已使用新 Runtime。

实际命令：

```sh
cmake --build build/native-release --target mirage-native-product -j 6
./build/native-release/apps/mirage start --shell /home/linductor/mirage/build/native-release/apps/native/mirage-native --tray /home/linductor/mirage/build/native-release/apps/tray/mirage-tray
./build/native-release/apps/mirage service status
```

实际桌面为 GNOME Wayland，原生窗口通过 XWayland 映射，2360×1600。使用既有
`tests/manual/native_conversation_acceptance.py::Wire` 发起 `product.control status`、
`session.list`、`session.chat.history(limit=256)`、`session.open` 和 `session.close`。
通过 Python Xlib/XTest 点击两个历史条目并检查真实窗口截图，历史消息与过程记录正常
显示。截图含用户历史，仅保留在用户私有备份目录，不纳入仓库。

结果：

- 迁移前为 schema 1，8 个非空会话、19 轮，原文件 329780 bytes。
  原件已保存在用户状态目录下的 `backups/session-files-20261018-145115/`，目录 0700、
  文件 0600；原文件 SHA256 为
  `a8d49006f64d8db78021aa57741e23347ee108cf37321dbac83ebb3f50f5fb4c`。
- 首次启动托盘 PID 968380、前端 PID 968399；frontend_ready=true、active_work=0。
  既有 8 个历史加内部主会话共 9 项；逐会话历史接口均返回原有轮数。
- 临时打开 30 个空会话，真实服务同时注册 39 个会话，超过旧默认 25 上限。
  逐个关闭本次创建的会话后恢复 9 项；没有调用模型，也未向用户历史添加测试消息。
- 首次保存将旧文件切为 schema 2 标记，生成 8 个独立会话 JSON；逐文件按 ID 解码后，
  整个会话对象与原备份做结构化精确比较，8 个对象、全部 19 轮完全一致。
- 在 active_work=0 时通过 `product.control(action=quit, exit_epoch=0)` 正常退出。
  确认旧托盘/前端均退出后再次执行上述启动命令，新托盘 PID 968757、前端 PID 968776。
  原生窗口重新映射且 frontend_ready=true；再次验证 9 项注册、8 个历史、19 轮，
  所有会话文件内容仍与备份完全一致，权限均为 0600，无测试会话残留。
- `/proc/<pid>/exe` 与本次构建的 native/tray 文件 SHA256 分别相同，确认运行的是新构建。
  验证完成后保持产品窗口打开供维护者继续使用。私有报告为备份目录内的
  `first-start-report.json` 和 `second-start-report.json`。

本节关闭 Linux 真实桌面启动/迁移/重启恢复验收缺口；Windows 仍未验证，M6-44 保持
In Progress。未扩大为新的模型调用、供应商互通或全套桌面动作验收声明。
