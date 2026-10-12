# M7-08：动作后新观察与局部条件核验

> 状态：Completed（Linux X11 首交付；声明的局部后条件范围）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-08](../plans/m7-visual-tools-and-input.md)
> 决策：[DEC-056](../decisions/DEC-056-post-action-verification.md)
> 工作树：`a722f38` 上的 `feat/m7-conversation-desktop-tools`，未提交；保留既有 M7-01～05 改动

## 交付与证据边界

四个会话修改工具 2.1.0 在派发前固定可选窗口/语义条件，租约结算后强制新观察。
激活默认核验目标前台窗口；其他修改无条件仍可派发，但宿主不能返回成功。
精确窗口标题或语义角色/名称条件各最多 1024 UTF-8 字节；语义必须唯一命中。
新观察检查任务/会话/epoch、前台、采集前后标题/几何、取消与 1 秒协作预算，复用
Mira evaluate_observation 的质量/时间评估。最多 32 动作、512 语义节点、64 KiB 标题。

回执保留派发与输入释放语义，附 action_id、固定条件摘要、核验状态与原因。
有效新观察生成 evidence_id、实际 observed_digest 和 captured_at_ms；摘要覆盖窗口、
标题、几何、语义匹配数、固定条件及 epoch，原始期望文本不复制到普通过程。
时间戳只供展示，新鲜度以单调时钟为准。摘要和局部条件不证明任意用户意图或因果关系。

只读 desktop.action.verify 只重验本轮最近 action_id，不改变条件、不重新派发输入。
未验证阻止下一修改；不确定副作用即使条件匹配也保持本轮 fence。终态再观察最近
动作，所有动作都通过且无规范修改/核验工具失败才保留模型回答，并明确局部核验范围。
拒绝、缺条件、观察失败、前台/环境变化及 uncertain 转安全 failed，Cancelled 优先。
规范失败 ToolResultPart 是事实源，UI 的 Part 截断预算不能绕过成功门禁。

原生工具卡区分已派发、已核验、未验证、不确定和已拒绝，显示目标窗口；展开保留
证据身份/摘要/时间。没有新增 IPC 事件或 Part 状态枚举，旧/无法解析的证据不显示
已核验。实现位于 Adapter；复用 ConversationLoop/supplier 和既有 Executor task，
没有新线程、队列、调度器或生命周期 owner。Workflow 底层派发契约不宣称已接此门禁。

## 环境与依赖

Linux x86_64、GCC 13.3.0、C++20，私有 Xvfb/XTEST 和真实 Linux X11 Provider。
Mira 固定 `698fc77c151fa93b1b52ea4dd25cb95a3a60bee5`，沿用此前授权升级；本项无
额外 pin 或第三方源码变更。核对公开 ConversationLoop/supplier/Observation API，
窗口后条件是 Mirage 产品策略，不登记为 Mira/Mirador 能力缺口。
ASAN 构建同时启用 address,undefined，关闭 leak 检测；TSAN 在受控 ASLR 下执行。

## 三类真实 X11 任务

测试目标为私有 Xvfb 上的本地 X11 编辑器小应用。脚本只承担确定性的模型规划，
桌面发现、窗口激活、文本与按键均经生产 LinuxDesktopEnvironment/X11 Provider。
目标应用在其事件循环处理真实 XTEST 输入，没有 Fake Desktop Provider。

| 任务 | 预先固定的后条件 | 独立检查 |
| --- | --- | --- |
| 窗口激活 | 目标窗口前台，标题 Editor | 实际 Window Provider 前台 ID/标题一致 |
| 文本回显 | 精确标题 Text:probe | 应用收到真实键事件并更新窗口标题，前台再次读取一致 |
| Enter 提交 | 精确标题 Saved | 应用处理真实 Enter，文件实际落盘，独立读取内容为 probe |

每类任务还检查物理按键全部释放。模型 fixture 即使回答 TASK SUCCESS，条件未通过
也不能把文字变成成功终态。测试用 LatestMailbox 协调应用事件已处理，只在测试
侧有界轮询；生产核验立即采集，异步更新用只读重验，不通过固定等待伪造证据。
三类任务 3/3；这是实际 X11 组件和本地应用验收，不是在线模型业务规划或真实语义
表单验收。真实 AT-SPI2 表单、OCR/Canvas 与用户桌面接管仍分别补跑。

## 最终验证

