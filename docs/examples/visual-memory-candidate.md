# 动作前后图的视觉记忆候选（M15-13）

[DEC-142](../decisions/DEC-142-visual-action-evidence-candidate.md) 提供 integration 层
[候选接口](../../integration/mira/include/mirage/integration/visual_memory.hpp)。当前可验证
图像打包、Mira 记忆契约适配和隔离存储重开；Runtime Service、CLI/native 与自动采集
尚未装配，普通 Workflow Run 也不会因此自动多出历史截图。

## 数据与宿主责任

宿主在已经授权的同一操作中取得前后两帧，提供真实 session/task/operation、来源事件、
task epoch、同一授权/环境代次、应用/页面描述与明确有效期。适配器只校验这些宿主输入
的结构和一致性，不验证事件是否真的已写入 EventStore，也没有屏幕内容秘密检测器。
宿主必须从可信捕获/事件路径供给，不能把模型文字或导入文件当成已观察事实。

前后图按紧密 BGRA8 打包，行 padding 被丢弃；单工件同时覆盖来源、元数据和像素，
不创建两张图片的嵌套引用。MemoryRecord 为 Task scope / Episode / Observed，statement
只包含视觉工件摘要；应用/页面文字和原图在 Sensitive 工件中。Mira 禁止 Secret 记忆，
明确分类为 Secret 的输入直接拒绝，不降低标签继续保存。Observed 只表示宿主提供了
这对观察，不等于点击成功、页面未变、模型理解正确或下次可直接重放。

操作预算需调用方显式给出：单帧最多沿既有 capture 上限 8 MiB，完整工件最多 16 MiB，
manifest 最多 8 KiB；前后间隔/新鲜度必须为正且不超过 120 秒。它们约束单次操作，
不是磁盘总量、并发峰值或默认保留期；启用产品前仍需 M15-04/06 的部署预算和 owner。

## 调用次序

在现有 Executor 管理的操作上下文中，使用外部 owner 注入的同一套工件/记忆接口：

```cpp
// observed: 已授权、已获得真实来源的 VisualActionEvidence。
// limits: owner 在操作准入前确定的 VisualMemoryLimits。
auto bytes = mirage::integration::prepare_visual_action_evidence(observed, limits, context);
if (!bytes) {
    return bytes.error();
}
// owner 先取得全局/主体/任务配额预留，再调用 ArtifactStore begin/write/commit。
// spec: kVisualMemoryMediaType / Binary / Sensitive / content_schema={1,0}。
// committed_descriptor 是该次真实 commit 的结果，不能自行构造身份来冒充。
auto candidate = mirage::integration::make_visual_memory_record(
    stable_memory_id, committed_descriptor, shared_artifacts, limits, context);
if (!candidate) {
    // owner 处理失败与可能的孤儿工件；不能随意 erase 共享正文。
    return candidate.error();
}
// owner 显式提交 IMemory::apply（稳定 mutation id），检查结果后才算记忆已接纳。
```

准备函数不写存储，绑定函数只读取并检查工件；返回候选不代表 apply 成功。外部 owner
必须持有容量预留、幂等 ID、取消/关闭句柄和失败清理状态；自动重启扫描、孤儿回收、
共享引用删除、全局容量和崩溃恢复尚未装配，不能直接把示例作为产品持久化 owner。

## 有限写入接线（M15-14）

已有 Mira ContextMemorySupervisor 和共享 Store 的宿主可使用
[排定接口](../../integration/mira/include/mirage/integration/visual_memory_write.hpp)，
按 [DEC-143](../decisions/DEC-143-supervised-visual-memory-write.md) 推进工件提交、来源
事件回执/回读和 Memory apply。输入仍须是宿主真实授权/分类后的观察，长期存储准入
不能由这个有限操作接口代替。稳定 memory/mutation/event ID 必须始终绑定同一份候选，
正文改变要另行准入，不能复用原幂等键。

