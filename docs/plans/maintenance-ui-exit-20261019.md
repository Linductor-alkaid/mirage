# 任务事件导致前端与 pet 退出修复

> 状态：Completed
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-19

## 范围与依据

关联 M10-07/08（exec 映射与产品接线）、M6/M8 既有事件消费路径；依据 DEC-095、
DEC-049、DEC-012。修复工具输出截断破坏 UTF-8 后生成无效过程数组、导致所有事件
订阅者断连的问题。不修改 pinned 依赖，不增加并发路径。

现场最新任务终态 parts 为 `{}`；重开仍退出，托盘 active_work=0。已在私有 out
目录备份历史响应；重启取证时发现该会话无法 hydrate，其他会话正常。原始输出留存
按原有启动清理规则过期，不能声称已还原该任务每一步的原始过程内容。

## 工作项

- [x] exec 预览只发有效 UTF-8；截断尾部保留完整字符，非文本输出明确省略并保留原始留存。
- [x] 过程投影验证编码，终态只采用可编码过程；IPC 与持久化可往返。
- [x] 真实进程输出、事件订阅、历史与重启回归；ASAN/UBSAN 和完整 Release 测试。
- [x] 恢复故障会话可读终态，构建并部署修复，实测窗口与 pet 存活及重新打开。

## 验证记录

2026-10-19：开始修复。现场记录位于 `out/ui-exit-20261019/`，含用户内容，不提交。
目标环境 Linux GNOME/XWayland，native-release；Windows 未运行，负责人 Mirage 维护者，
取得 Windows 环境后补跑。具体命令与结果完成后追加。

### 2026-10-19：编码修复与定向验证

工作树基线 `741fa00`，分支 `fix/ui-task-event-exit`；Linux x86_64、GCC 13.3、
GNOME/XWayland，Release 和既有 sanitizer 预设。pinned 依赖未变。

先在旧实现增加真实 exec 的中文半字符截断与非 UTF-8 stdout/stderr 回归，
`basic_agent_toolset_test` 318 checks 中 4 项失败（`baseline-test.log`）。
现场读取原始 exec 留存也曾出现 UnicodeDecodeError；其后按已有启动规则清理，
只保留终态/history 及原始损坏会话文件的备份，不能恢复原过程字节。

修复复用 Desktop `is_valid_utf8`、Mira `parse_json`/`to_json_string`。
新增真实 exec → ConversationCapture → 终态事件 codec → 持久化 codec 往返，
拒绝无效 Part 编码；截断预览保留完整中文、留存引用仍读到完整 12 字节文本。
原有 native 桥接订阅、服务与取消/关闭测试同步复验。

实际命令（完整输出在私有 `out/ui-exit-20261019/`）：

```bash
cmake --build --preset native-release --target basic_agent_toolset_test conversation_budget_test mirage-native-product -j 2
ctest --test-dir build/native-release -R '^(basic_agent_toolset_test|conversation_budget_test|native_agent_integration_test|persistence_test|ipc_protocol_test|pet_status_test)$' --output-on-failure
cmake --build --preset asan --target basic_agent_toolset_test conversation_budget_test native_agent_integration_test runtime_service_test -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan -R '^(basic_agent_toolset_test|conversation_budget_test|native_agent_integration_test|runtime_service_test)$' --output-on-failure
cmake --build --preset ubsan --target basic_agent_toolset_test conversation_budget_test native_agent_integration_test runtime_service_test -j 2
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ubsan -R '^(basic_agent_toolset_test|conversation_budget_test|native_agent_integration_test|runtime_service_test)$' --output-on-failure
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check
```

Release 首轮定向 8/8（6 个业务测试、2 个 fixture），之后补充 UTF-8 留存续读
回归、终态诊断及头文件注释，已进入最终完整构建/测试。最终代码 ASAN/UBSAN
各 6/6（4 个业务测试、2 个 fixture），分别 35.04 / 27.94 s。
格式、62 个公开头边界检查（0 违例、保留 DEC-096 两个 seam）、文档结构与相对链接
检查通过。未新增并发/所有权路径，未单独运行 TSAN。


### 2026-10-19：完整回归、故障会话恢复与真实桌面验收

最终 `cmake --build --preset native-release -j 2` 和 `ctest --preset native-release`
通过，103/103，219.13 s；串行运行包含 Portal fixture 的两个有意的 30 秒期限场景。
完整日志为 `release-build.log`、`release-tests.log`。ASAN/UBSAN 与最终源码一致。

部署前再次确认 active_work=0，正常关闭托盘，备份原始损坏会话文件后原子替换其中
无法恢复的 `parts: {}` 为明确的恢复说明（有效 text Part）。用户输入、原始失败原因、
会话/轮次 ID、创建与结算时间保留；process_sequence 递增。恢复不表示原任务成功，
未重跑原网络任务或发起模型请求。备份与摘要在私有目录的 `fault-session.original.json`、
`latest-history.json`、`recovery-manifest.json`。

重新启动 native-release 托盘后 hydrate 6 个会话，无该文件解码错误。外部有界测试脚本
`python3 out/ui-exit-20261019/live-verify.py` 在真实 GNOME/XWayland 完成 8/8 检查：
窗口及 pet 实际可见，两个运行进程可执行文件 SHA-256 与构建一致，故障历史响应可读，
前端通过 WM_DELETE_WINDOW 正常关闭后由启动器重开，pet 通过产品控制正常停止/重启，
最后等待 35 秒跨越其 30 秒 fallback 状态同步周期，两者同 PID 且仍可见、ready/running。
证据为 `live-results.json`、`live-verify.log`、`live-fixed.log`。

首次桌面检查在 readiness 到达后立即断言已映射而失败；脚本改为有界等待 compositor
真实可见状态后复验通过，原始诊断保留在 `live-verify-initial-map-race.log`。
未为此修改产品行为。格式、边界、文档链接与 `git diff --check` 均通过；未修改
third_party，未创建提交或推送。本维护修复的 Linux 退出条件已完成，M10 其余平台
门禁不随之关闭。Windows 编译/运行未执行，负责人 Mirage 维护者，取得环境后按相同
exec 输出与重开场景补跑。
