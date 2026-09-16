# 调研：RPA / Agentic Automation 架构经验（源码核实）

> 状态：Completed
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 调研方式：对四个开源项目的 main 分支源码快照（2026-09-16 浅克隆 / tarball）与官方文档逐条核实外部报告断言，并补充调研
> 关联决策：[DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)、
> [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)、
> [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)、
> [DEC-010](../decisions/DEC-010-m1-permission-framework.md)
> 输入：外部报告《Mirage 可复用的 RPA / Agentic Automation 架构经验报告》（LLM 生成，
> 断言在撰写时未经核实；本文是对照源码的核实结果与修正）

证据时效说明：文中行号以 2026-09-16 源码快照为准，会随后续上游提交漂移；引用结论时
以文件名与类名为锚。

## 1. 调研目标与方法

Mirage 的核心问题与传统 RPA 大量重叠：如何把任务表示为可执行 Workflow，如何把 GUI、
系统接口与外部工具抽象为稳定行为，如何定位 GUI 元素并处理界面变化，如何复用已成功的
操作，如何判断操作真正成功，以及确定性执行失败后如何恢复。Mirage 的结构差异在于：上层
有 Mira Agent Harness（Workflow 可由 Agent 创建、修复），下层有 Mirador（OCR、检测、
几何分析等轻量视觉能力）。

本次调研不从四个项目中挑选集成对象，而是提炼可进入 Mirage 执行模型的架构经验。四个
项目代表四条路线：

```text
Robot Framework : 确定性行为组合（Keyword / Library 分层）
OpenRPA         : 完整桌面 RPA Runtime（多 backend 元素抽象 + 插件）
OpenAdapt 生态  : Demonstration → 确定性编译 → 验证 → 重放
Skyvern         : Agent 感知-行动循环 + Agent 作为 Workflow 节点
```

方法：每条报告断言标注 `证实` / `部分证实` / `未找到证据` / `与事实不符`，并附源码
证据；在此基础上补充报告未覆盖、但对 Mirage 更有价值的发现。

## 2. 总体结论

1. **报告的四条路线全部在真实代码中得到印证**，其"十项最值得吸收的设计"（报告第 16 节）
   方向成立；其中分量最重的断言——OpenAdapt 已转向"demonstration 编译为确定性
   Workflow、健康重放路径零模型调用"——在 `openadapt-flow` 源码中逐条属实。
2. **需要修正三处细节**：OpenAdapt Resolution Ladder 的顶层是结构化 locator（DOM/UIA）
   而非 OCR；`Effect` 不在其 Workflow IR 文件中而在 runtime 执行层；OpenAdapt 新架构
   是 2026-01 才冻结 legacy 的早期重构产物，"生产验证"仅限 benchmark 层面——只能作为
   设计参考，不能当作经过实战检验的模式照搬。
3. **对 Mirage 最重要的共同结论**（与报告第 17 节一致）：Agent 负责把未知任务变成已知
   任务；Workflow Runtime 负责把已知任务可靠、廉价、可验证地重复执行。落到 Mirage 当前
   阶段，最紧迫的不是引入 Workflow Runtime，而是把 `ElementReference` 按多提示组合设计、
   把解析顺序写进契约（见第 5 节）。

## 3. 逐项目核实与经验

### 3.1 Robot Framework：行为接口与实现解耦

架构：核心框架（`src/robot/` 的 `parsing` / `running` / `variables` / `model` /
`output` / `result` / `reporting`）只负责解析、执行、变量、日志与结果模型；实际自动化
能力几乎全部在外部 Test Library。User Guide 原文："The core framework does not know
anything about the target under test, and the interaction with it is handled by
libraries"（`doc/userguide/src/GettingStarted/Introduction.rst`）。

报告断言核实：

| 断言 | 结果 | 证据（快照） |
| --- | --- | --- |
| 核心框架克制、能力在 Library | 证实 | `libraries/` 内 BuiltIn/Collections/Process 等均为可拆卸普通 Library |
| static / hybrid / dynamic 三级 Library API | 证实 | `running/testlibraries.py:216-219` 的判定逻辑；dynamic 入口在 `running/dynamicmethods.py`（GetKeywordNames、RunKeyword、GetKeywordArguments、GetKeywordDocumentation、GetKeywordTypes、GetKeywordTags、GetKeywordSource） |
| Keyword 元数据齐全、User Keyword 由低层组合 | 证实 | `running/resourcemodel.py:188`（args/doc/tags/timeout/setup/teardown，setup/teardown 自 RF 7.0）；`running/userkeywordrunner.py:36` 负责展开 |
| Listener 可观察并修改执行状态 | 证实 | `output/listeners.py:186-300` ListenerV3Facade 覆盖 suite/test/keyword/for/while/if/try 全部节点；签名直接携带 running 与 result 的真实模型对象；User Guide 明示"model objects can be both inspected and modified" |

