# M10：基础 Agent 工具集

> 状态：In Progress（增量 A 全部交付；增量 B 交付至 Linux 后端与会话接入）
> 负责人：Mirage 维护者；平台验证由对应平台维护者执行
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M1 Provider/权限、M5 异步确认、M6 会话 owner、M7 会话工具注册的已交付能力
> 建议发布点：Linux 基础工具预览 → Windows 四工具预览；标签未定
> 更新日期：2026-10-16

## 目标

使 Mira 在 Mirage 真实会话中可直接调用 `exec`、`read`、`write`、`edit`，完成
“读文件 → 修改/生成文件 → 运行程序 → 读取验证结果”，与桌面观察/输入工具组合。
每次调用可取消、有预算、有明确权限与结构化结果，不复制 Mira 的 Agent 循环。

除 M10-01～05、07 已交付外，其余任务未实施；M10-02 前的"契约待冻结"说明已被
DEC-095 冻结取代，冻结值见该决策与下文预算表。

## 范围与非目标

范围：四工具 schema/提示、Provider 加法契约、Linux/Windows 文件与进程适配、
会话接入、目录配置、异步权限、版本冲突、输出读取、取消/shutdown、UI 摘要和验证。

首版不含交互 stdin/PTY、后台 job/poll、持久 shell、远程 SSH 环境、专用 grep/glob/ls、
多文件 patch、二进制编辑、通用 OS 沙箱或代码索引。read 先支持 UTF-8 文本；图像/PDF
另循现有视觉/文档能力，不在文本结果中嵌 base64。搜索可通过已授权 exec 使用已安装命令，
但不能假定 Windows 有 rg/Bash，也不能以 exec 绕过一次明确的策略拒绝。

不要求先关闭 M8/M9 全部门禁；复用其已有 owner/预算/取消接口，回归持续会话与
上下文压缩后的工具历史即可。

## 设计与决策依据

- [四项目调研与 Mirage 差距](../research/2026-10-16-basic-agent-tools-survey.md)。
- [DEC-095 提案](../decisions/DEC-095-basic-agent-tools.md)：本里程碑的待冻结选择。
- [主设计](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)第 5/12/15 节：
  Provider、Runtime owner 与权限边界。
- [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)、
  [DEC-010](../decisions/DEC-010-m1-permission-framework.md)、
  [DEC-020](../decisions/DEC-020-permission-async-confirmation.md)：范围、预算、取消与确认。
- [DEC-024](../decisions/DEC-024-desktop-atom-toolset.md)、
  [DEC-053](../decisions/DEC-053-conversation-desktop-tool-registration.md)：原子/会话边界。
- pinned Mira：`tool_executor.hpp`、`model_tool.hpp`、`model_contracts.hpp`、
  `conversation_loop.hpp`、`json.hpp` 及 `docs/api/model-agent-loop.md`。
- pinned kairo integration skill 的 scenario / blocking I/O card 和公开 API。

当前结论：依赖已提供工具框架和并发机制，本机文件/进程能力归 Mirage；无需升级依赖。
如果实现验证发现公开 API 确实无法承载，再按工程规范登记反馈，不能预先把本机适配缺口
归给 Mira，也不能修改 third_party 掩盖问题。

## 当前缺口与落点

| 层 | 当前状态 | 计划变更 |
| --- | --- | --- |
| desktop/filesystem | 只有 read_text_file，超大小拒绝 | 新增分页结果、版本/观察依据和 create/replace/edit 契约；保留原 API 行为 |
| desktop/process | 命令/预算/取消与双流结果，无 cwd | 加法 ProcessRequest/overload，提供 cwd 与实际 shell；不使用进程级 chdir |
| platform/linux | 文件只读、sh、进程组回收 | 安全打开/分页/临时文件提交、cwd、输出 sink |
| platform/windows | cmd 与 Job Object；没有 FilesystemProvider | 增加文件 Provider、路径/编码/句柄替换；单独验收 |
| integration/mira | 工作流有 read/execute，会话无二者 | 独立基础工具注册函数/小型 toolset，共享 Provider；不继续扩大桌面输入 handler 文件 |
| runtime/service | 已有 owner、权限和模型接线 | 持有 cwd/root 配置、版本观察与输出资源，桥接取消/权限/事件 |
| apps/ui | 已有过程卡片与权限确认 | 显示路径/cwd、退出状态、写入/冲突事实和有界 diff；原文按需查看 |

## 契约草案

### 1. 名称、目录与上下文

模型显示名拟为 `exec/read/write/edit`，内部 canonical 分别为 `local.exec`、
`local.read`、`local.write`、`local.edit`。利用现有 alias provider
显式映射并还原规范历史；先验证 hosted 名限制、名称冲突和三方言，再冻结。若不允许短名，
统一采用 `local_exec/local_read/local_write/local_edit`，不能靠提示词声称已开放短名。

