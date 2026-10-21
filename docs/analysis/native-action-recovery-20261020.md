# 原生会话无效动作恢复调查（2026-10-20）

> 状态：In Progress（本轮修复与离线回归通过；真实模型策略与 SSE 归因开放）
> 工作项：M9-01/09/12，关联 M7-25/26/31
> 问题：BUG-20261020-001
> 负责人：Mirage 维护者
> 基线：e63c8de + feat/desktop-input-policy 工作树
> 更新：2026-10-21（补查 SSE 公开诊断面与旧故障离线复验）

## 已确认事实

维护者要求调查最新一次聊天应用任务。只读分析本机最近保存的会话，不复制用户目标、
联系人、正文、完整历史、截图或认证配置到仓库。会话在 2026-10-20 22:13:43（本地）
保存为 failed，共 53 模型步骤、54 工具调用、4 次 OCR。最终错误为
`model call failed: invalid Messages SSE lifecycle or budget`。输入动作过程中和终止
传输错误分别分析，不能仅因最终 SSE 报错推导之前鼠标或应用输入已成功。

- 保存的图片为 1600×1000 的预览，源为 3200×2000。前段两次 click 的 y=980、
  height=26，超出图片高度；后一次用了新的 image_scope，仍是同一个非法矩形。
  回执把坐标越界与过期代际混成 stale_or_invalid_target，无法指导修改坐标。
- 前段保留参数的 click 均未请求 click_count=2。后半段部分参数和回执被展示预算
  省略，不能据此断言整个任务从未使用双击。用户报告未正确采用双击打开，需补强
  “选择”和“打开”的策略区分；不能为所有按钮自动双击。
- 明确存在成功派发 alt+f2，随后又提议 ctrl+alt+t（被 target_pixels_changed 拒绝）。
  用户报告“输入一个指令”弹窗，与运行命令入口一致；目前没有底层错发键的证据。
  不把系统命令框、应用搜索、聊天编辑器或终端视为同一个输入目标。
- 现有 ToolFailureTracker 按环境 epoch 维护计数；M9 连续任务中动作/观察边界可能
  换代，新 epoch 重开计数。不能将它当作跨观察的操作经验汇总；本例不同错误与
  换代交织，累计失败没有形成对模型持续可见的明确纠正要求。
- 既有图片重用注记按 PNG digest + action epoch 计数；即使派发动作后像素完全相同，
  新动作 epoch 也会重开视觉重复计数，不利于识别“已派发但画面没进展”。

## 本轮实施边界

1. 拆分参数/过期/越界拒绝。越界返回 invalid_target_bounds、field 与当前图片
   几何的简短纠正 hint，明确 x+width<=image width、y+height<=image height；
   不通过再截图或弱化输入守卫修复错误坐标。规范失败回执仍受 512 字节限制。
2. 在既有 DesktopActionVerification 有界动作记录中保留临时的尝试摘要、固定
   失败代码/字段/纠正提示；每轮以最多 4 条、2048 字节供给最近未被成功尝试替代的
   干净拒绝经验，同一参数错误跨普通 epoch 累计。仅是历史恢复事实，不是当前观察、
   授权或成功证明；任务/authority 换代和取消不得传播。摘要不含输入正文。
3. 复用 pinned Mira 已有 ModelInputIdentityProvider。视觉重复身份改为任务/会话/
   authority/图源及映射 + 像素摘要，不因普通 action epoch 变化丢失静态重复注记；
   freshness 与可派发守卫继续按原 epoch 检查。无新 inference、worker 或监控 owner。
4. 明确图像坐标与源截图/ROI 坐标；单击只选中且目标是打开时，重新落点后用同一
   调用 click_count=2，而非两个隔着观察的单击。系统快捷键必须按其实际入口选择；
   意外弹窗先识别/关闭并重获目标焦点，不能继续盲输或 Enter。

不建立第二套任务状态机、长期学习系统或全局工具永久黑名单，不用累计工具次数
截断健康任务。固定条件和 uncertain/input release 围栏保持。已有记录容量
32 active + 128 archive 不变；每记录新增摘要/字段/提示有显式上限，恢复供给独立
限制为 4 条/2048 字节，不发送用户输入正文或平台异常正文。

## SSE 与未能恢复的证据

