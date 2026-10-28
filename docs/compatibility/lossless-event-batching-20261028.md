# M9-29 普通事件无损批次验收

> 状态：Completed（M9-29批次增量，本地契约与脚本模型范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-29/18](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-130](../decisions/DEC-130-lossless-event-batching.md)
> 基线提交：372e1d4

## 行为与承载

Mira adapter沿既有supplier读取PerceptionView，新增私有同步批次格式化函数，
不创建线程、队列、定时器、像素缓存或模型；未修改固定依赖。已核对Mirador
公开SemanticChange：AppearanceChanged在产品事件中兼含位置/外观变化，因此
不能仅凭同文字/身份丢弃中间样本。这里保留原模型可见的逐次cursor、毫秒年龄、
帧ID可用性及完整框，共享相同类型/文字/确认/身份字段。批次额外提供帧序号。

最多8样本/行、首尾≤1秒，连续cursor、时间/帧序号不倒退、来源/身份/实例/文本
相同且确认；resync不组批。内容/关系变化、出现/消失、未确认观察、来源重置与
媒体关键帧不分组、不跨越。原始256条/256KiB日志和8帧预览容量不变。
单次最多16行，事件数组含括号/分隔符≤7000B，整个供给≤8192B；仅提交可容纳
前缀，外层裁整行后重新取最后cursor。ACK沿已完成且实际含供给的模型请求推进，
未完成、未呈现和未发送部分不消费；其他消费者无改变。

## 可复现结果

- perception_semantics_test：受控24次位置/外观观察及1次文本屏障，共25次观察，
  原平面25行6792B，批次4行3798B，约减少44.1%的事件JSON字节。完整恢复25个
  cursor及中间框、年龄、帧序号、8个可用ID；屏障保持独立。原16行限制下这25条
  需多次供给，本次4行可完整容纳；不将全部平面25行误报为旧单次请求。
- 同测试覆盖关键类型、文字/身份/实例/来源/确认、时间倒退/未来、frame倒退、
  gap与resync断组；96条长文本变化加1屏障触发7000B预算，逐前缀还原97条，
  无跳过/重复，原日志不变。
- desktop_image_supply_test经真实DesktopImageTurn/脚本模型验证：自动供给4行、
  请求未完成及实际输入未包含观察时ACK=0，完成后ACK=25；无额外capture/input。
  用大场景投影触发外层8KiB裁剪，实际cursor小于格式化器提供cursor，只确认
  最终呈现的连续前缀，未发送样本留在原日志。

这是受控序列化与请求生命周期证据。未测在线LLM token、模型调用次数、理解质量、
完整进程CPU/RSS或真实视频收益；既有真实模型精度/资源验收不能代替本轮这些证据。

## 实际执行

| 最终源码配置 | 结果 | 日志 |
| --- | --- | --- |
| Debug完整 | 120/120，340.35秒 | /tmp/mirage-m9-batch-fixed-debug-test.log |
| ASAN专项 | 8/8，27.68秒 | /tmp/mirage-m9-batch-fixed-asan-test.log |
| UBSAN专项 | 8/8，27.82秒 | /tmp/mirage-m9-batch-fixed-ubsan-test.log |
| TSAN专项 | 8/8，60.81秒 | /tmp/mirage-m9-batch-fixed-tsan-test.log |
| native-release专项 | 8/8，15.98秒 | /tmp/mirage-m9-batch-fixed-native-release-test.log |
| 服务/原生前端 | native-release构建通过 | /tmp/mirage-m9-batch-fixed-native-release-build.log |
| 格式/公开边界 | 67头零违反，DEC-096既有两项seam；私有头格式通过 | /tmp/mirage-m9-batch-fixed-debug-build.log |

专项为6业务测试及setup/cleanup两项。首轮Debug完整120/120（339.35秒），四配置专项
Debug/ASAN/UBSAN/TSAN各8/8。首轮native-release未进入测试：GCC13 -O3 -Werror
在JsonValue临时值set帧序号处报maybe-uninitialized。调整为构造Object时写入
帧序号、单条时移除该字段，再构造JsonValue；未关闭警告、修改依赖或输出契约。
原失败保留在/tmp/mirage-m9-batch-final-native-release-build.log；修正后服务/前端
构建与native-release专项8/8（15.98秒）通过。最终源码重跑结果如上表。

```sh
cmake --build build/debug -j2 --target perception_semantics_test desktop_image_supply_test task_perception_test significance_gating_test perception_storyboard_test continuous_perception_consumer_test mirage-format-check mirage-boundary-check
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/native-release -R '^(perception_semantics_test|desktop_image_supply_test|task_perception_test|significance_gating_test|perception_storyboard_test|continuous_perception_consumer_test)$' --output-on-failure
```

ASAN/UBSAN使用对应build目录同专项；TSAN在setarch x86_64 -R下运行。
多个独立配置并行执行，耗时仅作运行记录，不比较性能。未扩大超时/屏蔽竞争报告。

本地out/m9-event-batching-20261028/source.json记录5个代码/测试文件hash，
总SHA256为1d214eab7ed08792e115ceb3c550532c6fa177552344d2a3464027a41fe3c104；
同目录保存验证日志、首轮编译失败与字节测量，未作为源码提交。

## 未执行与后续

MSVC、交互Windows/原生Portal、在线模型/真实视频任务、低端设备性能本轮未验，
负责人Mirage维护者，在对应环境和可复现任务就绪后补跑。M9-29多目标重要性、
按语义去噪/唤醒、跨未知帧精确继承，以及M9-30内容时间账本仍开放。未安装或
重启用户服务，不把本地构建等同已更新运行中的二进制。
