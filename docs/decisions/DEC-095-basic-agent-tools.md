# DEC-095：Mirage 基础 Agent 工具集

> 状态：Proposed
> 日期：2026-10-16
> 负责人：Mirage 维护者
> 冻结里程碑：M10-02；未冻结前不实施公开契约与默认权限变更
> 替代/被替代：无；拟扩展 DEC-009/010/024/053，不覆盖其已生效语义

## 背景与问题

用户要求 Mirage 向 Mira 提供 exec/read/write/edit，并参考 ZCode、deepseek-harness
及 pi 等 harness 制定开发计划。现有 Provider 仅提供整文件读取和有界 shell；会话
未接二者，文件修改尚不存在。依据见[调研](../research/2026-10-16-basic-agent-tools-survey.md)。

## 决策提案

以下均为暂定默认值，负责人为 Mirage 维护者，最迟在 M10-02 冻结。

1. **边界**：通过 pinned Mira BuiltinToolRegistry / ConversationLoop 接入；Mirage
   的 integration 做 schema/名称/结果映射，desktop 定义平台无关契约，Platform Backend
   实现文件和进程机制。Runtime Service 唯一 owner 持有执行、观察版本与输出资源。
2. **工具**：模型看到 `exec`、`read`、`write`、`edit`；canonical 拟为 `local.exec`、
   `local.read`、`local.write`、`local.edit`，显式双向 alias。M10-02 必须验证 Mira 的
   hosted 名限制和三方言回放；若短名被拒绝，使用 `local_exec` 等兼容名并统一更新提示。
   不伪装为供应商 hosted shell，不修改 pinned Mira。
3. **文件**：read 提供有界行窗口与版本；write 新建采用 create-if-absent，覆盖要求
   同会话的完整观察；edit 要求同版本的目标片段观察、唯一字面匹配，支持显式 replace_all。
   片段观察只授权该片段的替换，不授权全文件覆盖。无模糊匹配、无多文件事务。
4. **一致性**：版本绑定文件身份和内容，修改前重查，临时文件完成后原子提交；本进程
   同文件冲突立即返回 busy，不自建等待队列。取消在提交前可阻止提交，提交后必须记录
   applied。对不协作外部写入只承诺检测到的冲突拒绝，不承诺 OS 级通用 CAS。
5. **执行**：每次新 shell，显式 cwd，不继承上次 cd/env；首版沿用 Linux sh / Windows
   cmd 的能力事实。timeout 为执行期限，到期终止并回收，不自动转后台。非零退出码是
   进程结果；spawn、权限、取消、超时与回收失败单列。后台/PTY 后续独立立项。
6. **权限**：read/write 范围分别配置且空范围拒绝；不把现有 read_roots 自动升级为
   write_roots。复用 filesystem.read / filesystem.write / process.execute 和现有异步
   确认。只读会话仅开放 read；文件修改默认仍 deny，exec 的产品启用策略在 M10-02
   显式冻结，不因新增目录自动授权。exec 按宿主用户权限运行，cwd/文件根不构成沙箱。
7. **生命周期**：取消贯穿权限等待、排队、文件分块和进程等待；长等待通过 kairo
   blocking worker，全部 handle/future 由外部 owner 消费。停机先停止接纳、取消、唤醒/
   回收 worker、消费有限结果，再由非 worker 线程 shutdown(true)。
8. **预算与结果**：模型预览、扫描、修改大小、输出磁盘总量和观察缓存都有上限；建议
   数值及错误表见 M10。大输出以同会话受控引用供 read 续读，超保留上限明确标记。
   模型结果含事实，UI/日志默认仅安全摘要，原始内容不无差别广播。

## 备选方案

- 只用 exec 完成文件操作：开发量小，但精确编辑、冲突检查和范围读取难以形成稳定契约。
- 直接复用旧工作流两个原子：缺写/edit、cwd/分页，且取消桥未闭合，不足以满足会话需求。
- 接外部 MCP 文件/shell 服务：引入额外进程、部署和信任边界，已有 Provider seam 可承载。
- 先实现后台终端、沙箱、多文件 patch：价值独立，但不应阻塞最小四工具交付。

## 影响与风险

新增 Provider 加法 API、Runtime 配置/持久化和名称 alias，可能影响协议 golden vectors、
模型工具目录与已有 Workflow 消费方。不得改变旧 read_text_file 的超限失败约定。
Windows 缺文件 Provider，不能只通过编译便声明可用；shell 执行可访问宿主权限允许的
文件/网络，必须在产品说明中清楚区分文件根与 OS 沙箱。

## 验证方式

按 [M10 测试矩阵](../plans/m10-basic-agent-tools.md)验证四工具往返、真实落盘/进程
退出、权限拒绝零副作用、版本冲突、提交点取消、执行中取消、超时、背压和 shutdown。
Linux/Windows、Debug/ASAN/UBSAN/TSAN 分开登记，未运行项不勾选。

## 关联文档和工作项

[M10-01～12](../plans/m10-basic-agent-tools.md)、[主设计第 5 节](../design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)、
[DEC-009](DEC-009-provider-scope-budget-cancellation.md)、
[DEC-010](DEC-010-m1-permission-framework.md)、
[DEC-020](DEC-020-permission-async-confirmation.md)、
[DEC-024](DEC-024-desktop-atom-toolset.md)、
[DEC-053](DEC-053-conversation-desktop-tool-registration.md)。