同名通用 SSE 错误历史上由 MIRA-20261009-002 处理过部分截断流，当前 pin 已包含
修复；本次没有保存失败响应的脱敏事件流，不能将旧反馈的根因直接套用。当前公开
ConversationLoop 对规范截断/可重试传输已有有限恢复；不能在已有 54 次工具调用后
重启整个 loop 做非流重放，否则可能重复输入或发送。没有修改 pinned parser、
扩大失败白名单或复发原聊天任务。需补齐最小脱敏 SSE 事件序列，才能按依赖反馈
流程归因及新增反馈；负责人 Mirage 维护者，补跑条件为受控 provider 抓流环境。

2026-10-21 续查使用当前 native-release 的 pinned Mira 库，重新编译并运行仓库内
[旧截断故障的脱敏探针](../dependency_feedback/repro/minimax-cutoff-sse.cpp)。
orphan-stop、truncated-tool-json、canonical-cutoff 三种样本均解析为 Incomplete，
进程退出 0；这是离线旧样本复验，不是本次失败流的复现。命令如下：

```bash
c++ -std=c++20 -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/minimax-cutoff-sse.cpp -Lbuild/native-release/third_party/mira -Lbuild/native-release/third_party/mira/third_party/kairo/src -lmira_core -lkairo -lpthread -o /tmp/mirage-cutoff-sse-20261021
/tmp/mirage-cutoff-sse-20261021
```

公开 `IModelProvider::last_sse_stats()` 只提供事件/字节/终态等计数；
`last_trace()` 只提供写入与响应头阶段。Mirage 的两个 provider decorator 已转发
这些接口，未发现诊断被 decorator 隐藏。`ProviderInferOptions::capture_raw_response`
公开契约是保存原始终态响应，不能据此承诺保存被 parser 拒绝的 SSE 事件流；当前
生产装配也没有注入 protected artifact store。ModelLayer 的 EventStore 为进程内
MemoryEventStore，已结束的进程无法从中恢复本次失败事件。本次没有取得额外失败流，
因此仍不能区分生命周期违规、累积预算或其他协议形态；不以旧样本通过代替根因结论，
也不据此登记未经复现的依赖缺口。

本轮不发送真实聊天消息、不操作真实桌面，不把受控脚本当成联系人消息发送成功。
实际实现/回归及剩余真实模型验收将在完成后追加。

