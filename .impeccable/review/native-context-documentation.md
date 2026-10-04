# 原生上下文占用文档同步（M6-06）

> 日期：2026-10-04
> 负责人：Mirage 维护者；执行：Impeccable documenter
> 状态：本次文档同步已完成；整体交付状态由计划与验收记录判定。
> 决策：[DEC-036](../../docs/decisions/DEC-036-context-usage-presentation.md)
> 方向：[原生 Agent 对话页](../../docs/design/native-agent-frontend.md)

本轮按已授权的窄扩展合并到既有原生系统，保留用户指定的 code-led ZCode 中性明暗世界。
修改范围为 `apps/native/DESIGN.md`、`apps/native/.impeccable/design.json`、`PRODUCT.md`
与本记录；未修改代码、根目录 DESIGN.md 或根目录 .impeccable/design.json，也未重建既有
字体层级或 legacy Web 系统。

## 已同步事实

- 用量只取 Mira ModelResponse.usage 的 Exact / ProviderReported input_tokens；展示最近
  成功请求，工具循环使用最终回复调用输入，不累计各调用或计入输出，不从草稿/bytes估算。
- 分母为显式配置的窗口预算；留空、零或缺失为未知。非零整数为2048–2000000，保存后
  映射Mira ProfileLimits。未配置保持既有Mira默认运行预算，UI分母仍未知，不声称供应商
  自动发现。模型页现在有五个单行字段，新增预算字段沿用16px标签/输入、44px输入高。
- 失败/取消保留前一次成功值；新成功缺用量重置未知；较旧序列不能覆盖较新值。旧历史
  不持久化用量，服务重启后显示未知。真实零且预算已知显示0%；原始数值/百分比允许超额，
  仅圆环与进度条图形钳制100%。
- 入口36×32px，圆环显示20×20px，SVG视框24×24、r10/stroke4；路径弧避免dash-array
  依赖。明暗环色分别为#737373/#b1b1b1，底环25%透明度、占用弧70%，未知只显示灰底环。
- 详情320×224px（宽受column约束），右对齐composer，上方8px、顶部至少68px；标题和
  百分比16px，Token摘要14px，来源/模型/本次引用13px，进度条高6px、圆角3px。
  浅色surface、深色dark-composer、12px圆角与细描边沿用既有系统。
- sidecar补充尺寸、数据生命周期、预算字段和验收来源；composer示意改为真实未知空环，
  新增自包含静态上下文详情示意。原生数据与交互仍由IPC/EUI管理；示意不伪装为运行实现。

## 核对来源与结果

核对 `apps/native/app.cpp`、`context_usage.hpp`、`chat_model.hpp/.cpp`，并核对
`integration/mira/src/model_layer.cpp` 的质量筛选与ProfileLimits映射。设计/产品陈述与
DEC-036、方向契约及[验收记录](../../docs/compatibility/native-context-usage-20261004.md)一致。

逐一查看五张必须截图：`native-context-unknown-light.png`、`native-context-usage-light.png`、
`native-context-usage-dark.png`、`native-context-usage-min-light.png`、`native-context-usage-min-dark.png`。
正常逻辑1180×800与最小860×620的明暗状态保持环、发送按钮与完整详情可见；未知态为
“未知 / Token 用量尚不可用”，没有虚假0%。

[独立finish review](native-context-finish-verdict.md)的窄扩展结论为ship，其当时记录的文档
前置项由本轮同步关闭；不扩展其视觉验收范围。另查看`native-context-model-window-light.png`、
`native-context-model-window-invalid.png`与`native-context-final-light.png`：新增字段沿用既有
表单，abc触发可读整数范围提示；本轮恢复128000、保存后经model.get确认，再回到
详情。静态截图与review不单独证明键盘或live motion。

[真实请求事实](native-context-live-usage.json)为SiliconFlow Qwen/Qwen3.5-4B，input_tokens=391、
window_tokens=128000；0.3%与实现的四舍五入显示一致。128000明确为配置测试预算，不是
供应商容量发现。文档不包含凭据。

文档校验：sidecar JSON解析、DESIGN frontmatter的公开token引用/组件属性与八节顺序、
narrative和正文同步、证据路径存在性及陈旧Token/210px声明清理通过。本轮是文档变更，
未另跑C++测试；技术验证以验收记录为准。

## 保留边界

Windows及真实IME没有新增验收，负责人Mirage维护者；补跑条件为目标Windows EUI dev构建
和GUI真机环境。运行中ZCode像素对照仍受截图权限限制，补跑条件为取得该参考窗口截图权限。
这些限制不写成系统禁止规则，也不扩展Linux截图结论。原生未知态与“上次请求”语义是真实
行为，并非缺陷；无新增窄扩展缺陷被固化为设计规则。既有EUI/字体及legacy差异超出本轮，
维持原记录。
