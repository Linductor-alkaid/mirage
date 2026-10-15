# 最新 Native 会话：过程截断、桌宠退出、输入法干扰与用量缺失

> 状态：In Progress（第一阶段修复已完成；第二阶段、输入法与展示预算待实施）
> 日期：2026-10-15（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-11/14/15/16、M8-03/05/12、M7-10
> 范围：日志、公开契约与代码调查、隔离进程故障注入；BUG-20261015-002 第一阶段已修复
> 并提交（commit 944afcf）；其他两项未修改产品代码。未重启用户 tray/native/pet，
> 未修改用户输入法或会话文件。新增可复现探针及计划记录。

## 现场与结论边界

读取 `~/.local/state/mirage/session-state.json`（schema 1，10 个会话），按
`created_at_ms` 选择最新会话 `188f2076…`、轮次 `a9d69384…`。该轮有 29 个过程
part，终态 `failed`，错误为 `dialog turn was cancelled`。这证明记录了取消，不能
仅据此判断取消的发起方，更不能归为预算耗尽。

调查时用户 tray/native 正运行于 `build/native-release`，pet 已不存在；
`product.control(status)` 给出 `pet_state=failed`、`pet_pid=0`，原因为
`pet process exited unexpectedly; start it again to recover`。两个在运行的用户程序
没有被本轮替换。三个 native-release 程序的磁盘时间均为 13:19 左右，不能以当前
源码或 native-debug 构建结果证明该 release 已包含全部在途修改。

## BUG-20261015-001：展示预算被思考占满，后续工具记录空白

### 已确认

`runtime/conversation/include/mirage/conversation.hpp` 与 DEC-049 定义每轮最多
96 段、每字段 16 KiB、文本/参数/输出合计 64 KiB。这是**展示和持久化投影**的
容量，区别于模型上下文、单请求输出 token 和累计执行步数。

最新轮的三个内容字段 UTF-8 字节合计**恰好 65,536**，其中 thinking 占 54,843。
两段长思考达到字段上限；之后一段思考只剩约 2 KiB，后续正文和最后一条
`desktop.screen.capture` 的参数及结果均为空，并带 `truncated=true`。前四条
截图记录有完整元数据，显示原始 3200×2000、供图预览 1600×1000。

`integration/mira/src/conversation_capture.hpp` 在复制响应后裁剪展示 part，并不
修改 canonical `ModelResponse` 或供图载荷。`apps/native/app.cpp` 把这一标记统一
显示为“已截断”。因此：

- 思考详情超过展示预算后缩短，是现行有界策略。
- 思考耗光总展示预算、后续截图连元数据都无法保留，是可用性缺陷。
- 这些记录不能证明截图像素被裁掉，也不能证明最后一张截图是否完整到达模型；
  相关完整供图证据没有保存在该展示文件中。
- 本次取消与展示裁剪是独立事实，不能宣称后者导致任务终止。

### 修复方向与验收

由 M9-14 冻结展示分类预算和最近活动窗口；给工具结果、错误、最终正文保留容量，
长思考只保留明确标记的摘要。达到段数/字节容量时采用可解释的淘汰策略，不能再
无差别清空后续工具。界面文案区分”展示摘要省略”和供应商输出不完整。
不简单提高 64 KiB，也不把展示文本重新当作模型历史或动作授权。

验证需覆盖长思考后截图、错误及最终正文仍可辨认，超 96 段后的新活动可见，
UTF-8 边界、IPC 1 MiB 帧和历史持久化上限。改变 DEC-049 策略前同步决策与设计。

**第一阶段修复**（2026-10-15）已完成：
1. DEC-093 冻结分类预算：工具 40 KiB、正文 16 KiB、思考 8 KiB
2. `conversation.cpp` 的 `valid_parts` 执行分类检查
3. `conversation_capture.hpp` 的 `bound()` 按类别上限裁剪
4. 工具的 input/output 总量可达 40 KiB（单字段可超 16 KiB）
5. 新增 `conversation_budget_test` 验证策略
6. 核心测试：`conversation_budget_test` 及 `tray_runtime_test` 通过

**待完成事项**（M9-14）：
- 最近活动窗口（段数上限淘汰策略）
- 思考摘要标记与提取
- 界面文案区分展示省略与输出截断

## BUG-20261015-002：状态请求延迟导致桌宠退出，诊断缺失

### 已确认的代码路径

