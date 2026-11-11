# 分层视觉生产配置与旧 Host 迁移验收

> 状态：Completed（Linux本机生产配置限定范围；高清原768MiB目标未达）
> 日期：2026-10-28
> 负责人：Mirage维护者
> 工作项：M9-25/27/28/32/33；[专项计划](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-128](../decisions/DEC-128-layered-perception-production-profile.md)，补充DEC-121～127

## 交付范围

Linux RuntimeService默认使用layered、200ms采样、5000ms完整识别巡检、有界跟踪。
`--perception-mode layered|legacy`、`--recognition-refresh-ms`、
`--object-tracking on|off`为稳定配置，历史experimental参数作为兼容别名；参数按顺序
覆盖，legacy模式恢复同步、250ms、关闭跟踪/识别延后。配置OCR资源和原有授权仍是
前提。源码嵌入者使用ServiceConfig的perception前缀字段；低层PpOcrPipeline可选
位置参数与默认值保持兼容。使用示例见[配置文档](../examples/layered-visual-sampling.md)。
Windows保留legacy默认，可以显式选layered，尚无本次目标平台验收。

旧VisualSessionHost的sample_only、准入和session变化比较、跟踪模板都使用
最长边320的Gray8工作图；source尺寸/rotation变化撤销原布局，即使两者产生相同
工作图尺寸。区域按边界floor/ceil回映，发布snapshot副本中的change保持原坐标。
OCR/YOLO仍接有界识别图及各自模型预处理，CV指纹不代入能力缓存；前台精读/精确
几何保持细节输入。新增1600×900缓存/坐标测试：纯CV零能力缓存条目；OCR仍接
1600×900；Refresh结果可ReadWrite复用；1000×500与1001×503的布局屏障有效。

独立采样owner新增4KiB“最后实际接纳帧”签名，识别在途/待消费时合并相同能力与ROI
的重复工作。比较独立于相邻帧，因此连续100→106→112的缓慢漂移仍累计越过阈值；
真实变化更新latest。完成后必要确认、年龄巡检、scope/代次重置继续执行。合并不生成
新识别证据，不续期历史文本或复活语义标签。

## 固定条件与证据

- 机器：Intel Core Ultra 5 225H，14逻辑CPU，Linux 7.0.0-38-generic，x86_64；native-release。
- 本地已验证PP-OCRv5 mobile与OmniParser YOLO manifest：
  `build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json`；
  SHA256 `ec703acaba9325c40d4e34c5ef542df63941481f9af4a6b2339aa5aee8545097`。
- 字体NotoSansCJK-Regular.ttc：SHA256
  `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`。
- 迁移前工作树冻结ppocr_pipeline_test（含既有第七阶段改动，并非已发布旧标签）：SHA256
  `2fc6e62748c5c60dd1d12c7053aa5b07ddf36a559301d5d229be4650f0a2aef1`。
  本轮精度二进制：`2c8c138c627e1a2440343a5b637e007df7d1cc1709809a6dd6d1c42b2f299778`。
- 当前587个自研源码/构建/验证脚本文件清单在source-final.json，总SHA256
  `1b0c5c0623b2d8f6a060abe9e4acd28acdee3436c3323b2d9a43fb97101f64ba`。
  高清容量复验器SHA256
  `9abad29dfa0a5bcdb9f8eb41485e4af5ed14d056dd393953148d6998d11d4c7d`；
  原768MiB矩阵的验收器hash保留在resources-final/matrix.json中，二者核心生产代码相同。
- 本地原始证据统一在`out/layered-production-20261028/`（构建产物，不提交）；
  quality-final、real-final-640、real-final-1600、x11、resources-final、resources-hd-capacity分别记录命令、
  样本/二进制hash、逐项结果。初次失败/试验放在smoke*.json与resources，不覆盖。

资源测试使用预加载640×360/1600×900像素源，每臂预热5秒、测量60秒，依次READY、
SAVED、空白，各20秒。legacy和layered加载完全相同OCR/YOLO能力，仅调度不同；
此处legacy不是旧版仅OCR产品，也不是迁移前二进制。单独顺序执行，不与编译/其他
模型测试并跑。CPU按一个逻辑核=100%，模型装载不计CPU窗口；采样停止与排空计入，
峰值RSS由getrusage统计，包含模型初始化/工作空间。调用数包含5秒预热。

预登记门：layered实得频率≥目标90%、p95间隔≤周期1.25倍、CPU≤150%单核、
组合模型RSS≤768MiB且相对匹配legacy增量≤64MiB；高清原门失败后容量分档修订见下文；无识别失败、像素保留≤20MiB、
SAVED可见≤3秒加两个采样周期。该门只对所测语料/配置有效，不是全局RSS上限。

## 精度与功能

