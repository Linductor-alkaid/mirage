# 三层感知第二阶段：媒体识别延后与未知证据投影

> 状态：Completed（本机实验契约验证；完整 M9-26 门禁仍开放）
> 日期：2026-10-28（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-26；关联 M9-25、M9-28/32/33
> 决策：[DEC-122](../decisions/DEC-122-deferred-recognition-unknown-evidence.md)
> 基线：`d30502a` 之上的工作树，分支 `feat/continuous-semantic-binding`

## 环境与范围

Linux x86_64，GCC 13.3.0，CMake 3.28.3；Mira `68a8bf17`，Mirador `2a9f4830`。
Debug/sanitizer 受控假后端；native-release PP-OCR/YOLO ON，产品接线测试使用
小型 NCNN fixture 网络，不是真实预训练权重/内容质量验收。没有访问用户桌面、
调用在线模型、安装/重启服务或修改依赖。既有时间拼图和记忆治理改动保留。

服务 CLI / ServiceConfig / PpOcr adapter / VisualObservationPipeline 同步传递
实验刷新间隔；0 默认关闭，100～5000ms 合法。确认媒体内的相邻/累计变化仅
运行 sample_only，延后 OCR/detector；到期即使静态也强制真实识别。区外变化
复用同次采集的像素进入识别，无第二次 capture；来源/尺寸/焦点策略重建 bootstrap。

历史对象保留 ID/文本但标 pending/未确认，scene_observed_at 不续期；首个延后
周期递增产品内容失效版本，后续延后不重复递增，旧标签不能跨未知区间自动恢复。
供模型的 scene 和摘要带 recognition_deferred、原因与单独 deferred_frames 计数；
提示明确历史内容不能用于确认变化/消失。显式 OCR/动作验证沿用原路径。
没有把旧快照标成新帧覆盖，没有调用 plan/accept/drain 推进未知周期。
沿用原 kairo worker/通信/关闭路径，新增缓存仅至多256对ID/实例等元数据。


本轮源码范围（不包含文档；按路径排序后，逐项追加路径、NUL、内容、NUL 计算 SHA256）：

```text
apps/service/host.cpp
integration/mira/include/mirage/integration/desktop_perception.hpp
integration/mira/src/desktop_perception.cpp
integration/mira/src/model_layer.cpp
integration/mira/src/perception_semantics.hpp
integration/mirador/include/mirage/integration/visual_observation_pipeline.hpp
integration/mirador/src/continuous_desktop_session.cpp
integration/mirador/src/continuous_image_observation.cpp
integration/mirador/src/perception_journal.hpp
integration/mirador/src/ppocr_pipeline.cpp
integration/mirador/src/ppocr_pipeline.hpp
integration/mirador/src/visual_observation_pipeline.cpp
runtime/service/include/mirage/runtime/runtime_service.hpp
runtime/service/src/runtime_service.cpp
tests/integration/perception_semantics_test.cpp
tests/integration/ppocr_pipeline_test.cpp
tests/integration/significance_gating_test.cpp
tests/integration/task_perception_test.cpp
tests/runtime/runtime_service_test.cpp
```

SHA256：`560fe5463e2723f4af78b66706d043ab78d3c85be31a3bf8d8ff829ccaf44416`。

## 复现命令

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|significance_gating|model_layer|ppocr_pipeline_test|runtime_service_test' --output-on-failure

cmake --build build/asan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 3
ctest --test-dir build/asan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure
cmake --build build/ubsan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 3
ctest --test-dir build/ubsan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure
cmake --build build/tsan --target continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 3
setarch x86_64 -R ctest --test-dir build/tsan -R '^(continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure

