# Computer Use 键鼠执行、结果观察与验证门控调研

> 状态：调研完成；方向已接受，后续分层门控 v2 首增量已接入源码
> 日期：2026-10-20
> 负责人：Mirage 维护者（策略取舍、后续实现与验收）
> 关联工作：M7-25/26/31；DEC-110 默认自然输入与完成边界；DEC-056/109/111 验证策略
> 本地基线：Mirage HEAD `d09fa8d` 加当前文档工作树
> 范围：10 个代表性公开项目/工具栈的官方文档及指定源码路径；未运行外部项目或桌面实验

2026-10-20 后续批准：维护者同意更新对应决策，
[DEC-056](../decisions/DEC-056-post-action-verification.md) 已接受执行、反馈与业务
验收分层，DEC-059/068/109/110/111 同步作用域。下述源码调查保留原取证范围；
策略接受不构成实现或收益实测证据。

同日实施后续：分层门控 v2 首增量及准确验证命令见 M7-25/26/31 专项记录。本文第 5 节
保留修改前的源码基线，不能当作 v2 当前行为；其他项目的调研范围及原始证据不变。

## 1. 结论

“程序化键鼠执行可以不逐事件做视觉验证”是合理方向；“因此键鼠行为不会失败”不成立。
确定的代码只能确定尝试执行什么，不能保证平台一定接纳、输入一定落到预想控件，或应用
业务一定完成。需要区分执行结果、环境反馈与任务成功，而不是统一叫作“验证”。

所查项目普遍保留动作后的环境反馈，但方式不同：基础循环常将截图交回模型，在下一轮
推理中检查结果；部分框架加反思、会话末评估或结构化效果回读。没有证据支持“所有项目
都逐点击执行固定后条件硬门禁”，也不能从基础 demo 缺少该门禁推出“它们认为动作不会失败”。

对 Mirage 的建议是取消普通低层动作默认附带的逐动作业务证明要求，保留执行错误处理、
输入释放与取消事实；在动作组边界、下一步确实依赖状态变化时及任务完成时观察/验收。
有明确定义、低成本的条件仍可使用可选本地验证。维护者已接受此方向并修订 DEC-056，
调研阶段未修改代码；后续 v2 实现不解除执行/释放不确定性围栏。

这也澄清 DEC-110 的“执行完成后返回或验证”：它首先规定顺序，不应由此推导为每个动作
都必须新增 OCR/语义匹配或一次独立模型验证。自然轨迹及按压/释放必须在本地完成，
是否需要额外业务验证应由上层动作组/任务策略决定。

## 2. 调研口径

| 层次 | 回答的问题 | 合适证据 | 不足以证明 |
| --- | --- | --- | --- |
| 执行 | 参数是否合法、事件是否成功交给平台、本次序列是否完成、是否取消/释放失败 | API 返回、异常、传输状态、本地动作生命周期 | 控件接收、页面变化、保存/提交成功 |
| 反馈 | 动作后现在是什么状态，下一步该做什么 | 新截图、无障碍/DOM 状态、焦点/窗口事件 | 单凭画面有变化证明原目标达成或因果关系 |
| 业务验收 | 搜索结果、文本实际值、保存文件等目标是否满足 | 与目标匹配的界面或实际业务状态 | 从 click/type 无异常直接推出整任务成功 |

本文将“未达到预先声明的后条件就禁止下一次修改”称为硬门禁；观察或模型反思本身不等于
这种门禁。一个模型 step、一个工具调用、一次动作组和一个原生 down/up 事件也不是同一
粒度。OSWorld 为评测环境；Browser Use 为浏览器代理，二者都不能直接当作所有桌面产品
的实现规范。10 个样本是目的性覆盖，不是全领域穷举或统计抽样。

## 3. 项目逐项证据

