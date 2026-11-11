# 第七阶段：分层工作图与分辨率对照

> 状态：Completed（仅本机工作图增量；整体M9门禁仍开放）
> 日期：2026-10-28
> 负责人：Mirage维护者
> 所属：[三层计划](../plans/m9-layered-perception-delivery.md) M9-25/27/28/33
> 决策：[DEC-127](../decisions/DEC-127-layer-specific-image-resolution.md)
> 环境：本机Linux/x86_64，GCC13.3、CMake3.28；HEAD d30502a，既有未提交工作树
> 实验：async recognition默认关闭，原有refresh/tracking配置及生产默认未变

## 改动与边界

原采样使用最长边1600的识别图生成签名/跟踪；最终64²签名不能代表前段仅处理64²。
现在先用Mirador公开resize_area降到最长边320，再用convert_color转Gray8，变化
检测、ObjectTracker和几何候选统一使用此工作图。区域签名裁剪也在该图完成。
CV工作图只在同步步骤内存活，模板仍由Mirador按既有预算拥有；无新线程/队列。

识别框先保守映射到CV坐标才种模板；任一边不足8CV像素的模板排除，不确认身份。
输出变化框、跟踪框和候选按边界floor/ceil回映至识别图，再沿既有source映射回桌面。
原尺寸变化撤销基线，即使两个尺寸缩到同样250×125。非法框不能被clamp为有效模板。
源时间、序号、generation、取消、完整识别年龄及unknown投影继续使用原契约。

工作图彩色中间缓冲≤409600字节、灰度≤102400字节、局部灰度crop≤102400字节；
签名/模板、两份识别像素槽、后端预处理与模型缓存另计。1600×900例子的CV图为
320×180、57600字节，相对5,760,000字节BGRA识别图少99%；这只比较工作图，
不是整个进程内存下降99%。缩放仍要读取输入像素，预处理成本及全链路CPU待配对。

OCR/YOLO不放大CV图，继续从识别图或其ROI独立准备输入；已验证模型参数保持
OCR检测侧960、行高48/行宽至多640、YOLO640。小于各层上限的图不强行再缩小。
本次只改独立采样owner；旧Host低频完整识别的缓存算法未全部迁移双分辨率，
其后续契约/平台验证由维护者在M9-33推进，不宣称所有旧路径已迁移。

## 真实权重与输入质量

使用已有校验资源，无下载、主桌面采集、键鼠动作或LLM请求：
PP-OCRv5-mobile/OmniParser，manifest SHA256
`ec703acaba9325c40d4e34c5ef542df63941481f9af4a6b2339aa5aee8545097`。
字体`/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`，hash与各样本、实际binary
hash在执行JSON。以下路径位于Git忽略的`out/layered-resolution-20261028/`。

| 重放 | 结果 | 实际行为 |
| --- | --- | --- |
| `release-640/report.json` | 12/12 | 640×360→320×180；文字变化OCR1/YOLO0，输出Status: SAVED |
| `release-1600/report.json` | 12/12 | 1600×900→320×180；文字变化OCR1/YOLO0，输出Status: SAVED |

两例均验证完整OCR/YOLO、实际CV尺寸/灰度字节、两段各20次静态零后端增量、自动
文字路由及实际内容、显式OCR/检测ROI、年龄完整巡检、空白负样本、零采集/动作引用。
20次为紧循环，不是5/10Hz或视频观看验收；每次识别耗时只是单次实测。

最终`ocr-resolution-matrix-current.json`及`quality-<上限>/report.json`独立生成英文/
中文/16px小字/中英2×图，按BOX算法缩测试
输入及GT（不是产品自动调参），用原PP-OCR模型检查精确文字和位置：

| 输入最长边上限 | 精确文字与位置 | 失败 |
| --- | --- | --- |
| 参考图（0，不再缩小） | 4/4 | 无 |
| 640 | 3/4 | 中英混排“提交 Submit”变为“提交Submit”，置信度0.992877 |
| 480 | 3/4 | 同一空格丢失，置信度0.993882 |
| 320 | 4/4 | 本组未失败；不能据此推断普遍优于640/480 |

全部保留原失败退出码和文本，不放宽空格/位置门禁；降低分辨率与质量非单调，
置信分也不能证明文本保真。因此本轮不把OCR全局输入机械压到640或320；更小
模型尺寸/动态精度策略需要更多小字/密集表单/图标/阴性样本和召回验证。

## 暴露的问题与修正

初次新增测试误用ImageBuffer非const data()，构建明确失败后改用view()读取。
随后新跟踪fixture没有先建立相邻采样基线，第一次sample按原契约给global并清空
模板；断言后读取空tracks导致测试SEGV。补真实bootstrap顺序和失败路径保护，
生产契约没有因此放宽。对应早期日志在`/tmp/mirage-resolution-debug-*`。

