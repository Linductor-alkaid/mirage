# M9-30 实际供图时间证据账验收

> 状态：Completed（M9-30证据首增量，本机契约与受控请求范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-30/24](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-132](../decisions/DEC-132-delivered-visual-evidence-ledger.md)
> 基线提交：76f02e1

## 已实现范围

retained_visual_evidence随既有连续观察自动进入下一次LLM请求，记录本次
DesktopImageTurn内实际呈现给已完成请求的历史事件帧/拼图样本。Mira的既有
ObservedProvider只对Completed、未取消/过期、非内部IME请求调用consumed；
本轮再要求通过validate_request的request ID与实际图工件完整身份匹配。图片
生成、工具回执、文字、播放状态、未完成/失败/缺图/预算扣留都不使帧数增加。

来源段最多保留32个样本ID/帧号/原单调时刻，不保留图像/弱引用或请求历史；
另有最多4个样本offer与一个请求ID。账本≤8KiB元数据，投影≤1KiB，整体自动
供给仍≤8KiB。32条后保留时间最新窗口、history_truncated=true，不报告伪造的
全任务unique总数。乱序消费可以插入，重复呈现不增加数；提供首末年龄、保留
观察span、相邻样本最大间隔。经过更多时间只增长年龄，不增长已观察span。

源feed替换、epoch、drop/resync新屏障、失败/停止/退化或权限改变退役当前段，
清当前样本并留下原因；旧offer迟到或epoch ABA不能复活旧段。恢复后只有新的
实际完成请求可以重新计数。图片替换clear只清offer/待请求绑定，保留已有账；
任务结束随owner释放，不跨harness turn/会话持久化，不增加线程、队列或定时器。
Mirador coverage是识别区域有效性，非模型供图/视频观看证明；复用现有Mira消费
边界和kairo生命周期，不实现第二套视觉覆盖或调度，不修改pinned依赖。

## 关键验证

- perception_storyboard_test：只offer不计数，Completed消费计数；重复/乱序；
  观察span与间隔、时间推进只增年龄；非法ID/帧号/时刻、超期、相同身份冲突/
  时序矛盾和批次容量原子拒绝；缺事件/预览/来源不接受；32样本窗口及淘汰后
  老帧重供给不增加窗口；源/drop/resync/失败/停止/退化/权限及lease替换，
  迟到与ABA屏障、恢复后新offer可记录，摘要≤1KiB。
- desktop_image_supply_test：真实storyboard/event_frame工具、自动supplier、
  validate_request及ObservedProvider消费回调。失败/Incomplete/Cancelled、缺图、
  context budget扣留、外任务/过期、工件字节/媒体/detail伪造不记账/不ACK帧。
  一次拼图记录2样本，后续单帧到3、4；图片替换不丢账，事件/播放状态不扩大span；
  源gap清当前段且旧响应不复活；零额外capture/input、完整供给≤8KiB。
- 内容寻址工件专项：不同观察时刻的单帧像素相同，旧请求即使具有相同artifact
  ID/digest，也不能为新帧记账；当前通过guard的request ID才可消费。旧拼图epoch
  改变后再恢复原值仍拒绝，必须重新构建/供给/验证新请求。
- event_frame补齐同源事件、原观察时刻/非零帧号、未来时刻与权限校验；不能用
  跨源/无事件预览充数。旧合成TaskFeedFixture补真实source/frame/time关联，未
  放宽生产检查。历史预览弱引用不被账本延长。

这些是受控契约与请求边界证据，不能证明模型理解正确或看完整个视频。scope为
retained_event_frames_only，lifetime为current_harness_turn；普通capture/OCR及
旧turn的图片未纳入本账。unobserved不意味着全上下文从未出现任何其他图像。
即使sparse_observation也保持continuous_coverage=false、audio=false、
complete_video=false、content_understanding=not_verified。>=120秒的迟到样本
不录入当前段，last_rejection解释原因；不把未录入误称为从未发送。

## 实际执行与失败记录

| 最终源码配置 | 结果 | 日志 |
| --- | --- | --- |
| Debug完整 | 120/120，349.22秒 | /tmp/mirage-m9-evidence-final-debug-test.log |
| ASAN专项 | 11/11，130.49秒 | /tmp/mirage-m9-evidence-final-asan-test.log |
| UBSAN专项 | 11/11，112.99秒 | /tmp/mirage-m9-evidence-final-ubsan-test.log |
| TSAN专项 | 11/11，243.21秒 | /tmp/mirage-m9-evidence-final-tsan-test.log |
| native-release专项 | 11/11，46.92秒 | /tmp/mirage-m9-evidence-final-native-release-test.log |
| 服务/原生前端 | native-release构建通过 | /tmp/mirage-m9-evidence-final-native-release-build.log |
| 格式/公开边界 | 67头零违反、DEC-096既有两项seam，私有头格式通过 | /tmp/mirage-m9-evidence-{format,boundary,private-format}.log |

专项含9业务及setup/cleanup两项。第三轮Debug专项4/4（14.19秒，2业务+setup/cleanup）
通过，随后冻结源码运行完整矩阵。首轮3/4：旧拼图测试假定epoch ABA后可直接
ACK旧请求；按新契约保留拒绝，新增重新发布/验证请求后成功路径。第二轮3/4：
纯契约发现sync的unavailable判定漏接authorized，修正生产条件后权限撤销清段，
未放宽测试。原日志/tmp/mirage-m9-evidence-{first,second,third}-test.log保留。

```sh
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/native-release -j2 --target desktop_image_supply_test task_perception_test perception_semantics_test perception_storyboard_test continuous_perception_consumer_test significance_gating_test layered_perception_test runtime_service_test harness_desktop_tools_test mirage-service mirage-native
ctest --test-dir build/native-release -R '^(desktop_image_supply_test|task_perception_test|perception_semantics_test|perception_storyboard_test|continuous_perception_consumer_test|significance_gating_test|layered_perception_test|runtime_service_test|harness_desktop_tools_test)$' --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mira/src/perception_content_evidence.hpp integration/mira/src/desktop_image_supply.hpp
```

ASAN/UBSAN同专项使用对应build目录；TSAN使用setarch x86_64 -R。配置并行执行，
耗时仅作运行记录，不比较性能。未扩大timeout、屏蔽竞争报告或更新用户服务。
本地out/m9-content-evidence-20261028/source.json记录8个代码/测试文件hash，
总SHA256：7dd1878374b8a7601f2cbb7e194a28e3850f8f2591e6808683eea33808fb2572。
该目录保留原失败与最终日志，不作为源码提交。

## 未执行与后续

M9-30继续In Progress：播放器时间/媒体身份绑定、暂停/跳播/结束、内容问题及
完整观看状态机未实现；本轮不以证据子状态替代playback_confirmed/answered。
MSVC/交互Windows/原生Portal、在线LLM及真实视频/音频、长期资源/低端设备
未执行，负责人Mirage维护者，在对应环境与固定授权媒体任务/语料就绪后补跑。
不宣称token、调用次数、视频完成率或CPU/RSS改善。
