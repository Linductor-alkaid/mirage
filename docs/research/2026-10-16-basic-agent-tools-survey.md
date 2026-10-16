# 基础 Agent 工具集调研：exec / read / write / edit

> 状态：Completed（源码与文档调研，不代表功能完成）
> 日期：2026-10-16
> 负责人：Mirage 维护者
> 关联工作项：[M10-01](../plans/m10-basic-agent-tools.md)
> 后续选择：[DEC-095 提案](../decisions/DEC-095-basic-agent-tools.md)

## 1. 结论

建议为 Mirage 会话提供 `exec`、`read`、`write`、`edit` 四个基础工具，与现有桌面
工具并存。四工具足以形成“读取材料 → 创建/修改文件 → 运行命令 → 检查结果”的最小
闭环；专用搜索、后台进程和多文件 patch 可独立追加。这个结论来自以下源码对照，
不是四工具与完整 coding harness 等价的性能结论。

Mirage 已有执行底座，但尚缺会话接入与文件修改能力。优先复用 Mira 的工具校验、
规范往返、循环和生命周期，在 Mirage 的 Provider / Platform Backend 内补足本机
操作。首版采用明确的文本替换和修改前版本校验；不复制其他项目的调度器或队列。

## 2. 方法与版本

本地项目只读检查，`git status --short` 均为空；以下结论对应所列 commit，而非对未来
版本的承诺。外部项目是能力与语义参考，不引入为依赖、不复制代码。

| 对象 | 本次依据 | 证据等级 |
| --- | --- | --- |
| Mirage | `7e670dccaad9b76e07175667d63ea79d11e25a8e` 工作树 | 公共头、会话注册与后端源码静态检查 |
| pinned Mira | `a29374aaf05b956cbd5ceeafc65bba4c6172675e` | 公开工具/循环/结果头、API 文档 |
| pinned Mirador | `ae8cf87581c1ebe393a67374d9977a38354d0911` | README 模块地图；视觉能力不承担文件/进程操作 |
| ZCode | `~/ZCode`，`29628c9acdb81b703bbd4080c207a0e7ce5e276e` | 工具 handler、文件状态与执行 adapter 源码 |
| deepseek-harness | `~/deepseek-harness`，`86a402200d95fecef4529d969836f1fe13c6cb0d` | tool-fs / observation-policy / tool-bash 源码及包文档 |
| pi | `earendil-works/pi`，`5ab7fe74c400097b386662205cd93900695b8a71` | 固定提交的七份工具源码；旧 `badlogic/pi-mono` URL 当前重定向至此 |
| OpenCode | 官方 Tools / Permissions 文档，2026-10-16 访问 | 文档级交叉检查；未检查其实现，不作内部行为推断 |

pi 提交由 `git ls-remote https://github.com/earendil-works/pi.git refs/heads/main`
定位，并从对应固定 SHA 下载 `index.ts`、`read.ts`、`bash.ts`、`edit.ts`、
`edit-diff.ts`、`write.ts`、`truncate.ts` 到临时目录检查。复现本地结论可在对应
仓库执行 `git show <上表提交>:<下文源码路径>`。未运行这些 harness 的业务测试、
模型对照或目标平台性能测试。

## 3. 外部能力对照

| 能力 | ZCode | deepseek-harness | pi | Mirage 取舍 |
| --- | --- | --- | --- | --- |
| 命令执行 | Bash；目录/预算/取消/输出 adapter，另有后台任务 | bash / pwsh 可组合；前后台与 jobs 服务协作 | 基础组合为 read/bash/edit/write，另有 PowerShell 工具 | 对模型统一 `exec`；公开实际 shell，不假定 Windows 有 Bash |
| 文本读取 | Read，行号、范围、token 预算与 partial 标记 | read，1-based offset/limit，行/字节/单行上限，大文件流读 | offset/limit，2000 行或 50 KiB 截断 | 分页、有界扫描、明确续读位置；不先整文件装入内存 |
| 创建/覆盖 | Write；已有文件要求有效读取状态 | write；观察策略可选择 createIfAbsent / replaceIfVersion | write 创建父目录并覆盖文件 | 创建与覆盖分开验证，默认防止盲目覆盖 |
| 局部编辑 | Edit；唯一匹配、replace_all、读取/过期检查；内部还有匹配修复策略 | edit；字面替换、唯一匹配或 replace_all；可注入版本门禁 | 当前为单文件 edits 数组；唯一且不重叠，保留 BOM/换行，内部有 fuzzy 辅助 | v1 单块字面替换，歧义不写；暂不引入 fuzzy / 批量 patch |
| 结果展示 | 模型文本与 UI 展示分开，有 diff/执行状态 | typed output、render、presentationMeta 分开 | 文本结果、diff/patch details、渲染分离 | 模型事实、UI 摘要、受控详情分开，不能只给 UI 成功提示 |

