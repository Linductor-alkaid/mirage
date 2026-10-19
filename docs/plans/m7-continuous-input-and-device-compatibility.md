# M7 专项：连续输入、持键观察与设备兼容性

> 状态：Proposed
> 负责人：Mirage 维护者
> 所属计划：[实施总计划](mirage-implementation-plan.md) / [M7](m7-visual-tools-and-input.md)
> 前置：M7-03/09/10/21 已交付能力；未完成的平台门禁继续保留
> 建议发布点：连续输入与设备兼容性预览，具体发布名由维护者另定
> 更新日期：2026-10-19

## 目标与范围

使 Agent 能执行平滑鼠标运动、定时按压、持续组合键，并在持键期间观察和决策；
通过输入法状态与实际目标回显闭合文本输入。设备层适配与动作能力并行评估，验证
对过滤软件注入或依赖设备输入的应用是否具有实际兼容性收益。

本专项不改变现有已验完整动作默认值，不引入独立 Agent 循环、线程/调度体系或
修改 pinned 依赖，不以文档授权驱动安装和硬件部署。兼容性分 OS、后端、应用和
版本取证，不承诺全部程序支持、来源不可识别或硬实时。连续感知不是首增量前置，
受权的按需截图可用于持键观察。

## 设计与决策依据

[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md) 为 Proposed；
[连续输入调研](../research/2026-10-19-continuous-input-and-hid.md)提供来源和当前差距。
沿用 DEC-052/054 的权限/所有权/清理，扩展 DEC-100 的动作语义，与 DEC-109 的观察
及局部门控协同。主设计第 5.1 节索引提案，不把候选契约写成已交付能力。

## 工作项

所有新项初始为 Proposed、未实施，不重新关闭 M7 已完成的历史范围。公共契约与
预算先冻结，再进入相应实现；设备过滤复现与部署选型可在 M7-25 同期只读核对。

| 编号 | 状态 | 结果与验收 | 前置与冻结点 |
| --- | --- | --- | --- |
| M7-25 | In Progress（契约冻结稿待评审） | 已交付平台中立契约层 `input_session.hpp/.cpp`（能力位、会话键词表、预算数值、状态机、版本化回执）与 golden/负向矩阵 `m7_input_session_contract.json`，`input_session_contract_test` 对账通过（1224 检查项）；维护者评审通过后本项标 Completed | DEC-110 评审；冻结摘要见 DEC-110"M7-25 契约冻结稿"；评审意见可直接调整数值并升 budget_version |
| M7-26 | In Progress（X11/Portal 首增量） | Provider 层 opt-in `smooth_move_to`（minimum-jerk 基线、冻结预算复用、段间协作取消/落后停止）与 X11 真实实现（每段 XFlush 实时投递）；共享 minimum-jerk 调度生成器；`desktop.vision.click`/`click_region`/`desktop.session.click`（Portal，会话同源起点，未知起点前置拒绝）的 `glide_ms` opt-in 参数（默认关闭，不带参数行为不变）；真实 Xvfb 轨迹/时间/取消证据与工具正负矩阵另记；定时按压、独立修饰键、组合交叠与 Windows 路径继续推进 | M7-25 冻结契约；逐后端能力 opt-in |
| M7-27 | Proposed | 同一 owner 跨工具观察保持输入；Alt-Tab 正反向及持键显示瞬态界面；模型失联、拒绝、超时、撤权、接管、shutdown、迟到命令与释放失败封锁 | M7-25/26；普通持键与等待决策预算分别验证 |
| M7-28 | Proposed | IME/布局/模式/预编辑与候选状态的部分可观测投影；单独 Shift、候选提交、切窗焦点和最终文本验收，缺状态显式 unknown | M7-25/26，跨轮候选场景接 M7-27；与 M9-15 前端 IME 干扰区分 |
| M7-29 | Proposed | Linux uinput / Windows VHF 的部署选型与受控原型；过滤 fixture、权限/报告容量/断连释放及实际受影响应用对比，分别形成采用或不采用的证据 | 公共动作契约接 M7-25；原型前冻结平台、依赖/驱动来源、部署和恢复方案；集成验收接 M7-26/27 |
| M7-30 | Proposed | 外接 USB HID 的协议/设备/固件选型与原型；相对坐标校准、按键报告、ACK 语义、链路中断释放及目标应用效果；明确硬件部署边界 | 公共动作契约接 M7-25；原型前冻结供应链、硬件和协议；集成验收接 M7-26/27，不等待 M7-29 全平台完成 |
| M7-31 | Proposed | 实际工具下模型自主完成连续动作/持键/IME/过滤应用任务；分平台资源、生命周期、sanitizer 和既有输入回归，提交可复现验收与产品能力清单 | 按已选 M7-26～30 增量验收；执行前冻结任务、阈值与应用/版本清单，未选/未验路径保留开放 |

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
- [ ] 时序/轨迹及 hold/repeat 有真实事件和控件效果证据；各后端能力声明与实测一致。
- [ ] 持键观察、正反选窗、临时界面截图、IME 候选和切窗文本接收端闭环通过。
- [ ] 正常、异常、提交拒绝、执行中取消、超时、shutdown；另含晚到、重复、物理
  竞争、断连和释放失败封锁矩阵，ASAN/UBSAN/TSAN 与故障注入按实现范围通过。
- [ ] 内核虚拟设备与外接 HID 分开记录过滤 fixture 和实际应用结果；没有用“已发送”
  或设备枚举代替目标应用接收和业务后条件。
- [ ] 模型自主任务与脚本测试分开报告，完成率/错误输入/时延/存量门槛预登记并实测。
- [ ] 主设计、决策、M7/总计划、安全文档、示例、schema/golden 和兼容性证据同步。

未执行项的共同原因是本轮仅构造决策、实现与环境选型尚未开始；负责人 Mirage
维护者。补跑条件为对应工作项接线完成，具备目标平台会话、模型、IME、受影响应用
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
