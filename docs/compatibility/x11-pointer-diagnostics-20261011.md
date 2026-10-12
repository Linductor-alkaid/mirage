# M7-09 X11 拒绝诊断与较晚取消补证

> 状态：Verified（有界诊断、私有回归与一次宿主较晚取消；间歇原因仍开放）
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：[M7-09](../plans/m7-visual-tools-and-input.md)
> 契约：[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)、[DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md)
> 前轮：[宿主 GTK 验收](user-host-gtk-pointer-20261011.md)

## 实现边界

新增能力全部位于测试边界，没有修改产品后端、公开工具契约、pinned 依赖或
锁文件，没有提交/推送。仍使用原 Executor finite driver、future 与外部 owner
shutdown；GTK/Python 仅为外部测试应用和编排。诊断不成为新的任务监控系统，
也不参与产品权限/归属决定，不触发输入重试。

`X11PointerDiagnostics` 按下前用现有系统 XRes API 取得源连接命名空间并保存
可见根窗口；最多 256 个连接、128 根子窗口。最多保留四次采样：按下、首次
运动、向下达到 50 逻辑像素、动作返回后。仅输出根窗口数、左键状态，以及
逻辑指针/首个矩形命中是否为目标、同连接、新可见、override-redirect 和窗口
类别。内部 XID 只用于查询/比较，不写出无关窗口 ID、标题、PID 或画面。第五
次采样只设置 truncated=true，不再查询或扩充存储。

这些采样不是原子快照，额外 Xlib 查询会改变测试时序。根分支是 WM frame 时
top_source=false 不能直接解释成外来遮挡；同连接图标也不能据此推定真正 grab
接收者或业务成功。产品仍独立运行 DEC-065 的下方目标/焦点/预算检查。机械
负例核对图标上方与下方外来窗口、已可见窗口、InputOnly、普通窗口、容量等
分类；下方外来遮挡的最上层仍是同连接图标，产品继续安全拒绝，证明只看该
分类不足以放行输入。

私有新增 pixel-change 场景：源截图标注后，GTK 原生 handler 只把列表文字
改成红色，并经 after-paint/sync 回执；产品新截图重验必须拒绝。变化只在
自有窗口 ROI 留像素数/包围框，独立要求 stale_observation、无输入副作用、
分隔位置不变、按钮/owner 释放和 settle=false。默认及整数 2× 回归均执行，
不把这个已知变化负例作为前轮间歇变化的根因证据。

新增 `gtk_pointer_scaled_handshake_test`：私有 Xvfb 1600×1200、GDK_SCALE=2，
人工握手由私有 XTest 驱动，四场景实际管道交接/诊断通过；其中取消达到
50 逻辑像素。原 1× 人工握手仍使用 8 像素，默认/2× 十一个历史控件场景
保持原 50 像素故障注入。宿主脚本新增 --diagnostics 和 --late-cancel，后者
只允许配合 --sample native-cancel，不覆盖历史目录，不继承其他运行的测试
控制环境变量；宿主仍必须取得实际人工松键，失败即停。

## 实际宿主补证

维护者再次在自有测试窗口点击并松开；真实 Xorg :0、eDP-1 3200×2000、GTK
3.24.41、scale=2、窗口 1248×720。此次 native-cancel 较晚取消 14 checks、
0 failure；按钮与 owner 释放，回执 cancelled、settle=false，顺序保持不变。
GTK begin/drop/end 各 1、failed=0：清理 release 可触发 drop，不能解释为业务
完成或回滚。诊断显示根窗口 95→98，较晚运动命中一个新出现的同连接
InputOutput override-redirect 图标。取证窗口已关闭，无需继续操作。

这一批只有一项通过；没有复现上一轮 target_obscured 或 stale_observation。
由于诊断本身增加查询，不能据此宣布兼容性已修复、间歇原因已找到或重复
稳定性已通过。失败历史继续保留在前轮报告。精确应用状态、采样和每个
artifact 的摘要见[机器可读证据](data/x11-pointer-diagnostics-20261011.json)。

## 验证与失败历史

