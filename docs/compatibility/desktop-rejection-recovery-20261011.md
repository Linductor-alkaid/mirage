# 最新会话拒绝恢复验收（M7-14 第二增量）

> 状态：本地实现与 Linux 验收已通过；真实用户任务待复验
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 决策：[DEC-070](../decisions/DEC-070-bounded-desktop-failure-evidence.md)
> 工作树：feat/m7-conversation-desktop-tools，基于 c786df7；尚未提交、推送或安装到用户系统

## 会话证据与原因

最新记录中，截图和默认 OCR 已成功，OCR 返回 89 区域；激活在参数校验阶段被拒绝，
readonly action.verify 证明未派发、无副作用。不能据此认定 Firefox 或 X11 无法激活。
旧历史隐藏条件正文，无法确定当时具体错了哪个字段。脱敏工具序列见
[诊断回执](data/desktop-rejection-recovery-20261011/session-errors.json)，没有复制用户
提示、回复、窗口标题、截图、凭据或原始会话 ID。未手工覆盖历史或配置。

持久化拒绝 JSON 为 720 字节；取前 512 字节不能解析。pinned Mira ToolExecutor 的
safe_error_summary 正好最多 512 字节，原 Mirage 在错误中嵌套完整 verification，
导致 canonical action_id 丢失，宿主将自身明确未派发拒绝当成未知失败，永久封锁本轮
后续修改。另一个收尾判定扫描所有旧失败卡片，即使修正已验证仍判整轮失败。
两处属于 Mirage Adapter，不登记依赖缺口、不修改 pinned 代码。

## 修复行为

- 已建动作记录的 canonical 失败 JSON 最多 512 字节，保留宿主持有的 action_id、
  派发/副作用/输入释放状态和原因。Provider 错误提取有界 message/error_code，
  控制字符转义膨胀时使用固定短消息。完整 verification 单独投影至产品过程/历史。
- 参数错误给出字段名；无关空 expected_* 视为未声明，非空错配、类型/长度错误、
  未知条件和缺失必需条件仍拒绝。历史只增加字段名与类型，不存条件正文。
- 收尾依同一 canonical 历史及宿主记录判定。自身未派发拒绝可在新观察后纠正；
  最后拒绝、过期/伪造/截断/未知及不确定输入仍不能成功，不自动重发输入。
- 编号失败返回 capture/analysis/publication/environment、稳定代码与处理建议。
  同类全屏超预算失败可用小 ROI 修正，保持原 8 MiB、1:1、五秒预算。旧历史的
  generic mark 错误不能还原精确阶段；不能断言用户当时两次失败均为同一预算错误。
- 模型指南明确条件字段、空编号没有目标，以及 image_pixels 到 desktop_pixels 的
  来源比例映射。canonical OCR 有区域正文，历史卡片故意只保留计数。

## 实际验证

Ubuntu GCC 13.3.0、CMake 3.28.3，Linux 7.0.0-38-generic x86_64；现有 pinned
Mira/Mirador/kairo、不增并发 owner。Release 启用 native/PP-OCR/bundled vision，
Debug 沿现有测试配置，sanitizer 定向使用现有 OCR 测试构建。

| 检查 | 最终结果 |
| --- | --- |
| native-release 全量 | 70/70：68 个业务测试 + 2 个隔离 fixture |
| Debug 全量 | 64/64：62 个业务测试 + 2 个隔离 fixture |
| ASAN + UBSAN 定向（泄漏/错误即失败） | 11/11：9 个业务测试 + 2 个隔离 fixture |
| TSAN 定向（setarch x86_64 -R；仅系统 GLib/GIO 抑制） | 11/11：9 个业务测试 + 2 个隔离 fixture |
| post_action_verification_test | 261 checks；4 个真实 X11 任务 |
| harness_desktop_tools_test | 478 checks，0 failures |
| visual_marks_test | 679 checks |
| 实际 Release 产品 / 新 DEB 解包产品 | 各 37/37 |
| 公共头边界 / 本次 8 个 C++ 文件格式 / diff | 53 头零违规；通过 |

