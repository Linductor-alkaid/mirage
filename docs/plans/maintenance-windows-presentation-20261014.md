# Windows 安装入口与托盘呈现维护

> 状态：In Progress
> 负责人：Mirage 维护者 / Codex
> 所属计划：[实施总计划](mirage-implementation-plan.md)
> 更新日期：2026-10-15
> 依据：维护者报告安装入口闪终端、间歇黑角、重复托盘和菜单过小；DEC-018 / DEC-045。

## 范围与工作项

- [ ] `BUG-20261014-004` Windows 图形启动入口不创建控制台，CLI 输出保持可用；覆盖安装、卸载和 payload。
- [ ] `BUG-20261014-005` 通知承载图标隐藏，产品只显示可交互主托盘；通知受理/拒绝和 teardown 回归。
- [ ] `BUG-20261014-006` 原生托盘菜单字号、行高、留白随 DPI 缩放，保持键盘、禁用态和原动作派发。
- [ ] `BUG-20261014-007` 对照已安装程序、当前 Release 和上游透明 fixture 定位间歇黑角；确认上游归属后按 CONTRIBUTING 提交 Issue/PR。

## 设计与依赖边界

图形入口复用既有 `mirage start` 控制面及托盘 Runtime owner，不新增并发路径。
菜单继续使用 Win32 原生 HMENU 消息循环，回调只投递既有 Executor 动作。
通知继续使用 Shell_NotifyIcon，隐藏独立承载图标；产品默认值同步 DEC-018/045。
透明问题先复现取证，不通过不透明背景或窗口裁剪掩盖呈现错误；尚未确定归属时不修改 pinned 依赖。

## 测试与退出条件

- [ ] Windows MSVC Release 构建、相关 CTest、PE 子系统与安装器 payload 检查。
- [ ] 开始菜单启动、重复启动/窗口恢复、单主托盘、菜单打开/关闭/动作和通知受理真机检查。
- [ ] 透明问题具有可重复证据及最小修复，或明确记录未定位限制和补跑条件。
- [ ] 文档、格式、分层检查通过；必要的上游贡献符合四组合本机门禁。

## 验证记录

工作项保持 In Progress：实现、本机运行与上游投稿已交付，真实安装升级和跨平台 CI
补证尚未执行；不以工作树运行或安装器编译替代这些门禁。

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
- [ ] Mirage CI / Codex：本机只有 Windows，Linux/native、ASAN/UBSAN/TSAN 由本次 MR CI
  补跑；失败则修复，未运行前不标通过。不新增并发路径，不声称其他 GPU/显示器已验收。
- [ ] 上游维护者：先评审 #111，再评审 #112；后者 draft 保留前置依赖，合并不在本次授权范围。
