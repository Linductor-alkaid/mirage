# M9-29 对象版本注意区域验收

> 状态：Completed（M9-29首增量，本机契约与脚本模型范围）
> 日期：2026-10-28；负责人：Mirage维护者
> 计划：[M9-29](../plans/m9-layered-perception-delivery.md)
> 决策：[DEC-129](../decisions/DEC-129-versioned-task-attention.md)
> 基线提交：022f2fa

## 交付与承载

扩展既有desktop.vision.focus，object_ref与矩形参数互斥。PerceptionSemantics
根据本任务已完成模型请求的实际供给认证引用；FeedLease再次验证当前source/
context、ID/incarnation/content_revision、观察年龄与确认状态。同版本移动
跟随新识别框；内容、实例、上下文、来源改变或未知/缺失/过期/gap/失败时单向
失效、恢复全局事件。坐标模式也撤销旧来源过滤。注意状态与原因自动供给下一次
LLM请求；wait明确返回attention_invalidated，不凭timeout暗示目标没变。

每消费者仅一项策略、无像素、保留字符串/绑定≤4KiB；沿用原串行任务上下文，
不加worker、队列、定时器或神经模型。等待通过原同步wait的取消检查重验，
无活动注意时不额外读快照；活动注意会增加有界元数据读取/对象扫描，未测全进程
CPU/RSS影响，不用DEC-128资源数字冒充本轮性能证据。ACK、256事件、8帧和8KiB
供给预算不变；其他消费者及共享采样/权限不改。

## 关键验证

- task_perception_test：两个真实ResidentDesktopPerception租约，共享受控同步feed；
  同版本移动跟随，拒绝错误实例/过大请求ID且保留旧策略；等待期间只改内容版本、
  不发事件或推进cursor，仍返回attention_invalidated并恢复区外普通事件。
  第二消费者的矩形过滤独立；出现事件绕过；ABA不会自动复活。取消优先、清除后
  超时、来源变更使矩形失效；共享producer的focus调用为0。
- 私有策略矩阵：未知scene、对象缺失、pending/身份未确认、实例改变、deferred、
  换源、上下文/source_session改变、drop、过期/未来时间与failed/stopped/degraded。
- perception_semantics_test：未完成供给拒绝，完成请求接受，同版本移动可用，
  内容改变后旧引用拒绝，沿用旧标注/未知/过期与跨来源回归。
- desktop_image_supply_test：实际注册/执行focus工具；未完成/被预算移除的供给、
  混合参数、缺项、负数/小数/溢出/越界、外任务、期限与接管拒绝；完成后接受，
  同版本移动后仍接受，内容改变拒绝；模型输入attention投影及8KiB检查，
  capture仍仅原初始化1次、input为0。

## 实际执行

| 配置 | 当前结果 | 日志 |
| --- | --- | --- |
| Debug完整 | 120/120通过，358.97秒 | /tmp/mirage-m9-attention-final-debug-test.log |
| ASAN专项 | 11/11通过，166.99秒 | /tmp/mirage-m9-attention-final-asan-test.log |
| UBSAN专项 | 11/11通过，137.27秒 | /tmp/mirage-m9-attention-final-ubsan-test.log |
| TSAN专项 | 11/11通过，293.77秒 | /tmp/mirage-m9-attention-final-tsan-test.log |
| native-release专项 | 11/11通过，45.89秒 | /tmp/mirage-m9-attention-final-native-release-test.log |
| 原生服务/前端 | mirage-service、mirage-native构建通过 | /tmp/mirage-m9-attention-final-native-release-build.log |
| 格式/公开依赖边界 | 通过；67头文件零违反，DEC-096既有两项seam | /tmp/mirage-m9-attention-style.log |
| 私有头格式 | 通过（task_attention、perception_semantics） | /tmp/mirage-m9-attention-private-format.log |

专项11项含9业务用例及setup/cleanup。多个构建/测试配置并行运行，时间只作
执行记录，不能当性能基准。TSAN未抑制竞争报告、未扩大超时。
本地out/m9-task-attention-20261028/source.json记录10个源码/测试/CI文件hash，
总SHA256：d32d4d465ccccaacf4799029d8c780dd4f489ac8a3178c82bea733d02f48fc0f。
未修改固定依赖；验证使用受控feed/脚本模型，不调用在线LLM或用户桌面。


首轮3业务测试（含setup/cleanup为5项）中task_perception两断言失败：旧坐标
夹具在首次epoch=0→1之前设置焦点，新来源屏障按新契约撤销；另一移动夹具
只改x而事件y范围仍在目标外。修正为先等bootstrap，以及给事件正确y坐标；
生产版本检查未放宽。随后5/5通过。原失败日志
`/tmp/mirage-m9-attention-first-test.log`、修正日志
`/tmp/mirage-m9-attention-fixed-test.log`保留。

扩展矩阵首轮还发现TaskFeedFixture声明了global注意投影，却在focus后只记录调用、
不更新投影，导致生产后置检查按契约返回applied=false。修正夹具回报region/global
状态后复验通过，未移除后置检查。ASAN/UBSAN/TSAN/native首轮均10/11，完整
Debug首轮118/120（同夹具影响desktop_image_supply及PNG别名用例）；原日志
`/tmp/mirage-m9-attention-{asan,ubsan,tsan,native-release}-test.log`与
`/tmp/mirage-m9-attention-debug-test.log`保留，不覆盖失败。

## 复现与未执行项

```sh
cmake --build --preset debug --target desktop_image_supply_test task_perception_test perception_semantics_test perception_storyboard_test continuous_perception_consumer_test significance_gating_test layered_perception_test runtime_service_test harness_desktop_tools_test
ctest --test-dir build/debug -R '^(desktop_image_supply_test|task_perception_test|perception_semantics_test|perception_storyboard_test|continuous_perception_consumer_test|significance_gating_test|layered_perception_test|runtime_service_test|harness_desktop_tools_test)$' --output-on-failure
```

ASAN/UBSAN/native-release使用对应build目录；TSAN使用setarch x86_64 -R。Windows
CI选择集新增可移植task_perception_test/perception_semantics_test，本机未跑MSVC、
交互Windows/Portal、在线LLM、低端设备或新的性能矩阵；由Mirage维护者在目标
环境与授权任务就绪后补验。M9-29多目标重要性与普通事件窗口合并、跨未知帧精确
语义继承仍未交付。当前layered CV-only不能证明对象有效，保守失效可能频繁，
持续空间观察应使用明确矩形；本增量不声明完整视频理解或M9整体完成。
