# DEC-116：桌面共享瞬时失败恢复与任务内静默重授权

> 状态：Accepted
> 日期：2026-10-26
> 负责人：Mirage 维护者
> 修正：DEC-015/DEC-074 中「失败捕获不可重授权」把瞬时失败等同于授权失效的推导；
> fail-closed 核心（任务内绝不弹授权 UI、绝不未经授权捕获）保持不变
> 补充：DEC-113（输入恢复只封锁输入通道，不波及观察通道）、DEC-115（连续采样失败语义）
> 工作项：M9-13；关联 M9-20/21

## 背景与问题

Mirage 的产品目标是让 Agent 在真实桌面自主完成任务。2026-10-26 22:36 的 bilibili
任务（session `a3ba4902…`）中，共享会话在任务开始约 20 秒后因一次捕获失败被永久
毒化：`PortalObservation::capture` 以 `failed_ = !out.ok` 把任何失败（含调用方取消、
超时、单帧解码错误）等同于会话死亡；supply 层随即以固定话术
「native sharing ended or failed; no reauthorization in this task」拒绝后续全部截图、
OCR 与输入。Agent 只能以 exec 取证，settlement 依 DEC-081 正确拒绝未验证结论，任务
失败。DEC-115 交付的持续感知后台采样与前台共享同一会话后，触发面从任务驱动的少量
前台捕获放大到约 4 次/秒，单次瞬时失败在一个长任务内几乎必然出现。

与 2026-10-18 事件（单个畸形参数毒化健康会话）同根：把「一次失败」当作「永久不可用」。
自主任务不允许任何导致任务终止或能力在任务内不可恢复的行为。

## 决策

1. **授权与会话分离**。授权是用户授予的桌面共享许可（remember 模式下以单次轮换的
   restore token 持久化）；会话（portal session + PipeWire 流）只是行使授权的载体，
   可静默重建。授权有效期间，任务内允许反复重建会话；重建走既有
   `consume → CreateSession/SelectDevices/SelectSources/Start → save` 轮换路径，
   全程无 UI。
2. **失败分类**（portal 层，`capture_portal`）：
   - 调用方取消、帧超时（`cancelled`/`portal_timeout`）：不动任何会话状态；
   - 流级失败（`pipewire_unavailable`、`invalid_stream_frame`、
     `unsupported_stream_format`）：只重建 PipeWire 流，保留 portal session；
   - 会话级死亡（`portal_session_closed`、`portal_unavailable`）：丢弃 session，
     若 remember 授权可用且连续重建失败少于 3 次，下次捕获静默重建；
     否则返回精确错误「需要用户在任务外重新授予桌面共享」；
   - 授权撤销（generation 变化/`desktop_authorization_changed`）：真实约束，
     会话废弃，任务内不恢复。
3. **无记住授权时的会话死亡**不再伪装成永久能力缺失：返回可操作错误，由模型转告
   用户；不得重复弹授权 UI，不得无限重试。
4. **输入不确定性只封锁输入通道**。`side_effect_may_have_occurred` 且未成功的输入
   一律走 InputRecovery（`AwaitingFrame` 待新帧重开 / `Unavailable` 本会话停用），
   并要求更新的源帧作为后续证据；捕获与 OCR 通道保持存活。删除
   `s.closed = true` 与供给层对不确定输入的会话毒化。「先观察、后输入、不重放」
   语义不变。
5. **后台持续采样无特权**。采样失败发布 `degraded` 并继续；连续 20 次失败只停止该
   advisory feed（`Failed` 状态对 Feed 可见），永不毒化前台会话，也不结束感知以外
   的任务能力。
6. **错误必须可诊断**。工具错误携带平台层真实错误码与有界消息，替代
   「native sharing ended or failed」类笼统话术；毒化仅剩授权类与 handler 异常类。
7. 不新增线程、调度器或任务状态机；全部状态归既有序列化 owner（会话对象、
   `DesktopImageTurn`、capture worker）持有。

## 备选方案

- 维持毒化策略：单次瞬时失败即任务死亡，与自主目标直接冲突，不采用。
- 任务内自由重试授权：可能弹出系统共享对话框打断用户，违反 DEC-015，不采用。
- 为后台采样建立独立第二会话：双份授权与双流带宽，且采样失败仍会独立毒化，
  不采用；共享会话加分级失败处理已满足需求。
- 把 `portal_timeout` 也视为流级失败重建流：静止画面的水位等待重建后同样无新帧，
  只增加 churn，不采用。

## 影响与风险

- 工具错误消息与平台契约注释变化：`desktop.screen.capture`/`desktop.vision.ocr`/
  `desktop.session.*` 的失败语义由「任务内终结」变为「分级恢复」，依赖旧话术的
  测试同步更新；会话契约注释改为「失败报告真实原因，记住授权下自动重建」。
- 静默重建消耗并轮换 restore token；连续 3 次重建失败后本任务不再尝试，需要用户
  重新授权。钥匙环不可用属授权类失败，按第 2 条最后一款处理。
- `portal_timeout` 在静止画面 + newer 水位下可能重复返回：会话保留，模型可等待、
  移动指针或改用其他路线。
- 本决策不承诺消除触发瞬时失败的平台级根因（mutter/PipeWire 停顿等），承诺的是
  单次失败不再升级为任务死亡；运行日志仍缺平台层细节，诊断增强另列工作项。

## 验证方式

供给层与采样层回归：capture/OCR/输入在瞬时失败后可恢复、授权撤销后仍拒绝、
不确定输入只封锁输入、连续失败停 feed 不毒化会话、重建失败预算与错误透传。
由 Independent-Verification-Agent 独立编写并执行（Debug 全量 CTest +
ASAN/UBSAN/TSAN 聚焦 + native-release 刷新），证据记录于
[事故与修复记录](../compatibility/desktop-sharing-resilience-20261026.md)。

## 关联文档和工作项

[M9 计划](../plans/m9-continuous-tasks-and-local-perception.md)、
[DEC-015](DEC-015-linux-backend-dependencies-and-event-loop.md)、
[DEC-074](DEC-074-task-scoped-wayland-observation.md)、
[DEC-113](DEC-113-autonomous-action-recovery.md)、
[DEC-115](DEC-115-task-continuous-perception-feed.md)、
[Wayland fail-closed 事故记录](../compatibility/wayland-capture-failclosed-20261014.md)。
