# M7-09 Xorg 宿主 GTK 输入验收

> 状态：Verified（四类场景分别取证；间歇拒绝原因与 M7-09 总范围仍开放）
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：[M7-09](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)、[DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md)

## 环境与方法

真实用户 Xorg :0、eDP-1 3200×2000、GTK 3.24.41、GDK 整数 scale=2；测试
窗口实际物理大小 1248×720。分支 feat/m7-conversation-desktop-tools，基线
c786df7；本增量未提交/推送、未升级 pinned 依赖或修改锁文件。用户逐窗口
点击专用开始区域并松开。输入前确认物理按钮未按住、前台恰为该自有窗口，
不调用宿主 XSetInputFocus；只通过 ScreenProvider root capture 取得自身窗口
ROI，没有采集/保存无关桌面画面、读取模型凭据或请求在线推理。

外部 Python 编排等待 GTK 原生松键回执，再把显式窗口 ID 和状态管道传给有限
C++ driver。driver 复制自身管道句柄；外部编排保有 GTK 进程和原句柄，失败
即停止、关闭自有测试窗口。人工等待最多 120 秒，driver 最多 45 秒，状态包
4 KiB/5 秒，现有 owner 30 秒、引用 5 秒、动作 1 秒与零延迟 release 保持。
动作通过既有 Executor submit_auto/future.get、视觉 worker、动作 lease 完成，
外部 owner 最后 shutdown；没有新增 Mirage worker、调度器或业务队列。