### 3.1 ZCode：模型体验与修改前观察

源码根为 `apps/zcode-cli/packages/`：

- `core/src/tool/handlers/read.ts`、`read-text.ts`：行号、范围读取、超预算 partial
  fallback 与续读指引；将文件状态记录与模型显示关联。
- `core/src/tool/handlers/write.ts`：已有文件覆盖有未读取/状态过期错误；
  `edit.ts::getEditableReadStateFailure` 检查有效读取状态和当前 revision。
  不能把它概括成“仅有一个文件路径就允许覆盖”，也不能把全部 partial 读取都当成
  完整文件观察。该版本会排除 `isPartialView` 的编辑依据。
- `core/src/tool/handlers/edit.ts`、相关匹配 helper：描述为精确替换，但实现还会调用
  `findEditMatch` 和匹配策略归一；Mirage 如选择严格字面语义，应独立冻结。
- `adapters/src/exec/node-execution-adapter-*.ts`、`process-tree.ts`、
  `bash-output-policy.ts`：进程机制与工具 handler 分开；输出默认长度 30,000，
  配置上限 150,000（该策略的长度单位，不能直接当作 UTF-8 字节）。
- `core/src/tool/bash-timeout-policy.ts`：该快照默认 120,000 ms、最大 600,000 ms。
  后台生命周期另有 handler/registry；这不是 Mirage v1 必须复刻的默认值。

值得借鉴的是清晰的可修复错误、修改前观察和模型/UI 分离。读取状态检查的强度取决于
revision 来源；mtime/size 不能作为 Mirage 对外宣称“任意并发写入都不会丢失”的依据。

### 3.2 deepseek-harness：工具、机制与策略分别承载

源码路径：

- `packages/fs/tool-fs/src/{index,read,read-render,write,edit}.ts`：工具负责 schema、
  读取窗口与呈现，操作通过 `ctx.fs`。默认 read 为 2000 行、50 KiB、单行 2000
  字符；10 MiB 起选择流式读取。输出上限与底层扫描成本是两件事。
- `packages/fs/fs-observation-policy/src/index.ts`：按 session / target 保存观察。
  未观察或确认不存在的 write 使用 createIfAbsent，观察到已有文件则使用
  replaceIfVersion；edit 未观察即失败。该策略是可组合插件，裸工具本身不强制启用，
  不能把组合默认语义误写成所有部署恒成立。
- `packages/shell/tool-bash/src/{index,background,render}.ts`：工具消费 shell seam，
  每次新 shell，可指定 workdir。非零退出报告给模型，基础设施错误单独失败。
  组合 jobs 时前台命令也先登记任务；配置允许时等待超时交接到同一个后台 job。
  缺少 jobs 或关闭 promotion 时才采用 deadline kill。故“超时”必须区分等待超时和
  执行期限，Mirage 首版选择后者，不悄悄升级为后台任务。
- `packages/shell/bash-sandbox/`、`pwsh-sandbox/` 是执行器侧约束；不能把工具描述、
  `workdir` 或文件工具的路径根误认为 shell 已受 OS 沙箱限制。

最适合 Mirage 的部分是 Provider seam、原子创建意图和版本门禁。其 JS jobs/队列/
调度实现不适用 Mirage；相应职责必须由 Runtime owner 与 kairo 承载。

### 3.3 pi：四工具最小组合与可替换操作接口

