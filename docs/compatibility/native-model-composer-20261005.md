# 原生模型配置与输入工具栏验收（2026-10-05）

> 工作项：M6-07（Linux 首步）
> 决策：[DEC-037](../decisions/DEC-037-native-model-composer-and-web-retirement.md)
> 负责人：Mirage 维护者
> 环境：Linux / XWayland，C++20，EUI dev 4691fc0a；Mira、Mirador pin 不变
> Git：codex/native-agent-workbench；本地提交，不推送或合并

## 实现与参考依据

参考 [ZCode](https://github.com/zai-org/ZCode) 的模型服务商分栏和
V4ComposerToolbar 源码，核对提交 29628c9acdb81b703bbd4080c207a0e7ce5e276e。
正常窗口使用服务商列表与配置详情，小窗口使用具名配置选择器；关闭时保留选中名称，
打开时显示各配置名称。六项配置、协议、推理能力声明及保存/放弃均使用真实服务 ACK。
这次没有取得运行中 Wayland ZCode 的截图，不声明像素级复刻。

输入栏从左到右为附件、访问权限、上下文比例、模型、思考深度，发送按钮位于末端。
模型目录最多 12 项，名称唯一；服务持久化采用有界配置。运行中拒绝更换模型。
生产 SocketHttpTransport 的替换通过公开 worker_name 配置区分旧/新实例，避免替换时
阻塞 worker 名称冲突；没有修改 pinned 源码或另建并发生命周期。

附件为用户选择的非空 UTF-8 普通文本文件，单次最多 4 个、合计 8 KiB；不支持 PDF、
图像、目录或任意文件解析。Executor 管理读取和结果，POSIX 拒绝 FIFO 与符号链接。
提交前可检查全文并删除；附件标记为不可信上下文。任务文字在附件前，附件单独发送时
用文件名生成会话标题，防止空标题。

权限为默认/只读：当前通用 harness 默认只有既有 wait 工具，只读不注册工具；这里没有
新增文件、桌面或 RPA 工具授权。思考深度按每轮请求接线，只有明确声明支持的模型可选；
配置声明不是供应商兼容性证明。上下文为最近成功请求的真实输入 Token / 显式窗口预算，
未伪造草稿实时统计。

## 构建、回归与故障注入

执行 native-debug/native-release 配置与全树构建。移除三个旧 bridge 测试后，Debug
全量 CTest 为 48 个。空闲环境顺序执行 `ctest --preset native-debug -j1`：48/48，
45.89 秒。首轮并行及一次与构建同时运行的顺序测试各出现 47/48：
event_subscription_test 的终态等待断言失败；单项重试及空闲全量通过。尚未独立确认
根因，保留失败事实，不把重试通过写成从未失败。负责人：维护者；补跑条件：有负载的
event_subscription_test 重复运行并分析终态等待/超时证据。

最后生产 transport 修正后 Release 原生模型、harness、依赖门禁 3/3；最后标题修正后
Debug 与 Release 原生模型/harness 各 2/2。最终单元 207 checks、集成 87 checks。
覆盖附件边界、取消/迟到结果、关闭、目录保存与活动拒绝、推理协议、只读工具禁用，以及
真实 transport 在同一 Executor 上连续三次更换模型后关闭。集成沿用既有异常、队列
拒绝、执行中取消、超时、shutdown 故障注入。

ASAN（detect_leaks=1）、UBSAN、TSAN（本机以 `setarch x86_64 -R` 执行）各完成原生
模型/harness 2/2，无对应诊断；标题修正后的 ASAN/UBSAN 模型 1/1 复验通过。
完整自研格式检查、48 个公开头边界检查（0 violations）、git diff --check 通过。
命令可按 `CMakePresets.json` 的 native-debug/native-release、asan/ubsan/tsan 配置复跑，
CTest 目标为 native_chat_model_test 与 native_agent_integration_test。

## 真实模型与界面证据

使用维护者授权的本机 Mira SiliconFlow 环境凭据，隔离临时配置/状态，模型
Qwen/Qwen3.5-4B。实际系统文件选择器加载 77 字节文本，用户任务
“提取附件中的颜色校验值，只回复精确文本。”得到“青柠-37”；最后请求 432 / 128000，
界面显示 0.3%。模型目录保存/即时更换实际 ACK 成功；凭据只经环境引用，不纳入证据。
推理等级由离线请求断言验证，截图中启用能力的配置是演示夹具，未宣称真实供应商推理
互通通过。[脱敏结果](../../.impeccable/review/native-model-live-results.json)。

以下截图均来自实际 Mirage 窗口；正常尺寸 1180×800、最小尺寸 860×620（逻辑像素），
明暗、小窗口、配置下半部与 footer、附件检查/提交、权限、模型、推理和上下文都已打开核验。

- [模型设置浅色](../../.impeccable/review/native-model-settings-light.png) / [深色](../../.impeccable/review/native-model-settings-dark.png) / [下半部](../../.impeccable/review/native-model-settings-lower-dark.png)。
- [最小浅色具名选择器](../../.impeccable/review/native-model-settings-min-light.png) / [深色](../../.impeccable/review/native-model-settings-min-dark.png) / [浅色关闭](../../.impeccable/review/native-model-settings-min-light-closed.png) / [深色关闭](../../.impeccable/review/native-model-settings-min-dark-closed.png) / [选中备用配置](../../.impeccable/review/native-model-settings-min-selected.png)。
- [放弃](../../.impeccable/review/native-model-settings-discard.png) / [保存](../../.impeccable/review/native-model-settings-saved.png)。
- [输入栏](../../.impeccable/review/native-model-composer-light.png) / [最小浅色](../../.impeccable/review/native-model-composer-min-light.png) / [最小深色](../../.impeccable/review/native-model-composer-min-dark.png) / [模型选择](../../.impeccable/review/native-model-picker.png)。
- [添加附件](../../.impeccable/review/native-model-attachment-menu.png) / [已加载](../../.impeccable/review/native-model-attachment-loaded.png) / [原文检查](../../.impeccable/review/native-model-attachment-preview.png) / [实际回复与标题](../../.impeccable/review/native-model-attachment-reply.png)。
- [真实占比](../../.impeccable/review/native-model-context.png) / [权限](../../.impeccable/review/native-model-permissions.png) / [只读](../../.impeccable/review/native-model-readonly.png)。
- [未支持推理](../../.impeccable/review/native-model-reasoning-unavailable.png) / [最小窗口](../../.impeccable/review/native-model-reasoning-unavailable-min.png) / [等级夹具](../../.impeccable/review/native-model-reasoning-levels.png)。

独立初审发现空标题、小窗口配置辨识、PRODUCT 当前事实三项问题：
[初审](../../.impeccable/review/native-model-finish-verdict.md)。同一 reviewer 对修正列表
复核：[最终 verdict](../../.impeccable/review/native-model-final-verdict.md)。四份原生 DESIGN /
JSON 已由独立 documenter 按源代码同步。

## 未执行与后续

Windows 构建、原生中文 IME 和供应商推理互通未执行；原因分别是当前 Linux 环境、
没有实际 IME 测试输入、供应商模型能力未验。负责人：维护者；补跑条件为 Windows
native preset 与安装包/窗口/IME 真机交互、真实支持 reasoning 的模型请求。
统一入口的活动退出确认属于 M6-04，保持 Planned；本项不代表整体生命周期验收。
