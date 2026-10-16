# YOLO 接入 Mirage 与感知修复验收（2026-10-16）

M7-23 / DEC-097；维护者要求将更新、修复和 YOLO 接入 Mirage。
Mirador 分支 pin `932d425fe114f0b5b3919cde2ef49ba5d2d5b0ac`，
[上游 PR #40](https://github.com/Linductor-alkaid/mirador/pull/40) 组合 #37/#39 与
模型预处理修复；本记录不表示 master 已合并或新版本已发布。

## 交付行为

- 原生产品启用 PPOCR 时默认编译 YOLO 支持；无 detector 清单仍运行 OCR。
- `MIRAGE_YOLO_MODEL_DIR` 指向已转换、锁定的本地模型目录后，构建生成包含 UI
  Detector 的 vision 包。权重和许可证一同复制；运行时不联网、不下载。
- 产品启动从 manifest 校验 param/bin 的大小、SHA-256，复制私有资源后装配
  Mirador YOLO backend。只在显式 mark 调用；普通 OCR、图像 OCR 不调用 Detector。
- 使用现有 VisualSessionHost 和进程 Executor owner；每次 mark 刷新检测，结果
  经现有融合、全局坐标映射、SoM 与引用注册表交付。阶段错误清空引用，不降为假成功。
- 修复停止管线后再次装配的 worker 同名拒绝：每实例使用独立 source/worker 身份；
  仍为原进程 owner 管理，stop/join 先于后端/私有资源销毁。
- 同步交付 OCR 动态行宽上限 640、几何线段 16/4 及上游细描边/变化检测/跟踪优化。
  上游新增精细变化模式可供使用，本次没有修改动作守卫或降低 tracker 判定阈值。

## 真正执行的验证

Linux x86_64 / GCC 13.3.0 / Release；单线程 ncnn、原固定 PP-OCRv5 资源与
OmniParser-v2 icon_detect（640、bilinear114、confidence=.25）。

`ScreenProvider → PpOcrPipeline::mark → OCR + YOLO + geometry → fusion → SoM → registry`
实际执行 11 个场景：两张网页、表单、代码、几何、长行、H.264 解码三帧、空白、
先前获授权的真实 Wayland 正文裁剪。GTK 在独立 Xvfb 的 (37,53) 按 1:1 显示，
Mirage 原生 X11 capture_roi 采集，逐像素一致 11/11；每张真实调用 Detector 一次。
不把导出器退出成功称为视觉质量全通过。

对最终产品融合后的表单框，6 个独立目标一对一匹配：IoU .8429–.9180；中心偏差
.5–1.0 px；6/6 编号锚点落在目标内。编号、ref、全局 bounds 与锚点一致检查全部
通过。合成负原点 (-25,53)、原点改变、非零 ROI (-80,61,32,32) 验证具体期望
坐标；合成 OCR 使用负图隔离 detector 坐标，真实语料另验 OCR/Detector 混合融合。

已查看实际表单、Python 网页、几何、视频和宿主裁剪的产品标记图：下载按钮可标注，
六个表单控件保持独立；视频目标位置随帧更新，四个矩形均覆盖。局限仍存在：右侧
状态面板被模型误提议，融合后吸收面板内多行 OCR；Python 插图与空白背景存在额外
几何候选；多语言/长正文仍有识别错误。禁用 Cancel 被标注不证明它可交互。

所有 11 次导出使用 5 秒共享期限，包含独立进程模型装载的 wall 为 1.845–4.788s。
这是本批数据和负载下的结果，不是普遍五秒保证。原生 Wayland 宿主图在 X11 回放；
本批没有原生 Wayland 输入、真实多屏/混合 DPI 或 Windows 点击闭环的验收。

[机器可读证据](data/yolo-mirage-integration-20261016.json) 只保存计数、坐标指标和
耗时；截图、权重、完整 OCR 及 tensor 不进 Git。本次验证资源包生成和目录布局，未构建/安装 DEB 或替换运行中的产品。
本地画廊为
`build/yolo-evidence/review.html`。

## 自动化与复现

- Release 相关 CTest 10/10（8 个测试 + 2 个状态 fixture），覆盖资源包、篡改拒绝、
  实际 runtime 合成模型、负原点/ROI、引用、普通 OCR 不调用 detector、强制刷新、
  backend 输出错误、预取消/期限、停机和同进程重新装配。
- 同一 Debug 构建同时启用 ASAN/UBSAN，`UBSAN_OPTIONS=halt_on_error=1`，相关
  CTest 7/7（5 个测试 + 2 个 fixture），无 sanitizer 诊断。ncnn CPU dispatch/
  AVX variants 关闭；此构建只验证内存/UB，不用于性能比较。
- 无模型配置 `MIRAGE_WITH_PPOCR=OFF/MIRAGE_WITH_YOLO=OFF` 的 integration 目标
  构建通过，默认最小图不引入模型 runtime。公开头边界检查 60 头、0 违例；修改的
  C++ clang-format 通过，Python 资源包负例与导出报告检查通过。
- Mirador 独立真实推理对照、默认预处理等价、输入像素误差、原始目标坐标和
  Release/严格 sanitizer 58/58 见其 PR #40 的 benchmark 文档。旧合成大核/大
  stride 触发 ncnn 成本启发式 signed overflow，改用等价池化+1×1 常量 fixture；
  没有抑制 sanitizer，也未修改 ncnn。该 runtime 组合限制仍记录在上游。

```sh
cmake --preset native-release -DMIRAGE_WITH_YOLO=ON \
  -DMIRAGE_YOLO_MODEL_DIR=/absolute/path/to/converted-models
cmake --build --preset native-release --target mirage-native-product
```

模型转换准备方法见 pinned Mirador `integrations/detector_yolo/eval/README.md`；
资源期望值见 `packaging/vision/yolo.lock.json`。无目录时不自动获取模型。

使用已有 corpus 时：运行 `tests/manual/mirador_review_run.py`，指定 binary、包含
Detector 的 manifest、output 和 `--results-dir integrated-all`；再运行
`tests/manual/yolo_product_review.py --root <output>`，验证坐标并生成画廊。
CI、PR 评审和跨平台验收状态须分别记录，不由本机结果推断。

PR 交付：[Mirage #98](https://github.com/Linductor-alkaid/mirage/pull/98)，目标分支
`feat/m10-basic-agent-tools`。提交时 Mirage 8 项 CI 在运行，上游 #40 已有 13/14
成功、静态检查仍运行；未将待完成的平台验证标为通过。具体后续状态以 PR checks 为准。
