# M9-28 有界慢识别派发原语验证

> 状态：Completed（仅本机派发原语验证；M9-28保持In Progress）
> 日期：2026-10-28
> 负责人：Mirage 维护者
> 所属：[三层视觉计划](../plans/m9-layered-perception-delivery.md) M9-28
> 决策：[DEC-124](../decisions/DEC-124-bounded-recognition-dispatch.md)
> 环境：本机 Linux；现有 Debug/ASAN/UBSAN/TSAN/native-release 构建目录

## 实现与结论边界

新增私有 `ContinuousRecognitionHost`，由进程唯一 kairo Executor 注册 blocking
worker，传输复用 LatestMailbox。每个源串行调用一个 observer；普通新帧仅替换
latest，不取消在途工作。完成结果未被消费时不开始下一项，避免覆盖待消费事件。
结果和 callback 输入都有原始像素、采集时间、source_sequence、代次与期限；
consumer 必须核对来源和历史年龄，不能重标成最新帧。callback 可在实际 OCR/
融合前将原始时间传给 Mirador，而不是只在完成结果上补时间。

两份像素代次槽，各最长边1600/≤10MiB，消费者保留原帧同样占槽；weak 传输
历史不保留像素。callback 输出事件/对象各≤256、跟踪/候选各≤8，单结果动态
容器和字符串 capacity 合计≤512KiB，固定 DTO/控制块另计；至多两个完成结果
可被保留，总动态元数据上限1MiB。callback 内部临时分配、模型内存和传入但尚未
接纳的调用方图像不计入这项保留预算，完整产品峰值仍须 M9-28/32 汇总。

代次屏障取消旧工作、清除旧 latest/未消费结果，迟到成功也不得发布；stop 先停
接纳、取消和唤醒，再在锁外 join 并释放 callback/pixels。运行与排队期限、后端
异常、失败结果、外部取消、消费背压、拒绝和 superseded 均有明确结果或计数；
Executor 和 comm 保持 worker 与传输事实源。未添加线程、调度器或依赖能力绕行。

这是 M9-28 第一批基础设施验证，**没有接入产品后台主循环**。生产仍同步调用
observe_image，CV 双基线/tracker 尚未迁移到独立采样 owner，ROI/外围重验未交付。
不能据此宣称产品 OCR 不阻塞 CV、已经达到5/10Hz、降低真实模型 token/CPU，或
完整视频内容任务已经验收。生产默认和已有实验开关行为保持不变。

## 源码与复现

新增源文件：

- `integration/mirador/src/continuous_recognition_host.hpp`
- `integration/mirador/src/continuous_recognition_host.cpp`
- `tests/integration/continuous_recognition_test.cpp`

加入 `integration/mirador/CMakeLists.txt` 与 `tests/CMakeLists.txt`。不修改 pinned
代码、依赖指针或锁文件，保留此前视觉与记忆治理工作树；未提交/推送/安装/重启。
以上5项按路径排序，逐项追加路径/NUL/内容/NUL得到 SHA256：
`c14818e3951b6a05bd8cab083306624b006f46d10803e150c1ac484c0d48aa8f`。摘要清单位于
`/tmp/mirage-layered-phase4-source.json`，不覆盖后续改动后的源码。

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|visual_object_tracking|visual_session_test|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|continuous_recognition|significance_gating|model_layer|ppocr_pipeline_test|runtime_service_test' --output-on-failure

cmake --build build/asan --target continuous_recognition_test continuous_capture_test continuous_perception_consumer_test task_perception_test -j 2
ctest --test-dir build/asan -R '^(continuous_recognition_test|continuous_capture_test|continuous_perception_consumer_test|task_perception_test)$' --output-on-failure
cmake --build build/ubsan --target continuous_recognition_test continuous_capture_test continuous_perception_consumer_test task_perception_test -j 2
ctest --test-dir build/ubsan -R '^(continuous_recognition_test|continuous_capture_test|continuous_perception_consumer_test|task_perception_test)$' --output-on-failure
cmake --build build/tsan --target continuous_recognition_test continuous_capture_test continuous_perception_consumer_test task_perception_test -j 2
setarch x86_64 -R ctest --test-dir build/tsan -R '^(continuous_recognition_test|continuous_capture_test|continuous_perception_consumer_test|task_perception_test)$' --output-on-failure

