# M7 专项：连续输入、持键观察与设备兼容性

> 状态：Proposed
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md) / [M7](m7-visual-tools-and-input.md)
> 前置：M7-03/09/10/21 已交付能力；未完成的平台门禁继续保留
> 建议发布点：连续输入与设备兼容性预览，具体发布名由维护者另定
> 更新日期：2026-10-20

## 目标与范围

使所有键鼠动作默认由本地自动执行自然时序，模型只表达动作意图，无需提供时间；
普通点击完成移动、按下及释放后，才返回动作结果或进入后置验证。支持定时按压、
持续组合键，并在显式受控持键会话期间观察和决策；
通过输入法状态与实际目标回显闭合文本输入。设备层适配与动作能力并行评估，验证
对过滤软件注入或依赖设备输入的应用是否具有实际兼容性收益。

默认行为迁移按 M7-25/26 分增量冻结和验收，以当日实施记录中的入口/平台范围为准；
未覆盖入口与平台继续开放。
本专项不引入独立 Agent 循环、线程/调度体系或
修改 pinned 依赖，不以文档授权驱动安装和硬件部署。兼容性分 OS、后端、应用和
版本取证，不承诺全部程序支持、来源不可识别或硬实时。连续感知不是首增量前置，
受权的按需截图可用于持键观察。

## 设计与决策依据

[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md) 为 Proposed；
[连续输入调研](../research/2026-10-19-continuous-input-and-hid.md)提供来源和当前差距。
沿用 DEC-052/054 的权限/所有权/清理，扩展 DEC-100 的动作语义，与 DEC-109 的观察
及局部门控协同。主设计第 5.1 节索引提案，不把候选契约写成已交付能力。

## 工作项

各项当前进度见下表，不重新关闭 M7 已完成的历史范围。公共契约与
预算先冻结，再进入相应实现；设备过滤复现与部署选型可在 M7-25 同期只读核对。

| 编号 | 状态 | 结果与验收 | 前置与冻结点 |
| --- | --- | --- | --- |
| M7-25 | In Progress（契约冻结稿待修订/评审） | 已交付平台中立契约层 `input_session.hpp/.cpp` 与 golden/负向矩阵，对账证据见验证记录；新增待交付：所有键鼠动作默认自动规划时序、无模型时间参数的工具契约、普通动作结果/后置验证不得早于执行与释放完成、各类默认时序及总动作预算和兼容迁移；同步代码/golden 后评审 | DEC-110 第 2 节 2026-10-20 维护者要求；当前预算 v1 保留至修订，数值调整升 budget_version |
| M7-26 | In Progress（X11/Portal 完整动作自然时序） | 点击自动接近与有界随机轨迹 v2 已交付；本轮扩展滚动/拖动的接近重验、逐步滚动、按住曲线拖动、键盘停留/组合间隔/逐字文本，契约与验证见下方实施记录。独立修饰键、跨观察持键、组合交叠及 Windows 继续推进 | M7-25 完整动作子集契约；逐后端能力与限制取证 |
| M7-27 | Proposed | 同一 owner 跨工具观察保持输入；Alt-Tab 正反向及持键显示瞬态界面；模型失联、拒绝、超时、撤权、接管、shutdown、迟到命令与释放失败封锁 | M7-25/26；普通持键与等待决策预算分别验证 |
| M7-28 | Proposed | IME/布局/模式/预编辑与候选状态的部分可观测投影；单独 Shift、候选提交、切窗焦点和最终文本验收，缺状态显式 unknown | M7-25/26，跨轮候选场景接 M7-27；与 M9-15 前端 IME 干扰区分 |
| M7-29 | Proposed | Linux uinput / Windows VHF 的部署选型与受控原型；过滤 fixture、权限/报告容量/断连释放及实际受影响应用对比，分别形成采用或不采用的证据 | 公共动作契约接 M7-25；原型前冻结平台、依赖/驱动来源、部署和恢复方案；集成验收接 M7-26/27 |
| M7-30 | Proposed | 外接 USB HID 的协议/设备/固件选型与原型；相对坐标校准、按键报告、ACK 语义、链路中断释放及目标应用效果；明确硬件部署边界 | 公共动作契约接 M7-25；原型前冻结供应链、硬件和协议；集成验收接 M7-26/27，不等待 M7-29 全平台完成 |
| M7-31 | Proposed | 实际工具下模型自主完成连续动作/持键/IME/过滤应用任务；分平台资源、生命周期、sanitizer 和既有输入回归，提交可复现验收与产品能力清单 | 按已选 M7-26～30 增量验收；执行前冻结任务、阈值与应用/版本清单，未选/未验路径保留开放 |

