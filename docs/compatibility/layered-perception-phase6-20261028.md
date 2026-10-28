# M9-28 OCR / 检测器能力选择与局部识别验证

> 状态：Completed（仅本机raw ROI实验；M9-28完整门禁保持In Progress）
> 日期：2026-10-28
> 负责人：Mirage 维护者
> 所属：[三层视觉计划](../plans/m9-layered-perception-delivery.md) M9-28第3步
> 决策：[DEC-126](../decisions/DEC-126-selective-regional-recognition.md)
> 环境：本机 Linux 7.0.0-38-generic / x86_64；GCC 13.3.0，CMake 3.28.3
> 工作树：feat/continuous-semantic-binding，HEAD d30502a；既有pinned依赖
> 配置：Debug与ASAN/UBSAN/TSAN；native-release启用PPOCR/YOLO

## 实施范围

仅默认关闭的async实验接入区域/能力选择，配置好的YOLO加入完整后台识别。
纯文字/检测对象hint分别选OCR/检测器，未知区域组合可用能力；一个扩8像素的
整数ROI、面积≤全帧1/4，超限/不确定/完整识别年龄到期回退全帧。
这不是语义分类保证，也未改变默认串行路径的YOLO策略。

原Host新增regional_only分支，直接复用Mirador公开ROI执行；保持原串行worker、
前台优先和取消，禁止change/fusion/coordinator/覆盖/语义推进。结果最多32区域，
无entity/instance/confirmed身份，只作为原时间和原尺寸的历史区域观察。
完整签名/年龄保持；一份局部签名去重识别，文字/标签/位置输出摘要去重通知，
外围变化和完整年龄驱动全屏巡检。unknown不提供当前动作引用或确认消失。

`ContinuousImageRecognizer`实验回调增加work参数；sample增加实际执行能力/ROI
描述；feed/model dispatch增加owner接受的regional/OCR/detector成功计数。
计数不涵盖失败或丢弃前已经运行的backend尝试；原生backend_calls/detector_calls
仍是原始调用事实源。普通CV计数本身不增加事件/模型唤醒。

资源沿用两份≤10MiB像素槽和≤512KiB完成结果元数据预算；采样owner另保留
≤32个无字符串hint、一个64²局部签名和一个输出摘要。局部签名裁剪临时缓冲
≤3MiB，摘要序列化预留16KiB；模型、backend输出/预处理、原有缓存另计，未验收
全进程峰值/CPU或5–10Hz。开启tracking时弱证据优先全帧，可能减少ROI命中。

## 验证场景

- 纯文字、纯检测、未知和超ROI预算分别选能力；bootstrap/待确认/跟踪弱证据/
  全局变化/年龄巡检仍完整；无可用检测器可退化OCR，静态不因此反复重试。
- 实际Host/PerceptionSession给后端的裁剪尺寸、返回坐标、后端调用次数；局部请求
  没有snapshot/continuous/coverage，query的last_analyzed_frame/epoch/policy/
  temporal保持，analysis_age继续增长，原CV-only完整bootstrap仍有效。
- 真实capture owner→dispatcher→pipeline→journal：文字ROI不增加检测调用，静态
  不重复OCR；像素再次变化但文字/标签/位置输出相同，不增加事件游标。
- 局部失败/取消/超限不提交基线；非法ROI/坐标系、能力为空、回调返回不同scope
  明确失败；外围变化和完整年龄没有被局部成功续期。
- Native小型NCNN网络覆盖async完整OCR+YOLO、OCR-only ROI、detector-only ROI的
  实际调用数；不代表预训练模型/真实桌面质量。原有前台、迟到/代次、拒绝、3秒
  超时、取消token像素释放、停止和平台session关闭测试继续执行。

## 失败轮次与修正

新增负向测试发现raw ROI的坐标系检查不足：非oriented请求会交给Mirador返回
执行失败，而不是在Host准入时拒绝。新增显式roi_space校验；未放宽断言。
首次专项6/6通过；加入完整链路和新负向后4项中layered专项1断言失败，其余通过；
修正后专项4/4通过。最终冻结源码后的结果另列，不用中途成功替代最终门禁。

