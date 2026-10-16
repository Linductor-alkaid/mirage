# Mirador 反馈台账

> 状态：Active
> 负责人：Mirage维护者；处理方：Mirador上游
> 更新日期：2026-10-09
> 工作项：[M7-06](../plans/m7-visual-tools-and-input.md)
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

## MIRADOR-20261009-001

状态 Open；上游 PR [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35)
已于 2026-10-09 创建并回读 Open、作者 Linductor-alkaid、head `0527166`（基于上游
master `f0f6758`）；合并与授权升级 pin 前本条保持 Open。负责人 Mirage 维护者，
处理方 Mirador。

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

状态 Open；随 mirador#35 第 2 提交 `93c3bda` 待评审合并。负责人 Mirage 维护者，
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

状态 Open；随 mirador#35 第 3 提交 `668459b` 待评审合并。负责人 Mirage 维护者，
处理方 Mirador。

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
