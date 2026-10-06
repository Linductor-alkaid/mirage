disposition: recapture

本轮独立窄范围复核采用 `reference/degraded/finish-reviewer.md` 契约；只审查 Linux 开发 desktop entry 注册与窗口身份关联。没有提供 Dock 像素 capture，原四张品牌行截图不覆盖 Dock，不沿用其 ship 结论。未运行 HTML detector。

## recapture

缺失：能显示当前运行 Mirage 在 GNOME Dock 中真实图标的全屏或 Dock 区域截图。有效 capture 应包含可定位的 Mirage Dock 项及其红发蓝眼 Mira 图标；截图须在注册条目且重新启动当前 native 窗口后取得。GNOME Screenshot / Introspect 拒绝所导致的未取证不能由 accessibility 名称或窗口图标属性替代。此 disposition 仅表示 Dock 像素验收待补，不撤销此前已捕获界面的历史证据，也不 ship 整个前端。

可复核的技术事实（2026-10-04，当前未提交工作树，Linux/X11）：

- `apps/native/CMakeLists.txt` 仅在 Linux 且 native frontend 启用时生成条目；`mirage-native-register-desktop` 为显式目标，依赖 executable 及其资产复制。普通构建不修改用户应用目录；single-/multi-config executable 与 Icon 路径均来自 target generator expression。
- `org.mirage.native.desktop.in` 的条目 ID、`StartupWMClass=org.mirage.native` 与 `app.cpp` 的 appId 一致；`register_desktop.cmake` 检查源条目存在，遵循绝对 `XDG_DATA_HOME` 或 HOME fallback，且桌面数据库刷新失败可见。
- 独立执行 `desktop-file-validate /home/linductor/.local/share/applications/org.mirage.native.desktop`，退出 0、无诊断。
- 独立以 Gio 读取 `org.mirage.native.desktop`，Name 为 Mirage，StartupWMClass 为 org.mirage.native；FileIcon 指向 `build/native-release/apps/native/assets/mira.png`，GdkPixbuf 解码为 1254×1254。对 Gio commandline 通过 GLib `shell_parse_argv` 解析后，Exec 文件存在且有执行权限。这里不能直接把 `get_executable()` 的带引号原字符串作为文件路径验证。
- 独立执行 `xprop -id 0x1a0000b WM_CLASS _NET_WM_NAME`，当前 Mirage 窗口 WM_CLASS 两字段均为 org.mirage.native，标题 Mirage · Agent。窗口与注册条目的身份关联通过。
- 初始 `/tmp/mirage-dock-accessibility.json` 只有 role/name/path；随后已核对补存的 `native-dock-registration-evidence.json`。其 `gnome_accessible_nodes_with_states` 确有 name 为 Mirage、role 为 push button、showing=true、screen_bounds 为 [0,1106,132,120] 的节点，另一个 Mirage push button showing=false。附件支持 GNOME Shell 可访问性中存在可见 Mirage 项，坐标符合左侧 Dock；状态证据缺失已解决。该证据仍不能证明图标像素已正确显示。
- 父执行者报告已用临时 XDG_DATA_HOME 实际运行注册脚本、校验输出条目逐字节匹配；此次独立复核未重跑该写入操作，仅确认脚本的目录选择和显式注册实现。

注册实现与当前身份关联未发现需要修改代码的 material 问题。其技术证据支持“开发 desktop entry 已注册并可解析、当前窗口身份匹配”，不支持“Dock 图标视觉验收通过”。Dock 补验保持未完成；负责人为 Mirage 维护者，补跑条件为获得允许的桌面截图方式或维护者提供当前 Dock 的有效截图，再按同一窄范围重审。保留原始 Mira PNG、显式注册行为与当前 appId。
