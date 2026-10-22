# 连续视觉规划、覆盖采纳与时间状态消费

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-05/06/07/18
> 依据：DEC-091、DEC-109；Mirador DEC-026（Experimental）

本增量消费 Mirador master `2a9f483` 已交付的连续感知契约、关系视图、token
绑定/覆盖账本和确认状态/事件。直接使用上游 plan_cycle、accept_work、时间/焦点
查询及 drain，不复制关系、覆盖或去抖算法。pin 审计见
[依赖记录](../supply-chain/mirador-continuous-perception-20261022.md)。

## 执行与坐标边界

`VisualSessionConfig::continuous` 显式启用独立的背景观察宿主；其融合输出固定为
图像 oriented 坐标，直接使用当前帧和当前融合快照调用上游 `build_relation_view`
及 `ContinuousPerception::plan_cycle`。原有宿主默认继续输出 display 坐标。
这避免用缺少变换的 display 快照调用上游暂不支持 display 空间的规划面，也不
复制整份快照来伪造新坐标。背景宿主不支持 marks、模板登记/匹配或发布动作引用。
发布 Adapter 对非 display 快照返回 invalid_coordinate_space，保留原活动引用集，
防止图像坐标背景结果意外被注册为全局可派发目标。

单个源仍复用既有 VisualSessionHost 的串行 kairo blocking worker；协调器在该
worker 使用，无新增线程、timer、模型或调度器。输入可直接使用连续采集基础层的
共享 Frame owner，无需再次复制像素。

每次请求携带非零、由可信 owner 维护的 `source_revision` 与声明式 attention/
importance。前者表示源实例/采集几何/授权范围的产品代际，不能直接用“需求重开”
代替平台变更证明。较旧 source revision、同代非递增 frame sequence、同代图像
尺寸/旋转切换均拒绝；变换/实例变化须由 owner 重开 source revision。新代在
worker 上重建 PerceptionSession，清理旧缓存/稳定 ID，并通知上游协调器 source
reset。输出同时携带产品 source revision 和协调器 source epoch；两者不混用。

注意策略经上游校验与版本管理；来源、策略、importance 或证据身份变更使后台签名比较
基线失效，不能因像素相同跳过新配置的分析。后台签名仍相对最后完成的分析；
skip 不发布旧关系/规划作为当前结果。失败、取消与超时不提交后台比较基线。

请求结算前释放 frame owner 与 attention payload；mailbox 即使保留已结算请求
版本，也不能继续占住采集代槽。worker 的 Executor 唤醒同时取消在途能力，确保
Executor 优先关闭也能释放协作式阻塞，而非只唤醒空闲 condition-variable 等待。

## 结果与资源契约

| 存量 | 暂定默认上限，配置只能收紧 |
| --- | --- |
| coordinator 自有状态 | 512 KiB，256 个实体、256 个待 drain 事件、16 个 attention 条件 |
| 单份关系视图 | 256 实体、1,024 关系、512 KiB |
| 单份 WorkPlan | 64 KiB，包含 attention、token/source 字符串、reuse 和 invalidation 的实际 capacity |
| 时间/焦点/drain 输出 | 沿用实体/事件/条件数量上限与协调器字符串预算，单列 state_owned_bytes；不保留历史输出 |
| 原有 frame/result cache | 沿用 VisualSessionConfig；与上述增量分别计量 |

负责人 Mirage 维护者；最迟 M9-07/12 实机验收前复核。关系视图仍保留上游的
Unvalidated/Incomplete，不把关系存在当作覆盖证明。WorkPlan.coverage 只表示快照
中已列区域的覆盖，不意味着屏幕所有内容都已识别。importance 不改变置信度。
单份结果预算不约束消费者无限保留已完成 future，生产接线须限制消费端存活代数。

## 采纳和结算边界

默认 full_frame_evidence_digest=0：只执行 token 化规划，不采纳缓存/预算化结果，
未覆盖区域保持 unknown；未采纳的在途描述符由下轮替换，计数可见。后台 skip
既不产生当前结果，也不推进确认窗口。

可信 owner 可传非零 full_frame_evidence_digest，标识能力实现、模型和参数；变化时
必须更新。此时准入要求至少启用 OCR 或 detector，所有启用的能力均为无 ROI 的
kRefresh，禁止预算化 OCR 与几何分割，避免把局部/缓存/部分结果冒充完整证据。
全部能力成功并融合后，在同一不可变帧仍存活时 plan_cycle，再把实际已完成的全帧
证据对应的工作 token 批量 accept。失败、取消、超时与旧帧拒绝均不会走采纳路径。
这是识别后的覆盖/状态消费，尚未把 token 规划移到昂贵识别前，也未实现 ROI 调度。

每个成功 cycle 返回 WorkPlan、可选 WorkAcceptReport、TemporalSemanticState、
FocusStateResolution 及 EventDrain。上游在有效观测中推进两帧确认窗口；只观察
到一次不产生 begin，通知过滤只影响事件发布，源重建 end 保留旧 epoch。队列按
max_events 和协调器字节预算有界；dropped_total/dropped_events、resync_required、
replaced_inflight、reuse.validated_frame 直接回显，不隐去 gap。时间状态的
pending_events 是 drain 前值，返回 events 已从协调器移除，消费者必须消费该 future。
焦点查询当前随分析结果返回，尚无独立的无帧 worker 命令或生产订阅协议。

采纳前最后一次协作取消检查为本轮提交边界；进入成功采纳/查询/drain 后同步结算
成功，不再因随后到达的取消丢弃已 drain 事件。plan/预算/accept 失败时不 drain；
此前已确认或 reset 生成的事件留在上游有界队列，供后续成功结果提取，溢出显式。
原始像素在 future 就绪前释放。shutdown 沿用既有 worker 生命周期，无新 owner。

完整前置选择、静态精确验证快路径、无帧焦点切换、native/IPC 消费、模型按需供给
和通知 gap 恢复仍由 M9-07/18 推进。当前覆盖/事件仅用于本地事实，不授权输入。

## 验收

以真实 worker 和 fake backend 验证：旧 display 路径兼容、共享帧输入、oriented
关系/规划与来源回显、静态 skip、同像素策略/importance/source 变化不误跳过、
源重建后缓存不可跨代复用、乱序/几何变更拒绝、未见焦点无隐式 bootstrap、
显式 bootstrap、各预算失败、取消/期限/异常/admission/shutdown 和恢复。新增
全帧刷新采纳、两次有效观测确认、静态覆盖复用、微小像素失效、身份/源重置、
通知过滤与事件溢出取证；不以缓存命中作为新完成证明。
运行 Debug 全量、Release 及 ASAN/UBSAN/TSAN 定向；实际模型、低端设备与平台
资源结论继续保持未验收，不把上游 Delivered 等同于 Mirage 产品已完成。
