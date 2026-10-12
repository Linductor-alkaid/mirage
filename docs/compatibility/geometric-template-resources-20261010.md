# M7-07 本地模板、几何候选与编号点击

> 状态：Verified（Linux 受控本地模板范围；M7-07 整体 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-061](../decisions/DEC-061-geometric-template-resources.md)

## 环境与版本

Linux x86_64（Ubuntu 24.04，kernel 7.0.0-34）、GCC 13.3.0、CMake 3.28.3、C++20。
工作树基于 c786df7 / feat/m7-conversation-desktop-tools，含既有 M7 与并行 UI 变更，
本增量未提交。Mira c75378b、Mirador ae8cf87（v0.3.0-121）、kairo ef821dc
（v0.6.0）、eui-neo a7e625f、opt-in ncnn e54f7b1 未改 pin 或源码。完整摘要、
公共图案样本和动作回执见[数据归档](data/geometric-template-resources-20261010.json)。
外部模型 manifest 与字体摘要沿归档列明；模板清单路径参与 revision，所以普通
与 ASAN 的私有输出目录不同会产生不同 revision，不要求摘要跨配置相同。

## 装配与边界

模板通过显式审核的本地清单装配；尺寸/布局、ID、资源 SHA-256、字节数和容量都
在启动时检查。最多 16 个 BGRA8 patch、4 MiB 像素；不增加图片 decoder、模型或
依赖。首次模板 mark 请求在已有 kairo serial worker 注册资产，以 Mirador 索引
匹配已有闭合提案的 tight bounds，不扫描整帧、不自研匹配或 NMS。普通 OCR 和
固定文本后条件不启动模板。配置进入既有 canonical revision，变更需重启 owner。

0.95 阈值、覆盖整个库的候选上限与所有返回层级的 runner-up 检查维持明确匹配。
同 patch 多个强命中、融合后不同身份/位置命中、融合扩大匹配框时，ID 为空，保留
视觉/几何候选。没有把容器中心作为模板 anchor，也没有把缓存身份作为可点击语义。
原始模板命中仍是 session 诊断事实；发布身份只来自本次分析的有效 evidence。

mark 1.1.0 为每个编号增加 template_id，无匹配为空；OCR/模板混合时保留真实 OCR
文本来源和独立模板 ID。ID 不能代替 scope/ref/编号执行输入；既有权限、epoch、
新截图逐像素核对、固定窗口、释放/围栏及新观察业务条件继续约束视觉点击。
格式、示例与资源来源见[资源说明](../supply-chain/visual-template-resources-20261010.md)。

## 实际 Canvas

[脚本](../../tests/manual/visual_template_acceptance.py)在私有 Xvfb（520×220，窗口
480×180、原点 (20,20)、标题 Canvas 固定、无 a11y）绘制两个无文字按钮：线条和
交叉图案。离线模板是相同公共绘图的 72×72 patch，包含轮廓一像素边界；采集由
实际 ScreenProvider 完成，不用线上画面自动登记或注入匹配框。PP-OCR/Mirador
geometry/index/fusion/SoM/PNG supplier 都是实际路径，Mira provider 使用 ID 选择
对应编号。实际 XTest 按下/释放后应用绘制 Task saved，经只读 verify 与终态新
OCR 核验。脚本选择仅证明工具装配，在线模型视觉推理未验。

| 样本 | 期望 |
| --- | --- |
| line | line-icon 编号实际点击，新文本 verified |
| cross | cross-icon 编号实际点击，新文本 verified |
| swapped | 图标交换位置，仍按 cross-icon 选择新的编号/位置 |
| unchanged | 已点击但应用不重绘，unverified、宿主 failed、成功回答被抑制 |

普通与 ASAN/UBSAN（detect_leaks=1、halt_on_error=1）实际权重场景各 4/4。每例一个实际 PNG 输入，完整
按下/释放；backend 计数仍 1→2→3→4，身份/编号来自当次 scope，固定文字条件在
派发前声明。窗口标题不充当业务后条件。

```bash
python3 tests/manual/visual_template_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-template-20261010
```

## 回归与负向