真实 X11 第四场景经实际 ModelLayer、Mira ConversationLoop 和 ToolExecutor：先从
root 取焦，错配条件拒绝，读取 action.verify，重新 window.list，空可选字段修正，
目标窗口真实取得焦点，最终结算成功、键盘全部释放。没有网络模型或用户浏览器操作。
其他三个任务保留激活、文字回显、提交落盘。负例验证未知/截断/伪造/不确定回执仍封锁。

两个实际产品各运行默认 PP-OCR 和公开模板 workflow，模型 disabled，11 个运行、
6 次物理点击、两次 Runtime owner 正常退出。它们是默认装配回归；会话恢复由前述
真实 ToolExecutor/X11 测试证明，不能将 workflow 回归称为在线模型验收。
源码、最终日志 SHA-256、包与产品回执见
[机器摘要](data/desktop-rejection-recovery-20261011/summary.json)。

首次恢复回归暴露旧卡片收尾门禁和 Provider JSON 被嵌套后丢失错误原因；修复后通过。
旧 backend_scope 测试期望永久封锁，现按明确无副作用拒绝后的新观察允许另一完整键盘
动作，检查目标未点击、键下/键上释放。初次期望计数漏算两个键事件，随后修正。
首次构建缺失已审计临时 XRes SDK，按原包 SHA-256 恢复；首次 Release 全量 67/68，
仅固定 OCR 源缓存被清理。从持久缓存核对锁定五文件后恢复，最终全量 68/68。
没有更换依赖或降低门禁，失败历史不计为成功。

收尾发现用户历史文件的摘要/mtime 变化，并新增一个较早时间的会话，最新诊断轮次仍
保留。既有 observation_face / session_client / overlay 等 Runtime 测试未完整隔离
默认会话/配置目录，不能再声明测试未触及用户状态。没有删除新增记录或覆盖现有历史；
service.json 的修改时间仍为本轮前。已为所有 Linux CTest 增加每测试独立 XDG 状态、
配置与数据目录，以及开始清理/结束回收 fixture；同构建的多次 CTest 串行运行。
隔离后的全量及 sanitizer 复验另记机器摘要，并核对用户文件摘要和 mtime 保持稳定。
最终全量数量包含两个隔离 fixture：Release 70 项、Debug 64 项；定向 11 项含 9 个
业务测试。前面 68/62/9 是补隔离前的行为验收，不抹掉这次隔离遗漏。

## 复现与使用

```sh
cmake --build --preset native-release
ctest --test-dir build/native-release --output-on-failure
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
python3 tests/manual/local_visual_workflow_acceptance.py --build build/native-release --output /tmp/mirage-m714-product-replay
cpack --config build/native-release/CPackConfig.cmake -G DEB -B out/packages
```

定向 sanitizer 运行九项 local_visual_workflow_test、local_semantic_workflow_test、
visual_marks_test、visual_text_verification_test、post_action_verification_test、
harness_desktop_tools_test、runtime_service_test、visual_template_index_test、tray_runtime_test；
环境分别为 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 / UBSAN_OPTIONS=halt_on_error=1，
以及 TSAN_OPTIONS=halt_on_error=1:suppressions=tests/support/tsan-glib.supp（需绝对路径）
和 setarch x86_64 -R。完整真实 OCR 权重未做 sanitizer 验收。

当前用户桌面入口指向已重建 native-release/apps/mirage；退出旧托盘后重启，在新会话
复验原任务。默认 OCR 无需重配。既有显式关闭/自定义设置继续优先。原在线浏览器
业务、用户 GNOME 焦点/全屏 DPI、Windows/原生 Wayland 未在本轮验收；负责人 Mirage
维护者，补跑条件为新产品进程、目标应用初态与桌面权限准备完成。M7-14 第二增量保持
In Progress，待真实任务复验与提交评审；M7-11 及 M7 其他开放项不因此关闭。
