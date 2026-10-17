# 统一完整鼠标动作验收

> 日期：2026-10-18
> 状态：Verified（Linux X11 与 Portal 注入边界）；当前产品启动被桌面托盘环境阻塞
> 负责人：Mirage 维护者
> 工作项：M7-21 / DEC-100；基线 `f699c49`，分支 `fix/desktop-click-sequence`

## 交付与证据

编号、图片矩形、共享帧三种目标路径均提供 click/scroll/drag：click 和 drag 使用
可选 button=left/middle/right（默认 left）；click_count 沿用 1..32；双轴轮步均为
正数右/下、负数左/上，默认 0，绝对值总量 1..16。区域新增 scroll_region、drag_region，
共享新增 drag。完整拖动为 16 段线性路径、最多 2048 源桌面像素、一秒协作期限。

X11 在同一前台窗口内逐步检查焦点、几何、遮挡、物理按钮和修饰键；清理和最终物理
按钮检查改为选定按钮的掩码。Portal 使用原 stream-local 坐标映射及释放账本；拖动
同样检查取消/期限/会话停止，在失败按下或中途异常后仍释放。没有引入新并发或更改依赖。
三条路径共享动作后验证；输入完成与业务完成继续分开。

| 验证 | 实际结果 |
| --- | --- |
| native-release 产品和九个实际测试目标 | 构建成功；九项均通过（分次执行，见下述失败/复验历史） |
| ASAN / UBSAN 八个实际测试目标 | 全部通过（分次执行），无 sanitizer 报告 |
| X11 实际点击 | left/middle/right 各收到三组正确按钮的按下/释放 |
| X11 实际拖动 | 三种按钮均到达终点且存在对应 held-mask motion；各自按下后取消仍释放正确按钮，取消回执且无遗留 |
| Portal 边界 | 三种按钮 click/drag 顺序与坐标比例；拖动中失败/异常/取消/超时/会话停止、释放失败与提前取消；越界/超预算无输入 |
| 工具集成 | 编号/区域/共享的按钮、次数/轮步/终点透传，非法类型/按钮/步骤/目标、权限/新帧变化/释放失败、后条件与收尾门禁 |
| GTK3 私有 Xvfb | 105 checks、0 failures；实际双轴滚动、分隔条、原生列表拖放、禁止效果、中断/焦点/遮挡及目标变化拒绝 |
| 静态 | 全仓 C++ 格式、变更私有头格式、公共头边界（61 个头、0 新违规）、Python 语法及 git diff --check 通过 |

Release 实际测试：provider_contract_test、x11_pointer_actions_test、x11_backend_test、
harness_desktop_tools_test、desktop_image_supply_test、post_action_verification_test、
visual_marks_test、gui_fallback_test、portal_input_test。ASAN/UBSAN 同组去掉 harness。
GTK fixture 为系统 Python/GTK 外部进程，不宣称覆盖 GTK 内部 sanitizer。

本轮全组首跑 Release 43.56 秒、ASAN 56.19 秒、UBSAN 58.55 秒，只有 Portal fixture
未清理前一轮 wheel 事件导致三条旧 key 断言失败。修正 fixture 后只重跑 Portal，
三配置均 1/1，通过，分别 0.07/0.10/0.09 秒；未改动产品代码。
随后增加三种按钮实际按下后取消的针对性断言，只重编译/重跑 X11 pointer test，
三配置均 3/3（含隔离 setup/cleanup），分别 3.26/4.02/3.41 秒。
首轮编译还因新测试参数遮蔽成员触发 -Werror=shadow，重命名测试参数后构建成功。

GTK 原负例只把目标外列表文字改红，却期待 splitter 拖动因 stale_observation 拒绝，
与 DEC-083 的目标区域稳定策略不符。将 repaint fixture 改为改变实际 paned/分隔条/目标
走廊的背景，保持布局与位置不变；新负例证明没有输入派发，最终 105/105，通过。
此修复仅改变受控应用负例，不弱化产品守卫。

日志前缀 `/tmp/mirage-unified-pointer-`：
- `{native-release,asan,ubsan}-verified-build.log` 和 `*-tests.log` 保存构建/全组首跑。
- `*-portal-fixture-{build,tests}.log` 保存 Portal fixture 修复及定向复验。
- `*-cancel-{build,tests}.log` 保存选定按钮取消清理的最终定向检查。
- `gtk-corrected.log`、`format.log`、`boundaries.log` 保存 GTK 与静态结果。

## 产品运行限制与补验

检查启动前未发现运行的 Mirage tray/native/pet，默认 IPC socket 不存在。
本轮 native-release 产品已重建；标准入口启动失败，直接 tray 诊断确认
`no notification area / StatusNotifierWatcher; frontend will not open`。
DBus NameHasOwner(org.kde.StatusNotifierWatcher)=false，未绕过产品宿主规则。
持久化数据与启动前完整相同，8 个会话、19 轮记录；诊断见
`/tmp/mirage-unified-pointer-tray-diagnostic.log` 与
`/tmp/mirage-unified-pointer-live-validation.json`。

真实供应商模型任务未重跑，当前产品实例未启动成功；Mirage 维护者恢复桌面托盘宿主后
使用已登记 native-release 入口启动，复验模型选择右键、滚动及拖放并读取新后条件。
Portal 仅边界 fixture 通过，真实 Wayland compositor 手势效果由维护者在授权测试窗口
补验。Windows 完整动作 opt-in 仍未交付；不从原始 Win32 输入能力推导模型工具已可用。
X11 跨窗口/任意轨迹/持续持键不属于本次线性完整手势契约。本轮未新增共享状态或
生命周期路径，未运行 TSAN。未执行范围保持开放，不标记全平台完成。
