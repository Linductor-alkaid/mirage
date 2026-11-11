# 三层感知第三阶段：后台有界对象跟踪

> 状态：Completed（本机默认关闭实验验证；M9-27完整门禁仍开放）
> 日期：2026-10-28（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-27 / M9-08；关联 M9-26/28/32/33
> 决策：[DEC-123](../decisions/DEC-123-bounded-background-object-tracking.md)
> 基线：`d30502a` 之上的工作树，分支 `feat/continuous-semantic-binding`

## 环境与实现范围

Linux x86_64，GCC 13.3.0，CMake 3.28.3；Mira `68a8bf17`、Mirador `2a9f4830`。
没有修改 pinned 代码/锁文件、启动或安装用户服务、调用在线模型、下载新权重。
Debug/sanitizer 用受控假后端，native-release 的产品接线另用小型 NCNN fixture，
不是预训练模型质量或真实桌面/视频验收。原生重新配置触发本地NCNN大范围重编译。

私有 VisualObjectTracking 封装公共 ObjectTracker，运行在既有后台 companion
worker。服务 CLI / ServiceConfig / PpOcrPipeline / session 配置贯通；
`--experimental-object-tracking` 默认关闭，要求 DEC-122 非零刷新间隔。
前台 companion 不分配池。停止时先取消/join原worker，再清空私有池，避免
Executor持有注册项时保留轨迹模板。

已确认 OCR/检测实体才可初始化模板；entity/instance映射到私有单调token。
复用只代表近似变化门控，无新的verified_sequence；外观确认来自公共NCC/PSR
验证和证据commit。位置/文字变化或新实例重建轨迹并显式discontinuous，内容
版本随之失效；首次验证失败即使尚无deferred视图也应用同一失效规则。重叠歧义、弱证据、失踪、预算、新几何候选请求同次像素重识别。
全局布局、来源/尺寸/焦点策略变化清空旧轨迹。lost不是disappeared，近邻
reacquired不恢复旧标注、文字年龄或可执行引用；CV-only对象仍按DEC-122未知。

预算：8目标，64KiB公共池，4位置/目标，单份16²模板，0负模板，无深度会话。
每目标verify工作计量上限1MiB、最多8次/帧；模板固定，避免漂移学习。几何
观察只选一个≤128²变化ROI，公共crop/灰度/线段/merge/propose，最多128线段、
8候选；只产生几何事实，不把相似矩形单独作为身份确认。绑定元数据≤8条，
无额外整屏/历史帧缓存。多对象步骤失败/取消/异常后清空池并要求bootstrap，
不发布部分轨迹或提交失败CV基线；识别已成功时，跟踪初始化Status失败仅作为
附加诊断，不抹去识别结果。

sample/view 独立保留≤8轨迹/8候选，分析坐标经原映射恢复到共享源像素。
模型仅投影≤4轨迹且逐条JSON合计≤650B，附总数和有界计数/原因；标明
input_actionable=false、content_freshness=not_established。候选只给数量，
实际ROI调度/慢识别解耦留待M9-28。journal拒绝超预算或长字符串跟踪投影并给原因。


源码摘要覆盖下列27项（含2份CMake；25份C++格式检查），按路径排序、逐项追加
路径/NUL/内容/NUL计算 SHA256：`5a0c5e80af32e17e2143ee544292deb36ed167a9124b888bc731a0d258afe935`。

```text
apps/service/host.cpp
integration/mira/include/mirage/integration/desktop_perception.hpp
integration/mira/src/desktop_perception.cpp
integration/mira/src/model_layer.cpp
integration/mira/src/perception_semantics.hpp
integration/mirador/CMakeLists.txt
integration/mirador/include/mirage/integration/visual_observation_pipeline.hpp
integration/mirador/include/mirage/integration/visual_session_host.hpp
integration/mirador/src/continuous_desktop_session.cpp
integration/mirador/src/continuous_image_observation.cpp
integration/mirador/src/perception_journal.hpp
integration/mirador/src/ppocr_pipeline.cpp
integration/mirador/src/ppocr_pipeline.hpp
integration/mirador/src/visual_object_tracking.cpp
integration/mirador/src/visual_object_tracking.hpp
integration/mirador/src/visual_observation_pipeline.cpp
integration/mirador/src/visual_session_host.cpp
runtime/service/include/mirage/runtime/runtime_service.hpp
runtime/service/src/runtime_service.cpp
tests/CMakeLists.txt
tests/integration/continuous_perception_consumer_test.cpp
tests/integration/perception_semantics_test.cpp
tests/integration/ppocr_pipeline_test.cpp
tests/integration/significance_gating_test.cpp
tests/integration/task_perception_test.cpp
tests/integration/visual_object_tracking_test.cpp
tests/runtime/runtime_service_test.cpp
```

## 复现命令

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_object_tracking|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|significance_gating|model_layer|ppocr_pipeline_test|runtime_service_test' --output-on-failure

cmake --build build/asan --target visual_object_tracking_test continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 3
ctest --test-dir build/asan -R '^(visual_object_tracking_test|continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure
cmake --build build/ubsan --target visual_object_tracking_test continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 2
ctest --test-dir build/ubsan -R '^(visual_object_tracking_test|continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure
cmake --build build/tsan --target visual_object_tracking_test continuous_perception_consumer_test visual_session_test significance_gating_test task_perception_test perception_semantics_test desktop_image_supply_test ppocr_pipeline_test runtime_service_test -j 2
setarch x86_64 -R ctest --test-dir build/tsan -R '^(visual_object_tracking_test|continuous_perception_consumer_test|visual_session_test|significance_gating_test|task_perception_test|perception_semantics_test|desktop_image_supply_test|ppocr_pipeline_test|runtime_service_test)$' --output-on-failure