Runtime 持有每 session 的稳定 workspace cwd；relative path/workdir 均相对此目录，
不依赖服务进程 cwd，不在 shell 内拼接 `cd ...`。无 workspace 时只接受绝对文件路径，
exec 返回 `workspace_required`；不猜测用户 home 为默认可写根。cwd 只决定定位，
read_roots/write_roots 决定文件工具边界，权限判定另行叠加。

### 2. 四工具输入与结果

| 工具 | 建议输入 | 结果中的必要事实 |
| --- | --- | --- |
| exec | `command`，可选 `workdir`、`timeout_ms` | resolved cwd、shell、exit_code/终止原因、stdout/stderr、有无截断、可续读输出引用 |
| read | `path` 或 `output_ref` 二选一；可选 `offset`、`limit` | 行号/范围、文本、eof/truncated、续读位置、文件版本/观察范围或输出有效期 |
| write | `path`、`content` | created/replaced、applied、实际字节数、新版本、有界 diff 摘要 |
| edit | `path`、`old_string`、`new_string`，可选 `replace_all=false` | applied、替换数、新版本、有界 diff 摘要 |

`read` 的 path/output_ref 互斥由 schema 支持范围和 handler 双层验证；不能让未识别字段
进入机制层。输出引用不是宿主文件路径，adapter 按当前 session 解析受控输出存储，
再使用同一分页呈现；不自动扩大 read_roots。Mira 的规范结果使用 JsonValue /
ToolExecutionRecord / ToolResultPart，沿现有过程 decorator 回填。不得只更新 UI。

read 行号从 1 开始；输出截断返回实际 next_offset。只在确认 EOF 时返回确定总行数，
不为计算总行数强制扫完整文件。超长单行无法在当前预算内完整返回时给稳定
`line_too_long` 及长度事实，不静默删字符后允许 edit；offset 越界有明确错误。
空文件、无末尾换行、CRLF/BOM、非法 UTF-8、二进制与特殊文件分别定义。

write 新建默认 create-if-absent；覆盖要求本会话同版本的完整文件观察。edit 可以
使用同版本的已读取行片段，但 old_string 的所有命中范围必须处于观察范围；
replace_all 需要完整观察，避免修改未看到的重复片段。由 host 保存观察状态，模型
不能自报“已读”或提供伪造 revision。被缓存容量淘汰或重启后的观察必须重新取得；
成功 write/edit 可更新本会话观察，不要求机械重读已提交内容。

edit 的 old_string 非空；new_string 可为空实现删除。默认匹配恰好一次，零次/多次均不写。
无变化返回 `applied=false`；不以编辑成功伪装修改。首版不做 Unicode/空白 fuzzy 修复；
可将 CRLF 视图归一为 LF，但必须保留 BOM 和未修改行的原始字节。混合换行必须明确支持
或拒绝，不自动重写全文件。

### 3. 文件提交与平台边界

版本至少绑定真实文件身份及有界内容摘要；不能只比较 mtime/size。处于可修改大小上限内的
文件在 read 时取得一致的全文摘要，但只缓存摘要和观察范围；超过修改上限的大文件仅供
有界分页，不为生成可写版本强制扫描全文，write/edit 明确拒绝。Provider 持有路径解析/
打开/比较/提交机制，integration 不直接调用 OS 文件 API。修改前、确认返回后和提交前
重新检查取消、deadline、权限策略快照有效性、目标身份与版本。

创建只使用无覆盖提交；已有文件写到同目录临时文件，完整写入并检查错误后提交，清理失败
临时文件。保留适用权限/属性；写入的崩溃持久性与目录同步要求在 M10-02 明确，不能把
原子可见性写成断电持久性。初版父目录不存在时拒绝，自动建目录另列后续能力。

文件根检查不能只 canonicalize 后按路径重开；使用目录/文件句柄锚定，验证最终目标。
初版写路径含 symlink/reparse point 或目标为多硬链接文件时拒绝并解释，避免作用于
范围外对象或改变链接语义。Windows 还需覆盖大小写、盘符、UNC、设备路径、ADS、共享
占用与只读属性。普通读路径也需防检查后换链竞态。API/保证不足的平台 fail closed。

本进程按文件身份持有有界单写租约，冲突立即 `busy`；不建立 ad-hoc 排队器。
版本重查与 rename/replace 之间对任意外部写入不存在通用文件系统 CAS：首版只承诺
拒绝已检测冲突与不暴露半文件。必须用故障注入记录最后提交窗口；若要求严格跨进程
不丢更新，需要协作锁或更强文件系统能力，不能通过本计划虚构保证。