报告未覆盖、对 Mirage 更有价值的发现：

1. **三模型分离**：数据/计划模型（`running/model.py`）、执行器（Visitor 遍历各
   BodyItem 自带 `run`）、结果模型（`robot.result`，可从 output.xml 离线重建，支撑
   rebot 后处理、合并、审计）三者独立。Mirage 的 Workflow Runtime 若从一开始分离
   plan / runner / result，任务回放与审计几乎零成本。
2. **显式失败语义**：`continue_on_failure` 按 tags（continue-on-failure /
   stop-on-failure）决定步骤失败后是否继续，teardown 默认继续
   （`running/context.py:216`、`running/status.py`）。
3. **能力协议进程内 / 跨进程同构**：Remote Library（`libraries/Remote.py`）把跨语言
   XML-RPC 实现为一个 Dynamic Library，进程外能力与进程内能力走同一套
   get_keyword_names / run_keyword 协议。这印证了 Mirage 能力发现/调用协议应做到
   "进程内与经 Local IPC 完全同构"（与 DEC-007 的协议分层一致，属其后续演进参考）。
4. **局限**：无内置并行（User Guide 明示并行只能在 Library 层实现）；文本 DSL 空白
   敏感、转义规则繁多；变量运行期解析导致静态校验弱；单线程遍历 + 全局命名空间使
   并发语义模糊。Mirage 不应复制这些（直接用结构化 JSON 契约 + Executor 并发）。

### 3.2 OpenRPA：桌面元素抽象与 Recorder 语义提升

架构：单体内多插件。主程序 `OpenRPA/`（WPF 机器人 UI + Windows Workflow Foundation
宿主，`WorkflowInstance.cs` 用 `WorkflowApplication` 运行，workflow 持久化为 XAML）；
共享层 `OpenRPA.Interfaces/`（IElement、Selector、Input、Overlay）；能力插件约 30 个：
`OpenRPA.Windows`（UIA/FlaUI）、`OpenRPA.Image`、`OpenRPA.Office`、`OpenRPA.IE`、
`OpenRPA.NM` + `OpenRPA.NativeMessagingHost`（浏览器）、`OpenRPA.Java` + JavaBridge、
`OpenRPA.SAP` + SAPBridge、`OpenRPA.TerminalEmulator`、`OpenRPA.Forms`、
`OpenRPA.Database`、`OpenRPA.WorkItems`、PS / Script / FileWatcher / AviRecorder 等。
WF 的 Activity 树本身就是支持 Sequence/Flowchart/状态机、可持久化恢复的图形化
Workflow IR——印证"IR 应为图而非线性脚本"。

报告断言核实：

| 断言 | 结果 | 证据（快照） |
| --- | --- | --- |
| 大量能力插件组成 | 证实 | 上列仓库根目录约 30 个项目 |
| Recorder 把原始输入提升为语义 Activity | 证实 | `OpenRPA.Office/Plugin.cs:71` 检测 `ControlType == "DataItem"` 生成 `ReadCell<string>`，键入值时按解析类型替换为 `WriteCell<int>/<double>/<string>`（:124-146）；`OpenRPA.NM/Plugin.cs` 收到 click 消息后取 DOM 数据构造含 xpath/cssselector 的 `NMSelector`（`NMSelectorItem.cs`），生成带 Selector 的 `GetElement` Activity（:204-224） |
| 统一元素抽象 + Selector | 证实（细节修正） | `OpenRPA.Interfaces/IElement.cs` 极薄（RawElement/Rectangle/Value/Name/Focus/Refresh/Click/Highlight/Items）；Selector 为属性包 JSON，逐属性可启停。**失效处理不是自动模糊重识别**，而是：通配符放宽（`PatternMatcher.FitsMask`）+ 逐属性手工 `Enabled=false` + anchor 锚点 + `GetElement` 自带 Timeout 轮询重找（BreakableLoop）+ `WindowsCacheExtension` 缓存——半自动组合 |
| 鼠标/键盘是兜底而非基础 | 证实 | `Interfaces/UIElement.cs:307-387` Click 先尝试 UIA `InvokePattern.Invoke()`（语义调用），失败才回退 SendInput 物理鼠标（`Interfaces/Input/MouseSimulator.cs`）；`ClickElement` 默认 `VirtualClick = Config.local.use_virtual_click` |

