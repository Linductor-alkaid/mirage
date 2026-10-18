# DEC-103：桌面工具期间的桌宠整窗排除

> 状态：Accepted
> 日期：2026-10-18
> 负责人：Mirage 维护者
> 冻结里程碑：M8-06
> 替代/被替代：无；细化 DEC-084/089

## 背景与问题

桌宠和气泡会覆盖目标、污染截图与 OCR；气泡输入穿透不能证明底层图像未受遮挡。
M8 前五项已有持续使用反馈，本轮推进 M8-06 的 Linux 工具链首增量。

## 决策

先采用有界工具调用期间隐藏整窗的保守策略，不在旧截图上推算安全停靠位置。
工具租约在无桌宠时也持有启动门禁：租约释放前拒绝创建新 pet 进程，防止用户在
观察/拖动中途启动或重启桌宠重新遮挡目标。已有进程的重复 Start 保持幂等；Stop
与 owner shutdown 不等待租约或动画，租约仍阻止替代进程抢先出现。

隐藏在工具的观察、输入和核验之前生效，持续至 handler 和核验结束；下一次工具
重新排除，再由既有逻辑取得新证据。原焦点、遮挡、权限和输入租约判定保持有效。
自动迁移及按目标区域优化仍留在 M8-06 后续门禁。

Runtime 通过当前 PetProcess 的实际受管进程建立排除租约，不允许模型指定 PID、
窗口或隐藏状态。Platform Backend 使用 X-Resource 1.2 的服务端 client PID、
_NET_WM_PID 和桌宠协议标记共同核验唯一窗口；同名、伪造属性或 PID 不够。
未启动桌宠时为无操作；正在启动、停止、身份不明、窗口不唯一或能力不足时拒绝。

Linux X11 采用空 ShapeBounding，覆盖窗口内角色、气泡、尾部和阴影，输入区域也
受 bounding shape 裁剪。保持窗口映射、位置与焦点，不向底层补画虚构像素。租约
持有原 bounding shape，重叠调用引用计数，最后释放才恢复；每次进入与释放核验
身份/形状，失败向工具返回明确错误。异常由 RAII 收敛；排除租约不新建 worker、队列或 timer。
同轮隐藏退出修复单独拆出 200 ms kairo control timer，与 30 秒 IPC 同步解耦。
渲染在短时排除期间继续消费最新状态，保证新文字不能突破服务端 shape、恢复时
不重播隐藏期间的陈旧内容；隐藏期节能和长等待暂停另行优化，不宣称已经完成。

该实现接入 conversational harness、workflow desktop atoms 和直接 IPC desktop.observe；
普通主窗口、第三方遮挡和 Overlay 不进入排除白名单。原生 Wayland/Portal、Windows
及 compositor 重绘确认属于独立后续门禁。Wayland 环境拒绝本排除能力，不将
XWayland shape 当作 Portal 已获得干净新帧的证据。

### 直接 IPC 观察（2026-10-18 第二增量）

观察 handler 已在 serial owner 上运行，直接准入呈现租约，禁止重新 submit/get 自等待。
五秒总预算覆盖许可、排除、采集、组装与末次核验；失败或取消也先显式恢复再应答。
语义/窗口/指针观察同样受租约保护。视觉许可在隐藏之前判断；拒绝不触碰窗口。

有产品 pet owner 时，visual=true 必须走真实运行中的视觉 pipeline，隐藏后重新采集，
不得从预先发布的缓存推断避让成功，即使 pet 此刻停止也不能证明旧代次来源。
不配置 pet 的嵌入保留 registry-only 兼容路径。响应使用本次 refresh 返回的精确视觉
代次；若组装期间 epoch 或 registry 代次变化，明确拒绝，不替换成别的 producer 结果。
wire 形状和错误码集合保持兼容，仍使用 unavailable / permission_denied，详情给出原因。

## 备选方案

- 按目标移动：需要跨屏安全位置和几何版本协议；保留后续优化。
- 整轮隐藏：用户长推理期间失去状态呈现，不采用。
- 标题/PID 白名单或截图遮罩：无法证明真实所有权或恢复底层像素，不采用。

## 影响与风险

新增无平台/依赖类型的进程呈现租约，以及 integration 回调。pinned Mira 的公开
Runtime/环境 API 与 Mirador 图像 API 未提供产品子进程窗口排除，本策略属于 Mirage。
任务仍由现有 kairo 路径承载，串行 owner 负责准入，有限工具任务负责使用和释放。
X11 同一安全会话可被其他客户端修改；每次校验拒绝可观察失配，不把它当安全隔离。
XSync 只证明 X server 请求完成，不证明真实 compositor 已完成重绘；实机截图及
输入闭环通过前，M8-06 保持 In Progress。

## 验证方式

隔离 Xvfb 验证真实像素和输入命中、气泡区域、属性伪造、重复窗口、嵌套租约、
取消、形状变化、释放恢复与窗口销毁；integration 验证拒绝时 handler 不执行、
异常/取消/后置核验恢复以及无桌宠路径。执行 ASAN/UBSAN，跨上下文增加 TSAN。
真实 compositor、Overlay、在线桌面会话和其他平台分别补跑。

## 关联文档和工作项

[M8-06](../plans/m8-mira-pet.md)、[桌宠设计](../design/mira-pet.md)、
[气泡设计](../design/mira-pet-bubbles.md)、[DEC-084](DEC-084-mira-pet-process-and-animation.md)。
