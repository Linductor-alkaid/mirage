# DEC-097：YOLO 候选接入视觉标注

> 状态：Accepted
> 日期：2026-10-16
> 依据：维护者明确要求将更新、修复及 YOLO 接入 Mirage。
> 工作项：M7-23

视觉 Pipeline 在已有 manifest 中接受可选 `detector` 配置，装配 pinned Mirador 的
YOLO/ncnn backend。只在显式 `mark` 时运行，普通 OCR 和图像 OCR 不调用 Detector。
使用已有 VisualSessionHost、进程 Executor owner、融合、全局坐标变换、SoM 和引用
注册表；不引入新线程、调度器或模型 runtime。Detector 使用 Refresh，防止近似图像
指纹复用旧候选。候选不证明控件可交互或禁用状态，原有动作权限与重新观察规则继续生效。

`MIRAGE_WITH_YOLO` 随 PPOCR 默认启用编译，显式关闭后带 detector 的 manifest 拒绝
启动。模型由离线 `MIRAGE_YOLO_MODEL_DIR` 提供，构建期校验固定锁、复制到 vision 包；
未提供时保留 OCR 包。运行时无下载，已声明 detector 缺失/损坏必须拒绝，不能静默退回 OCR。
通用模型可由自定义清单提供；内置离线准备仅接受已审计 OmniParser v2 UI 模型。

新增 `detector` 字段：`model_id`、`artifacts.param/bin`（path/sha256/bytes）、
`class_names`（1–256 项）、`input_side`（64–1024、32 倍数）、`preprocessing`
（`area0` 或 `bilinear114`）、`confidence_milli`（1–1000，默认 250）。全部清单内容
进入 revision。权重上限 128 MiB，param 上限 2 MiB；推理单线程、输入上限 1024、
候选上限 1024，融合/发布沿用既有有界结果与超时，不静默吞掉阶段失败。

上轮 OCR 动态宽度 640、几何线段 16/4 调优一并交付，非通用最优声明。
Mirador 的 area/pad0 默认不改；UI 模型明确选 bilinear/pad114。ncnn 字节双线性核
与 OpenCV 有舍入差异，应分别验证输入误差、相同输入的独立推理以及最终目标位置。

验收：无模型构建、合成模型资源/故障/坐标回归、真实 UI 权重端到端标注、负原点和
非零 ROI、引用与锚点、取消/期限及停机、ASAN/UBSAN、安装包布局。真实宿主多屏/DPI
与跨平台点击成功率不由离线标注结果代替。

管线替换为每实例分配独立 source/worker identity。kairo 停止后保留注册诊断，
复用固定 worker 名会被拒绝；新实例仍由原进程 owner 管理，旧实例先停机清理。
同一进程重建的回归必须通过，不能只验证首次启动。