M7-26 新增默认迁移范围（In Progress，分增量交付）：以本地自动时序替代模型传
`glide_ms` 才平滑的启用条件；覆盖移动、单击/右键/多击、拖动、滚动、按键、组合键
和逐键文本输入，以及语义目标转 Pointer Action、视觉编号/区域、共享会话等入口。
同一动作 lease 内完成轨迹与按压/释放再结算；后置验证不得在途中启动。模型工具
以不暴露通用时间入口为目标，内部 Provider 可接收本地规划出的具体时长。不能因
模型省略时间、后端能力不足或起点未知而静默切回瞬移。旧 `glide_ms` 兼容与预算
随 M7-25 修订，真实平台、跨入口和取消/完成顺序证据并入 M7-26/31。

2026-10-20 维护者接受的验证策略按 [DEC-056](../decisions/DEC-056-post-action-verification.md)
实施：M7-25 还须冻结执行/观察/业务条件分离的回执、动作组及条件依赖/恢复契约和
版本兼容；M7-26 取消普通输入默认 `verify_kind`/逐动作业务硬门禁，组末/依赖处按需
观察，迁移 `execute`/`observe`/`settle` 与工具说明；M7-31 验证其实际调用数、任务
结果及保留的输入安全围栏。v2 门控子集的契约/golden 与会话接线已完成，见下方
实施记录；默认自然时序、新动作组接口和未验平台仍保持开放。

动作链为 M7-25 → M7-26 → M7-27；IME 与设备路线按各自前置推进，M7-31 分增量
验收。每项只有在已声明的结果及全部适用门禁通过后才标 Completed；多个平台范围
未全验时保留工作项开放，并记录已通过的限定子范围。

## 生命周期、容量与失败约束

Runtime Service 继续持有进程内唯一 Executor owner，Task 的输入会话持有单写者
lease、键状态、任务/timer 句柄与清理结果。有限推进消费 future；轨迹与期限使用
公开 timer，状态版本阻止迟到回调；阻塞传输使用公开 blocking worker。跨上下文
按语义选用 comm，不自建队列/调度器，拒绝/失败/背压由 Executor 设施观察。

禁止无限持键、无限命令、隐式续租、自动设备热切换和不确定输入重发。清理只处理
本任务资源，物理同键竞争与设备离线按 DEC-110 封锁；shutdown 由外部非 worker
停止生产、取消/释放、回收 worker、结算 future 后执行。soft deadline 的释放延迟
需实测，不把排队超时当作强制中断。

## 风险与阻塞

| 项目 | 处置/解除条件 | 负责人 |
| --- | --- | --- |
| 持键回执与现行 input_released 语义冲突 | M7-25 冻结稿已定版本化回执 v1：`holding` 独立表达正常持有，`release_confirmed` 仅在 released 置位，旧客户端不接新持键回执；待评审 | Mirage 维护者 |
| 焦点切换与物理输入竞争 | 明确允许的转换、接管与封锁，受控和真实桌面验证 | Mirage 维护者 |
| 模型等待期间预算耗尽 | 不无限续期，释放并回报真实副作用，慢模型/失联故障注入 | Mirage 维护者 |
| 未知具体过滤应用 | 先用有规则 fixture；真实应用清单与环境到位前兼容性收益保持未验证 | Mirage 维护者 |
| 驱动/设备部署、签名与固件 | 原型前单列选型/供应链和恢复条件，无环境不安装、不关闭门禁 | Mirage 维护者 |
| IME 状态访问限制 | Backend 投影部分状态并标 unknown，任务相关反馈补证 | Mirage 维护者 |

