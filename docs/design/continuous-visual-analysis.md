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
| 前置 AttentionPlan + 最终 WorkPlan | 合计 64 KiB，包含 vector capacity 与 token/source 字符串 |
| 单次空间工作 | 4 个区域、累计 4,194,304 源像素；识别前整批准入 |
| 时间/焦点/drain 输出 | 沿用实体/事件/条件数量上限与协调器字符串预算，单列 state_owned_bytes；不保留历史输出 |
| 原有 frame/result cache | 沿用 VisualSessionConfig；与上述增量分别计量 |

负责人 Mirage 维护者；最迟 M9-07/12 实机验收前复核。关系视图仍保留上游的
Unvalidated/Incomplete，不把关系存在当作覆盖证明。WorkPlan.coverage 只表示快照
中已列区域的覆盖，不意味着屏幕所有内容都已识别。importance 不改变置信度。
单份结果预算不约束消费者无限保留已完成 future，生产接线须限制消费端存活代数。

## 采纳和结算边界

默认两种 evidence_digest 均为 0：只执行 token 化规划，不采纳缓存/预算化结果，
未覆盖区域保持 unknown；未采纳的在途描述符由下轮替换，计数可见。后台 skip
既不产生当前结果，也不推进确认窗口。

全帧模式下，可信 owner 可传非零 full_frame_evidence_digest，标识能力实现、模型和参数；变化时
必须更新。此时准入要求至少启用 OCR 或 detector，所有启用的能力均为无 ROI 的
kRefresh，禁止预算化 OCR 与几何分割，避免把局部/缓存/部分结果冒充完整证据。
全部能力成功并融合后，在同一不可变帧仍存活时 plan_cycle，再把实际已完成的全帧
证据对应的工作 token 批量 accept。失败、取消、超时与旧帧拒绝均不会走采纳路径。
token 覆盖采纳仍在识别后；空间模式的选区规划已前置，契约见下节。

每个成功 cycle 返回 WorkPlan、可选 WorkAcceptReport、TemporalSemanticState、
FocusStateResolution 及 EventDrain。上游在有效观测中推进两帧确认窗口；只观察
到一次不产生 begin，通知过滤只影响事件发布，源重建 end 保留旧 epoch。队列按
max_events 和协调器字节预算有界；dropped_total/dropped_events、resync_required、
replaced_inflight、reuse.validated_frame 直接回显，不隐去 gap。时间状态的
pending_events 是 drain 前值，返回 events 已从协调器移除，消费者必须消费该 future。
焦点查询可随分析返回，也可通过下述无帧 worker 命令读取；生产订阅协议仍开放。

采纳前最后一次协作取消检查为本轮提交边界；进入成功采纳/查询/drain 后同步结算
成功，不再因随后到达的取消丢弃已 drain 事件。plan/预算/accept 失败时不 drain；
此前已确认或 reset 生成的事件留在上游有界队列，供后续成功结果提取，溢出显式。
原始像素在 future 就绪前释放。shutdown 沿用既有 worker 生命周期，无新 owner。

对象/文本焦点前置选择、静态精确验证快路径、native/IPC 消费、模型按需供给
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


## 空间 ROI 前置执行（M9-07/18）

新增显式 spatial_evidence_digest（非零）启用空间模式，与 full_frame_evidence_digest
互斥，由可信 owner 维护能力/模型/参数身份。只允许非空 ROI-only 条件，坐标须为
当前源 oriented 整数像素且完整落在帧内；无 stable-id/text/label 解析回退。
OCR/Detector 的基础请求不带 ROI，统一 kRefresh、kOriented 输出；禁止预算化
OCR/geometry，要求 analyze_change，至少开启一个昂贵能力。空间选择只约束识别，
不授权输入，也不把未覆盖外围报告为未变化。

先用 fuse_evidence 对当前帧的空证据作无状态融合，附当前 change，再用现有
coordinator.plan_attention 选择明确的空间区域。空证据只表示本帧尚无识别事实，
不推进 session tracker 或连续状态。所有区域在预算内才开始能力调用；每区域的
OCR/Detector 都完整成功后才融合实际证据，按同一空间策略 plan_cycle 并批量采纳。
当前模式不跳过静态刷新，近似 gate 不能抑制小字更新。采纳继续在识别后完成。

max_spatial_work_items 默认 4，max_spatial_pixels 默认 4,194,304，均为只收紧的
暂定上限。像素以源 ROI 面积累计（重叠照计）；每种启用能力最多 4 次调用、各自处理这批
区域，预算不把 OCR/Detector 合并成一次识别。它不是 backend resize 后像素，也不是
实测性能收益。前置 AttentionPlan 与最终 WorkPlan 合计受 max_plan_bytes 约束。
结果保留前置计划及已完成 OCR/Detector 调用数，失败时也可诊断实际完成进度。

