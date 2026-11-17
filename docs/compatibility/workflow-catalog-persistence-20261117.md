# 工作流目录持久化与重启恢复验收

> 状态：Completed（M15-22 Linux 限定增量；完整 M15 继续开放）
> 日期：2026-11-17
> 工作项：M15-22；依据：[DEC-151](../decisions/DEC-151-durable-workflow-catalog.md)
> 负责人：Mirage 维护者
> 基线：`26899ba` 已本地提交，未推送；本增量在同一工作树继续

## 交付范围与依赖边界

默认启用产品工作流目录文件，复用 LocalStateStore 的受限读取、私有权限和原子替换。
Mira 公开 WorkflowRuntime 提供 IR、摘要、DryRun/发布、执行和 Skill；公开库记录没有
产品定义正文的目录恢复接口，因此产品索引仍由 Mirage 管理，不修改依赖或另建执行库。
JSON 编解码使用 Mira，执行注册使用原有 Mira Skill；串行服务 owner 与 kairo DoubleBuffer
保持原路径，无新线程、队列或后台轮询。

文件 schema 1 的每条记录分离原始文本 head 与最后发布定义对象，带各自摘要、head
发布状态、callable 和更新时间。保留 head 原字节，维持 workflow.get 契约；读取时由 Mira
重算身份与摘要。最多128个资产，head/published 每份各256KiB，总文件72MiB，任一预算
先到即拒绝；服务期已确认发布摘要最多1024个，重启只恢复最后发布版本。

默认位置是 session state 根下 `workflows/workflow-catalog.json`；可独立设置
`workflow_state_directory`，显式 `persist_workflow_state=false` 仅保留服务期目录。
学习元数据仍位于独立 Mira SQLite owner，目录不携带原始截图、Run 或模型尝试。
IR 参数随用户定义私有保存，目录不是秘密识别或擦除设施。

## 提交与恢复契约

保存和导入只建立产品草稿，不把 NotValidated 记录覆盖到同摘要的 Mira 可执行发布版本。
发布先验证 DAG 和 Mira DryRun、准备不可变 handler；文件提交后才激活入口和交换可见
投影。写入失败不确认成功，旧内存目录与调用入口保持；服务显式执行仅接纳已确认摘要，
防止预验证成功而写盘失败的版本在本服务期被执行。持久提交后若入口激活失败，工作流
目录读写和新运行返回 unavailable，要求重启；已有 Run 仍可查询/取消。

LocalStateStore 通过临时文件、文件同步、rename 和目录同步保存。其错误回执不代表
磁盘文件必然未变化（例如 rename 后目录同步失败）；这类持久性不确定窗口沿用底层语义，
不宣称崩溃注入/断电保证。本轮强制写失败覆盖的是提交前目录不可写，检查旧文件和内存
不变。重启以磁盘上完整且通过校验的快照为准，始终不自动执行动作。

启动开放 IPC 前完整校验文件、身份、预算、已发布依赖和环路，按被调用目标在先
重新 DryRun/发布，再恢复编辑 head。草稿可以包含尚未发布的循环，已发布调用图不可有环。
缺文件是空目录，损坏/超限/悬空依赖/重新验证失败则启动失败并保留文件，不修剪或覆盖。
重启不恢复 Run、历史版本、活动任务或模型尝试；零 Strict 运行和零桌面动作。

工作流仍为独立资产，调用最新发布目标；未发布编辑不撤销最后发布目标，调用方无需重发。
删除被其他已发布定义调用的目标会被拒绝；删除成功后入口失效且不再出现在 atom 目录。
内部注册表无注销面，使用空快照撤销，服务隐藏此类条目；现有每进程调用注册容量仍有界。

## 功能验证

四配置沿用离线缓存：native-debug/ASAN/UBSAN native=ON，TSAN native=OFF；fetch、OCR、
vision bundle、pet 均 OFF。在仓库运行，PRESET 替换为四配置之一；TSAN 的 ctest 前加
`setarch x86_64 -R`。ASAN/UBSAN 同时构建并在过滤器包含 native_conversation_view_test：

```sh
cmake --build --preset PRESET -j 3 --target workflow_catalog_store_test workflow_catalog_service_test workflow_composition_service_test native_workflow_editor_test native_workflow_integration_test ipc_protocol_test runtime_service_test session_client_test
ctest --preset PRESET -R '^(workflow_catalog_store_test|workflow_catalog_service_test|workflow_composition_service_test|native_workflow_editor_test|native_workflow_integration_test|ipc_protocol_test|runtime_service_test|session_client_test)$' --output-on-failure
```

| 配置 | 完整功能矩阵 | 本地日志 |
| --- | --- | --- |
| native-debug | 10/10 | `build/m15-catalog-accept-test.log` |
| ASAN | 含真实 renderer，11/11 | `build/m15-catalog-asan-test.log` |
| UBSAN | 含真实 renderer，11/11 | `build/m15-catalog-ubsan-test.log` |
| TSAN | 功能10/10，不含GPU renderer | `build/m15-catalog-tsan-test.log` |

计数含两个状态隔离 fixture，均0 skip。测试包括全 RuntimeService 停止/重开、原始 head
字节往返、草稿/发布分离、调用链按依赖恢复（被调用目标 ID 字典序反在后）、更新后旧
调用方执行新版、循环拒绝、删除依赖保护及删除后重启、导入草稿跨重启、文件损坏/稀疏
超限拒绝、显式关闭持久化、发布/保存/删除/导入写失败、内部未提交摘要不可执行。
虚拟文件 Provider 计数和空 Run 列表证明启动没有 Strict 运行或动作；不是用户桌面实跑。
旧调用在更新时持有原快照、取消和关闭另由原 composition/native/service 门禁回归。

