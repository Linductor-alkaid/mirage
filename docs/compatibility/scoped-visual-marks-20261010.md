# M7-07 同源编号图与视觉点击验证

> 状态：Verified（Linux 首增量；M7-07 保持 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-058](../decisions/DEC-058-scoped-visual-marks.md)

## 范围与依赖

Linux x86_64 / GCC 13.3，Mira c75378b、Mirador ae8cf87、其传递 kairo ef821dc；没有
依赖修改。Runtime 的真实 OCR 装配提供 mark 与 registry scope/ref 同锁回调；模型
支持图片时使用既有 PNG artifact/source/supplier。无真实管线或图片能力不暴露编号图；
不允许修改、无窗口/完整输入时不暴露视觉点击。默认无模型下载。

本轮实现：worker 内 Mirador SoM/crop、全局原点与裁剪的显式变换、编号/同源图与
快照 scope 映射、完整可见区域选择、点击前新采集像素/几何/前台校验、单写者输入与
动作后新观察。模板 enrollment/probe 的既有结果可进入相同编号路径，未新增模板缓存。

## 验证方法

`visual_marks_test` 使用真实 Mirador session/SoM/crop，模型无关 OCR/Detector fixture，
无 AccessibilityProvider。验证负原点、裁剪不改编号、画外/部分目标拒绝、同名 @v1
换代、像素/显示器/窗口/前台变化、授权拒绝与等待中的 takeover、过期/取消、capture
期间竞态、异常/释放失败/执行中取消后释放、预算、worker 拒绝与关闭。模板 probe
验证融合 identity 和编号定位；Mira ModelLayer loop 验证图片输入→编号选择→点击→
固定窗口后条件、failed canonical result 与模型假报完成的宿主门禁。

`x11_visual_marks_test` 创建独立 Xvfb 自绘矩形窗口，没有 a11y 树。真实屏幕采集与
XTest 完整输入收到局部 (50,35) 的左键 press/release，应用处理事件后改标题为 Clicked，
重新调用只读 verify 并查询窗口，固定标题条件得到新证据。候选由 fixture 提供，这证明
无语义输入和后观察的机械闭环，不证明在线模型识别或真实 OCR 定位该矩形的质量。

opt-in `ppocr_pipeline_test` 追加真实 ncnn 检测+识别后的同源 SoM/编号/像素分离检查；
微型网络验证装配，真实 OCR 质量继续引用[正式 pin 复验](real-ocr-correctness-20261009.md)。

## 实际结果

| 门禁 | 结果 | 范围 |
| --- | --- | --- |
| native-debug build / ctest | 59/59，0 failure | 含既有 provider、模型、生命周期与 UI 回归 |
| native-release build / ctest | 59/59，0 failure | 同一版本与契约 |
| ocr-debug build / ctest | 58/58，0 failure | 真实 ncnn 组合装配与新增 SoM；原生 UI 关闭 |
| opt-in ASAN+UBSAN | 4/4，0 failure | visual_marks、x11_visual_marks、ppocr_pipeline、visual_session；泄漏检查启用 |
| opt-in TSAN | 4/4，0 failure | 同四项；测试进程关闭 ASLR；无 race 报告 |
| 格式 / diff / 文档 | 0 违例 | 本轮 21 个 C++ 文件，9 份文档 227 条本地链接；未修改 pinned 源码 |

回归曾发现 raw capture 失败时提前退休图片改变旧 supplier 契约；已将退休处理限定到
mark 刷新边界，再补跑以上全部门禁。原截图契约与用例保持，失败 mark 清除本轮旧
编号图，成功 screenshot 替换本轮编号图。

## 复现

```bash
cmake --preset native-debug
cmake --build --preset native-debug -j 3
ctest --preset native-debug -j 3
cmake --preset native-release
cmake --build --preset native-release -j 3
ctest --preset native-release -j 3
cmake --preset ocr-debug
cmake --build --preset ocr-debug -j 3
ctest --preset ocr-debug -j 3
```

sanitizer 复用 M7-06 的 `build/ocr-asan` / `build/ocr-tsan`（scalar ncnn；前者联合
ASAN/UBSAN，后者 TSAN），构建并运行 visual_marks、x11_visual_marks、ppocr_pipeline
与 visual_session 四个测试；不据 scalar 配置宣称生产性能。TSAN 普通启动因 shadow
地址映射冲突未进入测试，复用既有 `setarch x86_64 -R` 对测试进程关闭 ASLR 后运行。

```bash
cmake --build build/ocr-asan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test visual_session_test -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|visual_session_test)$' --output-on-failure
cmake --build build/ocr-tsan --target visual_marks_test x11_visual_marks_test ppocr_pipeline_test visual_session_test -j 2
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -R '^(visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|visual_session_test)$' --output-on-failure
```

## 剩余门禁

M7-07 保持 In Progress。后续由维护者补在线模型读取真实 OCR 编号图、真实宿主 Canvas
任务；固定 OCR 条件与实际 Xvfb Canvas 已由 [第二增量](visual-text-canvas-20261010.md)
补验。mark 的 ROI 在全屏采集后裁剪，源图仍受 8 MiB 预算限制，
高分辨率屏幕的局部采集/分析待补；无文字图标的受控模板配置/几何候选还未进入产品装配。宿主混合
DPI、多显示器缩放、Windows/Wayland 待对应环境补跑；本轮只验证负原点和明确 1:1
像素映射，缩放不符会拒绝。首轮仅用窗口/语义条件；第二增量增加固定 OCR 文本条件。

五秒有效期与严格原图像素相同会拒绝慢模型和动画页面；重新观察后再选择，未放宽安全
边界或声明实时性。图片不保存为仓库证据，fixture 无用户数据。所有新增变更保留在当前
M7 工作树，未提交或推送。