`apps/pet/bridge.cpp` 每两秒以 1500 ms 期限请求 `pet.status`。
`SessionClient::call` 超时后按 DEC-012 关闭该连接，即使事件此前持续到达。
`SessionWorker::run` 返回后把 `live` 置 false，却没有输出其 `RunExit/diagnostic`。
`apps/pet/app.cpp` 发现 disconnected 持续五秒便主动关闭窗口，进程可以以 0 退出；
Runtime 只能报告通用的 unexpected exit。

另一条问题路径是 `Bridge::connected()` 和隐藏退出判定同时使用 `stale`：
状态过期、事件缺口或 timer 容量问题也会被当作 owner 失联。这些条件应分别处理。
不允许根据单一 stale 标志推断 owner 已经死亡。

### 隔离复现

新增探针 [pet_transport_timeout_probe.py](../../tests/manual/pet_transport_timeout_probe.py)，
使用独立 Xvfb、DBus、XDG 目录和有界 IPC mock，仅启动本次拥有的桌宠及窗口 fixture，
不接触用户 tray、不调用模型、不修改输入法。

```bash
python3 tests/manual/pet_transport_timeout_probe.py --build build/native-release \
  --output /tmp/mirage-pet-transport-active-20261015 --delay-ms 0
python3 tests/manual/pet_transport_timeout_probe.py --build build/native-release \
  --output /tmp/mirage-pet-transport-delayed-20261015 --delay-ms 2000
python3 tests/manual/pet_preview_acceptance.py --build build/native-release \
  --output /tmp/mirage-pet-regression-20261015
```

先行探针：正常回复时进程在 22 秒观察窗结束仍存活；每 50 ms 持续发送合法全量
状态事件、仅将 readiness 后的状态请求回复延迟两秒时，进程约 8.68 秒后以 0
退出。版本化探针复跑：无延迟组 readiness 成功、22.008 秒仍存活；延迟组
readiness 成功、8.688 秒退出、exit code 0，延迟的单次请求后连接结束。
结果分别保存在上述输出目录的 `result.json`，进程输出在 `pet.log`。
这两个 probe 成功完成测量，不表示故障验收通过。本轮既有隔离窗口/生命周期验收 **32/32**
通过，说明既有门禁没有覆盖上述传输延迟。

这是**已复现的缺陷路径**，尚不能认定就是用户这次退出的唯一原因。现场 tray 的
stdout/stderr 指向 `/dev/null`，没有桌宠 IPC 断开原因或 OS 子进程退出状态。
没有得到现场崩溃、OOM 或信号证据，不能据“突然消失”断言崩溃。

### 入场动作的边界

现有 `AnimationPlayer::begin_startup` 已有本地入场和历史终态抑制逻辑；其单元
验证不能代替真实进程入场取证。`disconnected` 是安全打断态，可中断本地入场；
隐藏/恢复回调还会 reset 播放器。当前证据不足以确认本次没入场是断联打断、
窗口可见性通知、资源加载，还是其他时序。需记录启动、素材 ready、首帧呈现、
visibility、IPC 断开及关闭原因的先后顺序，不修改素材来掩盖生命周期问题。

### 修复方向与验收

M8-05/12 负责区分 transport、同步状态、owner 存活和显式 stop。优先消费已有
完整事件，减少无必要状态轮询；溢出/序列异常通过有界快照恢复，停止旧连接后再
重连并重新核验 owner/PID。连接恢复只能重建观察，不重放桌面动作。临时超时
不能静默退出，实际 owner 退出仍须有界收敛，不能改成永不退出的孤儿进程。

所有连接、重连任务、timer 与关闭仍由 pet 进程唯一 Executor 管理；记录可消费
future、拒绝、停止和失败，遵循 pinned kairo integration 公共能力，不自建线程。
M8-03 覆盖新进程入口一次播放、bootstrap 期间状态变化、初始/重复可见性回调、
恢复与真实断联打断；补录可观察关闭原因。

**第一阶段修复**（commit 944afcf，2026-10-15）已完成并通过探针验证：
1. 区分 transport 健康（`live`）、同步状态（`stale`）与 owner 存活
2. 降低轮询频率从 2秒 到 30秒（事件流已提供实时更新）
3. 单次请求超时不关闭连接，标记 stale 但不抛异常
4. `check_hidden_exit()` 只在 `!live` 时退出，不考虑 `stale`
5. `connected()` 只检查 `live`，不检查 `stale`

探针证据：
- 无延迟：22.005s 存活 ✓
- 延迟 2000ms：22.009s 存活 ✓（修复前 8.688s 退出）
- Debug 核心测试：6/6 通过

详见 [第一阶段实施记录](../compatibility/pet-transport-staleness-fix-20261015.md)。

