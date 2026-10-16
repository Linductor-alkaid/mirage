# DEC-094：SessionClient 超时恢复与失败诊断

- 编号：DEC-094
- 状态：**Proposed**
- 负责：Mirage 维护者
- 时间：2026-10-15

2026-10-16 / CI 跟进：在 M7-23 分支复用维护分支已提交的 `bb3d083` / `b15d728`
超时恢复修复，未纳入其他整改。请求通过 kairo MpscChannel 传递；总量准入 16
覆盖 pending 与 abandoned，拒绝时返回 unavailable。超时帧在发送前跳过，已发送
帧的迟到响应只释放单未决位置；真实未知 id 仍关闭连接。run() 不再与 call()
竞争超时语义，排队帧跨 readiness slice 保留。新增活跃超时、迟到响应/事件、排队
恢复、过载拒绝、停止与传输失败回归。重连策略仍待评审，DEC 状态维持 Proposed。

> 替代/被替代：本记录**扩展** [DEC-012](DEC-012-ipc-event-subscription-and-wire-schema.md)
> 的单连接单未决请求纪律，不改变该纪律本身，但允许客户端在请求超时后选择性恢复连接。

## 背景与问题

[BUG-20261015-002](../analysis/native-session-truncation-pet-ime-20261015.md) 揭示：
当 `SessionClient::call()` 的单次请求超时时，当前实现立即将整个 session 标记为
`dead_`（第 137 行），导致：

1. **过度反应**：即使事件流仍在正常工作，单次请求超时也会关闭整个连接
2. **混淆概念**：
   - 请求超时 ≠ 连接失败
   - 同步状态失败 ≠ owner 失联
   - transport 异常 ≠ 暂时的响应延迟
3. **无法恢复**：`dead_` 标志一旦设置，该 SessionClient 实例永远不可用
4. **诊断缺失**：`SessionWorker::run()` 返回的 `RunExit` 和 `diagnostic` 未被消费，
   用户只能看到"unexpected exit"

[第一阶段修复](../compatibility/pet-transport-staleness-fix-20261015.md)（commit 944afcf）
在 bridge 层缓解了症状：降低轮询频率、区分 `live`/`stale`、超时不关闭连接。但
SessionClient 层仍将请求超时等同于连接死亡，这是架构上的过度耦合。

## 决策

### 1. 区分三种失败模式

SessionClient 必须区分以下三种失败：

| 失败类型 | 含义 | 当前行为 | 是否可恢复 | 应该如何处理 |
|---------|------|---------|---------|------------|
| **RequestTimeout** | 单次请求超时，transport 可能仍正常 | 标记 `dead_`，连接关闭 | 是（事件流可能仍在） | 标记 `sync_stale`，允许后续重连 |
| **TransportFailure** | wire read/write 失败，连接已断 | 标记 `dead_`，返回 ConnectionLost | 是（可重连） | 返回 ConnectionLost + diagnostic |
| **ExplicitStop** | 外部调用 `stop()` 明确停止 | 标记 `stop_requested_` | 否（正常关闭） | 返回 Stopped |

### 2. SessionClient 状态机修订

新增状态标志（不破坏现有 `connected_` / `dead_` 语义，逐步迁移）：

```cpp
enum class Health {
    Healthy,        // 连接正常，同步状态正常
    SyncStale,      // 最近的请求超时，但 transport 可能仍工作
    Disconnected    // transport 失败或显式关闭
};
```

- `Healthy`：所有请求都在期限内完成，事件正常到达
- `SyncStale`：最近的请求超时，但 `run()` 循环仍在运行（事件可能还在到达）
- `Disconnected`：`run()` 已退出或 `stop()` 被调用

### 3. 超时处理变更

#### 当前行为（session_client.cpp:137）

```cpp
// The frozen single-outstanding discipline forbids issuing the next
// request while one is unanswered, so the session ends here; the
// owner reconnects and resyncs from snapshots (DEC-012).
dead_ = true;
dead_reason_ = "request " + std::to_string(id) + " timed out after " +
               std::to_string(timeout.count()) + " ms";
```

#### 新行为

