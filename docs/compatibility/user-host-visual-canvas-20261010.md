# M7-07 用户宿主视觉取证（供给范围化与验收入口）

> 状态：Verified（受控 Xorg 离线 3/3 与门禁 2/2；在线仅 1/2，M7-07 整体 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-062](../decisions/DEC-062-visual-capture-scope.md)、[DEC-058](../decisions/DEC-058-scoped-visual-marks.md)、[DEC-055](../decisions/DEC-055-desktop-image-supply.md)

本记录承接[在线验收](online-visual-canvas-20261010.md)的剩余门禁"用户宿主取证"。
维护者已切换真实 Xorg 会话，XDG_SESSION_TYPE=x11、DISPLAY=:0，受控公共
Canvas 的 root ROI 采集、点击和新观察已补证。此前 XWayland 失败记录继续保留；
原生 Wayland 授权/撤销属 M7-10，不在本记录范围。

## 范围机制

按 [DEC-062](../decisions/DEC-062-visual-capture-scope.md)，harness 将
`DesktopToolContext::capture_scope` 固定为公共 Canvas 窗口 id：mark 只供给窗口
范围的编号图（无 crop 时 ROI=窗口而非整屏），click 只接受 scope 窗口，OCR 文本
过滤为窗口内 regions。用户桌面的其他像素与文本不进入模型；上传前裁剪在工具边界
完成。X11 抓取不感知遮挡：runbook 要求 Canvas 置顶无遮挡，harness 在枚举到与
Canvas 几何相交的其他可见窗口时直接拒绝执行（遮挡守卫），mark 与 click 之间的
像素一致性重采集保证期间出现的遮挡被拒绝。

## Runbook（正式宿主取证）

1. 进入真实 Xorg 会话（GDM 登录界面选 "Ubuntu on Xorg"），清理工作区，记录
   合成器/WM、DPI/缩放、显示器布局。
2. 准备样本与资源同[在线验收](online-visual-canvas-20261010.md)（OCR 权重清单、
   字体、MiniMax provider 配置；凭据只走子进程环境与 SecretRef）。
3. 在新会话的终端确认 `echo "$XDG_SESSION_TYPE"` 输出 `x11`，运行人工离线入口
   （不联网、不读凭据，自动生成公共绘图与 geometry/ROI 清单，失败首例即停止）：

```bash
cd /home/linductor/mirage
python3 tests/manual/visual_host_acceptance.py \
  --binary build/ocr-debug/tests/x11_visual_marks_test \
  --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json \
  --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf \
  --output /tmp/mirage-m7-host-xorg-20261010
```

   每例窗口固定 480×180，不要调整大小；应看到两块黑色区域。15 秒内在两块之间
   的中央空白处单击并松开，随后保持鼠标、键盘和窗口不动，窗口关闭后继续下一例。
   三例为 right / left（请求原点不同，使用 WM 实际摆放）/ unchanged。入口默认
   要求 Xorg；当前 Wayland 会话在任何窗口/采集前拒绝，回执保存 preflight_error。
   `--allow-xwayland` 仅用于明确的诊断，不能把它当成 Xorg/原生 Wayland 验收。

4. 在线三例（含 unchanged 负例；真实 MiniMax，只上传公共 Canvas 局部图）：

```bash
cmake --build --preset ocr-debug -j3 --target x11_visual_marks_test
MIRAGE_CANVAS_HOST="$DISPLAY" MIRAGE_CANVAS_OPERATOR=1 MIRAGE_CANVAS_ROI=1 python3 tests/manual/visual_online_acceptance.py --provider /home/linductor/mira/docs/model_provider/minimax --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-m7-host-xorg-20261010/host-manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-host-online-<date>
```

   人工模式逐例提示，首个失败立即停止，不继续弹窗或重试；报告 attempted 与
   total 分开。父进程超时也写入回执，不能当成子进程执行成功。
5. 门禁负例：MIRAGE_CANVAS_NEGATIVE=permission 或 focus，搭配
   MIRAGE_CANVAS_HOST="$DISPLAY"、MIRAGE_CANVAS_LIVE=0 和相同 binary/manifest/
   before/after 参数运行。无需人工点击：前者注入 screen.capture gate 拒绝，
   后者由第二个自有公共窗口取得焦点，mark 均应在分析回调前拒绝且无图片/鼠标
   事件。这验证真实 Xorg Provider 和工具门禁，不替代原生产品权限提示 UI 验收。
