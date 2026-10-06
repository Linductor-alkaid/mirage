# 原生前端 PR 栈 CI 验收与清理

> 状态：Blocked（仅 EUI 上游合并权限；自有仓库功能 PR 已验收合并）
> 日期：2026-10-07
> 负责人：Mirage 维护者 / Codex；外部合并：EUI-NEO 上游维护者
> 工作项：M6-28；所属：[M6](m6-native-frontend.md)
> 依据：维护者明确授权修复 CI、合并相关 PR、清理工作分支及过期 build；沿用既有产品契约。

- [x] 原生前端基础分支与 Mirage #67–72 修复、同步，实际 CI 全绿后按依赖合并；补建 #73，将完整产品组合合入 master。
- [x] Mira #81 同步当前 master（Kairo），最新 head 完整 CI 成功后合并；Mirage 继续固定已批准 pre-Kairo 源码。
- [x] EUI #88 复核 OpenGL/Vulkan × GLFW/SDL2 四项 CI，全绿；保留未合并分支和 pin。
- [ ] EUI #88 上游合并：当前账户仅 pull 权限，负责人为上游维护者；获得其合并结果后复核 dev 包含测试 head，才可关闭此项。
- [x] 记录精确 head、检查链接、合并结果；本轮 7 条 Mirage 与 3 条 Mira 功能分支已在本地和远程删除。
- [x] 更新并保留 desktop entry 指向的 native-release；确认无在用程序映射后清理 9 个过期 build，保留用户历史、配置、凭据和未跟踪证据。

## CI 与合并证据

