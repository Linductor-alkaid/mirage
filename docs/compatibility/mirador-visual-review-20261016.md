# Mirador 标注视觉复核：真实屏幕、网页与视频帧

> 状态：Completed（本轮复核交付；质量验收仅 5/11，非全能力通过）
> 日期：2026-10-16
> 负责人：Mirage 维护者；执行及视觉复核：Codex（非人工签字）
> 工作项：[M7-20](../plans/m7-visual-tools-and-input.md)
> 依据：DEC-016、DEC-057、DEC-058、DEC-060、DEC-063、DEC-068
> 数据：[逐图结论与哈希](data/mirador-visual-review-20261016.json)

## 结论

实际执行了 `ScreenProvider → PpOcrPipeline::mark → mirador OCR/geometry/fusion/SoM`，
逐图查看原图、真实编号图和结构化识别文字。11 个基线场景采集像素一致 11/11，
编号、引用、区域内 anchor 对应 11/11；按下述场景标准，质量通过 5/11。
这说明采集和坐标映射可用，但当前默认 OCR 配置与几何候选质量不足以支持复杂桌面
“完整、正确理解”的声明。框画得正确不等于文字识别正确。

保持图和权重相同，仅把现有 manifest 的 `rec_width` 从 320 调到 640，独立长行
样本从 5/8 精确匹配提升到 8/8；暗色代码样本从 12/17 提升到 16/17。
因此长行问题优先归为应用配置适配，不登记为已确认的 Mirador API 能力缺口。
640 仍不能修复全部误识别、多语言和长正文问题。本轮没有改变产品默认值、模型或依赖。

## 环境与方法

- 基线 HEAD：`dbd99110a7bac6d48802f61192303a6c142d5da5`，当前
  `feat/m10-basic-agent-tools` 工作树有其他任务的未提交改动；本轮未创建 commit。
- Ubuntu 24.04.5 LTS / Linux 7.0.0-38-generic / x86_64 / GCC 13.3.0。
  `native-release`，`MIRAGE_WITH_PPOCR=ON`，正式离线 PP-OCRv5 mobile 包。
- Mirador：`ae8cf87581c1ebe393a67374d9977a38354d0911`；Mira：
  `a29374aaf05b956cbd5ceeafc65bba4c6172675e`。资源及每张输出的 SHA-256 见数据文件。
- 当前 Wayland 宿主：用户在系统共享框授权后，调用真正的
  `ScreenProvider::create_desktop_capture_session()` 采得 3200×2000 帧。
  仅裁取本任务正文 `[1300,390,1400,680]` 做视觉复核；没有将其他聊天内容放入报告。