cmake --build build/native-release --target mirage-native mirage-service ppocr_pipeline_test significance_gating_test -j 3
ctest --test-dir build/native-release -R '^(ppocr_pipeline_test|significance_gating_test)$' --output-on-failure
cmake --build build/debug --target mirage-format-check mirage-boundary-check
```

## 验证结果

| 验证 | 实际结果 | 限制 |
| --- | --- | --- |
| Debug 全量构建 + 相关回归 | 构建退出0；20/20，48.65s | 18套业务测试 + 隔离 setup/cleanup；不是全量 CTest |
| ASAN 最终专项 | 10/10，46.36s | 8套业务测试 + 隔离；无 ASAN 报告 |
| UBSAN 专项 | 10/10，47.45s | 无 UBSAN 报告 |
| TSAN 专项 | 10/10，84.65s | 使用 setarch，无新 suppression |
| native-release 最终前端/服务构建 | 退出0 | PP-OCR/YOLO ON；未启动/安装 |
| native-release 产品接线/门控 | 4/4，1.25s | 2套业务测试 + 隔离；小型 NCNN 网络，不证明预训练模型质量 |
| CLI 解析 | 12组符合退出码 | 合法值配 --help，不启动服务 |
| 公开头边界 | 67头、0违规、2既有 DEC-096 缝 | 无新增第三方类型泄漏 |
| 本轮19个源码格式、diff | clang-format / git diff --check 通过 | 全仓格式仍有既存失败，见下文 |
| 本地 Markdown 文件链接 | 445处、0缺失 | 本轮相关16份文档；不验证远程可达性 |

临时日志前缀为 `/tmp/mirage-layered-phase2-`（debug-test、asan-final-test、
ubsan-test、tsan-test、native-final-test、cli、boundary-format）；测试本身含后端
调用/状态断言，不依赖临时日志作为运行输入。

全仓 mirage-format-check 未通过，停在未修改的
`integration/mira/src/desktop_action_verification.cpp:490`；本轮修改的服务参数
块顺带消除了上阶段 host.cpp 的格式阻塞，未扩展到无关源文件。负责人 Mirage
维护者，在独立格式清理后补跑全仓门禁。

验证场景：

- 默认/实验各 bootstrap 三个媒体帧，之后两个媒体变化帧：小型 NCNN 产品
  fixture 默认后端增加2次，实验增加0次；总计数分别5/3。两者显式 OCR 均
  再增加1次，全程 capture_calls=0，验证 create 配置接线与已有像素路径。
- 假 OCR + counted detector：延后三帧均不调用两个后端；关键帧仍有事件。
  原对象文本/ID保留，未确认，识别时刻不变；第一次失效递增，后续版本稳定。
- 区外变化同次像素立刻真实识别；字幕 text_changed 仍按 Mirador 第二次真实
  观察确认，CV-only 不算确认观察。来源/焦点变更重新识别，来源 epoch 变化。
- 500ms 实验期限过后，静态帧仍调用后端；注入后端失败不续期，恢复后再识别。
  已取消请求不调用后端。非法配置在启动前拒绝；运行配置0为默认回退。
- 语义 journal 与 model projection：原年龄至少2000ms，不签发待确认对象引用；
  已消费的旧引用拒绝；恢复出相同文本也不激活旧标签，新的消费/标注可以激活。
- ContinuousDesktopSession：成功延后单独计数，不冒充 analyzed；后续延后
  不更新 scene_observed_at。复用既有 sample/session 异常、拒绝、运行中取消、
  timeout/shutdown 矩阵与 companion 隔离回归，不新增并发设施。
- CLI 合法值0/100/2000/5000配合 --help 返回0；负值、1/99/5001、尾缀、
  int溢出、空值和缺参均返回2，共12组，不启动服务。

开发轮次：首轮 Debug 5项（含隔离）中 significance_gating 失败：fixture 没有
开启显式 run_ocr，且把两次观察的字幕确认错误断言为第一次即产生事件。补齐
配置并分别断言首次识别内容、第二次确认事件后，专项6/6通过（16.59s）。这轮
失败不算通过，也没有修改 Mirador 去抖规则。后续增加 counted detector 与
产品小型 NCNN fixture 再跑最终矩阵。收口时检测到 ASAN/native 首次构建早于
新增的历史证据提示文本；重新构建这两套产物并完成最终专项，表中使用最终结果。

## 限制与后续

本轮只证明实验调用分频与未知证据语义；不声称精确跨帧 reuse、事件零漏报、
实得5/10Hz、真实媒体质量/CPU/RSS/token收益。原识别和 CV 仍共用串行 worker，
慢识别分离归 M9-28，生产 tracker 归 M9-27。媒体字幕/瞬时提示可能在刷新间隔
内丢失；确认去抖延迟另计，刷新参数不构成端到端延迟上界。

[MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)
仍 Open，精确 ROI 验证/覆盖复用按公开 API 继续核对，未向上游发送或改 pin。
M9-26 仍 In Progress：Windows/MSVC、真实屏幕/Portal、固定模型视频任务、低端
长时间资源和在线 A/B未执行。负责人 Mirage 维护者；在目标 runner、授权桌面、
固定模型/任务及硬件就绪后按 M9-33 补跑，再决定参数与生产默认切换。
