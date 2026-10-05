# 原生 API Key、草稿与历史删除验收

> 日期：2026-10-05
> 状态：Linux 首步验证；Windows 待补验
> 负责人：Mirage 维护者
> 工作项：M6-09；决策：[DEC-038](../decisions/DEC-038-api-keys-and-draft-sessions.md)

## 实现与参考

模型页直接输入 API Key，默认遮蔽、眼睛切换本次输入；已存密钥不回传，留空保持，
显式移除后保存清除。普通 service.json 仅保存 credential_ref。Linux Secret Service /
Windows Credential Manager 在 Platform Backend，integration 复用 Mira ISecretResolver。
失败关闭，不退回明文文件。旧环境变量配置可继续读取。

参考 ZCode 源码 29628c9acdb81b703bbd4080c207a0e7ce5e276e：ApiKeyInput / ProviderDraftSave
与 v4 draft-root 的首发提升语义。参考来源是公开源码，未宣称对正在运行的 Wayland ZCode
作像素复刻。新建只保留 UI 草稿；首次提交才打开远端，实际消息才进入历史。侧栏垃圾桶确认
后调用 session.delete，活动拒绝，写盘成功前不改变内存/可见历史。

系统 API 依据：[Secret Service](https://specifications.freedesktop.org/secret-service/latest/)
及 [CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)。
所有调用在既有 Executor 有限 handler / Mira blocking transport worker；不创建自研线程、队列。
GDBus 方法共享2秒预算，连接/认证套接字设置2秒超时；不将二者宣称为整体调用硬实时上限。
密码编辑单一适配的限制和移除条件：[EUI-20261005-004](../dependency_feedback/eui-ledger.md)。

## 自动化证据

| 检查 | 实际结果 |
| --- | --- |
| native-debug 全量构建与 CTest | 49/49，通过 |
| native-release 相关4项 | 4/4，通过 |
| ASAN / UBSAN 相关4项 | 各4/4，通过 |
| TSAN | 默认运行受 unexpected memory mapping / 启动崩溃阻断；同4项经 setarch x86_64 -R 运行4/4，无race诊断 |
| Native ChatModel / secret-edit | 340 checks，0 failures |
| Native agent integration | 126 checks，0 failures |
| 真实系统钥匙环 probe | 10 checks，0 failures；synthetic 项写/读/移除 |
| IPC golden | 新写Key、移除、删除、warning 及既有vectors通过 |
| 公共头边界 | 49 headers / 0 violations |
| 全树格式、diff whitespace | 通过 |

126 checks 包含凭据写拒绝、配置写盘失败后的新引用回滚、留空保持、显式移除、旧引用清理
失败 warning；空草稿重复新建、首发、活动删除拒绝、写盘失败保留历史、主会话产品对话清除、
迟到消息拒绝、删除后的服务重启无复活。原有正常/异常/拒绝/取消/超时/shutdown覆盖保持。

复跑：native-debug 全量；其他预设构建并运行 native_chat_model_test、credential_store_test、
ipc_protocol_golden_test、native_agent_integration_test。系统 probe 需同一桌面用户的已解锁
Secret Service：`./build/native-debug/tests/credential_store_test --probe-keyring`。
调试日志位于本机 /tmp/mirage-key-*-tests.log，保留至本轮结束后七日；不提交运行日志。

## 实际窗口与模型

在独立临时配置/状态和 socket 中验证，没有覆写用户配置。正常1180×800和最小860×620逻辑
窗口（2×像素）检查明暗模型页/删除确认、掩码、synthetic 显示与零历史/真实消息。
八次实际新建操作未增加远端会话；首发出现历史。Key通过遮蔽输入粘贴并保存，私下核对系统
凭据与已授权本机 Mira 的 SiliconFlow Key一致；model.get与普通配置没有实际 Key。
Qwen/Qwen3.5-4B真实回复成功；重启不注入 Key 环境变量后，新 Agent 会话同样回复成功。
实际历史垃圾桶确认后 ACK 移除，再次重启确认原 ID 不在 session.list。

截图位于 `.impeccable/review/native-keys-*.png`；显示模式图只使用 demo-key-for-visual-check。
真实密钥不显示在截图、命令参数、日志或仓库。一次实机键入测试因未重新聚焦输入，保存了
测试用 synthetic 值，供应商拒绝；重新聚焦全选后核对系统凭据，实际调用成功。保留失败历史
截图用于故障态取证，不把接纳或“已配置”当作供应商验证成功。

## 限制与补跑

- Windows Credential Manager 实现未在本 Linux 环境编译/运行；负责人维护者，条件为
  Windows MSVC native preset、已登录桌面凭据读写与重启调用，不标记目标平台完成。
- 系统钥匙环必须可用且解锁；此阶段不自动弹出钥匙环 unlock/create prompt。
- EUI 密码输入 Adapter 暂不支持撤销/重做；遮蔽时复制只复制掩码，不读取已存密钥。
- DEC-028 已有恢复限制：hydrated 历史是产品记录，未恢复 pinned Mira 活动身份，可查看/
  删除，但重启后的旧 Agent 会话不能继续运行。此轮 Key 重启验证使用新会话，不掩盖该限制。
- 主会话历史删除保留设备默认身份和任务审计；不声称擦除所有日志/任务证据。
- Linux 中文 IME、原生 Wayland、Windows窗口、托盘整体退出与完整安装生命周期仍由原
  工作项补验；M6-04不在本轮范围内。


## 独立复核修正

首次独立复核 disposition=fix：删除确认缺少目标上下文、重连未按权威列表清理已删除历史。
删除窗现展示首条输入和最近消息摘要，支持相似/省略标题核对，高度316px；其余确认保持252px。
ChatModel同步服务端ID时移除消失的稳定历史，空历史也清理旧行，保留活动请求和本地草稿。
新增11条检查验证外部删除、活动保护和选中草稿保持，334 checks通过；Debug/Release /
ASAN/UBSAN/TSAN -R 的ChatModel已在修正后重建重测。13张相同路径的矩阵重新捕获。
真实成功回复的修正前取证另存 native-keys-real-model-before-review.png；修正矩阵的同标题
记录使用独立 localhost:9 故障注入生成，捕获前恢复真实模型配置，不把失败示例当成功调用。
原生无HTML detector；复核使用fresh通用子agent加载finish-reviewer输出契约（当前harness
不提供按名选择shipped角色），独立于构建上下文。最终结论另附。

第二次修正将删除预览限制为UTF-8完整的前256字节、首行，并规范控制字符；新增6条
长ASCII/CJK/多行检查，340 checks在五个预设通过。明暗/大小四张确认图使用非选中行
作为删除目标，目标最近消息与当前会话不同；外部删除后重连仍保留本地草稿。

最终独立[复核结论](../../.impeccable/review/native-credentials-finish-verdict.md)为
`disposition: ship`，范围仅为原两项修正与其引入的预览回归，不扩张为整体平台验收。

独立final documenter同步根/原生两套DESIGN与JSON侧车，保留中性色板和既有字阶；
确认当前直接Key、草稿接纳、固定目标ID删除与256字节摘要规则。既有根DESIGN的
assets/provenance.json相对链接漂移仍保留，本轮新增文档链接目标可解析，不宣称全树文档无漂移。