最后字节预算诊断改动后的存储/服务复验：ASAN/UBSAN 各5/5（含renderer及隔离fixture），
TSAN 2/2，日志 `build/m15-catalog-final-PRESET-test.log`。最终能力三态/协议补测 Debug6/6、ASAN/UBSAN各5/5、TSAN4/4，日志为
`build/m15-catalog-final-debug-test.log` 与 `build/m15-catalog-copy-PRESET-test.log`。
均0 skip；没有扩大为完整 M15 验收。

开发期修正包括：测试 fixture 同名、可选强ID转换、TempDir 链接 IPC、include 格式；
临时响应子对象 range-for 的 C++20 生命周期问题修复为显式持有响应；启动失败按既有
report.clean 语义检查，不误归因为泄漏。一次构建失败后误执行旧测试产物/缺失测试，
不作为最终结论。session_client 的原始 JSON 读取断言揭示字节契约，改存 head 文本；
草稿默认运行预期由 pinned_runtime 调整为 invalid_state，见 DEC-151。末轮构建一度
使用不存在的边界目标而未执行，已改为 mirage-boundary-check。历史本地日志保留，
以上成功矩阵及最后受影响复验才是最终取证。

## 原生提示与视觉证据

safe_workflow_editor 与 durable_catalog 独立；编辑器另存 capability_confirmed，初始/断线
状态不能当成已确认非持久化。已确认持久服务显示“已保存的工作流可在服务重启后恢复”，
已确认旧/关闭服务显示“当前目录不跨服务重启保留”，未知显示“持久化状态待服务确认”。
保存确认仍要求验证发布后才可运行，不暗示未保存编辑或旧 Run 已存盘。

沿用 EUI/OpenGL 产品 renderer、隔离 Xvfb、随产品字体和合成状态，不请求模型、不操作
用户桌面。明暗×1180/860四幅持久状态、一幅860旧能力、一幅未知能力的 PNG/hash 见
[证据包](../../.impeccable/review/m15-workflow-storage/finish-packet.md)。最终 renderer **41426 检查、0失败**（`build/m15-catalog-final-render.log`）。
产品 mirage-native/mirage-service/mirage、格式和公开头边界构建通过，68头0新违规，
2个既有 DEC-096 seam，见 `build/m15-catalog-final-build.log`。不改设计系统、
配色或图布局，无新位图素材；Web HTML/CSS detector 不适用。

重捕在 build/native-debug/apps/native 运行（替换仓库绝对路径）：

```sh
../../tests/native_conversation_view_test /ABS/REPO/.impeccable/review/m15-workflow-storage workflow
```

PPM 经 Pillow 无损转 PNG，保留六个必需场景，尺寸/SHA256 在 captures.json；临时PPM
和编译日志不进入提交。首评仅发现未知能力提示 F1，修正独立确认状态并增加模型/渲染
断言；最终[限定评分](../../.impeccable/review/m15-workflow-storage/finish-verdict.md)为 ship，唯一F1 resolved；
[文档交接](../../.impeccable/review/m15-workflow-storage/documentation-review.md)已同步当前能力，
六图尺寸/hash均通过。评分只覆盖F1，不将图片当存储正确性的替代证明。

## 未执行范围与补跑责任

Windows 实跑、断电/故障注入、真实桌面条件变化、在线模型监督恢复收益、原始视觉工件
长期配额和完整候选审阅均未由本轮证明。负责人 Mirage 维护者；Windows 需相应构建/
文件系统和桌面环境，故障注入需单独私有存储夹具，在线收益需授权的真实任务和配置模型。
具备环境后按 M15-03～09 与完整退出矩阵补跑。此前受控在线学习证据仍仅证明其原范围；
本轮无模型调用或依赖升级。完整 M15 保持 In Progress。

## PR #127 全量 CI 回归修正（2026-11-17）

首次远端 [CI 43998355576](https://github.com/Linductor-alkaid/mirage/actions/runs/43998355576)
在8920dc2的Debug/Release/ASAN/UBSAN/TSAN均出现相同两项失败；此前专项通过不覆盖这两个
既有消费方，本段保留原失败。BUG-20261117-001：默认持久目录启用后的测试隔离/重启
预期未同步；负责人 Mirage 维护者。

`tray_runtime_test` 的各场景只关闭旧持久化项、未指定工作流路径，带SlowClipboard的场景
留下已发布定义；后续纯Environment场景误加载它，因缺少原子Provider而重验失败。修正
共享config夹具，使每个TempDir拥有独立workflow_state_directory，保持默认持久化开启，
不放宽服务启动的依赖校验。`workflow_experience_cli_test` 仍断言同路径服务重启后目录
为空；修正为先验证整个目录和三份定义恢复、旧Run为空，再显式删除源夹具定义，继续
验证候选文件导入、损坏/冲突拒绝、发布和实际运行。没有关闭持久化或删除恢复门禁。

修正只影响测试与取证，不改变产品契约、依赖pin或原生布局。四配置受影响回归
Debug/ASAN/UBSAN/TSAN各6/6通过（含两项隔离fixture），日志为
`build/pr127-final-PRESET-test.log`，PRESET取native-debug/asan/ubsan/tsan。覆盖tray、
经验CLI、catalog service与composition service；clang-format与diff检查通过。开发期
新增CLI断言曾错比源流程的初始定义，测试已有步骤已发布revised版本；改为验证最新
修订并额外验证归纳定义后通过。原产品实现未改。首轮CI失败保留，最新远端验证以
[PR #127检查](https://github.com/Linductor-alkaid/mirage/pull/127/checks)为事实源。
完整M15的未执行边界不变。
