# DEC-133：绑定实际供图证据的显式内容任务评估

> 状态：Accepted（维护者继续推进M9，M9-30第二增量）
> 日期：2026-10-28；负责人：Mirage维护者
> 关联：DEC-121/132；M9-30/24

## 决策

增加desktop.vision.content：begin声明observed_samples或entire_video及requires_audio，
assess提交answerable/insufficient、≤1024B回答、≤512B限制与最多4个证据引用；
status仅按需读现有状态。开始前未激活，不按自然语言关键词猜测用户意图。模型
提示要求内容任务先声明范围；范围/音频需求在本turn不可降级，同配置重复begin
幂等，interrupted时同配置begin可显式重开。此范围是模型解释，不宣称已独立验证
模型对用户意图的判断；自动意图识别及真实内容质量仍需验收。

证据引用由Adapter按task/session/epoch命名空间、来源段、帧ID/帧号/单调时刻
摘要生成。工具供图可给出待消费引用，但只有通过DEC-132完成请求消费、仍在
当前32样本窗口的引用才能用于评估。不得凭工具成功、未呈现图片、外任务、过期
来源或任意ID完成评估；同像素/ABA不复活。状态机为idle→locating→observing_content
→coverage_assessed/insufficient，证据失效进入interrupted。它是内容领域策略，不
代替Mira Task/Session状态机；playback_confirmed/播放器时间仍未知。

answerable必须引用实际内容证据且回答非空；entire_video或requires_audio因当前
缺完整播放/起止/音频覆盖，自动成为insufficient并丢弃所提“完整回答”。显式
insufficient可无引用，限制说明必填。新证据会使已有评估回到observing_content，
需重新评估；来源段变化或被引用样本淘汰使评估失效，不能静默沿用旧结论。

## 产品收口

复用现有Mira ConversationLoop和Mirage产品settlement边界；不重写运行循环。
已激活内容任务必须有当前有效评估才可成功收口。成功仅允许observed_samples且
不需音频，展示已校验证据归属的有界assessment回答，并固定披露“仅基于稀疏片段”；
不使用随后自由生成的终答覆盖该范围。未评估/insufficient/interrupted返回明确的
内容证据不足失败，不把打开/播放/文字终答当完成，也不追加无界推理重试。其他
动作、供给、取消失败仍优先，取消不能被内容评估复活。未调用begin的非内容任务
行为不变；该门禁不保证模型一定声明正确任务范围，亦不独立证明回答语义正确。

## 预算与承载

复用单个DesktopImageTurn及DEC-132元数据；一项意图、一项当前评估、最多4引用，
额外保留≤4KiB，无像素、worker、定时器或队列。自动content_task摘要≤512B，
不重复长回答；引用主要随实际发图元数据给出，status最多回传4项（≤1KiB）。
整个连续观察仍≤8KiB，原日志/预览容量不变，不固定逐帧调用LLM。

核对Mira公开ConversationLoop及既有ObservedProvider/产品settlement；Mira承载
生命周期，Mirage仅判断本产品内容证据。Mirador的识别coverage不等于该内容任务
问题可回答，不重复视觉算法、不修改固定依赖。

## 验证与限制

纯状态/引用认证、未消费/跨任务/源/gap/淘汰、范围不可降级、显式重开、非空回答、
不足/音频/全视频门禁、原子拒绝、预算；真实工具、Completed供图及ModelLayer
终答覆盖/失败/取消，五配置与格式/公开边界后提交。真实媒体、播放器状态、在线
模型正确声明范围/回答内容、MSVC和交互平台未执行时单列补跑条件。M9-30整体
继续In Progress，不以本机协议用例宣称完整视频理解。


实际配置矩阵、失败修复及未执行范围见
[验收记录](../compatibility/grounded-content-assessment-20261028.md)，工具协议见
[示例](../examples/grounded-content-assessment.md)。M9-30按计划继续In Progress。