## 测试与退出条件

- [ ] M7-25 的公开契约、数值预算、默认值与迁移文档冻结，依赖公开能力核对完成。
- [ ] 所有适用键鼠入口不传通用时间参数仍有实际自然时序；模型慢响应不阻塞已准入
  轨迹。记录事件时刻，证明成功动作结果及后置观察/验证均在完整动作最后释放之后，
  工具最终返回遵循所选验证契约；持键 `holding` 与最终释放验证单独验收。
- [ ] 普通动作不带 verify_kind 可返回实际执行结果并继续；有界动作组统一观察、首个
  失败停止并报告前缀；条件失败只阻止依赖步骤，合规恢复不降低原条件。普通业务
  未知、已知部分失败及执行/释放 uncertain 分别验收；终态复用有效证据，证据不足
  不假成功。与旧门禁对照记录假成功/假失败、错误输入、调用数及端到端时延。
- [ ] 时序/轨迹及 hold/repeat 有真实事件和控件效果证据；各后端能力声明与实测一致。
- [ ] 持键观察、正反选窗、临时界面截图、IME 候选和切窗文本接收端闭环通过。
- [ ] 正常、异常、提交拒绝、执行中取消、超时、shutdown；另含晚到、重复、物理
  竞争、断连和释放失败封锁矩阵，ASAN/UBSAN/TSAN 与故障注入按实现范围通过。
- [ ] 内核虚拟设备与外接 HID 分开记录过滤 fixture 和实际应用结果；没有用“已发送”
  或设备枚举代替目标应用接收和业务后条件。
- [ ] 模型自主任务与脚本测试分开报告，完成率/错误输入/时延/存量门槛预登记并实测。
- [ ] 主设计、决策、M7/总计划、安全文档、示例、schema/golden 和兼容性证据同步。

上述整体门禁尚未全部执行；已完成的限定增量见下方记录。默认自动时序迁移、完整
键鼠覆盖及相应平台/模型/设备验收仍待实现或环境到位；负责人 Mirage 维护者。
补跑条件为对应工作项接线完成，具备目标平台会话、模型、IME、受影响应用
及所选驱动/硬件。不能以某平台、fixture 或静态检查代替这些门禁。

## 验证记录

2026-10-19：依据维护者讨论新增 DEC-110 与 M7-25～31 Proposed 项，同步 M7、
总计划、主设计提案索引及调研。仅检查本轮文档差异、编号、链接和空白；未改变
产品或 pin，未执行构建、模型/桌面输入、设备/驱动原型或平台验收。

2026-10-19：M7-25 契约冻结稿交付。`desktop/environment` 新增平台中立契约层
（`input_session.hpp`/`input_session.cpp`，纯类型与纯函数，无并发路径）与 golden
`tests/desktop/data/m7_input_session_contract.json`；独立验证代理编写
`input_session_contract_test`（1240 检查项：golden 全量对账、48 条状态×事件矩阵、
键词表往返、30 条命令正负矩阵、旧 chord 词表回归、回执默认值），debug 与 ASan
均通过（0 失败、无 sanitizer 报告），既有 action/provider/basic_contract 回归通过。
第一轮发现并修复两处冻结稿缺陷（`input_session_outcome_name` 缺定义；golden 8 行
命令用例缺注入源能力位），复验确认修复未引入语义偏差。未接线 Runtime/Provider、
未注册工具、未执行真实输入或平台验收；M7-25 完成待维护者评审。

