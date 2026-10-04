# DEC-037：原生模型配置、输入工具栏与旧前端退役

> 状态：Accepted
> 日期：2026-10-05
> 依据：维护者明确要求参考 ZCode 模型配置和指定工具栏顺序，并清除旧 TS 前端。
> 关联：M6-07、M6-08；替代 DEC-033 中迁移期保留 CEF/TS 的局部约定。

## 决策

1. 设置采用 ZCode 的服务商导航/配置分栏，保留当前服务端凭据环境变量契约。增加最多 12 个命名模型配置，服务端持久化并返回目录；切换通过 model.set，活动任务期间拒绝切换，失败保持原配置。
2. 输入栏顺序为附件、访问权限、上下文比例、模型、思考深度、发送。每个本地会话保留自己的权限和思考选择；提交冻结到 IPC 请求。只读禁用工具，默认仅开放已注册安全工具（当前仅 wait）；完整桌面访问尚未实现，不提供虚假授权。
3. 附件首步仅支持用户主动选择的 UTF-8 文本，最多 4 个、合计 8 KiB，引用与附件与草稿共同受 16 KiB 请求上限限制；文本明确标记不可信。使用 EUI 公开同步系统文件对话框，文件读取由前端唯一 Executor 管理，拒绝非普通文件、二进制、超限，不隐式读取目录。取消选择无错误，加载失败可见。
4. 思考档位默认不传参数；供应商配置明确启用 reasoning_effort 后提供 minimal/low/medium/high，经 Mira 公共 ModelRequest.generation 映射。未配置能力时禁用并说明；供应商拒绝参数向用户返回错误，不降级重试。
5. 删除 ui/、apps/desktop/、apps/devbridge/、CEF CMake 下载器和相应测试/Node CI。C++ IPC golden 位于 tests/runtime/data，既有向量保持不变，只移除 TS 消费者元数据。依赖锁使用 schema 3 明确 native frontend，不再登记 npm/CEF，所有 gitlink 和 nested pin 验证继续强制。
6. 保留旧文档作为历史，标记旧方案已退役。清理前备份工作树未提交旧文件至 /tmp/mirage-legacy-before-retirement-20261005；不回写第三方源码。原生 UI 安装替代 CEF，完整托盘退出确认仍由 M6-04 验收，不因本次清理宣称整体生命周期完成。

## 验收

Linux Debug/Release、IPC/配置/权限/reasoning 和附件边界测试；ASAN/UBSAN，跨上下文附件和 shutdown 的 TSAN；真实模型文本附件请求；正常/最小明暗窗口、模型设置和弹出菜单截图；依赖门禁保持拒绝错误 pin。Windows GUI 与真实 IME 由维护者在目标环境补跑。

2026-10-05 Linux 首步已执行，结果和限制见[模型/输入栏验收](../compatibility/native-model-composer-20261005.md)
及[退役验收](../compatibility/native-retirement-20261005.md)。
