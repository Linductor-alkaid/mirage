# 对象版本注意区域

> 状态：M9-29首增量；契约见[DEC-129](../decisions/DEC-129-versioned-task-attention.md)
> 日期：2026-10-28；负责人：Mirage维护者

下一次模型请求自动供给的semantic_scene包含object_ref。请求正常完成后，可以
使用实际供给的引用设置任务注意区域：

```json
{"object_ref":"<completed-request semantic_scene object_ref>"}
```

公开DesktopPerceptionFeed新增默认拒绝的focus_object接口；已有自定义Feed仍可
使用坐标形式，重建并实现接口后才支持对象形式。

以上是desktop.vision.focus参数，不与x/y/width/height混合。它关注一个来源/
上下文/实例/内容版本已确认的对象；同一版本移动时，消费者沿新识别框更新过滤区域。
任意字符串、外任务引用、未完成请求的引用、已变内容或旧实例均不被接受。它不产生
可点击引用或完成证明，不触发截图、OCR/YOLO或额外模型调用。

持续观看固定空间，例如视频播放器或进度区域，在同一来源上用原矩形形式：

```json
{"x":100,"y":100,"width":800,"height":450}
```

坐标为feed的shared_source_pixels。所有分量与右/下边界须在0..1000000内；宽高为0
时清除（保留旧接口的任一零尺寸清除语义），四个字段仍须齐全。矩形关注的是空间，
不是其中内容已被识别；不会裁剪共享采样或影响其他任务。来源变更/gap/失败时它也
会失效，防止把旧显示布局继续当新布局过滤。

模型供给新增attention：mode为global/region/object（DEC-131增加objects），active、reason、目标版本和
region字段对应当前消费者。对象未知/失踪、内容/实例/上下文改变、过期或来源异常时，
active=false且reason非空，过滤恢复为全局；等待中的desktop.vision.wait返回
attention_invalidated。根据新证据重新声明意图或明确清除：

```json
{"x":0,"y":0,"width":0,"height":0}
```

出现/消失/来源重置/未确认区域观察仍绕过焦点过滤。失效不会清空历史事件、推进
ACK或主动发起模型请求。现有layered CV-only区间不签发内容有效性，对象注意可能
很快失效；此时应使用与任务意图相符的空间关注，不能循环重设旧对象引用来冒充
跨未知帧继承。DEC-130已接通普通事件无损批次；DEC-131新增[多对象显式唤醒优先级](multi-object-wake-priority.md)。
自动任务重要性、精确跨未知帧继承和M9-30内容覆盖继续开放。
