# M7-09 GTK 原生控件效果验收

> 状态：Verified（滚动/分隔条效果与负例；原生列表 XDND 尚未交付）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-09](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)

## 范围与版本

Linux x86_64 / GCC 13.3 / 私有 Xvfb 800×600 / 系统 GTK 3.24.41 / 系统
Python GI。当前分支 feat/m7-conversation-desktop-tools、基线 HEAD c786df7；
增量未提交/推送。Mira c75378b、Mirador ae8cf87、传递 kairo ef821dc 均保持。
GTK 仅是自有外部验收应用，未成为产品链接依赖，未下载软件/模型、未操作用户桌面。
首增量机械事件证据保留在[原记录](bounded-scroll-and-drag-20261010.md)。

`gtk_pointer_fixture.py` 使用真实 GtkScrolledWindow/GtkTextView、GtkPaned 和
GtkTreeView，效果由控件原生 handler 产生；读取 adjustment、分隔位置和列表
顺序作为独立 oracle。标题来自原生状态变更信号，不在收到 release 时假定成功。
候选框由控件分配几何构造的 FakeOcrBackend 提供，实际截图、Mirador fusion/
SoM、视觉 registry、Mira 规范 BuiltinToolRegistry、scroll/drag handler 和
DesktopActionVerification 都是真实路径。工具前固定局部标题条件，派发后另读
原生控件状态，再只读 verify/终态 settle，并检查旧 epoch 请求被拒绝。
这是应用输入与工具/核验组合验收，不是 OCR 定位、在线模型选号或一般拖放证明。

driver 的有限任务通过 Executor submit_auto/future.get 管理；现有视觉 worker
承担分析。GTK 进程使用自己的原生 GUI loop，测试状态命令/回执分别限制 4 KiB、
5 秒，正常效果最多 32 次/2 秒补读。关闭先停视觉生产者，再回收自有 GTK/Xvfb
进程，最终由外部 owner shutdown。测试应用关闭动画、在 after-paint/显示同步
后发布 ready，避免初始布局尚未绘制导致产品逐像素门禁合理拒绝。

## 应用场景与行为

| 场景 | 原生状态 / 预期 |
| --- | --- |
| 双轴滚动 | 2 个向右、3 个向下离散步改变水平/垂直 adjustment；新观察验证 Scrolled |
| 滚动边界 | 两轴已到底，再滚动不移动；派发可成功，readonly/settle 必须未验证 |
| 分隔条完整拖拽 | 当前位置 130，目标右移 60；实际位置到 190，验证 Resized |
| 分隔条禁用 | 输入完整释放，但位置不变；后条件与 settle 必须失败 |
| 按下后取消 | 在首个 Button1 按下后取消、未运动；释放确认、位置不变、终态失败 |
| 原生列表 XDND | GTK 生成拖拽图层触发 target_obscured；释放且列表不变、终态失败，兼容性仍开放 |

每个场景均检查 Button1 最终未按住、ActionCoordinator 已释放，旧编号不能重发。
负例通过表示门禁按预期生效，不表示列表拖放功能已交付。

## 发现与修正

首轮原生列表正例未完成：零间隔的 16 个 motion 在 GTK 被合并为一次，实际
drag-begin=1、drop=0、failed=1、data=0，列表未变化，后端却报告派发成功。
这是应用效果验收失败，不能用首增量的机械输入通过覆盖。

