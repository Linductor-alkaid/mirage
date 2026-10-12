# DEC-052：通用会话中的视觉工具与键鼠输入

> 状态：Accepted
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 冻结里程碑：M7（M7-01 实现前）
> 替代/被替代：无；建议扩展 DEC-034/DEC-024，细化 DEC-016 引用使用纪律

## 背景与问题

立项时原生会话使用公开 ConversationLoop，仅暴露 wait。M2/M3 的 Provider 和视觉
适配已经存在；真实图片工具回填、生产视觉后端、完整输入释放与当前会话接线尚未闭合。
能力核对见[调研](../research/2026-10-09-visual-tools-and-input-capability-review.md)。

## 决策

维护者授权闭合 M7-01/03，按 [DEC-054](DEC-054-scoped-complete-desktop-actions.md)
冻结结构化观察/完整动作边界。图片、OCR 与视觉产品接线仍按 M7-04～07 独立交付。

1. 保留通用 ConversationLoop；桌面工具经 BuiltinToolRegistry 扩展，在
   integration/mira 复用 DesktopAtomToolset 的 spec/handler，服务注入依赖与权限。
   不默认开放其所有副作用工具，不重新实现 Agent 循环。
2. 首增量用结构化观察与完整离散动作打通闭环。后续截图以标准编码 Artifact +
   ImagePart 入模型；逐轮输入扩展通过 Mira 反馈流程推进，见
   [MIRA-20261009-001](../dependency_feedback/mira.md)。
3. 感知优先语义，补真实 OCR、模板/几何，再按需求用 VLM/SoM。
   原图与 SoM 同源，使用显式坐标变换；不把几何候选宣称为可点击控件。
4. 引用动作携带 session、observation/scope、环境 epoch；解析与执行前核验完整身份、
   新鲜度、前台窗口和 display。跨快照 @vN 重名不得回指新目标；核验不能仅在
   resolve 前读取 current()，还需防止核验与动作之间的换代竞态。
5. 模型优先调用完整 click、key chord、type_text；按下/释放由有所有权的动作上下文
   管理，取消/紧急停止/关闭必须释放本动作持有的输入。共享桌面先单写者，第二个
   活动会话动作明确拒绝；只读观察可并存。禁止无主 key-down/button-down。
6. 所有捕获和输入分别经 screen.capture/input.inject；每轮只读限制与 PermissionController
   共同判定。引用快照和截图是非可信外部数据，不提升为系统指令。事件只记摘要与引用，
   不复制截图、输入文本或凭据。
7. 回执区分拒绝、已派发、完成与不确定，保留 side_effect_may_have_occurred。
   后观察是执行证据，单纯画面变化或 ConversationOutcome::Answered 不证明目标满足；
   业务验证缺席时明确报告未验证。未知副作用不自动重试。
8. Runtime Service 唯一 owner；有限步骤用 submit_auto/submit_cancellable 且消费 future，
   视觉沿用串行 blocking worker，UI 分发沿用 comm。取消从任务 owner 桥接到 Provider
   token，不为取消轮询建立私有线程/队列；queued timeout 不冒充运行中强制终止。

## 备选方案

- 直接改用 AgentLoop：可另行评估，但当前 binding 拒绝输入，设备决策和验证语义需
  同时扩展；首增量成本超出接工具范围。
- Mirage 自建 multimodal model/tool 循环：重复 Mira 能力，否决。
- 只暴露裸坐标和 press/release：无法闭合旧观察与取消释放，否决为产品默认。

## 影响与风险

涉及工具 schema、引用/动作回执、权限、artifact 和模型能力投影；需同步设计、协议
golden/示例、安全与平台证据。本轮将已冻结契约补入主设计，落地状态与验证由 M7 计划跟踪。
真实 OCR 可选 runtime 与模型交付需供应链审计；原生 Wayland 需独立授权采集/输入适配，
XWayland 能操作 X11 窗口不能推出可控制整个 Wayland 桌面。

## 验证方式与关联工作

[M7](../plans/m7-visual-tools-and-input.md) 的失败矩阵、三类真实任务和平台门禁。
依据：[DEC-016](DEC-016-mirador-visual-integration-contract.md)、
[DEC-024](DEC-024-desktop-atom-toolset.md)、[DEC-034](DEC-034-native-model-agent-loop.md)、
[工程规范](../project/project-standards.md) 5/7/8/9.4。
