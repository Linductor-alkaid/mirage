# Host 显式 Workflow Agent 恢复（M15-15）

[DEC-144](../decisions/DEC-144-host-workflow-recovery.md) 将 Mira 的恢复编排接到
[MiraHost](../../runtime/mira_host/include/mirage/runtime/mira_host.hpp)。这是受控宿主接口，
当前 Runtime Service、IPC/CLI、native 和自动失败触发尚未接线。

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

受控测试验证修复后实际 atom 续跑，再从完成的 Run 编译出修正后的经验候选。这里没有
装配长期 Memory/Lesson，也没有在线模型、真实桌面变化/接管或 Windows 验收；当前范围
和实际测试结果见 [M15](../plans/m15-supervised-rpa-experience.md)。
