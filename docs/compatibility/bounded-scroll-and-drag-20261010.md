# M7-09 有界滚动与完整拖拽首增量验收

> 状态：Verified（Linux X11 首增量；M7-09 全范围保持 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-09](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)

## 范围与版本

Linux x86_64 / GCC 13.3 / CMake / Xvfb / XTest；当前分支
`feat/m7-conversation-desktop-tools`，基线 HEAD `c786df7e047f03864f03f3bb1facd413f5111c03`。
增量保留在既有 M7 工作树，未提交/推送。本轮未改变依赖：Mira c75378b、Mirador
ae8cf87、传递 kairo ef821dc、UI-only eui-neo a7e625f。复用 pinned 公共工具/模型
API、Mirador 几何/fusion/SoM、已有 Executor 调用任务与视觉 worker。

新增独立 Provider 能力与 `desktop.vision.scroll` / `desktop.vision.drag` 1.0.0；
scope/编号、固定窗口、权限、lease、原图重采、五秒引用与四次采集门禁保持。
双引用在单次 registry 锁内解析。滚动两轴离散步总量 1..16；拖拽为左键完整
动作，最多 2048 像素、16 段；单次协作预算最多 1000 ms，服从更短的任务/引用
剩余寿命。取消/超时/接管等失败仍释放自有按钮，释放不确认 fence。
两工具进入既有 mutation 后观察、只读 verify 和宿主终态门禁。

## 测试方法

`x11_pointer_actions_test` 在私有 Xvfb 窗口采集真实事件，检查四向轮步数量、
完整拖拽 press→带 Button1 motion→release、终点与最终按键状态。覆盖零/超量
步数、整数溢出、同点/窗外/过长路径、非法时限；预先按住左键/修饰键、焦点、
几何与遮挡拒绝；Button1 按下后取消、异常探针、超时、焦点变化、窗口移动和
takeover 均检查实际释放。轮步后取消只执行第一步；外部 owner 消费有限任务
异常，shutdown 后提交明确拒绝。

同一测试还组合真实 Screen/XTest、Mirador 闭合几何编号/ROI/SoM、Mira
ModelLayer/规范工具循环：scroll/drag 各一个回显正例、一个不回显负例，共 4 个
场景。固定 IModelProvider fixture 从本次 mark 回执选择编号，图片通过现有
supplier；Canvas 在收到实际 release 后改标题/重绘，负例保持不变。动作后观察、
只读 verify 与终态各取新窗口状态，不回显时即使模型返回 done 也必须失败。
Canvas 事件处理是有限测试任务里的有界回调，非生产业务调度设施。
这证明机械输入与局部标题后条件闭环，不证明在线模型规划、真实应用拖放语义或
OCR 文本后条件质量；无需网络、模型凭据或用户桌面。

`visual_marks_test` 沿真实 Mirador 管线和故障 InputProvider 测试两种工具的成功、
不变、权限拒绝、源像素变化、非法终点/预算、异常、释放失败、I/O 失败、执行中
取消与 takeover。检查负原点坐标、旧 epoch/编号无法重复派发、刷新后旧 scope
拒绝、能力缺失不注册、缺双引用 resolver 不注册 drag。fake 回执只证明 Adapter
门禁，真实输入由上述 XTest 测试承担。

`action_contract_test` 追加双引用同锁消费、发布等待、换代和缺终点拒绝；其他定向
测试覆盖现有 Runtime owner、完整输入、验证与视觉点击回归。

## 实际结果

| 门禁 | 实际结果 | 范围 |
| --- | --- | --- |
| native-debug 全量构建 / CTest | 61/61，0 failure，39.58 s | 含实际 service、原生 UI 与既有桌面/模型/生命周期回归 |
| native-release 全量构建 / CTest | 61/61，0 failure，34.54 s | 同一工作树与契约 |
| ASAN+UBSAN 定向构建 / CTest | 8/8，0 failure，33.65 s | 泄漏检查开启；上述输入/工具/Runtime/引用/验证路径 |
| TSAN 定向构建 / CTest | 8/8，0 failure，38.96 s | 同八项，测试进程关闭 ASLR；无 race 报告 |
| 新 X11 测试 | 299 checks，0 failure | 含四向轮步、完整路径/释放、故障和 4 个 Mira 组合场景 |
| visual_marks / action_contract | 669 / 63 checks，0 failure | 两工具门禁与双引用锁边界；sanitizer 同样通过 |
| 变更文件格式检查 | 14 个 C++ 文件，0 violation | 准确文件集合见复现命令 |
| 公共头文件边界检查 | 53 headers，0 violation | 未泄漏 pinned 类型/头文件 |
| 全仓格式检查 | 未通过 | 未修改的 apps/native/app.cpp 有既有格式违例；本轮不做无关格式化 |

首轮新测试曾有 243 checks / 14 failures：fixture 错用 canonical_json_digest，
与 Mira ToolCallOutput 的 payload digest_string 契约不符；复用视觉 worker ID
也影响组合场景。已改用公开 digest_string(to_json_string(arguments)) 并给四个
session 唯一 ID，独立补跑通过，再完成上述全量和 sanitizer 回归。新测试编译
阶段还修正 Xlib None 宏与 pinned C++ 头文件顺序、测试目标的 mirador::fusion
链接，未修改依赖或放宽产品门禁。首轮失败不据修复后结果追溯改写为通过。

