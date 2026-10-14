# Windows 安装入口与托盘呈现维护

> 状态：In Progress
> 负责人：Mirage 维护者 / Codex
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-15
> 依据：维护者报告安装入口闪终端、间歇黑角、重复托盘和菜单过小；DEC-018 / DEC-045。

## 范围与工作项

- [ ] `BUG-20261014-004` Windows 图形启动入口不创建控制台，CLI 输出保持可用；覆盖安装、卸载和 payload。
- [ ] `BUG-20261014-005` 通知承载图标隐藏，产品只显示可交互主托盘；通知受理/拒绝和 teardown 回归。
- [ ] `BUG-20261014-006` 按 GPT 参考提供白色圆角、阴影、分组、悬停和 DPI 缩放，保持键盘、禁用态和原动作派发。
- [ ] `BUG-20261014-007` 对照已安装程序、当前 Release 和上游透明 fixture 定位间歇黑角；确认上游归属后按 CONTRIBUTING 提交 Issue/PR。

## 设计与依赖边界

图形入口复用既有 `mirage start` 控制面及托盘 Runtime owner，不新增并发路径。
菜单使用现有 Win32 pump 上的 modeless 弹层，标准 BUTTON 保留可访问名称/禁用态，回调只投递既有 Executor 动作。
通知继续使用 Shell_NotifyIcon，隐藏独立承载图标；产品默认值同步 DEC-018/045。
透明问题先复现取证；上游通用 alpha 修补与 Mirage 产品固定圆角保护分别记录。
已确认修补不能消除目标机器全部黑角，允许在单一 Windows 窗口适配边界按产品轮廓裁剪；不宣称通用透明问题已解决。

## 测试与退出条件

- [ ] Windows MSVC Release 构建、相关 CTest、PE 子系统与安装器 payload 检查。
- [ ] 开始菜单启动、重复启动/窗口恢复、单主托盘、菜单打开/关闭/动作和通知受理真机检查。
- [ ] 透明问题具有可重复证据及最小修复，或明确记录未定位限制和补跑条件。
- [ ] 文档、格式、分层检查通过；必要的上游贡献符合四组合本机门禁。

## 验证记录

工作项保持 In Progress：实现、本机运行、跨平台 CI 与上游投稿已交付，真实安装升级
及上游评审仍开放；不以工作树运行或安装器编译替代安装门禁。

2026-10-14：工作前仓库干净，基线 cb973b2；新分支 codex/windows-desktop-polish。
本机 Windows 11 / MSVC 19.44 / x64。已安装与当前 Release 的 mirage-native SHA256
一致（216dd2ca8fad5e9c3b1422bb7d0e5c092ce66e18a1f5d3f56f1baf3672ce2c3b）。
维护者确认从开始菜单打开时黑角间歇复现。源码确认透明 hint、RGBA clear 和主托盘禁用 EUI tray 均已设置；
NotificationBackend 独立 NIM_ADD 是第二个无菜单图标的来源。
上游 GLFW/OpenGL transparent_window_viewer 已在独立目录配置和 Release 构建。
截图工具的透明区域可能显示黑色，必须区别捕获底色与真实桌面合成，不能单凭窗口截图确认上游根因。

### 2026-10-15：Windows 本机修复与回归

- BUG-004：`mirage-launcher` 从首次指令起是 GUI 子系统，共享 CLI start 控制面；tray
  同样为 GUI，CLI 为 CUI。`dumpbin /headers` 验证三者分别为 2 / 2 / 3。图形入口在
  私有 named pipe 成功冷启动 tray/runtime/frontend，三次重复调用复用同一 frontend PID；
  无效端点能弹出原错误文本。NSIS 的新增文件、快捷方式和卸载删除已同步，payload 完整。
- BUG-005：通知承载 NIF_STATE / NIS_HIDDEN；既有 win32_notification_test 验证受理、
  预算/UTF-8/取消拒绝以及 teardown/reopen，未关闭通知能力。新包实际通知区计数待升级验收。
