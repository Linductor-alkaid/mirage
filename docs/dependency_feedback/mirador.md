# Mirador 反馈台账

> 状态：Active
> 负责人：Mirage维护者；处理方：Mirador上游
> 更新日期：2026-10-22
> 工作项：源头为 [M7-06](../plans/m7-visual-tools-and-input.md)；维护性收口见
> [依赖复用整改计划](../plans/dependency-reuse-remediation.md)（REUSE-16）
> 总入口：[依赖反馈](ledger.md)；维护规则：[工程规范9.4](../project/project-standards.md)

## 核对基线

pinned版本：`fb0dc3f85393b77845de8bffeab4201dae97c5d9`；能力核对以
`third_party/mirador/include/`与`third_party/mirador/docs/`的公开API、设计、决策为准。
现有[M3验证记录](../plans/m3-mirador-integration.md)未登记Mirador缺口。
M7-06 本轮核对 PP-OCR 公开后端、集成指南与 smoke；复现见下表，不修改 pinned 代码。

## 问题索引

| 编号 | 主题 | 状态 | 复现/版本 | 上游 | 跟进负责人 |
| --- | --- | --- | --- | --- | --- |

| MIRADOR-20261009-001 | resize_area 重采样不保留常量，破坏 OCR letterbox | Resolved（本地） | fb0dc3f；公开最小复现 | [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35) 已合并（ae8cf87），pin 已升级并复验 | Mirage 维护者 |

| MIRADOR-20261009-002 | 连通域 min_x/min_y 不随洪泛更新，组件/文本框边界截断 | Resolved（本地） | fb0dc3f；回归用例即复现 | [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35) 第 2 提交，随 pin ae8cf87 生效 | Mirage 维护者 |

| MIRADOR-20261009-003 | PP-OCR 参考预处理偏离参考部署，真实模型 0/4 | Resolved（本地） | fb0dc3f；四语料公开复现 | [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35) 第 3 提交，随 pin ae8cf87 生效 | Mirage 维护者 |