首轮矩阵并行运行时，Debug23/23与native5/5通过；ASAN的图像供给专项超过原30秒
上限，TSAN的既有2048×512迟到结果用例未满足“3秒识别期限内等3帧”的前置，
其断言后仍front()空事件导致SEGV。未观察到TSAN数据竞争报告，但不能将该轮记为
通过。独立TSAN layered专项随后3/3通过（12.52s）；给测试失败路径加诊断/空事件
保护，保留原3秒期限、6秒等待及断言，不扩大timeout或添加suppression。最终矩阵
串行执行，避免用并行运行的通过结果覆盖失败事实。负载是可能因素，首次失败的
精确调度原因未证明；本轮不据此作实时性/性能承诺。ASAN图像供给独立重跑
3/3通过（业务18.43s）。测试新增诊断首次构建遗漏iostream头，补齐后重建，
该轮失败不计入最终通过。随后代码复查补齐raw分支提前返回的缓存占用诊断，
避免默认零值误导资源判断；补充signature/result cache/coordinator实际占用断言。
该修改仅查询同一worker内现有统计，不推进状态；完整串行矩阵与最终专项分别列出。

## 复现命令

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_object_tracking|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|continuous_recognition|significance_gating|model_layer|ppocr_pipeline_test|runtime_service_test' --output-on-failure

cmake --build build/asan --target layered_perception_test continuous_recognition_test continuous_capture_test continuous_perception_consumer_test visual_object_tracking_test task_perception_test ppocr_pipeline_test runtime_service_test desktop_image_supply_test -j 2
ctest --test-dir build/asan -R '^(layered_perception_test|continuous_recognition_test|continuous_capture_test|continuous_perception_consumer_test|visual_object_tracking_test|task_perception_test|ppocr_pipeline_test|runtime_service_test|desktop_image_supply_test)$' --output-on-failure
# UBSAN同targets/正则，将build/asan替换为build/ubsan。
# TSAN同targets/正则，将build/asan替换为build/tsan，ctest前加setarch x86_64 -R。

