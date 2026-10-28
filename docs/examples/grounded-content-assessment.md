# 以实际画面证据评估内容任务

关联：[DEC-133](../decisions/DEC-133-grounded-content-task-assessment.md)、
[M9-30](../plans/m9-layered-perception-delivery.md)。本接口由既有连续感知工具集提供，
是内容任务的显式声明/评估，不是自动播放器或独立调度器。

用户只要求描述已观察画面时，先声明：

```json
{"action":"begin","scope":"observed_samples","requires_audio":false}
```

通过desktop.vision.storyboard选择2–4个已保留frame_ids，或event_frame查看单帧。
实际发给模型的图像元数据包含content_ref（拼图每个tiles元素一项）。此时只得到
待消费引用，只有本次请求Completed、通过供给校验并记入时间证据账后，才能评估：

```json
{"action":"assess","verdict":"answerable","answer":"在已观察的片段中……","limitations":"只基于稀疏画面，没有声音证据，未观察的片段未知。","evidence_refs":["从实际图像元数据复制的64字符引用"]}
```

示例引用是占位文字，必须替换为实际content_ref。范围声明代表模型对用户问题的
解释，主机没有独立证明这一解释正确。回答≤1024字节、限制说明非空且≤512字节，
最多4个不同引用；成功只发布该评估回答，并自动加稀疏片段说明。随后自由终答不能
覆盖它。对回答本身的语义正确性仍需验证，引用有效不等于理解正确。

若用户要求完整视频或声音，begin必须忠实使用entire_video或requires_audio=true。
当前缺播放器起止/完整覆盖/声音证据，即使提交answerable，主机也记录insufficient，
丢弃拟回答并使该内容任务失败收口，不把需求降成片段描述。也可以主动报告不足：

```json
{"action":"assess","verdict":"insufficient","answer":"","limitations":"没有足够的内容画面证据。","evidence_refs":[]}
```

新实际供图会撤销旧评估，需重新判断；来源/权限/gap屏障或引用淘汰进入interrupted。
同范围、同声音要求的begin可显式重新开始，不能通过改变需求清掉限制。本turn的
声明不可撤销为普通任务，不跨turn持久化。

引用上下文丢失时可按需调用{"action":"status"}，返回当前状态及最多4项已完成
供图引用（JSON≤1KiB），不得轮询。状态摘要自动随下一次既有LLM请求提交，≤512B，
全连续观察仍≤8KiB；工具不触发截图、OCR、YOLO、输入或额外LLM请求。

尚未声明begin的任务保持原行为。因此这道门禁不能保证模型一定声明内容任务或
正确理解用户目标；真实视频、在线模型与意图一致性验收仍开放，见
[验证记录](../compatibility/grounded-content-assessment-20261028.md)。