6. 纪律：全程人工在旁；绝不向 Canvas 边界外注入输入；90 秒父进程护栏与既有
   期限不放宽；失败样本保留为证据，不自动重试。
7. 证据归档：本记录追加结果表，`data/` 新增回执 JSON（含 display/origin/
   capture_scope/window_id/a11y 字段），同步 M7 计划与总计划门禁状态。

## 已知宿主行为（2026-10-10 XWayland 探针）

GNOME/mutter 在 Wayland surface 持焦时拒绝 X11 侧取焦：EWMH 激活（source 0/1/2、
真实时间戳、`_NET_WM_USER_TIME`）全部被拒，属平台行为限制而非 pinned 依赖缺口。
harness 相应处理：守卫与点击目标使用合成器实际摆放几何（探针实测请求 (20,20) 被
摆到 (0,138)）；EWMH 激活有界轮询；仍被拒时经 provider 在守卫验证无遮挡的 Canvas
中心注入**一次**焦点辅助点击（click-to-focus），事件排空后脚本记账不变；焦点仍
不可得则 fail-closed。若不想触发辅助点击，可在弹窗后手动点击 Canvas 一次再跑。
XWayland 预跑首轮因该防护 fail-closed（无任何越界像素或点击），其中位置样本由
遮挡守卫拒绝，构成守卫在真实桌面的有效证据。

## 环境与预跑证据

Ubuntu 24.04 / GCC 13.3.0；当前会话 XDG_SESSION_TYPE=wayland（DISPLAY=:0 为
XWayland）。基于 c786df7 / feat/m7-conversation-desktop-tools 在途工作树，未提交。
范围语义由假环境单测覆盖：visual_marks_test 331 checks、ppocr_pipeline_test
113 checks，0 failure——含未聚焦 scope 拒绝、越界 crop 拒绝、无 crop 按窗口几何
供给（负原点显示器）、click scope 不匹配拒绝、OCR 窗口外文本过滤与 window_id
输出；并做突变敏感性验证（临时还原错误 ROI 语义时新断言确实失败，产品文件已
逐字节还原复核）。

最终二进制本地回归：ocr-debug 59/59、native-debug 60/60、native-release 60/60，
0 failure；ASAN/UBSAN 定向 7/7、TSAN 定向 7/7（detect_leaks=1、halt_on_error=1、
TSAN 经 `setarch -R`）。离线 Canvas 驱动 text 5/5、geometry 3/3、template 4/4，
unchanged 负例均 unverified / condition_not_matched；遮挡守卫负例 exit 1，消息
符合契约。最终二进制（焦点宽限放宽后）复跑 ocr-debug 59/59 与三驱动同上。

XWayland 真机预跑（证据 /tmp/mirage-m7-scope-host-20261010/）：cross-right /
cross-left / POS=60,60 / unchanged 四例的遮挡守卫全部按合成器实际摆放几何通过；
EWMH 激活与焦点辅助点击都被 mutter 拒绝移交焦点，harness 在严格 front 门禁
fail-closed——无像素供给、无脚本点击派发。宿主焦点宽限已放宽到 15 秒：弹窗后由
操作者物理点击 Canvas 一次即可继续（mutter 只认真实点击），正式宿主在线取证按
runbook 在该配合下补跑。测试侧两轮修复（raw-X 实际摆放测量、栈序感知守卫排除
WM 自身 frame 链）只动验收 harness；产品代码在范围语义之外零变化。


## 第八增量：人工配合与绘制修复（2026-10-10）

本轮操作者能够操作实际桌面并按提示点击；首次报告 Canvas 很小、全白、没有
黑色区域。该次不能作为有效图案感知证据。fixture 原先只 XPutImage 一次，WM
map/reparent 时可能丢失绘图。现由明确 owner 持有背景 pixmap，窗口映射稳定后
重绘；清屏/Expose 时仍恢复同一公共内容。预采集前的自有 drawable 逐像素检查
通过，操作者再次确认看到了两块黑色区域。尺寸仍固定 480×180，未用改变尺寸
掩盖 DPI/坐标问题，不把视觉查看确认当作模型感知成功。

MIRAGE_CANVAS_OPERATOR=1 必须观察到中央空白处的左键释放，15 秒准备期限与
累计 32 个事件上限；前台仍需等于固定窗口。该模式不注入焦点辅助点击。准备
事件在 Agent admission 前退休；物理按钮仍按下、队列未排空或人工未取焦均拒绝。
Agent 回合开始后不再过滤操作员输入，不增加线程、队列或调度设施。新离线
[入口](../../tests/manual/visual_host_acceptance.py)给出逐例操作提示，失败即停止，
回执保留成功/失败/未尝试数量、scope 来源和工具证据。

