# DEC-095：Mirage 基础 Agent 工具集

> 状态：Accepted（M10-02 冻结）
> 日期：2026-10-16；冻结：2026-10-16
> 负责人：Mirage 维护者
> 冻结里程碑：M10-02（已完成，证据见下文与 M10 计划验证记录）
> 替代/被替代：无；扩展 DEC-009/010/024/053，不覆盖其已生效语义

## 背景与问题

用户要求 Mirage 向 Mira 提供 exec/read/write/edit，并参考 ZCode、deepseek-harness
及 pi 等 harness 制定开发计划。现有 Provider 仅提供整文件读取和有界 shell；会话
未接二者，文件修改尚不存在。依据见[调研](../research/2026-10-16-basic-agent-tools-survey.md)。

## 决策（已冻结）

1. **边界**：通过 pinned Mira BuiltinToolRegistry / ConversationLoop 接入；Mirage
   的 integration 做 schema/名称/结果映射，desktop 定义平台无关契约，Platform Backend
   实现文件和进程机制。Runtime Service 唯一 owner 持有执行、观察版本与输出资源。
2. **名称（M10-02 验证结论：短名可用）**：模型显示名与注册 wire name 统一为
   `exec`、`read`、`write`、`edit`。证据：pinned `mira/src/model/model_tool.cpp`
   `is_known_hosted_tool_name` 名单（web_search/file_search/code_interpreter/
   computer_use*/mcp/bash/shell/terminal/image_generation/canvas）不含四名；
   现有 tool alias provider 的 ASCII 规则对短名为恒等映射，三方言不产生转义或
   冲突；`BuiltinToolRegistry` 的 hosted 名拒绝与 schema 子集门禁对四名及其
   schema 放行（`local.*` canonical 别名方案不再需要，`local_exec` 兼容名方案
   作废）。不伪装为供应商 hosted shell，不修改 pinned Mira。
3. **文件**：read 提供有界行窗口与版本；write 新建采用 create-if-absent，覆盖要求
   同会话的完整观察；edit 要求同版本的目标片段观察、唯一字面匹配，支持显式 replace_all。
   片段观察只授权该片段的替换，不授权全文件覆盖。无模糊匹配、无多文件事务。
   行视图与 edit 匹配共用同一 "\r\n"→"\n" 归一视图（迁移说明：read 返回的行文本
   不含 CRLF 的回车符，字节级重建一律走 `observed_range` 原始区间事实；孤立 "\r"
   保留在行视图中，BOM 保留在首行）。续读 offset 语义全表面统一：eof 窗的
   `next_offset` 是唯一合法续读位置，`skip_lines` 达到或越过行数一律
   `invalid_argument`（空流 offset 0 除外），文件路径与留存输出引用同规则。
4. **一致性**：版本绑定文件身份（device/volume + file index）和全内容摘要；摘要仅对
   不超过单文件修改上限（1 MiB）的文件计算，超大文件仅供有界分页并拒绝写。修改前
   重查版本；写经同目录临时文件 + 原子 rename 提交。本进程同文件冲突立即返回 busy，
   不自建等待队列。取消在提交前可阻止提交（清理临时文件、applied=false），提交后
   记录 applied=true。对不协作外部写入只承诺检测到的冲突拒绝，不承诺 OS 级通用
   CAS；最后提交窗口由故障注入记录。v1 承诺原子可见性，不承诺断电持久性
   （不 fsync 文件与目录）。
5. **执行**：每次新 shell，显式 cwd（经子进程内 chdir/fchdir 定位，不改服务进程
   cwd，不在命令行拼接 `cd`）；不继承上次 cd/env。Linux v1 为 `sh`、Windows v1 为
   `cmd`，结果返回实际 shell 事实。timeout 为执行期限，到期终止并回收整个进程
   组/Job Object，不自动转后台。非零退出码是进程结果；spawn、权限、取消、超时与
   回收失败单列。后台/PTY 后续独立立项。
6. **权限**：read/write 范围分别配置且空范围拒绝；不把现有 read_roots 自动升级为
   write_roots。复用 filesystem.read / filesystem.write / process.execute 和现有异步
   确认。只读会话仅开放 read；exec 的产品启用为显式配置开关（缺省关闭，不随目录
   或工具注册静默开启），首次使用确认策略沿用现有 Confirm/Allow 规则，不迁移已有
   allow 配置。exec 按宿主用户权限运行，cwd/文件根不构成沙箱。
7. **生命周期**：取消贯穿权限等待、排队、文件分块和进程等待；长等待通过 kairo
   blocking worker，全部 handle/future 由外部 owner 消费。停机先停止接纳、取消、
   唤醒/回收 worker、消费有限结果，再由非 worker 线程 shutdown(true)。
8. **预算与结果**：模型预览、扫描、修改大小、输出磁盘总量和观察缓存都有上限；
   冻结数值见 M10 计划预算表（read 2000 行/50 KiB、扫描 16 MiB/5 s、修改 1 MiB、
   命令 64 KiB/30 s 默认 120 s 最大、预览 50 KiB/2000 行、留存每流 4 MiB、
   session 32 MiB 全局 128 MiB 终态后 1 h、每 session 1 exec 全局 4、观察缓存
   128 文件/1 MiB、目录每类 16 根）。大输出以同会话受控引用供 read 续读，超保留
   上限明确标记（retained_truncated）。模型结果含事实；UI/日志默认仅安全摘要
   （ID、字节数、路径摘要、结果码），原始内容只进授权模型与详情路径。
