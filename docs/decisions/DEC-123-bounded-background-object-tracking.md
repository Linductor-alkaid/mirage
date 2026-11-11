# DEC-123：后台有界对象跟踪与识别回退

> 状态：Accepted（按维护者授权继续推进 DEC-121）
> 日期：2026-10-28
> 负责人：Mirage 维护者
> 工作项：M9-27 / M9-08；关联 M9-26/28/32/33

## 决策

在现有 VisualSessionHost 的后台 companion worker 内私有消费 pinned Mirador
ObjectTracker。服务增加 `--experimental-object-tracking`，必须同时显式设置
DEC-122 非零刷新间隔；默认关闭。前台图像 companion 不分配 tracker，不新增线程、
调度或帧缓存，不改变动作引用与完成证据。

识别确认的对象才可初始化外观模板。绑定 source/policy + entity/instance；内部
track token 与识别 ID 分开，避免 closed track ID 重用。轻量采样使用公共 change
门控、verify_track/commit_track_evidence 和失踪/重捕获账本；复用不刷新最后外观
验证序号。识别内容/位置不一致或旧轨迹已不确定/失踪时重新初始化并标连续性中断，不用
同文字/同坐标证明身份。候选与其他目标重叠时保守报告歧义并恢复识别。

tracking、uncertain、lost、terminated 是跟踪事实；失踪不产生 disappeared，
重捕获不恢复旧语义标签。跟踪报告只提供 advisory 位置和证据，不签发输入引用，
不推进内容识别年龄或 Mirador 覆盖。全局布局变化清空私有轨迹并重新识别。CV-only 对象继续按 DEC-122 pending/未知。
不确定、预算不足或几何新候选请求真实识别，使用同次拥有的像素。

首批预算固定为8目标、每目标4条位置、1份16²模板、0负模板、64KiB池；单目标
验证工作计量1MiB，最多8次/帧。保持 pinned NCC/PSR 门限，仅收紧邻域比例到0.25。
几何观察仅处理一个≤128²变化ROI，使用公共 crop/灰度转换/线检测/merge/proposal，
最多128线段、8候选；不是结构相似即可确认身份的证据。全帧缓存和 NanoTrack
权重不增加。预算/排除/池字节数经既有感知报告可见。

多目标步骤没有公开事务/克隆接口：取消或致命失败后清空私有跟踪池，要求识别
重新初始化，不发布部分轨迹，也不提交失败 CV 基线。停止时先取消/join既有worker，
再清空私有轨迹池，避免注册记录持有模板直到进程退出。已成功采纳的识别结果不因
附加跟踪初始化失败被丢弃，跟踪失败作为诊断与恢复条件保留。

完成请求的就绪状态必须在future唤醒前可见。RequestState显式settled原子标记
先于promise就绪发布，前台优先准入只检查未settled命令；worker尚未清理在途
指针不等于请求仍在执行。这样前台完成后立刻提交的后台工作可正常排队，不因
收尾窗口被误拒绝。该标记只服务既有请求生命周期，不增设队列或监控。

## 范围与后果

本增量先接近邻外观跟踪、失踪/近邻重捕获、几何候选及产品配置/报告。全局运动
补偿、任意位置重捕获、E2结构身份联合验证、对象级精确文字reuse和慢识别分离
仍未交付，M9-27保持 In Progress。近似 CV 与 NCC 不能排除所有相似对象交换；
默认关闭，真实视频/平台/5–10Hz与资源质量配对验证按M9-33，不以单测声明性能。
高歧义或预算不足场景可能回退到每周期识别，开关不承诺所有场景都有资源收益。
首次验证失败也须失效旧语义，即使此前没有发布过deferred视图；新OCR同字同位置
不能掩盖这次连续性中断。

验证覆盖真实公共 tracker 的移动/空白/重复目标/重现、source/policy/instance
隔离、内容变化失效、有界拒绝、取消/期限/关闭、产品实际后端调用和各 sanitizer。
不修改 pinned 代码，公开接口不足按既有反馈台账登记。


## 关联

[交付计划](../plans/m9-layered-perception-delivery.md)、
[设计](../design/layered-visual-perception.md)、
[使用示例](../examples/layered-visual-sampling.md)、
[第三阶段验证](../compatibility/layered-perception-phase3-20261028.md)。

## 后续生产配置（2026-10-28）

[DEC-128](DEC-128-layered-perception-production-profile.md)在维护者授权下部分替代
本文的Linux默认关闭/整体门禁前不得开放约束，按本机配置验收开放layered默认
与稳定CLI，并记录高清内存分档和legacy回退。本文其余覆盖、身份、年龄、取消/
生命周期和预算契约继续生效；本阶段历史证据不升级为完整平台或视频内容验收。
