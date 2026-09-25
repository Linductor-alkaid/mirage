# DEC-006：UI 技术路线与分发打包形态

> 状态：Accepted
> 日期：2026-09-15（2026-09-21 壳选型与更新通道冻结，见文末修订节）
> 负责人：Mirage 维护者
> 冻结里程碑：M1（技术路线与打包形态）；M3（壳选型与更新通道，已冻结）
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
3. **渲染壳【已冻结 2026-09-21：CEF】**：与 Mirage 的 C++ 栈同语言、可经 CMake 集成、
   Linux / Windows 渲染行为一致。备选 Tauri（系统 WebView、体积小，但 Linux 侧
   WebKitGTK 版本碎片化且壳为 Rust）与 Electron（自带 Chromium 但引入 Node 运行时）。
   M3 冻结依据的 PoC 取证：窗口嵌入、本地资产加载、IPC 桥延迟基线、包体积与内存
   基线，见 [壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)与修订节。
4. **分发打包【已定案】**：Linux 发布 `.deb`；Windows 发布 `exe` 安装包（NSIS /
   WiX-exe 等具体生成器在 M5 里程碑计划确定）。版本与发布遵循工程规范 10.5
   （语义化版本、CHANGELOG、SBOM 与兼容性门禁）。
5. **更新通道【已冻结 2026-09-21】**：Linux 的 deb 以 apt 仓库为唯一更新路径（包管
   理器与自更新器不并行，避免互相覆盖），仓库经 GPG 签名；Windows 以签名的应用内
   更新器下载新安装包升级（更新清单 ed25519 签名 + SHA-256 + Authenticode 双层），
   含原子切换与回滚。差分/全量策略复核结论：**全量优先**（Linux apt 无内建二进制
   差分；CEF 无内建差分框架），差分更新 M5 按实测流量成本评估、不提前承诺；实现
   在 M5。复核记录见
   [壳二进制锁定与更新通道复核](../supply-chain/shell-binary-locking.md) 第 3 节。
6. **工具链与依赖锁定**：Web 前端工具链（包管理器、打包器、CEF 二进制获取）在 M5
   里程碑计划定案（已于 2026-09-26 M5-01 定案，见修订记录）；CEF 二进制版本与前端
   依赖必须进入依赖锁定与 SBOM 机制（工程规范
   9.1），不得出现未锁定的二进制来源。M3-06 已完成进入机制的形态复核：壳二进制以
   工件 pin（URL + 版本 + SHA1 + license）并入 `dependencies.lock.json` 校验，npm
   树以 `package-lock.json` + 仓库级概要条目锁定，SBOM 由构建目标再生成（CEF 组件
   清单经 chrome://credits 导出）；见
   [壳二进制锁定与 SBOM 机制复核](../supply-chain/shell-binary-locking.md)。
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

- M3：壳选型 PoC 与冻结评审——**已完成（2026-09-21）**，证据见
  [壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)与文末修订节。
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

## 修订记录

### 2026-09-21：壳选型与更新通道冻结（M3-06）

**冻结结论：渲染壳冻结为 CEF**（pinned `152.0.8+g1ce985c+chromium-152.0.7977.134`
stable 起步），更新通道按决策 5 冻结（apt + GPG / Windows 双层签名更新器、全量
优先）。取证与证据等级见
[壳 PoC 基线报告](../benchmarks/shell-poc-baselines.md)（桥延迟 / 内存 / 载荷 /
本地资产 / 窗口嵌入，CEF 与 Electron 为实测，Tauri 为系统运行时证据 + 结构分析，
补跑条件已登记）；锁定与 SBOM 机制复核见
[shell-binary-locking.md](../supply-chain/shell-binary-locking.md)。

CEF 相对优势（实测）：与产品 C++ 主栈同语言、经 CMake 集成（PoC 即以独立 CMake
工程验证）；原生窗口嵌入路径实测可行（X11 `SetAsChild` 子窗口），为 M5 Overlay/
合成场景保留路径；无 Node 运行时，供应链面收敛为单一工件 pin。

CEF 的已量化代价（接受）：发布载荷 ≈559 MB（strip 后 libcef + 资源，Electron 为
296 MB）；`file://` 模块资产受 CORS 限制，需 ≈100 行本地资产 scheme handler 层
（已在 PoC 验证可行，无 socket 服务进程）；X11 ozone + `no_sandbox` 的运行开关
在 M5 打包时转正式方案（沙箱与 GPU 策略届时定）。

**备选封存理由**（重开条件：上游/生态实质变化或本决策被新决策替代）：

- **Electron**：桥延迟与内存与 CEF 同量级（64B p50 98 µs vs 102 µs；4 KiB p50
  112 µs vs 142 µs；RSS final 0.78–0.89 GiB vs 0.44–0.81 GiB），无性能优势；壳
  编排语言为 JS/Node，与产品 C++ 主栈相悖并使 Node 运行时 + npm 依赖树整体进入
  SBOM 与审计面；预编译二进制经 npm postinstall 拉 GitHub releases，多一个镜像
  治理面；`file://` 模块放行依赖其对 Chromium 的补丁差异。体积略小（296 MB）不
  构成决定性优势。
