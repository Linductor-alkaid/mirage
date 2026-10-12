# M7-07 4K 显示器上的窗口局部预采集

> 状态：Verified（Linux 受控 4K 小窗口；M7-07 整体 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)、[DEC-062](../decisions/DEC-062-visual-capture-scope.md)

## 实现与范围

显式 OCR manifest 可设 roi_capture=true（缺省 false，非布尔值启动拒绝，进入
canonical revision）。mark 的显式 crop，包括窗口 capture_scope 的默认范围，
在 ScreenProvider::capture_roi 采集前生效。用同一 ROI 像素进行 OCR、geometry、
template/fusion/SoM，Mirador translation 将 bounds/anchor 映射到全局；编号只
属于该次分析与 scope。mark 1.2.0 参数/结果字段不变，说明编号属于当前分析。
未开启时保留整屏分析后裁剪，无 crop 仍整屏，不扩大预算或自动缩放。

EnvironmentVisualRefresh::capture_roi 明确 frame 的像素范围，display_geometry
保持物理显示器布局，image_geometry 为供给图范围。mark 核验三者和精确帧尺寸，
click 重采同一 ROI 逐像素比较；窗口、显示器、前台、scope/epoch、年龄、租约与
释放/后条件门禁均保持。局部范围外的变化不退休局部像素，范围内变化拒绝输入。
越界/超预算、provider 不支持/失败都不回退整屏。

管线显式 refresh_region 用于 owner 已确定范围的 OCR 后观察，返回全局坐标与
完整 display identity；普通 refresh 不变。宿主 harness 在局部模式为固定公共
Canvas 窗口装配该回调，没有模型自行决定后条件采集范围的入口。所有分析仍由
原 serial worker/future 承载，没有新任务路径、线程、队列、decoder 或依赖 pin。
三十秒 owner、五秒观察/图/引用、8 MiB capture、4 MiB geometry gray/work、128
regions 和 16 KiB 元数据预算不变。

## 环境与测试方法

Ubuntu 24.04 / Linux 7.0.0-34 x86_64、GCC 13.3.0、CMake 3.28.3、C++20。
工作树基于 c786df7 / feat/m7-conversation-desktop-tools，含既有 M7 与第六增量，
未提交。Mira c75378b、Mirador ae8cf87、kairo ef821dc、eui-neo a7e625f、ncnn e54f7b1
不变；权重与字体沿[资源审计](../supply-chain/real-ocr-resources-20261009.md)。

[局部驱动](../../tests/manual/visual_regional_acceptance.py)使用 3840×2160 私有 Xvfb，
公共 Canvas 为 480×180，原点 (1000,700)，无 a11y。首先要求整屏 33,177,600 字节
在默认 8 MiB capture 预算被拒，再走窗口 ROI（345,600 字节）；程序在 mark 和每次
OCR 回调核验 frame/marked frame 为 480×180、capture_roi 非空。实际
Screen/XTest/PP-OCR/Mirador geometry/index/fusion/SoM 与 PNG supplier 复用既有
路径。三个驱动共 12 例：文字四正例/不重绘、几何左右/不重绘、模板双身份/交换/
不重绘。脚本选号证明工具装配，在线模型或用户宿主另列证据。

```bash
python3 tests/manual/visual_regional_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-regional-20261010
```

单测 [visual_marks_test](../../tests/integration/visual_marks_test.cpp)检查 4K 负原点
显示器上的局部 mark/click（零整屏 capture）、同范围新采样、ROI 像素变化、物理
显示器/窗口变化、帧尺寸差异、provider 失败、越界/预算、取消/期限、关闭与默认
兼容；[ppocr_pipeline_test](../../tests/integration/ppocr_pipeline_test.cpp)覆盖四种
坏 roi_capture 类型且在资源 staging/像素采集前拒绝。已有 scope/权限/输入释放与
业务终态失败测试不变。验收入口一并修正非默认原点的私有 oracle 本地坐标，以及
POS 临时字符串 c_str 的寿命；不修改产品平台聚焦行为。

最终产品代码的 native-debug / native-release 各 60/60、ocr-debug 59/59；
ASAN/UBSAN 与 TSAN 定向各 8/8，0 failure，无 skip。定向八项是 visual_marks、
x11_visual_marks、ppocr_pipeline、desktop_image_supply、post_action_verification、
visual_text_verification、visual_observation_loop 和 visual_observation_e2e。
完整回归后只给 harness 增加失败工具回执与耗时输出；五个配置重新编译该 target，
x11_visual_marks_test 各 1/1。未改变产品或验收成功门禁。

