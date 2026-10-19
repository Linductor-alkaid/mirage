# DEC-054：作用域观察与完整桌面输入

> 状态：Accepted
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：M7-01 / M7-03（Linux X11 首交付）；扩展 DEC-052/053
> 冻结里程碑：M7-01
> 替代/被替代：扩展 DEC-053，不删除首增量历史

## 背景与问题

会话首增量已接观察、激活与文本工具，但只有窗口 ID 不能阻止旧观察重名引用，原始
按下/释放调用也不能表达完整输入的所有权。现有 Provider 需增加作用域与清理结果，
服务需要共享资源准入，才能在保持 Mira loop/Executor 边界的同时开放 click/key。

## 契约

维护者授权闭合 M7-01/03。保留公开 ConversationLoop 和原有工作流 schema；
会话修改工具统一要求 window_id、observation_scope、environment_epoch。
window.list 签发可用于窗口激活的观察；window.front / semantic_snapshot 签发当前
窗口输入观察。每轮只保留最新一份观察，绑定会话，最多 512 个语义节点、5 秒有效期。
scope 是不可预测的新 ID；epoch 由 Runtime 唯一 ActionCoordinator 在动作准入、
结算、接管与恢复时推进。平台外部变化另通过前台 ID/几何和 backend scope 核验，
不声称 epoch 自动覆盖所有 OS 事件。

语义快照返回 backend scope，AT-SPI2/UIA 在其已有锁内验证 scope 后执行，阻止
@eN 重名回指。视觉注册表提供 scope 与 ref 的同锁执行边界，供后续 M7-07 消费；
当前会话仅接受语义 @e 引用，不开放无来源坐标或图片工具。

窗口激活、type_text、完整 key、语义 click 经共享权限控制器逐次判定，权限等待后
重新核验身份/新鲜度。ActionCoordinator 无等待队列，第二个动作（含另一会话或
工作流）明确 desktop_busy；读取可并存。拿到 lease 后检查前台与几何，再执行有界
平台动作。输入工具成功只表示 dispatched，verified=false；未知副作用不自动重试。

InputOutcome 增加 side_effect_may_have_occurred 和 input_released。完整 key/click
需 Backend 明确声明支持；原始 down/up 不作为会话工具。Backend 先预检物理状态和
全部字符/键映射，只释放本动作发出的按下。每个释放使用独立清理路径，取消和原动作
超时不能禁止释放；异常/部分派发/释放失败返回 uncertain，释放失败使 owner 保持
fenced，恢复不得掩盖失败。普通动作 1 秒，文本 64 KiB，清理不复用已取消 token。

Runtime 的 supervisor 接管入口停止当前 lease 并拒绝新动作；恢复需无活动/脏输入，
随后旧观察全部失效。shutdown 首先关闭动作准入，再取消/排空 Executor 任务，
未释放输入使 shutdown 报告失败。所有动作仍在既有 Executor 有限任务内推进，
不引入线程、调度器、通信队列或平行任务监控。

X11 支持 ASCII/现有键盘映射及换行；中文缺映射时整批在派发前拒绝，不改键盘布局或
剪贴板。Windows/Wayland 真机和完整视觉动作在 M7-10 / M7-07 单独验收；不以本机
结果扩大平台声明。截图 artifact / 逐轮 ImagePart / OCR 的预算与来源纪律沿 DEC-052，
实际编码/模型扩展分别由 M7-04/05/06 交付。

## 备选方案与影响

独立 down/up 工具让取消发生在两次模型调用之间，无法由单个有界动作收敛，拒绝为默认
接口。只在 resolve 前读取 current scope 不能阻止发布竞态，采用 Backend/registry 同锁
验证和执行。为输入建立等待队列会引入排队期间观察过期与生命周期负担，当前直接拒绝
第二个 lease，仍使用既有 Executor 完成/失败/取消设施。

会话修改工具升为 2.0.0；原 workflow 输入 schema 保持兼容，Runtime 注入同一 owner。
物理状态核验与 OS 派发仍有外部竞争，不能保证跨进程原子输入或强制中断 Xlib。释放
不确定时保守 fence 会要求外部检查/清理和 Runtime 重启，不能用普通 resume 掩盖失败。
完整安全边界及示例见[桌面动作契约](../security/desktop-actions.md)。

## 验证要求

同名旧 scope、不同会话、epoch/前台/几何变化、权限拒绝、schema/budget 负向；
按下后取消/异常/超时/shutdown 的释放、释放失败 fence、第二动作拒绝与恢复重观察；
真实 X11 点击/组合键/换行、中文整体拒绝与物理按下状态；Debug/Release、ASAN/UBSAN、
TSAN 和既有回归。实现状态与实际证据追加到 [M7](../plans/m7-visual-tools-and-input.md)。

2026-10-19：跨观察持键及设备层输入的后续扩展见
[DEC-110](DEC-110-continuous-input-and-device-compatibility.md)（Proposed）。本记录的
单次完整动作与释放失败语义继续生效，新会话须独立版本化，不能直接开放无主 down/up。

关联：[DEC-052](DEC-052-visual-tools-and-input.md)、[DEC-053](DEC-053-conversation-desktop-tool-registration.md)、
[兼容性证据](../compatibility/scoped-complete-input-20261009.md)。