9. **错误码（冻结）**：`invalid_argument`、`workspace_required`、`not_found`、
   `not_regular_file`、`unsupported_encoding`、`line_too_long`、
   `scan_budget_exceeded`、`file_too_large`、`not_observed`、`stale_file`、
   `match_not_found`、`ambiguous_match`、`busy`、`capacity_exceeded`、
   `output_expired`；复用既有 `permission_denied`、`cancelled`、
   `deadline_exceeded`、`io_error`。另有一个 provider 特有码：create 路径的
   `already_exists`（NOREPLACE 提交撞上并发创建或未读已存在文件；与租约
   `busy` 语义分开，提示先读后 replace），映射 InvalidArgument。到 pinned
   Mira ErrorCode 的映射沿用 desktop_atom_toolset 既有映射表并补充：
   `busy`/`capacity_exceeded`/`scan_budget_exceeded`/`file_too_large` →
   ResourceExhausted、`workspace_required`/`not_observed`/`stale_file`/
   `match_not_found`/`ambiguous_match`/`line_too_long`/`already_exists` →
   InvalidArgument、`not_regular_file`/`unsupported_encoding`/
   `output_expired`/`unsupported_platform` → UnsupportedCapability（模型可
   修复类携带重读/缩小范围事实）。不凭字符串合并不同失败。

## 备选方案（已否决）

- 只用 exec 完成文件操作：开发量小，但精确编辑、冲突检查和范围读取难以形成稳定契约。
- 直接复用旧工作流两个原子：缺写/edit、cwd/分页，且取消桥未闭合，不足以满足会话需求。
- 接外部 MCP 文件/shell 服务：引入额外进程、部署和信任边界，已有 Provider seam 可承载。
- 先实现后台终端、沙箱、多文件 patch：价值独立，但不应阻塞最小四工具交付。
- `local.*` canonical 名 + 别名映射：M10-02 验证短名直接可用，别名层属多余状态，
  不引入（本条为冻结时新增否决项）。

## 影响与风险

新增 Provider 加法 API、Runtime 配置/持久化和工具注册，可能影响协议 golden vectors、
模型工具目录与已有 Workflow 消费方。不得改变旧 read_text_file 的超限失败约定
（超限拒绝，绝不静默截断）。Windows 缺文件 Provider，不能只通过编译便声明可用；
shell 执行可访问宿主权限允许的文件/网络，必须在产品说明中清楚区分文件根与 OS 沙箱。

## 验证方式

按 [M10 测试矩阵](../plans/m10-basic-agent-tools.md)验证四工具往返、真实落盘/进程
退出、权限拒绝零副作用、版本冲突、提交点取消、执行中取消、超时、背压和 shutdown。
Linux/Windows、Debug/ASAN/UBSAN/TSAN 分开登记，未运行项不勾选。

M10-02 冻结证据（2026-10-16，分支 `feat/m10-basic-agent-tools`）：pinned 源码核对
（hosted 名单、schema 子集关键字表、ToolBridgeLimits 256 KiB/1 MiB、registry 校验
路径）确认短名与所需 schema 关键字全部受支持；`basic_agent_toolset_test` 经
`BuiltinToolRegistry::register_tool` 真实门禁注册 `exec/read/write/edit` 通过
（schema 子集与 hosted 名检查的执行证据），短名为 tool alias provider 的恒等
ASCII 映射、三方言无转义冲突；`local.*` 别名方案据此否决。契约与实现由
basic_contract_default_test（74 checks）、linux_file_actions_test（448 checks）、
basic_agent_toolset_test（210 checks）在 debug/asan/ubsan/tsan 四 preset 下
钉住，证据明细见 M10 计划验证记录。

## 关联文档和工作项

[M10-01～12](../plans/m10-basic-agent-tools.md)、[主设计第 5 节](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)、
[DEC-009](DEC-009-provider-scope-budget-cancellation.md)、
[DEC-010](DEC-010-m1-permission-framework.md)、
[DEC-020](DEC-020-permission-async-confirmation.md)、
[DEC-024](DEC-024-desktop-atom-toolset.md)、
[DEC-053](DEC-053-conversation-desktop-tool-registration.md)。


## 2026-10-19：exec 预览编码修正（M10-07/08）

ProcessProvider 返回有界原始字节，不能假设截断后的 stdout/stderr 是 UTF-8。integration
在构造模型 JSON 前验证预览：字节预算截断的尾部最多回退三个字节，保留完整 UTF-8
字符；仍无效则返回 `[non-UTF-8 output omitted]`（受原预览字节预算限制），并置
`output_truncated=true`。stdout/stderr 独立处理，exit_code/terminated 不变。
原始留存、引用、字节计数和 read 的非文本拒绝纪律不变。不引入新编码器，使用既有
Desktop UTF-8 校验及 Mira JSON 公开 API。

过程记录仍遵循 DEC-049。修复与证据见
[任务事件退出维护记录](../plans/maintenance-ui-exit-20261019.md)。