cmake --build build/native-release --target mirage-native mirage-service ppocr_pipeline_test layered_perception_test continuous_recognition_test -j 2
ctest --test-dir build/native-release -R '^(ppocr_pipeline_test|layered_perception_test|continuous_recognition_test)$' --output-on-failure
cmake -DROOT_DIR=/home/linductor/mirage -P cmake/BoundaryCheck.cmake
```

## 最终结果

最终源码清单与摘要在`/tmp/mirage-phase6-source.json`，仅统计本阶段18份C++文件，
按路径排序，逐项追加路径/NUL/内容/NUL计算SHA256：
`3c989324f739e10438af87bf7d356d489449484c9b2b6963111db618e8ea8849`。
工作树含此前视觉及记忆治理改动，未提交、未安装/重启服务，不修改pinned依赖。
构建日志在`/tmp/mirage-phase6-{debug,asan,ubsan,tsan,native}-build.log`；最终测试
及静态门禁执行后在此追加真实结果。

## 验证分层

完整串行回归在缓存诊断补齐之前、测试失败保护之后运行（源码摘要
`91424e7762257915ae1cba65137a62403cbf2f168c0bc7a8b5086807a9006743`）：

| 门禁 | 实际结果 | 范围 |
| --- | --- | --- |
| Debug完整构建/相关回归 | 构建退出0；23/23，56.23s | 21套业务测试+隔离，不是全仓CTest |
| ASAN | 11/11，49.67s | 9套业务测试+隔离 |
| UBSAN | 11/11，48.16s | 同上 |
| TSAN | 11/11，73.34s | setarch；没有race报告或新增suppression |
| native-release | 前端/服务构建退出0；5/5，11.88s | 3套业务测试+隔离；实际小型NCNN网络 |

日志`/tmp/mirage-phase6-{debug,asan,ubsan,tsan,native-release}-verified-test.log`，
命令在上节；结构化退出码在`/tmp/mirage-phase6-verified-matrix.json`。

最终源码另补只读缓存占用查询及相应断言，重新构建5种配置的layered、原Host、
continuous consumer、dispatcher及PP-OCR专项，native前端/服务也重链接。最终专项
只针对这些直接受影响路径，结果与前面的完整矩阵分别记录；没有把修正前的
23/11项宣称为最终源码的完整重跑。命令如下：

```sh
cmake --build build/debug --target layered_perception_test continuous_perception_consumer_test visual_session_test ppocr_pipeline_test continuous_recognition_test -j 2
ctest --test-dir build/debug -R '^(ppocr_pipeline_test|layered_perception_test|continuous_recognition_test|continuous_perception_consumer_test|visual_session_test)$' --output-on-failure
# ASAN/UBSAN同命令替换构建目录；TSAN额外setarch x86_64 -R。
# native-release同专项；另构建mirage-native/mirage-service。
```

最终专项结果（当前上节18份源码摘要）：

| 配置 | 实际结果 | 范围 |
| --- | --- | --- |
| debug | 7/7，17.86s | 5套业务测试+隔离 |
| asan | 7/7，18.79s | 5套业务测试+隔离 |
| ubsan | 7/7，18.71s | 5套业务测试+隔离 |
| tsan | 7/7，20.83s | 5套业务测试+隔离 |
| native-release | 7/7，18.00s | 5套业务测试+隔离 |

最终layered专项146断言、dispatcher310断言、原Host281断言、continuous consumer
2319断言；native PP-OCR265断言，均0失败。实际NCNN fixture覆盖完整OCR+YOLO及
两种独立ROI调用；尚无预训练模型质量结论。最终无sanitizer报告或新增suppression。
日志`/tmp/mirage-phase6-{debug,asan,ubsan,tsan,native-release}-final-test.log`，
结构化结果`/tmp/mirage-phase6-final-matrix.json`。5配置专项构建与native产品重链接
退出0，分别见同前缀`-final-build.log`/`-final-relink.log`。

本轮18份C++ clang-format、git diff --check、67公开头边界（0违规，2处既有
DEC-096缝）通过。全仓格式仍在既存未修改的desktop_action_verification.cpp:490
失败；没有顺带格式化。8份同步文档本地链接/一级标题/围栏检查通过，不验远程或
页内锚点。完整矩阵/最终专项的区别及早期失败均保留，生产默认仍关闭。

## 未执行门禁

真实Portal/Windows、预训练模型召回率和裁剪上下文质量、全进程峰值/CPU、5/10Hz
统计、视频内容理解/token/完成率A-B尚未运行；负责人Mirage维护者，具备固定模型、
授权来源与目标设备后按M9-33补跑。局部身份关联/精确覆盖/任务注意力/历史对象投影
仍在M9-28/29，不将此实验等同整个三层交付完成；生产默认保持关闭。


## 2026-10-28：预训练权重能力验证

维护者进一步要求验证实际能力。本节新增真实权重/受控像素与独立X11采集验证，
不沿用前文小型NCNN网络作为质量证据。仍属M9-28/33实验，当前产品默认不变。
模型manifest为构建树`share/mirage/vision/ppocrv5-mobile-v1/manifest.json`，SHA256
`ec703acaba9325c40d4e34c5ef542df63941481f9af4a6b2339aa5aee8545097`；PP-OCRv5与
OmniParser各artifact仍由产品按manifest校验，字体和样本摘要见execution.json。

### 发现与修正

首次真实权重重放9/10通过，自动纯文字路由失败：实际OCR框[45,133,218,45]，
变化块边界到180，而文字框底边178，严格包含使READY→SAVED仍执行OCR+YOLO。
保留原失败报告`out/layered-capability-20261028/real-replay/`，没有修改门禁。
按DEC-126补充有界8像素边缘容差，要求真实交叠、其他对象框不冲突，混合框也
作为阻挡；hint超过32或不完整则组合能力。工作ROI取真实变化与原框并集再扩边。
增加薄文字框、容差外邻近变化、检测/混合框歧义和hint预算退化回归，维持未知
历史观察/原完整年龄，不改依赖或实验默认。

### 实际结果

- 同一独立字体/图像/权重重放修正后10/10通过：完整OCR+YOLO；READY文本；
  两段各20次静态采样零额外后端调用；自动OCR-only；显式OCR-only ROI识别
  SAVED且保留坐标/未确认；detector-only ROI；5.1秒真实Executor延时后完整巡检；
  空白零对象；无capture/可执行引用。detector-only在纯文字ROI返回空，是执行/
  空结果语义证据，不能宣称此ROI提供检测召回率。
- 同一文字变化的单次观察由OCR1+YOLO1、1571.88ms，变为OCR1+YOLO0、105.25ms。
  这是同机两次受控重放中的单个样本，不是统计速度提升或端到端任务完成率结论。
- 独立英文、中文、小字、中英2×四图：4/4文字精确匹配与位置包含检查通过。
- 独立Xvfb/GTK→真实X11 ScreenProvider→产品mark：表单与空白2/2通过，原像素
  与源图逐字节一致；表单13个融合区域/编号，空白零区域。6/6已标定表单目标
  IoU≥0.8、中心误差≤2px、锚点在目标内；未执行任何桌面点击。此为显式mark
  通路，异步路由由前面的受控像素/完整生产链fixture单独验证。

原始报告位于Git外`out/layered-capability-20261028/`：real-replay（失败）、
real-replay-fixed（修正）、text-quality、x11/current-product及x11/summary.json。
可视化为x11/review.html，已查看实际marked.png。manual runner及driver源码随库
保留，默认CTest不自动启动模型、桌面或浏览器。

```sh
cmake --build build/native-release --target layered_perception_acceptance ppocr_pipeline_test mirador_visual_review -j 2
# 重放完整命令见接口示例“预训练权重的受控重放”。
/usr/bin/python3 tests/manual/ppocr_quality_acceptance.py \
  --binary build/native-release/tests/ppocr_pipeline_test \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \
  --output out/layered-capability-20261028/text-quality
