# 三层感知首阶段：显式 CV-only 采样

> 状态：Linux生产配置已开放；底层阶段示例与跨平台/视频门禁保留
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 工作项：M9-25；决策：[DEC-121](../decisions/DEC-121-three-tier-perception-delivery.md)

## 生产服务配置（DEC-128）

配置OCR资源的Linux服务默认layered、5Hz采样、5秒完整巡检和有界跟踪，不需要
experimental参数。Windows默认legacy，目标平台验证后再评估默认切换。

```sh
mirage-service --perception-mode layered
mirage-service --perception-mode layered --perception-period-ms 100
mirage-service --perception-mode legacy
```

模式参数设定一组默认值，后续单项可覆盖：`--recognition-refresh-ms 100..5000`、
`--object-tracking on|off`。legacy恢复同步、250ms、关闭跟踪/媒体延后；完整回退
是一条模式参数。历史experimental参数继续接受，其中旧refresh=0会关闭async/tracking。
源码嵌入者将ServiceConfig旧experimental前缀字段换为perception前缀；低层
PpOcrPipeline的可选位置参数维持兼容。新默认不替代原桌面授权，不自动安装/重启服务。
本机640重放满足768MiB门；高清1600重放原768MiB门失败，生产容量档为1024MiB，
不包含完整Agent/UI开销。验收及限制见[生产记录](../compatibility/layered-perception-production-20261028.md)。

以下为各阶段的底层接口示例；阶段当时“默认关闭”的描述保留历史意义。

复用既有 `VisualSessionHost` 和外部 kairo owner，不新增 worker。先以非零
analysis_policy_revision 完成一个带全帧 evidence digest 的识别周期，使
background admission 基线和识别状态有效；图像源、采集 owner 与权限配置不变。

在该 bootstrap 的相同 source_revision、geometry、attention、importance、
full_frame_evidence_digest、analysis_policy_revision 下，可提交后续采样：

```cpp
VisualAnalysisRequest sample;
sample.sample_only = true;
sample.frame = std::move(next_owned_frame); // same source; monotonically newer sequence
sample.continuous = bootstrap_identity;    // same trusted model/policy identity, not new evidence
sample.analysis_policy_revision = policy_revision;
sample.analyze_change = true;
sample.admission = VisualAnalysisAdmission::when_changed;
sample.deadline = deadline;
sample.external_cancel = cancelled;
// run_ocr/run_detector/fuse/template/geometry stages remain disabled.
auto result = host.submit(std::move(sample)).get(); // consume every settled result
if (result.outcome.kind == VisualAnalysisOutcome::Kind::kSampled) {
    // result.sampled: source revision/epoch, frame sequence, policy and observed_at.
    // result.change: relative to sampling_baseline_sequence.
    // result.admission_change: relative to admission_baseline_sequence (recognition).
    // No snapshot/continuous/adoption; no refreshed OCR age or confirmation count.
}
```

上述变量来自已经启动/授权并完成识别的调用方，不是独立可运行的初始化示例。
产品 image/background companion 必须保留对应 `image_session`/`background_session`
选择；不同 companion 的基线和语义状态不共享。CV-only 请求不运行视觉后端，
不产生融合快照、语义事件或输入引用，因此不应用普通“完成识别”处理器消费它。

两个比较各有用途：相邻成功采样报告局部运动；相对最后识别的比较保留累积
变化，不会因连续多个微小变化而永久跳识别。它们都是阈值化 CV，不证明文字
或语义未变；单字符变化的覆盖精确验证和证据年龄策略由 M9-26 继续推进。

首次 CV-only 请求以最后识别基线为相邻基线；成功采样只替换自身 4096B 签名。
成功识别使二者重新汇合，额外 CV 签名释放。取消、超时、格式失败及拒绝不推进
采样水位或基线；请求返回 kRejected 时检查是否需要重新识别 bootstrap，不做
无界静默重试。普通非零 digest 的识别准入检查未放宽。