对 Mirage 的经验：

1. **薄公共元素接口 + 每 backend 三件套**（`IPlugin` + `IRecordPlugin` + Selector）：
   `IRecordPlugin.ParseUserAction(ref IRecordEvent)` 是干净的"原始事件 → 语义动作"
   提升点；OpenRPA 用硬编码规则（只认 DataItem），Mirage 可用视觉/语义模型替代并在此
   超越它。
2. **语义调用优先于键鼠模拟**：Provider 能语义触发的动作（UIA/AT-SPI2 invoke、
   应用对象方法），Input Provider 的键鼠合成只做兜底。这是一条可直接写进行为执行
   契约的排序规则（设计文档第 9 节）。
3. **selector 工程化组合**（通配符、逐属性启停、anchor、带超时轮询、缓存）是抗 UI
   变化的实用兜底手段；同时其源码中大量 0-results 日志与手工修 selector 的编辑器
   说明纯属性匹配维护成本高——这正是 Mirage 用视觉/语义 resolver 可以超越的空间。

不应照搬：.NET Framework / WPF / WF 深度绑定（无跨平台可能，WF 已停止演进）；`IElement`
过薄且泄漏（`RawElement` 是 object）；每接一个 backend 都是一个大子项目的桥接成本。

### 3.3 OpenAdapt 生态：demonstration → 确定性编译 → 验证 → 重放

生态布局（仓库全部存在）：

| 仓库 | 职责 | 快照状态 |
| --- | --- | --- |
| `OpenAdapt` | pip 启动器/安装器；自述 "The compiler and the runtime live in openadapt-flow" | v1.16.0；legacy 单体 v0.46.0 冻结于 `legacy/`（`docs/LEGACY_FREEZE.md`，2026-01） |
| `openadapt-flow` | 编译器 + 运行时 + IR（`openadapt_flow/{compiler,runtime,ir.py}`） | v1.35.1（2026-09-09），近 30 天 ≥100 commits，最活跃 |
| `openadapt-capture` | 原生桌面录制（screen/input/timing/window 证据同步） | 活跃 |
| `openadapt-types` | canonical Pydantic schema + 38 个 JSON Schema | v0.18.0，自述 API 尚不稳定 |
| `openadapt-grounding` | OCR text anchoring + 可选模型 grounding | 自述 "Status: Research. Not required by the product" |

报告断言核实：

| 断言 | 结果 | 证据（快照） |
| --- | --- | --- |
| 转向确定性 Workflow、健康路径零模型调用 | 证实 | flow README："compiles the recording into a script… The default healthy path makes no generative-model API call"；benchmark：MockMed 100/100、0 模型调用、4.9s p50（对比 agent 37.5s / 约 \$0.27） |
| 生态拆分 | 证实 | 上表 |
| Resolution Ladder | 证实（措辞修正） | `openadapt-flow/docs/RESOLUTION_LADDER.md`：**顶层是结构化 locator（DOM/UIA）** → 本地/全局模板 → OCR → landmark geometry → 可选 grounding 模型（默认关闭）。OCR 是"本地主力层"而非最高层 |
| Postcondition 与 Effect 分离、独立 system-of-record 验证 | 证实 | 屏幕级 `Postcondition`（`ir.py:437`）与系统级 `Effect`（`runtime/effects/effect.py:253`，含 `EffectKind`/`ReadbackSpec`/`EffectVerdict`）分离；验证通道实测存在 `runtime/effects/{rest,fhir,sql,file_arrival,email_delivery,graphql,document_arrival}.py`；`openadapt_types/oracle.py` 有 `OracleTier`/`OracleChannel`，actor 与 oracle 通道分离，README："Production VERIFIED needs tier 2 or 3" |
| 歧义时 halt 而非猜测，durable checkpoint + resume | 证实 | 终止态含 `HALTED_BEFORE_EFFECT` / `RECONCILIATION_REQUIRED`（主 README："Never blind-retried"）；`ir.py` `HaltObservation`；`runtime/durable/{program_checkpoint,resume,controller}.py`：checkpoint 保存 frame stack / loop cursors / 已绑定参数 / 已完成 effect keys；resume 需认证审批（`ApprovalRecord`），"resume is DETERMINISTIC… never to a free-form agent"，已确认的写绝不重放 |
| Workflow Program IR 结构 | 证实（一处修正） | `ir.py`：`ProgramGraph`、`State`、`Transition`、`Guard`、`LoopSpec`、`Predicate`、`HealEvent` 全部存在；**`Effect` 不在 `ir.py`**，在 `runtime/effects/effect.py` |
| openadapt-types canonical schema | 证实 | `ComputerState` / `UINode`（`computer_state.py`）、`ActionTarget` / `Action` / `ActionResult`（`action.py`，Target 支持 node_id / description / 坐标三级优先）、`Episode` / `Step`（`episode.py`）、`FailureRecord`（`failure.py`） |

