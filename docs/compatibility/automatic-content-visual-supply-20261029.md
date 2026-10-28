# M9 内容任务自动供图验收

> 状态：Completed（本机契约与受控模型范围，M9整体仍进行中）
> 日期：2026-10-29；负责人：Mirage维护者
> 计划：[M9-30/29/24](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-135](../decisions/DEC-135-content-automatic-visual-supply.md)
> 基线提交：0aa833b

## 范围与受控对比

content 1.1.0的begin可用auto_storyboard=true显式启用；省略/false保持原手动模式。
提示推荐内容观看启用。既有supplier按需选缓存中的2–4新帧，不再要求取图工具
往返，不添加后台模型请求。构建尝试与Completed后均有5秒门禁，提供retry_after_ms；
相同待消费图片保持同一工件，显式图片优先。评估成功/不足后停止，源/权限/gap
中断需同配置显式重开，幂等begin不绕过冷却。低预算/文本步骤不建图。

复用DEC-134时间窗与DEC-132消费/来源/请求证明，打包函数与手动storyboard共享。
单图槽、320格/640²、原缓存不增；只有一个门禁时刻、原因/错误码，诊断≤256B，
自动观察≤8KiB。无worker、timer、队列、后台请求或新平台/依赖实现。实际自动
供图会退役旧图/输入引用，metadata披露；后续交互需原新鲜输入来源，不误用拼图。

| 受控同两帧问答 | 模型请求 | 取图工具 | 实际图片/样本 | 额外本地capture/input |
| --- | --- | --- | --- | --- |
| 手动begin→storyboard→assess→终答 | 4 | 1 | 1 / 2 | 0 / 0 |
| 自动begin→assess→终答 | 3 | 0 | 1 / 2 | 0 / 0 |

由真实ModelLayer配受控提供器的运行计数断言验证，最终都输出同一有界片段回答与
稀疏说明，不使用自由“完整看完并听清”的终答。它证明减少该流程1次模型往返，
不是实测在线调用率或token收益，图像数量也没有被误记为零。

## 验证内容

- 纯时间门禁用可控单调时刻测试5秒前/边界、Completed延长、不缩短、显式重置；
  冷却提示、失败码和≤256B，任务自动模式不可变、评估停止、中断/重开及≤512B摘要。
- 真实supplier/guard/ObservedProvider：无需图像工具即可拿到有引用拼图；同工件
  重试、失败/Incomplete不消费、Completed记账，ACK失败保留预览；冷却、幂等begin、
  评估停止、gap/旧请求/权限；预算恢复立即供图、显式图片优先、弱引用不足、打包
  失败码、text-only/取消与总输入≤8KiB、零额外capture/input。
- ModelLayer手动/自动对照，自动完整视频/声音不足、未评估、来源失效与取消保持
  门禁。新content_visual_supply_test复用原二进制的受控入口，将独立PNG场景分组，
  原desktop_image_supply_test仍全量运行；没有扩大原30秒超时（TSAN沿既有120秒规则）。

## 实际执行

| 最终源码配置 | 实际结果 | 日志（前缀/tmp/mirage-m9-auto-supply-） |
| --- | --- | --- |
| Debug完整 | 121/121，323.05秒 | final-debug-{build,test}.log |
| ASAN专项 | 6/6，19.54秒 | final-asan-{build,test}.log |
| UBSAN专项 | 6/6，19.64秒 | final-ubsan-{build,test}.log |
| TSAN专项 | 6/6，46.49秒 | final-tsan-{build,test}.log |
| native-release专项 | 6/6，12.25秒 | final-native-release-{build,test}.log |
| 服务/原生前端 | native-release构建通过，跨日期ncnn版本生成信息引发依赖重编译，pin/锁未变 | final-native-release-build.log |
| 格式/公开边界 | 67头零违反、DEC-096既有两项seam；三私有头格式通过 | format.log、boundary.log、private-format.log |

专项含四业务（时间/打包、供图全量、自动供图、alias协议）及setup/cleanup。
构建后串行运行Sanitizer专项，不扩大原超时；耗时不作为性能比较。

首轮专项5/5，14.81秒。扩展自动模式负例时，计数断言的布尔条件触发-Wparentheses；
修正为直接判断“自动或未评估”，未放宽生产校验。第二轮专项5/5，15.23秒；随后
补齐supplier打包/摘要之间的来源退役重验并冻结源码进入最终矩阵。

```bash
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/native-release -j1 --target perception_storyboard_test desktop_image_supply_test tool_alias_provider_test mirage-service mirage-native
ctest --test-dir build/native-release -R '^(perception_storyboard_test|desktop_image_supply_test|content_visual_supply_test|tool_alias_provider_test)$' --output-on-failure
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
clang-format --dry-run --Werror integration/mira/src/content_visual_supply.hpp integration/mira/src/content_task_assessment.hpp integration/mira/src/desktop_image_supply.hpp
```

ASAN/UBSAN同专项；TSAN加setarch x86_64 -R。原始日志/源码摘要与验证manifest在
ignored的out/m9-auto-supply-20261029。11个构建/代码/测试文件总SHA256：
3ba833a03269667bcd9a2e9716c662f6dbf8331d351968a36ebbfa2715075c59。

## 未执行与后续

尚未执行真实在线模型、真实视频质量/图像token/CPU/RSS与平台端到端A/B，不能将
受控25%请求减少推广到所有任务。普通交互仍可能需要首次授权/新鲜输入截图和
按需细节图；自动内容模式也依赖模型忠实声明范围，不保证减少所有工具或模型请求。
后台CV采样、识别路由及关键事件不因本增量降频或被忽略。

完整视频/播放器身份、进度/暂停/跳播/结束、声音及真实Windows/MSVC、原生Wayland
Portal交互仍未验收。负责人：Mirage维护者；补跑条件：授权媒体/模型与目标平台，
先接可信覆盖来源，再测真实调用次数/成本/正确率。M9-30和M9-29自动语义去噪/
跨未知帧继承保持In Progress。未安装、部署或重启用户运行服务。
