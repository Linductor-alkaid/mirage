# M9-29 多对象显式唤醒优先级验收

> 状态：Completed（M9-29显式多目标增量，本机契约与脚本模型范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-29/18](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-131](../decisions/DEC-131-multi-object-wake-priority.md)
> 基线提交：8b93b58

## 实现与能力边界

工具targets形式与旧形式互斥，每目标由已完成请求实际供给表认证，FeedLease再
按当前来源/context/身份/内容/年龄重验，全部有效才原子替换。复用原TaskAttention、
快照、可取消wait与串行任务owner；未改固定依赖、采样、模型或全局权限。
Mirador公开稳定ID及SemanticChange不承载消费者任务意图，位置与外观共用事件
类型，不足以推断语义相同；本轮执行模型显式wake/observe意图，不新增视觉算法。
已核对Mirador integration fusion/snapshots卡及公开snapshot/continuous事件头，
kairo communication卡；没有新增跨上下文通信、worker、队列、定时器或无主任务。

最多4对象、至少1个wake，无像素、保留字符串/目标≤4KiB。新objects模式保留
全部原始事件和预览，仅对准确observe身份、已确认且不与wake区域相交的位置/
外观变化延后wait返回event；未知、非目标与其他类型均保守唤醒。原始订阅仍可
被此类事件唤醒，任务wait在原期限内继续；不忙轮询模型或截图，不承诺零本地
元数据工作。任一目标失效使整组单向退役并明确唤醒，未宣称跨未知帧版本有效。

自动供给新增attention.targets，保留身份、版本、框和priority，不重复语义标签/
请求ID。日志256条/256KiB、8预览、16输出行、事件7000B和完整8KiB预算不变；
按原顺序和实际呈现前缀ACK，不将重要性用于重排或跳过。关键事件立即触发wait
并不保证它在同次字节预算完整呈现，需要继续处理截断、gap与未发送部分。

## 验证用例

- task_perception_test：私有策略逐目标移动；容量1–4及第5个/空/重复/全observe/
  旧版本拒绝且旧策略保留；同ID不同实例/来源、未确认、未知身份、非目标、
  无效几何及关键类型不延后；与wake区域重叠也唤醒。
- 同测试逐一改变次要目标的内容/实例/缺失/pending、scene未知、deferred、来源/
  context/drop/年龄/失败：整组失效、清框、不ABA复活；默认旧feed拒绝新接口。
- 两个真实ResidentDesktopPerception租约共享受控同步feed：observe事件保留，
  第一个wait超时，另一个全局消费者立即event；共享日志未被ACK删除。等待中
  先送observe事件再送wake事件，仅后者结束任务wait，原始订阅恰调用两次；
  次要内容变化无新cursor仍attention_invalidated；取消前/等待中均返回cancelled，
  无共享producer focus调用。
- desktop_image_supply_test：经真实工具注册/执行与DesktopImageTurn自动供给，
  未完成/实际供给被移除不授权；4目标成功、请求归属正确；无效参数、重复、
  过量、全observe、无效priority/ref、混合形式、外任务、过期与接管/取消拒绝；
  不支持feed返回false。4目标投影与96条长事件共同触发字节预算，延后目标仍
  自动提交，只ACK实际供给前缀，97条原始日志保持，capture/input均0。

用例通过不等于在线模型选择了正确重要性。本轮是受控feed/脚本请求；未测新的
全进程CPU/RSS、在线token/调用次数或视频理解质量，不沿用此前资源数字作证明。

## 实际执行与原始失败

| 最终源码配置 | 结果 | 日志 |
| --- | --- | --- |
| Debug完整 | 120/120，345.41秒 | /tmp/mirage-m9-multi-final-debug-test.log |
| ASAN专项 | 11/11，147.26秒 | /tmp/mirage-m9-multi-final-asan-test.log |
| UBSAN专项 | 11/11，146.24秒 | /tmp/mirage-m9-multi-final-ubsan-test.log |
| TSAN专项 | 11/11，302.52秒 | /tmp/mirage-m9-multi-final-tsan-test.log |
| native-release专项 | 11/11，45.81秒 | /tmp/mirage-m9-multi-final-native-release-test.log |
| 服务/原生前端 | native-release构建通过 | /tmp/mirage-m9-multi-final-native-release-build.log |
| 格式/公开边界 | 67头零违反、DEC-096既有两项seam；私有头格式通过 | /tmp/mirage-m9-multi-{format,boundary,private-format}.log |

专项含9业务及setup/cleanup两项。工具接线完成后的Debug专项4/4（18.41秒，含2个
业务及setup/cleanup）通过，之后追加等待中取消及默认接口拒绝并启动最终矩阵。
首个构建因嵌套JSON schema括号错误失败，拆成明确的reference/target schema后
修正；工具用例初稿未接入main，被-Werror=unused-function拒绝，随后用既有
Executor submit_auto + get接入。原日志/tmp/mirage-m9-multi-first-build.log与
/tmp/mirage-m9-multi-tool-build.log保留，未将未执行用例写成通过。

```sh
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mirador/src/task_attention.hpp
cmake --build build/native-release -j2 --target desktop_image_supply_test task_perception_test perception_semantics_test perception_storyboard_test continuous_perception_consumer_test significance_gating_test layered_perception_test runtime_service_test harness_desktop_tools_test mirage-service mirage-native
ctest --test-dir build/native-release -R '^(desktop_image_supply_test|task_perception_test|perception_semantics_test|perception_storyboard_test|continuous_perception_consumer_test|significance_gating_test|layered_perception_test|runtime_service_test|harness_desktop_tools_test)$' --output-on-failure
```

ASAN/UBSAN使用对应build目录同专项，TSAN使用setarch x86_64 -R。多配置并行，
时间仅为运行记录，非性能比较；不修改超时或抑制竞争报告。

本地out/m9-multi-attention-20261028/source.json保存7个代码/测试文件hash，
总SHA256为286232a89f58bc63e95a68478bf92fefccfe108089ee57a5f623fed647e47d5d；
该目录保留日志和原始编译失败，不作为源码提交。

## 未执行与补跑

MSVC/交互Windows/原生Portal、在线LLM与真实视频任务、低端设备资源本轮未验。
负责人Mirage维护者，在对应环境、授权任务/固定语料就绪后补跑。当前layered
CV-only未知周期会保守退役整组，应按任务需要采用空间关注；自动重要性推断、
语义去噪/升级、跨未知帧精确继承及M9-30内容时间账本继续开放。
没有安装/重启用户服务，本地构建不表示运行中的二进制已经更新。