| 检查 | 实际结果 | 范围 |
| --- | --- | --- |
| 冻结二进制配对文本/框 | 27/27不退化 | 4类英文/中文/混排/数字×16/24/32字号×1/2 DPI，加2密集多行与空白；文字完全相同、框各分量差≤1px |
| 独立标注文本 | 两臂均27/27完全匹配 | 低分辨率/高置信度空结果不能据此证明普遍无损 |
| 实际PP-OCRv5/YOLO重放 | 640、1600各12/12门通过 | 自动文字路由、20帧静态零调用、OCR-only/YOLO-only ROI、完整年龄巡检、空白与无动作引用 |
| X11隔离应用 | 两张图像素精确；表单6/6目标通过 | IoU≥.8、中心误差≤2px；空白无新增对象，anchors原坐标一致 |
| 稳定CLI解析 | 18/18 | 合法/非法值、legacy/layered及历史别名；合法值配合help退出，不启动用户服务 |
| Linux默认产品接线 | native-release ppocr_pipeline_test通过 | mode5不传实验开关，真实ServiceConfig/RuntimeService工厂与原生授权采集夹具；其他模式显式回退 |

X11证据为本轮Host迁移版本，最终提交合并只影响独立采样owner，不影响该前台路径。
X11精读的max-rss-kib分别806388、762572，未经过资源producer的768MiB门；其中
表单精读高于768MiB。它提示固定模型与较复杂图像的工作空间开销，不能以受控单行
重放宣称所有桌面内容都在该预算内。实际Portal/Windows会话及更密集视频仍需补验。

## 资源配对

以下为最终核心源码在原768MiB门下的八臂结果，每臂60秒；CPU以单核计。

| 输入 | 调度/目标Hz | 实得Hz | p95间隔ms | CPU% | 峰值RSS KiB | SAVED可见s | OCR/YOLO | 识别失败 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 640 | legacy/5 | 3.83 | 1052.58 | 31.89 | 683364 | 3.117 | 16/16 | 0 |
| 640 | layered/5 | 4.98 | 200.27 | 46.28 | 692124 | 0.208 | 22/21 | 0 |
| 640 | legacy/10 | 7.27 | 100.27 | 33.47 | 683596 | 2.062 | 16/16 | 0 |
| 640 | layered/10 | 10.02 | 100.16 | 48.10 | 692256 | 0.255 | 22/21 | 0 |
| 1600 | legacy/5 | 3.67 | 1237.66 | 38.95 | 863536 | 3.765 | 16/16 | 0 |
| 1600 | layered/5 | 5.00 | 200.15 | 54.19 | 901108 | 0.261 | 21/20 | 0 |
| 1600 | legacy/10 | 6.65 | 100.50 | 45.40 | 863176 | 3.569 | 16/16 | 0 |
| 1600 | layered/10 | 10.00 | 100.17 | 57.82 | 897892 | 0.253 | 21/20 | 0 |

640两生产臂通过原资源门。高清两生产臂均只失败RSS门，其余门通过；两个高清
同步臂SAVED期限失败。高清5/10Hz分层相对同步RSS增量为37572/34716KiB，仍
低于64MiB。精度、CV/模型尺寸和核心源码保持不变，依据同步基线及公共API核对，
DEC-128另冻结高清1024MiB容量档后重新四臂验证；原768MiB目标仍未达到。

| 高清调度/目标Hz | 实得Hz | p95间隔ms | CPU% | 峰值RSS KiB | SAVED可见s | OCR/YOLO | 识别失败 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| legacy/5 | 3.62 | 1386.58 | 39.36 | 862764 | 3.518 | 16/16 | 0 |
| layered/5 | 5.00 | 200.18 | 55.05 | 899924 | 0.260 | 21/20 | 0 |
| legacy/10 | 6.63 | 101.11 | 45.68 | 863532 | 3.562 | 16/16 | 0 |
| layered/10 | 10.00 | 100.19 | 61.06 | 897788 | 0.157 | 21/20 | 0 |

高清生产两臂通过1024MiB容量档及其余门；相对同步RSS增量分别为37160/34256KiB，均≤64MiB。四臂original_768mib_pass均为false。匹配同步路径的SAVED期限仍失败，原始exit=1保留。

640的768MiB与高清的1024MiB是所测配置的部署容量档，不是任意图像/捕获平台或
整个UI/Agent进程的内存上限。CPU高于同步路径，未声称降低全进程CPU；在途去重
相对首轮降低静态重复调用及CPU，收益主要是稳定采样和变化可见时间。


## 生命周期、回归与工程门禁

| 配置 | 最终当前源结果 | 日志 |
| --- | --- | --- |
| Debug完整 | 120/120通过，335.41秒 | /tmp/mirage-production-current-debug-test.log |
| ASAN专项 | 14/14通过，53.16秒 | /tmp/mirage-production-current-asan-test.log |
| UBSAN专项 | 14/14通过，51.48秒 | /tmp/mirage-production-current-ubsan-test.log |
| TSAN专项 | 14/14通过，109.11秒 | /tmp/mirage-production-current-tsan-test.log |
| native-release专项 | 14/14通过，46.31秒 | /tmp/mirage-production-current-native-test.log |
| TSAN 2048×512大图 | layered_perception_test连续3次通过 | /tmp/mirage-production-current-large-tsan.log |
| 格式/公开依赖边界 | 通过；67头文件零违反，DEC-096既有2项seam | /tmp/mirage-production-current-style.log |

