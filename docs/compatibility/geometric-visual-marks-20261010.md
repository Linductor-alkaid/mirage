# M7-07 无文字闭合区域的产品编号与点击

> 状态：Verified（Linux 无文字闭合按钮增量；M7-07 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-060](../decisions/DEC-060-geometric-visual-marks.md)

## 范围与装配

可选 OCR manifest schema 1 新增布尔 geometry_marks，缺省 false；类型错误在
资源装载/worker 启动前拒绝，配置参与 canonical revision。启用后仅 mark 路径
运行已有 Mirador SegmentGrowingLineDetector/filter/merge/propose_regions，
候选经既有 EvidenceSet/PerceptionSession/SoM/PNG supplier 发布。继续使用同一
kairo blocking worker 与 Runtime owner，没有增加 Detector、matcher 或调度器。

使用公开默认几何参数：4 MiB 灰度转换/工作内存、1024 线段、64 提案。提案预算
在候选筛选前检查，超限拒绝整体；产品仅融合 tight 面积不超过整帧 1/4 的提案，
原始提案仍在 session 分析回执。空文本 OCR 不作为融合事实；这两项避免空 OCR
框或窗口/面板外框通过 containment 把内部候选吞成一个编号。其他 OCR、融合
与匹配算法不变，普通 refresh/固定文本后条件不启用几何。

闭合区域没有文字、模板身份或交互语义；目标编号沿 DEC-058 的权限、scope、
epoch、全区域可见、新截图逐像素相同和固定窗口门禁执行。成功仍由 DEC-059 的
动作前固定业务文本与新 OCR 证据判定。较大控件/无轮廓图标不在本范围。

## 实际 Canvas 验收

[脚本](../../tests/manual/visual_geometry_acceptance.py)在私有 Xvfb 520×220 中绘制
480×180 Canvas，原点 (20,20)，标题 Canvas 始终不变，无 AccessibilityProvider。
初始画面是两个黑色方形轮廓，无文字；实际 PP-OCR/几何/SoM 发布的每个编号均
检查 geometry 来源、空文字/模板身份。脚本 provider 在同源 PNG 中分别选择左/右
目标，通过真实 XTest 按下/释放，应用收到事件后绘制 Task saved，再经只读重验和
终态新 OCR 核验。脚本选择只证明装配，不证明在线模型视觉推理。

| 场景 | 结果 |
| --- | --- |
| 左侧无文字目标 | 真实按下/释放；新 OCR verified |
| 右侧无文字目标 | 选择另一个编号；新 OCR verified |
| 右侧点击后不重绘 | unverified；宿主 failed，成功回答被抑制 |

每例一个 PNG 输入、一次完整点击；backend 计数 1→2→3→4，scope 更新。测试
只在目标框中心 ±1 px 接受实际事件，固定预期在工具调用前声明；不是按画面变化
认定成功。外部资源来自既有 PP-OCR 清单，脚本仅生成增加 geometry_marks 的本地
清单，不下载/复制/分发模型。样本摘要、动作回执与来源摘要归档于
[证据 JSON](data/geometric-visual-marks-20261010.json)。

```bash
python3 tests/manual/visual_geometry_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-geometry-20261010
```

## 回归

[编号测试](../../tests/integration/visual_marks_test.cpp)使用真实 pinned 几何算法，
验证默认关闭、双目标、负原点、窗口外框筛选、大空 OCR 框、普通 OCR 不产生几何、
裁切目标不可选、取消、过期、shutdown、提案超限整体失败与非法面积参数准入拒绝。
[清单测试](../../tests/integration/ppocr_pipeline_test.cpp)覆盖 string/number/null/object
伪布尔值拒绝。已有视觉动作测试继续覆盖权限、换代、异常、执行中取消、拒绝/超时、
围栏与关闭，不自动重发副作用。相关后台任务均沿现有 Executor future/worker 生命周期。

