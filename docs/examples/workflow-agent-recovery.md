# Workflow Agent 显式恢复：Host 与产品服务

[DEC-144](../decisions/DEC-144-host-workflow-recovery.md) 将 Mira 的恢复编排接到
[MiraHost](../../runtime/mira_host/include/mirage/runtime/mira_host.hpp)。这是 M15-15 的受控宿主接口；
M15-19 已增加文末的产品 IPC/CLI 接线，native 和自动失败触发仍开放。

## 装配和调用

外部 owner 先创建使用 Workflow 同一 Executor 的 ModelGateway，配置有效的 profile、
provider、传输及 token/费用预算，并设置同一 WorkflowEventBridge。Gateway 仅路由该
profile；装配期和恢复期间不能并发重配、共享原生会话或绕开 ModelLayer 的推理串行化。
Host 不读取 ModelLayer 私有 gateway，也不创建第二套模型栈。

在已启动 Host 并 attach_workflow_surface 后，绑定外部模型：

```cpp
auto model = std::make_shared<mirage::integration::WorkflowRecoveryModel>();
model->gateway = shared_gateway;
model->profile_id = configured_profile_id;
model->provider = configured_provider;
mirage::runtime::WorkflowRecoveryOptions options;
options.model_call_deadline = std::chrono::seconds{5};
options.max_decision_repairs = 0;
options.max_tracked_runs = 32;
auto attached = host.attach_workflow_recovery(model, options);
if (!attached.ok) {
    return attached;
}
// run_id 必须是当前 surface 中已经进入 WaitingAgent 的 Run。
auto admitted = host.start_workflow_recovery(run_id);
if (!admitted.ok) {
    return admitted;
}
```

以上为装配片段；完整可运行的受控例子见
[Host 测试](../../tests/runtime/workflow_recovery_host_test.cpp)。每个 Host 只绑定一次，
每个 Run 只启动一次显式恢复，同时最多一个尝试；请求期限限 1ms～30s，尝试内决策
修复限 0～1 次，跟踪条数限 1～32。关闭/重绑或新建 Run 不能用来重置恢复预算。

由现有宿主驱动调用 poll_workflow_recovery(run_id, after_ordinal)，每次至多等待 1ms。
先检查 ok，再检查 settled；收到回执后把 ordinal 保存为游标。未返回新回执只表示
目前没有新的已结算结果。patched-and-resumed / resumed-without-patch 也不证明任务
完成，仍须查询 workflow_run_view，或消费实际终态事件。模型决策、补丁校验、权限与
副作用检查、epoch 和审计均沿 Mira；不要根据文字理由自行执行桌面动作。

## 取消、故障与关闭

cancel_workflow_recovery 只停止模型尝试；cancel_workflow_run 同时请求取消恢复与 Run，
终态不能被迟到回答复活。关闭先停止恢复生产者，检查 shutdown_workflow_surface 和
host.shutdown 的结果及 clean 报告，再关闭外部 Gateway/Store，最后由非 worker owner
关闭 Executor。关闭 surface 后查询 Run 会拒绝；可用已记录的终态事件核验关闭结果。
不要从 Executor worker 自等待或销毁仍被恢复引用的环境/工具。

