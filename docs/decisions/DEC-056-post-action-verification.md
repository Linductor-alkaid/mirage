# DEC-056：会话动作后的新观察与条件证据

> 状态：Accepted（维护者授权继续推进 M7-08）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-08](../plans/m7-visual-tools-and-input.md)
> 扩展：DEC-054/055

## 选型与边界

Mira AgentLoop 已提供设备 Observe/Act/Verify 和 ILoopVerifier；ConversationLoop 明确
只以 Answered 结束，不执行设备验证。原生会话继续复用后者及公开 supplier，不另建
循环或状态机。Mirage 的窗口/语义后条件属于产品桌面策略，在 Adapter 中核验；
观察的时间/质量要求复用 mira::evaluate_observation，生命周期沿既有 Executor task。
Mirador 的 OCR/SoM 未被本项替代，视觉条件待 M7-06/07 后另验。

## 契约

四个会话修改工具升至 2.1.0，原有 required 字段保持。新增可选 verify_kind：
window_focused、window_title_equals、semantic_node_exists；后两者分别需 expected_title
或 expected_role/expected_name。字符串各最多 1024 UTF-8 字节。预期条件必须在修改
前声明，验证工具不能事后更换；激活省略条件时默认核验目标前台窗口，其他动作省略
条件仍可派发，但终态不能报告业务验证成功。匹配只证明声明的局部后条件，不能证明
任意用户意图或因果关系。

每次 handler 结束、输入租约结算后强制新观察。前台窗口、任务/会话/epoch、取消与
采集前后几何须一致；语义条件使用新树、512 节点预算，名字/角色精确匹配，重复命中
拒绝。单次核验协作预算 1 秒，无固定 sleep 作为证据。回执区分派发与验证，并携带
action_id、verification.status/reason/condition_digest/evidence_id/environment_epoch，
以及实际窗口/几何/语义匹配数的 observed_digest 和 captured_at_ms。标题最多 64 KiB；
新鲜度用单调时钟，时间戳仅供展示。
原始期望文本与语义名不复制到普通过程面，只保留摘要和身份。

新增只读 desktop.action.verify(action_id)，仅核验本轮最近动作、固定条件及当前 epoch。
允许异步业务更新后再次采集，不重新派发动作，不改变条件。未验证最近动作时拒绝
下一修改；不确定副作用后本轮所有修改保持拒绝，等待用户检查。失败/旧/异会话 ID
不能恢复为成功。最多 32 条动作记录，沿用 loop 的总工具预算，无队列/worker/定时器。

supplier 仅送非可信动作证据摘要；终态再采集核验最近动作。只有所有声明的动作
后条件都有通过证据，宿主才保留模型回答，并明确其核验范围。规范失败 ToolResultPart
是失败事实源，过程投影的 16 KiB/总 64 KiB 预算不能绕过门禁。拒绝、缺条件、条件
不符、观察失败、环境变化或 uncertain 时转为安全 failed/未验证终态，抑制模型的
“已成功”回答；Cancelled 优先。过程卡显示目标和核验状态，展开可查证据身份。

## 验证

fixture 覆盖假成功、缺条件、拒绝、异常/取消/超时、环境/前台变化、迟到与不确定输入
自动重发拒绝、条件固定及证据脱敏。Linux 私有 Xvfb 中用真实 X11 Provider 验证
窗口激活、文本回显、提交落盘三类本地应用任务；不以 Fake Provider 冒充目标平台
业务验收。真实语义表单、OCR/Canvas 与 Windows/Wayland 保持各自未完成门禁。
命令、迭代记录和首交付范围见[验收记录](../compatibility/post-action-verification-20261009.md)。

## 后续扩展（2026-10-10）

[DEC-059](DEC-059-visual-text-postconditions.md)增加固定 OCR 文本后条件与严格新
识别证据；实际 Xvfb Canvas 正/负例见
[第二增量验收](../compatibility/visual-text-canvas-20261010.md)。原窗口/语义契约和
未完成的用户宿主/在线模型/图标/平台门禁不变。

## 2026-10-11 部分更新

[DEC-068](DEC-068-bundled-vision-and-recoverable-tool-rejection.md) 在 Linux 产品首范围
更新本决定的以下部分：本轮自身可证明未派发且无副作用的拒绝允许纠正后再试；未知、已派发未验证与不确定回执仍封锁修改，最后动作未验证不得结算。 其余契约继续有效。

## 2026-10-14 部分更新

[DEC-086](DEC-086-dbus-activatable-launch-and-evidence-fence-lift.md) 修订"不确定副作用
等待用户检查"的适用范围：`application_running` 条件事后取得机器证据（运行中 + 实例 id）
时，记录落 verified、围栏解除、settle 通过；`canonical_failure_` 按记录当前状态派生。
点击/窗口类条件的 uncertain 仍维持人工复核。其余契约继续有效。