**关键补充——编译器的确定性纪律**（报告未展开，对 Mirage 最重要）：

- `compiler/compile.py` 参数推断标注 "DETERMINISTIC… no model, \$0"；compile-time
  模型标注是 opt-in 且默认关闭，replay 不读取。
- `compiler/induction.py` 自述 "all deterministic + structural — ZERO model calls"；
  证据不足以唯一确定结构时 **honest refusal**（如 `ambiguous_loop` 拒绝编译），不猜。
- 单条 trace 是 evidence 不是 spec；推断 `ParamSpec` / `LoopSpec` / 分支需要多条 trace
  的结构性归纳；repair 只生成待人工审阅的候选 bundle，不自动生效。
- 编译产物是可 lint / certify / visualize 的 ProgramGraph bundle（设计 RFC：
  `openadapt-flow/docs/design/WORKFLOW_PROGRAM_IR.md`）。

**成熟度限定（重要）**：legacy 2026-01 才冻结；README 自述 identity gate 只覆盖部分
步骤（OpenEMR 示例 12 步仅 arm 4-7）、types "API is not stable across minor versions
yet"；grounding 仓库为 research 定位。架构理念自洽、工程质量高，但属快速迭代的早期
重构产物，"生产验证"限于 benchmark 层面。Mirage 引用其设计时应作为方向参考并保留
自己的验证责任。

### 3.4 Skyvern：Agent 即 Workflow 节点

架构（2026-09 重构后）：感知-行动引擎核心在 `skyvern/forge/agent.py`
（`execute_step`：`scrape_website` 产出含元素树与截图的 `ScrapedPage` → LLM 生成
结构化 actions → handler 执行 → 递归下一步，受 `MAX_STEPS_PER_RUN` 硬上限约束）；
workflow block 类型定义在 `skyvern/forge/sdk/workflow/models/block.py`；Playwright
兼容 SDK 在 `skyvern/library/`；run→代码缓存体系在 `skyvern/core/script_generations/`
与 `skyvern/services/script_service.py`；另有可插拔 RunEngine（skyvern_v1/v2/v3、
openai_cua、anthropic_cua、ui_tars 等）。

报告断言核实：

| 断言 | 结果 | 证据（快照） |
| --- | --- | --- |
| perception-action loop | 证实 | `Agent.execute_step` → scrape → `agent_step` → 执行 → 递归 |
| 结构化 Workflow 与多种 block | 证实（实际更多） | `BlockType`（`skyvern/schemas/workflows.py:476`）共 **30 种**：TASK、FOR_LOOP、WHILE_LOOP、CONDITIONAL、CODE、VALIDATION、ACTION、EXTRACTION、LOGIN、WAIT、FILE_DOWNLOAD/UPLOAD、HTTP_REQUEST、HUMAN_INTERACTION、SEND_EMAIL 等；报告列举不全 |
| 同一 session 混用 selector / AI action / agent task / workflow | 证实 | TS 端 `SkyvernBrowserPage`："combines Playwright's page API with Skyvern's AI capabilities"——`page.click(selector)` / `page.act(prompt)` / `page.agent.runTask(...)` / `runWorkflow`；click 为 **selector 优先、失败回退 AI**（Python 端同构：`library/skyvern_browser_page_ai.py`、`skyvern_browser_page_agent.py`） |
| 成功 run 生成可复用 script | 证实 | run 经 `transform_workflow_run_to_code_gen_input` 转换，libcst 生成可运行 Python；`run_script` 动态 import 执行；参数/结构化输出保留为 schema；失败自动降级回 agent 并重生成 |

