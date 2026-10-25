# 工作分支集成与清理（2026-10-26）

> 状态：Completed（初始七个工作分支全部验收、合并并清理）
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-26

## 范围与策略

维护者授权整理全部工作分支，逐个通过 PR、验收 CI 后合并 master，并清理分支。
初始 master 为 `4ec413a`，远端无开放 PR。本地分支均在此基线上线性叠加。
采用 merge commit 保留已有提交身份及开发链，避免子分支因 squash 重复提交；
每次合并核对当前 head、master 合成结果、CI 与评审意见。失败先修复，不绕过检查。
当前未提交实现先检查、形成范围清晰的提交，再纳入最终 PR；不丢弃源码和验收资料。

## 分支关系与工作项

| 工作项 | 分支 | 初始提交 | 相对上一个边界的内容 | 初始 master 之外提交数 | 状态 |
| --- | --- | --- | --- | --- | --- |
| BR-01 | feat/m7-27-held-pointer-travel | 01b4308 | 受控移动、设备守卫与窗口切换 | 1 | Completed |
| BR-02 | feat/continuous-capture-infrastructure | 54f8d97 | 连续采集与 Mirador 规划 | 3 | Completed |
| BR-03 | feat/output-limit-failure-category | 7dd436b | 截断分类与 Runtime/IPC 投影 | 7 | Completed |
| BR-04 | feat/continuous-perception-cycle | e966b13 | 覆盖/时间事件、ROI 与恢复闭环 | 15 | Completed |
| BR-05 | perf/frontend-long-session | 2af42c5 | EUI 更新与长会话性能 | 19 | Completed |
| BR-06 | build/mira-upstream-sync | 87c970c | Mira 升级、结构化终态与基线格式 | 21 | Completed |
| BR-07 | feat/keyboard-ime-state | 2af42c5 + dirty | 输入法状态、ROI fill、生产任务连续感知 | 19 + 未提交实现 | Completed |
| BR-08 | feat/m4-world-projection（仅远端） | e0ebeda | 已关闭 PR #36，退役 Vue/Three.js；不属维护者工作 | 与 master 分叉 | Cancelled |

前六项构成祖先链；BR-07 与 BR-05 起点相同，在 BR-06 基础上完成集成验收。
BR-08 经维护者明确要求直接删除，不归档、不恢复旧前端。

## 验收与清理

- [x] 每项 PR 记录 head、CI URL/结果、合并提交和影响范围，所有适用 job 成功后合并。
- [x] 当前未提交源码经检查、构建与相关测试；不以旧工作树结果代替最终集成结果。
- [x] 合并后核对 master 包含各功能提交；无遗漏、无未保存工作，初始七项功能 PR 均已合并。
- [x] 删除初始七条已合并本地/远端工作分支，完成 master 同步，归档本聊天临时工作树并清理失效元数据。
- [x] 最终 master tree 与通过 PR CI 的 head tree 核对并回填本记录；平台限制保留，未跑项目不标通过。

## 验证记录

2026-10-26：fetch 全部远端 heads 并核对祖先图、PR 历史及当前 dirty 清单。
旧 World 分支 PR #36 为 Closed/unmerged，维护者明确其不属当前工作并要求直接清理。


## PR 与本机集成基线

