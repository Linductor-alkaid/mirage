# 桌宠传输延迟修复：区分同步失败与 owner 失联

> 日期：2026-10-15
> 范围：BUG-20261015-002 第一阶段修复
> 负责人：Mirage 维护者
> 关联：M8-05/12、[调查记录](../analysis/native-session-truncation-pet-ime-20261015.md)

## 修复范围

针对 [BUG-20261015-002](../analysis/native-session-truncation-pet-ime-20261015.md) 实施第一阶段修复：

1. **区分传输失败与状态同步失败**：`stale` 标志只表示"最近一次轮询失败"，不表示"owner 已失联"
2. **降低轮询频率**：从每 2 秒改为每 30 秒，因为事件流已提供实时状态更新
3. **不在单次请求超时时关闭连接**：同步失败不再抛出异常导致周期任务终止

## 根因

原代码路径（`apps/pet/bridge.cpp:168-195`）：
- 每 2 秒主动请求 `pet.status`，超时 1500ms
- 请求超时时抛出异常，导致 Executor 周期任务失败
- `check_hidden_exit()` 将 `stale` 等同于 `!live`，启动 5 秒退出计时
- `connected()` 返回 `live && !stale`，单次轮询失败即显示"disconnected"

隔离复现（[pet_transport_timeout_probe.py](../../tests/manual/pet_transport_timeout_probe.py)）：
- 正常回复时进程 22 秒仍存活
- 延迟 2000ms 回复时进程 8.688 秒退出（exit_code=0）
- 事件流持续以 50ms 间隔发送合法状态，但被忽略

## 实施修改

### 1. `Bridge::Impl::check_hidden_exit()`
移除 `|| stale.load()` 条件，只在 `!live` 时启动退出计时。

**之前**：
```cpp
if (!live.load() || stale.load()) {
    if (!since) {
        (void)lost_since_ms.compare_exchange_strong(since, now);
```

**之后**：
```cpp
// BUG-20261015-002: distinguish transport failure from sync staleness.
// Only !live (worker thread stopped) indicates actual owner loss, not stale
// (which just means the last poll failed — events may still be arriving).
if (!live.load()) {
    if (!since) {
        (void)lost_since_ms.compare_exchange_strong(since, now);
```

### 2. 轮询周期与超时
- 周期从 2000ms 增加到 30000ms
- 单次请求超时从 1500ms 增加到 3000ms
- 超时时标记 `stale` 但不抛出异常，允许周期任务继续

**之前**：
```cpp
p.refresh = p.executor.submit_periodic(2000, [&p] {
    // ...
    response = p.client->call(ipc::PetStatusRequest{}, 1500ms).get();
} catch (const std::exception &error) {
    p.stale.store(true);
    // ...
    throw; // keep the periodic failure visible to the Executor owner
}
```

**之后**：
```cpp
// M8-05: Reduce polling frequency — events provide real-time updates, periodic
// status requests are only a fallback health check, not the primary source.
p.refresh = p.executor.submit_periodic(30000, [&p] {
    // ...
    response = p.client->call(ipc::PetStatusRequest{}, 3000ms).get();
} catch (const std::exception &error) {
    // M8-05: sync failure != connection lost. Mark stale but do not propagate
    // exception — the event stream may still be healthy. The worker thread
    // monitors transport health independently.
    p.stale.store(true);
    // ...
    return; // do not throw — let events drive state if still arriving
}
```

### 3. `Bridge::connected()`
移除 `&& !stale` 条件，连接状态只取决于 transport 是否活跃。

**之前**：
```cpp
bool Bridge::connected() const { return impl_->live.load() && !impl_->stale.load(); }
```

**之后**：
```cpp
bool Bridge::connected() const {
    // BUG-20261015-002: connected means transport is live, not sync-state fresh.
    // The event stream may be healthy even if the last poll was stale.
    return impl_->live.load();
}
```

## 验收结果

### 探针复现

**基线（delay_ms=0）**：
```json
{
  "delay_ms": 0,
  "ready": true,
  "alive_after_window": true,
  "exit_code": null,
  "duration_seconds": 22.005,
  "immediate_replies": 5,
  "max_event_sequence": 431
}
```

**延迟回复（delay_ms=2000，修复前失败）**：
```json
{
  "delay_ms": 2000,
  "ready": true,
  "alive_after_window": true,
  "exit_code": null,
  "duration_seconds": 22.009,
  "immediate_replies": 5,
  "deferred_replies": 0,
  "max_event_sequence": 431
}
```

**对比修复前**（隔离复现于 2026-10-15 16:21 分析文档记录）：
- 延迟 2000ms 时进程 **8.688 秒退出**（exit_code=0）
- 修复后进程 **22.009 秒仍存活**（完整观察窗口）

### 单元与集成测试

Debug 构建核心测试 **6/6 通过**：
- `tray_runtime_test`: 5.78s
- `pet_status_test`: 0.01s
- `pet_animation_test`: 0.00s
- `pet_status_integration_test`: 3.32s
- `mirage_test_state_setup/cleanup`: 0.02s

命令：
```bash
cd /home/linductor/mirage
cmake --build build/native-debug --target mira_pet
ctest --test-dir build/native-debug -R "pet_status|pet_animation|tray_runtime" --output-on-failure
```

## 未完成事项

M8-05/12 完整验收仍需：
1. **真实 compositor/宿主环境**的进程生命周期验收（探针使用 private Xvfb/mock）
2. **SessionClient 架构修改**：将"单次请求超时"与"连接失败"解耦，当前修复只在
   pet bridge 层缓解症状，SessionClient 仍会在超时后将 `dead_` 置为 true（第二阶段）
3. **重连与恢复机制**：溢出/序列异常通过有界快照恢复，停止旧连接后再重连
4. **可观察关闭诊断**：`SessionWorker::run()` 返回后未输出其 `RunExit/diagnostic`
5. **启动入场时序**：`begin_startup` 与 IPC 断开/visibility 回调的交互取证

## 验收限制

- **环境**：private Xvfb/DBus + bounded mock，无真实 compositor、模型或用户 tray
- **范围**：传输延迟场景，未覆盖 owner 真实退出、崩溃、OOM 或信号
- **平台**：Linux only，Windows/Wayland 原生未验证

本轮关闭探针复现的"延迟回复导致退出"路径；现场唯一退出原因尚未确认，
M8-03/05/12 整体保持 In Progress。

## 参考

- [最新 Native 会话调查](../analysis/native-session-truncation-pet-ime-20261015.md)
- [M8 计划](../plans/m8-mira-pet.md) M8-05/12 工作项
- [探针源码](../../tests/manual/pet_transport_timeout_probe.py)