没有 bootstrap、来源/尺寸/策略/证据配置改变、过期 sequence、缺少持续 host，
或混入 OCR/融合等阶段时明确拒绝。kSampled 的 `observed_at` 是该源帧的观察
时间，不能刷新语义证据时间；query_continuous 仍返回最近识别的原始年龄/状态。

当前生产 `observe_image` 未默认发出 sample_only，不能据该接口的存在宣称
视频期间 OCR 调用已下降。验证见
[首阶段记录](../compatibility/layered-perception-phase1-20261027.md)。


## DEC-122 媒体识别延后实验

服务入口 `mirage-service`（不是 launcher 的 `mirage start`）新增
`--experimental-recognition-refresh-ms N`：0 默认关闭，实验范围100～5000ms。
例如在独立测试环境向服务参数加入 `--experimental-recognition-refresh-ms 2000`；
需要现有 `--ocr-manifest` 与授权采集源。该参数是进程启动配置，不写入用户设置。
历史ServiceConfig开关现名`perception_recognition_refresh_ms`，与私有PpOcr adapter同步传递；
直接使用 `VisualObservationPipelineConfig` 时设置 chrono 字段
`experimental_recognition_refresh = std::chrono::milliseconds{2000}`。

只有已完成全帧 bootstrap、对象已确认、媒体区域已确认、同来源/尺寸/焦点策略且
未到重识别期限，才尝试 sample_only。相邻和累计变化报告均须完全包含于媒体区域；
区外变化立即以同次 capture 的已有像素执行识别，不再 capture。到期即使无变化也
强制识别。前台 text_only OCR、动作验证保持原路径；配置0或重启不带该选项即回退。

延后周期不提供新识别证据：`recognition_deferred=true`、
`recognition_reason=media_deferred_unknown`，`deferred_frames` 增加而 `analyzed`
不增加。场景保留对象ID和历史文本，标 pending/未确认，原 scene_observed_at 不变；
模型语义场景的 age_ms 继续增长，不能标注、验证动作或据此确认消失。首个延后周期
使旧语义内容版本失效，后续延后不重复递增；恢复识别出同样文字也需要重新标注。

阈值化 CV 不证明没有微小变化，媒体中的字幕/提示可能延后甚至漏掉；Mirador 的
变化确认去抖只累计真实识别，不累计 CV-only。刷新间隔是调度条件，受识别耗时和
采集调度影响，不能当作内容事件最大延迟保证。失败/取消/超时不续期，接收方查看
既有错误/状态；前台持有 worker 时正常返回 skipped，恢复后重新采样或 bootstrap。
精确 ROI reuse、跟踪生产接线、慢识别分离和在线 A/B 仍是后续工作。

设计见 [DEC-122](../decisions/DEC-122-deferred-recognition-unknown-evidence.md)，
验证见 [第二阶段记录](../compatibility/layered-perception-phase2-20261028.md)。


## DEC-123 有界后台跟踪实验

在独立服务测试参数中同时设置 `--experimental-object-tracking` 与
`--experimental-recognition-refresh-ms 2000`。前者默认false，要求后者非零；
CLI入口仍是 `mirage-service`，launcher/persistent设置尚不提供该实验选项。
直接调用方设置 `VisualSessionConfig::enable_object_tracking=true`，pipeline同时
设置 DEC-122 的刷新间隔。关闭选项并重启即退回第二阶段，两个选项都不设则原默认。

识别确认后初始化私有外观模板，CV-only 周期比较变化并验证邻域。跟踪报告单独
出现在 sample/view 的 tracking（可选）；Model observation只投影有限文本记录：
entity/instance、state、evidence、verified_sequence、advisory bounds、reacquired、
discontinuous，以及verified/reused/excluded/evicted、pool_bytes、原因和候选数。
最多8轨迹/8几何候选存于feed，模型最多4轨迹且记录JSON字节总量≤650；实际截断
可从total_tracks与返回数量判断。坐标随现有分析→共享源像素映射缩放。