X11 完整拖拽追加 XTest 原生 8 ms motion delivery delay，清理 release 仍零
延迟，步前/后取消、预算、窗口/指针归属检查保持。它属于同一次 Executor
有限调用任务中的平台输入参数，没有新增私有 timer、sleep、任务或调度器。
参见 [X.Org XTEST 语义](https://www.x.org/releases/X11R7.5/doc/Xext/xtestlib.html)及
DEC-064 的追加说明；16 段、2048 像素、1 秒与五秒引用限制均不增加。

带间隔后，GTK 原生拖拽图层触发指针归属守卫，结果 target_obscured 并释放。
尝试过 XShape input-region 命中解析，实际仍未通过；已撤销该尝试，没有保留
额外形状逻辑/编译宏，也没有按类型、标题或 PID 白名单绕过归属。列表 XDND
作为安全拒绝负例保留；正例范围改为原生分隔条实际位移，两者不相互替代。
后续须先冻结能识别输入接收者/拖放目标的安全契约，再补原生列表正例。

组合测试初期也发现 fixture 初始调整滚动位置后尚未绘制，触发 stale_observation；
已通过 GTK 原生 after-paint/sync 准备握手修正测试，产品像素守卫未放宽。
编译时修正候选浮点坐标与 verification 注册/装饰接口选型；不是依赖缺口。

## 实际结果

| 配置 / 门禁 | 实际结果 | 范围 |
| --- | --- | --- |
| native-debug 全量构建 / CTest | 62/62，0 failure，40.97 s | 显式启用 GTK 验收，含原生 UI/service 与既有回归 |
| native-release 全量构建 / CTest | 62/62，0 failure，35.35 s | 同一契约与工作树 |
| ASAN+UBSAN 定向 | 9/9，0 failure，36.36 s | 泄漏检查开启，含 GTK driver、完整输入、引用、验证与 Runtime |
| TSAN 定向 | 9/9，0 failure，42.07 s | 同九项；测试进程关闭 ASLR；无 race 报告 |
| GTK 原生控件组合 | 四配置分别 6 场景 / 469 checks，0 failure | 正例与负例分开核对，无原生列表正例声明 |
| 本轮 C++ 格式 / Python 编译 | 2 个 C++ 文件、1 个 Python 文件通过 | 仅本轮增量；无无关格式化 |
| 公共头 / diff | 53 headers、0 violation；diff check 通过 | 未泄漏平台或 pinned 类型 |

本轮机器可读结果及原生状态见
[验证摘要](data/gtk-pointer-effects-20261010.json)。当前机器原始日志为
`/tmp/mirage-m7-gtk-*.log`，各配置成功断言和 GTK 状态保留在相应 build 目录
`Testing/Temporary/LastTest.log`。首轮列表失败、节拍后拒绝与撤销的形状尝试分别
保留原日志，不能将最终六个场景通过解释成列表拖放正例通过。全仓格式检查的
既有 apps/native/app.cpp 问题沿首增量记录保持，本轮没有重做该无关格式化。

## 复现

GTK 验收默认不注册 CTest（MIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=OFF）。显式
开启时要求系统 Python GI/GTK3；缺少时 configure 明确失败，不 skip/自动安装。
可用 MIRAGE_GTK_TEST_PYTHON 指定合适的系统 Python。

```bash
cmake --preset native-debug -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON
cmake --build --preset native-debug -j2
ctest --test-dir build/native-debug -j2 --output-on-failure
cmake --preset native-release -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON
cmake --build --preset native-release -j2
ctest --test-dir build/native-release -j2 --output-on-failure
```

sanitizer 复用已有联合 ASAN/UBSAN、TSAN 配置；GTK/Python 系统进程本身没有
被 sanitizer instrumentation，不宣称检查了 GTK 内部竞态/内存。

```bash
cmake -S . -B build/ocr-asan -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON
cmake --build build/ocr-asan -j2 --target gtk_pointer_effects_test x11_pointer_actions_test x11_visual_marks_test visual_marks_test post_action_verification_test action_contract_test harness_desktop_tools_test runtime_service_test x11_backend_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -R '^(gtk_pointer_effects_test|x11_pointer_actions_test|x11_visual_marks_test|visual_marks_test|post_action_verification_test|action_contract_test|harness_desktop_tools_test|runtime_service_test|x11_backend_test)$' --output-on-failure
cmake -S . -B build/ocr-tsan -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON
cmake --build build/ocr-tsan -j2 --target gtk_pointer_effects_test x11_pointer_actions_test x11_visual_marks_test visual_marks_test post_action_verification_test action_contract_test harness_desktop_tools_test runtime_service_test x11_backend_test
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -R '^(gtk_pointer_effects_test|x11_pointer_actions_test|x11_visual_marks_test|visual_marks_test|post_action_verification_test|action_contract_test|harness_desktop_tools_test|runtime_service_test|x11_backend_test)$' --output-on-failure
```

## 未完成项与补跑条件

M7-09 保持 In Progress。原生列表 XDND 由维护者继续确认安全的指针/接收者
契约，完成后在同一应用补正例与外来遮挡/取消测试；本轮拒绝不是 mira/mirador
能力缺口。用户 Xorg 真实宿主应用需操作者取焦后另验，本轮不弹窗、不要求
操作者动作。Windows/原生 Wayland、在线模型、OCR 真实定位/文本后条件、
任意应用/跨屏/DPI、完整权重 TSAN 期限按原计划补验，不因本次控件通过关闭。

第三增量已新增可选 XRes 的同连接辅助图层解析，并通过列表正/负效果与中途
收敛测试，见[后续验收](x11-native-dnd-20261010.md)。本文件的六场景、失败
记录与当时未交付结论保持，不追溯重写为列表正例通过。
