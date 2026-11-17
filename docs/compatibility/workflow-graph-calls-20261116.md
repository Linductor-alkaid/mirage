# 工作流图编辑与动态调用验收

> 状态：Completed（M15-21 Linux 限定增量；完整 M15 继续开放）
> 日期：2026-11-16
> 工作项：M15-21；依据：[DEC-150](../decisions/DEC-150-workflow-graph-composition.md)
> 负责人：Mirage 维护者

## 实现范围

侧栏左上角会话/工作流切换，资产目录，真实 IR 的顺序节点、条件/验证标记、回环与
失败回退连线。节点检查器可编辑参数、前置条件、完成验证、失败处理和既有回环次数；
可更换原子任务、添加调用、提取连续任务段为独立草稿、保存/验证发布及进入调用目标。
运行与显式恢复保留原控制面。图的滚动和检查器独立，最小窗口 860×620。

工作流是独立资产，调用不构成父子所有权。`workflow.<id>` 在实际分派时读取目标的最新
已发布版本；正在执行的一次调用不重放。发布前检查候选调用图，自调用和任何间接环路
均拒绝，重复的无环调用允许。Mira 的现有深度 2/运行/步骤预算作为额外资源边界，不能
替代循环检验。Mira Skill 仍执行实际调用，失败与取消向上传递。

原子目录显式广告 `safe_workflow_editor`，旧服务缺省只读；保存/发布带 `expected_head`
执行 CAS，失败不丢本地草稿。每次保存/发布后只做一组有界目录刷新；无新线程或轮询。
发布 handler 用 kairo DoubleBuffer 发布不可变快照，旧的活动调用持有自己的快照。
当前服务内目录沿用 DEC-023，尚不能跨服务重启保留；长期学习存储是另一个已交付路径。

## 功能与边界验证

复现命令（PRESET 为 native-debug / asan / ubsan / tsan；TSAN 的 ctest 前使用
`setarch x86_64 -R`，沿用现有环境方式）：

```sh
cmake --build --preset PRESET --target native_workflow_editor_test workflow_composition_service_test native_workflow_integration_test native_workflow_model_test workflow_recovery_service_test ipc_protocol_test -j 2
ctest --test-dir build/PRESET -R '^(native_workflow_editor_test|workflow_composition_service_test|native_workflow_integration_test|native_workflow_model_test|workflow_recovery_service_test|ipc_protocol_test)$' --output-on-failure
```

编辑投影测试覆盖 IR 往返、结构化条件、非法 JSON、任务段提取、修订/错响应/断线、
版本冲突前置条件、未知服务只读以及调用来源返回路径。真实原生 RuntimeBridge 验证
读取、目录、草稿编辑、验证发布和运行；服务测试验证直接/两节点/三节点环路拒绝、
合法重复调用、调用目标更新后的实际动作次数、更新时已开始的调用不重放、取消与关闭。
原 IPC 与恢复门禁保持回归。最终实测如下（均 0 skip）：

| 配置 | 实测结果 | 最终日志 |
| --- | --- | --- |
| native-debug | 功能 8/8；真实 renderer 40657 检查、0 失败 | `build/m15-workflow-graph-review-native-debug-test.log`、`build/m15-workflow-graph-hover-capture.log` |
| ASAN | 功能 + native renderer 9/9 | `build/m15-workflow-graph-review-asan-test.log` |
| UBSAN | 功能 + native renderer 9/9 | `build/m15-workflow-graph-review-ubsan-test.log` |
| TSAN | 功能 8/8；不含 GPU renderer | `build/m15-workflow-graph-review-tsan-test.log` |

计数包含两个状态隔离 fixture。配置沿用离线缓存：native-debug/ASAN/UBSAN 的 native=ON，
TSAN 的 native=OFF；四者 fetch/OCR/vision bundle/pet 均 OFF。新配置需先使用对应
`cmake --preset PRESET` 并显式传入上述开关，再运行本节命令。原生 renderer 另建
`native_conversation_view_test`；ASAN/UBSAN 上述最终过滤器额外包含该测试。
格式及公开头边界通过，0 新违规（67 个被检查头、2 个既有 DEC-096 seam）。

