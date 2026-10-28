# 视频任务：事件文本与时间拼图

> 状态：In Progress（工具契约已实现；真实媒体任务验收待执行）
> 日期：2026-10-27
> 负责人：Mirage 维护者
> 工作项：M9-24；依据：[DEC-120](../decisions/DEC-120-temporal-storyboard-observation.md)

先导航到媒体，确认播放，再消费内容。持续 feed 已自动提供 OCR 文本与事件，
优先复用 semantic_scene；只有覆盖、年龄或细节不足时补 OCR/单帧。

1. 在连续 feed 的像素坐标声明 `desktop.vision.focus`。它是通知过滤，不裁剪
   视频、不改变采集、不影响其他任务；不可直接使用归一化的 media 坐标。
2. 用 `desktop.vision.wait` 等待相关进展，读取事件文本及 `preview_available`。
   超时不证明画面未变。不要把通用 wait + capture + 推理变成轮询。
3. 需要时间对照时，将 2–4 个实际存在且可用的 frame_id 一次打包，例如：

   ```json
   {"frame_ids":["<first retained frame_id>","<later retained frame_id>"]}
   ```

   调用 `desktop.vision.storyboard`。示例字符串是占位符，必须替换为本任务
   feed 提供的 ID。图片按真实观察时间排列，左到右、上到下，每格 320×320，
   保比例缩小且不放大。`tiles` 给出有效内容位置、frame_sequence、source_epoch、
   offset_ms、age_ms、since_previous_ms。时间是观察时钟，不是播放器进度。
4. 根据多帧描述发生了什么、顺序及可观察区间；小字或细动作按需读单张
   `event_frame`。拼图和事件帧都不签发可点击引用、不验证当前桌面状态。
5. 缺帧/过期返回 `available=false`，不要循环补抓来凑数；等待后续相关证据或
   如实说明当前覆盖不足。若用户要求完整观看，须取得相应内容/结束覆盖，不能
   把任意两张预览或一条 media_playback 事件当作看完整段。

拼图复用现有预览，没有 capture/OCR/detector 副作用；只有模型请求实际消费
图片后才逐帧 ACK，后续不重复供图。`sparse=true`、`complete_video=false`、
`audio=false` 始终成立；dropped/filtered/resync 不能被隐藏。没有声音或字幕
证据时不要推断旁白。图片数量降低不承诺 token 等比例降低。

当前只打包既有事件帧，不保证慢动作/字幕/全屏视频的采样完整；也没有自动完成
视频任务的状态机。实验方案与剩余能力见
[调研记录](../research/2026-10-27-layered-perception-and-video-storyboards.md)。
