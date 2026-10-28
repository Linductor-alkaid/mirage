# DEC-132：实际模型供图的有界时间证据账

> 状态：Accepted（维护者继续推进M9授权，M9-30首增量）
> 日期：2026-10-28；负责人：Mirage维护者
> 关联：DEC-120/121；M9-24/30

## 决策与范围

在既有串行DesktopImageTurn中增加只存元数据的时间账，记录实际进入已完成模型
请求的retained event_frame/storyboard样本。工具返回、事件文字、媒体active、
截图文件已生成、请求失败/未完成/图片被上下文预算移除均不计入。只证明图片
已呈现给该次模型请求，不证明模型正确理解内容，更不证明完整观看。

账本为当前来源段保留最多32个不同历史帧的ID/帧序号/单调观察时刻，无像素，
元数据≤8KiB；当前待消费offer最多4帧。32条后保留时间最新的窗口，明确truncated，
不把累计unique数伪装为全任务帧数。按观察时间排序，给出保留帧数、首末年龄、
observer_span和相邻样本最大间隔；间隔之外的开始/结束、播放器时间、暂停/跳播
和声音均未知，complete_video=false、content_understanding=not_verified。
普通当前capture/OCR不纳入本首增量，输出scope明确为retained_event_frames_only。

模型下一请求的既有连续观察自动供给retained_visual_evidence，摘要≤1KiB，仍受
整个8KiB观察供给限制，不新增工具调用或后台模型推理。证据子状态为unobserved、
sparse_observation、interrupted，只是M9-30内容任务状态机的事实基础，不冒充
playback_confirmed/coverage_assessed/answered。完整任务状态、播放器绑定和真实
内容问题验收继续开放。

## 来源、消费与生命周期

每次发图前从同一view校验当前epoch、事件与预览原时刻和非零帧号，绑定本地
来源段代次。事件帧补齐与拼图相同的来源/未来时刻校验，不读取跨源/无事件预览。
同一帧重复呈现不增加数量，乱序读帧按真实时刻插入。源lease替换、epoch改变、
drop/resync新屏障、失败/停止/退化使旧段退役、清除当前样本，保留中断原因。
旧offer迟到不可进入新段；恢复后只有新来源段的实际完成供图才能重新计数。

只在既有Completed请求回调中，匹配通过validate_request的请求ID与实际
image artifact完整身份后记账/ACK；内容寻址的相同像素也不能混淆新旧帧时刻。取消、
权限代次改变、跨任务、缺图/伪造图片身份和失效offer不计数。offer随image clear
退役，已消费账不随下一次图片替换丢失。绑定来源替换集中到单一私有入口；无持久化、
任务间共享或跨turn累积，不引入新并发/调度/通信设施，沿用Mira请求生命周期及
kairo管理的现有任务。关停时随task owner释放，不延长预览像素生命周期。

## 能力核对与后果

Mirador的coverage证明当前识别/区域有效性，不证明某张图进入Mira完成请求或
视频观看时间；Mira现有ObservedProvider Completed回调已提供消费边界。因此
这是Mirage Adapter证据编排，不重复实现视觉覆盖算法，不修改固定依赖。
不采用“页面打开/两帧/经过十秒即看完”或由观察时钟推算播放进度的方案。

## 验证

纯契约覆盖去重、乱序、32条窗口/预算、源与gap屏障、迟到/非法/过期offer、
当前时间推进不扩大已观察span；真实工具/请求覆盖文字和失败不计数、实际图计数、
拼图一次多样本、图片替换不丢账、身份/权限/取消、8KiB与零额外capture/识别。
Debug全量、ASAN/UBSAN/TSAN/native专项及格式/公开边界通过后提交；在线模型、
真实视频内容与目标平台未执行项在专项记录中保留负责人和补跑条件。

示例见[视频观察](../examples/temporal-video-observation.md)，结果及范围见
[专项验收](../compatibility/delivered-visual-evidence-20261028.md)。
