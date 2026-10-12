# DEC-076：原生 OCR 有界结果与取图诊断

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-10 / M7-14，承接 DEC-075

新用户会话已验证默认 ncnn 识别 106 区域及同源图像，说明 DEC-075 接线生效。
另一张图超过 16 KiB 输出预算；最新任务在后续 ScreenCast 取图时超时，日志不能
区分 Remote 获取与 PipeWire 状态/格式/首缓冲阶段。旧应用引用拒绝后刷新列表可恢复，
网址 404 属于模型导航问题，不能把进程 verified 当页面成功。

1. 原生 OCR 的 16 KiB 总预算保持。已通过文本/几何/128 区域门禁的结果按原有顺序
   装入完整区域，超额返回有界前缀，明确 truncated、total_region_count、region_count。
   原生检测器候选预算固定为 512（X11 保持 128），五秒 deadline 不变；
   已识别结果验证后仅映射前 128 区域，再应用 JSON 预算。超过 512 仍失败。
   不截断单段 UTF-8、不伪造全屏覆盖，不把不合法或超单段预算的输出改为成功。
   模型不得凭截断结果证明某文字不存在；有图片时可读取同源图片。
2. 仅补充 Portal Platform Backend 的有限阶段诊断：Remote 或 stream、PipeWire
   状态、格式是否就绪、缓冲是否就绪。不记录屏幕内容、句柄、用户 URI 或第三方
   原始错误。保持五秒期限、失败关闭、同任务不重新授权；未定位之前不盲目加超时
   或重试，也不声明已解决实机超时。
3. 加强既有 ModelLoop 能力说明：动作后重新 application.list，URL 派发只证明
   进程局部结果；404 后不能反复提交同一失败网址。业务完成仍需页面/任务证据。
4. 复用现有 Runtime owner、Mira loop、Mirador OCR 与 Executor worker，不新增
   并发路径、不修改 pinned 依赖。自动用例验证有界前缀与真实 canonical 成功、
   无引用/文本泄露及原拒绝门禁；实机超时需要维护者在共享环境补验。

## 实机准入增补

首轮探针取三帧成功，OCR 在 detector 阶段超过 128 候选而返回 kBudgetExceeded，
并非 JSON 包装拒绝。原生候选 cap 用公开 PpOcrDetOptions 固定为 512，仅 native
backend；X11 保留 128。允许额外识别工作的代价是可能触发既有五秒期限，仍必须
显式超时而不是保证任意密集画面成功。DTO 记录分析候选总数，完整验证后最多映射
128 区域，最后装入 16 KiB 前缀；超过候选 cap 的画面仍失败。

执行证据见[本轮验收](../compatibility/native-ocr-budget-and-capture-diagnostics-20261011.md)，
原共享超时与在线业务仍由 M7 计划跟踪。
