# DEC-081：原生 CUA 能力与观察引用衔接

> 状态：Accepted
> 日期：2026-10-12
> 负责人：Mirage 维护者
> 工作项：M7-10 第七增量，关联 M6-38 / DEC-080

2026-10-15 局部修订：[DEC-091](DEC-091-local-semantic-perception-and-action-loop.md)
新增无需向模型供图的文本观察/目标引用路径；M9-05/06 在新契约下解耦
input_actionable 与供图。旧 image_scope 路径的身份、新鲜度和源帧水位守卫保留，
不得通过删除图片或延长旧引用冒充新能力。

最新测试证据：一次调用未暴露的 desktop.window.list 被模型桥拒绝；另一轮在交互截图
后 OCR 产生替代图，模型使用原 image_scope 点击被 interactive_image_required 拒绝。
重新捕获后同轮一次点击成功，后续 OCR 因静态共享流无新 buffer 超时，并关闭本轮共享。
拒绝的两次点击均 dispatched=false，不能描述为键鼠传输失败；成功点击的读帧条件也不
证明浏览器已导航。原始会话仅就地读取，仓库保留最小去敏故障摘要，不复制用户历史。

注册表的 exposed_tools 是本轮可调用名称唯一事实源，宿主提示显式列出闭合集合。
有 desktop.session 输入时，缺少 window API 不表示无法点击可见窗口取得焦点；提示
相应像素输入路线及证据限制。Mira 对未暴露工具仍 fail closed，不注册伪工具或重试
已产生不确定效果的动作，不修改 pinned 依赖。

OCR 刷新后返回实际新的 image_scope 与 input_actionable，观察区域仍 actionable=false。
旧图引用返回 stale_image_scope，不扩大引用期限、不将旧坐标绑定到新图；实际输入仍
使用既有局部像素重校验、权限/authority、取消与释放守卫。模式由 DEC-080 限定工具集。

静态流保留一个不超过 32 MiB 的自有最新源帧副本，PipeWire buffer 及时归还以避免
单 buffer 生产者被阻塞。处理取消、格式变化和关闭，且
区分最新可用画面与动作后新增源帧；不得用旧缓存证明动作后条件。该部分验收继续记录
实际结果，真实 GNOME 验证尚未执行前不得关闭 M7-10。

参考 [CUA 原生驱动方案调研](../research/2026-10-12-cua-native-access-and-observation.md)。

源帧提供会话内递增 source_sequence（零为未知）；CaptureLimits 水位要求新于指定
序号，调用方拒绝零/旧值。动作前记录最新源序号，动作后的首次观察必须超过此水位。
普通静态观察可返回最新副本，回执不宣称已知真实捕获时间；未到首帧不能返回缓存。
每次 native capture 的 image_scope 为独立临时引用，不能用内容摘要代替引用代数。

备选是扩大超时、始终等待新 buffer，或给动作后验证无条件复用旧帧。扩大期限无法
解决静态生产者不交新帧的问题，旧帧也无法证明动作效果，因此采用有界自有副本与
源帧水位。代价是动作无新增可观察画面时仍可能明确返回未验证，必须由目标证据或
人工确认收敛，不能以输入已发送声明业务完成。