| 验证 | 实际结果 |
| --- | --- |
| ocr-debug 全量 | 59/59，0 failure，24.02 s |
| ASAN/UBSAN 定向 | 7/7，0 failure，14.07 s |
| TSAN 定向 | 7/7，0 failure，14.57 s |
| 完整外部权重几何 Canvas | 3/3；左 mark 1、右 mark 2，负例未验证 |
| 同 Canvas ASAN/UBSAN | 3/3，0 failure |
| 原有文字 Canvas 回归 | 5/5，0 failure |
| native-debug 全量 | 60/60，0 failure，26.16 s |
| native-release 全量 | 60/60，0 failure，22.07 s |
| headless-debug 全量 | 59/59，0 failure，26.57 s |
| headless-release 全量 | 59/59，0 failure，23.35 s |

定向七项是 visual_marks_test、x11_visual_marks_test、ppocr_pipeline_test、
post_action_verification_test、visual_text_verification_test、visual_session_test、
visual_cache_geometry_test。ASAN 开启 detect_leaks=1，UBSAN/TSAN 开启
halt_on_error=1；TSAN 使用既有进程级 setarch -R，没有改系统设置。包含真实 X11、
合成 ncnn 权重和真实几何，不等于完整权重 TSAN 业务验收。

新增几何清单 SHA-256 为
`1f69ede65687a09af3addb555a96e1365a06eb7773f090576626a4fad02a5213`，canonical
revision `6364328632e0ff6bd31cc22744a51307b2c5ecfb8961d44cd51fc227d1d0344b`，
来源清单和字体摘要记录于 JSON。原始日志保留在 /tmp/mirage-m7-geometry-*；归档仅
含公共 fixture 文本、样本摘要和动作回执，不包含凭据或用户桌面图片。

```bash
cmake --build --preset native-debug -j 3
ctest --preset native-debug --output-on-failure -j 3
cmake --build --preset native-release -j 3
ctest --preset native-release --output-on-failure -j 3
cmake --build --preset ocr-debug -j 3
ctest --preset ocr-debug --output-on-failure -j 3
cmake --build build/ocr-asan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test post_action_verification_test visual_text_verification_test visual_session_test visual_cache_geometry_test -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|post_action_verification_test|visual_text_verification_test|visual_session_test|visual_cache_geometry_test)$' --output-on-failure -j 1
cmake --build build/ocr-tsan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test post_action_verification_test visual_text_verification_test visual_session_test visual_cache_geometry_test -j 2
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|post_action_verification_test|visual_text_verification_test|visual_session_test|visual_cache_geometry_test)$' --output-on-failure -j 1
```

实际 ASAN/UBSAN Canvas 复验在上述几何脚本前增加相同环境，binary 改为
build/ocr-asan/tests/x11_visual_marks_test，并使用独立 output 目录。文字 Canvas
使用 [既有脚本](../../tests/manual/visual_canvas_acceptance.py)、ocr-debug binary 和
相同来源资源。8 个 C++ 文件格式检查、9 份文档的本地链接/标题/fence 检查和 git diff --check
通过；依赖源码工作树干净，暂存区为空。

原生构建初跑遇同工作树并行 UI 改动的声明/定义顺序错误；该改动修正后上述最终
构建与测试通过。本增量没有编辑 UI 文件。补充 headless Debug/Release 的最终
构建和全量各 59/59 通过，以上均使用最终源代码构建。

## 剩余门禁

M7-07 保持 In Progress。产品模板资源、几何候选模板匹配与无闭合轮廓图标待装配；
在线模型读取编号图、用户宿主 Canvas、高分辨率预采集、混合 DPI 与 Windows/
Wayland 由维护者在目标环境补证。OCR 权重分发许可沿既有供应链审计，不扩大声明。
上一增量完整权重 TSAN 0/5 的期限限制仍开放，见
[记录](visual-text-canvas-20261010.md)；本轮定向 TSAN 不代替完整权重业务链路。
在原 5 秒观察/30 秒 owner 预算内完成目标环境复验后才可关闭该项，不扩大期限。
没有新增依赖缺口或依赖源码/pin 变更；没有 commit、push 或 MR，保留既有在途和无关改动。