本机 GNOME Wayland/XWayland（eDP-1、3200×2000、root depth 24）重验：公共
fixture 回读匹配，操作者释放事件已确认，前台/遮挡守卫通过；产品 root ROI
ScreenProvider 随后捕获 io_error / XGetImage failed for the requested region。
backend_calls=0、未完成 mark、无自主点击。自有 drawable 回读只是测试准备验证，
从未送入模型或作为 ScreenProvider 回退，不能宣称宿主桌面采集已通过。需要
维护者在真实 Xorg 会话补跑原路径；这只证明本机当前会话的失败，不推出所有
XWayland 都不能采集。原生 Wayland/Portal 授权仍属 M7-10。

网络补证另列：DNS 查询一度在 1.495 秒返回地址，因此按原条件补跑一次私有
4K ROI 在线三例。闭环 0/3、正确选号 1/3，所有样本仍发生端点 DNS 超时；右目标
已正确选择 mark 2，但五秒引用过期导致 stale_observation 拒绝 click，实际按下/
释放均零。保持原安全期限，没有静默重试、地址硬编码或其他传输设施。
公共回执与资源摘要见[第八增量归档](data/operator-host-visual-canvas-20261010.json)。


最终回归（本轮产品代码未改）：native-debug / native-release / ocr-debug /
ocr-asan / ocr-tsan 五配置重建 x11_visual_marks_test 并定向各 1/1；ASAN/UBSAN
使用 detect_leaks=1、halt_on_error=1，TSAN 使用 halt_on_error=1 与 setarch -R。
真实权重 4K 局部 Canvas 普通与 ASAN 各 12/12（文字 5、几何 3、模板 4），全部
不重绘负例 condition_not_matched / unverified；第七增量完整产品回归不作新的
执行声明。私有 Xvfb host-mode 人工缺席负例在 15.049 秒拒绝，fixture 回读正确、
无 operator release/工具证据/Agent admission，未注入任何人工或 XTest 输入。
新脚本 Python 编译与帮助检查通过，当前 Wayland 默认 preflight 在弹窗前以
exit 2 拒绝，报告 attempted=0；脚本本身的完整 Xorg 三例仍待真实会话执行。
本轮未改 pinned 依赖、产品任务路径或预算；未提交/推送。

## 第八增量：真实 Xorg 补证（2026-10-10）

维护者切换会话后实测 XDG_SESSION_TYPE=x11、DISPLAY=:0、WAYLAND_DISPLAY
为空，ubuntu:GNOME，eDP-1 3200×2000 @ 60 Hz；GNOME scaling-factor=0
（自动）、text-scaling-factor=1.0。公共 Canvas 仍为 480×180，不改变产品采集
预算。范围是该显示器上的固定窗口 ROI，不能据此宣称任意 DPI/跨屏支持。

首次 right 完整通过，实际原点 (132,138)；left 在复合验收断言失败，单独诊断
复验通过，实际原点 (2664,362)。首次断言没有足够诊断，仍保留为失败。加入
输入计数和工具回执后，right 重现了另一个明确问题：presses=releases=1、
wrong_presses=0、images=1，readonly verify 读到旧画面而 condition_not_matched，
随后终态新 OCR 才通过。XSync 只完成 X server 请求，不保证合成器已呈现重绘。

fixture 现于成功点击后的自有重绘步骤内，通过原 ScreenProvider 读取同一公共
ROI，逐像素核对预定绘图；最多 500 毫秒，每次未匹配后间隔 20 毫秒让合成器
推进。窗口/焦点/几何变化、捕获错误或期限耗尽直接失败。该等待在现有 Executor
回合内执行，不创建线程、队列或调度器；核对用帧立即丢弃，不作为模型图片或
产品验证结果。产品随后仍独立取得新截图与 OCR，五秒引用/观察、三十秒 owner
保持。两个真实正例均在第二次呈现核对、27 毫秒后通过，并非用固定 sleep
推断任务成功；此前失败和诊断回执没有覆盖或删除。