change gate复用的verified_sequence不推进；它不是文字的新观察。CV-only依旧
保留原识别时刻与pending对象，tracking或reacquired不提供可执行引用，也不激活
旧标注。lost只是跟踪失踪；只有真实识别的覆盖与确认账本可以产生disappeared。
邻域验证歧义/弱匹配、预算或新几何候选可令requires_recognition=true，使用同帧
已拥有像素执行完整识别。识别文字/位置不一致会重建私有轨迹并标discontinuous，
强制使原内容语义版本失效。全局布局、来源/尺寸/策略改变重新bootstrap。

这是默认关闭的首批接线。只覆盖近邻重现；没有全局运动补偿、远距离候选关联、
结构身份联合验证或对象级精确文字reuse。小相似对象可能视觉不可区分，不用
tracker单独保证身份或放行动作；真实平台/视频/5–10Hz/资源A-B仍待验收。
详见 [DEC-123](../decisions/DEC-123-bounded-background-object-tracking.md) 与
[第三阶段验证](../compatibility/layered-perception-phase3-20261028.md)。

## DEC-125：独立采样与异步识别实验

在已配置验证过的 OCR 模型、已授权来源的独立服务测试环境，加入：

```text
--experimental-async-recognition --experimental-recognition-refresh-ms 2000
```

可额外加入 `--experimental-object-tracking`。异步开关默认false，依赖非零刷新
期限；仅服务CLI，不自动写入UI、launcher或持久化配置。关闭该开关即恢复此前
串行路径。采样周期仍由 source 的 sample_period 指定，默认250ms，接受100～1000ms；
设置100/200ms不代表实测已达到10/5Hz，也不承诺真实硬件/模型的质量或CPU收益。

采样 worker 每帧维护相邻/累计变化与可选跟踪，识别 observer 经有界派发器调用
原 VisualSessionHost，不并发访问 OCR backend。正常新帧仅覆盖latest；bootstrap、
待确认对象、累计变化、跟踪重验和年龄到期可请求真实识别。确认的媒体内变化可
在年龄预算内延后；DEC-126进一步接入下述局部能力选择和到期完整巡检。

结果事件和≤640边预览使用原始帧与采集时间。异步结果对应过去的帧，因此当前
semantic_scene 暂停供给，recognition_deferred=true；历史事件/预览和 advisory
tracking 仍可读。此实验不提供当前文字reuse或完整视频覆盖。任务语义绑定与
精确历史对象投影还需后续接线，不能把模糊签名相似或模板跟踪当文字刷新。

Feed/model 的 dispatch 包含 CV样本、admitted/superseded/rejected、cancelled/
timed_out/discarded、在途状态和保留像素字节；CV计数推进本身不产生语义事件。
短暂前台暂停、焦点或尺寸变化撤销代次；权限/任务退役禁止供给。停止保持先停
source，再回收识别，最后关闭 OCR/进程 Executor 的顺序。取消探针只保留工作项
weak引用，分析 host 留存已结束 token 不占用原帧槽。source停止后显式释放
平台session和journal，旧观察token引用的元数据也不延长这些资源生命周期。

[决策与限制](../decisions/DEC-125-layered-capture-owner.md)。


## DEC-126：OCR / 检测器按区域选择

继续使用上节默认关闭的async开关，不新增CLI选项。若OCR资源manifest中已有
通过校验的detector模型，异步完整识别现在也执行它；未配置检测器仍可运行OCR，
不会发现模型或下载资源。默认串行后台与显式mark行为不变。