| 验证 | 结果 | 范围 |
| --- | --- | --- |
| native-debug 全量 | 56/56，无 skip | 当前完整工作树回归 |
| native-release 全量 | 56/56，无 skip | 当前完整工作树回归 |
| 联合 ASAN/UBSAN | 5/5 | 核验、harness、图片供给、Runtime 集成、会话证据解析 |
| TSAN post_action | 137 checks，通过，无竞态报告 | 核验 fixture 与三类真实 X11 任务 |
| TSAN harness | 435 checks、0 failures，无竞态报告 | 会话、取消/超时、拒绝、输入 fence 与关闭 |
| 公共头/格式 | 53 headers、0 violations；格式通过 | pinned 类型边界与格式 |
| 原生 renderer | 21,875 checks、0 failures | 目标/已核验、未验证和失败不确定卡；浅/深色实渲染已检查 |
| Impeccable detector | 0 findings | 本轮原生工具卡局部改动 |

以下命令在仓库根执行；renderer 另列工作目录。所有有限任务保留并消费 future，
测试 Executor 由外部 owner 关闭。没有新增独立并发路径。

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug
cmake --build --preset native-release -j3
ctest --preset native-release
cmake --build --preset asan --target post_action_verification_test harness_desktop_tools_test desktop_image_supply_test native_agent_integration_test native_chat_model_test -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan -R '^(post_action_verification|harness_desktop_tools|desktop_image_supply|native_agent_integration|native_chat_model)_test$'
cmake --build --preset tsan --target post_action_verification_test harness_desktop_tools_test -j2
setarch x86_64 -R build/tsan/tests/post_action_verification_test
setarch x86_64 -R build/tsan/tests/harness_desktop_tools_test
cmake --build --preset native-debug --target mirage-boundary-check mirage-format-check
mkdir -p /tmp/mirage-post-action-render-20261009
(cd build/native-release/apps/native && /home/linductor/mirage/build/native-release/tests/native_conversation_view_test /tmp/mirage-post-action-render-20261009)
/home/linductor/.agents/skills/impeccable/scripts/impeccable detect --json apps/native/app.cpp
```

post_action_verification_test 额外覆盖缺条件/假成功、异步回显重验、前台切换、环境换代、
取消、权限拒绝、异会话/旧动作、试图改条件、语义重复命中、不确定输入不重发、
文本/期望脱敏和 UI 投影之外的规范工具失败。既有 harness 继续覆盖 handler 异常、
运行中取消/超时、提交拒绝与 Runtime shutdown。native_chat_model/view 校验有效
证据、矛盾/失败回执不显示已核验，以及未验证/不确定状态。
浅/深色的 post-action-verified 截图为 1180×800，位于上述 renderer 输出目录；
已检查目标窗口、已核验文字、展开证据和正文/工具卡的布局，没有扩大到整页新设计。

## 迭代记录与限制

初版 fixture 错用了排序后的 canonical 摘要，Mira Provider 需要其实际序列化工具
参数的摘要，导致工具校验拒绝；修正 fixture，与生产协议保持一致。既有 harness
最初仍期待拒绝动作后成功和后续输入继续执行，更新为新终态门禁，同时保留稳定
error_code/input_released 回执。首轮全量 55/56，native renderer 的新增断言找错
标题子节点而超时；修正选择器后最终 debug/release 各 56/56，未绕过渲染用例。
新增观察摘要/时间和失败卡状态后，最终代码的全量与 sanitizers 均已复跑。

Xvfb 可能先探测被占用 display，打印 socket 创建失败后选择空闲 display；xkbcomp
有未识别扩展键名警告。最终三个真实任务和进程退出成功，无 TSAN 竞态报告；不将
这些启动诊断写成产品失败，也不将默认 ASLR 下 TSAN 启动限制写成测试通过。

本轮没有重跑在线模型业务任务，不沿用 M7-04/05 在线合成图作为动作业务证据。
Windows/Wayland、真实语义表单、OCR/Canvas 和跨进程原子事务没有本轮证据。
负责人 Mirage 维护者：真实语义应用就绪后补表单用例，视觉条件在 M7-06/07 后补验，
目标平台与权限环境就绪后按 M7-10 执行。Xlib 的协作式取消限制仍在。
M7 整体保持 In Progress，下一项优先真实 OCR 装配与供应链取证；本项不创建 MR，
不操作上游 issue，也不修改既有 `.impeccable` 未跟踪文件。
