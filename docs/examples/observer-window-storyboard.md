# 按观察时间窗读取稀疏拼图

关联：[DEC-134](../decisions/DEC-134-observer-window-storyboard-selection.md)、
[M9-30](../plans/m9-layered-perception-delivery.md)。工具为desktop.vision.storyboard 1.1.0。

模型可以一次请求最近30秒的已保留、未消费事件帧，无需逐个枚举frame_id：

```json
{"window_ms":30000,"max_frames":4}
```

window_ms为100–120000整数；max_frames为2–4整数，省略时默认4。它与原
{"frame_ids":["实际帧ID1","实际帧ID2"]}互斥，不能混合参数。显式ID模式行为保持。
窗口相对观察者的单调时钟，绝不是播放器播放位置或完整观察时长。

最多检查现有8帧缓存与256事件。有效候选必须有同来源、同原观察时刻的事件、
非零帧号与活预览；过期、未来、窗口外和已完成消费样本不参与。选最早/最新，再
选离已选时间最远的候选，同分取较早，输出按时间排序。非均匀时间分布也可
保持观察跨度；它不分析画面内容，不保证保留语义重要片段或小字幕。

可用时仍是一张最多640×640 PNG、2–4格，每格最长边≤320，复用Mirador缩放。
metadata补selection=observer_window_maximin、window_ms、eligible_frames和
selected_frames，保留原时刻、格位和content_ref。没有新采集、OCR、YOLO、
音频或固定模型调用。图片进入既有下一请求，实际Completed后才计入时间账。

失败/Incomplete/预算扣留不算消费，可重试同一窗口。已经完成供图，即使预览
ACK失败仍不会在该来源段的当前32样本账内被重复选择。无新候选返回
no_new_temporal_frames；只剩1张返回need_two_new_temporal_frames，不用旧图凑数。
应等待新事件，必要时显式event_frame读单帧，不能轮询该窗口代替事件等待。

来源或权限退役使旧offer失效；容量、几何或时序歧义明确返回不足，预览淘汰不
补帧。8帧/120秒只是缓存与有效性上限：指定30秒不代表缓存保留了30秒内容。
源/gap屏障清账后可以重新观察当前有效帧，不能把它解释为跨段连续覆盖。

长期任务的32样本账已满时，时间早于其最早保留样本的预览也不进入窗口选帧，
即使无法再证明它是否曾消费。这与账本拒绝录入过早样本的边界一致，防止ACK
失败残留的老图在账本滚动后被重新当成新图；代价是可能跳过未读的过早预览。
显式frame_ids模式仍可按需查看这些有效预览，不把历史未知当全局已读计数。

内容问题继续使用[显式评估](grounded-content-assessment.md)声明范围与声音要求，
凭实际供图引用形成有界回答。时间分散、Completed供图和引用校验均不证明理解
正确或看完整视频；[验收](../compatibility/observer-window-storyboard-20261028.md)
保留真实视频/在线模型与播放器门禁。


内容观看也可[声明自动供图](automatic-content-visual-supply.md)，在已有模型请求
中按需使用该窗口策略，省去逐批调用本工具；仍不创建后台模型请求。