- BUG-006：16 px 字号、40 px 最小行高、220 px 最小宽度、20 px 横向留白，按 DPI 缩放。
  新 win32_tray_menu_test 在 96/144/192 DPI 检查尺寸、禁用、绘制及原生菜单文字；
  `--preview` 实际检查外观与 Down/Down/Return 的选中、退出。菜单动作仍使用既有 ID/Executor 路由。
- BUG-007：GL 角像素 alpha=0 且无错误，缺陷为 bundled GLFW 没有初始化 DWM 重定向
  表面。原候选 GL clear 方案撤销。移植 GLFW #2815 的透明 WM_PAINT 修复，旧 procedure
  回归计数 0 / exit 1，修复计数 1 / exit 0，六帧边缘匹配自有背景；fixture 外圈与最终
  Mirage 主窗口圆角都透出真实桌面。上游 Issue #109、前置 SDL2 build Issue #110、
  PR #111 / Draft PR #112 已提交，未合并。pin/锁文件同步，详见
  [升级审计](../supply-chain/eui-windows-presentation-20261015.md)。

Windows Release 实际构建命令：`cmake --build build/windows --config Release --target
mirage mirage-launcher mirage-tray mirage-native win32_tray_menu_test win32_notification_test
win32_product_process_test host_dpi_awareness_test provider_contract_test --parallel 6`。
定向 CTest：`ctest --test-dir build/windows -C Release --output-on-failure -R
'^(win32_tray_menu_test|win32_notification_test|win32_product_process_test|host_dpi_awareness_test|provider_contract_test|dependency_lock_gate_test)$'`，
最后一轮 6/6、0 failed（7.16 s）。format/boundary 检查通过，公共头 59 项、0 违规。

上游四独立组合 Release 构建与完整 CTest 均执行；GLFW/OpenGL 35 passed/1 skipped，
其他组合 34 passed/2 skipped，各 36 项、0 failed。明确 skip 与临时本地 Vulkan 工具见审计，
不宣称未执行的后端 alpha 保证。预览均为本机生成的测试进程，不发布用户桌面截图。

NSIS 实际执行 `packaging/windows/stage-payload.ps1` 与 `build-installer.ps1` 生成安装包。
最终交付使用新的 payload 目录，避免重复 staging 目录叠套；所有测试进程按私有端点收敛，
未覆盖正在运行的旧安装，也未修改其开始菜单。

## 剩余验收与负责人

- [ ] 维护者：退出旧托盘后安装新包，再从真实开始菜单冷启动/重复启动，检查唯一可交互
  图标、托盘打开/退出和圆角；包含卸载删除 launcher 的实际验证。补跑条件是允许安装升级的目标机器。
- [x] Mirage CI / Codex：源码 `939c769` 的八项 CI 全部通过，包含 Windows 全树、Linux
  Debug/Release/ASAN/UBSAN/TSAN、native 与 format/boundary；见下方精确验收记录。
- [ ] 维护者：Windows x64 ASAN 运行库补齐后运行菜单定向测试；本机仅 x86 运行库可用，
  x64 首次链接失败，不记为通过。现有 Win32 ASAN 的结果不替代 x64 覆盖。
- [ ] 上游维护者：先评审 #111，再评审 #112；后者 draft 保留前置依赖，合并不在本次授权范围。

### 2026-10-15：源码 CI 与最终交付

