# M7 慢模型定位、本地知识复用与后续顺序

> 状态：Proposed（后续产品方案；当前证据与接口核对已完成）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07 / M7-09 / M7-11](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-058](../decisions/DEC-058-scoped-visual-marks.md)、[DEC-059](../decisions/DEC-059-visual-text-postconditions.md)、[DEC-023](../decisions/DEC-023-workflow-contract-face.md)

## 结论与范围

认可已验证的受控 Linux X11 视觉、完整输入与动作后验证能力，下一主线转入
M7-09 滚动/拖拽；M7-07 慢模型在线增强与其他未验范围继续保留。当前五秒引用
策略是保守执行契约，不能扩展成“所有在线模型必须在五秒内思考完成”的产品
前提。一次慢请求正确被拒绝证明安全边界生效，仍不等于该业务任务完成。

维护者要求先定位并思考路线，本轮不实现本地语义记忆、App Model 或新的会话
workflow 工具，不改变既有引用期限、权限、失败终态、输入或依赖版本。

## 已能定位到的阶段

原始依据是[网络调整后三例](../compatibility/data/network-adjusted-online-visual-canvas-20261010.json)，
使用 MiniMax-M3 / Messages / none、私有 4K 上公共 480×180 ROI。

| 样本 | mark 开始/完成 | click 开始/完成 | 标注后等待 | 结果 |
| --- | --- | --- | --- | --- |
| cross-right | 1515 / 1849 ms | 3881 / 4223 ms | 2032 ms | 完整正例通过 |
| cross-left | 1843 / 2133 ms | 9541 / 9541 ms | 7408 ms | 正确选号，过期拒绝，零输入 |
| unchanged | 6502 / 6833 ms | 9358 / 9612 ms | 2525 ms | 完整负例通过 |

这些是同一 harness 稳态时钟的过程投影。慢例 mark 用时 290 ms，click 入口
拒绝用时小于投影的一毫秒分辨率；主要间隔位于标注完成后、工具执行前。
源码调用链核对：

1. [ModelLayer](../../integration/mira/src/model_layer.cpp) 使用 pinned ConversationLoop，
   `complete_harness_turn` 本次未传 preview，`inference.stream=false`。
2. [ObservedProvider](../../integration/mira/src/conversation_capture.hpp) 同步调用公开
   `IModelProvider::infer`，完整、有效响应返回后才投影 pending tool；随后 handler
   开始投影 running。当前路径未从未验证的流式片段派发输入。
3. [click_mark](../../integration/mira/src/desktop_visual_tools.cpp) 的 `current()` 在
   重新截图前先要求 captured_ 年龄小于五秒；注入前再次检查。左目标超过期限，
   因此 backend_calls=1 只含标注分析，没有执行前重采集和鼠标派发。

定位结论是“等待下一模型响应的路径与即时引用期限发生冲突”，并未精确定位为
provider 推理慢。7408 ms 还包含模型请求构造/图像供给、传输、服务端处理及响应
解析；回执未分别计时。pinned [TransportTrace](../../third_party/mira/include/mira/model_transport.hpp)
只给 write_started/write_completed/headers_received 进展位，不含 DNS、TLS、
首字节耗时。现有 DialogProcessSink 也没有 infer 开始事件，仅有响应后的工具
投影；不能仅靠该 sink 声称已经测得每次请求时长。

进一步精确取证可在既有 Provider 适配边界采集有界请求开始/返回摘要，或消费
Mira Gateway 的 EventStore 事件，经明确诊断出口投影。它是独立诊断增量，
不需要新线程、调度器或重跑真实输入；本轮未实现，不作为 M7-09 前置。

## 本地知识与执行时事实

建议分开保存可复用知识和临时执行引用。可复用知识表达“保存按钮的语义、已验
外观、应用页面、操作步骤与后条件”；执行时重新观察并解析为当前窗口、目标、
scope/ref 和 epoch。长期保存 @vN、编号或坐标，无法建立当前可执行性。

