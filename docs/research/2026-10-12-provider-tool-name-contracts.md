# 主流模型服务的工具名称接口调研

> 状态：Completed（文档调研）；跨供应商运行验证与统一别名策略实施未完成
> 日期：2026-10-12
> 负责人：Mirage 维护者
> 关联：M6-41、DEC-043
> 方法：读取供应商官方 API 文档/公开 schema，对照当前 pinned Mira 和 Mirage Adapter；复用此前 HUA/DeepSeek 证据

## 结论

含点号的 Mirage 内部名称不能直接作为跨供应商的模型工具名称。OpenAI Chat
Completions、Azure OpenAI Chat、Anthropic Messages、DeepSeek Chat、百炼 Chat、
Z.ai/BigModel、Kimi Chat 和 Bedrock 的已查规范均排除点号。HUA 与 DeepSeek
Messages 的既有实际请求也已证明点号拒绝。

限制随接口变化，不能只按模型厂商归类：有 64 与 128 两种常见上限；Google 原生
声明允许点号，Vertex AI 还有首字符要求；Bedrock Runtime 与 Agent 的规则不同。
“OpenAI/Anthropic compatible”不是每项校验完全一致的承诺。此处是围绕 Mirage
预设及常见云/聚合服务的样本调研，不是市场占有率统计，也不是全供应商实测。

根据已确认规范求交集，建议 Mirage 的默认对外函数别名采用：

```text
^[A-Za-z][A-Za-z0-9_]{0,63}$
```

即 ASCII 字母开头，仅字母、数字、下划线，总长 1～64。它比多数接口更保守，涵盖
Bedrock Agent 的字母开头/无横线约束。内部名称可继续使用 `desktop.window.list`，
对外别名如 `desktop_window_list`。这是工程建议；未明示规范的供应商仍需运行验证。

## 已确认的名称约束

以下“允许”指官方文档/schema 声明；只有明确标注的行包含既有在线实测。字母均指
ASCII，不能把“字母”理解为任意 Unicode。所有长度针对完整对外名称，包含添加的前缀。