**第二阶段修复**（commit TBD，2026-10-15）进行中：
1. SessionClient 层面解耦"请求超时"与"连接失败"（DEC-094）
   - 引入 `Health` 状态：`Healthy` / `SyncStale` / `Disconnected`
   - `SyncStale` 表示同步请求超时，但事件流可能仍在工作
   - 请求超时不再设置 `dead_`，而是设置 `health_`
   - `finish_run()` 时将 `health_` 设为 `Disconnected`
   - 响应成功时恢复 `health_` 为 `Healthy`
2. 可观察关闭诊断：`run()` 返回 `RunExit` 和 `diagnostic`
   - `RunExit::Stopped`：owner 显式停止
   - `RunExit::ConnectionLost`：transport 失败或协议违规
   - `diagnostic` 记录具体失败原因（超时、帧错误、解码失败等）
3. 构建通过，等待完整验证

**待完成事项**：
- 完整验证第二阶段修复（超时后恢复、关闭诊断可见）
- 重连与恢复机制、序列异常通过快照恢复
- M8-03 启动入场时序与真实断联打断验证

## BUG-20261015-003：ASCII 键通知被输入法组合，缺查询和切换能力

### 已确认

本机只读 `ibus engine` 返回 `libpinyin`。这证明引擎身份，**不单独证明引擎内部
当前为中文模式**。最新轮在键入后的后续思考中描述：预期命令
`pwd && echo FOCUS-OK` 在终端变为中文词，末尾仍出现输入法候选框。此描述与用户
现场报告、当前引擎及平台实现一致；不是将中文内容误当作工具参数。

该轮 `desktop.session.type_text` 接收 20 字节文本，结果为 dispatched，核验
`reason=fresh_shared_frame_only`、`verify_kind=shared_frame_observed`。按 DEC-077，
这仅证明派发、释放和新帧取得，**没有证明输入文字等于请求文字**。

`platform/linux/src/portal_input.cpp` 的 Text 路径把 ASCII 转成键符通知；
`platform/linux/src/x11_backend.cpp` 也使用模拟按键。两者都没有 IME 检查/切换。
现有 InputProvider 和 DesktopCaptureSession 没有状态查询或切换公开契约；核对
Mira/Mirador 公开头文件未发现可直接复用的操作系统输入法控制能力。这属于 Mirage
Platform Backend 职责，不登记为依赖缺口。Native 编辑框自身的 IME 支持不能解决
Agent 向外部终端输入的问题。

### 修复方向与验收

由 M9-15/M7-10 定义平台无关的输入法状态、能力和切换结果：引擎、可确认的模式、
组合状态及其 unknown，明确不支持情形。Linux 分别适配实际可用的 IBus/Fcitx
协议；Windows 另走其平台实现，不把 Linux SDK 暴露给 Desktop/Mira。

字面文本输入应在同一次有界动作中查询、保证直通输入、读回确认、输入并核验
目标文字；把这些工作下放给宿主，避免要求模型连续调用查询/快捷键/截图。
切换需走现有键盘授权和动作 owner；若存在未提交组合、无法可靠判断/切换或
焦点发生变化，先明确拒绝，不猜快捷键、不擅自提交候选、不覆盖用户剪贴板。
是否恢复原引擎、何时恢复及竞态处理需在实施前冻结；取消和 shutdown 不应遗留
新的输入状态或通过“恢复”提交用户组合文本。

不能把键盘布局当作输入法模式，不能把收到新帧当作文字核验。支持按键事件的
能力与可靠字面文本传输能力应分开描述。核验不足时返回 unverified/unsupported，
不输出文字已命中的声明。重复输入未知副作用的动作仍禁止自动重试。

验证至少涵盖中文引擎/内部英文态、已有候选、引擎缺席、切换/读回失败、焦点变化、
取消与恢复、终端命令和 URL；实际 IBus/Fcitx/Wayland/Windows 分别取证，协议
mock 不能冒充平台验收。新契约及默认策略需同步 DEC-077、设计和工具描述。

## BUG-20261015-004：任务执行与失败期间用量不可用

### 已确认

用户补充“模型用量不可用”。定位到原生上下文详情的“Token 用量尚不可用”：
它读取 `session.context_usage`，不是累计 Token 或费用面板。DEC-036 当前契约
明确只接受最近一次**任务成功结算**所携带的模型输入用量，历史暂不持久化。
这是现行策略造成的产品缺口，不能先归因为供应商解析错误。

链路有四处共同限制：