取消与提交点竞态：提交前取消清理临时文件、`applied=false`；提交后不回滚用户可能已
再次修改的文件，保留 `applied=true` 的 operation 事实。任务可进入 Cancelled，
但不能向用户声称没有发生写入，迟到结果也不能重新激活终态任务。

### 4. exec 的执行与输出

每调用新 shell；v1 Linux 为 sh、Windows 为 cmd，以实际能力返回 shell/encoding。
不承诺 shell 语法跨平台相同。cwd 通过子进程启动参数/子进程内机制设置；不把密码或
任意 env 覆盖参数开放给模型。输出按平台编码转换为 UTF-8；非法字节标记替换/解码问题，
stdout/stderr 分开，不声称恢复两条管道的全局时间顺序。

timeout 是整次执行期限，受当前 Operation 剩余期限夹紧，权限等待同样计入期限。
正常非零退出仍返回进程结果，要求模型检查 exit_code；spawn/I/O/回收错误为工具失败，
取消/超时携带已有输出和终止事实。无后台转交；正常退出也回收本次子进程组/Job Object
内残留子进程。进程组不能保证捕获主动逃逸的恶意 daemon，发布说明不能称为通用沙箱。

输出边读边 drain，有限内存预览和有限磁盘留存。超过保留预算仍排空管道，返回
`retained_truncated=true`，不能阻塞子进程或无界落盘。stdout/stderr 保留引用可由 read
访问，过期/越权引用明确失败；不把源内容放进广播播报、普通日志或全局可读临时文件。

### 5. 权限与资源建议值

read、write/edit、exec 分别使用既有 `filesystem.read`、`filesystem.write`、
`process.execute`。只读会话仅注册 read，exec 一律视为可能修改环境，不做命令文本
“看起来只读”的启发式放行。缺 Provider 不注册；缺门禁不注册执行/修改工具。
read_roots 不推导 write_roots，写根为空即拒绝；已有 filesystem.write 默认 deny 保留。

exec 在显式开启基础工具执行能力后按既有策略判定。是否把产品 exec 首次使用设为 confirm、
如何迁移已有 allow 配置，必须在 M10-02 冻结，不能静默改全局默认。确认面展示 resolved
路径/cwd、操作类型和受控命令详情。明确展示 exec 使用宿主用户权限，文件根不限制 shell
内部读写/网络；本里程碑不宣称 OS 沙箱。如果产品要求工作区隔离执行，先单独立项后端约束。

以下为待测建议值，集中配置、边界拒绝，不将它们变成新的累计步骤上限：

| 预算 | 建议值 | 满额/超限行为 |
| --- | --- | --- |
| read 单次文本 | 2000 行、50 KiB，先达者生效 | 明确 truncated/next_offset；超长行明确失败 |
| read 底层扫描 | 每次 16 MiB / 5 s，受 Operation deadline 夹紧 | scan_budget_exceeded；不能声称已到 EOF |
| 单文件 write/edit | 原文/新文各最多 1 MiB；请求仍受 Mira 当前 256 KiB 参数上限约束 | 参数阶段或读前拒绝，禁止先产生副作用；1 MiB 非模型可一次提交的保证 |
| 命令 | 64 KiB；30 s 默认、120 s 最大 | 超长/无效期限启动前拒绝；超时终止回收 |
| 模型命令预览 | 两流合计 50 KiB / 2000 行 | 有界尾部预览、截断标记及输出引用 |
| 命令输出留存 | 每流 4 MiB；session 32 MiB、全局 128 MiB；终态后 1 h | 达单调用上限继续 drain；总量准入不足启动前拒绝；到期引用失效 |
| 活动执行 | 每 session 1 个 exec、全局最多 4 个；文件修改同文件 1 个 | capacity_exceeded/busy；Executor 总量准入仍为最终门禁 |
| 观察缓存 | 每 session 128 个文件，元数据/片段范围 1 MiB | 有界淘汰；淘汰后修改要求重读，不保存整份原文副本 |
| 目录配置 | 每类最多 16 个根；路径/命令/结果统一校验 | 超限配置拒绝，不自动放大 |

JSON 转义、diff 与 envelope 也计入实际结果预算；必须低于 Mira ToolBridgeLimits 的
1 MiB 结果上限，并与 M9 请求历史预算协同。磁盘写满/留存失败有明确结果，不伪造输出
完整可回读。日志只记录 ID、字节数、路径摘要和结果码；任意命令输出不保证可自动识别
所有秘密，原文访问和发送模型必须沿已有受控载荷规则。

### 6. 错误与生命周期

