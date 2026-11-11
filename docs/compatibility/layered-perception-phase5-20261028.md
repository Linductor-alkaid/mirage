# M9-28 独立采样与异步识别实验验证

> 状态：Completed（仅本机异步实验；M9-28完整门禁保持In Progress）
> 日期：2026-10-28
> 负责人：Mirage 维护者
> 所属：[三层视觉计划](../plans/m9-layered-perception-delivery.md) M9-28第2步
> 决策：[DEC-125](../decisions/DEC-125-layered-capture-owner.md)
> 环境：本机 Linux；已有 Debug/ASAN/UBSAN/TSAN/native-release 构建目录

## 交付边界

新增 `connect_layered_desktop` 与默认关闭的服务开关
`--experimental-async-recognition`，依赖非零
`--experimental-recognition-refresh-ms`；可选已有 object tracking 开关。
Runtime resident factory 根据实验开关选择旧串行或新分层接线，默认行为保持。
实际接口及参数见 [使用示例](../examples/layered-visual-sampling.md)。

采样 owner 独占相邻/识别双签名、ObjectTracker、journal；独立派发 worker 等待
和转交慢识别，OCR/融合仍由原 VisualSessionHost 串行执行，不并发调用 backend。
正常新帧仅覆盖latest，bootstrap、待确认对象、累计变化、跟踪重验和年龄到期
触发真实识别；确认媒体内变化可在年龄预算内延后。CV 不推进融合/覆盖。

识别 callback 在调用前取得原始采集时间/来源代次，强制真实 OCR，绕过旧内部
CV媒体延后分支，并禁用分析 worker 的 tracker。识别成功才更新采样 owner 的
识别基线；已确认且实际观察的对象在原像素上建立模板，每次完整识别后重建，
不声称连续性跨越慢识别间隔。跟踪只提供 advisory，不刷新文字或签发动作引用。

完成事件/预览保留原帧、原尺寸映射、采集完成时间及 source_sequence。
时间是Adapter取得该帧的steady_clock完成时刻，不是媒体PTS，也不表示音频覆盖。当前采样已到
后续帧时暂停 semantic_scene 供给，保留历史事件/预览、原识别年龄和 advisory
tracking；不把近似签名比较当精确reuse或新帧覆盖。这个保守投影会降低当前对象
标注可用性，历史对象投影/精确覆盖和任务注意力仍需后续交付，不能宣称视频理解
质量已经保持或提高。源/尺寸/焦点/前台暂停代次撤销旧工作，短于采样周期的暂停
也有效；权限/任务退役立即禁止Feed供给。关闭先停source再回收识别及OCR。

派发器仍最多两份≤10MiB原帧；采样多两个64²签名与可选64KiB模板池。
当前分析帧、area resize中间缓冲、OCR包装拷贝、模型/缓存及平台raw capture另计，
尚无全进程峰值/CPU验收。dispatch投影CV/准入/替换/拒绝/取消/超时/丢弃和保留
像素计数；CV推进本身不生成语义事件或模型调用。已有原worker签名/缓存未消除。

## 复现命令

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_object_tracking|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|continuous_recognition|significance_gating|model_layer|ppocr_pipeline_test|runtime_service_test' --output-on-failure

cmake --build build/asan --target layered_perception_test continuous_recognition_test continuous_capture_test continuous_perception_consumer_test visual_object_tracking_test task_perception_test ppocr_pipeline_test runtime_service_test desktop_image_supply_test -j 2
ctest --test-dir build/asan -R '^(layered_perception_test|continuous_recognition_test|continuous_capture_test|continuous_perception_consumer_test|visual_object_tracking_test|task_perception_test|ppocr_pipeline_test|runtime_service_test|desktop_image_supply_test)$' --output-on-failure
# UBSAN相同target/正则，将build/asan替换为build/ubsan。
# TSAN相同target/正则，将build/asan替换为build/tsan，ctest前加setarch x86_64 -R。

