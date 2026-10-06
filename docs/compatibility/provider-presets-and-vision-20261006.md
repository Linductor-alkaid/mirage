# M6-22：厂商预设、Messages与MiniMax识图验收

> 状态：Completed（Linux本地验收范围）
> 日期：2026-10-06
> 负责人：Mirage维护者
> 依据：[DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)、[M6计划](../plans/m6-native-frontend.md)、MIRA-20261006-001

## 交付范围

会话侧栏外边距24px，品牌/导航图标中心48px、文字68px；底部分隔线和信息图标同轴。服务名称输入框内边距8px，非编辑标题与文字起点一致。正常1180×800与最小860×620明暗状态使用真实EUI renderer检查。厂商导航224px，窄窗口56px图标栏，2px滚动条及悬停名称；厂商图标来源/摘要/许可证见[provenance](../../apps/native/assets/providers/provenance.json)。

11项公开API预设取自ZCode `29628c9acdb81b703bbd4080c207a0e7ce5e276e` 的`config/provider/zcode-builtin.json`：OpenAI、Anthropic、MiniMax中国/国际、DeepSeek、Kimi、百炼中国/国际、Z.ai、BigModel、Xiaomi MiMo。选择后形成可编辑草稿，预填URL/协议/默认模型；输入Key保存，经服务ACK才进入已应用目录。不自动连接或发现模型；未保存改动不能因切换预设丢失。只对MiniMax有真实互操作证明，其余为配置便利，账号和模型可用性依厂商当前授权而定。

Messages方言及SecretRef ApiKey认证由Mira公开层交付，Mirage只选用公开方言，不新增HTTP循环或并发设施。拒绝不可表达字段/内容及extended thinking签名回填，保留原OpenAI行为；启用思考深度在配置验证前拒绝。MiniMax-M3上下文预算1,000,000；未知厂商预算不伪造。

## 实测结果

Linux x86_64、GCC 13.3、CMake/Ninja、原生EUI GLFW/OpenGL；Executor2ae4fc8保持不变。实际命令与结果见下表，不将目标平台缺测写成通过。

| 验证 | 命令/证据 | 结果 |
| --- | --- | --- |
| Debug/Release/ASAN/UBSAN | `cmake --build build/<配置> --target mirage-native mirage-service native_agent_integration_test native_conversation_view_test persistence_test -j 3`；`ctest --test-dir build/<配置> -R '^(native_conversation_view_test\|native_agent_integration_test\|persistence_test)$' --output-on-failure` | 每配置3/3通过；原生renderer219 checks，harness173 checks |
| 完整Debug回归 | `cmake --build build/native-debug -j 3`；`ctest --test-dir build/native-debug --output-on-failure` | 50/50通过 |
| TSAN | 构建harness/persistence；`setarch x86_64 -R ctest --test-dir build/tsan -R '^(native_agent_integration_test\|persistence_test)$' --output-on-failure` | 2/2通过；普通ASLR启动出现unexpected memory mapping，不计成功 |
| Mira协议 | `/tmp/mira-stream-build` 的anthropic/dialect/sse/transport/gateway/tool_loop/conversation_loop | 7/7；三种sanitizer下新增Messages fixture/变异种子实际通过；完整UTF-8分片、工具JSON、缺终态/错误/预算、usage元数据、真实socket认证及注入拒绝；确定性单字节变异种子 |
| 格式/分层 | `cmake -DROOT_DIR=/home/linductor/mirage -P cmake/CheckFormat.cmake`、`mirage-boundary-check`；Mira format/docs/SBOM/platform/architecture | 通过 |
| 真实预设 | 私有Xvfb/DBus/IBus与独立Runtime Service，Release，从空配置选MiniMax，只填Key保存/刷新 | 138次流式预览，1063字节终态；历史一致、输入法定位、等待动效、未发送草稿不创建会话、正常退出通过 |
| 真实工具 | MiniMax-M3经Mirage ModelLayer/ConversationLoop调用wait1ms | ok=1，steps=2，tools=1，回复“工具往返成功” |
| 真实图片 | MiniMax-M3经Mira公开Messages Provider发送两张合成PNG | 2/2；验证码、左颜色、右形状全部一致，4/6预览，input_tokens385，message_stop终态 |

图片包含随机六位代码及图形，问题不泄漏答案；不上传用户截图。第一次图片请求失败：MiniMax的usage额外对象字段被当作计数拒绝，修复为仅验证已知计数并加入回归fixture；保留[失败记录](../../.impeccable/review/provider-presets-20261006/vision-initial-failure.json)，不计成功。正确结果为7G3PDH/蓝/圆、0X5JJ9/绿/三角，图片4580/5304字节。