- **Tauri 2.x**：本环境未能完成可运行 PoC（无 Rust 工具链、webkit2gtk-dev 不可
  安装，补跑条件已登记），仅完成系统运行时取证（Ubuntu 24.04 webkit2gtk 2.52.6，
  栈安装体积 ≈135 MB）；结构性风险维持原判：系统 WebKitGTK 版本碎片化使"渲染行
  为一致 + 设计工作流复用"（决策 1）跨发行版不可控，壳为 Rust 与 C++ 主栈双语
  言；体积优势（≈0 打包引擎 vs CEF ≈559 MB）不足以抵消一致性与双语言成本。
- **PySide6/uv 与本地 HTTP + 浏览器**：维持原否决理由（见备选方案节）。

### 2026-09-26：前端工具链定案与壳二进制锁定落地（M5-01）

**决策 6 兑现（Web 前端工具链定案，M5-01 落地）**——M1.5 起的既成事实逐项复核
确认，以最小成本定案，无框架迁移：

- **包管理器**：npm（workspaces）。CI 以 `npm ci` 安装、禁止裸 `npm install`
  漂移；Node 版本下限 22（`ui/package.json` `engines` 声明，CI setup-node 22）。
- **打包器**：Vite 7（app 构建 / dev server / 本地字体打包，无网络资源）。
- **组件框架**：React 19（DEC-014 既定选型复核确认）+ Base UI + vanilla TS
  主题注入不变；《前端设计规范》§2.2 token 命名映射复核**一致**——实现事实源
  `ui/app/src/theme/primitives.ts` 与规范 L1 `--mir-*` 基准逐项对应（console /
  cream / amber-300 阶为 M1.5-07/08 任务控制台语系已记录的 L1 增补，非漂移），
  React 形态下 token 消费路径（CSS 自定义属性）不变，无需映射迁移。
- **lint 工具链（新定选）**：ESLint 10 + typescript-eslint 8 +
  eslint-plugin-react-hooks 7（flat config `ui/eslint.config.js`；CI frontend
  作业增 lint 步骤）。此前仅 tsc 严格检查；react-hooks v7 新增 `purity` /
  `set-state-in-effect` 两规则对 M1.5 已交付视图存在 7 处既有发现（5 处渲染期
  `Date.now`、2 处 effect 内 setState），修复需视图级重构（时间源注入 /
  effect→render 派生），登记为 M5-06 / M5-07 重做对应视图时的清理范围，当前在
  lint 配置内记录性豁免（含理由注释），其余规则全量生效。
- **测试**：vitest（M1.5 起事实，复核确认）。
- **CEF 二进制获取**：官方 CDN（`cef-builds.spotifycdn.com`），版本与双平台
  摘要经 `dependencies.lock.json` schema v2 工件 pin 锁定（见下）。

**壳二进制锁定落地（schema v2）**：`dependencies.lock.json` 升至 schema v2
（M5-01，机制设计见
[shell-binary-locking.md](../supply-chain/shell-binary-locking.md) §2）——
`artifacts[]` 登记 CEF `152.0.8+g1ce985c+chromium-152.0.7977.134` stable
standard 双平台 pin：linux64（674,894,043 B，sha1 `add0a51f…`）为官方 index 与
PoC 本地 sha1sum **双源一致**（index 提取 2026-09-26；本地复核 2026-09-21）；
windows64（359,844,028 B，sha1 `fcefc344…`）为官方 index 提取（2026-09-26），
本地下载-摘要复核随 M5-02 首次消费执行并回填 provenance。`frontend` 条目登记
npm 树概要（`ui/package-lock.json` 的 sha256 于**每次 configure 重算比对**，
漂移即构建失败；许可证清单生成随 M5-11 发布门禁接线）。

**"未锁定二进制不进默认构建"门禁**：`cmake/MirageDependencies.cmake` 扩展——
schema 版本强制、工件 pin 结构校验（缺员 / 非 40-hex sha1 / 空值 fail
closed）、npm 哈希活动门禁、`mirage_require_locked_artifact()` 消费门禁
（未注册工件被产品目标消费即 configure 失败）；`dependency_lock_gate_test`
正负 7 例回归（Linux 矩阵与 windows 作业双面）。当前默认构建图无 CEF 消费
目标，门禁语义自本变更起由测试持续锁定。

**沙箱与 GPU 策略（PoC 暂定值转正式方案，M3-06 预告项）**：

- **沙箱默认启用**。PoC 的 `no_sandbox` 仅因取证环境未配置 chrome-sandbox
  setuid（基线报告 §3.4），属打包事项而非产品策略：Linux `.deb` 须以正确属主
  / 模式安装 chrome-sandbox（M5-11），Windows 按 CEF 默认沙箱承载；任何环境性
  回退必须显式记录原因与范围，不允许静默默认禁用。
- **GPU 保持启用、不设跨平台禁用开关**。CEF 的 GPU 进程在 PoC 环境（X11
  ozone / XWayland）正常，基线报告 §3.4 的 GPU 段错误为 Electron 特有证据；
  个别环境的稳定性回退须凭实测证据逐环境处理（RULE-08），不外推、不预先全局
  禁用。

验证证据见 [M5 计划](../plans/m5-desktop-product.md) M5-01 验证记录。
