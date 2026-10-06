# TS / CEF 前端退役验收（2026-10-05）

> 工作项：M6-08（Linux 构建与打包首步）
> 决策：[DEC-037](../decisions/DEC-037-native-model-composer-and-web-retirement.md)
> 负责人：Mirage 维护者
> Git：codex/native-agent-workbench；本地提交

维护者明确要求清除旧 TS 前端。删除 ui、CEF desktop、Web devbridge 及对应测试、
CEF 下载配置；保留运行时 IPC golden 与原生消费者。依赖锁 schema 3 描述 native-cpp
入口和 EUI，不含 CEF artifact。Pinned Mira/Mirador/EUI 提交保持不变。

CLI 的 mirage start、托盘默认启动目标、Linux desktop entry 与安装包已接原生前端，
CI 去掉 Node/npm/CEF 构建，Windows job 改为 native 构建。CLI --socket 通过
MIRAGE_NATIVE_SOCKET 传给原生进程。没有改变状态栏整体退出活动确认的待实现状态；
M6-04 不因删除旧壳而完成。

本轮开始前七份既有未提交修改保存在本机
`/tmp/mirage-legacy-before-retirement-20261005/`，包括 CLI、desktop、M5 计划和 Linux
打包文件；原 M5 历史实施记录保留，当前事实由 M6/DEC-037 替代。旧 TS 源码仍可从
Git 历史恢复。无关的旧 UI 截图未纳入这次提交。

## 执行结果

- Debug/Release 全树构建成功；Debug 空闲顺序全量 CTest 48/48。负载下曾出现旧事件
  订阅测试失败，详情和后续条件见[模型/输入栏验收](native-model-composer-20261005.md)。
- 全新 Ninja Release verify-only/native 配置成功，目标图含 mirage-native，未含旧
  desktop/devbridge/CEF。该路径不需要 Node/npm。
- dependency_lock_test 9 个门禁场景通过：原生有效锁、旧 CEF 项/schema、错误入口、
  错误依赖/缺失源码和错误 artifact 类型均明确拒绝。
- `sh -n packaging/linux/postinst packaging/linux/prerm` 与已安装入口的
  desktop-file-validate 通过。
- `cmake --build --preset native-release` 后 CPack DEB 成功。最终包
  `build/native-release/mirage_0.1.0_amd64.deb` 为 13,712,328 字节（约 13.1 MiB），
  Installed-Size 27207 KiB；内容为四个可执行程序、四个 UI 资产、desktop entry 和
  hicolor 图标，无 Chromium/CEF/Node。GL/X11 动态依赖明确声明，建议安装
  zenity 或 kdialog 与 CJK 字体。
- 以提取的包运行 `mirage start --no-tray --socket <隔离路径>`：原生窗口出现，
  进程环境中的 socket 正确，session.list 观察到 UI 会话，服务 status 正常；窗口以
  WM_DELETE 关闭，测试服务经 IPC shutdown 清理。未执行 root 安装/卸载。
  此 smoke 在最后标题/小窗口视觉修正前执行；最终 UI 的 Debug/Release 构建、测试和
  实机画面另有上述证据，最终包重新生成成功。
- 自研格式、公开头边界（48 / 0）、diff 空白检查通过；提交前检查未纳入凭据、日志、
  构建产物、无关截图或 submodule 修改。

## 明确保留的缺项

Windows 原生全树/NSIS 编译未在 Linux 执行，Windows 构建/快捷方式/安装退出必须由
维护者在 Windows native preset 和 NSIS 环境补跑。Linux 包 root 安装/卸载、独立
托盘活动退出确认亦未验收；负责人为维护者，补跑条件是隔离桌面安装环境以及 M6-04
退出协议实现。没有把 M5 旧 Web 测试结果迁移为原生功能等价或跨平台证明。
