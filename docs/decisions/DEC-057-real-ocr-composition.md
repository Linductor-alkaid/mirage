# DEC-057：真实 PP-OCR 资源与产品装配

> 状态：Accepted（维护者授权继续推进 M7-06）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-06](../plans/m7-visual-tools-and-input.md)

## 承载与资源

复用 pinned Mirador 的 PpOcrBackend 检测＋识别组合、PerceptionSession、变化检测、
结果缓存与融合，沿用 Mirage VisualSessionHost 的 Executor blocking worker。
不自建推理循环、OCR 后处理或第二个 Executor。新增 MIRAGE_WITH_PPOCR（默认 OFF）
只启用 Mirador 已锁定的 ncnn e54f7b1；默认构建不获取 runtime 或模型。

产品通过 --ocr-manifest 显式提供本地资源清单，schema=1，列出 model_id、检测/识别
param/bin、UTF-8 字典的路径、SHA-256 与精确字节数，及输入尺寸/显示器。
启动前有界读取并核对资源，私有临时副本供后端加载，结束清理；不联网获取权重。
model_revision 从已验证的资源和配置推导，不允许用户填任意旧 revision 来复用缓存。
所有错误可见；关闭选项、坏清单/权重、缺 Screen Provider 时不暴露真实 OCR。

## 产品边界与生命周期

Runtime Service 是唯一 Executor owner，初始化后装配后端与视觉 session。
会话按可用性暴露只读 desktop.vision.ocr，每次像素采集前通过 screen.capture 权限；
结果为非可信 OCR 文本、来源、全局坐标与快照身份，不等同可点击目标或业务成功。
普通工具过程不持久化 OCR 全文，只记录数量和来源；规范模型结果保持文本。
既有 desktop.observe 的视觉请求刷新同一管线，经相同权限。M7-07 再接 SoM/点击。

关闭先拒绝生产者、取消模型/任务，再停止视觉 worker、结算 future，最后外部 owner
shutdown(true)，后端与资源副本最后释放。使用 1 个配置显示器/1 个 session，有限
图片/缓存/结果预算；取消与超时协作式，ncnn 单次 forward 不支持中途强制打断。
产品使用公开 `OcrRequest.cache_policy=kRefresh`，每次新采集均重新推理。
现有结果缓存使用 64-bit dHash，均匀亮度变化会得到相同指纹；这适合近似感知，
不能当作文字相同的证明。本轮不自建缓存；精确复用仍是后续门禁，计数验证相同图
两次调用 backend 两次，变化图第三次。重启/资源摘要变化创建新 session。

## 验证与剩余门禁

普通构建验证缺能力拒绝；opt-in 验证组合 backend、坏摘要/身份/预算、取消/超时、
拒绝/关闭与缓存命中。真实模型须独立记录来源、许可、转换、摘要和中英文/小字/混合
DPI 样本。合成网络只验证装配/推理契约，不证明 OCR 质量；未通过真实模型验收前
M7-06 保持 In Progress，不扩大 Windows/Wayland 或实时性保证。

资源边界：清单 32 KiB，param/字典各 2 MiB，bin 各 64 MiB；所有字段白名单，
文件非空、精确大小/SHA-256，字典 UTF-8。det_side 32..960，rec_width 32..640，
rec_height 16..64；capture 沿用 8 MiB，session cache 沿用 4/16 MiB，boxes 上限 128。
工具每框文本 1 KiB、总 JSON 16 KiB，超预算失败，不截断冒充完整 OCR；显示器
身份/几何/主显示状态或 epoch 改变、失败/过期会使本次结果无效，管线错误清空 registry。
模型清单的摘要证明一致性，不证明来源可信；显式清单应来自维护者审核的模型包。
这些预算不承诺限制任意 ncnn 模型的全部内部 tensor 分配。

OCR failed 规范 ToolResult 进入宿主终态门禁，即使模型回复“读完”也不能把本轮标成 ok；
取消具有最终优先级。OCR 文本是外部数据，禁止当作系统指令；盒子不声明交互语义。
时间字段 observed_at_ms 为读取完成时间，不冒充底层截图精确时间。

2026-10-09 真实 PP-OCRv5 样本暴露
[MIRADOR-20261009-001](../dependency_feedback/mirador.md#mirador-20261009-001)：
公开 resize_area 不保持常量，影响识别 letterbox。没有本地重采样补丁，未修改 pin；
实验模型转换也不作为发布包。装配验证与真实质量结论分别见
[验证记录](../compatibility/real-ocr-composition-20261009.md)，来源/摘要/许可状态见
[资源审计](../supply-chain/real-ocr-resources-20261009.md)。M7-06 保持 In Progress。

2026-10-10 状态补记：M7-06 正式 Mirador pin ae8cf87 的公开语料字符/位置 4/4 已验收，
历史阻塞已解除；见[正确性复验](../compatibility/real-ocr-correctness-20261009.md)。
M7-07 编号图与视觉点击的契约由 [DEC-058](DEC-058-scoped-visual-marks.md) 承接。

2026-10-10 清单扩展：schema 1 可选布尔值 geometry_marks（缺省 false），只开启
编号刷新中的几何候选，配置参与 revision。预算、语义和窗口容器筛选见
[DEC-060](DEC-060-geometric-visual-marks.md)；模型资源与许可边界未扩大。


2026-10-10 清单扩展：schema 1 可选 templates，要求 geometry_marks=true，最多
16 个审核过的本地紧密 BGRA8 patch，启动核验精确大小/SHA-256、尺寸与容量；
资源/清单参与 revision。首次注册与匹配在已有 worker 执行，语义与歧义约束见
[DEC-061](DEC-061-geometric-template-resources.md)。不增加 decoder、网络来源或模型。


2026-10-10 清单扩展：可选 roi_capture 布尔值（缺省 false）由
[DEC-063](DEC-063-regional-visual-pre-capture.md)承接，非布尔值在 staging 前拒绝，
进入 canonical revision；资源来源、分发许可和预算不变。

## 2026-10-11 部分更新

[DEC-068](DEC-068-bundled-vision-and-recoverable-tool-rejection.md) 在 Linux 产品首范围
更新本决定的以下部分：Linux 原生产品默认启用固定 PP-OCRv5 / ncnn 包；普通开发服务保持显式启用，OCR 原始采集上限为 32 MiB，其余生命周期与身份规则保留。 其余契约继续有效。
