# DEC-087：与 ZCode 对齐的每调用输出预算与上下文 preflight

> 状态：Accepted
> 日期：2026-10-14
> 负责人：Mirage 维护者
> 扩展：DEC-082/085
> 参照：ZCode `apps/zcode-cli/packages/core/src/runtime/methods/model-token-limits.ts`、
> `turn-output-token-continuation.ts`、`turn-model-step.ts`、`compact.ts`（usage 锚点）

## 选型

对齐 [ZCode](../../../ZCode/README.md) 的五层预算设计在 Mirage 架构内的可承载部分：

1. **声明值优先**：`ModelLayerConfig.max_output_tokens` 语义改为"模型声明的每调用
   输出上限"——声明值（1..65536）原样使用、绝不裁剪；`0`（缺省）= 未声明，取
   32000 基线（ZCode 缺省同值）。
2. **思考安全地板保留**：思考档调用 `max(基线, 32768)`。与 ZCode 的有意差异：Mirage
   无上下文压缩子系统，声明过小的思考模型会陷入"续写-再截断"螺旋而无法压缩自救，
   地板是该缺口的兜底。
3. **每请求 preflight 收缩**（替代原静态 `window/2` 钳制）：声明了上下文窗口时，
   wire 预算 = `min(基线, 窗口 − 已用量估算 − 1000)`；余量非正时保留基线（本地估算
   非 provider 权威）；窗口未声明不收缩。落点在 `ObservedProvider::infer`——对话与
   harness 两条路径的共同必经点，只缩不涨，路由准入（profile 限额）对两个值都保持
   真实。
4. **用量锚点**：上次成功响应的 provider 报告 `input_tokens`（pinned 方言已合成为
   含 cache 读写的 context 总量）+ 自该响应以来的增量估算；增量用 pinned
   `mira::estimate_input_tokens`（bytes/3、1600/图、64/其他）双侧同尺，系统偏差在
   差值中抵消。首请求或无锚点时用纯本地估算。锚点按会话存于装饰器内有界表
   （512 条，RULE-07）。
5. **续写深度 3**：`max_output_continuations` 2 → 3（ZCode 对齐），整轮资金化乘数
   相应 21x → 22x（16 轮 + 2 恢复 + 3 续写 + 1）。

思考强度与输出预算保持分离轴：wire 只携带模式标志与（可选）`output_config.effort`，
从不携带 token 预算（DEC-082）；per-model 声明经 profile capabilities 进入 pinned
方言编码器，与 ZCode 的声明式 option map 在架构上同构。

## 边界

- ZCode 的三级 compact（micro/auto/reactive）不对齐：独立子系统，超窗输入在
  Mirage 仍显式失败。preflight 余量非正时保留基线的语义因此必要。
- 每模型 `max_output_tokens` 声明的产品入口（IPC/UI 编辑器字段）为后续增量；配置
  结构（`ModelLayerConfig`）已承载该语义。
- 截断识别沿用 pinned `IncompleteReason::MaxOutputTokens`（PR#93），未复制 ZCode 的
  raw finish-reason 集合；pinned 解析已覆盖 length/max_tokens 语义。

## 验证

锚定更新：envelope 720896、续写场景 (c)/(d) 重写为 3 深度、preflight 交互值由独立
验证代理实测锚定。见
[验收记录](../compatibility/zcode-budget-alignment-20261014.md)。
