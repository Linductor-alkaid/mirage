# M9-30 观察时间窗拼图选帧验收

> 状态：Completed（M9-30第三增量；本机契约/受控请求范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-30/24](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-134](../decisions/DEC-134-observer-window-storyboard-selection.md)
> 基线提交：8ff342a

## 范围

storyboard 1.1.0新增window_ms（100–120000）、可选max_frames（2–4，默认4）；
与frame_ids模式互斥，原显式参数/供图语义保持。只读8预览/256事件，过滤窗口外、
未来、过期、失效弱引用、缺同源原时刻/帧号及当前32样本账已完成的候选。最早/
最新加最远时间分散，平手取早、按时间输出，不把启发式标为语义选帧。

保留既有单PNG、320格/640×640、Mirador resize、source/gap/权限与Completed
证明边界；失败/Incomplete/预算扣留不消费，ACK失败不引发本段已消费帧反复选择。
不足2张、容量/几何/时序歧义明确不足，无新采集、worker、timer、队列或像素缓存。
Mira现有工具循环和supplier未被替换；新增的只是同步有界产品证据选择策略。

长期任务的32样本账已满时，时间早于其最早保留样本的预览也不进入窗口选帧，
即使无法再证明它是否曾消费。这与账本拒绝录入过早样本的边界一致，防止ACK
失败残留的老图在账本滚动后被重新当成新图；代价是可能跳过未读的过早预览。
显式frame_ids模式仍可按需查看这些有效预览，不把历史未知当全局已读计数。

## 验证

- 纯契约：8候选非均匀时刻，2/3/4帧结果、相同时距平手、窗口含边界；已消费
  与未完成重试；弱引用、未来/120秒过期、缺事件/帧号/时间、容量/几何/时序歧义、
  取消与来源/gap段；复用真实拼图打包检查640×640上限。
- 真实工具：参数互斥/类型/容量，候选/选中数、原格位与引用；真实ObservedProvider
  guard/Completed回调，预算扣留/失败/Incomplete可重试；ACK失败保留原预览但不
  再次选中；显式ID可继续按需读、gap可重新观察、epoch ABA旧请求不能消费、权限撤销。
  完整自动观察≤8KiB，零额外capture/input。
- 真实ModelLayer受控提供器：内容begin→window_ms拼图→实际图→引用assess→终答，
  只展示有界片段答案与稀疏说明；原完整视频/声音/未评估/来源失效/取消回归保持。
  这只是协议与合成小画面证据，不代表在线语义正确率或真实内容识别精度。

## 执行结果

| 最终源码配置 | 实际结果 | 日志（前缀/tmp/mirage-m9-temporal-window-） |
| --- | --- | --- |
| Debug完整 | 120/120，326.28秒 | final-debug-{build,test}.log |
| ASAN专项 | 5/5，21.81秒 | final-asan-{build,test}.log |
| UBSAN专项 | 5/5，24.85秒 | final-ubsan-{build,test}.log |
| TSAN专项 | 5/5，51.35秒 | final-tsan-{build,test}.log |
| native-release专项 | 5/5，11.11秒 | final-native-release-{build,test}.log |
| 服务/原生前端 | native-release构建通过 | final-native-release-build.log |
| 格式/公开边界 | 67头零违反；DEC-096既有两项seam；两私有头格式通过 | format.log、boundary.log、private-format.log |

专项含三业务（选帧/打包、真实供图/ModelLayer、工具alias协议）及setup/cleanup。
配置构建结束后串行运行专项以限制并行负载，未扩大既有超时。运行时间仅为记录，
不代表性能比较。

首轮专项4/4，14.58秒。复核发现32样本窗口滚动后，ACK失败残留的过早预览可能
失去“已消费”成员资格而被重选，补before_retained_window过滤及40次滚动测试。
此前配置日志以pre-window-bound保留，不作为最终源码验收；其中UBSAN桌面供图
触及既有30秒超时，其他配置专项通过，未扩大timeout或删减用例。最终验证重新
构建全部受影响目标，按相同断言和超时执行。

```bash
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/native-release -j2 --target perception_storyboard_test desktop_image_supply_test tool_alias_provider_test mirage-service mirage-native
ctest --test-dir build/native-release -R '^(perception_storyboard_test|desktop_image_supply_test|tool_alias_provider_test)$' --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mira/src/perception_temporal_selection.hpp integration/mira/src/perception_content_evidence.hpp
```

ASAN/UBSAN使用相同专项，TSAN加setarch x86_64 -R。源码摘要及实际日志保留于
ignored的out/m9-temporal-window-20261028，6个代码/测试文件总SHA256：
adb917ec1ab61db62b7637f9eeaa75e519ec44acc58df77955b2b7add6050f31。最终源码不含依赖变更。

## 未执行与后续

M9-30继续In Progress：时间分散不是语义重要性，短动作/小字幕可能被漏选，ROI与
真实视频/在线模型A/B、CPU/内存/token收益尚未验收。8预览和120秒上限不构成
指定窗口完整保留；不报告播放器进度、暂停/跳播/结束或声音来源。工具仍由模型
按需调用，不自动唤醒/固定供图，也不保证模型主动声明和正确理解内容任务。

未执行真实Windows/MSVC和原生Wayland Portal交互、用户服务部署或真实媒体播放。
负责人：Mirage维护者；补跑条件：授权媒体/模型与目标平台，先接入可信播放器
时间/结束和声音证据，再评估内容正确率、漏报/重复率与实际成本。M9-29自动
语义去噪与跨未知帧精确继承门禁保持开放。