2026-10-19：M7-26 X11 首增量。Provider 层新增 opt-in `smooth_move_to`
（minimum-jerk 基线，复用 M7-25 冻结预算，段间协作取消/落后停止/超时）；
X11Backend 真实实现（XQueryPointer 起点、零距离短路、4ms 节奏下限、250ms 落后
停止、每段 XTestFakeMotionEvent + XFlush 实时投递）；`desktop.vision.click` 与
`desktop.vision.click_region` 新增可选 `glide_ms`（50–2000，默认不带=原行为不变，
带参数时该动作墙钟上限扩为 glide+1000ms，旅行失败不点击并如实回报）。
独立验证代理产出真实 Xvfb 事件级证据：600ms/400px/40 段旅行期间服务端采样到
minimum-jerk S 形中间位置（202→598）、实际耗时 600ms、150ms 取消停在 (241,300)
与理论 eased 值精确吻合（8 轮零 flake）；负面矩阵（49/2001ms、65 点、timeout
不覆盖）全部无副作用拒绝；工具层 128 项正负检查（默认零 move 调用、脚本失败
不点击、能力缺席 unsupported_platform）；`smooth_move_contract_test` 37 项。
debug 全量 99/99 通过，ASan 无报告。过程中验证代理抓出 Xlib 输出缓冲缺陷
（缺每段 flush 时服务端只见瞬移、取消声称未发生的副作用），已修复并复验。
落后停止分支在 Xvfb 单机不可注入延迟、未直接演练；真实合成器/XWayland 节奏、
定时按压/独立修饰键/组合交叠、Portal/Windows 路径继续推进。

2026-10-19：维护者实测两个问题修复（会话取证：点击走 `desktop.session.click`
Portal 共享路径，且目标已被先前 verified 动作达成后仍被判失败）。其一，glide 接入
共享路径：`SharedInputAction.glide_ms`（仅 Click）、`build_smooth_move_schedule`
共享最小 jerk 调度生成器（X11 同步下沉复用）、Portal 派发按样本节奏 notify（同步
DBus 调用天然无缓冲问题）、会话自记录指针位置作为同源起点（Portal 无绝对回读，
未知起点以 `pointer_origin_unknown` 前置拒绝而非瞬移）、`desktop.session.click`
schema 新增可选 `glide_ms`（50–2000，非 click/越界拒绝）。验证代理抓出并修复
2x 缩放会话下样本被二次缩放的缺陷（HiDPI 会点到一半位置）；脚本桩证据：逐样本
命中逻辑坐标、末 motion 精确落点、取消无 button、pointer 出参回写。其二，DEC-056
结算修订：尾部干净无副作用拒绝 + 拒绝后成功桌面观察 + 作答 → 回合 ok（新增
note_observation 通路）；端到端复现维护者场景并通过；harness 层/service 级 7 个
旧契约场景按新契约更新期望，throwing（uncertain）/cancelled/expired 不变。debug
全量 100/100、native-release 108/108，ASan 无报告。真 Portal 实机 glide（混合
DPI）待维护者下一次会话实测。

2026-10-20：维护者第二次会话取证修复。其一，glide 起点持久化：会话记录显示模型确实
传了 glide_ms 但两次 `pointer_origin_unknown`——起点原本活在 CastSession，普通
capture churn/回合边界即丢（回执里 observation_only=true 佐证 cast 中途重建）。起点
状态提升为后端级 `PortalPointerState`（进程生命周期，跨 cast 重建与回合边界），任务内
任何一次已派发点击/拖拽后 glide 即可用；`pointer_origin_unknown` 的派发 message 与
canonical 回执新增顶层 `hint` 字段（走 refreshed_image_scope 同路径，512 降级后仍
存活——验证代理抓出内嵌 JSON 方案恰好 513 字节触发降级吞掉指引的缺陷，已改为顶层
字段并有确定性降级测试）。其二，DEC-056 再修订：`validate_request` 对 OCR/mark 失败
改为有序重推导——失败视觉读取之后的任何成功桌面观察（vision/capture/listing）都重新
支撑回答；维护者 turn-1（OCR 超时但点击已 verified、模型以 exec+观察作答被误判
"桌面视觉读取失败"）的端到端复现场景转为 ok，失败后无观察、最后视觉失败等旧保护保留。
独立验证：OCR 矩阵 5 场景 + 单元矩阵、hint 完整性与降级存活、portal_input 404 项、
post_action 764 项；debug 全量 100/100、native-release 108/108，ASan 无报告。
真 Portal 环境的 PortalPointerState 注入管线仍只有间接覆盖（portal_screen_fixture
无法驱动两次真实派发），待维护者下一次会话实测 glide 连击。