| 实际 Xorg 样本 | 结果 | 证据 |
| --- | --- | --- |
| 离线 right / left | 2/2 | root ROI → 编号图 → XTest → readonly 与终态新 OCR 均 condition_matched；每例 backend_calls=4、图片 1 |
| 离线 unchanged | 1/1 | 输入已释放，画面不重绘，两个验证均 unverified / condition_not_matched；宿主 failed |
| screen.capture 拒绝 | 1/1 | 注入工具 gate 拒绝，mark capture denied；mark callback=0、图片=0、鼠标事件=0 |
| scope 焦点丢失 | 1/1 | 第二个 managed 公共 fixture 经 WindowProvider 取得焦点；mark 拒绝，分析/图片/鼠标事件均零 |
| MiniMax cross-right | 通过 | 正确 mark 2；mark/click/verify 各一次，按下/释放各 1、错误点击 0；click_age_ms=3759、backend_calls=4、readonly/终态均 verified |
| MiniMax cross-left | 失败，安全拒绝 | 正确 mark 1，但 click 返回 stale_observation；无副作用、input_released=true、实际按下/释放均 0；宿主 failed、回答为空 |
| MiniMax unchanged | 未运行 | 人工在线入口在第一个失败后停止；attempted=2、total=3，不计为通过或失败 |

在线闭环为已尝试 **1/2**（计划总数 3），正确选号 **2/2**；MiniMax-M3 /
anthropic.messages.v1 / adaptive，右目标回合 16.440 秒、左目标 13.591 秒。
supplied_input_items=4 是 supplier 条目数，不能写成上传 4 张图。本轮未见端点
DNS 错误，不把一次运行当作历史网络问题全部解决。正常重绘后等待仅针对测试
fixture，不对慢模型续期或重发输入。在线驱动补了人工逐例提示、首败停止与父进程
超时回执，不新增模型凭据通道。

焦点负例初版用了 override_redirect 窗口，正确被 X11 WindowProvider 排除，
所以准备断言失败；改用受 WM 管理的自有公共窗口并通过 Provider 有界激活后
通过。常规 x11_visual_marks_test 已自动覆盖两个负例；这验证集成 gate，不
替代原生产品权限提示 UI 或对任意用户窗口的验收。正例运行在呈现同步修正后，
独立负例分支加入前；最终二进制再跑普通/ASAN 真实权重与各配置定向回归。

最终 native-debug / native-release / ocr-debug / ocr-asan / ocr-tsan 重建 harness，
定向各 1/1，含权限/焦点负例；普通与 ASAN/UBSAN 实际权重 4K 用例各 12/12
（文字 5、几何 3、模板 4）。ASAN detect_leaks=1、halt_on_error=1，UBSAN
halt_on_error=1，TSAN halt_on_error=1 与 setarch -R。定向 TSAN 通过不替代
此前完整权重 TSAN 的业务期限失败；本轮产品源码未改，未重复宣称完整产品套件
已执行。公共回执、资源/日志摘要与失败历史见
[Xorg 补证归档](data/xorg-host-visual-canvas-20261010.json)。

M7-07 仍 In Progress。下一增量先测量 mark 产出到 click 准入的模型耗时，核对
pinned Mira 的 reasoning/请求配置以减少回合开销；如需显式重观察，先冻结
无副作用前有界刷新与重新选号的契约，不能复用过期 scope 或自动重发不确定
输入。维护者在相关改变后补跑同条件在线三例；完整权重 TSAN、原生权限 UI、
主题/尺度/自由轮廓、整屏 4K/DPI/跨屏与其他平台仍独立验收。无新依赖缺口、
pin/产品源码变更，本轮未提交/推送。

第九增量补充：在线入口现有 --reasoning none|adaptive（默认仍 adaptive），
none 复用 pinned Mira Disabled。私有补证仅不重绘负例完整通过，网络仍间歇
失败，因此真实 Xorg none 三例未执行；此前宿主 adaptive 记录不覆盖。相关
配置、工具阶段耗时、输入前显式刷新与失败保留边界见
[模型视觉耗时取证](model-visual-latency-20261010.md)。网络稳定后先补私有正/负
三例，通过再按本 runbook 配合人工取焦，不自动续期或重发输入。

2026-10-10 网络调整后补证：私有 MiniMax none 三例验收 2/3、选号 3/3，本批未
报 DNS 错误。右目标与不重绘负例均闭合；左目标从 mark 完成到 click 请求
7408 ms，原五秒期限拒绝，实际输入零、回答为空。真实 Xorg none 三例因私有
前置未通过仍未启动；没有新宿主窗口，也无需维护者操作。不以私有新证据替代
上表真实 Xorg adaptive 的 1/2。详情和下一步请求阶段取证见
[网络调整后的记录](model-visual-latency-20261010.md#维护者调整网络后的补证)，
原始回执见[独立归档](data/network-adjusted-online-visual-canvas-20261010.json)。