**最值得抄的结构**：Agent task 在类型系统上就是众多 block 中的一种——
`BaseTaskBlock.execute` 内部就是调 `app.agent.execute_step`，无任何特权。条件/循环
是显式 block（`ForLoopBlock` / `WhileLoopBlock` / `ConditionalBlock`，分支支持 Jinja
条件与 Prompt 条件两种）。

补充发现与教训：

1. **感知格式比模型选择更影响成本**：v1 把全页 DOM 树喂 LLM，官方自述其"occasionally
   spiraled re-reading the page"，v3 改用紧凑 `observe` 快照。对 Mirage：给 Agent 的
   DesktopObservation 应是紧凑语义快照 + 截图，而非原始 UIA/AT-SPI2 树。
2. **定位以 DOM 为主、视觉为辅**：scraper 给可交互元素注入标记 id 并建
   `id_to_css_dict`，页面刷新后按元素 hash 重解析；缓存形态是 action plan 缓存
   （URL + goal 复用动作序列）与代码缓存，**无** embedding 语义缓存；步级重试上限
   `MAX_RETRIES_PER_STEP=5`。
3. 不应照搬：近万行 `agent.py` 与 1.8 万行 `block.py` 的 God object；云端中心化编排
   （Postgres + FastAPI + 队列）与 credit 计费假设；浏览器-only 的覆盖面。

## 4. 报告修正清单

| # | 报告表述 | 核实结果 |
| --- | --- | --- |
| 1 | Robot Framework 四条断言（Library 分层、三级 API、Keyword 元数据、Listener） | 全部证实 |
| 2 | OpenRPA 插件体系、Recorder 语义提升、鼠标为兜底 | 证实；注意 Selector 失效处理是半自动工程组合，不存在自动模糊重识别 |
| 3 | OpenAdapt Ladder "OCR text anchoring 是本地优先层" | 措辞修正：顶层是结构化 locator（DOM/UIA），OCR 为本地主力层、位于模板之下 |
| 4 | Workflow Program IR 含 `Effect` | 修正：`Effect` 在 `runtime/effects/` 执行层，IR 内是 `Postcondition`；两者有意分离（屏幕可见结果 vs 系统真实副作用） |
| 5 | OpenAdapt 当前架构（隐含经过生产验证） | 限定：早期重构产物，验证限于 benchmark；grounding 仓库为 research 定位 |
| 6 | Skyvern block 列举（Task/Loop/HTTP/Code 等） | 证实且不全：实际 30 种 block 类型 |
| 7 | 报告第 16 节"十项设计"与第 17 节闭环模型 | 十项方向全部成立；闭环在四个项目源码中均有对应实现证据 |

## 5. 对 Mirage 的落地建议

### 5.1 立即生效（M1 尾部 / M2 设计输入）