2026-10-20：M7-25/26/31 默认自然输入要求补充（`docs/dec110-natural-input` 工作树，
未提交）。依据维护者本轮要求与 DEC-110 第 2 节，明确全量键鼠默认本地自然时序、
常规工具无需模型时间参数，以及普通完整动作执行/释放后才返回动作结果或进入
后置验证；受控持键的 `holding` 单独结算。同步 DEC-110、DEC-100 迁移补充、主设计、
本专项、M7 与总计划；M7-25 保持 In Progress，新增契约/golden 修订及默认迁移待交付。
已核对 Mira `environment.hpp`、现行视觉/区域/共享工具 `glide_ms` 路径，以及 pinned
kairo 集成指南的 by-requirement/scheduling 和公开 timer/cancellable 接口；沿用
既有 Executor 生命周期，不新增调度设施或依赖反馈项。

验证：Linux 7.0.0-38-generic x86_64，纯文档工作树；`git diff --check` 通过。
Python 3 标准库逐文件检查 6 份变更 Markdown 的相对文件链接、单一一级标题、标题
层级和代码围栏：428 个本地文件链接，0 错误；引用契约/工具文件存在且与上述差距
描述一致。未改代码、schema/golden 或依赖，未执行构建、sanitizer、真实输入或模型
验收；原因是本次仅修订要求。负责人 Mirage 维护者；补跑条件为 M7-25/26 实现接线
完成并具备目标后端/桌面与模型环境，新增门禁保持未勾选。

2026-10-20：按维护者要求调研 CU 项目的键鼠验证粒度，关联 M7-25/26/31，
交付[执行与验证门控调研](../research/2026-10-20-computer-use-action-verification.md)。
覆盖 10 个代表性项目/工具栈，8 个公开仓库以 `git ls-remote` 获取提交并逐文件下载
核对一致；厂商官方指南核对动作组、动作后截图与最终结果检查。源码显示 Mirage
当前逐动作固定后条件门禁强于基础截图反馈循环，建议改为按依赖/动作组/任务边界
验收，但不据此假设程序化输入不会失败。建议保持 Proposed，DEC-056 现行契约及
代码未变；若采纳，M7-25 需先修订状态/回执/恢复语义，M7-26/31 对照实验再验收益。

验证环境为 Linux x86_64，HEAD `d09fa8d` 加本轮文档工作树；`git diff --check` 通过，
相对链接、标题层级与代码围栏已核对。未运行外部项目、模型/桌面测试、性能基准或
sanitizer，原因是只读调研及文档变更；负责人 Mirage 维护者，补跑条件为新策略确认
并实现、目标后端/应用及对照实验环境具备。原有工作项状态和未验平台门禁保持开放。

2026-10-20：维护者批准更新决策，DEC-056 新增 Accepted 的执行/反馈/业务验收分层
修订：普通输入默认无逐动作业务门禁，动作组及状态依赖处观察，终态可复用有效证据；
显式条件约束依赖步骤，恢复不降格原条件；已知失败与执行/释放 uncertain 分开处置。
DEC-059/068 增加部分替代说明，DEC-109/110/111、主设计、安全契约、文本验证专项、
M7/总计划与调研状态同步。DEC-109/110 的其他候选能力仍 Proposed，M7-25/26/31
及 M7-32～34 保持原未完成状态；未改 schema/golden、代码或依赖。

验证：Linux x86_64，HEAD `d09fa8d` 加 `docs/dec110-natural-input` 未提交工作树；
`git diff --check` 通过。Python 3 标准库检查当前 14 份变更 Markdown：506 个本地
文件链接存在，单一一级标题、标题层级与代码围栏 0 错误；差异复核确认历史证据保留，
已接受策略与待冻结字段/实现边界分开。未执行构建、sanitizer、平台输入或新门控测试，
原因是本次仅决策/文档更新；负责人 Mirage 维护者，补跑条件为 M7-25 契约及
M7-26 接线完成、目标后端/应用/模型具备。新增自动化与平台验收门禁未勾选。