```cpp
auto completion = mirage::integration::schedule_visual_memory_write(
    supervisor, shared_events, shared_artifacts, shared_memory,
    runtime_id, stable_memory_id, stable_mutation_id,
    std::make_shared<const mirage::integration::VisualActionEvidence>(std::move(observed)),
    limits, context);
// 在外部非 worker owner 上消费，不能在 Supervisor/Store worker 中自等待。
auto result = completion.get();
if (!result) {
    // 仅 error.domain == "mirage.visual_memory.write" 时，domain_code 是写入阶段。
    // diagnostic_artifact 是已知工件 ID；错误不代表未写入，须交 owner 复核。
    return result.error();
}
// result.value().mutation 是实际接纳回执；completed_after_cancel 标记 apply 期间取消。
```

事件 mirage.visual_evidence_recorded.v1 只含来源关联与工件身份，不含应用/页面/像素。
收到 Buffered、错误身份/序号或无法按序号回读的回执，不进入 Memory。Supervisor
层满容量/关闭直接返回其原错误域和错误码；已进入业务的失败保留阶段和已知工件 ID，
底层异常正文不进入诊断。commit/apply 报错或异常可能已写入，禁止自动重试、回滚或
erase 共享工件；清理/重启恢复仍交外部 owner。apply 已成功时即使任务同时取消，
仍报告实际写入与 completed_after_cancel，不把已取消的上层任务恢复为活动。

关停先停止生产者，调用 begin_shutdown 并消费所有 futures、确认关键排空完成，再
关闭 SQLite/其他 Store，最后关闭唯一 Executor。Store 与取消 probe 的 owner 必须
活到 future 和排空结束。在途 max_in_flight 不是持久配额；已登记
[MIRA-20261112-001](../dependency_feedback/mira.md#mira-20261112-001)，产品长期 owner
仍需上游总量准入与恢复契约。

## 读取与失效

先以当前授权构造 scope，调用 Mira get/query；再用 `review_visual_memory` 校验记录与
工件。它核对 scope、类型、等级、来源、有效期、敏感度、整体/每帧摘要和所有偏移。
跨 Task、已擦除/过期/篡改/缺失的工件、导入来源或被提升验证等级的记录均不能返回图。
底层读取失败保留错误码，诊断使用固定 reason，不复制存储错误正文。

返回 `VisualMemoryReview` 的两张历史图，source_sequence=0，没有实时 scope/编号/
引用、窗口坐标或授权 token。它们可用于历史审阅和未来定位候选，执行仍须新观察、
唯一定位及既有权限/ActionCoordinator 门禁。旧记忆不能作为当前页面或动作后成功证据。

## 本机验证

```bash
cmake --build --preset debug --target visual_memory_test
ctest --preset debug -R '^visual_memory_test$' --output-on-failure
```

[测试](../../tests/integration/visual_memory_test.cpp) 使用小型受控图与独占临时目录，
实际接入 FileArtifactStore 和 SqliteMemoryStore，验证显式写入、两存储关闭重开、
Task scope 查询/隔离、再次读取、擦除后重开仍不可用以及工件总量拒绝。SQLite worker
沿唯一测试 Executor 管理并在 shutdown 前 close。不使用用户桌面/配置或在线模型；
这里是进程内重建存储对象，不是完整服务重启、断电、共享工件删除或产品收益验收。

范围与未完成工作见 [M15](../plans/m15-supervised-rpa-experience.md)。

写入链路专项为 visual_memory_write_test，使用真实事件/工件/SQLite 与故障代理验证
部分写入、假回执、取消/超时、接纳拒绝和关闭。公开接口微配额 probe 在正文上限 1
字节下，8 次擦除后目录文件长度和为 1760 字节，16 次后为 3520 字节；不包含文件系统
块分配、可回用空间、SQLite/WAL 规模或生产总占用/峰值基准。