| 配置 / 检查 | 实际结果 |
| --- | --- |
| Debug 全量构建 / CTest | 65/65，0 failure，47.24 s |
| Release 全量构建 / CTest | 65/65，0 failure，40.53 s |
| ASAN+UBSAN 定向 / detect_leaks / halt_on_error / 串行 | 12/12，0 failure，74.36 s |
| TSAN 定向 / halt_on_error / setarch -R / 串行 | 12/12，0 failure，84.01 s |
| GTK 默认 / 2× / 1× 握手 / 2× 握手 | 四配置各 112 / 112 / 79 / 79 checks，0 failure |
| X11 输入与诊断分类 | 四配置各 549 checks，0 failure |
| Release 编译关闭 XRes | 两项 2/2；GTK 105 / X11 475 checks，释放且严格回退 |
| Debug 私有服务端禁用 X-Resource | GTK 105 checks，0 failure；断言实际扩展不存在 |
| 恢复 Release ON 后的变更测试 | 五项 5/5，0 failure |
| 格式 / Python / 公共头 / diff | 3 个 C++、2 个 Python、53 headers / 0 violation；diff check 通过 |

定向 12 项沿前轮 11 项加 gtk_pointer_scaled_handshake_test。原日志为
/tmp/mirage-diag-*-test.log，详细断言在各 build 的 Testing/Temporary/LastTest.log，
关闭 XRes 的断言另存 /tmp/mirage-diag-nores-detail.log。完整机械回归中的新增
75 checks 核对分类、容量/截断、释放；关闭能力时只检查有事实依据的未知分类
和容量，不以 unknown 冒充连接归属已确认。全仓未改动的 apps/native/app.cpp
既有格式违例保持，本轮没有混入无关格式化。

sanitizer 只检查 Mirage driver/产品组合，不检查系统 GTK 内部；不关闭完整
OCR 权重 TSAN 门禁。所有测试继续原五秒观察/引用、1 秒动作与 30 秒 owner
预算，不能用测试耗时推定性能保证。

首次机械诊断断言误认为图标下方外来窗口也应是最上层命中，549 checks、
1 failure；实际最上层仍是同连接图标。已修正测试 oracle，产品的下方遮挡
拒绝一直保持，未改守卫。原失败日志 /tmp/mirage-gtk-diag-private2.log 保留，
最终结果另录。私有已知像素变化初验为 1× 808 像素、bounds=16,82,57,241；
2× 2310 像素、bounds=33,164,115,483。字体/主题可改变数量，测试只要求
确有同 ROI 像素变化，不固定这些计数作为产品契约。

## 复现与剩余工作

沿用前轮 opt-in GTK 与临时 XRes SDK 配置，不安装系统库或下载权重。

```bash
cmake --preset native-debug -DMIRAGE_ENABLE_GTK_POINTER_ACCEPTANCE=ON -DMIRAGE_ENABLE_X11_DRAG_HELPERS=ON -DX11_XRes_INCLUDE_PATH=/tmp/mirage-xres-sdk/root/usr/include -DX11_XRes_LIB=/lib/x86_64-linux-gnu/libXRes.so.1
cmake --build --preset native-debug -j2
ctest --test-dir build/native-debug -R '^(gtk_pointer_.*test|x11_pointer_actions_test)$' --output-on-failure
# 实际用户 Xorg；仅在人工点击并松开后进入有限输入 driver。
/usr/bin/python3 tests/manual/gtk_pointer_host_acceptance.py --binary build/native-debug/tests/gtk_pointer_effects_test --sample native-cancel --late-cancel --diagnostics --output /tmp/mirage-gtk-new-diagnostic
```

M7-09 保持 In Progress。后续在新准备的宿主窗口取源截图与失败变化区域，以及
失败前的有界命中分类；按证据区分绘制变化、辅助图层时序和真实外来遮挡，再
决定产品修正。负责人 Mirage 维护者，条件为实际 Xorg/GTK 可操作，人工取焦，
失败不重发。本轮只提供诊断和单例补证，Windows/原生 Wayland、任意应用/
DPI/跨屏、OCR 定位/在线模型与既有完整权重 TSAN 门禁仍另验。
