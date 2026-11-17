# 原生工作流运行与显式恢复验收（2026-11-16）

> 状态：Completed（Linux 受控原生运行/显式恢复入口；完整 M15 仍开放）
> 负责人：Mirage 维护者
> 工作项：M15-20；依据：[DEC-149](../decisions/DEC-149-native-workflow-controls.md)
> 基线：89fc251 上的本工作树改动；尚未提交或推送

> 本记录保留 M15-20 验收时的布局和截图；当前主入口与图编辑由 [M15-21](workflow-graph-calls-20261116.md) 扩展。

## 范围与环境

Ubuntu 24.04.5 LTS / x86_64、GCC 13.3.0、CMake 3.28.3 / Ninja、clang-format 18.1.8。
Mira/kairo/Mirador/eui pin 和锁文件均未变更。投影与 IPC 测试使用隔离状态目录、受控文件
Provider 和脚本模型；渲染为 Xvfb + 真实 EUI/OpenGL 产品代码和交付字体，输入为明确的
合成投影。没有接管用户桌面、读用户文件/凭据、调用在线模型或重启用户产品进程。

现有窗口标题栏增加工作流入口；可读取目录/Run/学习容量，显式运行当前可运行版本的
默认参数、停止 Run、请求/查询/取消恢复、在确认实际完成和成功恢复后另存经验。
M15-19 的单 Run 一次、固定模型至服务重启、权限及校验契约保持，UI 不构成授权。

## 功能验证

四个预设分别构建并运行：

```bash
cmake --build --preset PRESET --target native_workflow_model_test native_workflow_integration_test workflow_recovery_service_test native_agent_integration_test -j JOBS
ctest --preset PRESET -R '^(native_workflow_model_test|native_workflow_integration_test|workflow_recovery_service_test|native_agent_integration_test)$' --output-on-failure
# Debug -j4；ASAN/UBSAN/TSAN -j3；TSAN 的 ctest 前加 setarch x86_64 -R
```

Debug、ASAN、UBSAN、TSAN **各 6/6，0 skip**（4 个业务门禁 + 2 个隔离 fixture），
无 sanitizer 报告。Debug 投影/原生链路/服务恢复/既有原生 Agent 分别为
102/71/121/557 检查；TSAN 为 102/71/126/557，服务中的有界轮询使断言次数不同。
独立评审修正后再次只重建/运行投影与原生链路两个受影响门禁，四配置各 **4/4，0 skip**，
分别为 **110/71 检查**、0 失败；未修改的服务恢复/既有原生 Agent 不重复执行。
原生链路真实运行 RuntimeBridge 的 Executor、串行请求、异步 IPC 与 RuntimeService，
完成缺失文件 -> 显式恢复 -> 实际 atom 成功 -> Run completed -> 单独 Lesson；
随后显式新 Run/停止，以及取消恢复、迟到响应/关闭。模型调用数验证没有自动恢复。

投影测试覆盖三项并行读取的单组预算、目录 128/Run 256 上限、重复/坏状态拒绝、
单飞控制、错误目标/载荷、迟到/重复响应、断线/事件过期、默认版本绑定、取消确认
不冒充终态、完成不冒充 Lesson、关闭学习后的写入门禁。失败/不匹配控制响应使快照
失效，保留旧显示并禁用动作，不能假定失败代表零副作用。

首轮六项功能矩阵配置 native/OCR 为 OFF。随后为内存检查在 asan/ubsan 打开 native，
保留 OFF 的 OCR/资源打包/桌宠，单独构建产品和 renderer；评审后的四项回归中
ASAN/UBSAN 保持 native=ON，Debug/TSAN 仍为 OFF，测试路径均为投影/IPC。
GPU renderer 与这些功能门禁分开记录。
日志为 build/m15-native-debug-test.log、build/m15-native-PRESET-test-final.log；
首个四配置矩阵与后续控制错误收口后的最终矩阵分开保留，不用旧结果替代最终代码。

## 原生构建与渲染

```bash
cmake --preset native-debug -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_BUNDLE_VISION=OFF -DMIRAGE_ENABLE_PET_PREVIEW=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build --preset native-debug --target mirage-native native_conversation_view_test -j 4
ctest --preset native-debug -R '^native_conversation_view_test$' --output-on-failure
# ASAN/UBSAN 分别配置后构建/测试相同目标：
cmake --preset PRESET -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_ENABLE_NATIVE_FRONTEND=ON -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_BUNDLE_VISION=OFF -DMIRAGE_ENABLE_PET_PREVIEW=OFF
cmake --build --preset PRESET --target mirage-native native_conversation_view_test -j 3
ctest --preset PRESET -R '^native_conversation_view_test$' --output-on-failure
```

