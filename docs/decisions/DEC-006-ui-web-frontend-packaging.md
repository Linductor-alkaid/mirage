# DEC-006：UI 技术路线与分发打包形态

> 状态：Accepted
> 日期：2026-09-15
> 负责人：Mirage 维护者
> 冻结里程碑：M3（技术路线与打包形态提前于 M1 定案；壳选型与更新通道为暂定默认值，M3 冻结）
> 替代/被替代：无

## 背景与问题

设计文档第 17 节将 `ui/`（workspace / tasks / workflow / execution / overlay /
settings）与 `apps/desktop`、`apps/tray` 定义为 Mirage 桌面产品，但未固定 UI 的实现
技术栈、进程形态与分发打包方式。总计划将本决策登记为 DEC-006（原定 M3 前定案）。

需要定案的原因：UI 与 Runtime Service 的唯一耦合面（Local IPC 契约）、构建工具链、
发布安装包形态与升级通道都依赖该决策；越晚定案，契约与发布工程的返工风险越大。

讨论过程中曾评估以 Python（uv 管理依赖）实现前端的路线，本记录一并封存该备选的
否决理由。

## 决策

1. **UI 采用 Web 前端技术栈**：`ui/` 以 HTML / CSS / TypeScript 与组件化框架实现。
   产品形态是独立桌面应用——界面渲染在应用自有窗口与嵌入式渲染引擎中，不是浏览器
   网页，不要求用户打开浏览器或保持联网。
2. **UI 是独立进程**：GUI、Tray 与 CLI 一致，仅经 Local IPC 与 Runtime Service 交互
   （与总计划 `EXEC-02` 一致）。IPC 契约是 UI 与 runtime 的唯一耦合面，契约 schema
   带版本号并纳入兼容矩阵测试（机制选型见 DEC-007（待建），UI 消费的感知 schema 见
   DEC-005（待建））。
3. **渲染壳【暂定默认值】CEF**：与 Mirage 的 C++ 栈同语言、可经 CMake 集成、
   Linux / Windows 渲染行为一致。备选 Tauri（系统 WebView、体积小，但 Linux 侧
   WebKitGTK 版本碎片化且壳为 Rust）与 Electron（自带 Chromium 但引入 Node 运行时）。
   壳选型 M3 冻结，冻结前以 PoC 验证：窗口嵌入、本地资产加载、IPC 桥延迟基线。
4. **分发打包【已定案】**：Linux 发布 `.deb`；Windows 发布 `exe` 安装包（NSIS /
   WiX-exe 等具体生成器在 M5 里程碑计划确定）。版本与发布遵循工程规范 10.5
   （语义化版本、CHANGELOG、SBOM 与兼容性门禁）。
5. **更新通道【暂定默认值】**：Linux 的 deb 以 apt 仓库为唯一更新路径（包管理器与
   自更新器不并行，避免互相覆盖）；Windows 以签名的应用内更新器下载新安装包升级，
   含原子切换与回滚。签名方案与差分/全量策略 M3 复核、M5 实现。
6. **工具链与依赖锁定**：Web 前端工具链（包管理器、打包器、CEF 二进制获取）在 M5
   里程碑计划定案；CEF 二进制版本与前端依赖必须进入依赖锁定与 SBOM 机制（工程规范
   9.1），不得出现未锁定的二进制来源。
7. **Python / uv 不作为 UI 技术栈**：uv 是 Python 包/项目管理器，不是前端框架也不是
   打包器；此前评估的"PySide6 + uv"路线随本决策关闭。

## 备选方案

- **PySide6 / Python + uv（原生 Qt 控件）**：优势是 Python 生态成熟、uv 锁文件可
  复现、wheel 化分层更新灵活。否决原因：产品选择复用 Web 前端的视觉表现力与设计
  实现工作流，Qt 原生控件路线无法覆盖；且避免在同一产品内引入第二应用语言。若未来
  出现 Python 生态强需求，可另立决策重开。
- **本地 HTTP 服务 + 系统浏览器**：实现最轻、无壳体积。否决原因：托盘、Overlay、
  全局快捷键、独立窗口等 Desktop Product 集成能力受浏览器宿主限制，不满足产品形态。
- **Tauri / Electron 壳**：未否决，保留为壳选型备选（见决策 3），M3 依据 PoC 复核。

## 影响与风险

- **安装包体积**：CEF / Chromium 使两平台安装包增加约 100–200 MB 量级；以该代价
  换取渲染一致性与设计工作流。
- **供应链面扩大**：前端依赖与 CEF 二进制进入锁定与 SBOM 范围，依赖审计工作量增加。
- **进程模型边界**：CEF 多进程（browser / render / GPU）属于 UI 应用内部实现，不与
  Executor 强制规则冲突（Executor 约束 Mirage 自研 C++ 并发，壳内线程/进程由壳自身
  管理）；但壳与 IPC 桥之间的投递仍须遵守"第三方回调只做有界校验与投递"边界
  （AGENTS.md Executor 规则 11）。
- **Overlay 平台限制**：M5 Overlay 的置顶/透明/点击穿透在 Linux 受合成器与 Wayland
  限制；该职责在 Platform Backend 解决，不随壳选型转移。
- **契约兼容负担**：IPC 契约是唯一耦合面，演进必须向后兼容或双版本并存；这是 UI 与
  runtime 可独立发版（进而支持分层更新）的前提。

## 验证方式

- M3：壳选型 PoC 与冻结评审（CEF vs Tauri 对比包体积、内存基线、IPC 桥延迟、
  Linux 发行版兼容性），结论回写本记录。
- M5：deb / exe 安装包在目标平台完成安装、升级、卸载验证；更新通道演练（签名校验、
  失败回滚）；IPC 契约兼容矩阵测试。
- 持续：`dependencies.lock.json` 与 SBOM 覆盖 CEF 二进制及前端依赖；发布前按工程
  规范 10.5 门禁核对。

## 关联文档和工作项

- 设计文档第 17 节（本次同步补充 UI 形态与分发说明）。
- 总计划：`EXEC-02`（GUI/CLI/Tray 经 IPC 交互）、M5 Desktop Product、尚未冻结决策表
  DEC-006 行（本次更新）。
- DEC-005（待建，DesktopObservation 契约）、DEC-007（待建，Local IPC 机制）。
- 工程规范：第 9.1 节（依赖锁定）、第 10.5 节（版本与发布）。