运行命令框与快捷键的一手对应关系已核对
[GNOME 官方快捷键文档](https://help.gnome.org/gnome-help/shell-keyboard-shortcuts.html)：
Alt+F2 打开命令窗口；Super 打开 Activities，可用于应用搜索。它们是不同入口；
当前桌面自定义快捷键仍须从实际观察确认。

补充归因：安装应用清单已经报告目标 running=true；它不证明前台可见，也不能据此
判断“尚未启动”。应优先恢复现有窗口/启动器/托盘入口，避免盲目反复启动进程。
现有失败跟踪还把同 epoch 的参数错误计入整个工具的永久封锁：本轮将 canonical
干净的参数/目标引用拒绝保留为可修正失败，只作统计与经验供给，不能阻止后续正确
参数；真实能力缺失仍封锁。此调整不改动作自身的权限、像素、焦点或释放守卫。

## 受控验证与构建命令

- action_recovery_facts：跨普通 epoch/新 image_scope 汇总同参数失败；相同尝试成功后
  移除旧失败提示；40 个拒绝跨 active/archive 仍可归纳；四条/2048 字节硬限，
  不回传输入正文；authority 换代、异任务与取消隔离；控制字符膨胀后仍返回合法
  不超过 512 字节的 canonical 失败回执。
- desktop_image_supply：ROI 越界给出实际图片尺寸和字段提示，仍零输入；普通
  action epoch 后同 PNG 的重复计数延续；相同 PNG、不同裁剪位置/任务/会话/
  task_epoch/authority 时清零。四个 Executor 有限任务并发生成 512 个连续唯一
  计数并消费全部 future，未新增私有线程/队列；此为受控竞争注入，不声称替代 TSAN。
- harness_desktop_tools：三次无效 key 后有效 Shift 仍派发且仅产生 down/up 一对；
  下一轮确实收到同参数三次失败和修正策略。随后注入同名 SSE 协议错误，保留
  terminal failure 且不重放之前工具。真实平台缺失、权限、uncertain/release、
  动作条件与有界持续循环负例继续回归。

首次定向构建发现测试把显式 Result 当作 bool 传入 helper，编译即拒绝；改为检查
Result 成功与 tool failed 字段后再断言。两轮定向各 5/5（3 用例 + state 夹具）通过，
后一轮包含错误分类纠正、实际请求供给和 SSE 后不重放场景。没有调整输入守卫或
删除负例来通过验证。

最终使用以下命令（sanitizer 沿既有 native/OCR 关闭配置，native-release 产品开启）：

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset debug --target mirage-boundary-check
cmake --build --preset native-release --target mirage-native-product action_recovery_facts_test desktop_image_supply_test harness_desktop_tools_test post_action_verification_test desktop_action_policy_test visual_text_verification_test gui_fallback_test local_visual_workflow_test visual_glide_tools_test action_contract_test -j 3
ctest --preset native-release -R '^(action_recovery_facts|desktop_image_supply|harness_desktop_tools|post_action_verification|desktop_action_policy|visual_text_verification|gui_fallback|local_visual_workflow|visual_glide_tools|action_contract)_test$' --output-on-failure -j 3
cmake --build --preset asan --target action_recovery_facts_test desktop_image_supply_test harness_desktop_tools_test post_action_verification_test desktop_action_policy_test visual_text_verification_test gui_fallback_test local_visual_workflow_test visual_glide_tools_test action_contract_test -j 3
ctest --preset asan -R '^(action_recovery_facts|desktop_image_supply|harness_desktop_tools|post_action_verification|desktop_action_policy|visual_text_verification|gui_fallback|local_visual_workflow|visual_glide_tools|action_contract)_test$' --output-on-failure -j 3
cmake --build --preset ubsan --target action_recovery_facts_test desktop_image_supply_test harness_desktop_tools_test post_action_verification_test desktop_action_policy_test visual_text_verification_test gui_fallback_test local_visual_workflow_test visual_glide_tools_test action_contract_test -j 3
ctest --preset ubsan -R '^(action_recovery_facts|desktop_image_supply|harness_desktop_tools|post_action_verification|desktop_action_policy|visual_text_verification|gui_fallback|local_visual_workflow|visual_glide_tools|action_contract)_test$' --output-on-failure -j 3
```

四种配置最终构建成功，产品已重建且未启动/重启；没有清理运行目录或变更依赖 pin。
最终完整/定向回归结果见下方。真实原任务未重放，消息未发送；
SSE 失败本身与模型实际策略改进仍需受控 provider/应用实测，不宣称已解决全部原因。

首次最终全量为 Debug 103/104，native-release/ASan/UBSan 定向各 11/12：唯一
失败是 post_action_verification 的旧断言绑定了通用英文文案，实际错误码、提示
与 512 字节限制均已通过。改为检查文案仍指向 action_id；同时把夹具旧 direct-click
bootstrap 提示换为 DEC-110 当前批准的“刷新 cursor metadata、不得直点建立起点”。
没有放宽错误码/提示末尾/JSON/长度检查或改变产品代码。受影响 DEC-088 的一处
既有本地文档链接多退了一级目录，也同步修正。修正后重新构建并复验。


## 最终结果与剩余门禁

- Debug 全量 **104/104**（含夹具，76.66 秒），无 skip。
- native-release、ASan、UBSan 定向各 **12/12**（10 用例 + state setup/cleanup），
  分别 18.55 / 69.89 / 61.74 秒；sanitizer 无报告。
- 恢复事实专项四种配置各 100 检查、0 失败。跨上下文计数的 512 次受控竞争注入、
  整工具误封修正、实际请求供给与 SSE 后不得重放均通过。
- 四种配置构建成功，完整 native-release product 已更新，未启动/重启产品。
  public header 边界 65 个头文件、0 违规（保留 DEC-096 两条既有 seam）；
  40 份变更/新增 C++ 的格式检查、JSON、14 份 Markdown/442 个本地链接与
  git diff --check 通过。依赖 pin/lock、用户会话和设置未修改。

未执行 TSAN，现有共享诊断状态继续使用原互斥区，通过四个 Executor 有限任务的
受控竞争注入验证；不据此宣称全部竞态消除。Windows、真实聊天应用与模型策略
改善、实际业务目标以及失败 SSE 原因均未验收。负责人 Mirage 维护者；需在独立
受控账号/应用与明确授权下重做真实模型任务，SSE 需收集最小脱敏事件序列后再
按反馈流程归因。M9-01/09/12 与 BUG-20261020-001 不因此全部关闭。
