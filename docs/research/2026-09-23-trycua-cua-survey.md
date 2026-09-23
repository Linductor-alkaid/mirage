# 调研：trycua/cua——沙箱化 Computer-Use 基础设施与三平台驱动

> 状态：Completed
> 日期：2026-09-23
> 负责人：Mirage 维护者
> 调研方式：公开资料调研。经研究子代理并行抓取 GitHub 仓库元数据与关键源码文件、
> 官方文档站（cua.ai/docs）、官方博客（Linux / Windows 后端技术深潜）、Releases
> 与 YC 发布资料后交叉汇总；所有关键断言附来源 URL，信息时效截至 2026-09-23。
> **可靠性分级**：本调研未克隆仓库做本地源码核实，正文按三级标注——（a）来源
> 明确的事实（附 URL）；（b）合理推断（标"推断"）；（c）未能核实的存疑点
> （标"未找到公开资料"或存疑说明）。个别源码级断言（如 Python 栈 Windows
> handler 无 UIA）仅核实了单文件，引用前建议复核。
> 关联决策：[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)、
> [DEC-008](../decisions/DEC-008-m1-environment-binding-and-reference-providers.md)、
> [DEC-009](../decisions/DEC-009-provider-scope-budget-cancellation.md)、
> [DEC-010](../decisions/DEC-010-m1-permission-framework.md)、
> [DEC-015](../decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)、
> [DEC-017](../decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)
> 输出消费方：[设计方案·动作解析与 Platform Backend 边界](../design/Mirage%EF%BC%9ALinux%20-%20Windows%20%E6%A1%8C%E9%9D%A2%E7%AB%AF%E8%AE%BE%E8%AE%A1%E6%96%B9%E6%A1%88.md)、
> DEC-015 的 AT-SPI2 就绪检查与输入分发纪律、DEC-017 的 UIA 动作分发链与
> Session 0 代理模型、DEC-005 的观察错误契约、DEC-008 的引用解析/失效语义
> 范围裁剪：pinned mira / mirador 已交付能力（executor 并发原语、agent 控制面、
> JSON 编解码、视觉感知算法）的实现模式**不在学习范围**，理由见 §1.3；本文只
> 固定 Mirage 自研决策面（Platform Backend、观察契约、引用解析、权限归属）
> 上的经验。

## 1. 调研目标、方法与范围裁剪

### 1.1 对象与动机

Cua（YC X25 公司 Cua AI, Inc. 的开源主仓库，MIT 协议）定位为"给 AI Agent 一台
可用的计算机"的开源平台，现主打 "Computer-Use 2.0"：Agent 在同一任务内于代码、
API、GUI 之间切换。它是目前与 Mirage 定位最接近、概念映射度最高的开源项目——
同样面对"统一桌面动作接口 + 结构化观察 + 跨 Linux/Windows/macOS 平台后端"的
问题域，但走了**沙箱隔离**而非 Mirage 的**宿主直连**路线。选它作对照的价值
在于：其 Rust driver（cua-driver-rs）用非 Python 原生架构打通了三平台桌面
驱动，对 Mirage 的 Linux（AT-SPI2/X11）与 Windows（UI Automation/Win32）
backend 有直接可对照的实现经验。

### 1.2 方法

GitHub API 元数据 + README + `libs/` 目录 + Releases；官方文档站概念文档；
官方博客两篇后端深潜（Linux / Windows）；关键源码文件直接抓取
（`interface/base.py`、`computer-server` handlers、agent loops）。详见 §9。

### 1.3 范围裁剪（Mira / mirador 职责部分不学）

| Cua 部分 | 对应 Mirage 依赖职责 | 处理 |
| --- | --- | --- |
| Agent loop 与 20+ 模型 provider 适配（`cua_agent/loops/`） | MiraRuntime / agent 侧（mira 交付） | 不学；只取其"承载层"定位佐证（§6） |
| SoM / OCR / OmniParser 感知算法（cua-som、cua-perception） | mirador 视觉感知 | 不学 |
| VM / 容器 / 云 Fleet 编排本身（lume、lumier、fleet） | Mirage 无对应自研面（宿主直连路线） | 仅作隔离模型对照（§7.3），不学编排 |

纳入范围（Mirage 自研决策面）：统一 computer 接口的 API 形状（§5）、观察采集
与错误语义（§4）、三平台后端实现路径（§4/§5）、权限归属模型（§6.3）、对标
分析（§7）。

## 2. 项目全景