14项含12个业务测试与setup/cleanup。覆盖正常/异常/拒绝/取消/超时/shutdown、
最新值替换和消费背压、源/焦点/尺寸屏障、原帧时间、未知投影、静态在途去重及累计
漂移、局部能力选择和原坐标。大图专项启用MIRAGE_TEST_LARGE_REPLAY=1；TSAN
使用setarch x86_64 -R，没有禁用或抑制竞争报告。Windows CI选择集已加入可移植
视觉用例，但未在本机冒称执行MSVC或交互桌面测试。

AT-SPI矩阵发现并修复测试夹具名称读写竞争，随后两用例各5次连续通过，最终综合
TSAN仍14/14。首次复用观察锁导致同步RPC阻塞，已撤回改用独立名称锁。原失败与
成功日志见[语义快照记录](semantic-snapshot-window-occurrence-20261028.md)。
同标题窗口按同(application,title)出现序号定位，不再因全局同名拒绝；前台不匹配
与捕获期丢失分别给出可操作错误，仍保留作用域与stale_observation契约。

## 资源账本与失败记录

| 组件 | 有界内容/预算 |
| --- | --- |
| 采样owner签名 | 相邻/完整识别/最后接纳/局部各64² Gray8，常驻合计≤16KiB；当前临时签名另4KiB |
| CV工作图 | 彩色缩放≤409600B、Gray8≤102400B、局部Gray8裁剪≤102400B |
| 跟踪 | ≤8目标、64KiB池、1MiB验证工作，过小模板排除；不确认精确文字/身份 |
| 派发像素 | 一个在途/待消费及一个latest，各≤10MiB；外部保留原帧仍占槽 |
| 完成结果 | 动态元数据≤512KiB/结果，至多两个；无消费时不继续启动 |
| 路由/ROI | ≤32 hints、局部签名4KiB已计上表、输出摘要预留16KiB；ROI临时≤3MiB |
| 旧Host | Adapter完整/采样签名≤8KiB、Mirador内部变化签名另4KiB；识别缓存/模型独立预算 |

这不是可相加后视为进程RSS的估算：共享缓存、模型workspace、OCR包装、临时编码与
平台捕获仍另计；实际进程峰值必须测量。追踪/变化均复用Mirador公开API，调度、取消、
通信与关闭复用Mira交付的kairo。未修改third_party与dependencies.lock.json。

初次冒烟15秒的512MiB条件失败：691012KiB；改OCR det单线程仍694896KiB且延迟
变长，已撤回，不宣称降至512MiB。完整配对前按DEC-128冻结768MiB与相对64MiB门。
首轮60秒（尚无在途去重）640×360 layered 5/10Hz均达到目标，CPU62.76/66.32%，
31/30及29/28次OCR/YOLO。匹配legacy为3.75/5.62Hz，SAVED延迟3.469/15.491秒，
10Hz有3次识别失败。该结果暴露静态在途重复latest，不作为最终优化后成本声明。
新增独立接纳签名后重新运行全部配对，不修改门槛以替代优化。

## 复现与未关闭门禁

```sh
cmake --build --preset native-release --target layered_production_acceptance layered_perception_acceptance ppocr_pipeline_test
python3 tests/manual/layered_production_quality.py --baseline out/layered-production-20261028/baseline/ppocr_pipeline_test --binary build/native-release/tests/ppocr_pipeline_test --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --output out/layered-production-repro/quality
python3 tests/manual/layered_perception_acceptance.py --binary build/native-release/tests/layered_perception_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --source-width 640 --output out/layered-production-repro/real-640
# Repeat the preceding command with source-width 1600 and real-1600.
python3 tests/manual/layered_production_resources.py --binary build/native-release/tests/layered_production_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --corpus-640 out/layered-production-repro/real-640 --corpus-1600 out/layered-production-repro/real-1600 --output out/layered-production-repro/resources
```

本次不安装/重启用户服务，不调用LLM、下载模型或操作用户桌面。精度语料与资源重放
可复现，但未覆盖真实视频语义理解、音频、字幕单字符全域召回、在线token/任务完成率、
Windows/MSVC交互桌面、Portal真实会话长任务、低端设备及更大/密集语料。上述事项由
Mirage维护者在目标会话、固定任务与音视频语料准备后按M9-29～33补跑，保持In Progress，
不把本机默认切换写成整个M9或完整视觉层验收完成。

生产开放结论：本机配置按上述容量档通过频率、精度非退化、时效、增量内存与
生命周期门；Linux默认开放，Windows保持legacy默认。原768MiB高清性能目标、
广域精度与目标平台/视频门禁均不因本结论关闭。
