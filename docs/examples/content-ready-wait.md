# 等待内容可供图

关联：[DEC-136](../decisions/DEC-136-content-ready-wait.md)、
[自动供图](automatic-content-visual-supply.md)。wait工具版本1.1.0。

先用content的begin声明自动内容任务；当已有证据不足、还需要新内容时调用：

```json
{"until":"content_ready","timeout_ms":30000}
```

`after_cursor`可选，缺省为当前游标。它在同一次既有工具调用内等待5秒供图门禁
到期及至少2张有效的新缓存帧；只有明确媒体关键帧的唤醒会在本地继续等。不会
采图、OCR/YOLO、编码或新建模型请求，原事件不ACK/删除。下一次既有supplier
才尝试附带低清拼图，仍受原图像支持、文本步骤和预算准入约束；实际Completed后
才计入已看证据。

| status | 含义与处理 |
| --- | --- |
| content_ready | 当前快照有可选新帧且门禁到期；让既有请求供图，不必再调截图工具 |
| event | 有可见非媒体事件，先处理；原区域/对象注意策略仍生效 |
| timeout | 总等待期限结束，不能据此声称画面未变或视频看完 |
| content_inactive | 未声明自动模式、已评估或证据中断；检查原内容状态，勿轮询截图 |
| gap / attention_invalidated / source_unavailable | 来源或焦点失效，先处理恢复和证据范围 |
| cancelled | 任务、deadline或权限取消，沿原任务路径收敛 |
| source_capacity_exceeded | feed超过原8预览/256事件契约，诊断来源，勿增加无界缓存 |
| wait_no_progress / wake_budget_exhausted | feed过早/无进展返回或达到64次内部等待，显式诊断；勿忙重试 |

底层其他明确状态（如unavailable）原样返回。feed的既有注意过滤不被绕过；已被
显式wake策略延后的事件会在下一次feed唤醒或deadline后检查。内容模式不会额外
将可见非媒体事件猜成不重要。关键事件可先于图片就绪返回，保留处理机会。

省略until或传event保留原等待；auto_storyboard省略/false不会启用内容合并。
真实来源的`PerceptionEvent.temporal_keyframe`默认false，仅两个媒体关键帧生产
分支设true，事件JSON仅在true时输出该字段。文字`keyframe`不具有同样效力。
自定义feed应保留事实位，未设置时保守按普通事件唤醒。公开结构新增字段，消费者
需随本版本重编译；它不是语义重要性或完整视频覆盖的证明。

[验收记录](../compatibility/content-ready-wait-20261029.md)给出可控时间线和配置结果。