持久证据：[当前外观](../../.impeccable/review/provider-presets-20261006/provider-preset-minimax-light.png)、[会话](../../.impeccable/review/provider-presets-20261006/normal-light.png)、[真实会话结果](../../.impeccable/review/provider-presets-20261006/live-results.json)、[识图结果](../../.impeccable/review/provider-presets-20261006/vision-results.json)、[内联复核](../../.impeccable/review/provider-presets-20261006/finish-verdict.md)。真实窗口截图来自最后厂商图标微调之前；最终外观来自本次真实renderer确认批次，不能将两者混作同一时点。

## 复现与限制

受控付费图片探针`third_party/mira/tests/m3/m3_messages_probe.cpp`默认拒绝执行，不注册ctest。外部驱动`tests/manual/minimax_vision_acceptance.py --provider <授权MiniMax配置目录> --probe /tmp/mira-stream-build/tests/mira_m3_messages_probe --output <受控输出目录>`读取已授权本机配置/Key，仅通过子进程环境传递。Key不进入CLI参数、日志或仓库。真实窗口驱动`tests/manual/native_conversation_acceptance.py`增加`--model-settings --preset-minimax`，其余受控环境参数沿用脚本帮助；每次执行发起真实请求。

现有会话附件仍为文本；本次证明模型协议/Provider具有识图能力，尚未提供用户会话的图片附件入口。首阶段不支持Messages扩展思考/服务端工具，明确提示而非假可用。没有ZCode本机原生窗口像素对照，不声明完整1:1复刻；参考的是固定源码的布局与公开API预设。其他厂商未取得授权Key，Windows/Wayland/物理高DPI未跑；负责人维护者在相应环境补跑。M6整体继续In Progress。

官方依据：[ZCode](https://github.com/zai-org/ZCode/tree/29628c9acdb81b703bbd4080c207a0e7ce5e276e)、[MiniMax Messages](https://platform.minimax.io/docs/api-reference/text-anthropic-api)、[Claude Messages](https://platform.claude.com/docs/en/api/messages/create)、[流式生命周期](https://platform.claude.com/docs/en/build-with-claude/streaming)。

Mira固定提交`7795e13cd6b8169f4936016c169702c2c60e876c`，[上游PR#80](https://github.com/Linductor-alkaid/mira/pull/80)已提交未合并，堆叠于#79。锁文件与gitlink相同，verify-only configure通过。Mirage依赖升级[PR#67](https://github.com/Linductor-alkaid/mirage/pull/67)独立评审，UI改动在其上堆叠。CI执行中，不将尚未结束的跨平台job写成通过。

## API Key保存按钮修复（BUG-20261006-001）

维护者报告输入Key后保存仍灰。遮蔽Adapter捕获上一帧的raw快照，连续事件会丢失后续字符；同一TextInputEvent同时带text/pasteText时，EUI分两次插入/推入undo快照，取最后一份无法从原raw重建编辑，变更回调不执行，按钮不启用。现在引用外部owner持有的最新草稿值，并以本批次第一份快照定位整体编辑；成功变更明确请求应用刷新/提示未保存，不放宽model_loaded或保存中门禁。undo/redo仍清空，raw不进入隐藏组件状态。

新增真实handler回归覆盖同一帧连续输入、同一event文字加粘贴、全选替换、隐藏掩码、保存启用、刷新不能覆盖草稿。修复前连续输入测试2项失败，记录/tmp/mirage-key-before-test.log；修复后Debug相关3/3，Release/ASAN/UBSAN renderer各1/1通过。命令`cmake --build build/<配置> --target native_conversation_view_test -j 3`及`ctest --test-dir build/<配置> -R '^native_conversation_view_test$' --output-on-failure`；Debug额外运行harness/persistence。格式与Python语法通过。没有新增并发/生命周期路径，不改依赖。

真实Release窗口在私有Xvfb/DBus/钥匙环使用合成fixtureKey验证：从空配置选MiniMax、粘贴保存、刷新，再只键盘输入替换Key，保存按钮启用，点击后服务ACK产生新的credential_ref。Key不写settings；不请求模型。命令`python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-minimax --settings-only --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-key-live-20261006`；fixture配置模型MiniMax-M3、公开地址和非凭据占位值。驱动拒绝未同时指定model-settings的settings-only，避免误发付费请求。

持久证据：[输入后保存可用](../../.impeccable/review/key-save-20261006/key-typed-save-enabled.png)、[ACK后恢复干净状态](../../.impeccable/review/key-save-20261006/key-typed-saved.png)、[结果](../../.impeccable/review/key-save-20261006/results.json)。Windows/Wayland尚未重跑，负责人维护者在相应输入环境补验；不以X11结果外推。