1. `ModelLayer::complete_harness_turn` 只有最终 `completion.ok`、答复通过展示/
   动作核验时，才从 `ConversationResult.last_usage` 填充 completion 的用量；
   `complete_dialog_turn` 同样在成功返回正文前填充。中间请求统计不进入 UI。
2. `settle_dialog_turn` 仅在 `completion.ok` 分支生成 `context_usage`。
   IPC `decode_context_usage` 也明确拒绝非 `ok` 轮次携带该对象；前端
   `ChatModel::apply_turn` 仅在 `status=ok` 时更新它。只放宽其中一层不能修复。
3. `PersistedChatTurn`、持久化编码和服务 hydrate 都没有用量字段，服务重启后
   已成功旧轮也不再提供这项数据。主窗口仅重新打开与服务重启需要分别验证；
   前者可从仍运行的服务历史拿到成功轮的内存统计，不能混为一谈。
4. `ObservedProvider` 已读取每次响应的 `ModelUsage` 并维护 preflight 锚点，
   `usage_observer` 仅供当前诊断/验收使用，没有接到产品 Runtime/IPC/UI。
   它现在还会过滤非 Exact/ProviderReported、缺 input 或已取消/过期的结果，
   不应直接充当完整请求计费台账。

最新任务虽已完成多个模型步骤，但最终为取消，当前进程的只读
`session.chat.history` 返回 `failed` 且不含 `context_usage`，因此按当前策略
必然显示不可用。尚未取得该现场每次响应的原始 usage 或独立用量 trace，不能
断言“供应商没有返回用量”，也不能据展示文件没有字段判断 token 消耗为零。

未配置窗口预算只会使已有用量显示“输入 / 未知 Token”，不是整个用量对象缺失
的同一问题。上下文占用、累计消耗、输出/思考 Token 和费用要分别说明。

### 修复方向与验收

由 M9-16 配合 M9-11 修订 DEC-036，再同步 integration、Runtime、IPC、持久化
和 UI 契约：

- 每次模型请求结算后发布有来源、请求/尝试身份、顺序、时间和质量的用量快照，
  不等待整个 Agent 任务成功。失败/取消保留此前已确认的用量，标明“最近已报告
  请求”；未完成的新请求不能伪装为已计量。迟到旧请求不覆盖较新的上下文占用。
- 上下文指标使用最近请求输入及其质量/时效，不能把跨调用输入之和当作当前
  上下文。压缩前后的数值跟随实际请求，估算若显示必须明确标为估算。
- 累计消耗以实际请求/尝试身份去重，在有界存量下累计已报告 input/output/
  cached/reasoning；缺失、部分、供应商报告和精确值可区分。遵循 Mira 归一化
  后的 cache/思考包含关系，避免重复相加；不凭 Token 数伪造金额。
- 持久化有界已知统计及来源，使重启后保留历史事实；不能把恢复的历史用量
  当作新建运行时当前上下文。旧文件没有数据时明确历史缺失，不从聊天字节补造。
- 界面区分尚未请求、请求在途、供应商未报告、部分统计、历史未保存和窗口未知，
  避免全部显示成“等待模型返回 Token 用量”。

验收覆盖“多次工具循环 → 主动取消”“请求成功但产品动作核验失败”、中途请求
失败、missing/partial/真实零、缓存统计、重复/乱序、压缩、主窗口重开和服务重启。
先运行现有成功/缺失用量回归，再补新契约测试；真实供应商响应形态需另行受控
取证。本轮没有付费调用模型或修改供应商配置，未实施产品代码修复。

本轮执行 `ctest --test-dir build/debug -R
'^(native_chat_model_test|native_agent_integration_test)$' --output-on-failure`：
**4/4 通过**（两项实际测试及 setup/cleanup，13.38 秒），覆盖现有成功用量、
供应商缺报告和失败不覆盖上次成功用量等行为；这是当前契约的对照，不是 M9-16
验收完成。文档链接、围栏、M9-01～16 唯一编号及 `git diff --check` 检查通过。

## 本轮结果与待补证

产品源码未修改，四个问题均未标作修复完成。计划中的持续循环与本地语义观察
主线保持原状态；不能因为 Mira 已升级或旧桌宠门禁全绿就关闭这些新问题。

后续实施顺序：先补桌宠可观察退出与传输恢复、入场取证，再修字面文本输入，
并行处理展示容量分配；随后用同一 release 产品复验。负责人为 Mirage 维护者。
真实桌宠退出原因和入场、真实 IME 切换/恢复、文字命中及截图像素边界仍待对应
日志/平台验收，不能从本轮隔离 probe 推断全部通过。