native-debug、ASAN、UBSAN 原生产品构建通过，renderer **各 3/3，0 skip**，
业务 binary 各 **34215 检查，0 失败**，无 sanitizer 报告；独立评审两项修正后
三配置再次重建相同 renderer，仍各 **3/3 / 34215 检查** 通过。最终日志为
build/m15-native-review-fix-test.log、build/m15-native-review-PRESET-render-test.log；没有执行 TSAN GPU renderer，
跨上下文 IPC/生命周期使用前述 TSAN 功能矩阵。字体/控件几何、原有会话回归、工作流
长名称、请求/禁用、失败/断线/终态和实际滚动均验证。工作流几何覆盖 1180×800、
860×620、明暗、侧栏展开/收起，最小窗口还启用较大字号。

最终截图从 build/native-debug/apps/native 执行：

```bash
../../tests/native_conversation_view_test /ABS/REPO/.impeccable/review/m15-workflow workflow
```

测试只捕获工作流场景，PPM 无缩放转 PNG；14 份截图和 SHA-256 清单保存在
`.impeccable/review/m15-workflow/`。8 份等待态覆盖完整尺寸/主题/侧栏矩阵，其余覆盖
完成、等待响应、断线、拒绝、滚动目录和空态。两轮集中视觉检查修正禁用样式并验证实际滚动；之后独立评审要求两项修正：
选中行次要文字对比度、拒绝提示先要求刷新。一次集中修正后重新捕获同一 14 个场景，
独立评审将两项都判定 resolved（ship，仅覆盖该修正清单）；
[首评](../../.impeccable/review/m15-workflow/finish-review.md)与
[复核](../../.impeccable/review/m15-workflow/finish-verdict.md)保留。
选中浅色行状态文字约 5.57:1、ID 12.58:1，刷新提示与可操作控件一致。
合成截图不能替代用户真实任务或在线模型验收。

## 开发期失败与修正

初次 native-debug 尚未配置而无法构建，按上述离线开关完成配置后继续。首次编译因
键盘激活 helper 缺前置声明及渲染夹具变量遮蔽失败，修正后重建；没有修改第三方。
首次真实原生链路矩阵 3/4：夹具错误地把 publish gate 的内部 DryRun 算作产品目录 Run，
实际 registry 只有显式产品 Run；修正为核对真实目录，最终矩阵通过。

首次 renderer 3/3 只检查首屏；增加真实滚动检查后暴露未绑定 ScrollView offset/onChange，
测试触发 bad_function_call 并异常退出，Xvfb 子进程保持输出管道，CTest 等待约 109 秒。
已终止本轮私有 Xvfb，补齐产品滚动位置回写、选择后回到详情、夹具回调存在性断言和
受控调用；没有修改/放宽 EUI。最终 renderer 与实际目录滚动通过。禁用按钮最初仅禁用
事件但外观仍像可操作，集中修正为既有 muted/hover 语义并加入禁用断言。

## 限制与后续

M15 整体继续 In Progress，M15-08 仅交付运行/显式恢复入口。候选审阅/编辑/导入发布、
参数编辑、自动受管资产目录、自动恢复、长期视觉工件配额/删除、真实应用变化/收益和
Windows 实跑未完成。负责人 Mirage 维护者；对应契约/产品入口/设备就绪后分别补验。
既有候选文件仍经 CLI 的显式 review/import/publish 门禁；本入口不会自动提升验证等级。

## 收口

独立复核后的改动仅为选中行前景、拒绝引导和对应回归。未使用网页 detector 评价原生
C++。文档复核保留现有视觉系统，不改 DESIGN.md 或 token sidecar，不修复无关历史漂移。
M15-20 限定增量完成；代码、测试、截图、决策、计划与产品说明按同一范围同步。

最终文档检查：14 份变更 Markdown 的 572 个本地路径链接、一级标题/代码围栏均通过；
14 份截图尺寸及 SHA-256 与清单一致，git diff --check 通过。公开头检查 67 个头、
0 新违规，DEC-096 两条既有 seam 保留；代码格式检查通过。DESIGN.md 与视觉 token
sidecar 保持不变，没有依赖指针或锁文件变更。当前新增 native 增量未提交。
