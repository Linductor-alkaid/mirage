# 内容任务自动供图

关联：[DEC-135](../decisions/DEC-135-content-automatic-visual-supply.md)、
[M9-30](../plans/m9-layered-perception-delivery.md)。content工具版本1.1.0。

内容观看任务只需声明一次自动模式：

```json
{"action":"begin","scope":"observed_samples","requires_audio":false,"auto_storyboard":true}
```

已有下一次模型请求会按需附带现有缓存的低清拼图，无需先调用capture、event_frame
或storyboard。不是每帧发图，也不会主动新建模型请求：后台采样/CV继续原频率，
OCR/YOLO仍由原识别层触发。模型没有请求时，不为供图唤醒它。

每次自动构建尝试至少间隔5秒，Completed后亦至少间隔5秒；窗口30秒、最多4张
未消费帧，复用8预览/256事件、320格/640²预算。指定窗口不等于完整覆盖。
已有未消费的显式图片优先并保持原工件重试，不重复编码；预算低或文本精读时
不构建自动图片。无2张有效新帧时使用[内容就绪等待](content-ready-wait.md)的
`{"until":"content_ready","timeout_ms":30000}`，在本地合并媒体唤醒，不轮询截图。

`content_visual_supply`随既有连续观察说明offered/image_pending/cooldown、
context_budget_low、候选不足、失败或assessed_or_interrupted；冷却时提供
`retry_after_ms`，打包错误提供原Mira错误枚举数值`failure_code`。诊断≤256B，
完整观察≤8KiB，不建立独立任务监控系统。关键事件仍应及时处理，不能为供图
冷却而吞掉事件或增加静默等待。

图片元数据含`automatic_content_supply=true`、`input_references_retired=true`。
自动供图会退役先前图像和输入临时引用；它是历史内容证据，不能点按。后续确需
交互时才通过原新鲜capture/动作路径取得输入来源；小字/局部细节不足仍可显式ROI。
实际Completed及来源/请求校验之后，才可用格位content_ref进行assess。

评估成功或不足后不再自动供图；来源、权限/gap中断停止，需同配置begin显式重开。
同配置begin幂等，不能靠重复begin清掉5秒门禁；本turn的范围/声音/自动模式不可
隐式切换。省略auto_storyboard或false保留原手动流程。模型提示推荐内容观看
启用true，但不会按自然语言关键词强制猜用户目标；也不跨turn持久化。

受控流程中，手动begin→storyboard→assess→终答为4次模型请求、1次取图工具；
自动begin→assess→终答为3次、0次取图工具，实际图片均1张/2格、本地额外capture
均0次。这只是协议对比，真实LLM调用/图像token/成本与质量收益仍需在线验收。
完整视频与声音不足门禁保持；[验收](../compatibility/automatic-content-visual-supply-20261029.md)
记录范围与配置结果。