| 供应商与具体接口 | 名称字段 | 字符/首字符约束 | 长度 | 点号 | 证据 |
| --- | --- | --- | --- | --- | --- |
| OpenAI Chat Completions | `tools[].function.name` | 字母、数字、`_`、`-`；文档未另加首字符限制 | ≤64 | 不允许 | [官方 API Reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create) |
| Azure OpenAI Chat v1 | `tools[].function.name` | 与其 `FunctionObject` 定义一致：字母、数字、`_`、`-` | ≤64 | 不允许 | [Microsoft v1 Reference](https://learn.microsoft.com/en-us/rest/api/microsoft-foundry/azureopenai/chat) |
| Anthropic Messages 客户端自定义工具 | `tools[].name` | `^[a-zA-Z0-9_-]{1,128}$` | 1～128 | 不允许 | [Define tools](https://platform.claude.com/docs/en/agents-and-tools/tool-use/define-tools) |
| DeepSeek Chat Completions | `tools[].function.name` | 字母、数字、`_`、`-`；工具名称唯一 | ≤128 | 不允许 | [Chat Completions API](https://api-docs.deepseek.com/api/create-chat-completion/) |
| DeepSeek Messages 兼容入口 | `tools[].name` | 既有错误响应要求 `^[a-zA-Z0-9_-]+$` | 本地证据未测边界 | 实测拒绝 | [DEC-043 本地证据](../decisions/DEC-043-provider-presets-and-anthropic.md)；不把 Chat 的 128 自动套用到 Messages |
| 阿里云百炼 OpenAI Chat 兼容接口 | `tools[].function.name` | 字母、数字、`_`、`-` | ≤64 | 不允许 | [百炼接口参数](https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope) |
| Z.ai Chat Completions | `tools[].function.name` | `^[a-zA-Z0-9_-]+$` | 1～64 | 不允许 | [Z.ai Reference](https://docs.z.ai/api-reference/llm/chat-completion) |
| BigModel Chat Completions | `tools[].function.name` | `FunctionObject.name` schema 同样要求字母、数字、`_`、`-` | 1～64 | 不允许 | [官方 OpenAPI 文档](https://docs.bigmodel.cn/api-reference/%E6%A8%A1%E5%9E%8B-api/%E5%AF%B9%E8%AF%9D%E8%A1%A5%E5%85%A8.md) |
| Kimi Chat Completions | `tools[].function.name` | 官方 `ToolDefinition` schema 为 `^[a-zA-Z_][a-zA-Z0-9-_]{0,127}$`；字母或 `_` 开头 | 1～128 | 不允许 | [Kimi 官方 OpenAPI 文档](https://platform.kimi.com/docs/api/chat.md) |
| Google Gemini 原生 GenerateContent | `tools[].functionDeclarations[].name` | 声明字段允许字母、数字、`_`、`:`、`.`、`-` | ≤128 | 声明允许，见下文差异 | [Gemini FunctionDeclaration](https://ai.google.dev/api/generate-content#FunctionDeclaration) |
| Google Vertex AI 原生 FunctionDeclaration | `functionDeclarations[].name` | 字母或 `_` 开头；其余字母、数字、`_`、`.`、`-` | ≤64 | 文档允许 | [Vertex AI 类型定义](https://docs.cloud.google.com/python/docs/reference/aiplatform/latest/google.cloud.aiplatform_v1.types.FunctionDeclaration) |
| Amazon Bedrock Converse / Runtime | `toolConfig.tools[].toolSpec.name` | `[a-zA-Z0-9_-]+` | 1～64 | 不允许 | [Runtime ToolSpecification](https://docs.aws.amazon.com/bedrock/latest/APIReference/API_runtime_ToolSpecification.html) |
| Amazon Bedrock Agent / Prompt ToolSpecification | `name` | `[a-zA-Z][a-zA-Z0-9_]*`；字母开头，无横线 | 1～64 | 不允许 | [Agent ToolSpecification](https://docs.aws.amazon.com/bedrock/latest/APIReference/API_agent_ToolSpecification.html) |
| HUA Chat Completions / gpt-6.1-sol | `tools[].function.name` | 未取得完整公开校验 schema；只能确认已测名称 | 未实测边界 | 实测点号 400，别名 200 | [本机 HUA 调用/回放证据](../compatibility/hua-tool-alias-20261012.md) |

Anthropic/DeepSeek 的当前官方页面写到 128，不能继续用旧的“所有供应商都是 64”
假设。Mirage 的 64 上限可以作为保守适配策略，但应明确与供应商真实上限区别。

Google 的同页文档还有值得保留的差异：`FunctionDeclaration.name` 接受 `:`、`.`，
而 `FunctionCall.name` / `FunctionResponse.name` 的描述只列字母、数字、`_`、`-`，
也为 128。因此“原生声明允许点号”有文档依据，“整个往返都保证点号可用”仍未实测。
Vertex AI 的 64 与首字符约束也不能套用到 Gemini API。推荐保守别名避开这些差异。

## 已覆盖但仍需补证的服务

没有查到明确约束不等于任意名称都允许。SDK 类型只有 `string` 也不能证明服务端
不校验；示例用 `get_weather` 只能证明示例形状，不能证明字符集与长度上限。

| 服务/接口 | 本轮能确认的内容 | 尚缺的证据 |
| --- | --- | --- |
| OpenAI Responses | 顶层函数为 `tools[].name`；当前 Reference 还含结构化 namespace，namespace 内函数名有 `minLength:1/maxLength:128`。顶层 Function 描述未给同样的完整字符规则 | 顶层字符集/点号行为的明确 schema 或实测；不能把 `text.format.name` 的 64 误当函数名限制，也不能把 namespace 子字段限制直接套给全部工具。来源：[Responses Reference](https://developers.openai.com/api/reference/resources/responses/methods/create) |
| MiniMax OpenAI / Anthropic 兼容入口 | 官方声明支持 `tools` 与 `tool_choice`，采用相应接口形状 | 页面没有独立列明名称 regex/上限；两个兼容入口分别补测，不能仅继承 Anthropic/OpenAI 的承诺。来源：[OpenAI SDK](https://platform.minimax.io/docs/api-reference/text-openai-api)、[Anthropic SDK](https://platform.minimax.io/docs/api-reference/text-anthropic-api) |
| Kimi Messages 兼容入口 | 本轮已查清 Chat 的 128/首字符规则 | Chat 规则不能替代 Messages 入口校验，需补取该入口规范/实测 |
| 百炼 Anthropic / Responses 兼容入口 | 本轮已查清 OpenAI Chat 的 64 规则；Responses 有独立工具结构 | 需按入口补证，不把 Chat 的规则当作其他入口事实。来源：[百炼 Responses](https://help.aliyun.com/zh/model-studio/qwen-api-via-openai-responses) |
| SiliconFlow Chat | 官方示例为 `tools[].function.name`，返回 `tool_calls` | 当前页面未明确列 regex/名称长度，需读取完整 schema 或在线边界验证。来源：[Chat API](https://docs.siliconflow.cn/docs/api/chat-completions-post) |
| OpenRouter Chat 聚合接口 | 官方规定每轮传 tools，并展示 assistant 回放和 `tool_call_id` 关联 | 没有在工具指南列全局名称字符/长度承诺；实际路由上游也必须满足。来源：[Tool Calling](https://openrouter.ai/docs/guides/features/tool-calling) |
| Mistral Chat / Conversations | 自定义工具由 `function.name` 声明并按名称返回 | 已查指南未给完整 regex/名称上限；Connector 名称的 64 不是函数工具名称限制。来源：[Function Calling](https://docs.mistral.ai/studio/conversations/function-calling) |
| Xiaomi MiMo | 官方平台可访问，但本轮公开页面没有可提取的完整工具字段规范 | 未确认，不以 Mirage 预设或第三方教程代替官方校验。来源：[官方平台](https://platform.xiaomimimo.com/) |

Responses Reference 在 web 阅读工具中多次读取失败，随后通过普通 HTTPS GET
读取官方页面前 8 MiB，并提取正文，确认 namespace 的子字段与顶层 Function 描述。
没有据搜索摘要把顶层规则标为已确认。BigModel/Kimi 的网页默认折叠子字段，实际读取
官方 `.md` 中嵌入的 OpenAPI schema 才确认名称约束。原始抓取留在忽略的
`build/tool-name-survey-20261012/`，不将整份第三方文档复制入仓库。

## 名称往返接口与 Mirage 的适配边界

| 方言 | 声明/强制选择 | 调用与 assistant 回放 | 工具结果关联 |
| --- | --- | --- | --- |
| Chat Completions | `tools[].function.name`；命名 `tool_choice.function.name` | `tool_calls[].function.name` | 保留 `tool_call_id`，不要把调用 ID 当工具名改写 |
| Anthropic Messages | `tools[].name`；命名 `tool_choice.name` | `tool_use.name` | 保留 `tool_use.id` / `tool_result.tool_use_id` |
| Responses | 顶层 `tools[].name`；命名工具选择；新 namespace 是独立结构 | `function_call.name` 与可能的 namespace | 保留 `call_id`；不能用拼接点号替代接口自有 namespace 结构 |
| Gemini 原生 | `functionDeclarations[].name`；`allowedFunctionNames` | `functionCall.name` | `functionResponse.name` 也需一致，有 ID 时保留匹配 ID |
| Bedrock Converse | `toolSpec.name`；`toolChoice.tool.name` | `toolUse.name` | 保留 `toolUseId` |

这些对应关系描述供应商接口，不表示 Mirage 已接入所有原生方言。pinned Mira 当前
承载 Chat Completions、Responses 和 Messages；本轮不新增 Gemini/Bedrock Mapper。

调研时 Mirage `ModelLayer::assemble()` 对全部 Chat Completions，以及官方 DeepSeek
Messages 配置启用 `tool_alias_provider`；其他 Messages 和 Responses 尚不启用。
这已经修复 HUA，但不能作为完整跨供应商策略：**官方 Anthropic 的 dot 禁止规则也
要求名称适配**，MiniMax/Kimi/百炼/MiMo 的 Messages 入口应按保守策略或明确证据
处理。Responses 的规则还需补证，不能因为没有在线失败就判定无需适配。

## 建议的后续方案与验收

以下是调研建议，不改变 DEC-043 已接受的当前适用范围，不将调研自动视为代码交付。

1. 对模型侧自定义函数统一产生保守别名，按“供应商 + 入口 + 方言版本”记录更宽规则。
   默认不要依赖点号、横线、Unicode、数字开头和超过 64 字符的名称。
2. 内部规范名、ToolId、用户记录与权限保持不变；别名表只在 Provider/Adapter 边界
   起效。覆盖声明、命名工具选择、返回解析和 assistant 回放，结果 ID 原样保留。
3. `.→_` 会使 `a.b` 与 `a_b` 相撞。当前显式拒绝可保安全；如需同时接纳，使用
   确定性短摘要后缀并检测真实碰撞。超长名缩短时也预留前缀/摘要预算，不静默截断。
4. 保持一轮对话的映射稳定，不按 tools 排序重新编号。明确指名工具的宿主说明应与
   实际对外别名一致；不全局替换用户正文、参数、结果文本或签名 thinking 内容。
5. 继续调用 pinned Mira 的 Provider/Mapper/工具解析能力，复用现有 Adapter，
   不修改第三方实现或构建第二套模型循环。若需要通用依赖新增命名契约能力，先按
   工程规范核对 API 并登记依赖反馈。

可复现验收矩阵应逐入口覆盖：普通下划线名、点号、横线、Unicode、空串、首字符
（字母/下划线/数字/横线）、长度 63/64/65/127/128/129、重复名、映射冲突、前缀
超限；同时覆盖同步/SSE、强制调用、普通返回、assistant 回放和结果关联。名称非法
应在执行动作前显式失败；未知返回名不得通过猜测或简单反替换被接受。

本轮只执行官方资料读取、源码契约对照和文档链接检查，未新增付费模型调用或使用
其他供应商凭据。在线证据仅复用既有 HUA/DeepSeek 记录。未改变运行程序、依赖 pin
或安装包；M6-41 的原任务与全量发布门禁保持原状态。

2026-10-12 后续：维护者已授权统一适配，DEC-043 更新为三方言统一保守别名，
声明、宿主说明、返回和回放共享同一规则。这里的原有覆盖缺口描述调研时快照；
实现和实际验证结果由 M6-41 与后续兼容性记录跟踪，不反写供应商资料为实测。