[Mirage Draft PR #95](https://github.com/Linductor-alkaid/mirage/pull/95) 基于现有 #94 的
fix/nsis-native-registry-view 分支，只包含本次呈现差异。精确源码验收 head 为
`939c769`，[CI run 38605542146](https://github.com/Linductor-alkaid/mirage/actions/runs/38605542146)
八项全部成功：format/public-header、Debug、Release、ASAN、UBSAN、TSAN、Windows full tree、native。
本记录后续提交仅更新 Markdown 证据，不改变已验收代码或 pin。

额外 Windows 菜单 ASAN：独立临时 CMake project 使用 `/fsanitize=address /utf-8 /Zi /W4 /WX`，
链接 `/INCREMENTAL:NO`，复用真实 win32_tray_menu_test 源码和头文件。x64 配置/编译成功，
链接失败于缺少 `clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`；核对 SDK 只有 x86 ASAN
运行库。以 `-A Win32`、RelWithDebInfo 重建并运行，51 checks / 0 failures，无 ASAN 报告。
未安装或变更系统运行库；x64 补跑负责人和条件保持上方开放项。

最终新 payload 位于 `build/windows-polish-final-20261015`，包含 GUI launcher、CLI、service、
tray、native 与平铺资源。最新 launcher 的 null-terminated argv 也已重建并重新冷启动/关闭验证。
NSIS 安装包为该目录下 `mirage-installer.exe`；本轮源码的四项修复可供维护者升级验证，
现有 Program Files 安装及默认开始菜单未覆盖。没有合并 PR 或创建 release/tag。
### 2026-10-15：安装版复发与 GPT 风格菜单跟进

维护者安装上一轮包后仍报告四角黑色；Program Files 与上一轮 payload 的 native SHA256
一致（8DE1DF014AFCFFFF12DE1AF5DD227590DBD2D2CF615D849FD319B3DD3FB71CF6）。因此先前
fixture/probe 的通过不作为“间歇黑角完全解决”的验收，BUG-20261014-007 保持开放。
已更新上游 #109 及 Draft #112，移除 closes 声明，明确 PatBlt 修补的证据范围；pin 不变。

本轮采用单一 Windows 私有窗口轮廓保护（12 logical px，随 framebuffer/render scale，
尺寸变化时更新，最大化清除；移除条件及整数边缘/阴影代价见升级审计）。这是产品保护，
不将其称作上游通用 alpha 修复。维护者给出的 GPT 图片用于视觉参考；菜单仅呈现既有
Mirage 动作，白色 300px 圆角弹层、40px 行、20px 文本留白、14px Segoe UI、灰色圆角
选中/悬停与退出组分隔线。DWM 阴影/圆角及 Windows 10 轮廓降级由私有 Backend 实现。
BUTTON 保留名称与禁用态；modeless pump 避免菜单期间阻塞停止，不新增并发设施。

实际验证：
- MSVC x64 Release native/tray/menu-test 重建成功。
- Windows 6 项定向 CTest 6/6、0 failed。最终可见性/静止鼠标高亮修正后 menu-test 补跑通过。
- 菜单/轮廓共 115 checks、0 failures：96/144/192 DPI；禁用项跳过、循环导航、Home/End、
  Enter、鼠标命令、Escape、失焦关闭、显式回收、workarea 限位；四角剔除、边缘保留、
  resize、最大化移除及恢复。
- 同源 Win32 ASAN / RelWithDebInfo 115 checks、0 failures，无 ASAN 报告。x64 ASAN
  运行库缺失的限制保持，Windows 10 和真实混合 DPI 跨屏未验收（维护者提供设备后补跑）。
- 真实新 native 私有 named pipe 启动，四角可见背景；实际最大化为矩形，恢复后圆角重新
  生效。私有 Runtime shutdown 后确认测试 native/tray 均退出，仅保留用户安装版。
- --preview 实际查看白色圆角/分隔，UIA 返回“打开应用”“退出应用”按钮；Down 的选择
  与可见灰色高亮一致，Return/Escape 关闭。静止鼠标生成的合成 move 不覆盖键盘选择。
- format、public-header boundary（59 headers / 0 violations）和 git diff --check 通过。
- 最新 payload 使用全新 build/windows-rounded-menu-20261015 目录；不重复拷贝 provider
  目录、不包含产物入 Git。NSIS 安装包 16,307,753 bytes，SHA256
  0A93F32D0DA5C85853E45473F2CDBA9375B58B4E32DC74E55BE1913EF799193A。
  native SHA256 F6E971FA858CE5DFCFC606EA646819F7F69E5B52C0FDF929DFC5E119439C6E51；
  tray SHA256 85EFE531BFADCA982FA8E7595DEC93C7CA1BB12CA89C85D66BE548DDF7C3EDB0。

仍开放：维护者从真实安装/开始菜单复验新包的四角、菜单和卸载；上游通用 alpha 根因及
Windows 10/混合 DPI/其他 GPU。上一轮 939c769 的 CI 是历史证据，不覆盖本轮新源码。
本轮继续更新现有 Draft #95，不合并 PR、不创建 release/tag。新增 CI 另记精确 head。
