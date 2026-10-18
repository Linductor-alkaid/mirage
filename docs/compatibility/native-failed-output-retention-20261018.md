# M6-46：失败轮次保留模型正文（2026-10-18）

> 状态：In Progress（Linux 本地回归已执行，实机/Windows 待补验）
> 决策：DEC-104
> 基线：`dcb9113` 加本轮工作树
> 负责人：Mirage 维护者

## 真实会话诊断

只读分析维护者最新会话的两轮失败记录；不复制私人请求、联系人、原文或思考内容到
仓库，不改写用户会话，不重试用户的外部通信任务。运行中的服务会话类型为 Wayland，
同时具有 WAYLAND_DISPLAY 与 XWayland DISPLAY。

两轮共 9 次模型调用、11 次工具请求。窗口/应用枚举、截图和 OCR 均返回
`pet_exclusion_unavailable`，内容指明需要 verified X11 capture path、Wayland pending；
其中截图在三次相同确定失败后被会话级 guard 拦截。没有调用输入或发送工具，因此
记录不能作为消息已发出的证据。这一拒绝发生在 Provider/Portal 授权之前，不能推断
Portal 授权本身失败。直接原因是 M8-06 的产品呈现排除前置条件未覆盖 Wayland，
而 decorator 又包住所有桌面工具，包括枚举入口。

当前可选的人工绕行是从托盘停止桌宠、使用新会话再测试既有 Portal 路径；新会话
避开旧会话的确定失败封锁。该路径本轮没有执行，不承诺授权或业务任务一定成功。
Wayland 桌宠联动仍需产品 backend 的隐藏及新帧确认，本轮没有以 XWayland shape
或跳过守卫冒充 Portal 干净帧。

## 覆盖正文的根因与修复

pinned Mira 的公开 ConversationLoopResult.answer 表达模型最终答复。宿主的 action
settlement 会把已回答轮次转成失败，但原 ModelLayer 只在 completion.ok 时保存答案；
最终正文因此不进入历史。UI ChatModel 又在 failed 时用 error 覆盖流式 text。

现保留被后置核验判失败的模型答案，采用已有 text Part（id terminal.reply）传输和
持久化，仍为 failed/error，不扩展 wire 字段、版本或 schema。16KiB 正文预算优先
给最终答复，旧中间文字缩减并标记；96 项容量优先移出思考，再移出最早的已结算项。
UI 提取正文并独立保存错误，Markdown 正文和错误各自排版，高度缓存包含错误。
复制/引用的正文不再混入系统报错。当前 UI 的中断预览保留；没有规范最终答案时不
把预览转成规范历史。旧记录已丢弃的最终答案无法恢复，未从思考内容合成替代答案。

## 验证

环境：Linux x86_64，GCC 13.3.0、CMake 3.28.3、Ninja，依赖版本未变。
测试使用脚本 provider 和私有 XDG/Xvfb，不调用在线模型，不操作用户桌面。

```bash
cmake --build --preset native-release --target native_chat_model_test \
  native_conversation_view_test conversation_budget_test harness_desktop_tools_test persistence_test -j3
ctest --preset native-release \
  -R '^(native_chat_model_test|native_conversation_view_test|conversation_budget_test|harness_desktop_tools_test|persistence_test)$'
cmake --build --preset native-release -j3
ctest --preset native-release -j2
# asan / ubsan / tsan 各构建以下四个测试。
cmake --build --preset asan --target native_chat_model_test conversation_budget_test \
  harness_desktop_tools_test persistence_test -j2
ctest --preset asan -R '^(native_chat_model_test|conversation_budget_test|harness_desktop_tools_test|persistence_test)$'
ctest --preset ubsan -R '^(native_chat_model_test|conversation_budget_test|harness_desktop_tools_test|persistence_test)$'
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan \
  -R '^(native_chat_model_test|conversation_budget_test|harness_desktop_tools_test|persistence_test)$'
# 从含实际字体资源的目录运行渲染取证。
cd /home/linductor/mirage/build/native-release/apps/native
/home/linductor/mirage/build/native-release/tests/native_conversation_view_test \
  /tmp/mirage-failed-output-render-final
```

针对规范模型答复随后权限/动作核验失败的真实 Runtime/IPC 路径，检查 failed 状态、
error 和 terminal.reply 同时保留；持久化往返、UTF-8/满分类预算/满96项/幂等覆盖、
当前预览保留、迟到预览拒绝、旧失败只有错误、规范失败历史恢复均有测试。
原生渲染在正常明亮和小窗暗色检查正文与失败原因同时可见且不重叠，并检查错误更新
会刷新高度缓存。截图为公开 fixture 内容，位于 /tmp/mirage-failed-output-render-final。

| 检查 | 实际结果 |
| --- | --- |
| Native 定向五类测试及状态 fixture | 7/7，6.37 秒 |
| 完整 Native Release 构建与回归（更新旧断言后） | 构建成功；101/101，107.83 秒 |
| ASAN：模型、预算、真实 Runtime/IPC、持久化及 fixture | 6/6，57.51 秒 |
| UBSAN：同上 | 6/6，34.99 秒 |
| TSAN：同上（禁 ASLR，halt_on_error=1） | 6/6，122.71 秒 |
| 原生渲染与布局断言 | 28,636 checks，0 failures；明亮/小窗暗色截图已人工复核 |
| 格式、公共头边界、diff 空白检查 | 通过；62 headers / 0 violations，保留 DEC-096 两处既有 seam |

日志前缀为 `/tmp/mirage-failed-output-`，依次为 `test.log`、`asan-test.log`、
`ubsan-test.log`、`tsan-test.log`、`render-final.log`。sanitizer 范围不含真实
EUI/GLFW 窗口；目标环境验证保持开放。
最终全量日志为 `full-build-final.log` / `full-test-final.log`，静态检查为
`static-final.log`。`native-release` 磁盘上的服务/托盘/前端程序已随完整构建更新。

## 过程中的失败与限制

新渲染用例首次编译漏写 namespace，且重复声明 session_id 触发 Werror=shadow；修正
fixture 后通过。首次独立截图从仓库 cwd 启动，缺字体，FT_New_Face 失败，28,627
检查中1项失败，截图不作为视觉通过证据；改从 CTest 配置的原生资源目录运行，
28,636 检查、0失败，复核中文正文/失败原因在明亮及小窗暗色均可读。

首次完整 Native 回归为 99/101，101.38 秒。失败项 post_action_verification_test
和 x11_pointer_actions_test 仍断言“核验失败必须删除模型答复”。按 DEC-104 更新为
检查失败状态/错误仍在、原文同时保留在 terminal.reply；原动作核验和输入释放断言
保留。原始失败日志为 `/tmp/mirage-failed-output-full-test.log`，不将其记为通过。

未重启当前用户运行实例、未修改用户历史文件。新代码需重新启动受管产品进程后生效。
Windows、完整原生 sanitizer 窗口及真实 Wayland 正向联动由 Mirage 维护者取得目标
环境后补跑；本轮 Xvfb 截图不能关闭 M8-06 的真实 compositor 门禁。