拟冻结可修复错误：invalid_argument、workspace_required、not_found、not_regular_file、
unsupported_encoding、line_too_long、scan_budget_exceeded、file_too_large、not_observed、
stale_file、match_not_found、ambiguous_match、busy、capacity_exceeded、output_expired。
复用既有 permission_denied / cancelled / deadline / I/O 类别，再给稳定 machine code；
具体与 ProviderError/Mira ErrorCode 的映射由 M10-02 锁定，不凭字符串合并不同失败。
可修复错误返回本次事实和重读/缩小范围建议；不自动重试有副作用操作。

有限准备工作沿既有 Executor worker，新增有限任务优先 submit_auto 并消费 future；需要
可取消准入时使用 submit_cancellable。长 pipe/文件等待通过 blocking worker，run 的停止
令牌配套 wakeup 解除等待；不能在会话 worker 同步等待同一已耗尽线程池的子 future。
Backend 提供有界等待/超时，不声称普通 stop flag 能中断任意 OS I/O。

Runtime owner 持有任务/worker/output handles 和容量租约，状态/失败使用 Executor
设施，跨上下文逐条结果走 MpscChannel，UI 订阅复用 Topic/LatestMailbox。
BlockingWorkerHandle 的 started 只是接纳，结果和回收分别观察；权限确认结束后重新校验。
关闭顺序：停止生产与接纳 → 取消 → wakeup/终止进程 → 回收 worker → 消费有限结果并记录
已提交写入 → 关闭输出资源 → 非 worker 线程 shutdown(true)。接管/暂停的阻止新副作用
规则同样覆盖 exec/write/edit，不局限于键鼠输入。

## 工作项

- [x] `M10-01` **Completed**：完成四项目调研与 Mirage/pinned 依赖差距表。
  验收：固定版本、具体源码位置、可借鉴项/不采用项、证据等级与局限已入调研文档。
- [x] `M10-02` **Completed**：冻结 DEC-095、四工具 schema/alias、错误码、版本/提交点、
  预算和产品权限/迁移；主设计差异经 DEC-095 契约表与 Provider 头文件承载。
  验收证据：短名经 pinned `is_known_hosted_tool_name` 核对可用；四工具 schema 经
  `BuiltinToolRegistry::register_tool` 子集门禁真实注册通过（basic_agent_toolset_test）；
  ASCII 短名为 alias provider 恒等映射，三方言无转义冲突；每个暂定值在 DEC-095
  有冻结结论（含否决 local.* 别名方案）。
- [x] `M10-03` **Completed**：pinned-free 文件分页/修改与 ProcessRequest 加法 API、
  capability 查询交付。偏差说明：计划中的 fake Provider 以"默认 fail-closed
  实现 + 真实 Linux Provider 确定性测试"替代覆盖（basic_contract_default_test
  钉住默认语义与旧消费者兼容；失败与提交点由 linux_file_actions_test 以真实
  临时目录确定性复现），未再维护第二份 fake 实现。
  验收：旧消费者仍编译、旧超限语义不变（既有 provider 测试回归通过）。
- [x] `M10-04` **Completed**：Linux 安全打开、分页、create-if-absent、版本校验、
  字面编辑及临时文件提交；同文件租约有界。
  验收：linux_file_actions_test（448 checks）覆盖字节/属性、拒绝路径、
  取消/并发注入无半文件无临时文件残留；busy 租约并发必现。
- [x] `M10-05` **Completed**：Linux exec 的 cwd、输出 sink 和可解除等待生命周期。
  验收：退出码/EOF 独立、双流无死锁、进程组回收、取消/超时/shutdown 语义
  实测（linux_file_actions_test exec 场景，含空格/非 ASCII cwd 与输出留存）。
- [x] `M10-06` **Completed**：Runtime 输出预算/有效期与 read(output_ref) 闭环。
  验收证据：basic_output_store_test（132 checks）覆盖截断预览续读至保留边界、
  跨会话/过期/伪造引用统一 output_expired、配额准入启动前拒绝、清扫与孤儿句柄
  归还配额、构造清理与 0600 权限；磁盘/内存上界由配置上界 + 实时核算钉住。
  范围说明：默认冻结值（4 MiB/32 MiB/128 MiB/1 h）经小配置等价触达同一代码路径，
  真实墙钟 1 h 未实测；RuntimeService 端到端接线为源码级核对（basic_output_directory
  → harness 上下文），真实服务进程验证随 M10-08/09 补齐。
- [x] `M10-07` **Completed**：四工具接入真实 ConversationLoop 路径（complete_harness_turn
  经 DEC-095 装饰器注册）；共享 handler 复用既有 CancelToken/deadline 桥；目录/权限
  按实际能力动态暴露。范围说明：产品开关 `DesktopToolContext::basic_tools` 默认
  关闭，配置持久化与产品入口归 M10-08；read(output_ref) 归 M10-06。
  验收：read→write(replace)→edit 字节级闭环、read-only 能力诚实注册、权限拒绝
  零副作用、异常回填经 basic_agent_toolset_test 覆盖；既有工作流/桌面工具测试回归通过。
