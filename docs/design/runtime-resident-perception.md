# Runtime 常驻桌面感知

> 状态：Implemented（本机验证范围见验收记录）
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 工作项：M9-22；关联 M9-06/07/09/10/11/21
> 决策：[DEC-117](../decisions/DEC-117-runtime-resident-perception-and-autonomous-input.md)

## Owner 与授权

Runtime Service 持有 ResidentDesktopPerception，后者复用既有 connect_continuous_desktop、
ContinuousCaptureHost 与 PpOcrPipeline。它不持有新的 Executor。每次来源实例使用唯一
incarnation，避免 worker 名称复用、旧事件帧 ID 或图像分析状态命中新来源。

服务在完成初始化后，通过 Executor 有限任务检查已记住的系统授权；ScreenCapture 策略
为 Allow 且系统已有 grant 才静默启动交互共享源。没有 grant 时保持无源，不调用系统
创建授权会话。首个任务也可把已建立的共享会话移交给 Runtime。观察源必须由对应的
ScreenProvider 支持；本轮产品接线为 Linux Portal，其他平台不凭适配器单测宣称支持。

后台 CaptureLimits.allow_authorization_prompt=false；活跃会话可继续采集，重建只允许
使用既有 restore token。不存在 token 时返回 desktop_authorization_required，不能创建
新的系统授权请求。平台仍负责实际授权/流恢复，不由 Adapter 模拟授权。

每个任务 attach 独立 SessionLease/FeedLease：自己的 task CancelToken、authority 校验、
事件起始游标、已消费预览和注意范围。只读租约的 input_supported=false 且 input 拒绝，
不能从 Runtime 的交互共享源继承输入权限。动作仍逐次经过 Mirage 权限与 ActionCoordinator。
任务的 image_scope、待核验动作和模型语义绑定继续由 DesktopImageTurn 所有。
窗口受限的 embedding 不自动接入全桌面常驻供给，不能通过文本事件绕过 capture_scope。

任务取消或正常销毁不取消 Runtime 的 token，也不停止采样。忘记授权使当前源 invalidate；
策略/authority 变化使旧租约即时不可用，不能通过恢复旧任务取得新权限。服务 teardown
先停止常驻源，再排空既有任务/分析并关闭 Executor。相应原生新任务/授权建立可接新来源。
恢复桌面控制或替换权限策略会提交至多一个有限的静默恢复任务，future 由服务保留并
在后续调度/关闭消费。后台准入失败时 connect 保留任务已授权的前台会话，并明确无
连续 feed；不能将后台失败扩大为整项任务的观察/输入失效。

## 资源与并发

平台 capture/pointer/input 继续由一个资源锁串行访问；前台动作持有已有 pause lease，
取消在途后台分析。资源锁只保护平台资源和 owner 替换，不承载事件队列或任务通信。
跨上下文的数据仍使用 kairo LatestMailbox、MpscChannel 和新增的 Topic 通知。

原有 raw 帧最多两代、每代 32 MiB；预览最多 8 张/8 MiB；事件 256 条/256 KiB，最长
120 秒。任务只保留共享源租约，不能无限钉住历史预览或增加源帧代数。单个消费者 ACK
不在共享日志上全局删除事件；共享历史保持容量/120 秒 TTL，任务独立过滤已读内容。
背景分析仍按既有 250 ms 尝试节奏和变化门控运行，不新建 VLM 或模型常驻实例。
持续采样但静态跳过 OCR 时保留 advisory 对象/标签，仍报告原始分析年龄；不因两分钟
没有重新 OCR 就清空静态状态，也不把旧文字标成新鲜成功证据。停止、降级或无来源进展
时仍按原有陈旧处理失效。

所有任务的注意范围在消费者本地过滤；不把最后一个任务的 ROI 遗留到全局生产者，
也不让一个任务的 focus 隐藏另一个任务所需变化。源重置、出现/消失等屏障事件保留。

latest_frame 返回由采集宿主共享持有的不可变图像，不进行采集或编码；前台显式
capture 优先复用一秒内、满足来源水位及字节预算的最新帧。由于现有 CaptureOutcome
持有值类型 ImageFrame，前台返回仍可能复制像素，不能称为端到端零拷贝。

source_sequence() 记录已完成 raw 采集的水位，包括被前台取代、尚未发布分析的帧。
动作后显式核验必须跨过动作前的实际水位，不能把后台已存在的新于旧规划图的帧
冒充动作后证据。latest_frame 本身不签发动作或成功证据。

## 输入和观察

共享输入、编号目标、区域目标的普通 click/scroll/drag 路径移除前置像素比较和到达
截图；保留身份/坐标空间/几何、权限、取消和输入释放检查。键盘同样不靠像素相似度
准入。共享输入回执增加 approach_completed、press_dispatched、release_confirmed；
未派发 press 且已释放的失败不再被标成“点击是否发生不确定”。super 规范化为 meta。

普通共享动作完成后回传当前 environment_epoch 和仍保留的 image_scope；它们表达原
输入意图，不延长原观察年龄或宣称任务完成。模型可连续执行已知的中间动作，显式后条件
仅在状态依赖或任务边界请求。工具版本：session 与编号/region 指针动作 5.0.0。

text_only OCR 使用独立 preview/metadata，既不生成 PNG，也不删除已有输入上下文。
旧图片停止再次自动供给，显式 capture 可以重新请求供图；supplied_image 仍表示此前
模型确实消费过相应输入意图。OCR 的新文字框自身仍不产生输入权限。

任务创建时可直接 attach 已授权常驻源，首次显式截图之前也可收到文本事件及当前对象
投影。每轮依旧至多 16 条事件、8 个已读实体摘要和 8 KiB 总供给。vision.state 2.0.0
优先读取该源，未连接时保留旧的 OCR 状态回退；它不取帧/编码。

新增 vision.wait 1.0.0：after_cursor 默认当前游标，timeout_ms 默认 5000、最大 30000。
在已有后台生产者上等待事件/来源 gap/取消/超时，返回状态及有界观察摘要，不暂停生产、
不额外 capture/OCR/PNG/模型调用。内部 Topic 最多 16 个等待订阅，每个容量 1；通知只
唤醒消费者，事实仍从有界 journal 读取，通知合并不删除事件。容量拒绝显式返回；
receive_for 最长 50 ms 以观察任务取消/期限，订阅 RAII 关闭，来源关闭会唤醒等待。

## 验证边界

数据和准确命令见[验收记录](../compatibility/runtime-resident-perception-20261027.md)。
验证必须区分驻留/静态 OCR 跳过、源取帧/应用复制、PNG 与模型供图；本轮没有交付平台
damage 或宣称静态零取帧。保留低端长驻 CPU/RSS、真实 Portal 长任务、在线完成率和
Windows 的独立验收，不以单元测试或私有 X11 夹具代替。