- **定位与命名**：原名 **c/ua = Container Use Agent**（"Docker Container for
  Computer-Use Agents"，[YC 发布页](https://www.ycombinator.com/launches/NJc-c-ua-docker-container-for-computer-use-agents)），
  后更名 **Cua（Computer Use Agent）**，从容器叙事扩展到云桌面、驱动与模型。
- **活跃度**（[GitHub API](https://api.github.com/repos/trycua/cua)，2026-09-23
  抓取）：25,989 stars、1,790 forks、1,040 open issues，创建于 2025-01-31，
  最近 push 2026-09-22，高度活跃。发行为 `cua-driver-rs` 每日 nightly（当前
  v0.28.3 系），二进制覆盖 macOS / Linux / Windows 三平台 arm64 与 x86_64
  （[Releases](https://github.com/trycua/cua/releases)）。
- **License**：仓库核心 MIT；感知扩展 `cua-som` 为 **AGPL-3.0**（依赖
  Ultralytics），`cua-perception` 混合 AGPL OmniParser + Apache-2.0 PP-OCR +
  ONNX Runtime。Mirage 若引用其感知包需逐包甄别。
- **平台矩阵**：Driver 直连三平台真机；Lume 在 Apple Silicon 上经
  Virtualization.framework 跑 macOS/Linux VM；Linux 沙箱 = 容器 + XFCE +
  Kasm 远程显示；Windows 沙箱走 QEMU/Hyper-V（云 Windows 沙箱 2025-11 GA，
  [博客](https://cua.ai/blog/cloud-windows-ga-macos-preview)）；另有云端
  Fleets（run.cua.ai）。

## 3. 架构：双栈并存（关键结构事实）

Monorepo 含 `libs/`（cua-driver、cua-bench、cua-s1、fleet、lume、lumier、
kasm、xfce、qemu-docker、python、typescript）、`rfcs/`、`samples/`、`docs/`
（早期根目录的 `VMs/`、`benchmarks/` 已重构进 libs，迁移路径未逐一核验）。

内部实际是**两套栈**，质量与深度差异极大：

1. **Python 沙箱栈**（`libs/python/`）：`computer`（客户端 SDK，`interface/base.py`
   抽象 + `macos/linux/windows/generic` 实现）→ guest 内 `computer-server`
   （`handlers/{macos,linux,windows,vnc,...}.py` 按平台分发）→ guest OS。
   数据流见[官方文档](https://cua.ai/docs/concepts/how-sandboxes-work)。
2. **Rust 直连栈**（`cua-driver`，即 `cua-driver-rs`）：安装在被控真机上，经
   MCP over stdio / CLI / Python-TS SDK 暴露；`cua-driver serve` 共享守护进程 +
   named pipe / socket 多客户端。**三平台的实现纵深都在这里**。

对标含义：不能拿 Python 沙箱栈当作"cua 的 Linux/Windows 能力"的证据——它的
Windows/Linux handler 很浅（§4）；真正的对标对象是 cua-driver-rs。

## 4. 桌面观察能力

### 4.1 各平台采集路径

- **截图**：三平台 handler 均以 PIL `ImageGrab` 为主（macOS 侧缩至最大
  1920px 再 base64/JPEG-PNG）。
- **Accessibility tree**：
  - macOS 最完整：`AXUIElementCopyAttributeValue`、`kAXRole/Children/TitleAttribute`、
    `AXUIElementCreateSystemWide`、`NSWorkspace` 前台应用、Quartz
    `CGWindowListCopyWindowInfo` 做 z-order 窗口枚举。
  - **Rust driver Windows**：UI Automation（`IUIAutomation` + cache request）；
    对 UIA 挂死的 VCL/SAL 应用（LibreOffice）回退 **MSAA/oleacc**
    （[Windows 博客](https://cua.ai/blog/inside-windows-computer-use)）。
  - **Rust driver Linux**：**AT-SPI 2 over D-Bus**
    （[Linux 博客](https://cua.ai/blog/inside-linux-computer-use)）。
- **注意（Python 栈的观察是浅的）**：Python 栈的 Windows handler **无任何
  UIA**，仅用 `win32gui` 窗口句柄拼"伪树"
  （`GetWindowText/EnumChildWindows/GetClassName`）；Linux handler 仅 pynput +
  Xvfb（[windows.py 源码](https://github.com/trycua/cua/blob/main/libs/python/computer-server/computer_server/handlers/windows.py)，
  单文件核实）。
- **SoM / OCR**：可选感知包 `cua-som`（Set-of-Mark，AGPL）与 `cua-perception`
  （OmniParser + PP-OCR），非核心路径。

### 4.2 观察的契约形态

Python 栈中截图与 AX 树是**两个独立调用，没有统一的"结构化 observation"
对象**；多模态融合靠模型侧完成。与之相对，"诚实的观察"体现在错误语义上：
被遮挡窗口截屏**显式标注"目标被覆盖"而非静默返回遮挡内容**；最小化窗口的
操作返回明确错误（来源：Windows 博客对 driver 行为的描述）。

## 5. 桌面操作能力

### 5.1 统一 API 面

`interface/base.py`（[源码](https://github.com/trycua/cua/blob/main/libs/python/computer/computer/interface/base.py)）
定义统一接口，分组与 Mirage Provider 集几乎一一对应：

- 鼠标：`mouse_down/up`、`left/right/double_click`、`move_cursor`、`drag/drag_to`
- 键盘：`key_down/up`、`type_text`、`press_key`、`hotkey`
- 滚动、剪贴板（`copy_to_clipboard/set_clipboard`）
- 文件：`list_dir`、`read/write_text/bytes`、`create/delete_dir` 等
- 窗口管理：`get_window_*`、`maximize/minimize/activate/close_window`
- shell：`run_command` 返回 `CommandResult`（结构化结果而非裸文本）
- 观察：`screenshot`、`get_accessibility_tree`；另有 `wait_for_ready` 就绪语义

### 5.2 各平台底层实现与动作分发链

- macOS：CGEvent 合成输入 + AX actions。
- **Rust driver Linux**：**XTEST**（明确弃用 XSendEvent）+ GTK3/4、Qt5、Tk
  的逐 toolkit 免焦点写入特例。
- **Rust driver Windows 的"高层优先"动作分发链**（最值得对标的单点设计）：
  1. UIA Pattern（Invoke/Toggle/Value/RangeValue/ExpandCollapse）
  2. 元素索引点击——"寻址控件而非坐标"
  3. 像素点击（UIA hit-test）
  4. `PostMessage` 后台投递
  5. `SendInput` 兜底，且需显式 `dispatch:"foreground"`，否则返回
     **`background_unavailable` 错误**而非静默抢焦点
- **多 Agent 并存**：每个 Agent 有独立 `cursor_id` 合成光标（透明点击穿透
  分层窗口），避免互相干扰、也为接管可视化提供锚点。

## 6. Agent 接入、部署与生态

### 6.1 Agent 接入

`cua_agent/loops/` 内置 20+ 模型专用 loop（anthropic、openai、gemini、
qwen3vl、uitars、internvl、glm45v、omniparser 等，
[源码目录](https://github.com/trycua/cua/tree/main/libs/python/agent/cua_agent/loops)）；
README 称经 **liteLLM** 集成，间接支持 Ollama 等本地后端（推断）。接入形态：
Python/TS 双 SDK、`cua-driver mcp`（MCP over stdio，默认）、一次性 CLI
`cua-driver call`；官方集成文档覆盖 Claude Code、Codex、Cursor 等
（[choose-a-cua-driver-integration](https://cua.ai/docs/concepts/choose-a-cua-driver-integration)）。

### 6.2 部署与隔离

- Lume / Lumier：Apple Silicon 本地 VM 与 Docker 化 macOS VM。
- Linux 沙箱 = 容器 + XFCE + Kasm 远程显示；Windows = QEMU/Hyper-V。
- 云端 Fleets：pool 定义启动制品与暖容量，claim 预约沙箱；`Image` 不可变
  分层（`apt_install/pip_install/run/copy/env`），本地构建、Fleet 只接受预
  构建制品；生命周期分 ephemeral / persistent / connect；`Sandbox.snapshot()`
  在 0.7.0 未实现（[how-sandboxes-work](https://cua.ai/docs/concepts/how-sandboxes-work)）。
- 权衡被明确表述为"启动延迟 vs OS 保真度"。

### 6.3 权限归属模型

文档以"隔离即 containment"为主，未给出形式化威胁模型；Driver 侧则持明确的
能力清单模型：**权限属于 runtime，Agent 不能自我扩权**；浏览器真实 profile
附着等高危能力需显式 grant。工程上，Windows 服务型 Agent 必须代理到用户交互
会话内的守护进程（named pipe），规避 Session 0 隔离；macOS TCC 权限附着于
签名 App 而非进程。

### 6.4 评测与生态

- **cua-bench**：gym 风格（`make/reset/step/evaluate`）的可验证跨平台任务
  基准框架，FastAPI worker + Playwright 模拟 provider，支持 RL 训练轨迹导出
  （[cua-bench](https://github.com/trycua/cua/tree/main/libs/cua-bench)）。
- 内置 **ScreenSpot-v2 / ScreenSpot-Pro** GUI grounding 评测脚本；2025-08 与
  **HUD** 合作做 Agent 评测（[博客](https://cua.ai/blog/hud-agent-evals)）。
- **OSWorld**：现行主仓库与 cua-bench 页面均未提及；2025 年早期资料中曾有
  OSWorld VM 集成，本次未能在现行仓库复核，**请勿引用为现状**。
- 与 OpenAI CUA / Anthropic computer use 的关系是"承载层"：统一 computer
  接口，模型侧 loop 分别适配其 computer-use 输出格式。

## 7. 与 Mirage 的对标分析

### 7.1 概念映射

| Cua 概念 | Mirage 概念 | 异同 |
| --- | --- | --- |
| `interface/base.py` API 分组 | DesktopEnvironment 的 Provider 集 | 几乎一一对应：screenshot/screen↔Screen、accessibility↔Accessibility、window/application↔Window/Application、clipboard/files/run_command↔Clipboard/Filesystem/Process；**Mirage 显式的 Notification Provider 在 cua 未见对应物（未找到公开资料）** |
| 截图 + AX 树独立调用，无统一对象 | DesktopObservation（Semantic + Visual 统一快照） | Mirage 更强的契约；cua 的融合靠模型侧 |
| 按 name/role/index/bounds 寻址控件（逐动作重查询，无快照锚定） | ElementReference / VisualReference（快照锚定，执行期解析） | Mirage 的快照锚定是真差异点，但需自行定义过期/重校验语义（§7.4） |
| Lume / 容器 / Fleet 沙箱 | Platform Backend 宿主直连 | 隔离模型相反，见 §7.3 |
| driver 的"权限属于 runtime"能力清单 | DEC-010 权限判定面 + Runtime Service 唯一 owner | 同构，可直接引用其设计语言 |

### 7.2 建议借鉴（按价值排序）

1. **动作分发优先级链与错误语义**（Rust driver Windows 路径）：UIA Pattern →
   元素寻址点击 → 像素点击 → 后台消息 → 显式 opt-in 的前台注入；不可后台时
   返回 `background_unavailable` 而非静默抢焦点。为 Mirage 的
   ElementReference 解析失败/不可达语义提供成熟先例，建议纳入设计文档的
   动作解析节。
2. **"诚实的观察"错误契约**：被遮挡截屏显式标注、目标不可达返回明确错误，
   直接支撑 DEC-005 "DesktopObservation 可验证"的目标，应成为 Screen
   Provider 的硬性错误契约（而非尽力而为行为）。
3. **Linux 后端策略**（AT-SPI2 over D-Bus + XTEST）：采树前先检查
   `org.a11y.Status` / `toolkit-accessibility`（否则 Chromium/Electron 首查
   丢树）；AT-SPI actions 优先于 XTEST 合成输入；GTK3/4、Qt5、Tk 逐 toolkit
   免焦点写入；默认走 XWayland、原生 Wayland 置于 feature flag 后。Mirage
   AT-SPI2 Backend 可直接采纳该顺序与"doctor 式自检"，**但 Wayland 不应照抄
   其后备地位**——Mirage 将 Portal/libei 类原生路径作为一等公民即是差异化
   机会。
4. **权限归属与 Session 0 模型**：Windows 服务型 Agent 代理到用户交互会话内
   守护进程（named pipe）；"权限属于 runtime、Agent 不可自我扩权"与 DEC-010
   同构，可引用其能力清单设计。
5. **`cursor_id` 多 Agent 合成光标** 与 base.py 的 `wait_for_ready` /
   `CommandResult` API 形状，供 Input Provider 与人机接管可视化参照。
6. **实现级细节清单**（设计 Mirage 后端时逐条对照）：UIA cache request 的
   使用、UIA 挂死应用回退 MSAA/oleacc、AT-SPI 注册表就绪检查、被遮挡截屏
   显式标注、`background_unavailable` 错误码。

### 7.3 差异与风险

- **两栈质量不均**：三平台纵深全在 Rust driver；Python 沙箱栈的 Linux/Windows
  handler 很浅（无 UIA、无 AT-SPI、仅 X11/pynput）。对标对象必须是
  cua-driver-rs 及其平台博客，不能引用 Python 栈证明"cua 某平台做不到"。
- **隔离模型相反**：cua 默认沙箱化（安全、可复现、云弹性，代价是启动延迟与
  保真度）；Mirage 宿主直连（低延迟、真用户环境）。因此 Mirage 必须自建
  cua 沙箱边界所兜住的安全故事——权限判定（DEC-010）、紧急停止收敛、引用
  失效处理——不能指望隔离边界兜底。
- **生态语言**：Python/TS/Rust vs Mirage C++20，代码不可复用；但 cua-driver-rs
  证明了**原生（非 Python）驱动架构在三平台可行**，反向验证了 Mirage 路线。
- **观察契约差异是机会也是义务**：cua 无统一 observation 格式、引用逐动作
  重查询（无快照锚定）。Mirage 的"统一 DesktopObservation + 快照锚定临时
  引用"是真差异点，但**引用过期/重校验语义是 cua 未解决而 Mirage 必须回答
  的问题**（DEC-008 的解析纪律需覆盖：快照失效、元素被遮挡/销毁、引用跨
  动作复用的判定规则）。

### 7.4 对现有决策的具体输入

| 决策/文档 | 本调研输入 |
| --- | --- |
| DEC-017（Windows backend） | UIA cache request + MSAA 回退；动作分发优先级链；`background_unavailable` 错误语义；named pipe 会话代理模型 |
| DEC-015（Linux backend） | AT-SPI2 就绪检查先行；AT-SPI actions 优先于 XTEST；逐 toolkit 免焦点写入；XWayland 默认 + Wayland feature flag（Mirage 可反转为 Portal 一等公民） |
| DEC-005（observation contract） | "诚实的观察"错误契约；被遮挡显式标注；无统一 observation 格式是 cua 缺口，佐证 DEC-005 方向 |
| DEC-008（reference providers） | 元素寻址点击先例；引用过期/重校验语义需自答（cua 未解决） |
| DEC-010（permission framework） | "权限属于 runtime、Agent 不可自我扩权"能力清单模型 |
| DEC-009（provider scope/cancellation） | `cursor_id` 合成光标作为输入收敛可视化的参照 |

## 8. 结论

Cua 是当前与 Mirage 问题域重叠度最高的开源对照系：统一 computer 接口的 API
形状、动作分发优先级链、"诚实的观察"错误契约、AT-SPI2/UIA 实现细节、权限
归属模型均值得逐条吸收进 Mirage 的 Platform Backend 与契约设计。它的沙箱
路线与 Mirage 的宿主直连互为镜像——吸收其实现纪律的同时，Mirage 必须自行
补上沙箱边界本可兜住的权限与失效语义；其 Python 栈在 Linux/Windows 上的
浅实现与 Rust driver 的纵深形成鲜明对照，说明三平台桌面驱动的难点不在语言
而在平台细节的工程投入。

## 9. 来源清单

- [GitHub 仓库](https://github.com/trycua/cua) ·
  [API 元数据](https://api.github.com/repos/trycua/cua) ·
  [libs 目录](https://github.com/trycua/cua/tree/main/libs) ·
  [Releases](https://github.com/trycua/cua/releases)
- [官方文档站](https://cua.ai/docs) ·
  [沙箱概念](https://cua.ai/docs/concepts/how-sandboxes-work) ·
  [Driver 集成](https://cua.ai/docs/concepts/choose-a-cua-driver-integration)
- [Linux 后端博客](https://cua.ai/blog/inside-linux-computer-use) ·
  [Windows 后端博客](https://cua.ai/blog/inside-windows-computer-use) ·
  [云 Windows GA](https://cua.ai/blog/cloud-windows-ga-macos-preview) ·
  [HUD 评测合作](https://cua.ai/blog/hud-agent-evals)
- [YC 发布页（c/ua 命名）](https://www.ycombinator.com/launches/NJc-c-ua-docker-container-for-computer-use-agents)
- 源码：[interface/base.py](https://github.com/trycua/cua/blob/main/libs/python/computer/computer/interface/base.py) ·
  [computer-server handlers](https://github.com/trycua/cua/tree/main/libs/python/computer-server/computer_server/handlers) ·
  [agent loops](https://github.com/trycua/cua/tree/main/libs/python/agent/cua_agent/loops) ·
  [cua-bench](https://github.com/trycua/cua/tree/main/libs/cua-bench)