验证覆盖多 ROI 输入面积、全局坐标恢复、静态刷新、部分阶段失败不采纳、预算拒绝
发生在 backend 前、源/策略切换、微小像素失效和取消/超时/shutdown。未知定位条件
继续走原有显式路径；文本/对象前置规划、零 forward 静态复用和实机资源验收未完成。

能力完成后的 token 必须与已执行计划中的区域/空间逐项匹配，才能送入 accept。
空间/全帧模式切换须更换非零证据身份；源 revision 重建除外。策略或源变化沿用
已有 policy revision/source epoch 失效逻辑。暂停/失败不提交后台签名基线，原始
像素照旧在 future 可见前释放。没有新线程、队列、timer 或 worker。

[本轮兼容性与验证](../compatibility/continuous-spatial-execution-20261022.md)记录实际
命令和局限。构造请求的最小用法（假设已有当前授权 frame）：

```cpp
VisualAnalysisRequest request;
request.frame = frame;
request.fuse = request.run_ocr = true;
request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
request.continuous = VisualContinuousRequest{};
request.continuous->source_revision = source_revision;
request.continuous->spatial_evidence_digest = capability_stack_digest; // 非零，包含 ROI 执行模式
mirador::AttentionCondition condition;
condition.roi = {20, 30, 200, 80}; // 源 oriented 整数像素；源几何变化后由 owner 重算
request.continuous->attention.conditions.push_back(condition);
auto result = host.submit(std::move(request)).get(); // owner 消费 future；不在 worker 自等待
```


## 无帧查询与焦点切换（M9-18，本轮实施）

query_continuous 接收 expected_source_revision（非零）、可选 attention、
drain_events（默认 false）、取消 token 和期限。在与分析相同的 worker 上执行，
只允许匹配已建立的 source revision；未知/旧源拒绝且不修改策略。没有 frame，
不调用 backend，不推进观测/确认帧序号，不产生当前 snapshot/WorkPlan。

成功终态是 kQueried，只有 queried 字段：来源/epoch/policy revision、上游
TemporalSemanticState、FocusStateResolution、可选 EventDrain、最后完成分析帧
及其 steady-clock 年龄。年龄不等同于每个实体的 freshness；后者照 upstream 回显。
源重建时清空完成时间，失败/skip/query 不更新；没有成功分析时年龄为 unknown。

策略更新先校验预算，提交前检查取消/期限；成功提交后同步查询/可选 drain 并结算，
不以迟到取消丢弃事件。更改策略会使后台比较签名失效，但不伪造新的观测；后续帧
仍由 owner 携带该策略。查询沿用一个在途+一个最新待处理命令的语义，会取消/替换
旧分析或查询；owner 应等待查询 future 后恢复帧供给，不宣称无限查询下帧公平性。

共享命令发布与 queued 指针在同一锁内排序，避免并发提交的 queued/mailbox 次序
相反造成 future 无法结算。host.stop/Executor wakeup 在同一边界关闭接纳；迟到
提交显式拒绝。mailbox 只承载有界命令，采用非等待 try_publish；版本槽不足显式拒绝且不取消原命令。
输入 policy 的 vector/string capacity 总量按 max_coordination_bytes 检查，最多
16 条条件；成功/拒绝结算均在 future 可见前释放 policy，未新增调度设施。
worker 每轮只读一次 latest，持续发布不能使停止检查饥饿；Executor wakeup 同时
取消待处理与在途命令，覆盖停止检查与 pickup 之间的窗口。


查询的最小用法（已有 source_revision，owner 暂停新的分析提交直至 future 结算）：

```cpp
VisualContinuousQuery query;
query.expected_source_revision = source_revision;
query.attention = desired_policy; // nullopt 只读；后续分析请求也须携带该策略
query.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
auto result = host.query_continuous(std::move(query)).get();
if (result.outcome.kind == VisualAnalysisOutcome::Kind::kQueried) {
    const auto& known = *result.queried; // 使用每个实体的 freshness；年龄不构成新帧证明
}
```

[无帧查询验证](../compatibility/continuous-state-query-20261022.md)覆盖无识别读取、
策略/源隔离、drain 来源、资源拒绝与混合生产者关闭竞争。


## 原生产品消费者（2026-10-22）

独立背景宿主继续适用于受管采集基础层。原生产品另在现有 VisualSessionHost
worker 中维护独立 image core，复用同一个 mailbox、取消与后端；旧 display core
及模板不因 image 源重建而清空。该路径按任务需求接收已授权图像，不另开周期采集。
产品文本供给、来源键、预算、查询和完整覆盖限制见
[接线设计](native-continuous-perception-wiring.md)及
[验证记录](../compatibility/native-observation-and-continuous-wiring-20261022.md)。