cmake --build build/native-release --target mirage-native mirage-service ppocr_pipeline_test layered_perception_test continuous_recognition_test -j 2
ctest --test-dir build/native-release -R '^(ppocr_pipeline_test|layered_perception_test|continuous_recognition_test)$' --output-on-failure
cmake -DROOT_DIR=/home/linductor/mirage -P cmake/BoundaryCheck.cmake
```

## 自动化场景

- 相邻每次6灰度小变化、相对识别基线累计12：相邻仍none但累计请求识别；取消
  不推进采样签名，媒体内/外与年龄到期分别验证，不把deferred当识别基线。
- 实际24²纹理由确认对象初始化，在独立CV中平移(+2,+1)得到NCC确认位置；弱证据
  请求重识别。使用公共ObjectTracker，模板/对象上限沿用DEC-123。
- 实际VisualObservationPipeline阻塞OCR时CV≥5次，后台不因每帧latest取消；
  后续确认并进入静态阶段，CV继续推进但OCR和事件游标不变、识别年龄不刷新。
- 显式前台OCR取消被阻塞的后台请求并完成，额外screen capture计数0；短暂停顿、
  焦点和尺寸代次取消旧识别，模板/journal重置；权限退役禁供给。
- 2048×512原帧缩到1600边分析，延迟事件仍按原尺寸映射；预览序号/时间属于
  首个原帧，不能借用后来的raw capture。新几何不继承旧journal。
- observer异常、非法刷新、Executor准入拒绝、3秒运行超时、关闭后Feed状态和
  原平台资源回收；迟到成功不带出语义。dispatcher原专项另验排队超时等生命周期。
- 已完成取消令牌被分析host/测试保留时仍可连续8次识别，释放像素槽且旧token
  可观察取消，不形成隐式帧缓存。model dispatch文本仍≤8KiB且不新增图片。
- native-release小型NCNN产品fixture：外部采样模式五次明确识别实际调用5次，
  不进入媒体deferred，不在分析worker重复启用tracker；切换代次再执行，前台精读
  正常。RuntimeService新增异步模式覆盖真实factory接线和单源关闭契约。

## 失败轮次与修正

1. 首次构建测试引用了不存在的FakeDesktopEnvironment.screen_provider字段，
   改为fixture已有capture_calls；未改变产品行为。
2. 首次媒体单测未建立相邻签名，首帧global使延后断言失败；补明确bootstrap步骤。
3. 实际pipeline焦点重启后持续consumer_frame_budget：DEC-124取消probe强引用Job，
   VisualSessionHost留存已settled请求中的token使两原帧槽被间接占用。改为weak Job
   probe；worker本身持有同步调用生命周期，退出后旧token自动取消。增加保留token
   的8轮回归，真实pipeline恢复；未放宽槽预算或修改依赖。
4. 关闭专项要求Feed stopped，检查发现worker可直接观察stop token退出而不再需要
   wakeup；将关闭准入/撤销排队/释放callback做成每条run退出路径均执行。首轮完整
   构建期间测试也继续补齐，收口统一冻结源码后重建，修复前结果不计最终通过。
5. native产品关闭计数一次失败；带诊断的重跑及12轮压力未复现，但补充的最小
   legacy source用例可稳定复现同类问题：保留观察取消token后，Session析构仍把
   raw session留在被token引用的Core中。改为worker全部join后显式reset raw、清空
   journal/observer；保留token的反例修复前失败、修复后专项与最终五套门禁均通过。没有
   放宽opened==1/closed==1断言。该修复也覆盖默认串行source的资源释放。
6. native产品fixture增加mode5后遗漏旧mode!=4失败预期，补齐新成功模式。未修改
   模型或降低产品断言。重复的native持久化目录亦分开，避免沿用旧fixture状态。

## 最终结果

| 门禁 | 实际结果 | 范围/限制 |
| --- | --- | --- |
| Debug全量构建/相关回归 | 构建退出0；23/23，54.21s | 21套业务测试+隔离，不是全量CTest |
| ASAN | 11/11，44.12s | 9套业务测试+隔离；无ASAN报告 |
| UBSAN | 11/11，42.70s | 无UBSAN报告 |
| TSAN | 11/11，69.42s | setarch；无报告，未添加suppression |
| native-release | 前端/服务/专项构建退出0；6/6，9.26s | 4套业务测试+隔离，含小型NCNN/Runtime真实factory接线 |
| Release关闭回归 | PP-OCR专项连续12轮通过，整体4.50s | 保留opened==1/closed==1；不作实机平台压力结论 |
| CLI解析 | 9组符合退出码/帮助预期 | help和非法值，不启动服务；生命周期由上列fixture验证 |
| 公共边界 | 67头、0违规，2处既有DEC-096缝 | 不新增第三方类型泄漏 |
| 本轮24份C++格式/diff | 通过 | 全仓仍在既存未改desktop_action_verification.cpp:490失败 |
| 本轮8份文档本地链接 | 362处，0缺失 | 不验证远程可达性或页内锚点 |

最终源码以以下26项按路径排序、逐项追加路径/NUL/内容/NUL计算SHA256：
`fd3239a932e845fe1a66a2e61fb1fbab43dc2ccb2bce7d69e832d77b4af4bef8`。这不是整个仓库/二进制摘要；构建使用现有pinned依赖和preset。
清单也写入 `/tmp/mirage-layered-phase5-source.json`。

```text
apps/service/host.cpp
integration/mira/include/mirage/integration/desktop_perception.hpp
integration/mira/src/desktop_perception.cpp
integration/mirador/CMakeLists.txt
integration/mirador/include/mirage/integration/continuous_desktop_session.hpp
integration/mirador/include/mirage/integration/visual_observation_pipeline.hpp
integration/mirador/src/continuous_desktop_session.cpp
integration/mirador/src/continuous_image_observation.cpp
integration/mirador/src/continuous_recognition_host.cpp
integration/mirador/src/continuous_recognition_host.hpp
integration/mirador/src/layered_visual_sampler.cpp
integration/mirador/src/layered_visual_sampler.hpp
integration/mirador/src/perception_journal.hpp
integration/mirador/src/ppocr_pipeline.cpp
integration/mirador/src/ppocr_pipeline.hpp
integration/mirador/src/visual_object_tracking.cpp
integration/mirador/src/visual_object_tracking.hpp
runtime/service/include/mirage/runtime/runtime_service.hpp
runtime/service/src/runtime_service.cpp
tests/CMakeLists.txt
tests/integration/continuous_recognition_test.cpp
tests/integration/desktop_image_supply_test.cpp
tests/integration/layered_perception_test.cpp
tests/integration/ppocr_pipeline_test.cpp
tests/integration/task_perception_test.cpp
tests/runtime/runtime_service_test.cpp
```

最终日志为 `/tmp/mirage-layered-phase5-{debug,asan,ubsan,tsan,native}-verified-{build,test}.log`；
关闭重复为 `native-lifecycle-verified-repeat.log`，CLI为cli.json，边界为boundary-final.log，
全仓格式为format-full.log（同一前缀）。中间 `*-complete-*`、`*-final-*` 仅作失败轮次
或修复过程记录，不用于最终通过结论。未把旧轮次的测试结果套到新源码上。

## 未执行与剩余门禁

| 项目 | 原因/负责人/补跑条件 |
| --- | --- |
| ROI识别/外围重验 | 尚未实现；Mirage维护者继续M9-28第3步，验证实际空间执行与证据覆盖一致后补齐 |
| 精确当前reuse/历史对象投影/任务注意力 | 仍保守未知；同负责人按M9-26/29处理覆盖、版本和语义升级，不能用模板位置替代文字 |
| Windows/MSVC | 当前Linux环境；同负责人在Windows+目标Backend跑异步取消/关闭/资源门禁 |
| 实得5/10Hz、CPU/RSS、漏报与真实视频质量 | 本轮合成/小型网络fixture；同负责人固定硬件/模型/任务和预登记阈值后按M9-33配对运行 |
| 上线A/B与默认切换 | 未执行；上述平台/资源/质量就绪后按M9-33验收，保持默认关闭 |

不修改依赖指针/锁文件、pinned代码，未提交/推送/安装/重启服务；此前视觉与记忆治理
工作树保留。M9-28和完整三层视觉目标仍In Progress。