- [x] `M10-08` **Completed**：workspace cwd/write_roots/exec 开关的配置持久化与产品入口
  交付（settings `basic_tools` 块严格编解码、policy.set RMW 保留、`--enable-basic-tools/
  --enable-exec/--workspace/--write-root` 旗标、环境写根独立构造、留存输出目录缺省、
  Windows 响亮拒绝）；取消令牌贯穿四工具（exec 运行中取消整组回收并携带输出）。
  验收：read_roots 不扩大写权限（独立写根向量+旗标源，测试钉住）；重启配置兼容
  （真实服务 policy.set RMW 回归）；缺配置有可操作提示（exec 无 workspace 退出码 2）；
  UI 取消贯穿实际进程（工具层实测：预取消零副作用、运行中取消有界返回）。
  范围说明：异步确认复用既有 filesystem.write/process.execute 门禁与规则；过程卡片
  经既有工具过程链路展示修改事实/退出状态/输出引用，专用"有界 diff"视图归产品 UI
  后续增强（事实字段已齐备）。
- [x] `M10-09` **Completed**：Linux 发布候选端到端与消毒器验收。
  验收证据：真实产品 E2E 三场景（deepseek 经默认设置接入，隔离 socket/state，
  `--enable-basic-tools --enable-exec --workspace --write-root` 产品旗标）在 debug 与
  release 构建上全部通过——① read→edit→write→exec→read 全链，版本指纹变化与落盘
  字节逐字节断言，exec 结果携带留存引用；② exec 运行中产品级取消（IPC CancelChat），
  工具结果 terminated=cancelled 且无存活进程；③ turn 间外部修改 → stale_file 检测 →
  模型重读 → 编辑落地且外部字节保留（多轮可修复失败）。消毒器矩阵：debug/asan/
  ubsan/tsan 全量 89 项通过（tsan 经 setarch -R），release 全量 89 项通过（唯一失败
  均为分支基线 session_client_test）。开放项已闭合（2026-10-16）：提交点崩溃注入
  用例并入 linux_file_actions_test——create/replace/edit × 大小两档 payload 共 6 矩阵
  × 24 次 kill -9 迭代（延迟按校准的提交窗口采样，67 次运行中击杀并观测到真实
  `.mirage-tmp-*.tmp` 残留），逐次断言目标恰为完整旧/新内容、残留仅临时文件、兄弟
  文件不变、kill 后重试有界落地；检查数 450→1707，debug/asan/ubsan/tsan 全部通过
  （注入只用 fork/kill/文件 API，无新增线程）。断电持久性仍不在契约内（DEC-095 v1
  明示不 fsync，进程崩溃面与断电面分离）。
- [ ] `M10-10` **Proposed**：交付 Windows FilesystemProvider 和 exec cwd/编码/输出接线。
  验收：MSVC 构建及真实 Windows 文件/进程/Job Object 矩阵通过；无 Bash/rg 时四工具仍可用。
- [ ] `M10-11` **In Progress**：完成跨平台安装产品回归、兼容性记录、例子与操作说明。
  验收：干净配置真实产品执行四工具；工作流/桌面工具/M9 历史压缩回归，平台声明与证据一致。
  进展（2026-10-16）：操作说明交付 `docs/examples/basic-agent-tools.md`（启用/权限/
  预算/错误恢复/留存清理/崩溃语义/验证）；Linux 侧干净配置真实产品四工具 E2E 于
  native-release 构建通过（deepseek，三场景，真实会话状态逐字节未动），目标回归集
  10/10（工作流/桌面/M9 历史压缩/宠物/tray），本机产品已更新供人工体验，证据见
  `docs/compatibility/basic-agent-tools-native-release-20261016.md`。同轮发现并修复
  E2E 驱动隔离缺陷（`--state-dir` 不覆盖 M5-08 会话存储，改注入 `XDG_STATE_HOME`，
  并清理了测试对真实会话目录的历史污染）。
  补充（2026-10-16 下午）：真实产品人工体验暴露两项配置装配缺陷并修复——① host.cpp
  默认配置文件分支漏应用 `basic_tools` 块（`--config` 分支有、产品托盘恰走默认分支，
  旗标 E2E 从未暴露），补齐并以启动诊断行暴露工具状态；② 持久化设置 `confirmation`
  拒绝 `"ipc"`，无法启用 DEC-020 确认枢纽，schema 放开。修复后对本机产品实例（托盘
  实测）真实模型验证通过：模型经 read 工具逐字读出工作区文件，turn ok。
  开放项（产品 UI 后续增量）：原生 UI 尚无 `permission.respond` 应答面，产品里
  `confirm` 规则恒为 fail-closed 拒绝；当前产品以 allow 规则运行（写域锁定工作区），
  待应答面交付后切换 confirm。剩余：Windows/MSVC 安装回归（依赖 M10-10，
  当前环境无 Windows）。
  追加（2026-10-16 晚）：工具暴露绑定会话访问模式（维护者决策，DEC-080 逐轮
  access）——`read` 在全部代理可用模式注册（完全访问/视觉），`write`/`edit`/`exec`
  仅完全访问轮注册（`register_basic_agent_tools` 增 `full_access` 门控，模型层逐轮
  传入 `tools_allowed`；chat_only 维持既有代理降级语义）。注册矩阵 294→309 检查
  （false 时恰仅 read，含 exec_enabled+workspace 与只读文件系统交叉组合），全量
  debug 89 项仅剩基线失败，asan 范围干净；本机产品已重建重启，完全访问轮真实模型
  read 回归通过。
