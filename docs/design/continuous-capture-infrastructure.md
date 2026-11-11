# 连续帧采集基础层

> 2026-10-27：已有宿主的生产所有权和前台最新帧消费按
> [DEC-117](../decisions/DEC-117-runtime-resident-perception-and-autonomous-input.md) 及
> [常驻设计](runtime-resident-perception.md) 更新；下文“生产消费者未接通”是首增量历史。

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-06/07/11/12
> 依据：[DEC-091](../decisions/DEC-091-local-semantic-perception-and-action-loop.md)、
> [DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md) 第 2/6 节

本增量交付可显式启用的 integration 采集宿主与共享帧 Adapter。生产消费者、静态
退避、平台 damage、pet 共存和文本供给接线继续由 M9 对应工作项承接；不改变
现有产品默认值，不为基础层加载模型、编码图片或启动 VLM。

## 所有权与生命周期

外部进程 owner 注入 kairo Executor 和一个已绑定授权/来源的同步 capture 函数。
进程 owner 同时配置有限的 max_in_flight_tasks 与队列预算，宿主不修改共享
Executor 配置；tick admission/执行失败以 Executor timer/失败状态为事实源。
函数仅在一个 blocking worker 串行调用，必须遵守 CaptureLimits 并定期检查
CancelToken。平台会话必须在这一执行上下文创建、使用和销毁，或提供自己的
Backend 亲和适配；宿主不替调用方创建授权或跨线程共享平台 session。

start 只注册 worker，不生产帧。外部 owner 通过 set_demand(true) 声明有授权消费者，
创建 kairo periodic timer；tick 只向 LatestMailbox 投递需求并唤醒 worker。慢 capture
合并 tick，永不并发调用 provider。set_demand(false) 取消 timer 和在途 capture，
推进需求代际、清空最新帧；旧 tick、迟到 capture 不得跨代发布。取消不强杀平台
调用，实际停止上界依赖 provider 的合作式检查。stop 必须由非 worker owner 调用，
最后外部 owner 才可 shutdown Executor。timer 回调使用弱所有权，不依赖取消能够
撤回已派发回调。Executor 先关闭也必须取消 capture、释放最新帧且不死锁。
已接纳 worker 在退出前释放 capture 函数拥有的 Backend 状态，避免 executor 注册表
保留 worker 对象而延长平台资源驻留；需在线程内创建的平台资源应由该函数懒创建
并独占拥有。未接纳 worker 时没有平台调用，函数在外部 owner 清理。
撤销需求停止本宿主取帧；Backend 自主推送的流和平台 buffer 仍需平台 session
owner 停止/归还。本轮没有接真实 Portal/其他推送源，不宣称它们已经按需求暂停。

每次成功帧携带 source_id、需求 epoch、宿主 sequence、采集完成时间及平台原始
source_sequence；后者为零时不能冒充有平台新帧证明。完整像素交付，不传裸 patch，
消费者的分析/发布基线继续独立维护。每次需求重开形成新 epoch；不从当前像素推断
输入权限或业务成功。时间戳是宿主接收时间，不是 compositor 的原始采样时间。

需求 epoch 不替代平台 authority/placement revision。本层没有推断 display transform，
消费者需另外提供可信的同源坐标变换；授权、目标来源或位置/变换变更时，owner
须撤销旧需求、重绑定来源并推进分析策略版本。SharedCaptureFrame 本身只用于
背景观察，不作为动作/核验引用的授权或新鲜度凭证。

## 首增量资源契约

以下为此 opt-in API 的暂定配置默认值，非产品性能参数；负责人 Mirage 维护者，
最迟在 M9-07/12 的目标设备实测前复核。

| 资源 | 首增量上限及行为 |
| --- | --- |
| 采集源/worker | 每宿主一个；进程总源数由外部 owner 限定 |
| 背景采样 | 默认 250 ms，允许 10 ms～60 s；软调度，不承诺 FPS |
| 单次 capture | 默认 2 s 合作式期限；超时结果不发布且暂停生产 |
| 单帧分配 | 默认 8 MiB，配置不超过 256 MiB；检查 vector capacity 及 BGRA 布局 |
| 存活像素 | 固定两代，含最新、在途采集与消费者/派生 Mirador owner；最多 2 × max_frame_bytes |
| 背压 | 无空代槽则不调用 provider；释放旧帧后下个 tick 恢复，无额外队列 |
| 帧复用 | 移动 provider vector 到不可变 owner；Mirador view 共享同一 owner，新增像素拷贝为零 |
| 诊断 | 有界错误码/消息、实际 capture/发布/取消/超时/背压计数及 kairo mailbox 统计投影 |

provider 必须在分配前遵守 max_bytes；宿主对返回数据的校验不能约束违规 provider
在返回前的临时内存。这里只限定本宿主管理的像素分配，不代表平台 buffer、模型
缓存、派生转换或全产品 RSS 上限。消费者主动复制像素的额外分配应单独预算。

输出 LatestMailbox 传弱帧引用，worker 持有当前帧的唯一宿主强引用；固定弱代槽
只跟踪像素所有权，避免通信原语内部历史值延长像素驻留。消费者取得共享强引用，
派生 Mirador Frame 的 owner 同样占用该代槽。撤权不能收回已交付的内存，调用方
必须按 epoch 拒绝旧证据；它不再能通过宿主查询为当前帧。

provider 拒绝、无效布局、超预算、超时或异常暂停生产并清空最新值，显式重开需求
才能恢复；不静默无限重试。异常同时重新抛给 kairo blocking-worker 失败设施。
mailbox 覆盖由 CommStats 计量，覆盖不等于算法跳帧；动作/等待结算不走此背景通道。

## 验收

使用真实 kairo worker/timer 和有界 fake capture，验证无需求零采集、串行合并、
多消费者同址复用、两代持有背压及恢复、代际失效、晚到/超时/取消、provider
异常、无效帧/预算、admission 拒绝与两种关闭顺序。接入 VisualSessionHost 证明
共享帧可用于既有变化门控且没有后台 OCR 全量重跑。ASAN/UBSAN 和 TSAN 分别取证。
真实桌面、4K 多源、低端设备资源/慢消费者 soak、平台授权与 pet 像素验收仍由
M9-12 执行；不能用 fake 时间或容量算术声明性能收益。

本轮实际命令、166 项专项检查、sanitizer/Release 与 Debug 全量结果，以及
TSAN 启动限制见[验证记录](../compatibility/continuous-capture-infrastructure-20261022.md)。

2026-10-22 后续串联：共享帧进入同源 VisualSessionHost 的 Experimental 关系/
规划路径，64 代采集到分析周转验证两代像素预算。结算后请求立即释放源帧，避免
邮箱历史槽占住旧像素；新的消费范围与实际测试见
[消费设计](continuous-visual-analysis.md)及[pin 审计](../supply-chain/mirador-continuous-perception-20261022.md)。