故障注入复现了 [MIRA-20261112-002](../dependency_feedback/mira.md#mira-20261112-002)
的 provider/future 异常问题。单一 integration Adapter 捕获 provider 异常、固定错误
正文，保留返回错误的身份和重试属性；它不是任意框架内部异常的通用保护。上游修复并
授权升级后移除此边界。[MIRA-20261112-003](../dependency_feedback/mira.md#mira-20261112-003)
的旧回执问题使多轮恢复保持关闭；游标不能修复上游尝试身份语义。

## 会话内 Episode 与恢复 Lesson（M15-16）

[DEC-145](../decisions/DEC-145-host-workflow-learning.md) 支持在创建 surface 时可选注入
[共享 Memory binding](../../integration/mira/include/mirage/integration/workflow_learning_memory.hpp)：

```cpp
auto learning = std::make_shared<mirage::integration::WorkflowLearningMemory>();
learning->memory = shared_memory; // 已装配在同一 Executor 上的有界后端
auto surface = host.attach_workflow_surface(executor, event_bridge, atom_toolset, learning);
if (!surface.ok) {
    return surface;
}
// 随后 attach_workflow_recovery；省略 learning 参数时学习保持关闭。
```

Host 将 scope 固定为 primary Session ID，不接受外部 scope，不为宿主创建存储。Mira
负责非 DryRun 终态 Episode、同 scope 失败检索与审计。检索条数/token/deadline 沿 Mira
暂定默认值 8/1024/250ms，只约束一次检索，不能当作磁盘总量或长期保留策略。

start_workflow_recovery 无论是否启用学习，均至多等 1ms 确认当前 Workflow 驱动已结算
（包括 carrier epoch 与可选 Lesson 检索）；尚未就绪返回明确错误且不消耗模型尝试。
WaitingAgent 快照可能先于检索完成；此时返回 invalid_state 且没有启动模型，交现有
宿主下一步再次检查，不循环盲重试、不新建后台轮询任务。已结算的恢复仍受单次限制。

修复后的 Run 真正进入 Completed 后，宿主可显式调用：

```cpp
auto learned = host.record_workflow_recovery_lesson(run_id);
if (!learned.ok) {
    // 错误说明已固定脱敏；写入可能已经发生，不据此删除或生成新的记忆身份。
    // 需要复核/重试时，沿同一 Run 的 Mira 幂等语义。
    return learned;
}
// learned.lesson_id / learned.digest 只确认本次记录；不代表通用技能或可自动发布。
```

未恢复、未完成、DryRun、未装学习或 surface 关闭均拒绝 Lesson。终态 Episode 写失败
只记 failed 审计，不改写任务终态；查询失败降级为空上下文，允许继续等待 Agent。
检索经验只作数据，不能绕过当前权限、取消、epoch 或不确定副作用判定。

外部 Memory 必须活到 Host 排空完成；先关闭 Host，再关闭 Memory，最后关闭 Executor。
M15-16 首增量验证了真实隔离 SQLite 的关闭/重开、会话作用域和显式擦除；后续
M15-17 / DEC-146 已接产品默认 owner 和固定本地用户 Agent scope，见下节。

受控测试验证修复后实际 atom 续跑、生成修正候选和显式 Lesson，下一次同签名失败能
召回 Lesson 并进入模型上下文。M15-16 当时未执行在线模型，后由 M15-17 补限定证据；
真实桌面变化/接管和 Windows 仍未验收；
当前范围和实际测试结果见 [M15](../plans/m15-supervised-rpa-experience.md)。

## 默认持久化与在线验收（M15-17）

产品 Runtime Service 默认使用 WorkflowLearningStore 打开 state/workflow-learning 下
的私有 SQLite；64 MiB 是数据库/WAL/SHM/journal 文件长度之和，4 MiB 留作维护。它不
等于文件系统物理块/目录开销，也不覆盖视觉工件或完整 M31。设置
`runtime.workflow_learning_enabled:false` 后不打开该库；打开失败不偷偷换临时数据库。
Store 必须在 Workflow 排空后关闭，关闭实际连接后才关闭 Executor。

直接 Host 省略 learning 仍保持未绑定；仅受信任的产品 owner 显式选择固定 Agent
subject。用户私有目录划定 OS 用户边界，历史 Lesson 不代替当前动作权限或现场观察。

恢复模式默认 `strict_json_schema`；新 pin 允许显式 `text`/`json_object`，仍执行同一
本地 JSON schema、决策和补丁校验。Messages 选择 text；无自动协议降级。单次生成
默认 2048 token（128～8192），thinking 可为空、none 或 adaptive，须符合模型能力。
Host 等驱动完成后才记录 Lesson；invalid_state 的前置拒绝尚未写入，其他写入错误可能
已提交，沿同 Run 幂等语义复核。

手动在线程序只在 MIRAGE_BUILD_MANUAL_ACCEPTANCE=ON 时构建，不加入 CTest。使用
当前配置模型与钥匙环，在指定私有目录运行两次受控修复，再用 --inspect 另起进程确认
持久记录；不向用户真实桌面派发动作，不保存凭据/原始回复。实际失败轮次、token 和
作用域见 [在线验收](../compatibility/workflow-learning-20261114.md)。


## 查看学习存储（M15-18）

```sh
mirage workflow learning --socket /path/to/service.sock
```

只读 IPC workflow.learning.status 返回 learning 对象：enabled、file_bytes、limit_bytes、
reserve_bytes、peak_admitted_file_bytes。未装配时为 false 和四个零，不打开库；读取失败
返回 unavailable，不冒充空库。这里的进程峰值是 Mira 已接纳文件长度快照，不是物理磁盘
或历史全生命周期峰值。库的路径、主体、记忆正文均不通过这个入口返回。旧服务不支持
此操作时明确失败；此命令没有写入或清理选项。契约见
[DEC-147](../decisions/DEC-147-workflow-learning-status.md)。

## 产品服务显式恢复（M15-19）

服务已配置模型，并且 `mirage workflow runs` 中的 Run 处于 waiting_agent 时：

```bash
mirage workflow recovery start RUN_ID --socket /path/to/service.sock
mirage workflow recovery status RUN_ID --socket /path/to/service.sock
mirage workflow runs --socket /path/to/service.sock
# 确认 Run 实际 completed 后，单独保存成功恢复经验：
mirage workflow recovery lesson RUN_ID --socket /path/to/service.sock
# 等待期间可显式取消模型尝试；Run 仍留待用户决定：
mirage workflow recovery cancel RUN_ID --socket /path/to/service.sock
```

命令输出完整 JSON，坏响应、旧服务、不匹配身份或输出失败均非零。start 只确认接纳；
status.settled 只表示模型尝试结算，Run 结果仍读 workflow runs。驱动尚在结算时可能
明确拒绝 start/lesson；用户查看状态后可再次显式调用，不自动消耗额外模型调用。
失败 Lesson 写入独立报错，不能据 Run 成功推断写入成功。

复用当前配置及 keyring 模型栈。恢复采用 Text 严格本地解析、thinking=none、30 秒、
2048 输出 tokens、每 Run 一次；权限和动作验证沿既有 Workflow。尝试未结算前聊天被拒绝，
取消后仍须等待结算。**首次绑定恢复后，切换模型需要重启服务**，包括已结算或取消后；
已存在 Run 不跨服务重启恢复。此限制用于保留当前编排器的尝试预算，见
[DEC-148](../decisions/DEC-148-service-workflow-recovery.md)。未接 native/自动恢复。