### 2026-10-20：M7-25/26/31 分层门控 v2 首增量

- 范围：按 DEC-056 Accepted 修订完成普通输入无默认业务条件、可选 depends_on、
  执行/条件/观察分离回执及无采集结算。区域/共享 verify_kind 改为可选，未请求时
  不后采集/OCR；独立恢复不删除原条件。未知执行副作用与释放失败仍封锁，较早
  进程运行证明不能清除较新键鼠 uncertainty。
- 契约：修改工具 3.0.0、verify 2.0.0、verification.policy_version=2；最多 8 个唯一
  条件依赖，当前 authority/动作 epoch/5 秒本地有效期，32 活动/128 归档记录。
  新鲜度在供给模型请求时判断，条件绑定仅由该请求下一次修改尝试消费；原目标/
  焦点/像素守卫继续检查，派发入口不采集新图。
  保留外层输入 schema=2，input_session_budget v1 未改。golden 为
  `tests/integration/data/desktop_action_policy_v2.json`，细节见 DEC-056 首增量契约。
- 状态：本首增量源码与下述验证已交付；M7-25/26 继续 In Progress，M7-31 整体
  保持 Proposed。新动作组、默认自然时序及未验平台不关闭，精确值仍由 M7-32～34 交付。
- 测试：新增 desktop_action_policy_test，757 项检查覆盖无条件输入/100 次归档、
  条件依赖/恢复、过期/epoch/authority、取消/未知输入/释放失败、进程证明不抵消未释放、
  较早证明不解新围栏、慢模型消费、单次条件绑定及原派发检查、
  schema/golden 和过程投影。模型任务提示同步取消逐动作必选条件，区分按需依赖与恢复。
  完整会话新增无条件输入→新观察→终态；共享脚本桩证明
  默认不后采集，settle 不增 capture/OCR。原 OCR 的授权/来源/预算负例改为显式重验
  或依赖操作，未删除原守卫。原生过程卡 execution_only 显示“已执行”。

环境：Linux 7.0.0-38-generic x86_64，GCC 13.3.0、CMake 3.28.3，HEAD `d09fa8d` 加
`feat/desktop-input-policy` 未提交工作树；Mira `a29374a`、kairo `32027c2`、Mirador
`cb45a10`、eui-neo `63117f7`，pin 未变。debug 为 native/OCR OFF；asan/ubsan 为
native ON、OCR OFF；native-release 前端编译使用其现有产品配置。未新建生产异步
路径、线程、队列或 owner；新增到期测试用 Executor timer、消费 future 并在外部 shutdown。