cmake --build build/native-release --target mirage-native mirage-service continuous_recognition_test -j 2
ctest --test-dir build/native-release -R '^continuous_recognition_test$' --output-on-failure
cmake -DROOT_DIR=/home/linductor/mirage -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mirador/src/continuous_recognition_host.hpp integration/mirador/src/continuous_recognition_host.cpp tests/integration/continuous_recognition_test.cpp
```

## 最终验证结果

| 门禁 | 实际结果 | 限制 |
| --- | --- | --- |
| Debug 全量构建/相关回归 | 构建退出0；22/22，47.63s | 相关测试，不是全量CTest |
| ASAN | 6/6，7.94s | 4套业务测试+2套隔离；无ASAN报告 |
| UBSAN | 6/6，7.37s | 无UBSAN报告 |
| TSAN | 6/6，8.91s | setarch；无报告，未加suppression |
| native-release | 前端/服务/新专项构建退出0；3/3，0.65s | 1套新业务测试+隔离；不是实机视频或模型基准 |
| 公共头边界 | 67头，0违规，2处既有DEC-096缝 | 新接口仅在Adapter私有头 |
| 本轮3份C++格式/diff | 通过 | 全仓格式仍在既存未改 desktop_action_verification.cpp:490 失败 |
| 本轮5份文档本地链接 | 284处，0缺失 | 不验证远程可达性或页内锚点 |

新专项最终含269条断言。最终五套日志统一为
`/tmp/mirage-layered-phase4-{debug,asan,ubsan,tsan,native}-verified-{build,test}.log`；
格式、边界各为 `format-full.log`、`boundary.log`（同一前缀）。全仓格式失败
不由本轮引入，不把它写成通过；本轮未清理无关代码。

## 测试覆盖

- 在途阻塞时连续提交30帧，实际只执行首帧与latest，28项显式 superseded；
  不取消在途识别，结果未消费前不执行latest。结果原始像素、序号、时间不变。
- 消费者保留两个结果时拒绝新帧；释放一个后恢复，像素代次和字节统计有界。
- 来源/策略代次屏障取消排队及运行工作，非协作 callback 迟到成功丢弃；已完成
  未消费结果同样清除。外部取消在完成之后、消费之前发生也再次检查。
- callback 异常/失败、运行中取消、排队期限、运行期限（含 callback 自报取消），
  超额容器capacity/字段长度、非法布局/焦点/期限、重复 worker 名称和关闭后
  准入全部覆盖；失败不带出部分语义结果。
- 两受控 RAII join 线程共提交300项，主线程并发消费/撤销代次；终态总账等于
  接纳总数，像素槽≤2，TSAN 检查。此线程仅为 tests 竞态注入豁免。
- 正常 stop 和 Executor 提前 shutdown 均覆盖运行/排队取消，释放 callback
  捕获状态和原帧，重复 stop/start 拒绝保持幂等。
- 实际 `ContinuousCaptureHost` + 合成 capture + 受控慢 observer：识别阻塞期间
  capture 至少推进5次，之后首帧和最终latest分别完成。仅证明调度分离，无真实
  桌面、模型或吞吐基准结论。

## 失败轮次与修正

1. 最初专项一条断言失败：测试要求每个输入带 focus，但末次恢复准入测试未提供；
   补齐 fixture 元数据，未修改产品语义。
2. 首轮 TSAN 报8条同源 lock-order-inversion：新增 start 在 Adapter 锁内调用
   start_worker，而 Executor stop 的 wakeup 在生命周期锁内获取 Adapter 锁。
   改为仅在锁内标记启动，锁外注册 worker，再锁内记录 admission；stop 已在锁外。
   未添加 suppression 或修改依赖。加入重复注册及提前关闭回归后重跑。
3. 收口将 callback 改为直接接收原始 Input，使帧时间在识别前可用；增加对应断言，
   对最终源码重新构建和运行全部本轮门禁。

首轮日志保留在 `/tmp/mirage-layered-phase4-*-complete-*.log`，最终结果使用新的
日志路径，不把修复前的通过结果当最终证据。

## 未执行门禁

| 项目 | 原因、负责人、补跑条件 |
| --- | --- |
| 产品 CV/tracker 独立 owner 与真实 OCR 并行 | 尚未接线；Mirage 维护者按 M9-28 第2步完成原帧基线/模板回流、pending/旧标注隔离后验证 |
| ROI、外围重验和生产配置 | 尚未实现；同负责人按第3步验证空间覆盖、前台优先和故障后才开放实验 |
| Windows/MSVC | 本轮Linux环境；同负责人在Windows/MSVC+目标Backend执行生命周期/资源专项 |
| 真实桌面/模型/视频与5/10Hz/CPU/RSS | 当前为合成fixture；同负责人在固定模型/硬件/可复位任务和预登记阈值就绪后按M9-33配对运行 |

M9-28 保持 In Progress；以上缺失项不勾选，不以基础设施单测代替产品验收。
