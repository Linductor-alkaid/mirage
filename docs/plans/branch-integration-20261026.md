# 工作分支集成与清理（2026-10-26）

> 状态：In Progress
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
| BR-01 | feat/m7-27-held-pointer-travel | 01b4308 | 受控移动、设备守卫与窗口切换 | 1 | In Progress |
| BR-02 | feat/continuous-capture-infrastructure | 54f8d97 | 连续采集与 Mirador 规划 | 3 | Planned |
| BR-03 | feat/output-limit-failure-category | 7dd436b | 截断分类与 Runtime/IPC 投影 | 7 | Planned |
| BR-04 | feat/continuous-perception-cycle | e966b13 | 覆盖/时间事件、ROI 与恢复闭环 | 15 | Planned |
| BR-05 | perf/frontend-long-session | 2af42c5 | EUI 更新与长会话性能 | 19 | Planned |
| BR-06 | build/mira-upstream-sync | 87c970c | Mira 升级、结构化终态与基线格式 | 21 | Planned |
| BR-07 | feat/keyboard-ime-state | 2af42c5 + dirty | 输入法状态、ROI fill、生产任务连续感知 | 19 + 未提交实现 | Planned |
| BR-08 | feat/m4-world-projection（仅远端） | e0ebeda | 已关闭 PR #36，退役 Vue/Three.js；不属维护者工作 | 与 master 分叉 | Cancelled |

前六项构成祖先链；BR-07 与 BR-05 起点相同，最终合入 BR-06 后继续验收。
BR-08 经维护者明确要求直接删除，不归档、不恢复旧前端。

## 验收与清理

- [ ] 每项 PR 记录 head、CI URL/结果、合并提交和影响范围，所有适用 job 成功后合并。
- [ ] 当前未提交源码经检查、构建与相关测试；不以旧工作树结果代替最终集成结果。
- [ ] 合并后核对 master 包含各功能提交；无遗漏、无开放 PR、无未保存工作。
- [ ] 删除已合并本地/远端分支，回到 master，清理本聊天已完成工作树和失效 worktree 元数据。
- [ ] 最终 master CI 验收并回填本记录；平台限制保留，未跑项目不标通过。

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
