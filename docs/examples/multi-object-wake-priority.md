# 多目标注意与唤醒优先级

> 依据：[DEC-131](../decisions/DEC-131-multi-object-wake-priority.md)
> 工作项：M9-29/18；日期：2026-10-28；负责人：Mirage维护者

对已完成模型请求实际供给的semantic_scene对象，可以一次设置多个目标：

```json
{
  "targets": [
    {"object_ref":"<下载进度对象引用>", "priority":"wake"},
    {"object_ref":"<可延后查看的辅助动画对象引用>", "priority":"observe"}
  ]
}
```

这是desktop.vision.focus的第三种参数形式，与单个object_ref、矩形四字段互斥。
1–4个不同对象、每个priority必填，且至少一个wake；引用须来自本任务已完成请求。
全部有效才原子替换策略，任一无效或feed未支持返回applied=false，保留旧策略。
公开DesktopPerceptionFeed新增focus_objects，默认拒绝；自定义feed需重建并实现。

objects模式与旧矩形/单对象模式不同：保留全部授权事件，仅改变wait是否立即返回
“event”。只有来源/ID/实例准确匹配observe的已确认位置/外观变化，且不与wake
区域相交，才延后唤醒；非目标、未知/不完整身份、内容/关系变化、出现/消失、
媒体帧与来源屏障仍会唤醒。observe不能表达“中间变化没有语义”，只表达“可等到
下次请求再看”。超时返回也可能包含延后事件，不代表画面未变。

继续调用desktop.vision.wait等待进展即可。低优先级事件会唤醒原始本地订阅，
任务等待继续至需要唤醒的事件、期限、取消或失效；无需轮询capture或手动OCR。
下一次请求自动携带仍在窗口内的全部事件，包括延后事件；按DEC-130批次和
既有16行/8KiB预算装配，原时间顺序和cursor/ACK保持。关键事件触发唤醒并不
保证它在同次字节预算内完整呈现，必须检查截断/gap和未提交部分。

自动供给attention.mode为objects，targets给出每个entity、instance_generation、
content_revision、priority与当前框；这些只是任务意图，不产生动作权限。
同版本移动分别跟随；任一目标未知/缺失/过期/版本改变或来源异常，整组单向失效、
恢复全局并返回attention_invalidated。需要新证据才能重设。清除仍用：

```json
{"x":0,"y":0,"width":0,"height":0}
```

layered的CV-only未知区间会使版本注意失效，未交付跨未知帧的精确语义继承；
持续空间关注可用[矩形模式](versioned-task-attention.md)。本增量不自动推断用户
任务重要性，不更改采样/OCR/YOLO、其他任务或权限。运行证据见
[专项验收](../compatibility/multi-object-wake-priority-20261028.md)。
