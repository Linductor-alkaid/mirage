# 连续视觉无帧状态查询验证

> 状态：In Progress
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-18；配套 M9-05/07/11/12
> 基线：Mirage 34bf01a 的后续工作树，feat/continuous-perception-cycle
> 依据：DEC-091、DEC-109 本轮无帧查询实施边界

## 范围与承载

Mirador `2a9f4830f4727498ed4cbad568a1d3ec357ece7e`、Mira `a29374a` 不变。
首次 fetch 遇到 TLS handshake 中断，重试成功后确认 master 仍为同一 pin；未修改
依赖源码/锁文件。按 pinned Mirador By API → Fusion and snapshots 和 kairo
By API → Blocking I/O 路由核对公开接口，未读取无关能力卡或实现源码。

query_continuous 在原 worker 上消费 query_temporal_state/query_focus_states 与
可选 drain_events；复用原有 latest-wins mailbox，没有新线程、队列或 timer。
结果单独标记 kQueried/queried；没有 frame、snapshot、WorkPlan 或新事件确认。
输入策略最多 16 条条件，vector/string capacity 在投递前按协调器字节上限
（默认 512 KiB）检查；结果沿用实体/焦点/事件上限并单列字节计量。

接口/年龄/策略更新/关闭契约和示例见[消费设计](../design/continuous-visual-analysis.md)。
查询会替换旧命令，owner 应等待 future 后恢复帧供给，并在后续帧请求携带新策略。
这不是并行事件订阅协议，也不以读取历史状态授权输入。

## 自动化验证

Linux x86_64、GCC 13.3.0；真实 kairo worker 与 model-free backend：

- 两帧确认后，无新图/无 OCR 的文本焦点查询立即命中；切到未见文本保持显式
  unresolved，确认帧不变。只读 peek 不失效比较基线，策略改变使下帧重新分析。
- 回显 source revision/epoch、policy revision、最后完成分析序号与年龄；skip/query
  不推进完成时间。查询抢占在途 OCR 后没有完成年龄，新源识别失败后也是 unknown。
- 未建立/旧源、legacy host、过量条件、保留大容量的 policy、非法条件、预取消、
  到期和关闭请求显式失败；无 queried/当前快照，已提交策略不被失败请求修改。
- 新源重建后识别失败：默认 peek 保留旧 epoch 的 source-reset end；显式 drain
  取得带原来源的事件，再 drain 为空，不生成虚假消失或重复确认。
- 三个受控生产者，每组 96 次混合分析/查询，共 288 次；分别在正常、Host stop、
  Executor 先 shutdown 情况下，每个 future 都在测试期限内结算。fixture RAII join，
  断言只在 owner 线程执行，不以脚手架线程代替被测 worker。

发布现在以 try_publish 和 queued 归属共同排序；无版本槽/关闭拒绝，不取消之前
的有效命令。worker 每轮只读一次 latest，使持续发布时停止仍可观察。对最后一轮
关闭检查补齐 queued 的取消信号，覆盖 stop 检查和拾取之间的窗口。

```sh
cmake --build --preset debug -j 4
ctest --preset debug
# 对 asan、ubsan、tsan、native-release 各运行；TSAN 的 ctest 前加 setarch x86_64 -R
cmake --build --preset asan --target continuous_perception_consumer_test continuous_capture_test visual_session_test visual_session_budgeted_test visual_observation_loop_test -j 2
ctest --preset asan -R '^(continuous_perception_consumer_test|continuous_capture_test|visual_session_test|visual_session_budgeted_test|visual_observation_loop_test)$'
clang-format --dry-run --Werror integration/mirador/include/mirage/integration/visual_session_host.hpp integration/mirador/src/visual_session_host.cpp integration/mirador/src/visual_observation_pipeline.cpp tests/integration/continuous_perception_consumer_test.cpp
cmake --build --preset debug --target mirage-boundary-check
cmake --build --preset debug --target mirage-format-check
git diff --check
```

## 实际结果与限制

ASAN、UBSAN、TSAN、native-release 最终定向各 7/7（含隔离 setup/cleanup）通过；
连续感知专项 1,908 checks 零失败。TSAN 仅测试子进程使用 setarch -R，没有
sanitizer 报告。补齐最后的关闭窗口后，Debug 最终全量 **110/110，通过，303.49 s**。
变更的四个 C++ 文件格式通过；65 个公开头零边界违规（DEC-096 两个既有 seam
保留），9 份 Markdown / 415 个本地链接、标题/围栏、M9 编号/开放状态与 pin/lock
检查通过。git diff --check 通过。

没有实际模型/桌面资源收益、4K 多源长期 soak、Windows 或真实 Wayland/pet 验证；
生产 native/IPC/模型供给、订阅与 gap 恢复仍未接线。负责人 Mirage 维护者，在平台/
消费者、设备/授权与预登记阈值齐备后，由 M9-11/12 分项补跑，不关闭 M9 整项。

仓库全量格式门禁仍在已知的基线
model_layer.cpp:1528 问题保持未修改，由截断分类维护分支修正后补跑，详见
[原审计](../supply-chain/mirador-continuous-perception-20261022.md)。
