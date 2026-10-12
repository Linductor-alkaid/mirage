# DEC-055：受控桌面图片与逐轮输入

> 状态：Accepted（维护者授权推进 M7-04/05）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-04/05](../plans/m7-visual-tools-and-input.md)
> 扩展：DEC-052/053/054；使用上游 Mira DEC-053 的公开入口

## 边界与选型

Mirador 没有公开 PNG/JPEG 编码接口；Mira 提供 artifact store、读取接口、三方言图片
映射与逐请求 supplier，但不负责桌面采集或编码。Mirage 在 integration Adapter 内
完成 PNG 包装、来源、保留与权限策略，不新增依赖，不复制感知管线或模型循环。
PNG 使用 RGB8、None filter 和 stored DEFLATE；去掉 BGRA 的未定义 alpha，保留 RGB
像素及尺寸，不缩放、不压缩优化。JPEG、OCR、SoM、图片引用点击另归 M7-06/07。

## 契约

`ModelLayerConfig` / 已有模型设置增加 `supports_images`，默认 false、严格布尔解码；
只有用户明确声明识图的模型注册 `desktop.screen.list_displays` 和
`desktop.screen.capture`。显示器枚举使用 Provider 的默认 16 项预算，给模型提供真实
来源 ID 和全局像素边界；不采集像素。原生设置页提供逐模型
“识图”开关，保存事务、模型切换与新模型默认关闭沿用既有流程。该配置不授予截图
权限；每次先判断 `screen.capture`，拒绝发生在像素采集之前。只读会话仍可在授权后
截图，完整输入和权限规则保持 DEC-054。

工具参数：`source` 为 display/window/roi，`source_id` 为对应来源 ID；roi 使用 display
ID 并必须给出全局像素 `x/y/width/height`、位于该显示器内。其他来源禁止 ROI 参数。
来源几何在采集前后核对；帧尺寸、stride、像素缓冲、取消/时限和桌面 epoch 不一致
均拒绝发布。工具回执只含 artifact ID/digest/MIME/敏感级别/大小、尺寸、原点、1:1
变换、task/environment epoch、捕获时间与最大年龄；无 base64、像素、窗口标题或密钥。
Screenshot 是指定时刻的屏幕证据，窗口截图包含遮挡；不能证明业务操作完成。

每轮最多 4 次已授权尝试；raw 与 encoded 各不超过 8 MiB，单次最多 5 秒。4K 整图
超预算明确拒绝，不静默缩放。仅保留最新一张；更新、年龄达到 5 秒、epoch 变化或
轮次结束时淘汰。过期/epoch 变化以规范文本说明需要重拍，不重放旧 ImagePart。

Mira MemoryArtifactStore 按内容去重，erase 保留墓碑。store 因此由单轮 owner 持有，
最多 4 个内容记录；替换先删旧内容再提交，重复像素可重新发布。IArtifactSource 通过
kairo LatestMailbox 接收当前 store，读取拒绝对外部引用开放：MIME、Sensitivity、
长度和 digest 均核验。发布失败、读取失败显式返回错误；Mailbox stats 为状态事实源。
不存在独立 worker、队列、调度器或 Executor owner。读取与推理由既有 Mira Gateway
管理，ModelLayer 关闭先排空推理，再关闭 transport、gateway/provider/source。

supplier 核对 task/session/task_epoch 并沿用 host CancelToken/deadline。图片和来源
文本进入主 User envelope，标为 Sensitive，不作为 System/Developer 指令；ImageDetail
统一 Auto。重试重复供给当前图，不在第一次 callback 清空，不累积到工具历史。
推理前 Guard 核对任务、epoch、年龄和实际请求中的图片；必需图片被丢弃、外来/旧引用
或 supplier 身份错误时禁止纯文本推理继续。累计 drop/degrade 令宿主 completion 失败。
正常 Answered 只表示模型回答，业务验收继续由 M7-08 承担。
Provider/loop 异常转为安全失败，过程状态仍收敛；关闭后的新工作由 Executor 准入
拒绝，不能以绕开提交直接调用同步入口来检验任务准入。

## 验证与影响

新增图像供给 fixture 覆盖 source/ROI/负原点、标准 PNG 解码、三方言 wire 映射、规范
工具往返、权限前置、取消、迟到/几何/epoch 变化、异常、同图替换、预算、退役和
身份/缺图拒绝；沿用 Debug/Release/sanitizer 与原生设置 renderer 回归。真实模型用
两张公共合成截图验证，不上传用户桌面。执行结果与限制见
[兼容性记录](../compatibility/desktop-image-supply-20261009.md)。
`MIRA-20261009-001` 仅在实际下游与在线图片验证通过后收口；失败记录保留。

## 2026-10-11 部分更新

[DEC-068](DEC-068-bundled-vision-and-recoverable-tool-rejection.md) 在 Linux 产品首范围
更新本决定的以下部分：窗口/显示截图改为 32 MiB 原始采集及最长边 1600 的只读预览，ROI 保持 1:1；预检拒绝不消耗四次采集额度。 其余契约继续有效。
