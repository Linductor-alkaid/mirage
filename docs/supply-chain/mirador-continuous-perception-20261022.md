# Mirador 连续感知公开能力消费审计

> 状态：Completed（本轮 Experimental pin 消费验收，非 M9 整体验收）
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 授权：维护者要求提交采集基础层后继续开发，并提醒 Mirador master 持续更新
> 工作项：M9-05/06/07/18；反馈 MIRADOR-20261019-001

## 升级范围

| 项目 | 实际值 |
| --- | --- |
| 旧 pin | `5565436c3770b53c2230e7b7083ca48e028368b4` |
| 新 pin | `23f7ed2d985187f1d4c53752c53aeba1674b952e` |
| 取证 | git fetch origin master；开始和开发末段两次核对，origin/master 均为新 pin |
| 范围 | 16 个提交，包含 PR #48～52；M8-07 收口及 M9-01/02/03 契约、关系视图、注意规划 |
| 构建 | mirador::fusion 增加 continuous_perception.cpp，无新增第三方组件 |
| 许可 | MIT，LICENSE 未变 |
| 嵌套 pin | googletest `063de7e9578f82b369302001269680b4b1553359` 未变；ncnn 装配未变 |
| 兼容性 | 原有单帧融合、OCR/budgeted OCR、Tracker 公开签名不变；新增面为 Experimental |

只更新 submodule gitlink 与 dependencies.lock.json，未修改上游源码或历史。
构建继续锁精确 commit；后续 master 更新经过公开 API 核对及消费验证再推进 pin。

## 已交付与待交付

按 pinned [公开头](../../third_party/mirador/include/mirador/continuous_perception.hpp)、
[DEC-026](../../third_party/mirador/docs/decisions/DEC-026-continuous-perception-and-attention.md)
与[上游 M9 计划](../../third_party/mirador/docs/plans/m9-continuous-perception.md)核对：

- 已交付领域契约、source epoch、预算、关系构建、attention policy/revision、
  当前快照焦点解析/规划、显式 unresolved/bootstrap/peripheral 记录。
- 完整 plan/accept 覆盖绑定、latest/confirmed/published 时间状态与事件 drain
  待上游 M9-04/05；头中数据模型不等于运行能力已经交付。
- 规划面暂不支持 display，且要求 frame/snapshot 同源同帧；Mirage 在独立 opt-in
  背景宿主完成 oriented 融合后调用。原 display 宿主保持原坐标和行为。
- Mirage 不复制关系/跟踪/稳定化/事件算法；采集、kairo 生命周期、权限/源版本、
  模型供给、通知和动作仍属 Mirage/Mira。

接线及预算见[连续视觉消费契约](../design/continuous-visual-analysis.md)。反馈
MIRADOR-20261019-001 转 Accepted（部分交付与消费），不关闭整个条目或 M9。

## 消费验证

基线 Mirage `1faba92`，分支 `feat/continuous-capture-infrastructure` 的未提交工作树。
Linux x86_64、GCC 13.3.0；Mira `a29374a` 不变。消费专项用真实 kairo worker 和
fake backend；64 代共享 capture→analysis 周转，宿主管理的原始像素不超过
两代/512 字节，OCR 实调一次。该断言不代表桌面资源收益或长期 soak。

最终定向：ASAN、UBSAN、native-release 各 7/7；TSAN 以
`setarch x86_64 -R` 运行相同定向组 7/7，通过且无 sanitizer 报告。7 项包括
5 个程序及隔离 setup/cleanup 两个夹具。消费专项 519 checks，采集专项 166、
visual_session 281、budgeted 136，均零失败；还运行 visual_observation_loop
验证旧 display 发布路径。新增负例确认 oriented 背景快照不能注册为全局动作引用，
且拒绝时保留原活动 scope。

Debug 最终全量 **110/110，通过，299.92 s**。格式、65 个公开头的边界检查均通过
（仅 DEC-096 原有两个 seam）；变更 Markdown 的本地链接/围栏/标题、M9 唯一编号
与开放状态及干净的 Mirador gitlink/lock 一致性检查通过。

```sh
git -C third_party/mirador fetch origin master
git -C third_party/mirador checkout --detach 23f7ed2d985187f1d4c53752c53aeba1674b952e
cmake --preset debug
cmake --build --preset debug -j 4
ctest --preset debug

# 对 asan、ubsan、native-release 各执行；TSAN 的 ctest 前加 setarch x86_64 -R
cmake --preset asan
cmake --build --preset asan --target continuous_perception_consumer_test continuous_capture_test visual_session_test visual_session_budgeted_test visual_observation_loop_test -j 2
ctest --preset asan -R '^(continuous_perception_consumer_test|continuous_capture_test|visual_session_test|visual_session_budgeted_test|visual_observation_loop_test)$'

cmake --build --preset debug --target mirage-format-check mirage-boundary-check
git diff --check
```

首轮发现旧路径预取消后签名未清理的集成回归，已恢复原语义；故障注入发现
Executor 先 shutdown 时，worker 唤醒未取消在途 OCR，专项因此超时，已接到同一
ExecutionContext。另在 future 可见前释放已结算请求的源帧/attention payload，
避免 mailbox 历史版本把连续采集的两代像素钉住。最终专项覆盖这两种关闭/资源
路径；这些属于应用接线问题，没有修改依赖掩盖问题。

未验收：真实模型/低端设备 CPU/RSS/唤醒、4K/多源长期 soak、生产 native/IPC
供给、平台推送流按需暂停、前置 ROI 调度、Windows/真实 Wayland/pet 共存。
负责人 Mirage 维护者；接线、设备/授权及预登记任务/阈值到位后由 M9-11/12 补跑。
