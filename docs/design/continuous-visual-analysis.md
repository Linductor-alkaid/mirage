# 连续视觉关系与注意规划消费

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-05/06/07/18
> 依据：DEC-091、DEC-109；Mirador DEC-026（Experimental）

本增量消费 Mirador master `23f7ed2` 已交付的连续感知契约、关系视图构建器和
注意规划。Mirador 的 M9-04 结果采纳/覆盖证明及 M9-05 时间状态/事件仍待交付，
Mirage 不复制这些算法。pin 审计见
[依赖记录](../supply-chain/mirador-continuous-perception-20261022.md)。

## 执行与坐标边界

`VisualSessionConfig::continuous` 显式启用独立的背景观察宿主；其融合输出固定为
图像 oriented 坐标，直接使用当前帧和当前融合快照调用上游 `build_relation_view`
及 `ContinuousPerception::plan_attention`。原有宿主默认继续输出 display 坐标。
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

注意策略经上游校验与版本管理；来源、策略或 importance 变更使后台签名比较
基线失效，不能因像素相同跳过新配置的分析。后台签名仍相对最后完成的分析；
skip 不发布旧关系/规划作为当前结果。失败、取消与超时不提交后台比较基线。

请求结算前释放 frame owner 与 attention payload；mailbox 即使保留已结算请求
版本，也不能继续占住采集代槽。worker 的 Executor 唤醒同时取消在途能力，确保
Executor 优先关闭也能释放协作式阻塞，而非只唤醒空闲 condition-variable 等待。

## 结果与资源契约

| 存量 | 暂定默认上限，配置只能收紧 |
| --- | --- |
| coordinator 自有状态 | 512 KiB，16 个 attention 条件 |
| 单份关系视图 | 256 实体、1,024 关系、512 KiB |
| 单份 attention plan | 64 KiB，按三个 vector 的实际 capacity 核算 |
| 原有 frame/result cache | 沿用 VisualSessionConfig；与上述增量分别计量 |

负责人 Mirage 维护者；最迟 M9-07/12 实机验收前复核。输出保留 upstream 的
`coverage=kIncomplete`、entity freshness 及显式 unresolved/bootstrap/peripheral
记录；几何从属不升级为平台 role，importance 不修改置信度。单份结果预算不
约束消费者无限保留已完成 future，生产接线仍须限制消费端存活代数。

当前规划发生在**同一帧识别与融合之后**，只为后续消费提供明确的工作建议。
它没有把当前 OCR 改为 ROI 调度，也没有承诺焦点切换可直接查询完整当前状态。
真正的昂贵阶段前置选择、覆盖采纳、事件 drain、模型任务注意及文字供给接线
继续由 M9-07/18 和上游 M9-04/05 承接。没有完整覆盖/新鲜度证明时不得作动作依据。

## 验收

以真实 worker 和 fake backend 验证：旧 display 路径兼容、共享帧输入、oriented
关系/规划与来源回显、静态 skip、同像素策略/importance/source 变化不误跳过、
源重建后缓存不可跨代复用、乱序/几何变更拒绝、未见焦点无隐式 bootstrap、
显式 bootstrap、各预算失败、取消/期限/异常/admission/shutdown 和恢复。
运行 Debug 全量、Release 及 ASAN/UBSAN/TSAN 定向；实际模型、低端设备与平台
资源结论继续保持未验收，不把上游 Delivered 等同于 Mirage 产品已完成。
