# DEC-033：EUI-NEO 原生 Agent 前端首步

> 状态：Accepted
> 日期：2026-10-03
> 负责人：Mirage 维护者
> 工作项：M6-01、M6-02
> 依据：维护者明确要求引入 EUI-NEO dev、无边框窗口及参考 ZCode 的对话页面。
> 替代关系：本轮原生前端选型替代 DEC-006 的 CEF 默认产品方向；既有 CEF 实现和验收证据保留，不代表原生端已经达到功能等价。

## 决策

1. 增加 UI 专用直接依赖 `third_party/eui-neo`，跟踪上游 `dev`，构建以
   `dependencies.lock.json` 的完整 commit 为准，不随配置自动漂移。核心依赖仍为
   Mira / Mirador；EUI 不承载 Agent、任务并发或 Runtime 生命周期。
2. 原生前端位于 `apps/native`，独立可执行文件 `mirage-native`，使用 C++20、
   EUI 的 GLFW / OpenGL 后端及公开 DSL。提供 `native-debug` / `native-release`
   preset，现有默认构建与 CEF opt-in 保持可用，本轮不切换托盘启动目标或安装包。
3. 使用 `.decorated(false)` 创建无边框窗口。窗口移动、缩放、最小化、最大化与
   关闭的 GLFW 适配仅存在于应用私有 `window_controls.cpp`，不暴露进 Runtime /
   Desktop / Platform 公共契约，不修改上游代码；继续复用 EUI 的输入与 IME 主循环。
4. 首步是可交互的对话页面：新建/切换/清空本地会话、草稿、多行输入、示例提示、
   明暗主题、消息滚动。页面明确标注“界面预览”，提交仅形成带“未发送”状态的本地
   消息，不虚构 Agent 回复、模型连接或桌面执行。预览数据只保存在进程内，有容量限制。
5. 页面参考 ZCode 的信息架构和中性色层级，独立实现，不复用 Electron、React 或其
   业务代码。简体中文、窄侧栏、会话标题、阅读列与底部 composer 是当前视觉基线。
6. 本轮无后台工作。业务状态仅由 UI 线程持有，不调用 EUI async/network/audio。
   后续真实 `session.*` 接入必须通过现有 Local IPC 和 Mira Executor，不允许使用 EUI
   内置异步线程池。关闭当前窗口仅退出前端，整体托盘生命周期在 M6-04 收口。

## 备选与后果

- 继续 CEF：可保留既有业务，但不满足维护者这次的轻量原生界面要求。
- 立即 fork EUI：暂不采用，先验证 dev 已修复输入问题；需要修改时另留反馈与授权。
- 同时迁移完整 IPC/设置/工作流：本轮不采用，先让窗口与会话页可见可验收。
- GLFW 无边框在 Linux 当前使用 X11/XWayland；不宣称原生 Wayland 支持或 Windows
  已验收。字体按系统常见 CJK 字体查找，缺失时使用随 EUI 分发的字体。

## 验收

见 [M6 计划](../plans/m6-native-frontend.md) 与
[原生前端说明](../design/native-agent-frontend.md)。

2026-10-03 集成核查：dev 的 LF/CRLF IME 摘要与 Release 异常选项存在构建问题，
见 [EUI 台账](../dependency_feedback/eui-ledger.md)。使用文档化外部 GLFW target 与
应用 CMake 编译属性解决，不修改 pinned 源码、不创建 worker；升级后按移除条件复核。

2026-10-04 维护者授权扩展：侧栏可调宽，主题入口改为设置/外观；继续沿用
ZCode中性导航与设置行。UI主线程持有本次进程内偏好，保留聊天草稿，不新增后台
任务或持久化契约。验收见 [设置与侧栏证据](../compatibility/native-settings-20261004.md)。