| 工作项 | PR | 合并次序 |
| --- | --- | --- |
| BR-01 | [#114](https://github.com/Linductor-alkaid/mirage/pull/114) | 1 |
| BR-02 | [#115](https://github.com/Linductor-alkaid/mirage/pull/115) | 2 |
| BR-03 | [#116](https://github.com/Linductor-alkaid/mirage/pull/116) | 3 |
| BR-04 | [#117](https://github.com/Linductor-alkaid/mirage/pull/117) | 4 |
| BR-05 | [#118](https://github.com/Linductor-alkaid/mirage/pull/118) | 5 |
| BR-06 | [#119](https://github.com/Linductor-alkaid/mirage/pull/119) | 6 |
| BR-07 | [#120](https://github.com/Linductor-alkaid/mirage/pull/120) | 7 |

2026-10-26：BR-08 已按维护者最后指示直接删除远端 head，不归档。BR-07 的
71 个原始 dirty 文件逐文件 SHA-256 核对后原样保存为 `ff2f0b9`，再合入 Mira
升级与格式修复；所有代码均在 Git 中。公开 JSON 证据共约 0.5 MiB，无模型、
运行日志、私有密钥或令牌；签名模式扫描与人工范围检查通过。

Linux x86_64 / GCC 13.3.0 / CMake 3.28.3；Debug 默认选项，Mbed TLS/native/
PPOCR 未开启。原始工作区与最终集成源码分别执行：

```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure -j4
cmake --build build/debug --target mirage-format-check mirage-boundary-check
```

原工作区 Debug 113/113（76.55 s）、合入 Mira 68a8bf1 后 Debug 113/113
（80.36 s），均 0 failed、0 skipped。集成源码全仓格式与 67 个公开头文件
边界检查通过（0 违规、DEC-096 两条既有缝）。未将本地环境当作 Windows/真机证明。

PR #116 当前 CI 揭示旧 harness 诊断 `fprintf` 换行问题，按仓库 clang-format
修复并通过完整静态检查；原格式失败保留为历史，不计入成功证据。修复正常非 force
推送并沿祖先链同步，Mira 的结构化终态判别没有回退为旧文本锚点。

每项合并还须确认最新 head 的八项 CI 全部成功、无未处理 review thread，并以
`git merge-tree --write-tree <current-master> <head>` 的 tree 与已测 head tree
逐位一致为准，防止基线改变后误用旧测试。PR retarget master 后才按固定 head SHA
执行 merge；若合成结果变化或 CI 失败，先同步修复及重新验证，不绕过门禁。


## 2026-10-26：合并与清理收口

初始七条工作分支分别通过最新 head 的八项 CI，按依赖顺序通过 PR 合入 master。
每次均确认无未处理 review thread，并核对当前 master 合成结果与已测试 head 的
完整 Git tree 一致；未 force-push、未绕过 CI 或修改仓库保护设置。

| 工作项 / PR | 已测 head | 合并提交 | CI |
| --- | --- | --- | --- |
| BR-01 / [#114](https://github.com/Linductor-alkaid/mirage/pull/114) | `01b4308` | `51e57b2` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210311899) |
| BR-02 / [#115](https://github.com/Linductor-alkaid/mirage/pull/115) | `54f8d97` | `7654292` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210468329) |
| BR-03 / [#116](https://github.com/Linductor-alkaid/mirage/pull/116) | `6056fed` | `58284da` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210813881) |
| BR-04 / [#117](https://github.com/Linductor-alkaid/mirage/pull/117) | `fd12b57` | `009cb68` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210811943) |
| BR-05 / [#118](https://github.com/Linductor-alkaid/mirage/pull/118) | `ad73b31` | `46b5f0b` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210812477) |
| BR-06 / [#119](https://github.com/Linductor-alkaid/mirage/pull/119) | `1bdba72` | `a7ec94d` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40210812453) |
| BR-07 / [#120](https://github.com/Linductor-alkaid/mirage/pull/120) | `b838326` | `877853f` | [8/8 SUCCESS](https://github.com/Linductor-alkaid/mirage/actions/runs/40211554643) |

完整 head、tree、合并时的 master、合并时间及各检查 URL 见
[结构化证据](../compatibility/data/branch-integration-20261026.json)。
最终功能集成 master `877853f` 与 PR #120 已测 `b838326` 的 tree 完全一致，
包含原始 dirty 快照 `ff2f0b9`，没有遗漏未提交源码。

七条原工作分支的本地/远端引用已删除；旧 World 分支按维护者最后要求直接删除，
不归档、不恢复退役前端。升级 worktree 经 Codex archive_worktree 归档并移除，
`/tmp/mirage-m7-27-validation` 的失效元数据已 prune；主工作树保留并同步 master。
文档收口以独立 PR 提交，其 CI 和合并状态由该 PR 的检查与合并事实记录。

Windows CI 包含全树编译和既有受控运行门禁，不等于新增 IME、Portal、真实设备、
在线模型或长时资源验收。这些原有 M7/M9 未完成项继续保留，不因分支合并勾选。
此次未安装产品、创建 release/tag 或修改模型资源。