第一轮五配置专项各8/8后，扩大真实重放到1600×900得到10/11：固定8个识别像素
无法包住CV块的量化边缘，误选组合能力且裁掉Status前缀，出现SAVEI。失败原始
记录`final-1600/`保留。把容差按每轴4个CV像素回映（至少8源像素）后选对OCR，
但固定裁剪padding仍导致分段/重复标点，见`fixed-1600/`；将同一context同时用于
ROI裁剪，得到`verified-1600/`完整Status: SAVED。邻框/混合/不完整hint仍组合，
工作ROI仍≤全帧1/4；容差不是新像素的语义证明。加入1600合成回归和实际SAVED门禁。
最终整数倍率版本再次重跑640/1600得到`release-640/`、`release-1600/`，均12/12；
`real-model-matrix-current.json`保留退出码；早先verified结果作为历史版本保留。

大图2048×512 TSAN曾单轮3/3通过；与真实模型重放同时复验时，resize后等待历史
事件失败，退出8，保留`/tmp/mirage-resolution-tsan-large-final-test.log`。当时其他
模型worker有CPU负载，但不能据此确定根因。新增resize诊断，不扩大100ms CV预算、
3秒识别期限、6秒fixture等待或删除原大尺寸入口。

随后单独运行仍在第2轮失败，诊断为callbacks=3/captures=44/cv=3、40次cv_failed，
没有TSAN数据竞争报告；日志`/tmp/mirage-resolution-tsan-isolated-test.log`保留，不能
归因为其他模型负载。按上限320优先取整数缩小倍率，将1024×512从320×160改为
256×128（1001×503取250×125），不为填满预算增加非整数重采样；新增准确尺寸
和非整数坐标/原布局屏障断言。CV失败现透出`cv_failed:<稳定错误名>`，仍受原预算。
该选择减少工作像素及fractional overlap，但单凭前后测试不能证明先前失败的精确
耗时根因。最终独立大图连续3轮通过：14.52s/9.77s/14.72s，总39.12s，退出0；
日志`/tmp/mirage-resolution-tsan-integral-test.log`。不扩大为有负载下的实时性保证。

## 复现

```sh
cmake --build build/native-release --target layered_perception_acceptance ppocr_pipeline_test -j2
/usr/bin/python3 tests/manual/layered_perception_acceptance.py \
  --binary build/native-release/tests/layered_perception_acceptance \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \
  --source-width 1600 --output out/layered-resolution-20261028/reproduce-1600
/usr/bin/python3 tests/manual/ppocr_quality_acceptance.py \
  --binary build/native-release/tests/ppocr_pipeline_test \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \
  --input-max-side 640 --output out/layered-resolution-20261028/reproduce-ocr-640
MIRAGE_TEST_LARGE_REPLAY=1 setarch x86_64 -R ctest --test-dir build/tsan \
  -R '^layered_perception_test$' --repeat until-fail:3 --output-on-failure
```

第二条质量命令预期能复现非零退出，不能标为“成功”。专项CTest选择六业务目标
layered_perception、continuous_recognition、visual_session、continuous_perception_consumer、
ppocr_pipeline、visual_object_tracking，加setup/cleanup为8测试；TSAN使用setarch。

## 未完成门禁

5/10Hz长任务、配对CPU/RSS/漏报、Portal/Windows、视频结束/字幕/音频理解和生产
默认切换未执行；负责人Mirage维护者，补跑条件为M9-33指定目标平台/模型/语料及
负载，按预登记阈值配对验收。少量静态文本不能保证所有图像“无精度损失”。

## 最终源码与专项收口

冻结7份相关实现/测试源码，按排序路径/NUL/内容/NUL计算SHA256：
`b0827c3e50306cfe6f0e8e17f5bd2589acef2feec2e30cfafa905b5f981219e2`。
清单在`out/layered-resolution-20261028/source.json`；前一版本另存
`source-before-large-context.json`，不把早期通过套到最终源码。

| 最终配置 | 构建 | 专项CTest | CTest秒 |
| --- | --- | --- | --- |
| Debug | 0 | 8/8 | 18.32 |
| ASAN | 0 | 8/8 | 19.16 |
| UBSAN | 0 | 8/8 | 19.02 |
| TSAN | 0 | 8/8 | 17.63 |
| native-release | 0 | 8/8 | 18.10 |

日志`/tmp/mirage-resolution-<配置>-verified-{build,test}.log`；六业务目标和两fixture
见上文。不以首轮final日志取代后续context、整数倍率和失败诊断修改后的verified
矩阵。单独大图三轮与常规矩阵分开，未让其他模型/测试并行占用其执行窗口。

本次5份C++格式dry-run通过、两份Python AST通过；公开边界67头/0违规，2处既有
DEC-096 seam保持。`git diff --check`通过；不宣称整个仓库format全部通过（上一轮
已记录无关文件的既有失败）。未改pinned依赖、模型manifest、线程/调度设施或生产默认。

最终release-640/release-1600实际执行binary hash已与当前手工验证binary核对，
最终文本对照重复得到4/4、3/4、3/4、4/4（640/480仍保留非零），未放宽门禁。
`final-verification.json`记录最终源码、两验证binary、manifest hash及矩阵/文档核验。
native服务、前端和mirador_visual_review重链接退出0，日志
`/tmp/mirage-resolution-products-verified-build.log`；未安装或重启用户服务。