原生 GTK 分配坐标按窗口 scale_factor 转物理像素。依据
[GTK Widget scale_factor](https://docs.gtk.org/gtk3/method.Widget.get_scale_factor.html)，
另注册私有 2× 回归及专用人工松键/继承管道交接回归；不推广到任意 DPI/跨屏。
候选仍由原生分配构造 FakeOcrBackend，Screen/ROI、Mirador fusion/SoM、临时
引用、Mira BuiltinToolRegistry、产品 scroll/drag 和只读验证是真实路径。
这证明宿主原生控件输入与工具/核验组合；不声明 OCR 定位、在线 Agent 选号或
Mirage UI 提交任务的完整端到端验收。

## 宿主结果

四类场景分别取得通过证据，来源为 round2 三项与 cancel4 一项；不是同批连续
4/4。每次输入均在新准备的窗口、新观察与人工松键之后开始，没有失败动作自动
重发。原生状态、派发/只读验证、物理 release 与 owner release 独立记录。

| 场景 / 回合 | 结果与独立状态 |
| --- | --- |
| 双轴滚动 / round2 | 11 checks、0 failure；水平 89.628、垂直 144.993（GTK 逻辑 adjustment），新标题后条件 verified=true |
| 原生列表重排 / round2 | 12 checks、0 failure；Alpha,Bravo,Charlie,Delta,Echo → Bravo,Charlie,Alpha,Delta,Echo；begin/drop/end 各 1，failed=0，verified=true |
| 禁止重排 / round2 | 11 checks、0 failure；输入派发完成但列表不变，readonly verify 与 settle=false；负例通过不等于业务成功 |
| 首段拖动后取消 / cancel4 | 12 checks、0 failure；GTK begin=1、end=1、drop=0、failed=1；回执 cancelled，物理左键与 owner 均已释放，settle=false，列表不变 |

取消补证在向下移动至少 8 个逻辑像素后请求取消，独立要求 begin=1；私有原生
取消/焦点/遮挡场景仍使用原 50 像素触发点，人工握手回归覆盖 8 像素路径。
不把测试中列表未变化推广为副作用回滚保证。全部宿主测试窗口已回收，操作者
无需继续配合。原始回合摘要及每个二进制/fixture/log 的 SHA256 见
[机器可读证据](data/user-host-gtk-pointer-20261011.json)。

## 失败与诊断历史

round1：滚动通过，列表正例 stale_observation、未输入、begin=0。只用 GTK
after-paint/sync 未能证明宿主像素稳定；外部编排追加松键后的 0.5 秒绘制等待，
再开始首次观察。没有放宽像素守卫或延长引用。其后列表正例通过，但这一等待
不能保证所有宿主绘制已稳定。

round2：前三项通过，50 像素取消场景在首段运动后被 target_obscured 拦截，
尚未触发取消；begin/drop/end 各 1，顺序不变且释放完成。此失败不能算取消
通过，也不能因 cleanup drop 信号认定任务完成。cancel3：改用首段 8 像素取消
后仍在输入前 stale_observation，begin=0。cancel4：增加仅记录自身 ROI 变化
像素数/包围框的失败诊断后，通过取消补证，诊断分支未触发。

间歇像素变化及较晚取消前的遮挡拒绝尚未隔离根因，不能宣称已修复或稳定兼容。
测试保持原拒绝语义，没有放宽 XRes 归属、焦点、权限或预算。早期 GTK_ACTION
日志的 verified 字段误输出期望值；round1 失败项的该字段不能作为成功证据。
已改成实际 settle 返回值，所有回合 passed 均同时要求 exit=0 和零失败断言。

ASAN/UBSAN 首轮 10/11：旧 harness expired 测试把 20 ms 期限设在 admission
之前，负载下模型请求尚未开始就超时，后续 front() 访问空请求列表触发 UBSAN。
修正测试在有限任务开始时设 1 秒期限，授权点有界等到期限，并明确要求已进入
input.inject 门禁；空列表先报告断言失败并正常 teardown。产品期限不变，失败
日志保留于 /tmp/mirage-gtk-host-ocr-asan-test.log，最终结果另录，不覆盖失败。

TSAN 多配置并行回归 10/11：2× GTK 在 native-dnd 标注阶段返回 cancelled，
保持现有五秒观察预算；没有 TSAN race 报告。隔离其他配置构建/测试后串行
补跑同 11 项，不增加产品或测试路径期限。失败日志保留于
/tmp/mirage-gtk-host-ocr-tsan-final-test.log，不把负载下超时解释成性能通过。

## 回归与复现

最终四配置结果同步到上述 JSON 与里程碑计划：

| 配置 / 检查 | 最终结果 |
| --- | --- |
| native-debug 全量构建 / CTest | 64/64，0 failure，48.19 s |
| native-release 全量构建 / CTest | 64/64，0 failure，44.15 s |
| ASAN+UBSAN 定向 / detect_leaks / halt_on_error | 11/11，0 failure，37.12 s |
| TSAN 定向 / halt_on_error / setarch -R / 串行 | 11/11，0 failure，69.40 s |
| GTK 默认 / 2× / 人工交接 driver | 四配置各 102 / 102 / 71 checks，0 failure |
| X11 完整输入机械测试 | 四配置各 474 checks，0 failure |
| 增量格式 / Python 编译 / 公共头 / diff | 2 个 C++、2 个 Python、53 headers / 0 violation；diff check 通过 |

定向 11 项为 gtk_pointer_effects_test、gtk_pointer_scaled_test、
gtk_pointer_handshake_test、x11_pointer_actions_test、x11_visual_marks_test、
visual_marks_test、post_action_verification_test、action_contract_test、
harness_desktop_tools_test、runtime_service_test、x11_backend_test。原日志为
/tmp/mirage-gtk-host-*-final-test.log，TSAN 最终串行日志为
/tmp/mirage-gtk-host-ocr-tsan-serial-test.log；断言保存在各 build 的
Testing/Temporary/LastTest.log。全仓未改动 apps/native/app.cpp 的既有格式违例
沿前轮记录保持，没有混入无关格式化。

私有默认/2×
各 11 场景、人工握手 4 场景，与宿主结果分开；GTK 系统进程不带 sanitizer
instrumentation，不声明 GTK 内部内存/竞态或完整 OCR 权重 TSAN 已通过。
历史第三增量的 XRes 编译关闭/运行期缺扩展、机械输入和安全负例继续保留于
[原生拖放记录](x11-native-dnd-20261010.md)。

```bash
cmake --preset native-debug -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON -DMIRAGE_ENABLE_X11_DRAG_HELPERS=ON -DX11_XRes_INCLUDE_PATH=/tmp/mirage-xres-sdk/root/usr/include -DX11_XRes_LIB=/lib/x86_64-linux-gnu/libXRes.so.1
cmake --build --preset native-debug -j2
ctest --test-dir build/native-debug -R '^gtk_pointer_(effects|scaled|handshake)_test$' --output-on-failure
# 当前真实 Xorg 上逐窗口人工点击开始，失败即停止。
/usr/bin/python3 tests/manual/gtk_pointer_host_acceptance.py --binary build/native-debug/tests/gtk_pointer_effects_test --output /tmp/mirage-gtk-host-new-run
# 单独补证须使用新目录；不覆盖失败历史。
/usr/bin/python3 tests/manual/gtk_pointer_host_acceptance.py --binary build/native-debug/tests/gtk_pointer_effects_test --sample native-cancel --output /tmp/mirage-gtk-host-new-cancel
```

M7-09 保持 In Progress。间歇拒绝、任意应用/复杂辅助图层、Windows、原生
Wayland、任意 DPI/跨屏、OCR 定位与在线 Agent 后条件均未由此关闭。负责人
Mirage 维护者；宿主剩余定位需在同 Xorg/GTK 环境保留新准备窗口、人工松键与
ROI 变化诊断，禁止自动重试不确定输入。M7-07 模型耗时及完整权重 TSAN 门禁
继续独立挂账。

后续第五增量新增有界分类/像素变化诊断与 2× 人工交接回归，单独取得一次
较晚取消宿主证据；见[诊断验收](x11-pointer-diagnostics-20261011.md)。本页
历史回合与失败计数保持；未将单次诊断通过解释为间歇原因已修复。
