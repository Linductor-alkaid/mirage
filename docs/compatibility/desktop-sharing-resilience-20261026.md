# 桌面共享单次失败毒化任务的事故与恢复决策

> 日期：2026-10-26
> 负责人：Mirage 维护者
> 关联：DEC-116（本事件引出的决策）、DEC-015/074/081/113/115、
> wayland-capture-failclosed-20261014
> 状态：已修复，待 Independent-Verification-Agent 独立复验

## 事故摘要

2026-10-26 22:36:09–22:37:46 的任务「去bilibili找个你喜欢的视频看看」
（session `a3ba4902d7db40bfa191ea2c224cbc10`，native-release 20:56 构建，
含 DEC-115 持续感知接线）以「桌面操作结果未验证或存在不确定副作用」失败。

时间线（会话日志与工具元数据）：

- 22:36:13 首次 `desktop.screen.capture`（interaction=true）成功，
  `source_sequence=2`、`continuous_events=true`：会话移交持续感知，后台采样启动。
- 22:36:19 第二次捕获成功，`source_sequence=226`（约 38 帧/秒），后台采样高速运行。
- 22:36:24–26 OCR 内部最后一次成功捕获（7 帧已分析、63 实体）。
- 数秒后 `source=roi` 捕获被拒：`desktop_image_supply.cpp` 会话预检命中
  「native sharing ended or failed; explain the result, no reauthorization in this task」。
  此后 OCR、`semantic_snapshot`（另有 4096 节点预算拒绝）、全部捕获失败；
  输入路由随会话死亡（模型记录 "No authorized input route"），视频无法点击播放。
- Agent 以 exec 进程/网络取证替代观察并作答；`verification.settle()` 按 DEC-081
  拒绝未验证副作用，turn 判失败。settlement 行为本身正确。

## 根因

1. `platform/linux/src/portal_screen_backend.cpp`：`PortalObservation::capture` 以
   `failed_ = !out.ok` 把任何失败（调用方取消、`portal_timeout`、单帧
   `invalid_stream_frame` 等）等同于会话死亡并销毁 PipeWire 状态；
   `capture_portal` 对既有 cast 的任何失败 `cast.reset()`，且 `attempted` 锁存
   禁止任务内重建（「no reauthorization in this task」）。
2. `integration/mirador`：DEC-115 移交后，后台采样器与前台共享同一
   `PortalObservation`，采样周期 250 ms。任一采样失败经同一毒化路径杀死前台
   会话；采样器自身也在首次失败即永久停止。触发面由任务驱动捕获放大到约
   4 次/秒，20 秒窗口内即命中。
3. `integration/mira`：supply 层以 `native_failed_`/`!reusable()` 预检把上述状态
   转成任务级永久拒绝，且固定话术丢弃真实错误码；`shared_input` 对不确定输入
   副作用 `native_session_.reset()`，连带杀死捕获/OCR。

期间 journald 无 portal/PipeWire 异常记录，runtime 服务无运行日志，底层触发码
不可考——可诊断性缺失一并按 DEC-116 第 6 条处理。

## 修复

按 DEC-116 实施：

- portal 层失败分类：取消/超时不动会话；流级失败仅重建流；会话级死亡在记住
  授权可用时静默重建（失败预算 3 次），撤销仍终局。移除 `failed_` 毒化与
  `attempted` 一刀切锁存；不确定输入只走 InputRecovery，删除 `s.closed = true`。
- supply 层：预检仅保留授权类拒绝；`new_source_frame_required` 与捕获失败不再
  销毁会话，错误透传真实码；`shared_input` 不确定副作用改为输入通道封锁 +
  更新源帧水位。工具描述同步恢复语义。
- 持续感知：采样失败发布 `degraded` 并继续；capture worker 连续 20 次失败才停
  advisory feed；`set_demand` 重开时重置预算。
- 契约注释：`desktop::DesktopCaptureSession` 文档改为「失败报告真实原因；记住
  授权下自动重建；无记住授权时提示用户重新授予」。

## 验证

Independent-Verification-Agent 独立编写并执行回归（记录见其回报）：瞬时失败后
恢复、授权撤销仍拒绝、不确定输入仅封锁输入、连续失败停 feed 不毒化会话、重建
失败预算、错误码透传；Debug 全量 CTest 114/114 + ASAN/UBSAN/TSAN 专项各 8/8
（TSAN 需 `setarch -R`，内核 ASLR 熵限制）+ native-release 聚焦 6/6。验证中
发现的 `rejected()` 误导性诊断后缀（对取消/超时虚报 "session closed"）已在
portal_screen_backend.cpp 按事实分流修正，并经同一代理增量复验 PASS（纯文本
变化、产物 0 残留、debug 全量复跑通过）。实机 bilibili 类长任务验收单列。
