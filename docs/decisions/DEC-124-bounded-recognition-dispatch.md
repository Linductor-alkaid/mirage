# DEC-124：有界慢识别派发与原帧结果

> 状态：Accepted
> 日期：2026-10-28
> 依据：维护者批准的三层方案、DEC-121/122/123
> 工作项：[M9-28](../plans/m9-layered-perception-delivery.md)

## 问题与选择

后台 capture 当前同步等待 observe_image；跳过 OCR 并未解除慢识别对采集的阻塞。
M9-28 先交付 Adapter 私有派发原语，再拆分 CV 状态所有权和接入产品。
本决策不改变生产默认，也不宣称已实现 5/10Hz 或完整 ROI 调度。

复用 kairo 的 blocking worker 和 LatestMailbox；不共享 Mirador session，不创建
第二个 Executor。一个源一个 observer 串行执行。普通新帧只替换 latest，不取消
在途识别，避免连续视频让识别永久无法完成。来源、几何、焦点策略、权限代次变更
由 owner 显式 invalidate，取消旧代次并清除旧 latest/未消费结果；迟到结果丢弃。

## 契约与预算

- 每源至多一个在途或待消费完成结果，加一个 latest；结果未取走时不启动下一项。
  输入为已授权 BGRA、最长边1600、像素 capacity≤10MiB，拒绝非法布局、越界焦点、
  未来/零采集时间、失效代次和没有剩余时限的输入。硬期限至多采集时间后3秒。
- 只有两个像素代次槽，weak mailbox 不保留旧像素。消费者持有结果原帧也占用槽；
  两槽仍被引用时拒绝新工作，不能用外部保留结果绕过预算。
- callback 的事件/对象各≤256、跟踪/候选各≤8；动态容器和字符串 capacity
  合计≤512KiB，固定 DTO/控制块另计。超额整批失败，不截断成完整覆盖。
- submit 返回 ticket、被替换 ticket 或明确拒绝原因。superseded 为明确终态；
  已启动工作产生 completed/cancelled/timed_out/failed 之一，异常不吞掉。
  代次屏障/stop 丢弃未消费结果并计数。容量与传输统计来自 kairo comm，worker
  启动、退出和异常由 Executor 管理；业务计数仅投影这些边界。
- callback 输入可在识别前取得原始时间/代次/期限；结果携带原始像素、
  source_sequence、采集时间、代次和票号，不能重标为最新帧。
  callback 输出仍须由后续 owner 校验和投影；派发器不融合、更新基线/覆盖、签发引用，
  不把 OCR 完成时刻当内容观察时间。
- owner 在 worker 外 start/stop；停止生产→取消和唤醒→join→释放回调及像素。
  Executor 提前 shutdown 也须唤醒取消。start_worker/stop 均在 Adapter 锁外
  执行，避免 Executor 生命周期锁与 wakeup 的锁顺序反转。callback 必须协作检查 CancelToken/有界超时；
  deadline 不是强制终止。外部依赖必须存活至 stop 返回。

## 后续接线与退出

第一批实现只在测试中连接真实 capture host 和受控慢 observer，证明慢识别时仍能
采集以及 latest/结果背压。产品 observer 尚在原串行 worker，不能据此声称 CV/
tracking 已独立运行。后续将 CV 双基线和 tracker 移到单一采样 owner，通过原帧
结果更新识别基线，增加 pending/历史投影和 ROI/外围重验后，才增加产品实验接线。
完整 M9-28 保持 In Progress，按平台、取消竞态、故障和资源门禁验收。


2026-10-28 后续接线由 [DEC-125](DEC-125-layered-capture-owner.md) 冻结：独立采样
owner和默认关闭的产品实验复用本文派发器；取消probe改为weak工作项引用，避免
被下游保留的已结束token隐式保留原帧，run退出路径始终关闭准入/释放资源。
本文首批基础设施证据保持历史范围，当前接线与未完成门禁见新阶段记录。