cmake --build build/native-release --target mirage-native mirage-service ppocr_pipeline_test visual_object_tracking_test significance_gating_test -j 3
ctest --test-dir build/native-release -R '^(ppocr_pipeline_test|visual_object_tracking_test|significance_gating_test)$' --output-on-failure
cmake --build build/debug --target mirage-format-check mirage-boundary-check
```

## 验证结果

| 验证 | 实际结果 | 范围/限制 |
| --- | --- | --- |
| Debug 全量构建/相关回归 | 构建退出0；21/21，45.93s | 19套业务测试+隔离；不是全量CTest |
| ASAN 最终专项 | 11/11，45.40s | 9套业务测试+隔离；无ASAN报告 |
| UBSAN 最终专项 | 11/11，45.37s | 无UBSAN报告 |
| TSAN 最终专项 | 11/11，71.47s | setarch，无新suppression |
| native-release 前端/服务/专项 | 构建退出0；5/5，1.72s | 3套业务测试+隔离；小型NCNN，不证明预训练模型质量 |
| CLI 解析 | 16组符合预期退出码 | 旧范围12组+新flag4组；help/解析，不启动服务 |
| 公共头边界 | 67头，0违规，2处既有DEC-096缝 | 未新增第三方类型泄漏 |
| 本轮25份C++格式/diff | 通过 | 全仓格式既存失败保留，见下文 |
| 本地Markdown文件链接 | 460处、0缺失 | 本轮相关18份文档，不验证远程可达性 |

最终日志前缀 `/tmp/mirage-layered-phase3-`，上述5组分别为
`debug/asan/ubsan/tsan/native-complete-build/test.log`；CLI为cli.log，边界为
boundary-final.log。计数与状态由测试内断言验证，运行不依赖临时日志。
最终收口增加首次验证失败的语义失效负例后重建全部五套，表中只使用最后结果。

新增 `visual_object_tracking_test` 实测公共 tracker，而不是伪造跟踪结果：

- 24²确定性纹理平移(+2,+1)后验证位置；近似不变门控保留最后外观验证序号。
- 空白连续3次得到uncertain/uncertain/lost；邻域重现确认reacquired并沿用
  原entity/instance，记录中断；失踪不产生语义disappeared。
- 同位重复目标均报告ambiguous，不提交身份更新；新instance/文字改变标中断。
- 大目标触发verify预算，8目标容量/64KiB池、超额排除/后续显式eviction有计数。
- 全局布局清空池并要求重新识别；运行中取消/过期deadline不发布部分轨迹，
  后续必须bootstrap。几何矩形产生≤8个候选并请求识别。
- 生产worker：bootstrap后移动只做CV、OCR实调数不变；缺失纹理触发同帧
  重识别，坐标与原轨迹不一致时重建并中断；显式前台OCR独立，焦点策略
  重新adopt；来源epoch变化后未确认对象不adopt；capture_calls=0。
- 首次CV失败发生在任何deferred视图之前时，即使新OCR同字同位置，也重新
  初始化并设置invalidate_content，旧语义不可延续。此前已有deferred的用例
  仍保持未知/标注失效，不因reacquired恢复。
- 产品小型NCNN fixture比较默认、仅DEC-122、DEC-122+跟踪三配置；零实体时
  同样保持媒体分频，tracking报告有无匹配开关；0间隔+跟踪被入口拒绝。
- 原供图测试带8条跟踪记录，模型文本≤8192B、轨迹投影有界、不改变拼图图像
  数/消费ACK；journal超容量有明确错误，clear释放视图。

开发轮次：最初跟踪测试71检查有2项fixture失败：前台使用128²图却返回位于
208px的OCR框；错误期待识别坐标变化后仍保留uncertain状态。修正为合法320²
前台图，并断言连续性中断/重建。另有range-loop复制被-Werror拒绝，改const引用。
专项8/8通过后补充异常清池/停止释放；编辑异常保护期间ASAN编译读到尚未收口的
function-try-block，修正后重新构建。上述失败轮次不算通过。

ASAN首轮还在既有companion隔离测试暴露2个断言失败：前台sample被拒绝并
唤醒future之后、worker清理in_flight之前，紧接提交的后台sample被误报
foreground busy，随后query也可能被拒绝。RequestState新增先于promise发布的
settled原子状态，准入只把未settled前台命令作为忙；补24轮连续前台拒绝→后台
采样的回归。修复后对最终源码重新构建/验证各矩阵，不将此轮计为通过。

全仓格式检查仍停在未修改的desktop_action_verification.cpp:490；本轮文件
单独检查，完整门禁失败如实保留，待独立格式清理后由Mirage维护者补跑。

## 边界与后续

这是默认关闭的有界外观/几何观察接线，未完成M9-27全部退出条件。尚未实现
全局运动补偿、远距离候选身份关联、结构与外观的身份联合验证、对象级精确
文字reuse、慢识别分离；全局布局先重置而非补偿。只靠NCC不能区分所有视觉
相同对象交换，因此跟踪位置不放行动作、不继承语义标签。

Windows/MSVC、授权Portal/真实桌面、固定模型视频内容、5/10Hz、CPU/RSS/
漏报/token/完成率A-B未执行。负责人Mirage维护者；目标runner、桌面、固定
任务/模型和硬件就绪后按M9-27/28/33补跑，相关勾选保持开放。
[MIRADOR-20261027-001](../dependency_feedback/mirador.md#mirador-20261027-001)
精确跨帧证据reuse仍Open；本批公共tracker能力足以承载，不升级或修改依赖。
