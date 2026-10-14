# Wayland 截屏降级为交互截图的事故与 fail-closed 修复

> 日期：2026-10-14
> 负责人：Mirage 维护者
> 关联：DEC-015（fail closed）、DEC-073（隔离 Portal 总线）、DEC-085（会话韧性）、
> wayland-portal-observation-20261011、DEC-084（桌宠进程与动画边界）
> 状态：已修复，待独立测试复验

## 事故摘要

2026-10-14 11:35 的会话中，`desktop.screen.capture` 首次调用即弹出 GNOME 前台交互
截图 UI（要求用户点击），随后返回 `portal_failed: Desktop did not return a screenshot`；
`desktop.vision.ocr` 因无共享会话报 "native sharing ended or failed"。Agent 丧失全部
视觉观测，任务以"桌面视觉读取失败或被拒绝"失败。会话记录
（`~/.local/state/mirage/session-state.json`，session `5e4c4d11…`）与
xdg-desktop-portal 日志（11:36:15 `InteractiveScreenshot didn't return a file`）均证实。

## 根因

当日 11:16 构建部署的 `build/native-release/apps/mirage-service` 是
`MIRAGE_LINUX_PORTAL=1` 且 **`MIRAGE_LINUX_PIPEWIRE` 未定义** 的变体：

- 二进制内无任何 ScreenCast/PipeWire 符号与字符串（`OpenPipeWireRemote`、
  `ScreenCast capture failed` 等 0 命中），NEEDED 缺 `libpipewire-0.3.so.0`；
  `capture_portal` 符号尺寸与带 PipeWire 的归档成员不一致，证明该批产物并非当前
  native-release 归档链接所得，而来自某个未启用 PipeWire SDK 的配置（本机未安装
  libpipewire-0.3 开发包，native-release 一直依赖
  `-DMIRAGE_PIPEWIRE_SDK_ROOT=/tmp/mirage-wayland-sdk/root`）。
- 该变体中 `capture_portal` 无条件走 Screenshot Portal 回退，且
  `interactive=TRUE`（原 portal_screen_backend.cpp:684），每次截图都弹前台 UI。
- 原 CMake 逻辑（platform/CMakeLists.txt）在找不到 PipeWire 时**静默跳过**，
  构建照常成功，产品能力大幅退化无人察觉。
- 对比：前一晚（10-13 23:20 起）的会话使用的旧二进制带 ScreenCast 路径
  （`desktop.session.click` 共享输入全部 verified，只有 PipeWire 路径可实现），
  截图全部成功。期间没有任何提交改动截屏代码（最后一次为 10-13 14:39 a7524f3），
  变化仅在部署了哪个构建产物。

## 修复

1. **fail-closed**（platform/linux/src/portal_screen_backend.cpp）：删除交互
   Screenshot 回退与其 PNG 解码路径。无编译期 PipeWire 路径，或会话 Portal 经
   内省确认缺少 ScreenCast 接口时，截屏如实返回
   `unsupported_capability`（错误信息分别指明"构建缺 PipeWire，请安装
   libpipewire-0.3 开发文件或设置 MIRAGE_PIPEWIRE_SDK_ROOT 后重建"与
   "会话 portal 不提供 ScreenCast"）。理由：交互截图会抢占用户桌面、返回无坐标
   映射的图像，不是自主观测路径；与 DEC-015 的 fail-closed 纪律及 a7524f3
   （诚实能力声明）一致。附带效果：`decode()` 的有界 PNG 读取与相关错误码
   （`invalid_portal_image`）随之移除。
2. **构建期告警**（platform/CMakeLists.txt）：Portal 可用但 PipeWire 不可用时
   输出 `WARNING`，禁止再次静默降级。
3. **部署自检**：重建 native-release 后以 `ldd`/`strings` 验证产物含
   ScreenCast 路径并链接 libpipewire，再行拉起（见下方验收）。

## 桌宠退出（同会话观察）的结论

用户报告"任务失败后 MiraPet 直接退出，疑似崩溃"。核查结果：**非缺陷、非崩溃**。

- coredumpctl、/var/crash（apport 为本机 core handler）、journal 均无
  mira_pet/mirage 崩溃记录。
- pet 的退出路径均为设计内：桥接断连 ≥5s 自退（apps/pet/app.cpp:202-203）或
  服务 owner 收敛（SIGUSR1 → 退出动画 → 关窗）。
- 服务侧 11:37:10 有序落盘（task-recovery.json `tasks:[]`、session-state.json）
  后整个应用结束。主窗口关闭**不会**停服务（runtime_service.cpp 仅将
  `frontend_ready` 置否），因此本次是用户整体退出（托盘退出/关闭）后 pet 按设计
  断连自退；DEC-084 决策 2"主窗口关闭不退出桌宠"的边界未被破坏。

## 测试与验收

- portal_screen_fixture / portal_screen_test 同步收缩（tests/platform/，由
  Independent-Verification-Agent 执行）：非 cast 模式收敛为
  `budget / noscreencast / tool / tool_denied`（budget 锚定入口契约：
  `max_bytes=0` → `invalid_argument`，小预算不再产生 `capture_too_large`——该码
  属于已删除的 PNG 解码路径，现仅存于 PipeWire 流路径）；`noscreencast` 断言
  `unsupported_capability` 并按编译变体区分信息子串；cast_*/rd_* 全部 36 场景与
  `pixels` 模式逐字保留；`absent` 期望按编译变体区分（PipeWire →
  `portal_unavailable`，否则 → `unsupported_capability`）。
- 实测（2026-10-14）：native-release 构建零警告（-Werror）；
  portal_screen_fixture 在 release 与 ASAN 双树通过（41 场景，含 19 cast + 17 rd）；
  白名单套件 provider_contract / x11_backend / desktop_provider_boundary /
  visual_observation_loop / desktop_image_supply / portal_input /
  portal_authorization / portal_stream_retention 全部通过。
- 部署产物自检（build/native-release，12:52 重建）：
  `readelf -d apps/mirage-service` NEEDED 含 `libpipewire-0.3.so.0`；
  `strings` 命中 `OpenPipeWireRemote`、`ScreenCast`、`session_handle_token`；
  回退字符串 `Screenshot Portal request failed` 与平台库内 `interactive` 为 0
  （二进制中残留的 `interactive` 来自 runtime/ipc 协议关键词表，与截屏无关）。

## 影响范围

- 仅 Linux Portal 截屏路径的错误语义与构建告警；ScreenCast/RemoteDesktop 正常
  路径行为不变。Windows 截屏（DXGI/WGC/BitBlt 分层）与 X11 路径不受影响。
- 无 PipeWire 的构建（如 CI 原生门禁）中，Wayland 截屏从"每次弹前台 UI、30 秒
  超时"变为"确定性 unsupported_capability"。