调度优先级为：bootstrap/待确认/跟踪不确定/全局变化/完整识别年龄到期→完整
OCR+可用检测器；其余局部累计变化合并为一个扩8像素、面积≤全帧1/4的ROI。
已确认OCR对象且无检测label作为文字提示，仅OCR；纯检测对象且无文字作为
检测提示，仅detector；混合/未知区域使用两种可用能力。超ROI预算回退完整识别。
真实模型验证后，hint使用既有8像素context作边缘容差，要求变化与原框交叠，
且不与其他已确认框重叠；混合框也阻挡选择。32个hint不足以完整描述对象时组合
执行，实际工作ROI包含原框与变化的并集。这些是路由提示，不是语义分类或目标
身份保证；启用tracking时，requires_recognition
优先完整识别，不凭弱轨迹省掉能力。

`ContinuousImageRecognizer`实验回调增加末尾的`const PerceptionRecognitionWork &work`；
将其连同原时间/代次传入`observe_image(..., captured_at, generation, work)`。
`work.roi`为空要求完整识别；非空使用分析图像坐标，不是共享桌面的原始尺寸。
低层`VisualAnalysisRequest::regional_only`只接受背景image companion、一个合法
oriented整数ROI、refresh能力，禁止融合/change/模板/marks/continuous adoption。
普通空间覆盖接口继续使用原有契约，未放宽其检查。

局部结果以`regional_text_observed`、`regional_detection_observed`或
`regional_observed_empty`进入历史事件，confirmed=false，entity/instance=0，
保留原帧时间和原尺寸坐标。空结果只表示所执行能力返回空，不证明消失。
当前semantic_scene继续未知，不可标注或作为输入引用。结果最多32区域，文本每段
≤256字节；超限/失败不提交局部门控，纯检测但无可用detector明确失败。

一份局部64²签名只阻止同区域重复识别；同能力/同ROI识别输出的文字、标签和
位置都相同时不追加通知。完整识别签名和年龄不更新，外围变化仍相对完整基线
检查，到期强制全帧巡检。局部签名同样是近似变化检测，不证明精确reuse。
额外保留≤32个无字符串hint、一个签名/摘要；裁剪签名临时缓冲≤3MiB，模型和
后端预处理另计。dispatch新增regional_completed/ocr_completed/detector_completed，
仅统计owner接受的成功结果，不等于包括失败尝试的原始后端调用数。

验证与限制见[第六阶段记录](../compatibility/layered-perception-phase6-20261028.md)
和[DEC-126](../decisions/DEC-126-selective-regional-recognition.md)。精确覆盖、局部
目标身份关联、真正任务注意力、真实模型/桌面/视频质量和资源A-B仍按M9继续验收。


### 预训练权重的受控重放

`layered_perception_acceptance`为显式manual目标，不进入默认CTest，不发现或下载
模型。使用已有校验manifest和本机字体，例如：

```sh
cmake --build build/native-release --target layered_perception_acceptance -j 2
/usr/bin/python3 tests/manual/layered_perception_acceptance.py \
  --binary build/native-release/tests/layered_perception_acceptance \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \
  --output out/layered-capability-20261028/real-replay-fixed
```

driver生成独立的READY→SAVED文字图和空白图，输出每步实际后端调用/ROI、识别
文字、时间和12项门禁，失败返回非零且保留报告。20次静态采样是紧循环，不能
作为5/10Hz或视频覆盖证据；单次耗时也不是延迟分布。此路径重放已拥有像素，
不会捕获用户桌面、调用LLM或产生动作引用。真实X11采集另用既有
`mirador_review_run.py`在独立Xvfb执行，范围和前后失败记录见第六阶段记录。

DEC-127后，报告同时记录实际CV宽高/灰度字节；增加`--source-width 1600`可验证
1600×900来源到320×180工作图的坐标/context。640、1280、1600均为驱动支持的
来源宽度。自动文字观察门禁检查实际SAVED事件，不能只看“请求了OCR”。
OCR受控输入对照可给`ppocr_quality_acceptance.py`增加`--input-max-side 640`
（或480、320，0保留参考图），BOX缩小测试图并映射标注，再核对精确文本与边框；
不是产品自动选分辨率功能。结果和模型/样本hash见第七阶段记录。