1. **`ElementReference` 按多提示组合设计**：semantic（role/text/description）/
   structural（accessibility path）/ visual（OCR anchor/template/几何）/
   spatial（relative_to/归一化坐标）/ raw（坐标）五类提示作为可选组合字段，由
   resolver 在执行时决定解析路径——而不是单一 selector 字段。落实在 M2
   Window/Accessibility Provider 与 reference provider 设计（设计文档第 5、7、9 节，
   [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
   的后续延伸），当前不改造成本最低、事后改造成本最高。
2. **解析顺序写进契约**：Accessibility（UIA/AT-SPI2）→ App Adapter → Mirador 缓存 →
   OCR/几何 → VLM（显式开启）。每次降级解析产生可追溯事件；VLM 只解决确定性 resolver
   无法解决的问题，不做默认 resolver。
3. **语义调用优先于键鼠**：Application/Accessibility Provider 可语义触发的动作，Input
   Provider 只做兜底（OpenRPA VirtualClick 经验）。
4. **canonical types 统一**：Action / ActionResult / Observation 由 `desktop` 层定义
   一套 canonical 类型，runtime 与 apps/ui 消费同一套（OpenAdapt 拆出
   `openadapt-types` 的动机）。

### 5.2 Action 语义层成型时（M2+）

1. **ActionDefinition 元数据契约**：input/output schema、timeout、retry policy、
   capability 来源、side_effect 标注、risk 分级（READ_ONLY / REVERSIBLE /
   SIDE_EFFECT / IRREVERSIBLE）。能力发现与调用协议参照 RF dynamic API：接口足够薄，
   使进程内 Provider 与经 Local IPC 的 Provider 完全同构（DEC-007 协议面的演进方向，
   不改变其 M1 冻结承诺）。
2. **执行事件分类学**：WorkflowStarted / NodeStarted / ActionResolved / ActionStarted /
   ObservationReceived / ActionCompleted / ActionFailed / AgentTakeover /
   WorkflowCompleted / WorkflowFailed 等，承载在 `executor::comm::Topic` 上（与根
   AGENTS.md 的通信组件路由要求收敛，不新建设施），同时服务 UI、调试、telemetry 与
   未来的 Recorder/回放。

### 5.3 Workflow Runtime 立项时（建议以总计划 `POST-NN` 吸收，触发条件为 M2 完整 Desktop 后端落地）

1. **IR 直接采用图/状态机**（OpenRPA-WF 与 OpenAdapt ProgramGraph 双重印证），最低
   支持 Sequence / Condition / Loop / Wait / Action / Agent / SubWorkflow / Return；
   **AgentNode 是无特权的普通节点**，内部循环受与确定性节点同一套预算与取消纪律
   （继承 DEC-009 的预算/取消模型，含步数与成本硬上限）。
2. **plan / runner / result 三模型分离**（Robot Framework 经验），结果模型支撑离线
   审计、合并与重放。
3. **Effect Verification**：区分屏幕级 Postcondition 与系统级 Effect；验证走非 GUI
   通道——Mirage 已有 Filesystem / Process Provider 可直接充当 system-of-record
   oracle，且要求 actor 与 oracle 通道分离；"用 GUI 完成操作，用非 GUI 通道验证结果"。
4. **Halt 语义**：`VERIFIED` / `HALTED` / `RECONCILIATION_REQUIRED` 作为一等终止态；
   高风险动作（IRREVERSIBLE）遇歧义 halt + durable checkpoint，而不是让 Agent 继续
   猜；恢复是确定性操作并可要求确认（衔接 DEC-010 的确认面语义与 Human Takeover
   约束）。
5. **Trajectory → Workflow 编译器默认确定性**：模型只做 opt-in 标注；证据不足诚实
   拒绝；repair 产物是待人工审阅的候选 bundle。Agent 成功轨迹与人工示范同为编译输入，
   产物是 canonical Workflow IR（而非直接生成代码）。

## 6. 与现有决策记录的关系

- [DEC-007](../decisions/DEC-007-local-ipc-and-runtime-service.md)（Local IPC 与
  Runtime Service）：本调研第 3.1 节 Remote Library 经验支持"能力发现/调用协议进程内
  与经 IPC 同构"作为其协议面后续演进参考；执行事件面（5.2）是协议之外的
  `executor::comm` 事件通道，不进入 v1 请求面冻结承诺。
- [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)
  （环境绑定与参考 Provider）：其 M1 Filesystem / Process 边界不受本调研影响；
  5.1 的多提示 `ElementReference` 与解析顺序契约是其"M2+ Provider 扩展"的设计输入。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)（Provider
  范围、预算与取消）：5.3 的 AgentNode 预算/取消纪律与"halt 优先于猜测"语义继承其
  预算/取消模型（Skyvern 的 `MAX_STEPS_PER_RUN` / `MAX_RETRIES_PER_STEP` 是同类机制
  的外部佐证）。
- [DEC-010](../decisions/DEC-010-m1-permission-framework.md)（Permission 框架雏形）：
  Action 的 risk 分级与"歧义 halt + 确认后恢复"是其 Capability / Rule / 确认挂点
  词表在动作语义层的自然延伸，可作为其"影响与风险"中预告的 M5 确认面产品化决策输入。

## 7. 参考源与快照信息

- Robot Framework：`github.com/robotframework/robotframework`（master 快照，
  2026-09-16）；User Guide 随仓库 `doc/userguide/src/`。
- OpenRPA：`github.com/open-rpa/openrpa`（master 快照，2026-09-16）。
- OpenAdapt 生态：`github.com/OpenAdaptAI/{OpenAdapt,openadapt-flow,openadapt-capture,
  openadapt-types,openadapt-grounding}`（main 快照，2026-09-16；flow 为 v1.35.1，
  2026-09-09）。
- Skyvern：`github.com/Skyvern-AI/skyvern`（main 快照，2026-09-16，大幅重构后形态）。
- 输入报告：外部 LLM 生成报告《Mirage 可复用的 RPA / Agentic Automation 架构经验报告》
  （2026-09-16 会话附件，未入库）；其全部可核实断言已在第 3、4 节给出对照结论。
