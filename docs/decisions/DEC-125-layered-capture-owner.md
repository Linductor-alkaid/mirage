# DEC-125：独立采样 owner 与异步识别实验接线

> 状态：Accepted
> 日期：2026-10-28
> 依据：维护者批准的三层方案；DEC-121/122/123/124
> 工作项：[M9-28](../plans/m9-layered-perception-delivery.md)

## 所有权与原帧

将默认关闭的异步实验接到现有 continuous desktop capture host。capture worker
唯一拥有 CV 相邻/识别双基线、ObjectTracker 和产品 journal；DEC-124 worker
仅串行调用识别 observer，实际 OCR/融合仍由原 VisualSessionHost 串行执行。
不跨线程共享 PerceptionSession/ContinuousPerception，不增加 Executor 或调度器。

时间使用Adapter取得原帧的steady_clock完成时刻，不推断视频PTS或音频覆盖。
识别输入携带采集时间、来源代次、原尺寸与分析尺寸。observer 强制执行真实识别，
跳过原 observe_image 的内部媒体采样分支；Mirador Frame 使用原采集时间。
只有成功、完整、非deferred的原帧结果更新识别基线；失败/取消/过期不提交基线。
tracker 在原帧上从实际观察且已确认的对象建立有界模板；该接线每次完整识别后
重建模板，连续性不跨越慢识别间隔作保证。当前帧仅验证这些模板，不生成新语义。

## 延迟、未知和屏障

普通新帧不取消在途 OCR，仅替换 latest。完成事件/预览按原始帧和时间入账，
不按新画面缩放或重标。后续已有采样帧时，当前语义场景暂停供给（保留历史事件）；
不通过近似签名比较签发当前内容/覆盖或复活模型标签。后续识别恢复前保持未知。
这是保守的首批投影，独立历史对象视图及更精确覆盖复用后续交付。

来源、原/分析尺寸、焦点策略、前台暂停代次均撤销识别代次，清空采样基线/模板
与当前场景。即使前台暂停短于采样周期，也通过单调暂停代次取消旧 observer。
权限/任务退役立即禁止 Feed 供给，输入取消 probe 持 weak owner，避免循环引用。
停止先置 retired，停止采样 producer，再取消/join识别，最后释放平台资源；
Session owner 在worker全部join后显式释放raw session与journal，即使旧观察
token仍引用Core也不延长平台资源/预览生命周期。Runtime 仍先停 resident producer，再停 OCR，最后由外部 owner shutdown。

## 调度与预算

每帧使用 Mirador 公共 make_change_signature/detect_change（不启用fingerprint
早退）得到相邻和累计变化。bootstrap、布局/候选/低置信、区外变化、识别年龄
到期请求真实识别；已知媒体内变化可在年龄预算内延后。签名相似不代表精确复用。
CV-only 不调用融合/plan/accept，不刷新文字年龄或语义事件确认窗口。

沿用每源一个在途/待消费结果加一个latest的两帧预算。采样新增最多两个64²签名
及64KiB跟踪池；当前分析帧≤10MiB，OCR包装/模型/缓存和缩放临时缓冲另计，
不能把派发器20MiB保留上限当整个进程峰值。跟踪预算与候选上限沿用DEC-123。
派发/cancel/superseded/拒绝/丢弃和CV计数经Feed投影，语义事件不因每帧CV推进
而自动唤醒模型。非实时周期采样，不宣称硬实时或实得5/10Hz。

新增实验开关必须依赖非零 experimental recognition refresh，默认关闭；UI/持久化
配置不隐式开启。ROI识别、外围重验、Windows与真实质量/资源门禁继续开放，
不能把此接线写成完整M9-28或三层目标全部完成。


## 后续区域能力选择

[DEC-126](DEC-126-selective-regional-recognition.md)在本实验接线增加work提示和
raw ROI结果；完整识别基线仍须完整实际执行，局部结果独立去重，不进入身份/
覆盖采纳。它替代本阶段“识别均为全帧”的现状限制，其他生命周期与默认关闭
边界保持；本决策的历史验证不包含后续源码。

## 后续生产配置（2026-10-28）

[DEC-128](DEC-128-layered-perception-production-profile.md)在维护者授权下部分替代
本文的Linux默认关闭/整体门禁前不得开放约束，按本机配置验收开放layered默认
与稳定CLI，并记录高清内存分档和legacy回退。本文其余覆盖、身份、年龄、取消/
生命周期和预算契约继续生效；本阶段历史证据不升级为完整平台或视频内容验收。