| 承载方 | pinned 已有能力 | 可以复用什么 | 必须保留的边界 |
| --- | --- | --- | --- |
| mirador | [PerceptionSession](../../third_party/mirador/include/mirador/perception_session.hpp)、变化检测、CapabilityResultCache | 同源内容/ROI/模型版本匹配的 OCR 或检测结果，按变化更新候选 | 缓存和近似变化检测不构成输入授权；缓存存在不表示当前窗口/焦点有效 |
| mirador | [SemanticSnapshot](../../third_party/mirador/include/mirador/semantic_snapshot.hpp)、稳定 ID、[VisualIndex](../../third_party/mirador/include/mirador/visual_index.hpp) | 融合证据、会话内身份、外观匹配和模板候选 | stable_id 仅会话内唯一，引用仍绑定 generation；外观相似不自动证明业务语义或 clickable |
| Mira | [WorkflowRuntime](../../third_party/mira/docs/api/workflow-contracts.md)、ToolCall / Navigate / Verify、App Model | 已验证步骤、条件、状态迁移与本地路径规划 | 运行版本、权限、取消、步预算、到达验证和不确定副作用不能跳过 |
| Mirage | Platform Backend、已有语义 Provider、观察/输入/验证 Adapter | 将知识目标解析为当次可执行对象；给 Mira 当前屏幕状态 | 平台类型留在 Backend，执行前新观察与动作后证据由宿主提供 |

Mirage 已经构造 WorkflowRuntime 并注册桌面 atom：见
[MiraHost](../../runtime/mira_host/src/mira_host.cpp)、
[RuntimeService](../../runtime/service/src/runtime_service.cpp)与
[DesktopAtomToolset](../../integration/mira/include/mirage/integration/desktop_atom_toolset.hpp)。
当前 ConversationLoop 的目标声明仍明确不暴露 workflow 工具；已有工作流承载
不等于会话→本地识别→workflow→新验证这一条产品路径已经交付。

mirador 的缓存主要是进程/会话内感知结果，不是持久化任务语义库。应用语义、
任务知识、已验证 workflow 版本及其持久化，应分别按 Mira 的 App Model/Memory/
workflow 契约和 Mirage 产品存储职责核对，避免新建平行事实源。产品 OCR 验证
当前启用 Refresh，不可为了减少延迟而静默把新验证改成旧缓存命中。

## 后续执行形态与验收

目标形态是：用户意图经模型规划/选择已有任务资产 → 本地识别当前状态 →
解析知识目标并取得新引用 → 有界完整动作 → 新观察与后条件验证。已知步骤由
workflow 本地推进；未知状态、歧义或缺少知识时再交给 Agent，再次执行前重新
观察。模型延迟消耗规划/恢复预算，鼠标按住期间不等待模型回合。

例如已验证“保存”任务保留按钮语义或经验证外观与完成条件。下次任务在当前
页面本地定位唯一目标后执行并核对完成条件；不再为每个已知步骤重复识图请求。
需要重新选择时明确交回 Agent；已拒绝动作、未验证或不确定副作用保持失败纪律。

这不是把现有 mark 的时间戳更新或旧引用续期。新能力须另行冻结目标解析、
置信度/歧义、知识失效、恢复、权限与证据契约。Mira IR 公开实参替换是 `$param`，
不能假定它已经支持任意上一步输出自动绑定下一步 scope/ref；宿主绑定与
ScreenStateProvider 接线仍需核对。后者必须廉价非阻塞，不能在回调内临时做 OCR。

M7-11 先作为 Proposed 设计项保留，后续最小验证场景包括：已知任务本地执行不
触发逐步模型请求；目标移动/重名/遮挡时重新解析或拒绝；任务版本和窗口身份
变化使旧知识失效；未知状态交回 Agent；动作后新验证、取消/接管和释放失败
保持既有规则。没有实现和运行证据前，不标记该能力完成。

M7-09 的前置 M7-03/08 已完成。下一实施批次先冻结 scroll 和完整 drag 的预算、
新鲜起终点、单写者租约、运行中取消/接管与最终释放契约，再做 Linux X11 受控
真实输入和负例。Windows 与其他平台按原计划独立验收；慢模型三例不阻塞该批，
也不以新批通过代替 M7-07 的未完成项。本轮仅完成分析和顺序调整，未执行新
模型请求、产品测试、依赖升级、提交或推送。