- [ ] `M10-12` **Proposed**：按退出条件复核里程碑、清理遗留和提交评审。
  验收：计划/设计/决策/安全/协议/测试证据同步；所有适用门禁通过，MR 符合工程规范。

## 交付顺序与依赖

| 增量 | 工作项与依赖 | 可评审结果 |
| --- | --- | --- |
| A 契约 | 01 → 02 → 03 | 决策、schema golden、fake 与兼容 API；不先开放真实副作用 |
| B Linux 最小闭环 | 04/05 → 06/07 → 08 → 09 | 真实四工具、输出续读、权限/取消/产品接线；可单独发布 Linux 预览 |
| C Windows 与收口 | 03 后可开展 10；09/10 → 11 → 12 | Windows 完整四工具和跨平台发布证据 |

这是增量/PR 拆分，不要求创建额外 chat 或并行 agent。初步工作量为 A 2～3、B 6～9、
C 4～6 人日（不含外部评审排队）；M10-02 根据文件提交与 Windows 环境验证重估，
不是日期承诺。Provider 实现可分别评审，合入前必须保留 API/fake/取消门禁。

## 测试矩阵

以下是待执行门禁。新增测试名称在实现时确定，不能把下面的命令视为本次已运行结果。

| 领域 | 必测场景 | 通过证据 |
| --- | --- | --- |
| schema/模型 | 未知字段/过大参数、hosted 名冲突、三方言请求/返回、历史 replay/compact | fixture 内容与实际 ToolResultPart 解析；失败后模型可重读修复 |
| read | 分页/EOF/空文件/offset 越界、UTF-8/CRLF/BOM、巨行、持续增长、scan budget | 正确范围和 next_offset，内存与扫描预算不超限 |
| write/edit | 新建竞争、未观察覆盖、过期/同大小同时间内容变化、0/1/多匹配、replace_all、no-op | 字节比对、revision、替换数；拒绝前后文件摘要一致 |
| 提交/恢复 | 短写、磁盘满、rename/replace 拒绝、提交前/后取消、外部写入窗口、崩溃注入 | 原文件/新文件二选一、applied 事实、临时文件清理；明确外部竞争限制 |
| 路径/权限 | ../、前缀混淆、换链、symlink/hardlink/reparse、空/越界根、确认拒绝/超时、策略变化 | 无越界内容泄露/写入；拒绝不启动进程；Windows 设备/ADS/共享占用负向 |
| exec | cwd 含空格/非 ASCII、正常/非零、spawn 失败、双流洪泛、EOF 早于退出、子进程持管道 | cwd/退出码准确、输出可读、无死锁；沿用现有 EOF 回归 |
| 生命周期 | 正常、任务异常、Executor 准入拒绝、排队取消、执行中取消、deadline、shutdown、接管 | Executor 状态/失败事件与资源计数，取消后 canary 不执行，无遗留子进程/句柄 |
| 输出/隔离 | 留存上限、过期、跨会话引用、慢 UI、缓存淘汰、日志/广播脱敏 | 有界资源、拒绝明确；原文仅进入授权模型/详情路径 |
| 产品 E2E | read → write → edit → exec → read 验证；文件外部修改后重读恢复；UI 取消运行命令 | sandbox 临时目录真实副作用、会话记录/过程卡片与终态一致 |
| 平台/回归 | Linux GCC/Clang、Windows MSVC；既有 workflow/桌面/权限/M9 历史 | 独立平台记录；Debug/Release、ASAN/UBSAN，跨上下文与关闭增加 TSAN/故障注入 |

实现阶段入口使用 `cmake --preset debug`、`cmake --build --preset debug`、
`ctest --preset debug --output-on-failure`；asan/ubsan/tsan 用对应 preset；涉及原生
接线增加 native-debug/native-release。定向测试名以合入的 CTest 清单为准，再跑所需
全量回归。Windows 单列实际 MSVC 配置和运行命令，不以 Linux 交叉编译替代运行门禁。