- Chromium 实际渲染两张公共网页：
  [Example Domain](https://example.com/)、[Python 下载页](https://www.python.org/downloads/)。
  另渲染中文/英文表单、暗色代码、几何图形、独立长行和空白负例。
  DOM 文本/布局是独立对照，不作为 Mirador 的输入区域。
- 视频先将三张变化画面编码为 H.264/yuv420p（CRF 18），再解码三帧；验证对象是
  解码后的像素。不是把原始 PNG 冒充视频解码结果。
- GTK3 在独立 Xvfb 的 `(37,53)` 位置按 1:1 显示每张图，由 Mirage 原生 X11
  `capture_roi` 真正采集。逐像素比较采集结果与显示源；正式管线生成原图/标注图及
  JSON。真实宿主图也经过该回放以复用正式 SoM 路径，坐标仅属于回放屏幕，不能用于
  原生 Wayland 输入。
- 新导出器的有限工作由 kairo `submit_auto()` 承载，消费 future；视觉 worker
  stop 后由外部 owner `shutdown(true)`。没有网络模型调用、坐标点击或新增产品并发路径。

每个质量 Pass 要求：该场景全部指定文本在忽略空白后精确匹配、目标覆盖与编号定位
正确，且没有影响本场景判断的多余标注。白屏要求 0 区域/0 编号。几何场景只计算
预定义四个矩形的覆盖，不把圆形检测当作已承诺能力。公共网页和宿主正文按可见内容
逐图判断，未伪造统一的字符准确率。置信度是模型输出，不是实测正确率。

## 逐图结果

| 场景 | OCR / 几何区域 | 视觉判定 | 具体证据 |
| --- | --- | --- | --- |
| 公共多语言网页 | 5 / 2 | Fail | 英文长行框正确却仅输出 `Ti`，另两行缺失；中文“请勿将”读为“请将”；其他脚本大量缺失/错读 |
| Python 下载页 | 27 / 0 | Fail | 标题 `Download th latest source elease` 丢字，两条说明行漏标；放大镜读作 `0`；多数导航框位置正确 |
| 中英表单/表格 | 21 / 0 | Pass | 21/21 文本精确匹配；中文、数字、小字、禁用按钮和灰色提示均被定位 |
| 暗色代码/路径 | 17 / 0 | Fail | 12/17 精确匹配；代码行变 `a`，错误标识/路径丢字符，`}` 读成 `1` |
| 几何轮廓 | 3 / 2 | Fail | 四个矩形只找到两个实心块；两个空心矩形漏标。加号作为 OCR `+` 出现且框偏大 |
| 视频准备帧 | 4 / 0 | Pass | 标题、Frame 001、状态及 Target 1 全部正确，区域对应当前帧 |
| 视频执行帧 | 4 / 0 | Pass | Frame 002 / Target 2 更新正确，目标右移 210 px |
| 视频完成帧 | 4 / 0 | Pass | Frame 003 / Target 3 更新正确，再右移 210 px |
| 空白负例 | 0 / 0 | Pass | 无文字/编号；原图和标注图像素相同 |
| 实际 Wayland 桌面正文裁剪 | 6 / 5 | Fail | 短标题可读；长正文大面积缺失/错字，目录图标误读为 `日`，有文字碎片几何候选 |
| 独立长行控制 | 7 / 0 | Fail | 8 个预定义文本仅 5 个精确匹配；47 字符英文行无有效区域，39 字符代码行变 `a` |

编号地图均和画出的区域一致，未发现统一坐标偏移。视频目标框覆盖完整蓝色块，文字和
目标位置正确；不要求它等同于紧贴字形的 OCR 框。表单的禁用按钮也会获得
`selectable=true`，该字段只说明完整区域可见，没有交互语义。

## 诊断对照与问题归属

1. **长行与默认宽度预算（应用配置）**：`rec_width=640` 是现有公开选项，Mirage
   已接受，无需修改依赖。相同 5 图复验：长行 5/8→8/8，代码 12/17→16/17；
   英文网页三行恢复；Python 标题和 Guide 长行恢复。宿主 OCR 从 6 区增至 10 区，
   仍有长正文错误。公开 `PpOcrRecOptions` 定义识别高度 48、动态宽度上限；
   对照复现后检查 pinned reference 实现，确认达到上限会固定高度压缩宽度。
   这不是截图缩放或全局坐标错误。后续应在应用层评估宽度/ROI/分块策略及资源预算，
   按 DEC-068 流程另行验证默认值，不能只凭这五图宣布 640 普遍正确。
2. **几何轮廓召回（默认算法/参数质量待改善）**：额外运行同一真实采集帧的
   `VisualSessionHost`，关闭 OCR/fusion，仅跑默认 geometry。融合前原始提案也只有
   `[71,187,111,81]`、`[571,357,131,56]` 两个实心块，排除 Mirage 的面积筛选或
   OCR 融合吞掉空心矩形。本轮未完成参数消融，不能把它宣布为 API 无法承载的缺口。
   后续由维护者验证线段/闭合参数与实屏召回，再决定是否需要上游反馈。
3. **模型及脚本覆盖**：640 下 `}`→`1`、图标→文字、Arabic/Cyrillic 错读、长
   正文丢字仍在。当前组合没有安装语义 UI/object detector；几何候选也不带按钮含义。
   必须维持模型支持范围和新观察校验，不能用模型置信度替代准确性证据。

本轮未确认需要新增依赖 API 的能力缺口，因此没有新增 `MIRADOR-*` 条目。
问题及复现已由 M7-20/本记录跟踪；若后续排除配置/模型职责仍确认上游缺口，再按
[工程规范 9.4](../project/project-standards.md#94-能力缺口与反馈台账)登记。

## 复现与产物

工具源码：[导出器](../../tests/manual/mirador_visual_review.cpp)、
[网页语料](../../tests/manual/mirador_review_corpus.cjs)、
[屏幕回放运行器](../../tests/manual/mirador_review_run.py)、
[独立 HTML 打包器](../../tests/manual/mirador_review_gallery.py)。需要 GTK3/Pillow、
Xvfb、ffmpeg、Playwright 与 Chromium，权重使用已有正式包，不下载其他模型。

本机实际执行（Playwright/Chromium 路径是当前环境安装位置）：

```bash
cmake --build --preset native-release --target mirador_visual_review -j 2
build/native-release/tests/mirador_visual_review build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json build/mirador-visual-review-20261016/host --desktop
MIRAGE_REVIEW_CHROMIUM=/home/linductor/.cache/ms-playwright/chromium-1243/chrome-linux64/chrome node tests/manual/mirador_review_corpus.cjs build/mirador-visual-review-20261016/corpus /home/linductor/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright
/usr/bin/python3 tests/manual/mirador_review_run.py --output build/mirador-visual-review-20261016 --binary build/native-release/tests/mirador_visual_review --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json
```

长行样本后来单独追加运行，未覆盖原始十图证据。对照 manifest 将五个 artifact 的
path 转为正式包中的绝对路径，只改 `rec_width=640` 后写入本机产物根 `rec640.json`：

```bash
/usr/bin/python3 tests/manual/mirador_review_run.py --output build/mirador-visual-review-20261016 --binary build/native-release/tests/mirador_visual_review --manifest build/mirador-visual-review-20261016/rec640.json --samples public-example,public-python,terminal-dark,host-crop,line-lengths --results-dir rec640
MIRAGE_REVIEW_GEOMETRY_DIAG=1 /usr/bin/python3 tests/manual/mirador_review_run.py --output build/mirador-visual-review-20261016 --binary build/native-release/tests/mirador_visual_review --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --samples geometry --results-dir geometry-diag
python3 tests/manual/mirador_review_gallery.py --review docs/compatibility/data/mirador-visual-review-20261016.json --output build/mirador-visual-review-20261016/review.html
```

本机受控产物根 `/home/linductor/mirage/build/mirador-visual-review-20261016/`：
`review.html` 可离线切换原图/标注、320/640 和查看区域文字；`samples/` 保留基线
PNG/PPM/JSON，`rec640/` 保留五图对照，`geometry-diag/` 保留融合前提案，
`corpus/sequence.mp4` 保留视频。`host/raw.ppm` 是完整真实桌面，仅本机保留，
不嵌入报告或提交。提交仅含小型摘要和脚本；产物保留到维护者归档/清理 build 目录。
主机分辨率变化后必须重新选择安全裁剪，不复用硬编码区域。

## 验证记录与限制

- Release 导出器构建通过；基线 11 图、640 对照 5 图和融合前几何诊断 1 图均实际
  执行成功并通过像素比较。质量判定不由进程退出码产生。
- `ctest --test-dir build/native-release -R '^(ppocr_pipeline_test|visual_marks_test)$' --output-on-failure`：
  2/2 目标通过（含状态 fixture 共 4/4）。
- `ctest --test-dir build/debug -R '^(visual_marks_test|visual_contract_test|visual_scene_mapping_test|portal_screen_fixture)$' --output-on-failure`：
  4/4 目标通过（含 fixture 共 6/6）。Portal fixture 为模拟协议，和宿主真实采集证据分开。
- ASAN/UBSAN 最初缺少目标二进制，CTest 返回 Not Run；分别构建
  `visual_marks_test visual_contract_test visual_scene_mapping_test` 后重新执行
  `ctest --test-dir build/{asan,ubsan} -R '^(visual_marks_test|visual_contract_test|visual_scene_mapping_test)$' --output-on-failure`：
  各 3/3 目标通过（含 fixture 各 5/5）。这两个预设 `MIRAGE_WITH_PPOCR=OFF`，
  不代表真实模型推理经过 sanitizer。初次导出器诊断扩展的 Status accessor 编译错误
  已修正并成功重建；没有依赖代码改动。
- Python 语法、Node 语法、相关 diff 空白检查通过。HTML 实际浏览器检查：11 个
  场景可选、640 长行显示 8 行、区域裁剪可见、无 JavaScript 错误；查看截图确认布局。
- 未执行：Windows、宿主多屏 DPI/旋转、真实语义 object detector、tracker/稳定 ID、
  视频实时吞吐、真实模型 ASAN/UBSAN。负责人 Mirage 维护者；补跑需相应目标环境、
  已登记模型/backend 与独立计划，不以本轮通过替代。
- 本批样本是定性诊断集合，5/11 不是产品在真实分布上的准确率。M7-07/M7-10 的
  其他门禁保持原状态，不因本轮审计交付关闭。