| MIRADOR-20261018-001 | PP-OCR rec 逐框串行且无批处理/截断，密集桌面帧无法进入交互预算 | Resolved | 上游 M8-01…06 交付（`5565436`）；pin 消费复验见 [M8-07 兼容性报告](../compatibility/mirador-budgeted-ocr-20261021.md) | [mirador#41](https://github.com/Linductor-alkaid/mirador/issues/41) 已提报 | Mirage 维护者 |

## MIRADOR-20261009-001

状态 Resolved（本地）；上游 PR [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35)
2026-10-09 经维护者授权合并（master `ae8cf87`），pin 已升级并完成独立复验（回执见
003 条目末尾的共同合并段）。负责人 Mirage 维护者，处理方 Mirador。

公开 API `resize_area` 对有效 Rgb8 2×1 全白输入（stride=6，预算 1024）重采样到
3×1，输出 `128 255 255`；应为 `255 255 255`，恒定输入的面积加权平均应保持恒定。
不需要权重、Screen Provider、授权、平台或并发，排除 Mirage 适配、模型配置与 API 误用。
原 API/实现没有禁止放大；`letterbox` 本身调用此 API 放大识别裁剪。

最小复现：[源码](repro/mirador-resize-constant.cpp)；编译/运行见
[复现说明](repro/README.md#mirador-常量缩放复现m7-06)。GCC 13.3.0，Linux x86_64，
默认缺陷模式退出 0；`--expect-fixed` 退出 1。两模式分别要求“现象存在/修复成立”，
API 或初始化错误退出 2，不能把退出非 0 笼统当作修复。

相关公开调用：`PpOcrRecBackend::recognize` → `letterbox` → `resize_area`。
同一英文文字裁剪使用原始 ncnn 输入方式读出 `Submit task`；使用 Mirador letterbox
读出 `cY`，模型 reshape 后的裸 ncnn 输出与原模型逐 token 一致，排除输出布局错误。
真实模型四例全部未达精确识别要求，见[验证记录](../compatibility/real-ocr-composition-20261009.md)。
黑色居中 padding 与模型原始动态宽度也有差异；缩放修复不代表全部质量问题自动解决。

影响：图像缩放、letterbox、感知指纹与真实 OCR；M7-06 质量和 M7-07 文字定位门禁延期。
最小期望：公开重采样对任何常量图、整数/非整数放大和缩小保持常量，覆盖共享源像素
跨多个目标像素的权重，保持尺寸/预算/错误契约；不是要求新增 Mirage 特有 OCR 策略。
验收：最小复现固定模式通过，缩放边界测试通过，现有 image/letterbox 回归，真实
中英文/小字/混合 DPI 样本重跑且报告命中/失败，授权升级后 Mirage 全量及 sanitizer 复验。

临时措施：无自研重采样替代，无直接依赖修补。产品 OCR 默认关闭，只以显式 opt-in
验证装配；使用者不能据推理完成宣称质量验收。维护者取得上游修复与授权升级后复核
清单、模型转换和真实样本，再移除限制。Mirador pin 未另改（仍 `fb0dc3f`，其
`resize.cpp` 与上游 master 一致）。

2026-10-09 上游修复提交：维护者授权直接修复并提 PR。上游 master `f0f6758` 核对
`resize.cpp` 与 pin 一致、缺陷仍在；根因是 `e432d51` 引入的权重表按源行/列索引
缓存，放大时共享源像素的权重被最后写入者覆写。修复按目标索引缓存连续源区间与
扁平化权重（`CoverageAxis`），权重、累加顺序与 half-up 舍入不变，与直接公式
逐位一致。commit `0527166`，6 个新回归在旧实现上全部失败（stash 对照）。

2026-10-09 回执更新：PR #35 在复核真实 OCR 质量时扩展为三个提交
（`0527166`/`93c3bda`/`668459b`，后者见 002/003 条目），标题改为
"fix(image): exact resize weights, component bounds, PP-OCR preprocessing
alignment"，已回读 Open。以修复分支叠加态（pin 未动）复跑[四语料字符与位置
验收](../compatibility/real-ocr-correctness-20261009.md) 4/4 通过。本条验收
中"真实中英文/小字/混合 DPI 样本重跑"已在公开语料层面闭合；宿主真实桌面/DPI、
上游合并与授权升级后的全量复验仍开放，状态保持 Open。

## MIRADOR-20261009-002

状态 Resolved（本地）；随 mirador#35 第 2 提交 `93c3bda` 经授权合并并随 pin
`ae8cf87` 生效（回执见 003 条目末尾的共同合并段）。负责人 Mirage 维护者，
处理方 Mirador。

公开契约：`db_postprocess_aabb`/`recover_contour_boxes` 依赖
`image_internal::find_components` 返回组件的完整包围盒。`collect_component`
以栅格扫描首个前景像素为种子设置 `min_x/min_y` 后，洪泛过程中从不更新（`max`
侧本就逐像素维护）；低行更靠左的组件边界被截断。可复现：构造顶行内缩的组件
（顶部两行仅右侧两列前景、其下六行左延四列），返回 bounds 宽度塌缩为顶行宽
（回归用例 `ContourBoxes.TopRowInsetBoundsCoverAllForegroundPixels`，旧实现
实测 `{8,2,2,8}`、应为 `{2,2,8,8}`）。真实影响：DB 文本框系统性切掉首字符
（480×180 "Submit task" 检测框 x=72 起、应为 24），`detect_change` ROI 回映
同样消费该统计。

影响：文本检测框、变化检测 ROI；M7-06 位置门禁。最小期望：组件 bounds 覆盖
全部前景像素。验收：回归用例通过、四语料位置门禁 4/4（见 003 验收）、既有
text_postprocess 10/10。已随 001 一并由 Independent-Verification-Agent 独立
复验。

## MIRADOR-20261009-003

状态 Resolved（本地）；随 mirador#35 第 3 提交 `668459b` 经授权合并并随 pin
`ae8cf87` 生效。负责人 Mirage 维护者，处理方 Mirador。

公开契约：PP-OCR 参考后端定位为真实 PP-OCR 模型的参考承载（DEC-015），但其
预处理偏离转换来源的参考部署（ncnn-android-ppocrv5 `ppocrv5.cpp`）：det 强制
960 方形 letterbox（参考为限长缩放、绝不放大、/32 画布 pad 114 近居中）、
阈值 0.5/0.5（参考 0.3/0.6）；rec 居中黑边压扁到固定 rec_width×rec_height
（参考为保纵横比缩放到高 48、行宽动态、无填充）。平面序未转 BGR。可复现：
[四语料验收](../compatibility/real-ocr-correctness-20261009.md)——修复前
0/4 精确命中且框切字，修复后 4/4 文本与位置通过（置信度 0.97-0.99）。归一化
`x/127.5-1` 本就正确：ImageNet 对由转换脚本融合进 det 图的 Scale 层承接
（`tests/manual/prepare_ppocrv5_reference.py` 第 44-53 行），无需外部再做。

影响：真实 PP-OCR 质量、M7-06 字符与位置门禁、M7-07 文字定位。最小期望：与
参考部署逐阶段对齐，保持既有预算/错误契约与 `OcrBackend` SPI。验收：四语料
文本+位置 4/4、ppocr smoke 40 ok、ASAN+UBSAN 63 checks 0 failure、合成契约
fixture（16×16 kernel）随下游同步。已由 Independent-Verification-Agent 独立
复验；彩色 UI/多行/旋转文本与真实桌面未验，宿主 DPI 变换另属 M7-06/M7-10。

2026-10-09 合并与升级回执（001/002/003 共同）：PR #35 CI 全绿（14/14，含
clang-format/clang-tidy、gcc/clang 矩阵、ASAN/TSAN/UBSAN、integrations-ncnn、
msvc、ndk）后经维护者授权以 merge commit 合并，master `ae8cf87`；工作分支已删
（本地与远端）。维护者授权升级 pin：gitlink 与 lock 同步至 `ae8cf87`
（v0.3.0-121），独立复验通过——native-debug/native-release 57/57、ocr-debug
56/56、四语料字符与位置 4/4（正式 pin）、ASAN+UBSAN 与 TSAN 各 63 checks
0 failure、公开复现探针固定模式 exit 0、mirador 全树 clang-format 178 文件
0 违例（证据见
[正确性复验](../compatibility/real-ocr-correctness-20261009.md)）。三条状态改
Resolved（本地）。剩余开放项不随本组关闭：OCR 权重分发的许可证据
（[资源审计](../supply-chain/real-ocr-resources-20261009.md)）、宿主真实桌面/
DPI 变换与彩色 UI/多行文本样本（M7-06 边界与 M7-07/M7-10 承接）。

## 登记规则

新增编号`MIRADOR-YYYYMMDD-NNN`，不可复用。每条先排除API误用、平台限制与Mirage
Provider职责，记录版本/公共API/测试核对、最小复现、影响、期望语义、最小能力、延期影响、
临时边界与移除条件、可验收结果、负责人、状态和按日期追加的上游回执/迁移记录。
没有对应能力缺口时保留空台账；Open只表示待处理，上游关闭后仍须授权升级和复验。

## MIRADOR-20261016-001：桌面质量参数与精细变化门控建议

状态：Proposed；负责人：Mirage 维护者；处理方：Mirador 上游。
上游：[issue #36](https://github.com/Linductor-alkaid/mirador/issues/36)（Open，已回读）。
工作项：M7-21/M7-22；证据：[产品调优与其他能力复核](../compatibility/mirador-tuning-and-capabilities-20261016.md)。
这是质量校准反馈，**不将已有参数能够解决的问题登记为缺少 API**；其中显式跳过
dHash 提前返回属于待讨论的最小 API 扩展，不构成当前产品的阻塞。

核对版本：pin 与反馈时上游 master 均为 `ae8cf87581c1ebe393a67374d9977a38354d0911`。
已核对 segment detector/proposal、change detection、OCR backend、index、fusion、
ObjectTracker 的公开头、对应 integration card 和相关测试。无模型、无平台最小
复现：[2px 空心矩形](repro/mirador-outlined-rectangle.cpp)：默认 residual=1.5 时
0 线段/0 提案；2.5/4 时 4 线段/1 提案。真实抗锯齿描边需 residual=4；只放宽
residual 且 min_length=8 在复杂正文触发 1024 线段拒绝。72 个画面/参数组合已记录，
没有扩大预算。Mirage 产品使用现有 min_length=16/residual=4 后矩形 2/4→4/4，
正文碎片 5→0；短线段取舍见 DEC-060。不是采集、坐标、权限或后处理调用错误。

全图 1000×680 中改动 16×16 像素，公开 `detect_change` 返回 similarity=1/kNone；
即使 fingerprint threshold=1 仍提前返回，当前 API 没有强制 block diff 的选项。
已知 ROI 裁剪后同 API 能检出，说明应用可使用目标局部复核；当前 Mirage 的新 OCR
和动作像素守卫保持原纪律。期望上游考虑显式精细模式或更清晰的粗筛消费边界，
避免下游将 kNone 当作精确等价。延期影响是通用局部变化漏检/额外复核开销，
不允许为绕过此问题另建线程、队列或调度器。

同时反馈已有配置经验：OCR rec_width=640 的长行收益/内存代价、实验跟踪
PSR=5 对 NCC=1 真匹配的拒绝及 PSR=4 的小样本对照。它们均有现有配置入口，
不请求新增应用业务 API。最小建议是 thin/antialiased stroke 的召回/噪声矩阵、
有界 desktop profile 或检测改进、可选择的精细变化模式和阈值校准说明。
验收需覆盖 1/2/3/4px、明暗/文字密集/小图标、预算与取消，汇报误候选、漏检、
资源开销和 inner/outer 重复框。单一 UI patch 的跟踪结果不能转正实验 API。

| 日期 | 跟进 |
| --- | --- |
| 2026-10-16 | 维护者授权调优并反馈；产品用现有参数完成复验，上游 #36 已创建并回读 Open。未修改 pinned 代码/指针；等待上游讨论，若需升级仍按独立授权/锁定/回归流程执行。 |

## 登记规则

新增编号`MIRADOR-YYYYMMDD-NNN`，不可复用。每条先排除API误用、平台限制与Mirage
Provider职责，记录版本/公共API/测试核对、最小复现、影响、期望语义、最小能力、延期影响、
临时边界与移除条件、可验收结果、负责人、状态和按日期追加的上游回执/迁移记录。
没有对应能力缺口时保留空台账；Open只表示待处理，上游关闭后仍须授权升级和复验。

## MIRADOR-20261016-002：YOLO 模型预处理配置

状态：In Progress；负责人 Mirage/Mirador 维护者；工作项 M7-23 / DEC-097。

公开接口核对：原 YoloDetectorOptions 固定 area/pad0；真实 UI 模型按 bilinear/pad114
部署。相同 checkpoint 转 ONNX 后可检出 Python 下载按钮，而既有路径漏检，见
[上游 issue #38](https://github.com/Linductor-alkaid/mirador/issues/38) 与 PR #39 真实验收。
期望最小能力为显式插值/padding、实际整数变换及配置缓存隔离，保持旧默认。

维护者已授权直接修复：上游 DEC-024 添加 adapter-only profile；本次 pin 包含修复、
PR #37/#39。真实模型同输入对照、默认等价、坐标与故障验证见本次兼容性报告；
PR 合并仍待评审。无应用侧图像重实现或新增并发设施。延期会继续漏按钮并使
模型默认输入不匹配；验收要求真实权重、独立输入/推理/坐标和 Mirage mark 链路。

## MIRADOR-20261018-001：PP-OCR rec 逐框串行且无批处理/截断，密集桌面帧无法进入交互预算

状态：Resolved（2026-10-21）；负责人 Mirage 维护者，处理方 Mirador 上游；工作项
M8-06 / DEC-107（上游 M8-01…07）。

公开接口核对：`PpOcrBackend::recognize`
（`third_party/mirador/integrations/ocr_ppocr/ppocr_backend.cpp:401`）对 det 产出的
每个候选框串行调用一次 rec，无批处理、无按分数的 top-K 截断、无 deadline 内部分数
产出（rec 循环仅在框间检查 `is_cancelled`，超时即整体返回 kCancelled 并丢弃已完成
行）。已排除 API 误用与 Mirage 职责：线程数、det_side、max_boxes 均为调用方选项并
已按生产设置（det_side=960、max_boxes=512）；DEC-076 的 512 候选是 Mirage 侧策略。

影响与证据：生产会话 `ab2c8dd3…`（2026-10-18 23:08，interactive Portal 路径）中
`desktop.vision.ocr` 连续 3 次 `native_ocr_expired`（timings：3 次调用恰好
15,000ms），整轮以"桌面视觉读取失败或被拒绝"失败。本地基准（打包
PP-OCRv5-mobile，1600×1000 密集合成帧，det 359 框，max_boxes=512）：
num_threads=1 时 det+rec ≈12.9s（约 35ms/框），det 仅占约 2%；rec 阶段
num_threads=4 在默认 libgomp 调度下反而劣化 1.6–1.8×（单行 rec 46.6→91–96ms，
fork/join 开销主导小张量）；512 框最坏情况估算 16–33s，任何线程配置都无法进入
Mirage 交互路径的 5 秒 OCR 预算（DEC-076 冻结）。同机降频状态，绝对值偏保守，
对比结论方向不变。

期望最小能力（任一即可显著缓解）：rec 批处理（多行一次前向）、按分数或阅读序的
top-K rec 截断选项、或 deadline 到达时返回已完成行的部分结果（新增显式状态而非
kCancelled）。延期影响：文本密集桌面（QQ/浏览器多窗口）的整屏 OCR 持续超时，
模型被迫退化为纯图像判断或触发 DEC-107 的"分析未完成"反馈路径；ROI 小图观测
可绕过但增加交互次数。

临时边界：Mirage 侧仅保留 det 四线程加速（`integration/mirador/src/ppocr_pipeline.cpp`，
rec 维持单线程避免负优化）；不改 pinned 代码、不加并发设施。移除条件：上游提供
上述任一能力并经 Mirage pin 消费复验。可验收结果：512 框密集帧在 5 秒预算内
完成 rec 或返回可用的部分文本。

上游回执：2026-10-19 以台账证据提报
[mirador#41](https://github.com/Linductor-alkaid/mirador/issues/41)
（PP-OCR: per-line serial rec cannot meet interactive budgets on dense desktop
frames），含测量表、根因定位、最小能力诉求与验收建议；处理进展随该 issue 跟踪，
合并前本条维持 Open。

上游交付与消费复验（2026-10-21）：上游以 DEC-025 交付预算 OCR（M8-01…06：
top-K/调用方优先级、软预算 + 硬 deadline 三分、显式 partial 与停止原因、
逐框完成态；rec batching 经 M8-05 实验收口为有证据的能力性 no-go，以
no-go 证据响应）；pin `cb45a102..5565436` 消费后，`read_image_ocr` 在
DEC-076 五秒预算内对 69 框密集场景稳定返回可消费的 partial 报告
（recognized=64/deferred=5/`rec_line_limit`，1.7–3.2 s，跨进程 digest 逐位
一致），可验收结果（"5 秒预算内完成 rec 或返回可用的部分文本"）满足，
临时边界（依赖 det 线程调优绕行）被预算路径取代——线程数保留为调用方
调优项而非预算前提。完整证据与边界见
[M8-07 兼容性报告](../compatibility/mirador-budgeted-ocr-20261021.md)。

## MIRADOR-20261019-001

连续感知与任务注意策略的公开契约。

状态：Accepted（上游部分交付与 Mirage Experimental 消费；完整覆盖/事件仍开放）；负责人 Mirage 维护者，处理方
Mirador 上游；工作项 M9-07/13/18，方案见
[DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)。
上游已有讨论：[issue #41 补充](https://github.com/Linductor-alkaid/mirador/issues/41#issuecomment-6258623068)
（2026-10-19 回读，issue Open）；2026-10-20 台账补充已回读为
[后续评论](https://github.com/Linductor-alkaid/mirador/issues/41#issuecomment-6285521869)，
不代表上游已承诺 API。
直接 PP-OCR 性能问题仍归 MIRADOR-20261018-001，两个交付层次独立推进。

最新消费基线（2026-10-22）：Mirador master `23f7ed2`，上游 DEC-026 已 Accepted，
M9-01/02/03 已交付。Mirage 消费同步关系构建、source epoch/策略与当前帧注意
规划，保持 Experimental/opt-in；完整覆盖 accept 与时间事件仍待上游 M9-04/05。
契约/命令/实际验证见[消费审计](../supply-chain/mirador-continuous-perception-20261022.md)。
下述 cb45a102 调查和日期化诉求保留为历史依据，不再代表最新 pin 的能力集合；
不将反馈整体标 Resolved，也不在 Mirage 复制未交付的通用视觉协调算法。

核对版本 `cb45a10271c53d3b189bb33eef3f692d94feb562`，公开
`perception_session.hpp`、`change_detection.hpp`、`fusion.hpp`、`object_tracker.hpp`、
`ocr_backend.hpp`、PP-OCR opt-in 头及 session/caching card；检查
`tests/fusion/perception_session_test.cpp`、`session_fuse_test.cpp`、
`stable_id_tracker_test.cpp` 与 `tests/image/change_detection_test.cpp` 对应契约用例
（源码核对，未执行依赖测试）。

可复核的边界：已有 ROI OCR/detector、请求参数/模型身份隔离的结果缓存、融合与稳定 ID，
不缺这些原语；
`fingerprint_early_exit=false` 也已提供，不能继续称为缺 API。但 `analyze_change`
相对上次分析输入推进签名，`fuse` 发布快照，现有公开接口未统一定义跳帧下的已确认/
已发布基线、区域 pending、任务注意配置与选择性事件。当前 OCR 返回
`Result<vector<TextRegion>>`，不能从空行区分未识别/缓存/预算截断；该部分与 #41
直接 partial-result 诉求协同。M9 连续场景还需明确以下组合行为，而非拼装调用即
可假定存在：焦点切换无需新像素即可查询状态，预算先用于相关候选，静音变化仍
更新内部状态，迟到结果不覆盖新内容。

最小验收复现序列（契约测试待上游/消费实现补齐）：帧 A 同时有目标和背景动画，
B 仅目标文字改变，C 仅背景改变且 B 的 OCR 尚未完成；合并/跳帧后仍需处理目标
变化。静音背景再切焦背景，应返回当前覆盖/年龄及未识别状态；不能漏报 B，也
不能凭旧缓存宣布 C 全部已确认。实现需覆盖策略切换和过期异步结果的版本冲突。

期望最小能力：在现有原语上提供同步、无调度器的连续协调契约，支持宿主传入
实体/锚定 ROI/文本类别焦点、任务权重和预算，输出来源可追溯的实体增量、识别
覆盖/新鲜度、已确认与待处理状态、可选择通知及基线 reset。任务重要性独立于
视觉置信度；外围保留预算与结构变化提示，不自行解释任务意义。具体接口命名由
上游评审，不预设必须新增某个 class，也不要求 Mirador 持有产品永久语义。

职责排除：屏幕采集、pet 排除/移动、任务规划、模型请求、产品语义绑定、输入权限
和通知/任务调度属于 Mirage/Mira；不是本反馈要求下沉的能力。延期影响：M9-18
完整通用连续协调验收暂不能完成；Mirage 仍可先交付共享帧、现有 ROI/cache 消费、
语义协议和 pet 平台探针，不应在应用复制第二套视觉稳定化/跟踪算法。

临时边界：私有 Adapter 仅消费已有 ROI/cache/change，产品语义保留在 VisualScene；
未识别区域标 unknown/pending，需完整结果时走受限新识别/图像回退。不修改
pinned 依赖，不新增线程/队列/调度器。移除条件：上游契约经评审交付、获准升级
pin 并通过消费侧验收。验收包含上述 A/B/C、微小文字/弹窗、焦点切换、策略过期、
ID 复用、partial/迟到、资源上限和取消；以实际 backend 调用及漏事件验证收益，
不能只测返回数据变少。

2026-10-20 补充（沿用本编号，状态仍 Proposed）：维护者明确低资源连续帧采用
增量视觉状态、事件驱动理解与按需 VLM。通用契约进一步要求跨帧实体/几何关系
差分携带来源、实例代际、证据时间窗和事件开始/更新/结束；遮挡、未分析、丢失
不等同确认消失，源重置显式终结旧代事件。现有 fusion/StableIdTracker 与
ObjectTracker 原语继续复用，不能将已有能力统称为缺失；完整连续关系/事件契约
仍需上游评审，未在 Mirage 复制算法。建议验收在 A/B/C 序列外补相似对象交换、
遮挡后重捕获、关系改变、乱序/迟到、源重置及关闭额外 VLM 的状态连续性。

产品事件日志预算/保留与 gap 恢复、任务重要性、VLM 升级去重、ROI/短窗模型请求
和动作反馈关联属于 Mirage/Mira；不要求 Mirador 持有视频历史或常驻模型。
研究映射及现有 API 边界见
[研究记录](../research/2026-10-20-event-driven-incremental-visual-perception.md)，
工作项 M9-05/07/08/18。仅补充本地诉求和验收方向，未修改 pin 或执行运行验证，
不代表上游已接受接口承诺；本补充已同日回读上游（见条目头部回执链接）。

同日范围澄清：MiniCPM-o 4.5 只作为视觉 token 压缩和时分复用机制的研究参考。
Mirador 仍负责通用视觉状态与证据筛选；模型内学习式重采样不下沉为通用视觉库
的新职责。时间单元的组织、Mira 消费与动作反馈关联在宿主侧冻结，不要求 Mirador
引入 MiniCPM 模型、权重、运行时或实现模型序列协议。本反馈范围和状态保持不变。