/usr/bin/python3 tests/manual/mirador_review_run.py \
  --output out/layered-capability-20261028/x11 \
  --binary build/native-release/tests/mirador_visual_review \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --samples form-light,blank --results-dir current-product
/usr/bin/python3 tests/manual/yolo_product_review.py \
  --root out/layered-capability-20261028/x11 --results-dir current-product --expect-samples 2
```

X11源图从既有`out/yolo-package-20261023/corpus`复制form-light.png、blank.png、
video-0/1/2.png和corpus.json；全为本地公开fixture，未读取用户桌面或其他会话。
模型未安装/更新，未重启用户服务。新边缘策略的回归与sanitizer结果在下方收口；
真实Wayland/Windows、视频内容理解、5/10Hz/CPU/RSS及广域模型质量仍未验收。


### 后续回归发现的测试同步与预算条件

首轮新策略矩阵Debug/ASAN/native各7/7通过，UBSAN和TSAN各6/7：未出现UBSAN
运行时错误或TSAN数据竞争报告，但存在断言失败，不能记为通过。

UBSAN的dispatcher用例在callback开头递增captured，主线程可能在submit完成前
读到5而尚未看到第3次superseded。改为成功submit后发布submitted计数，保留
captured/首帧/末帧/背压断言；单独连续5轮通过。没有修改生产派发逻辑。

TSAN的2048×512焦点用例补诊断后复现：captures=12、cv=2、failures=9、
state=degraded/error=cv_failed，reject/cancel/timeout均0。大图CV受instrumentation
影响耗尽现有100ms预算，不能把它归成数据竞争或焦点状态机成功。功能用例改成
2048×128→1600×100，仍验证两轴1.28映射、迟到原帧、焦点/resize/取消及原期限；
原大图条件保留为压力复现入口：

```sh
MIRAGE_TEST_LARGE_REPLAY=1 setarch x86_64 -R ctest --test-dir build/tsan \
  -R '^layered_perception_test$' --output-on-failure
```

该压力条件未达本机TSAN下100ms预算，仍是开放的资源/性能边界，不因缩小功能
fixture而宣称通过。未放宽生产100ms或识别3秒期限，也未新增suppression。
前述失败与诊断保留在/tmp/mirage-capability-{ubsan,tsan}-final-test.log和
/tmp/mirage-capability-tsan-diagnostic-test.log。最终常规矩阵另列，状态不覆盖压力
结果。源码范围和摘要在out/layered-capability-20261028/source.json（7份C++/Python/
CMake文件），固定方法同前文；不包含整仓或模型二进制。


### 本次能力验证收口

修正测试前置同步、补失败诊断和区分大图压力条件后，最终常规专项结果：

| 配置 | 实际结果（5套业务+隔离） |
| --- | --- |
| debug | 7/7，18.10s |
| asan | 7/7，18.74s |
| ubsan | 7/7，18.58s |
| tsan | 7/7，20.14s |
| native-release | 7/7，17.92s |

当前layered 157断言、dispatcher310、原Host281、continuous consumer2319、native
PP-OCR265断言，均0失败；没有sanitizer报告或新增suppression。最终预训练重放
real-replay-final/仍10/10通过，Python driver在执行前删除同目录旧报告，避免异常
运行展示陈旧成功。此收口覆盖薄宽图功能测试，前述大图TSAN压力失败仍保留。

本次7文件源码SHA256：`bd8f633cc2e538c3954e3bfd87b8160a08dda282e79f9397b413ec5626f8ab74`，路径清单与摘要在Git外source.json；
并非全仓/模型摘要。构建日志/tmp/mirage-capability-*-verified-build.log，常规回归
/tmp/mirage-capability-*-verified-test.log和verified-matrix.json；原失败轮次未覆盖。
native服务/前端和manual工具已构建或重链接，没有安装或重启用户服务。
受影响5份C++格式、Python语法、本地文档链接及公开头边界通过；全仓此前既存
格式失败仍不属本次修改。M9-28/33保持In Progress，默认实验仍关闭。