## 风险与阻塞

| 风险 | 当前处置/负责人 | 解除或补验条件 |
| --- | --- | --- |
| 文件根被误读为 exec 沙箱 | M10-02/08，Mirage 维护者 | 产品边界/默认执行权限冻结；若要求沙箱另立项 |
| 外部编辑与提交竞争 | M10-02/04/10，平台维护者 | 故障注入及保证范围记录；不承诺通用 CAS |
| 网络文件系统/异常设备 I/O 无法及时取消 | M10-02/04，平台维护者 | 首版支持环境限定并实测；不宣称任意挂载点都有硬上界 |
| 短名限制/JSON 转义超限 | M10-02/07，integration 维护者 | pinned 公开入口 fixture；无证据不升级依赖 |
| Windows 文件/编码环境缺失 | M10-10，Windows 维护者 | 可运行真实 Windows/MSVC 产品及故障注入；缺环境保持未完成 |
| 输出/观察存量挤占持续会话 | M10-06/11，Runtime 维护者 | 大输出/多会话/慢消费实测与容量统计 |

## 退出条件

- [ ] DEC-095 已冻结，公开 API/schema/错误/默认权限/配置迁移无未决关键项。
- [ ] Linux 真实产品四工具与桌面工具共存，多轮任务可修复失败并检查实际文件/进程结果。
- [ ] Windows 真实产品四工具与文件/进程门禁通过；Linux 预览不能关闭此项。
- [ ] 权限拒绝、范围越界、未观察修改、版本冲突和名称拒绝均无未授权副作用。
- [ ] 六类并发门禁及队列/容量拒绝、取消/提交竞态、输出配额与 shutdown 有可复现证据。
- [ ] ASAN/UBSAN、TSAN/故障注入及适用平台回归通过；未运行项记录原因/负责人/补跑条件。
- [ ] 主设计/安全/配置/示例/兼容性/计划状态、提交和评审均同步；未实现后续能力明确延期。

## 后续增量触发条件

- grep/glob/ls：真实任务因 shell 搜索兼容性、输出成本或只读场景受阻后，独立立项。
- 后台 job/PTY：真实构建/服务启动超过前台期限或需要交互输入时，先冻结 job ID、输出游标、
  stop/owner、容量与重启语义，再考虑工具扩展；不能将 v1 timeout 默认为后台交接。
- 多文件 apply_patch：单块 edit 往返成为可测瓶颈后，定义预检/部分提交/恢复语义。
- OS 沙箱：产品要求工作区隔离命令或网络策略时，单独设计平台限制与授权；不能靠命令解析替代。

## 验证记录

2026-10-16 / M10-01：在 Mirage `7e670dc` 工作树只读核对本地 ZCode / deepseek-harness
固定提交与 pi 固定 SHA 源码，补充 OpenCode 官方文档对照，形成调研、DEC-095 提案与本计划。
本次仅文档变更；未实施四工具、未修改 pinned 依赖，未执行产品测试/消毒器/Windows
验证。后续运行门禁由 M10-04～11 的责任人补齐，条件为对应实现可构建且目标环境可用。
文档校验：`git diff --check` 通过；Python3 静态检查新增三份文档的 27 个本地链接、
单一一级标题、标题层级、代码围栏、行尾空格和 M10-01～12 连续唯一编号，0 错误。
主设计与总计划新增入口已逐项核对。仅调研项勾选完成，其余功能与退出条件保持未完成。

2026-10-16 / M10-02～05、07（分支 `feat/m10-basic-agent-tools`，提交
d8f90a3～6fc602b）：增量 A（契约）与增量 B 的 Linux 后端、会话接入交付。
独立验证代理（Independent-Verification-Agent）编写并执行三个测试套件，主循环仅修复
其报告的三个产品缺陷（窗口版本缺文件身份导致全部 replace/edit 误报 stale_file；
流式读取丢失无尾换行文件的末行；观察 LRU 查询未触新时钟）后复验通过：

- `basic_contract_default_test`：74 checks / 0 failures——加法方法默认 fail-closed、
  capabilities 默认全 false、execute_request 仅空请求委托旧路径、ProcessOutcome
  位置初始化源兼容（static_assert）。
- `linux_file_actions_test`：448 checks / 0 failures（UNIX）——分页/EOF/空文件/
  offset 越界/CRLF 视图/BOM/巨行/二进制拒绝/扫描预算；版本身份+摘要变更检测；
  create/replace/edit 成功与拒绝矩阵字节比对；临时文件零残留；提交前取消
  applied=false 字节不变；mode 保留；读写范围隔离；SHA-256 已知向量；exec
  cwd（空格/非 ASCII）、退出/信号/超时/取消事实、双流、输出留存、整组回收；
  并发 replace 租约 6/6 轮 busy、无交错、无残留。
