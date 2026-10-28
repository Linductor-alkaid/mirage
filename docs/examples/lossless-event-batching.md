# 连续事件的批次供给

关联[DEC-130](../decisions/DEC-130-lossless-event-batching.md)。这是下一次模型请求
自动携带的结构化观察，不需要LLM主动调用OCR或轮询截图。原始事件日志逐条保留。

以下为两次同身份位置/外观观察的示意（只展示相关字段）：

```json
{
  "cursor": 42,
  "kind": "position_or_appearance_changed",
  "text": "播放",
  "confirmed": true,
  "entity": 7,
  "instance_generation": 2,
  "source_epoch": 3,
  "frame_sequence": 112,
  "frame_id": "preview-112",
  "preview_available": true,
  "age_ms": 100,
  "x": 90, "y": 20, "width": 30, "height": 20,
  "sample_count": 2,
  "prior_samples": [{
    "cursor": 41,
    "frame_sequence": 111,
    "frame_id": "preview-111",
    "preview_available": true,
    "age_ms": 300,
    "x": 80, "y": 20, "width": 30, "height": 20
  }]
}
```

先读prior_samples，再读行本身的最后一次观察；公共类型、文本、确认状态和身份
适用于每次样本。这两次观察可能有外观变化，不能只读最后一个框就声称中间无变化。
单条事件保留原平面结构；多条才添加sample_count、prior_samples和帧序号。

仅同来源/实例/文本且确认的连续位置/外观变化可分组，最多8次、首尾≤1秒；
出现/消失、文本/关系变化、未确认观察、来源屏障和媒体关键帧独立，resync禁用分组。
每请求最多16输出行、事件数组≤7000B、整个观察供给≤8192B。预算紧张时只提交
连续前缀；请求完成且确实包含该供给后才ACK，失败/未呈现部分留待后续请求。
“16”现在是行数上限，不是原始观察条数；并不保证每次达到128次，仍受时间、
身份、关键事件和字节预算约束。

帧ID只在原预览仍存活时可用；无效ID为空且preview_available=false。
8帧历史容量及既有2–4帧/320像素单格的storyboard预算不变，批次不会额外留图。
若需要判断变化意义，模型应使用仍可用的证据或重新观察；日志框不替代视频内容理解。
验收和范围见[本轮记录](../compatibility/lossless-event-batching-20261028.md)。
