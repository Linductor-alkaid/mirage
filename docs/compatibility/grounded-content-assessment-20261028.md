# M9-30 显式内容任务评估与产品收口验收

> 状态：Completed（M9-30第二增量；本机契约/受控请求范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-30/24](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-133](../decisions/DEC-133-grounded-content-task-assessment.md)
> 基线提交：d96f30a

## 交付范围

desktop.vision.content支持begin/assess/status，显式声明observed_samples/entire_video
和requires_audio；同turn不能降级范围或声音要求。状态为idle→locating→observing_content
→coverage_assessed/insufficient，来源或引用失效后interrupted；同配置begin显式重开。
这是单个DesktopImageTurn中的产品内容策略，Mira继续承载Task/Session/ConversationLoop，
复用原串行owner、Completed供图回调与ModelLayer产品settlement，无新并发/调度或依赖修改。

引用由task/session/epoch、来源段、frame ID/sequence/原单调时刻摘要形成。发图元数据
可给待消费content_ref，但只接纳DEC-132实际已完成、当前段32样本窗口内的引用；不凭
工具成功、未供图片、播放文字或任意ID证明内容。不同任务、来源/epoch ABA、窗口淘汰
不能复活旧证据。新样本即使不改变32样本数量，也会撤销既有评估；完全相同的重试幂等。

observed_samples且不需声音的answerable必须有非空≤1024B回答、非空≤512B限制和1–4个
有效不同引用。整个视频或声音缺少可靠覆盖证据，自动成为insufficient并丢弃拟回答；
主动insufficient可无引用。已激活任务若未评估/不足/中断，成功终答被产品层改为失败，
错误解释缺少什么。有效片段回答只发布有界assessment并固定披露稀疏性；自由终答不能
扩大已声明范围。原动作失败/取消优先，内容评估不能复活任务。未begin的任务保持原行为。

每任务新增≤4KiB元数据，不保留像素或请求历史；content_task自动摘要≤512B，status至多
4引用且JSON≤1KiB（包含最坏转义预算），完整连续观察≤8KiB，原图像/日志容量不增加。
没有增加固定LLM调用或主动截图。协议见[示例](../examples/grounded-content-assessment.md)。

## 验证内容

- 纯契约：未激活/未消费/跨命名空间拒绝；显式声明、幂等与范围不可降级；引用伪造/
  重复、空回答/限制、字节上限与非法结论原子拒绝，拒绝不改此前有效评估。
- 时间/源边界：新样本要求重评；drop/来源退役清评估、同配置重开、旧引用不恢复；
  满32样本继续消费仍使评估失效，被引用帧淘汰后interrupted；转义ID引用回包≤1KiB。
- 真实工具及供给guard/Completed回调：图片工具成功而未消费时不能引用；实际供图后
  可评估；状态引用回传、来源改变及错误owner/取消拒绝；完整连续供给预算、零额外输入/截图。
- 真实ModelLayer循环、受控模型提供器：begin→storyboard→完成供图→assess→自由终答。
  sample只展示受控评估回答与稀疏说明；entire_video/audio/unassessed/retired失败且抑制
  自由“看完并听清”声明；评估成功后取消仍以取消收口。使用4×2合成画面验证协议边界，
  不把受控文本当作真实视频理解成绩。

## 实际执行

| 最终源码配置 | 实际结果 | 日志（前缀/tmp/mirage-m9-content-assessment-） |
| --- | --- | --- |
| Debug完整 | 120/120，393.76秒 | final-debug-{build,test}.log |
| ASAN专项 | 首轮10/11，173.78秒；唯一超时项复跑3/3，20.22秒 | final-asan-{build,test}.log、asan-rerun-test.log |
| UBSAN专项 | 首轮10/11，107.79秒；唯一超时项复跑3/3，19.93秒 | final-ubsan-{build,test}.log、ubsan-rerun-test.log |
| TSAN专项 | 11/11，310.48秒 | final-tsan-{build,test}.log |
| native-release专项 | 11/11，46.14秒 | final-native-release-{build,test}.log |
| 服务/原生前端 | native-release构建通过 | final-native-release-build.log |
| 格式/公开边界 | 67头零违反；DEC-096既有两项seam；私有头格式通过 | format.log、boundary.log、private-format.log |

专项包含9个业务测试及setup/cleanup。ASAN/UBSAN首轮均只有desktop_image_supply_test
触及既有30秒超时；其余业务通过。结束本轮大部分并行负载后，保持源码、断言与超时
不变分别复跑该业务及setup/cleanup，19.89/20.16秒通过，无Sanitizer诊断；未把首轮
改写为完整11/11通过。耗时仅记录运行条件，不作为性能比较。

首轮编译发现拼图Json数组是只读视图，改为有界复制后写回；第二次发现新工具schema
变量与handler局部变量同名触发-Wshadow，修正命名。首轮专项3/4：新受控模型工具的
arguments_digest误用canonical排序摘要，与模型协议的payload序列化摘要不一致；修正
测试提供器为digest_string(to_json_string(arguments))，保留生产请求校验。第二轮专项
4/4，14.89秒（两业务及setup/cleanup）。未放宽断言、timeout或校验。

```sh
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/native-release -j2 --target desktop_image_supply_test task_perception_test perception_semantics_test perception_storyboard_test continuous_perception_consumer_test significance_gating_test layered_perception_test runtime_service_test harness_desktop_tools_test mirage-service mirage-native
ctest --test-dir build/native-release -R '^(desktop_image_supply_test|task_perception_test|perception_semantics_test|perception_storyboard_test|continuous_perception_consumer_test|significance_gating_test|layered_perception_test|runtime_service_test|harness_desktop_tools_test)$' --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mira/src/content_task_assessment.hpp integration/mira/src/perception_content_evidence.hpp integration/mira/src/desktop_image_supply.hpp
```

ASAN/UBSAN同专项使用对应目录；TSAN使用setarch x86_64 -R。本地
out/m9-content-assessment-20261028/source.json记录11个构建/代码/测试文件摘要及依赖锁；
总SHA256：4238f96a3857d8c8b6b24c787dde0ac50735c2e1f7f6af1e569b715fbebf4e96。原始日志与验证manifest保存在该ignored目录。

## 未执行与后续

M9-30整体继续In Progress。此门禁依赖模型显式begin：漏声明或错误解释用户目标仍可能
绕过领域检查；semantic_correctness_verified=false，主机只证明引用归属和有限要求，
不证明回答内容正确。普通capture/OCR、跨turn图片未进入本证据账。未提供播放器身份、
进度、起止/暂停/跳播/结束与声音证据，不可验收完整视频完成率。

未执行真实视频/在线模型、真实Windows/MSVC或原生Wayland Portal交互、实际播放控制、
音频或端到端性能精度。负责人：Mirage维护者；补跑条件：可授权媒体/模型及目标平台，
先接播放器覆盖来源，再测声明是否忠实、内容正确率、语义漏报与计算开销。未更新用户
运行服务、未安装、未部署或宣称这些门禁完成；M9-29自动语义去噪与跨未知帧继承仍开放。
