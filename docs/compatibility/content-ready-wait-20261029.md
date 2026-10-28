# M9 内容可供图等待验收

> 状态：Completed（本机契约与回归范围；M9整体仍进行中）
> 日期：2026-10-29；负责人：Mirage维护者
> 计划：[M9-30/29](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-136](../decisions/DEC-136-content-ready-wait.md)
> 基线提交：10d74a0

## 行为与证据范围

wait 1.1.0可选until=content_ready，仅用于已声明自动内容任务。复用原feed.wait，
在一次既有有限工具调用中等待门禁和2张新缓存帧，最多30秒/64次内部等待；
不采图、不编码、不新建模型/后台任务，不ACK或删除事件。只合并生产路径明确
标注的媒体关键帧，保留可见非媒体/关键事件和来源失效/取消的处理机会。
原feed注意过滤仍适用；显式延后的事件在下一feed唤醒或期限后检查。

新增公开PerceptionEvent.temporal_keyframe默认false，仅两条现有媒体关键帧分支
设true；journal保留，JSON只在true时输出。自定义feed未设置则保守唤醒。公开
结构消费者需随版本重编译；旧event等待、未开启自动供图保持原行为。媒体启发式
标记不是语义重要性的证明，content_ready不是供图/理解或完整视频完成的证明。

| 可控时间线 | 工具内feed等待 | 外部工具调用/返回 | 保留事件/预览 | 提前消费 |
| --- | --- | --- | --- | --- |
| t0刚供图门禁，t1…t4各1媒体帧，t5门禁到期 | 4次event＋1次timeout | 1次 / content_ready | 4 / 4 | 0 |
| 门禁已就绪，t1单帧、t2第二帧 | 2次event | 1次 / content_ready | 2 / 2 | 0 |
| 两帧已就绪，同时可见非媒体或伪标记事件 | 0 | 1次 / event | 原样 | 0 |

上述使用单调可控时钟与脚本feed，没有真实5秒sleep，不据此换算线上LLM调用数、
延迟、CPU/RSS或token收益。真正supplier/guard/Completed集成验证就绪工具本身
无图/无ACK，下一现有请求附1张2格拼图，Completed后才消费；额外capture/input为0。

## 验证内容

- 媒体事件多次内部唤醒、5秒边界/2帧门槛、30秒总deadline、未来游标不回退，
  就绪不消费、原事件和弱预览保留。
- 假keyframe文字、错误类型/confirmed/对象身份/来源/帧号/ID/时间/几何均不合并；
  非媒体事件先于就绪返回。来源代际/drop/resync/替换、gap、注意失效、失败/停止/
  degraded、容量、任务已评估、取消和底层失败状态明确退出。
- feed无进展event、提前/无进展timeout、64轮耗尽有明确状态；timeout允许底层毫秒
  取整1ms差异，没有私有sleep/调度器或无限重试。新时间测试全用可控时钟。
- 真实工具参数/旧event兼容、内容未开启、来源退役、权限取消，自动供图到实际
  Completed；两条真实媒体生产分支和journal/事件JSON的标记保留、普通批次屏障。
- 原ModelLayer自动/手动流程与失败收口回归，任务注意隔离与连续消费者路径回归。

## 执行环境与命令

Linux 7.0.0-38-generic x86_64，Ubuntu GCC13.3.0，CMake3.28.3，Ninja；
依赖pin/锁不变。Debug/TSAN无原生前端，ASAN/UBSAN沿现有缓存启用原生前端，
以上均PPOCR/BUNDLE_VISION=OFF；native-release为Release，原生前端、PPOCR、
BUNDLE_VISION=ON。没有安装部署、服务重启或在线模型访问。

```bash
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/asan -j1 --target desktop_image_supply_test perception_storyboard_test significance_gating_test perception_semantics_test task_perception_test continuous_perception_consumer_test tool_alias_provider_test
ctest --test-dir build/asan -R '^(desktop_image_supply_test|content_visual_supply_test|perception_storyboard_test|significance_gating_test|perception_semantics_test|task_perception_test|continuous_perception_consumer_test|tool_alias_provider_test)$' --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mira/src/content_ready_wait.hpp integration/mira/src/content_visual_supply.hpp integration/mira/src/perception_event_batch.hpp
```

UBSAN/TSAN/native-release构建/专项同上；TSAN的ctest前加setarch x86_64 -R；
native-release另构建mirage-service、mirage-native。专项含8业务加setup/cleanup，
不扩大原超时、不跳过测试；各配置构建后串行测试，避免构建资源竞争。

首轮纯逻辑5/5，1.92秒。初次扩展专项6/7：新测试错误要求非法参数仅以Result
错误返回，但现有registry亦允许failed工具结果；修正为接受原两种失败表达，
生产参数校验没有放宽。随后补齐提前timeout无进展检测（容忍1ms取整）；二轮
时间/自动供图专项4/4，0.86秒。复核后将初始view缩为epoch/drop标量，避免等待
期间留存第二份事件快照，最终源码重新构建验证。

## 最终结果

| 配置 | 结果 | ctest耗时 |
| --- | --- | --- |
| debug | 121/121，无跳过 | 356.98秒 |
| asan | 10/10，无跳过 | 33.95秒 |
| ubsan | 10/10，无跳过 | 32.68秒 |
| tsan | 10/10，无跳过 | 83.95秒 |
| native-release | 10/10，无跳过 | 16.79秒 |

各配置最终构建均成功；native-release同时构建mirage-service与mirage-native。
格式检查通过；公开边界67头零违反，保留DEC-096既有两项seam；三私有头格式通过。
未扩超时、未跳过测试。最终代码与测试摘要在执行前后及暂存区一致。

冻结的11个代码/测试文件总SHA256为
7129d8231fd45eb3e94bd94ae1944b981f1f88ee0c2ae0ee39e2ad5a0259eb7a。
原始日志前缀/tmp/mirage-m9-content-wait-；源码摘要、验证/提交manifest及日志副本在
ignored的out/m9-content-wait-20261029，本机保留30天后由维护者清理，不入提交。
可按上方命令和基线/源码摘要复现；耗时只记录执行，不作为性能比较。

## 未执行门禁

真实在线LLM主动采用、真实视频内容正确率/图像token/调用量和CPU/RSS A/B未验；
播放器身份、进度/暂停/跳播/结束、声音和完整视频覆盖未交付；真实Windows/MSVC
及原生Wayland Portal目标交互未验。负责人：Mirage维护者；补跑条件：授权模型/
媒体与目标平台，接入可信覆盖来源后测质量/成本。M9-29自动语义重要性与跨未知帧
继承、M9-30完整内容闭环继续In Progress。就绪后缓存可能退役、预算不足或受原
图像/文本步骤准入限制，后续supplier仍可不给图，必须保留原guard/Completed证据边界。
