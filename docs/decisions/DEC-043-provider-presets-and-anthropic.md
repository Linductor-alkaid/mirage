# DEC-043：厂商预设、Messages协议与视觉验证

> 状态：Accepted
> 日期：2026-10-06
> 依据：维护者本轮明确要求；M6-22；依赖修改和上游PR已授权

沿用DEC-042原生服务编辑器，预设在服务导航中可选择。预设提供公开Base URL、明确方言、默认模型及上下文预算，用户只需提供API Key后保存。连接字段仍可编辑；预设不冒充已配置服务，也不共享其他厂商的凭据，不自动发起网络请求。配置确认以服务ACK为准。未取得互操作证据的预设仅表示配置便利。

Messages方言由Mira通用模型层承载，新增anthropic.messages.v1、明确/v1/messages路由、SecretRef的x-api-key认证与有界SSE解析；不在Mirage实现私有模型循环或增加线程。工具调用与结果仍使用规范契约，失败/缺终态不得成为成功。首阶段支持文本、图片、普通工具；不可表达参数、extended thinking和非文本/工具响应显式拒绝，防止丢失必须回填的签名内容。MiniMax-M3使用默认关闭thinking的Messages入口做真实验证。

会话侧栏采用单一内容轴与24px外边距，品牌、操作和历史列表保持同轴；服务标题编辑器留8px文字内边距，静态标题保持相同文字位置。正常/最小窗口明暗状态统一验收。

MiniMax识图先按官方API规格核对本机模型（MiniMax-M3），使用无用户隐私的随机图形/文本测试图，经Mira公开Provider实际发送图片字节并验证返回内容，不能将文字连通性当成视觉能力证据。产品图片附件能力与此验证必须区分，未交付时明确记录。

2026-10-07：首阶段 thinking 拒绝边界已由 [DEC-046](DEC-046-session-thinking-and-empty-recovery.md) 替代；其余预设、认证、侧栏和视觉验证决策保持有效。

## 2026-10-12：DeepSeek Anthropic 工具名适配

用户的 DeepSeek 会话在发送“你好”后得到 `invalid_request_error`。使用已保存 Key 的
受控请求证明：相同端点的纯文本流式请求与简单工具请求均为 HTTP 200；工具名
`desktop.window.list` 返回 HTTP 400，响应要求 `^[a-zA-Z0-9_-]+$`；改成
`desktop_window_list` 则为 HTTP 200。根因是 Mirage 的桌面工具名含点号，DeepSeek
拒绝这种名称，与 `/anthropic` 或 `/anthropic/v1` 的路径无关。

仅对官方 DeepSeek Messages 配置，在 Mirage 的模型 Provider 边界映射工具名中的点号为
下划线。Mira ConversationLoop、工具 Registry、动作校验和用户可见工具记录仍使用原始
名称；发往服务端的工具声明及后续 assistant 工具调用回放使用映射名，返回的工具调用
按原工具 ID 与名称反向映射。映射冲突或不合法名称显式拒绝，不猜测工具身份。
不修改 pinned Mira，也不新增网络、线程或 Executor 生命周期。

## 2026-10-12：HUA Chat Completions 工具名适配

最新 HUA `gpt-6.1-sol` 会话首请求被拒绝。相同配置的受控纯文本请求 HTTP 200，
声明 `desktop.window.list` 时 HTTP 400 / `invalid_request_error`，唯一改为
`desktop_window_list` 后 HTTP 200。复用前述名称适配，把适用边界扩展为
`openai.chat-completions.v1` 配置，不硬编码 HUA 地址或修改用户模型/凭据。
Messages 仍只对既有官方 DeepSeek 配置启用；Responses 不在本次扩展范围。
规范工具名、工具 ID、调用 ID、结果和权限校验保持原契约，回放与返回调用均按同一
别名表处理。未知返回名交由 Mira 现有解析拒绝，冲突/非法名称在提交请求前显式失败。
承载方是 Mirage 的 Provider Adapter；消费 Mira 公开 IModelProvider 与工具解析能力，
不修改依赖或建立新的模型循环、网络或并发设施。跟踪项为 M6-41。

## 2026-10-12：统一模型侧函数名称适配

维护者在供应商调研后明确要求开始适配。当前 pinned Mira 的 Chat Completions、
Messages、Responses 三方言均通过同一 Mirage Provider Adapter 使用保守名称：
ASCII 字母开头，后续仅字母、数字、下划线，总长 1～64。规范名称中的点号和横线
转换为下划线；原名称以数字或下划线开头时加 `tool_` 前缀，前缀计入长度预算。
空名称、其他字符、超长名称和映射冲突显式拒绝，不静默截断或猜测工具身份。

内部 Registry 名称、ToolId、用户记录、权限与结果关联 ID 保持不变。工具声明与
assistant 回放使用同一转换；命名 tool_choice 继续以原 ToolId 由 pinned Mapper
选择转换后的声明。返回名称仅按当前声明的精确别名和 ToolId 还原；未知名称或
错误 ID 在 Adapter 返回不可重试的协议错误，未形成可执行提案，不产生工具动作。
本修订替代前述厂商筛选范围及未知名留给 Mira 解析的处理点，历史证据保留。宿主明确指名
工具的说明使用同一别名，用户正文、参数、结果和签名思考内容不做字符串替换。
这是统一的保守策略，不宣称各服务真实上限都是 64，也不新增 Gemini/Bedrock 原生
Mapper。哈希缩短/冲突消解如确有接纳需求另行验收；本轮保持显式拒绝。
依据 [名称调研](../research/2026-10-12-provider-tool-name-contracts.md)，工作项 M6-41。