[编号测试](../../tests/integration/visual_marks_test.cpp)用真实几何/index 检查双模板、
位置交换、负原点、相同外观不同 ID 的歧义、混合 OCR 身份保留、融合冲突/扩大框
拒绝身份、普通 OCR 无模板、坏资产/容量、候选预算、不可变模板库、注册中取消与
恢复、超时、fingerprint 失败、关闭后拒绝。[清单测试](../../tests/integration/ppocr_pipeline_test.cpp)
覆盖 15 类坏配置/资源，在 Screen 采集前拒绝。
[索引测试](../../tests/integration/visual_template_index_test.cpp)补候选截断拒绝，保留
既有确切/噪声/弱命中、容量与 eviction/replacement 验证。既有视觉点击/后条件
覆盖旧图、裁切、权限、窗口变化、不确定输入、只读重验与虚假成功。

坏配置检查另覆盖极大 thumb_side：先验证范围再计算 index 容量，避免非法参数算术
溢出。既有无模板几何 Canvas 回归 3/3，四类文字与不重绘负例 5/5，普通 OCR 路径
未启动几何/模板。构建与定向 sanitizer 最终结果：

| 组合 | 实际结果 |
| --- | --- |
| native-debug 完整构建/测试 | 60/60 |
| native-release 完整构建/测试 | 60/60 |
| ocr-debug 完整构建/测试 | 59/59 |
| ASAN/UBSAN 定向（实际几何/index，合成 OCR） | 8/8 |
| TSAN 定向（同上，含取消/恢复/关闭） | 8/8 |
| 实际权重模板 Canvas，普通 / ASAN/UBSAN | 4/4、4/4 |
| 原几何 / 文字 Canvas 回归 | 3/3、5/5 |

本增量 C++ 文件 clang-format --dry-run --Werror 检查通过，12 份相关文档的本地
链接/代码围栏和公共数据结构检查通过；git diff --check 通过。

复现完整构建/测试：

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug -j3
cmake --build --preset native-release -j3
ctest --preset native-release -j3
cmake --build --preset ocr-debug -j3
ctest --preset ocr-debug -j3
```

定向集合为 visual_marks_test、x11_visual_marks_test、ppocr_pipeline_test、
post_action_verification_test、visual_text_verification_test、visual_session_test、
visual_cache_geometry_test、visual_template_index_test。ocr-asan/ocr-tsan 是已有本地
构建目录，均启用 MIRAGE_WITH_PPOCR；其他开关沿各 sanitizer 基础预设。

```bash
cmake --build build/ocr-asan -j2 --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test post_action_verification_test visual_text_verification_test visual_session_test visual_cache_geometry_test visual_template_index_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -j1 --output-on-failure -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|post_action_verification_test|visual_text_verification_test|visual_session_test|visual_cache_geometry_test|visual_template_index_test)$'
cmake --build build/ocr-tsan -j2 --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test post_action_verification_test visual_text_verification_test visual_session_test visual_cache_geometry_test visual_template_index_test
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -j1 --output-on-failure -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|post_action_verification_test|visual_text_verification_test|visual_session_test|visual_cache_geometry_test|visual_template_index_test)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/manual/visual_template_acceptance.py --binary build/ocr-asan/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-template-asan-20261010
python3 tests/manual/visual_geometry_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-template-geometry-regression-20261010
python3 tests/manual/visual_canvas_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-template-text-regression-20261010
```

setarch 仅沿既有进程级 TSAN 映射 workaround，未改系统 ASLR。单元/平台定向集合
含真实几何/index 与合成 OCR/Xvfb；不能代替完整 PP-OCR 权重业务链路的 TSAN 期限
门禁。命令中的 /tmp 是当前私有验证资源目录，复跑需先按既有资源审计准备。

## 剩余门禁

M7-07 保持 In Progress：无闭合几何候选的自由轮廓图标、跨主题/尺度、在线模型
实际选号、用户宿主、高分辨率预采集、DPI 与 Windows/Wayland 各自待验。维护者
在目标环境和对应资源/授权可用后补证；不以受控 Xvfb 或脚本模型代替。
完整权重 TSAN 业务链路的期限限制仍开放，沿
[既有记录](visual-text-canvas-20261010.md)在原 5 秒观察/30 秒 owner 期限复验；本轮
定向 TSAN 不代替该门禁。外部模型分发许可沿资源审计，没有新 pin 或依赖源码修改。
本轮没有提交/推送或 MR，保留既有 M7 在途与并行 UI 改动。