```cpp
// Request timeout != connection failure. Mark sync as stale but do not
// close the transport — events may still be arriving. The caller decides
// whether to keep using this session (events-only) or reconnect.
health_ = Health::SyncStale;
last_timeout_reason_ = "request " + std::to_string(id) + " timed out after " +
                       std::to_string(timeout.count()) + " ms";
// response 标记为 unavailable，但不设置 dead_
timed_out.error = {"unavailable", last_timeout_reason_};
```

### 4. 可观察关闭诊断

`SessionWorker::run()` 返回的 `RunExit` 和 `diagnostic` 必须被消费并记录：

```cpp
// bridge.cpp SessionWorker::run()
void run(kairo::StopToken stop) override {
    std::string diagnostic;
    const auto exit_reason = impl_.client->run(diagnostic);
    impl_.live.store(false);
    
    // 记录关闭原因，而不是静默退出
    if (exit_reason == ipc::SessionClient::RunExit::ConnectionLost) {
        std::cerr << "mira_pet: IPC connection lost: " << diagnostic << '\n';
    } else if (exit_reason == ipc::SessionClient::RunExit::Stopped) {
        // 正常关闭，不需要警告
    }
    
    if (!stop.stop_requested())
        app::requestUpdate();
}
```

### 5. 重连机制（可选，本 DEC 不强制）

本 DEC 不强制要求实现自动重连，但必须允许调用方实现重连：

- `SessionClient` 提供 `reconnect(timeout, diagnostic)` 方法
- 重连**不保留**旧的未决请求（它们已经超时）
- 重连后必须重新订阅事件
- 重连后必须重新核验 owner/PID（通过首次状态请求确认）

重连由调用方（如 `Bridge`）根据策略决定：
- 立即重连？延迟重连？指数退避？
- 重连失败后如何处理？
- 这些策略属于产品层，不属于 SessionClient

### 6. 单连接单未决请求纪律不变

本 DEC **不改变** DEC-012 的单连接单未决请求纪律：

- 一个连接上仍然只能有一个未决请求
- 请求超时后，该请求被标记为失败，不再未决
- 下一个请求可以正常发出（如果 transport 仍健康）
- 事件帧不是请求，可以随时推送

## 验收

1. **探针验证**：
   - 延迟 2000ms 时，桌宠进程 22 秒仍存活 ✓（第一阶段已通过）
   - 新增：单次超时后，下一次请求仍能成功（`Health::SyncStale` → `Healthy`）
   
2. **失败诊断**：
   - `RunExit::ConnectionLost` + diagnostic 被记录到 stderr
   - 用户不再看到"unexpected exit"，而是明确的失败原因

3. **重连验证**（如果实现）：
   - 超时后重连成功，事件订阅恢复
   - 序列异常（seq 跳跃）被检测并重新快照
   - owner/PID 变更被检测并拒绝重连

4. **回归**：
   - Debug 核心测试通过
   - ASAN/UBSAN/TSAN 通过
   - 既有 32/32 隔离验收不回归

## 实施顺序

1. **Phase 1**（最小化，可独立交付）：
   - 修改 `SessionClient::call()` 超时逻辑：不设置 `dead_`，设置 `Health::SyncStale`
   - 修改 `run()` 循环：只在 transport 失败时退出，不因单次超时退出
   - Bridge 消费 `RunExit` 和 `diagnostic`

2. **Phase 2**（可选）：
   - 实现 `reconnect()` 方法
   - Bridge 增加重连策略（延迟、次数限制等）
   - 序列异常检测与快照恢复

3. **Phase 3**（M8-03）：
   - 启动入场时序验证
   - 真实断联打断验证
   - bootstrap 期间状态变化测试

## 依赖与约束

- 依赖：DEC-012（单连接单未决请求纪律）
- 约束：所有并发任务由 kairo::Executor 管理，不自建线程或队列
- 相关：M8-05（桌宠生命周期）、M8-12（会话韧性）

## 参考

- [BUG-20261015-002 分析](../analysis/native-session-truncation-pet-ime-20261015.md)
- [第一阶段修复证据](../compatibility/pet-transport-staleness-fix-20261015.md)
- [DEC-012：IPC 事件订阅与 wire schema](DEC-012-ipc-event-subscription-and-wire-schema.md)
