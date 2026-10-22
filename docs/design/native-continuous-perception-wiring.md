# 原生任务的连续感知接线

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-05/06/07/11/12/18；最新会话修复关联 M7-18/25/26
> 依据：[DEC-091](../decisions/DEC-091-local-semantic-perception-and-action-loop.md)、
> [DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)

## 本轮交付契约

维护者授权修复最新会话问题并进行 M9 接线。首个产品消费者使用任务已授权的原生帧，
不为接线重开 Portal、不在另一个 worker 使用交互 session、不增加后台周期取图。
默认 PP-OCR pipeline 开启图像空间连续分析；单一 VisualSessionHost worker 内维护
独立的 display 与 image-space session/coordinator，二者复用既有 backend，串行执行，
不新增模型驻留或 Executor owner。独立状态避免图像 ROI/source reset 清掉 display
模板、混入全局引用或把历史查询当成当前观察。共用原有 latest-wins admission、取消、
future 和 shutdown；查询也使用这一个 mailbox。

图像空间附加缓存预算为 frame 1 MiB、result 2 MiB；连续协调器 512 KiB，关系视图
512 KiB、计划 64 KiB、实体 256、关系 1024、事件 256。输出沿用 OCR 的区域/JSON
预算，不附完整关系表或事件正文。未请求图像空间分析的嵌入保持原行为。

同一 session/task/task_epoch/authority/源类型/ROI 原点与尺寸/预览尺寸具有稳定来源键；任一变化推进
source revision。普通动作 epoch 不等同于 source revision，但仍限制当前工具证据
和输入授权。读取缓存状态必须匹配当前任务的来源键并检查 authority；返回最后分析
帧和年龄，无帧查询不 capture、不 OCR、不形成当前可执行引用或核验证据。

预算化 OCR 保留现有五秒上限、K=64 和 partial/deferred 契约。连续融合和规划采用
其真实结果，不把部分、缓存或未完成证据登记为新的完整覆盖；本轮是 advisory 状态，
不伪造 accept_work 或确认事件。正常取图、动作及后条件仍走原有新鲜度和源帧水位。

文本优先入口允许显式 text_only OCR：已授权原始帧在预算内缩放后直接分析，不生成
PNG/artifact，也不发送图像。返回 image_pixels 文本框、来源元数据与连续状态摘要；
输入仍需显式取得并供给图像的新引用。旧 image_scope 在文本入口被退休，不能依靠
历史图片继续派发动作。需要视觉细节时显式 capture；不降低旧输入守卫。

## 后续验收边界

后台采样/静态退避、ROI 增量复用、焦点策略、事件订阅/gap 恢复、目标标定及完整
文本输入授权继续按专项计划推进。本轮须验证产品 callback 接通、同 worker 隔离、
来源重置、无帧查询、无 PNG 文本路径、部分 OCR、不跨权限/任务复用、拒绝/异常/
取消/期限/关闭；真实低端设备资源、Windows 与 compositor/pet 像素验收保持开放。


## 当前验证

本轮实现已通过最终 Debug 全量、三个 sanitizer 专项、native-release 产品与受控
PP-OCR/native agent 回归；窗口/应用另有当前宿主的只读探针。测试失败修复、
实际计数、二进制摘要和未验收项见
[验证记录](../compatibility/native-observation-and-continuous-wiring-20261022.md)。
背景采集需求/生命周期未接到交互 session，不能把这个按需消费者当作连续后台监控。