| 项目/工具栈 | 已核对的反馈/验证机制 | 对逐键鼠硬门禁的判断 |
| --- | --- | --- |
| OpenAI Computer Use | 官方集成循环先按顺序执行 `actions`，再截图回传；允许同一调用包含 click + type 等动作组。driver helper 不重试原始动作。文档还要求检查应用中的最终结果。[官方指南](https://developers.openai.com/api/docs/guides/tools-computer-use)、[driver 契约](https://developers.openai.com/api/docs/guides/tools-computer-use-sdk-drivers) | 所读基本循环未要求每个低层动作预声明业务条件并经独立验证才能继续；保留批后观察和结果检查，不能表述成“不验证” |
| Anthropic Computer Use demo | `BaseComputerTool.shell()` 执行命令后默认等待再附截图；type 分组执行后汇总输出/错误和截图。工具返回错误与图像，不在该函数内判断任务目标。[computer.py](https://github.com/anthropics/anthropic-quickstarts/blob/9ec32b91df50b0d4906cae64b13c6298055ff40d/computer-use-demo/computer_use_demo/tools/computer.py) | 所读执行器没有固定业务条件硬门禁；动作后视觉反馈由模型继续消费。代码中的固定等待是 demo 实现，不能作为 Mirage 可靠性依据 |
| Gemini Computer Use | 官方文档定义连续动作循环：执行 function call 后回传函数结果、新截图及 URL，供模型决定下一步。[官方指南](https://ai.google.dev/gemini-api/docs/computer-use) | 所读基本循环未要求每次 click/keypress 单独进行条件证明；仍要求新环境反馈 |
| UI-TARS Desktop SDK | `GUIAgent` 每轮先 `operator.screenshot()`，模型预测后顺序 `await operator.execute()`；捕获执行异常、处理返回状态，继续循环或结束。[GUIAgent.ts](https://github.com/bytedance/UI-TARS-desktop/blob/3ac2cb8d946d78d5ff92ad17b318eaafa18c178c/packages/ui-tars/sdk/src/GUIAgent.ts) | 所读 SDK 主循环没有 Mirage 式固定后条件硬门禁；下一轮截图承担反馈。最后 FINISHED 分支不能当作独立最终结果验证已发生 |
| Agent S3 | `Worker.generate_next_action()` 消费当前截图；`_generate_reflection()` 在后续轮次把历史动作和当前截图送反思模型，结果供动作生成器参考。构造参数 `enable_reflection=True`，可关闭。[worker.py](https://github.com/simular-ai/Agent-S/blob/15776a8dc659658b1963816ea006f7af1978e595/gui_agents/s3/agents/worker.py) | 有显式逐轮反思，反例于“CU 都不验证”；反思是模型建议，不等于逐原生事件的确定性后条件门禁 |
| Microsoft UFO² | EvaluationAgent 用轨迹及截图判定 Session/Round 完成情况；文档默认 `EVA_SESSION=True`、`EVA_ROUND=False`，并说明模型评估可能不准确。[evaluation_agent.md](https://github.com/microsoft/UFO/blob/eae3c0a8d168c47cb10df0652e293e092eda1078/documents/docs/ufo2/evaluation/evaluation_agent.md) | 存在专门评估，但这里是会话/轮次粒度，不能说每个 click 都强制调用评估器；[动作文档](https://github.com/microsoft/UFO/blob/eae3c0a8d168c47cb10df0652e293e092eda1078/documents/docs/mcp/action.md)另建议 Observe→Act→Verify |
| Cua Driver | 区分 `effect=confirmed/unverifiable/suspected_noop/partial/refused`；`verified=true` 只用于无障碍回读。文档明确事件投递不等于修改生效，未确认效果时要求继续观察检查。[driver 设计](https://github.com/trycua/cua/blob/4e4f57f60ff13f1b12f688ff845e277c389243f8/docs/content/docs/cua-driver/concepts/how-cua-driver-works.mdx) | 有结构化局部效果确认，是不能删去的反例；无法回读时报告 unverifiable，而非一律把原始键鼠升级为已验证业务成功 |
| Cradle | `SelfReflectionPreprocessProvider` 构造动作前后截图或执行期间视频帧，并把执行错误等信息交给反思流程。[self_reflection.py](https://github.com/BAAI-Agents/Cradle/blob/d7752fccf890d8d3818cd1d435f3705f604a1339/cradle/provider/process/self_reflection.py) | 有动作后反思设施；该预处理源码不足以证明所有运行配置都启用反思，或存在固定条件硬门禁 |
| Browser Use | 主 system prompt 要求每个 step 用浏览器状态检查上一步目标；支持一个 step 多动作，并要求最终 done 的成功声明与实际结果一致。[system_prompt.md](https://github.com/browser-use/browser-use/blob/aa093a843da5edbf323c0b4145c9b6a0db3ecde8/browser_use/agent/system_prompts/system_prompt.md) | 验证融入模型下一步推理；这是模型指令层证据，不能当作宿主代码必然强制成功的证明，也不能把 step 等同于每个按键 |
| OSWorld 环境 | `DesktopEnv.step()` 执行动作、等待并 `_get_obs()`；`evaluate()` 通过任务配置的 getter/metric 判断结果。普通 step 的 reward 在所读实现中为 0。[desktop_env.py](https://github.com/xlang-ai/OSWorld/blob/c0f31bbeec1252599f46b2932fdb1fab7292c23e/desktop_env/desktop_env.py) | 环境层将逐步观察和任务结果评测分开；不能把最终 evaluator 当作生产 Agent 每次输入的验证器，也不排除不同 Agent 自带验证 |

“所读路径未见”只限定上述源码/官方指南，未对完整仓库及所有自定义执行器证明不存在。
没有纳入只有宣传材料或不可见内部执行器的产品，不能替这些产品断言验证策略。

## 4. 为什么程序化输入仍然可能失败

Windows 官方 [SendInput 契约](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
明确说明返回成功插入输入流的事件数，调用可失败；UIPI 会阻止注入，已按下的键也可能
干扰本次输入。成功插入事件的计数并不包含“应用完成业务”的承诺。因此即使动作规划
与轨迹完全确定，执行层仍须检查返回值和失败事实。

Cua Driver 的说明提供了更接近 CU 的实例：有些应用会回显一次写入但不真正应用修改，
背景输入也受平台和目标工具包限制。对 Mirage 的工程推论是：焦点、IME、遮挡、界面
变化、异步处理等均可能使正确投递不产生预想效果；这些需要按任务相关性观察，而非
用“事件无异常”推导业务成功。

反过来，画面不变也不说明输入失败。例如输入框已聚焦时再次点击、按住修饰键、复制、
鼠标移到普通空白处都可能没有可见变化。强制每动作寻找文字变化或画面变化会制造误判。
这组例子是行为语义分析，不是本轮实际桌面测试。

## 5. Mirage 当前门控与可简化位置

本地核对 [DEC-056](../decisions/DEC-056-post-action-verification.md) 及
[DesktopActionVerification](../../integration/mira/src/desktop_action_verification.cpp)：

- `execute()` 在上一记录既非 verified 又非干净拒绝时拒绝下一修改；handler 返回后
  调用 `observe()`。这比单纯“执行动作→回传状态/截图→模型继续”更强。
- `observe()` 按条件读取平台状态、共享新帧或 OCR。当前源码视觉/共享条件的协作预算
  为 5 秒，其他为 1 秒；预算不是每次必然消耗的耗时，未做延迟测量。
- `shared_frame_observed` 的 verified 只证明已释放输入和新帧，reason 为
  `fresh_shared_frame_only`；同一 verified 字样与文本匹配的证据强度并不相同。
- `settle()` 再次观察最近动作，并要求全部记录满足 verified/干净拒绝等规则。
  是否重复了可复用的观察要按帧来源、采集时刻和任务语义审计，不能仅凭函数次数判冗余。
- DEC-056 2026-10-19/20 已记录“目标完成却因尾部干净拒绝/OCR 失败判失败”的维护者
  场景及修复。这是既有工程证据；本轮没有重新复现，也不说明所有剩余门控均有错误。

默认策略按以下职责分配推进（方向已接受，具体契约/接线尚未交付，以 DEC-056 为准）：

| 场景 | 建议行为 | 仍须保留 |
| --- | --- | --- |
| 普通移动、完整点击/按键、组合键、逐键输入 | 本地执行完整序列后回报实际执行结果；不默认强制模型提供 verify_kind 或逐动作 OCR | 返回值/异常、取消/超时、权限/目标守卫、输入释放、未知副作用 |
| 上下文稳定且无需中途分支的动作组，如聚焦搜索框→输入→Enter | 允许一个有界动作组结束后观察，由下一轮模型决定后续动作 | 中途失败/屏障立即停止；不能跨未知焦点变化盲目继续 |
| 下一步依赖状态变化，如等新窗口、输入框出现、菜单展开 | 在依赖处观察；有便宜可靠平台条件时可本地等待/验证 | 有界等待、超时如实报告；不把固定 sleep 当成功 |
| 已声明且确需精确值的目标，如字段实际文本、保存产物 | 在相应业务边界验证，沿用合适的平台/实际值证据 | 未满足与无法读取分开，不能降格为“点过了所以成功” |
| 受控持键期间观察、释放不确定、撤权/接管/设备掉线 | 继续专门的会话状态与安全处理 | 不将执行异常、释放失败与普通“业务效果尚未知”混成可忽略状态 |
| 整任务结束 | 根据用户目标检查最终结果，优先复用仍有效的观测 | 必要时补观察；没有证据时不能宣称业务已验证 |

普通工具回执应表达“序列已完成/被拒绝/失败/不确定”和释放事实；业务后条件是否成立
另行表达，不能把取消业务门禁实现为无条件 `verified=true`。新观察可以同时供反馈与
任务验收使用；如果下一轮本来就需要该截图，不必再为相同问题启动一次独立 VLM/OCR。
执行不确定时是否允许特定恢复动作，需要在策略中单列，不能随一般门禁一起放开。

## 6. 后续验收建议

上述方向已获接受；M7-25 继续冻结回执/状态契约，再由 M7-26/31 实施和验收；
与 DEC-109 的按需感知、DEC-111 的实际值条件一起梳理，避免叠加重复验证器。

- 无通用时间参数的自然轨迹执行完毕、输入已释放时，工具可回报执行结果，无须先
  得到业务后条件；回执不得夸大为业务成功。
- 有界动作组结束后才统一观察；与逐动作门控基线比较任务完成率、错误目标输入、
  假成功/假失败、恢复次数、观察/OCR/VLM 调用数和端到端 P50/P95。
- 注入平台拒绝、部分派发、焦点变化、IME 干扰、应用慢响应、无可见变化、取消、
  shutdown 与释放失败；不能以降低验证次数换取吞错或盲目重复输入。
- 对“执行已完成但业务未知”“业务条件明确不满足”“释放不确定”分别测试可继续、
  重观察/重规划和必须封锁的结果；跨 Windows/X11/Portal 分别留证。

负责人 Mirage 维护者；数值门槛和目标应用在实验前冻结。当前没有证明延迟或成功率收益，
不能将调研建议标成 M7-25/26/31 已完成，或据此关闭任何平台门禁。

## 7. 核对方法与限制

2026-10-20，Linux x86_64。使用官方网页检索/打开、`git ls-remote https://github.com/<owner>/<repo>.git HEAD`
取得表内 8 个仓库提交；以 `https://raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>`
下载并核对所列文件。厂商指南为当天公开页面，无稳定提交号。GitHub tree API 请求因
匿名速率限制失败，改用公开 git ref 与原始文件读取，不影响表内文件核对；没有下载整个
仓库或断言穷举完整代码。临时下载放在仓库外，不修改 pinned 依赖。

文档检查为 `git diff --check`，另核对新增相对链接、单一一级标题、标题层级及围栏。
本轮没有运行外部 Agent、比较模型成本/延迟、执行平台输入测试或更改门控实现；实际
收益与兼容性由上一节后续实验验证。当前关于 Mirage 门控强度的结论为源码/契约证据，
关于新策略优劣的结论为待验工程建议。