固定源码目录为
[`packages/coding-agent/src/core/tools/`](https://github.com/earendil-works/pi/tree/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools)。
[`index.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/index.ts)
的 `createCodingToolDefinitions()` 返回 read/bash/edit/write；grep/find/ls 和
PowerShell 是额外工厂。该快照不能只依据旧教程描述为“全部内置工具只有四个”。

[`read.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/read.ts)
支持文本分页及图片；默认本地操作仍使用 readFile，因此输出有限不等于峰值内存有限。
[`bash.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/bash.ts)
提供超时/abort、进程树终止、尾部预览与截断后的临时文件路径。
[`truncate.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/truncate.ts)
统一 2000 行/50 KiB 双上限。Mirage 应借鉴明确的截断事实，同时为保留文件增加总量和
有效期约束。

[`edit.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/edit.ts)
在本次快照已支持单文件多替换块、唯一/不重叠检查与 diff/patch；
[`edit-diff.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/edit-diff.ts)
含精确后 fuzzy 的辅助匹配。
[`write.ts`](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/src/core/tools/write.ts)
自动建父目录并覆盖；此默认本地路径未提供 Mirage 所需的 prior-observation 版本门禁。
其按文件串行修改队列也不能直接移植为 Mirage 私有队列。

### 3.4 OpenCode：最小集合之外的增量边界

[官方 Tools 文档](https://opencode.ai/docs/tools/)同样将 bash、read、write、edit
作为基础能力，并提供 grep、glob、apply_patch 等工具；read 支持行范围，edit
描述为精确替换。write/edit/apply_patch 共用 edit 权限。
[Permissions 文档](https://opencode.ai/docs/permissions/)提供 allow/ask/deny 策略。
这支持“先四工具、后专用搜索/patch”的交付拆分；不能据此宣称其完整并发、沙箱或
原子提交机制已经过本次源码核实。

## 4. Mirage 现状与依赖路由

| 已检查位置 | 已有事实 | 本计划需补足 |
| --- | --- | --- |
| [FilesystemProvider](../../desktop/filesystem/include/mirage/desktop/filesystem_provider.hpp) | 只读整文件；默认 1 MiB；超限拒绝；PathScope / CancelToken | 流式分页、版本/文件身份、创建/受保护覆盖/编辑 |
| [ProcessProvider](../../desktop/process/include/mirage/desktop/process_provider.hpp) | 同步有界 shell；30 s、每流 1 MiB、命令 64 KiB 默认；取消/超时/退出码/截断 | 显式 cwd、shell 事实、输出留存；模型侧注册及取消透传 |
| [Linux Backend](../../platform/linux/src/linux_desktop_environment.cpp) | 文件读取与 `/bin/sh -c`；进程组清理 | handle 锚定的文件修改、分页和 cwd 扩展 |
| [Windows Backend](../../platform/windows/src/windows_desktop_environment.cpp) | `cmd.exe /c`、Job Object 进程树回收；没有 FilesystemProvider | Windows 文件能力与 cwd/编码/替换验证 |
| [DesktopAtomToolset](../../integration/mira/src/desktop_atom_toolset.cpp) | 工作流原子含 desktop.filesystem.read_text / desktop.process.execute；会话 register_harness_tools 未注册二者 | 会话四工具、动态权限、统一结果与回归 |
| 同一原子的旧 handler | filesystem/process 调用传入新的空 CancelToken，未复用当前调用取消桥 | 不直接把旧 handler 放进会话；共享实现后补桥接与 deadline 检查 |
| [名称适配](../../integration/mira/src/tool_alias_provider.cpp) | canonical 名映射供应商兼容名称并还原 | 四工具 alias、历史回放和碰撞门禁 |

Mira 的公开 `tool_executor.hpp` 提供 BuiltinToolRegistry / spec / handler，
`model_tool.hpp` 提供参数/结果预算与 ToolExecutionRecord，`conversation_loop.hpp`
承载规范工具往返、取消和有限/持续循环；`json.hpp` 负责编解码。它没有交付本机
read/write/edit/shell 实现；`make_wait_tool()` 是已有参考工具。Mira 的
ToolModule/MCP 能力也不等于本机文件执行后端，本次无需先做模块/插件产品化。
Mirador 模块地图聚焦视觉/OCR/检测/cache，与这四个本机工具无重复能力。

kairo 已按 integration skill 的 scenario router 加载 blocking I/O card：长期可中断
等待由 `start_worker` / `IBlockingIoWorker::run` / `wakeup` / WorkerHandle 承载，
`started()` 仅是接纳，`request_stop()` 与 `stop()` 分别是请求和请求后等待。
实现阶段再按具体有限任务选对应公开能力，不把长 pipe/read 循环藏进普通池任务。
目前未确认依赖能力缺口；本机文件/进程适配是 Mirage 职责，不新增无证据反馈条目。

## 5. 实施含义与限制

1. v1 的价值在于四工具真实接入，而非新增一套通用 harness。先冻结 Provider 与模型
   schema，再实现 Linux、接入会话，最后关闭 Windows 门禁。
2. 保留旧整文件读取和工作流原子的语义；新增分页接口不能把旧“超限失败”静默改成成功。
3. 文件修改需定义范围、版本、提交点和取消竞态；原子替换只保证不暴露半文件，不能保证
   任意外部编辑器写入与本次版本校验完全互斥。相应保证必须由测试和平台条件限定。
4. `exec` 的 cwd 是执行目录，PathScope 是文件工具范围，二者都不是 shell 沙箱。
   使用现有 process.execute 权限并如实显示边界，不能把 read-only 会话开放给任意 shell。
5. 读取长文件、命令大输出和长期会话均需资源上限；可修复失败要完整回传 Mira，模型可
   重读后重试。不能吞失败、默认重放有副作用调用或把取消后的已提交写入当作未执行。

具体契约草案、工作项、预算、测试矩阵和验收顺序见 [M10 开发计划](../plans/m10-basic-agent-tools.md)。