| PR | 验收 head | 实际检查 | 合并提交 |
| --- | --- | --- | --- |
| [mira #81](https://github.com/Linductor-alkaid/mira/pull/81) | `5c0244e284e6` | 24/24 SUCCESS | `0eddcc4b23f7` |
| [mirage #67](https://github.com/Linductor-alkaid/mirage/pull/67) | `302006627649` | 8/8 SUCCESS | `f1fecea71e45` |
| [mirage #68](https://github.com/Linductor-alkaid/mirage/pull/68) | `3681b38c3bc6` | 8/8 SUCCESS | `7f7b381f8af2` |
| [mirage #69](https://github.com/Linductor-alkaid/mirage/pull/69) | `9584cf1b8596` | 8/8 SUCCESS | `35ffa923adf8` |
| [mirage #70](https://github.com/Linductor-alkaid/mirage/pull/70) | `2dabde8ef526` | 8/8 SUCCESS | `9d2efb530d04` |
| [mirage #71](https://github.com/Linductor-alkaid/mirage/pull/71) | `1134919faac8` | 8/8 SUCCESS | `d8ff3e217c9e` |
| [mirage #72](https://github.com/Linductor-alkaid/mirage/pull/72) | `fa2fde794697` | 8/8 SUCCESS | `4bf0651b7c9f` |
| [mirage #73](https://github.com/Linductor-alkaid/mirage/pull/73) | `f2cead0ebcc7` | 8/8 SUCCESS | `2d0e409af12c` |

完整 SHA 和逐项公开 CI 链接见 [机器可读证据](../compatibility/ci-acceptance-20261007.json)。
每个 Mirage PR 的八项门禁均包括格式/公开头边界、Linux Debug/Release/ASAN/UBSAN/TSAN、
Windows MSVC full-tree 与 native EUI。Mira #81 的 24 项包含 push/PR 两组实际检查，
覆盖 Linux、Windows、Android、sanitizer 和质量门禁；不把重复事件声称为额外平台。
Mira #79/#80 此前已经合并，本轮复核其结果并清理相应分支。

Mirage #67–72 保留原有堆叠 base 逐层合并；#73 的验收组合进入 master。
验收记录分支再以仅历史的普通 merge 保留 #69–72 的 GitHub merge 提交，合并前
`git diff --exit-code origin/master..HEAD` 验证源码树与 #73 主干一致。
记录分支 `codex/ci-acceptance-record-20261007` 仍须走 master PR、全量 CI 和精确 head 合并；
它的本地/远程分支在该 PR 合入后清理。未 force-push、绕过 CI 或直接推送 master。

## 已修复问题

Windows CRT 的 C4996 /WX 通过有所有权的环境值读取修复，布局/指针坐标 C4244 通过显式
float 转换修复，包括基础协议菜单的循环位置；保留 warnings-as-errors。
Linux native job 补齐 libcurl4-openssl-dev 和 Xvfb，保留原配置门禁。
Mira 当前主干已迁移 Kairo，Messages opt-in probe 及认证 fixture 的旧 Executor 类型、
链接目标和 include 改用当前公开 Kairo 能力。Mirage 的 mira pin 仍为
`fbc644be2fabfaa2f8257e579fbc1d368537224c`，没有借收尾升级并发设施。

#69 旧 TSAN job 的后续任务未 Completed，日志没有 race 报告；进一步回归发现真实
Process Provider EOF/退出状态问题，见 [BUG-20261007-001](maintenance-linux-process-eof-20261007.md)。
新增回归先在原实现稳定失败，再修复 fd 生命周期、实际退出观察和清理，保留取消与预算。
不根据缺少诊断的旧 CI 日志断言其唯一根因，也不放宽断言或关闭 sanitizer。

## 本地验证与产品构建

基础与最终组合 Release/native 三项 UI/Agent 测试各 3/3 成功，format/boundary 成功；
Mira 当前主干 Debug 全量 104/104，format/docs/SBOM/platform/architecture 成功。
进程修复 Release 两项 2/2；Debug/ASAN/UBSAN/TSAN 各对同两项重复三次，均六次成功。
最终登记入口的 native-release 从验收源码重建 mirage、mirage-tray、mirage-native，
模型、Agent、会话视图、Process Provider 和事件订阅五项测试 5/5 成功（16.54 秒）。

可复现命令：

```sh
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build build/native-release --target mirage mirage-tray mirage-native native_chat_model_test native_agent_integration_test native_conversation_view_test provider_hardening_test event_subscription_test -j4
ctest --test-dir build/native-release -R '^(native_chat_model_test|native_agent_integration_test|native_conversation_view_test|provider_hardening_test|event_subscription_test)$' --output-on-failure
```

本轮证据不替代 M6 尚未执行的 Windows 真实 GUI/IME 等产品验证；M6 整体状态保持原状。

## 清理结果

已删除本地和远程 Mirage 功能分支：`codex/native-agent-workbench`、
`codex/messages-dependency-pin`、`codex/provider-presets-vision`、`codex/tray-runtime-owner`、
`codex/messages-thinking-pin`、`codex/session-thinking`、`codex/conversation-scroll`。
已删除 Mira 三条分支：`codex/conversation-stream-previews`、`codex/anthropic-messages`、
`codex/messages-thinking`；移除干净的临时 Mira CI worktree。
删除前逐项复核 PR MERGED、验收 head 和祖先关系，保留相关 merge 历史在验收记录分支。

已删除 Mirage build 下 asan/debug/native-debug/tsan/ubsan，两个临时 native CI build，
以及两个临时 Mira build。实际释放 19,657,924,608 字节（约 18.31 GiB），
逐目录分配量在机器可读证据中。删除前检查 /proc 可执行文件映射，不移除在用目录；
保留 native-release（应用列表/Dock 登记入口）并更新构建。不使用全仓 git clean，
未删除既有未跟踪截图、交互证据、配置、密钥或会话记录。

## 外部阻塞

[EUI-NEO #88](https://github.com/sudoevolve/EUI-NEO/pull/88) 的精确 head 为
`ff1e7572132a7ff8fc70c8e52f7778fce4dd976f`，四项 CI 全绿，但上游仓库对已认证账户
Linductor-alkaid 的权限为 pull=true、push=false、maintain=false、admin=false。
因此该 PR 保持 OPEN，源分支和已批准 pin 保留，由上游维护者完成合并。
本轮不使用 fork 合并冒充上游合并，不宣称全部依赖 PR 已合并。