实际权重正常与 ASAN/UBSAN 局部场景各 12/12：文字 5、几何 3、模板 4。每例实际
按下/释放、一个 PNG 输入、backend 计数推进至 4；全部正例 click/verify 与终态新
OCR 通过固定文本条件，三组不重绘负例均 condition_not_matched / unverified、
宿主 failed，不能以派发成功代替业务结果。默认 roi_capture=false 的 520×220
整屏分析路径另跑模板 4/4、文字 5/5，未改变原编号/后观察契约。

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug -j3
cmake --build --preset native-release -j3
ctest --preset native-release -j3
cmake --build --preset ocr-debug -j3
ctest --preset ocr-debug -j3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/manual/visual_regional_acceptance.py --binary build/ocr-asan/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-regional-asan-20261010
```

ocr-asan / ocr-tsan 是以仓库 asan / tsan 预设另指定 binary directory、PPOCR=ON
配置的 opt-in 构建，ASAN 同时加 undefined sanitizer。定向 ctest 使用上述八个
名称；TSAN 用 TSAN_OPTIONS=halt_on_error=1 与 setarch x86_64 -R 规避本机地址
映射限制，没有修改系统 ASLR。精确 flags 分别为 -fsanitize=address,undefined / -fsanitize=thread，均加
-fno-omit-frame-pointer。可重建定向配置（命令中的正则对应上述八项）：

```bash
cmake --preset asan -B build/ocr-asan -DMIRAGE_WITH_PPOCR=ON -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/ocr-asan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test desktop_image_supply_test post_action_verification_test visual_text_verification_test visual_observation_loop_test visual_observation_e2e_test -j2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan --output-on-failure -R '^(visual_marks|x11_visual_marks|ppocr_pipeline|desktop_image_supply|post_action_verification|visual_text_verification|visual_observation_loop|visual_observation_e2e)_test$'
cmake --preset tsan -B build/ocr-tsan -DMIRAGE_WITH_PPOCR=ON
cmake --build build/ocr-tsan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test desktop_image_supply_test post_action_verification_test visual_text_verification_test visual_observation_loop_test visual_observation_e2e_test -j2
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan --output-on-failure -R '^(visual_marks|x11_visual_marks|ppocr_pipeline|desktop_image_supply|post_action_verification|visual_text_verification|visual_observation_loop|visual_observation_e2e)_test$'
```

配置和数据摘要见
[公共回执归档](data/regional-visual-capture-20261010.json)。

## 在线与完整权重 TSAN 补证

实际 MiniMax-M3 在相同 4K ROI 模式执行三例，本轮 0/3、选号 0/3：三例均
endpoint dns resolution exceeded its deadline，错误沿模型调用回流，宿主 failed、
最终回答为空，没有 click/verify、按下或释放。第一例已经完成局部 mark（480×180、
原点 (1000,700)、两个候选、一个图片供给项），下一模型请求 DNS 失败；另两例在
首个模型调用失败，未采集供给。原始失败记录保留，没有放宽期限或自动重试。
第五增量的 520×220 在线 3/3 仍有效，但不能替代本轮 4K 在线取证。

```bash
MIRAGE_CANVAS_ROI=1 MIRAGE_CANVAS_POS=1000,700 python3 tests/manual/visual_online_acceptance.py --provider /home/linductor/mira/docs/model_provider/minimax --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-m7-regional-20261010/regional-manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-regional-online-20261010
TSAN_OPTIONS=halt_on_error=1 MIRAGE_CANVAS_ROI=1 MIRAGE_CANVAS_POS=1000,700 setarch x86_64 -R python3 tests/manual/visual_geometry_acceptance.py --binary build/ocr-tsan/tests/x11_visual_marks_test --manifest /tmp/mirage-m7-regional-20261010/regional-manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-regional-tsan-diagnostic-20261010
```

完整权重 TSAN 首轮几何 1/3，两个正例未达到宿主 verified 门禁，不重绘负例
按预期失败；未出现 ThreadSanitizer race warning。为保留具体工具失败原因，验收
harness 在正例失败时输出耗时和 click/verify 的公共回执，单独复验仍 1/3。
两个正例的工具回执包含 cancelled_or_expired，实际输入 input_released=true；
左目标只读 verify 曾为 condition_matched，但终态新观察门禁仍失败。两例 owner
总耗时分别 17,174 / 18,075 ms、backend 计数 4；不能把一次 verified 或无竞态
报告等同整个业务通过。两轮事实均保留在数据归档。
该业务门禁与定向 8/8 分开记录，不宣称完整权重 TSAN 已通过。

负责人 Mirage 维护者：网络 DNS 恢复后按原命令/五秒引用与观察、三十秒 owner
期限补跑 4K 在线三例；完整权重 TSAN 在原预算目标环境补跑。运行窗口与样本均
是公共绘图，凭据只经子进程环境与 SecretRef，未将密钥、模型推理或用户桌面图
写入归档。

## 剩余门禁

本记录不证明整屏 4K、任意 DPI/缩放或跨屏窗口支持。ROI 必须完整位于配置所选
显示器；业务正确性只对应预先固定局部文本条件。用户宿主物理取焦和 Xorg 证据
仍沿[第六增量 runbook](user-host-visual-canvas-20261010.md)，不改变 mutter 的
焦点门禁。自由轮廓、跨主题/尺度、Windows/原生 Wayland 和完整权重 TSAN 期限
复跑仍开放；维护者在对应环境/资源/权限具备时补跑，不能用受控 Xvfb 代替。
外部权重分发许可沿资源审计。本轮未提交/推送、未修改 pinned 依赖或新登依赖缺口。