实际执行：

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset debug -j 4 --target desktop_action_policy_test
ctest --preset debug -R '^desktop_action_policy_test$' --output-on-failure
```

全量 101/101 通过；策略矩阵（含 5 秒到期、慢模型及释放失败）757 项/0 失败。完整会话
实际完成 4 个私有 X11 任务：激活、文本回显、提交落盘、拒绝后恢复。初轮旧测试的
自动结算采集/全局业务围栏断言按已接受的新契约调整，负例转为显式重验/依赖后通过。

ASan/UBSan 分别执行以下同参命令，preset 占位依次为 asan、ubsan：

```bash
cmake --build --preset <preset> -j 2 --target desktop_action_policy_test post_action_verification_test desktop_image_supply_test visual_marks_test gui_fallback_test visual_text_verification_test
ctest --preset <preset> -R '^(desktop_action_policy|post_action_verification|desktop_image_supply|visual_marks|gui_fallback|visual_text_verification)_test$' --output-on-failure -j 2
```

每种 sanitizer 合计 6 个不同相关用例通过，无 sanitizer 报告；夹具 setup/cleanup 另计。

```bash
cmake --build --preset tsan -j 2 --target desktop_action_policy_test post_action_verification_test
setarch x86_64 -R ctest --preset tsan -R '^(desktop_action_policy|post_action_verification)_test$' --output-on-failure -j 2
cmake --build --preset native-release -j 2 --target mirage-native
```

TSan 定向 2/2 通过，无 race 报告；直接运行原命令曾在 main 前因
`unexpected memory mapping` 失败，采用上述仅对测试进程关闭 ASLR 的命令后通过，
不改系统设置。native-release 前端编译通过。文档、golden JSON 与 `git diff --check`
已检查，变更仅在自研源码/测试/文档内。

限制：未安装/重启产品；在线模型/真实 GNOME Portal 的新策略验收、Windows 构建/
实机及端到端耗时/成功率对照未执行，脚本桩及 X11 证据不能替代。负责人 Mirage
维护者，补跑条件为对应 OS/授权会话、应用/模型就绪并预登记任务/阈值。当前
glide_ms 仍 opt-in；新跨工具动作组、部分执行前缀及精确文本取证继续按各工作项
推进，不以本次门控测试通过宣称已交付这些能力。


### 2026-10-20：M7-25/26 自动接近点击与 native-release

按维护者继续实施要求，将视觉编号、图片区域、Portal 共享点击切到本地默认自动接近，
不依赖模型回传时间；工具 4.0.0 不再接收 glide_ms。独立 motion policy v1 及 golden、
Portal 同源起点、到达守卫、完整结果边界与限制见 DEC-110 文末。

本增量验证记录：[自动接近点击验收](../compatibility/automatic-pointer-approach-20261020.md)。
M7-25/26 仍 In Progress；默认滚动/拖动/键盘自然时序、完整动作组/部分前缀和 Windows
等原未完成范围不关闭；native-release 构建状态按该记录填写，不把编译当作实机模型验收。

限定结果：Debug 101/101；ASan/UBSan 各 10 个不同相关用例、TSan 5 个不同用例通过；
native-release 完整 product 及 10 个不同相关 release 用例通过。无运行中产品实例，
未启动应用或做真实桌面输入；精确命令、负例修正与实测限制见该验收记录。


### 2026-10-20：M7-26 最新会话起点拒绝修复

最新会话的两次共享点击在派发前报 pointer_origin_unknown，随后键盘因 epoch
已推进而报 interactive_image_required。真实 GNOME 只读探测复现：视频正常，
Cursor 元数据缺失。修正 Format 后协商与 Cursor 容量后，同流有效起点/图像尺寸/
来源序号检查通过；没有发送桌面输入。

增加租约前起点预检，未知起点的无副作用拒绝保留原 epoch，允许仍有效的图像引用
用于独立键盘操作；不削弱移动后/释放失败围栏。回归与 native-release 更新结果见
[验收记录的故障修复增量](../compatibility/automatic-pointer-approach-20261020.md#最新会话故障修复增量)。
真实模型完整点击及手感验收仍开放，M7-26/31 未关闭。


本修复回归：Debug 101/101；ASan/UBSan/TSan 各 3 个相关用例（含夹具各 5/5）通过；
native-release 完整 product 及 5 个相关用例（含夹具 7/7）通过。没有运行中产品实例，
未启动/重启产品；真实 GNOME 证据限定为只读 Cursor 起点，不发送点击/键盘。


### 2026-10-20：M7-25/26 有界随机轨迹 v2

维护者要求加入曲线路径和随机性。自动点击在本地生成随机种子，将基础时长加
±12% 波动并维持 120～650 ms 上下限；平滑弧线、末端微调与部分轻微过冲按距离/
目标宽度收紧，屏幕边界裁剪，最终精确落点后重验/点击。旧显式时长 API 的确定性
基线保留，不新增模型参数或执行生命周期。固定种子用于回放和属性测试。

验收结果及 native-release 更新见
[有界随机轨迹验收](../compatibility/natural-pointer-motion-20261020.md)。
M7-25/26/31 仍开放，真实模型/手感及未实现平台不因本次轨迹测试而关闭。


本增量最终结果：Debug 101/101；ASan/UBSan/TSan 各 5 个相关用例（含夹具各 7/7）
通过，无 sanitizer/race 报告；native-release 完整 product 及 8 个相关用例（含夹具
10/10）通过。固定种子与旧基线合计 18559 检查通过。真实 GNOME/其他 compositor
模型点击、手感/人类数据校准仍开放；未启动或重启产品，未进行真实桌面输入。

### 2026-10-20：M7-26 Cursor-only 缓冲误关闭共享流修复

最新会话末轮两次点击均已执行并释放，第二次后置观察报 invalid_stream_frame，随后
截图/OCR 因共享关闭连续失败。Mutter 46.2 源码与隔离复现显示 Cursor-only 缓冲使用
size=0 / CORRUPTED，旧 consumer 把它视作映射故障，且会丢弃此前待消费的完整帧。
现仅消费其元数据并归还缓冲；有效像素来源序号/时间戳不因鼠标更新提升，新帧与桌宠
隐藏围栏继续生效。没有新增并发或依赖变更。复现、回归及 native-release 证据见
[自动接近验收的共享流修复增量](../compatibility/automatic-pointer-approach-20261020.md#cursor-only-缓冲误关闭共享流修复)。
native-release 完整 product 更新，最终 Portal 回归 5/5（含 2 夹具），新增缓冲矩阵
55/55，ASan/UBSan 各 1/1；真实 GNOME 只读画面/起点检查通过，未发送真实输入。
本次不关闭 M7-26/31 的真实模型及完整键鼠覆盖门禁。

### 2026-10-20：M7-25/26 完整滚动、拖动与键盘自然时序

按维护者要求新增 input_cadence_policy v1，限定 X11/Portal 已有完整动作：滚动与
拖动复用点击的自然接近和到达重验，滚轮按原方向逐步派发，拖动按住后走有界曲线、
精确落点并停留后释放；chord 依次按下/停留/逆序释放，文本按字符停留和间隔派发。
动作内记住触发清理的取消状态，不因释放后探针恢复 false 而丢失回执原因。

完整动作与文本预算分别为 3 s / 15 s，并受父期限约束；完整计划不足时在本阶段输入
前拒绝。模型无时间入口，相关工具升为 4.0.0，保持 DEC-056 v2 可选业务条件和原输入
释放围栏。没有新增线程/后台调度/跨工具持键或 pinned 依赖修改。实现、兼容性、准确
构建测试命令及最终结果见
[完整动作自然时序验收](../compatibility/natural-complete-input-20261020.md)。
最终 Debug 全量 102/102、ASan/UBSan 各 9/9、native-release 17/17（均含夹具）
通过，完整 product 更新，固定种子属性矩阵 79108 检查通过；没有发送真实桌面输入。
M7-25/26/31 仍开放；独立修饰键、组合交叠、跨观察持键、IME、Windows 与真实模型
平台/手感验收未以本次夹具测试替代。


### 2026-10-20：独立修饰键完整 tap

按维护者追问扩展 key 4.0.0 的完整动作词表：裸 ctrl/alt/shift/meta 映射逻辑左侧
键，自然停留后在同次调用中释放；is_base_key_symbol 与受控会话键词表不变，
ctrl+shift 等修饰键结尾组合继续拒绝。X11/Portal 使用已有所有权/清理路径，
Win32 同步键映射但本轮未在 Windows 编译或运行；不宣称已支持 Windows 完整
动作或自然时序。Shift 被用户按住时 X11 拒绝接管；运行中取消仍释放并保留
cancelled 回执。跨工具持续按住、左右侧会话执行与 IME 继续开放。详见
[本轮证据](../compatibility/dec109-111-input-progress-20261020.md)。

本轮最终 Debug 103/103；native-release、ASan、UBSan 定向各 10/10（含夹具）
通过，完整 native-release product 更新，未启动产品或发送真实桌面输入。
新增 Portal Shift 取消用例先发现沿用文本预算的夹具错误，修正后保留了无副作用
预算拒绝与真实按下后取消/释放两项断言；产品预算和取消逻辑未因此放宽。