开发中发现并修正：空 IR 不合法（新草稿保留待配置节点）、可选空名称不得编码为空字段、
动态调用保守副作用需完成回执、测试须等待上一驱动实际结算后再次准入。仅测试重试明确
的容量结算拒绝；产品不静默重试。首次 TSAN 直接运行遇到 ASLR memory mapping 错误，
按上述既有环境命令重跑。四配置初次完整矩阵还发现旧测试仍期待返回来源后强制过期；
在 CAS 保留保护下返回原草稿已可编辑，测试预期修正并补齐回环/导航覆盖后通过。
历史失败在 `build/m15-workflow-graph-*-release-*.log`，最终结果以上述 review 日志为准。

## 原生视觉证据

使用真实 EUI/OpenGL 产品 renderer、隔离 Xvfb、随产品字体与图标、合成场景；无模型或
真实用户桌面动作。图与调用、明暗、常规/最小、收起侧栏、参数错误、有界回环，以及
既有运行/恢复空/等待/取消/失败状态分别捕获。截图位于
[本轮证据目录](../../.impeccable/review/m15-workflow-graph/finish-packet.md)。
首轮发现输入缩进与参数横向截断，已统一产品 inset 并改为多行；调用入口上移到检查器
首位并提供节点直接入口。旧 renderer 仍点击已移除的标题栏入口，测试选择器已迁移；
该失败不作为产品图组件崩溃归因。独立首评 [fix](../../.impeccable/review/m15-workflow-graph/finish-review.md)
列出底色/对比度、默认占位、窄窗调用布局、保留提示和跨文档任务段标记五项，已同批
修正并补几何/标记回归。首轮限定评分四项 resolved、F1 hover partial，保留在
[第一轮评分](../../.impeccable/review/m15-workflow-graph/finish-verdict-round1.md)；唯一剩余
颜色修正后重捕相同 26 图，最终 [评分](../../.impeccable/review/m15-workflow-graph/finish-verdict.md)
为 ship，剩余 F1 resolved，F2–F5 沿用上轮 resolved；只覆盖原五项修正。最后的单色补正仅重建 native-debug 产品/renderer、运行 40657 检查并更新图片，
没有重复未受影响的功能/sanitizer 矩阵。PNG 尺寸和 SHA 记录于 captures.json。
产品 `mirage-native`、`mirage-service` 与 `mirage` 均已构建，记录于
`build/m15-workflow-graph-product-build.log`；没有启动用户服务或变更其运行会话。

重捕命令在 `build/native-debug/apps/native` 运行（路径替换为仓库绝对路径）：

```sh
../../tests/native_conversation_view_test /ABS/REPO/.impeccable/review/m15-workflow-graph workflow
```

renderer 输出 PPM；使用 Pillow 无损转换为同名 PNG，并记录真实尺寸与 SHA256 到
captures.json。26 份 PNG 是交付证据，原 PPM/编译日志留在本地生成流程，不进入提交。
[文档交接](../../.impeccable/review/m15-workflow-graph/documentation-review.md) 核对当前说明与
既有系统，DESIGN/token/sidecar 保持不变。

## 未覆盖

未执行 Windows、真实用户桌面收益、在线模型质量或工作流目录跨服务重启恢复；无性能
改善保证。完整候选审阅、参数设计器、任意分支/并行、节点拖拽和目录持久化继续由 M15
原计划跟踪。此记录不关闭完整 M15。

| 未执行项 | 原因 | 负责人 | 补跑条件 |
| --- | --- | --- | --- |
| Windows 原生与图交互 | 当前环境为 Linux | Mirage 维护者 | Windows 构建/桌面可用后重跑入口、图、调用与关闭矩阵 |
| 真实桌面/在线模型收益 | 本轮使用隔离脚本与虚拟文件 Provider | Mirage 维护者 | M15-09 固定真实任务集、配置模型并另行取证 |
| 跨服务重启目录恢复 | 本轮未实现目录持久化 | Mirage 维护者 | 完成资产存储/加载后追加重启与失败恢复门禁 |


最终文档/工作树收口：21 份 Markdown 的围栏/H1 与 628 个本地文件链接通过，26 图尺寸/
SHA 通过；git diff --check 通过，无新增超过 1 MiB 文件，无依赖 pin、DESIGN 或 token
变更。原 PPM 中间文件已清理，保留 PNG 和验证清单。M15-20 原增量与本轮 M15-21
均保留在当前工作树，尚未提交或推送。