本轮机器可读结果见
[验证摘要](data/bounded-scroll-and-drag-20261010.json)。原始构建/测试日志留在当前
机器 `/tmp/mirage-m7-scroll-drag-*.log`；CTest 的详细成功断言位于相应 build 目录
`Testing/Temporary/LastTest.log`。文档链接/围栏和 git diff 检查在归档前执行。

## 复现

```bash
cmake --build --preset native-debug -j2
ctest --test-dir build/native-debug -j2 --output-on-failure
cmake --build --preset native-release -j2
ctest --test-dir build/native-release -j2 --output-on-failure
cmake --build build/ocr-asan -j2 --target x11_pointer_actions_test x11_visual_marks_test visual_marks_test post_action_verification_test action_contract_test harness_desktop_tools_test runtime_service_test x11_backend_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -R '^(x11_pointer_actions_test|x11_visual_marks_test|visual_marks_test|post_action_verification_test|action_contract_test|harness_desktop_tools_test|runtime_service_test|x11_backend_test)$' --output-on-failure
cmake --build build/ocr-tsan -j2 --target x11_pointer_actions_test x11_visual_marks_test visual_marks_test post_action_verification_test action_contract_test harness_desktop_tools_test runtime_service_test x11_backend_test
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -R '^(x11_pointer_actions_test|x11_visual_marks_test|visual_marks_test|post_action_verification_test|action_contract_test|harness_desktop_tools_test|runtime_service_test|x11_backend_test)$' --output-on-failure
cmake --build --preset native-debug --target mirage-boundary-check
clang-format --dry-run --Werror desktop/environment/include/mirage/desktop/input_provider.hpp desktop/environment/include/mirage/desktop/visual_reference_registry.hpp desktop/environment/src/input_action.cpp desktop/environment/src/visual_snapshot.cpp integration/mira/include/mirage/integration/desktop_tool_context.hpp integration/mira/src/desktop_image_supply.hpp integration/mira/src/desktop_visual_tools.cpp integration/mira/src/desktop_action_verification.cpp platform/linux/src/x11_backend.hpp platform/linux/src/x11_backend.cpp runtime/service/src/runtime_service.cpp tests/desktop/action_contract_test.cpp tests/integration/visual_marks_test.cpp tests/platform/x11_pointer_actions_test.cpp
git diff --check
```

sanitizer 复用既有 `ocr-asan` / `ocr-tsan` 配置（ncnn AVX/AVX2/AVX512 关闭，
SSE2/OpenMP 开启）；本轮不加载生产 OCR 权重。ASAN+UBSAN 联合，泄漏检查开启；
TSAN 测试进程关闭 ASLR 以避免已知
shadow 映射冲突。测试中的 550 ms poll 是按下后的期限故障注入，不用于生产
调度或宣称实时控制。

## 限制与补跑条件

| 未完成项 | 原因 / 范围 | 负责人 / 补跑条件 |
| --- | --- | --- |
| Windows / 原生 Wayland | 未新增这些 Backend 的新能力；无目标平台运行证据 | 维护者；目标平台适配且权限/会话可用后执行同等输入/取消/验证矩阵 |
| 用户 Xorg 宿主真实应用 | 本轮只操作私有 Xvfb，未要求人工取焦 | 维护者；准备自有滚动/拖放窗口后补事件、效果、取消与焦点取证 |
| 在线模型选号/业务规划 | 本轮 Provider 为固定规范 fixture | 维护者；在既有受控视觉基础上另做在线任务，不改五秒引用策略 |
| 混合 DPI / 跨屏 / 任意应用 | 仅同窗线性路径和明确全局像素坐标；未声称跨屏或拖放协议语义 | 维护者；提供对应显示器和应用场景后补验 |
| 完整 OCR 权重 TSAN 业务期限 | 既有开放项，本轮没有运行其验收 | 维护者；按原 regional 记录补跑，不能用本轮定向 TSAN 替代 |

X11 查询与注入不是原子事务；core mask 可检测 1–5 号按钮，不提供扩展按钮
按住状态。清理确认依赖 XTest 请求/同步，拖拽另查 Button1 已释放；不宣称扩展
物理设备覆盖、平滑滚动、滚动像素距离或硬实时取消。完整拖拽不在路径中重做
OCR/视觉推断，最终业务效果只由固定后条件的新观察判断。

M7-09 保持 In Progress；受控 Linux 首增量与其他平台/真实应用门禁分别取证。
M7-07 的在线增强与历史失败记录保留，M7-11 本地语义/workflow 仍仅 Proposed。

后续[GTK 原生控件增量](gtk-pointer-effects-20261010.md)另取实际滚动位置/分隔条
位移，补原生列表 XDND 安全拒绝限制，并为 X11 运动加入 8 ms 平台交付间隔。
其结果不追溯改写本文件首轮机械输入证据，也不关闭原生列表/目标平台门禁。