- `basic_agent_toolset_test`：210 checks / 0 failures——能力诚实注册、权限拒绝
  零副作用、workspace_required、read→write(replace)→edit 字节闭环（经 pinned
  registry 真实调度，同时构成 M10-02 schema/名称门禁证据）、会话观察记录与 LRU。
- preset 矩阵：debug / asan / ubsan / tsan 四个 preset 三套件全部 0 failures；
  ASAN/LeakSan、UBSAN、TSAN 报告均为 0。TSAN preset 在本机因内核高熵 ASLR 需
  `setarch $(uname -m) -R` 运行（环境限制，非产品问题）；5 秒扫描时间预算仅有
  字节预算路径的确定性断言（时间路径未注入慢速文件系统装置）。
- 全量回归：`ctest --preset debug` 88 项中 87 通过，唯一失败为 `session_client_test`
  ——经干净 HEAD 复现确认属分支基线已存在问题，与本次改动无关。
- 未执行项：Windows（M10-10，本机无 MSVC/Windows 环境）、真实产品 E2E 与
  UI/配置接线（M10-08/09）。补跑条件分别为对应平台环境与产品入口可用。

2026-10-16 / M10-06（提交 7d823be～cf30e41）：Runtime 磁盘留存 store、integration
seam 与 read(output_ref) 闭环交付。独立验证代理两轮验证：首轮报告字节配额泄漏
（未 settle 句柄析构与过期清扫不归还配额，BUG-20261016-004），主循环修复（retain
实时同步条目字节事实；句柄存活超窗后停止留存）并统一 skip 越界语义（流式路径与
留存输出对齐内存路径的 invalid_argument 契约）后复验通过：basic_output_store_test
132 checks、basic_agent_toolset_test 271 checks、linux_file_actions_test 450 checks、
basic_contract_default_test 74 checks，debug/asan/ubsan/tsan 四 preset 全部
0 failures，ASAN/UBSAN/TSAN 报告为 0；全量 debug 回归 88/89，唯一失败仍为
`session_client_test`（分支基线已存在）。并行会话对
`docs/design/continuous-digital-entity-evolution.md` 的修订与
`docs/plans/m10-pre-work-checklist.md` 属另一工作流，未纳入本里程碑提交。

2026-10-16 / M10-09（提交 927d3cf）：真实产品 E2E 用已配置的 DeepSeek 端点
（anthropic 方言）在隔离产品实例上执行。模型在单个 turn 内真实调度七次工具
（read×2、edit、write、exec×3，其中一次主动 od 字节校验），版本指纹
0a20f79…→bb8d46c…→3af5ad3… 与落盘字节一致；产品级取消场景中 exec 于
exec-start 触发取消后整组回收（pgrep -x 无存活）；外部修改场景中 provider 拒绝
stale 编辑（stale_file 事实进入模型上下文），模型重读后重试成功且外部追加行保留。
另在首次场景中自然发生一次 exec 超时（find / 全盘扫描触顶 30 s 默认期限），
timeout 事实与部分输出正确返回——产品级 timeout 证据。debug 与 release 构建
三场景均通过；探针与驱动入 tests/manual/，驱动以 SCENARIOS 过滤并以精确字节
断言落盘效果。最初一次"sleep 幸存"经查为驱动 pgrep -f 误匹配外层 shell 命令行
文本（已改 pgrep -x），非产品回收缺陷。

2026-10-16 / M10-08（提交 969009c～fc471ad）：配置持久化、产品入口与取消贯穿交付。
独立验证代理一轮通过、无产品缺陷：persistence_test 353 checks（basic_tools 编解码
恒等、14 条严格拒绝矩阵、4096/64 边界）、runtime_service_test 611 checks（真实服务
policy.set RMW 逐字段保留回归）、basic_agent_toolset_test 294 checks（预取消四工具
零副作用、exec 运行中取消有界返回携带输出、未取消 token 全场景通过的反向保障）；
debug/asan/ubsan/tsan 四 preset 全绿，ASAN/UBSAN/TSAN 报告 0；全量 debug 回归
88/89，唯一失败仍为基线 `session_client_test`。host.cpp 旗标为 main() 内联逻辑，
源码级核对（含 Windows 拒绝分支与 exec-无-workspace 提前返回）；旗标与设置块混合
优先级组合矩阵未逐项实测（门控语义源码核对一致）。UX 记录：仅传 --write-root 不带
组开关时写根静默不生效（符合整组覆盖规格，产品提示归 UI 后续）。
