# 壳 PoC 基线：JS 桥延迟 / 进程树内存 / 引擎载荷（DEC-006 M3-06）

> 状态：Evidence（已完成采集）
> 日期：2026-09-21
> 对应工作项：[M3-06](../plans/m3-mirador-integration.md)（DEC-006 壳选型 PoC 与冻结）
> PoC 承载与复现步骤：[ui/shell-poc/README.md](../../ui/shell-poc/README.md)
> 原始数据：`ui/shell-poc/results/*.json`（提交于仓库）

## 1. 环境与条件

| 项 | 值 |
| --- | --- |
| 主机 | x86_64，Intel Core Ultra 5 225H（14 核），32 GiB RAM |
| 操作系统 | Ubuntu 24.04（内核 7.0.0-31-generic），Wayland 会话（壳经 X11 ozone / XWayland 或原生 wayland，见 §3.4） |
| 显示 | 真实 X display `:0`（X.Org 1.23，XWayland 承载） |
| CEF | 152.0.8+g1ce985c+chromium-152.0.7977.134（stable，linux64 standard 发行包，SHA1 见 `ui/shell-poc/versions.lock.json`），独立 CMake + GCC 13.3 构建 |
| Electron | 44.4.3（npm 锁定，`electron/package-lock.json`） |
| UI 测试物 | `ui/app/dist`（Vite 生产构建，813,004 B，含本地字体，无网络资源），同一构建供两壳加载 |
| 运行开关 | CEF：`no_sandbox=true`、`--ozone-platform=x11`；Electron：`--disable-gpu --no-sandbox`、`--ozone-platform=x11`（后端选型理由见 §3.4） |

## 2. 方法学（三壳统一口径）

### 2.1 桥延迟

- 页面：`ui/shell-poc/harness/bridge-latency.html`，无框架、无网络访问，按全局对象
  自动探测桥（CEF = `window.cefQuery` message router；Electron = contextBridge +
  `ipcRenderer.invoke`）。
- **样本定义**：1 样本 = 50 次连续 `await` 往返的耗时均值。理由：页面非
  crossOriginIsolated 时 Chromium 将 `performance.now()` 粒度量化至约 100 µs，逐次
  计时无法分辨更小的往返；50 次均值把量化误差摊薄至 ±2 µs。该口径对三壳一致。
- 序列：每载荷规模 warmup 20 样本（丢弃）后测量 400 样本；请求载荷 64 B 与
  4096 B 两种（JSON 字符串，pad 补齐）；响应为固定小 pong（含原生侧时间戳）。
- 原生段拆分：壳进程 handler 进入/离开以 `steady_clock` 打 µs 时间戳，用于确认
  测量值中原生处理占比可忽略（CEF/ Electron handler 均为 JSON 解析 + 回显，≤10 µs
  量级），桥延迟主体为渲染进程 ↔ 浏览器进程的 IPC 通道与任务调度。
- 统计：壳内 JS 计算 n / mean / stdev / min / p50 / p90 / p95 / p99 / max；原始
  400 样本/序列经桥分块回传落盘；`measure/summarize.py` 以原始样本独立重算百分位
  与壳内统计交叉核对（**本批两壳全部一致，偏差 0.0 µs**，无 shell 内统计造假面）。

### 2.2 进程树内存

- `measure/memory_sampler.py`：对壳根进程树（含 GPU / renderer / utility / zygote）
  每 500 ms 求和 `/proc/<pid>/status` VmRSS，直至根进程退出；报告 max / final /
  进程数峰值与完整序列。两壳口径一致。

### 2.3 引擎载荷（发布口径）

- CEF：pinned 发行包 `Release/` + `Resources/` 为打包输入；其中 `libcef.so` 原始
  1,428,168,760 B 含完整调试符号，**发布载荷按 strip 后 456,524,792 B 计**，加
  Release 辅助库（libEGL / libGLESv2 / libvk_swiftshader / libvulkan ≈16 MB）与
  Resources（icudtl.dat、locales、pak，86,027,867 B），**合计 ≈559 MB**。
- Electron：`node_modules/electron/dist/` 即发布载荷，**296,434,086 B**。
- Tauri：引擎不随应用分发，依赖系统 WebKitGTK 运行时（本机 Ubuntu 24.04 的
  webkit 栈安装体积 ≈135 MB：libwebkit2gtk-4.1-0 93.4 MB + javascriptcore 31.6 MB
  + gtk3 9.8 MB + soup 0.7 MB，`dpkg-query` 实测）；该体积由发行版承担、不进入
  Mirage 安装包，但跨发行版版本不可控（见 §4 与 DEC-006 冻结结论）。
- 对照项：Mirage UI 资产 813,004 B（三壳相同）。

### 2.4 依赖面（GLIBC）

`objdump -T` 实测最大 GLIBC 符号版本要求：**CEF libcef.so 与 Electron 主二进制均
为 GLIBC_2.25**——两壳对发行版 glibc 基线要求一致且宽松（Ubuntu 20.04+ 满足）。

## 3. 结果

### 3.1 桥往返延迟（µs，n=400/序列，每样本=50 次往返均值）

| 壳 | 载荷 | mean | stdev | min | p50 | p90 | p95 | p99 | max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CEF 152.0.8 | 64B | 137.0 | 94.2 | 34.0 | 102.0 | 288.0 | 342.0 | 492.0 | 528.0 |
| CEF 152.0.8 | 4096B | 169.6 | 85.8 | 54.0 | 142.0 | 286.0 | 362.0 | 484.0 | 572.0 |
| Electron 44.4.3 | 64B | 111.3 | 49.0 | 28.0 | 98.0 | 162.0 | 196.0 | 342.0 | 400.0 |
| Electron 44.4.3 | 4096B | 121.0 | 72.9 | 30.0 | 112.0 | 200.0 | 232.0 | 428.0 | 764.0 |
| Tauri 2.x | — | 未执行 | | | | | | | |

读法与限制：

- 两壳同为一个数量级（p50 ≈ 100–142 µs），**桥延迟不构成任一壳的否决项**；
  Electron 的 p50/p90 略优（invoke 通道直连 main 进程 V8 handler），CEF 尾部
  p95–p99 略高。
- 本基线为壳内桥开销，**不是** UI ↔ Runtime 全链路延迟（后者经 Local IPC
  Unix socket，在 M1-04/DEC-007 与 M1.5 dev bridge 各自有验证）；壳选型只对
  壳内一段负责。
- 单机、桌面空载条件；未声明跨机或负载下保证（RULE-08）。

### 3.2 进程树 RSS（GiB，500 ms 采样）

| 壳 / 页面 | max | final | 进程数峰值 |
| --- | --- | --- | --- |
| CEF / harness | 1.08 | 0.81 | 10 |
| CEF / ui | 1.09 | 0.44 | 10 |
| Electron / harness | 0.89 | 0.78 | 10 |
| Electron / ui | 0.91 | 0.89 | 10 |
| Tauri | 未执行 | | |

读法：两壳同为 Chromium 多进程形态，稳态内存同量级（final 0.44–0.89 GiB）；
max 含启动期 GPU/编译峰值。内存不构成否决项。

### 3.3 本地资产加载与窗口嵌入（功能验证）

| 检查 | CEF | Electron |
| --- | --- | --- |
| 产品 UI 生产构建加载并挂载（`document.readyState=complete`、`#app` 有子节点、标题 "Mirage 控制台"、1 script + 1 stylesheet、0 console 错误） | ✓ | ✓ |
| 加载通道 | `http://mira.local`（CEF scheme handler，自研工厂 ≈100 行） | `file://` 直载（Electron 对 file:// 模块放行） |
| 原生窗口嵌入 | ✓：应用自建 X11 窗口 + `CefWindowInfo::SetAsChild`，`window_embed.children` 非空（CEF 浏览器子窗口） | ✓：BrowserWindow 自管窗口（无"嵌入外部原生窗口"路径，Overlay 场景需另行验证） |

**CEF 的 file:// 限制是本 PoC 的关键集成发现**：Vite 构建的 `type="module" crossorigin`
资产在 CEF 的 `file://` 下被 CORS 拦截（origin 'null'，Chrome 仅允许
chrome/data/http(s) 等协议），产品实现需要一个本地资产 scheme handler 层（本 PoC
已验证该层可行：同源 http 语义、正确 MIME、防目录穿越，无 socket 服务进程）。
Electron 无此限制（其 Chromium 对 file:// 模块有补丁放行），但该差异以 ≈100 行
CEF 代码消化，不构成否决。

### 3.4 运行开关与后端选型（影响复现的条件）

- 两壳均强制 `--ozone-platform=x11`（XWayland 承载）：宿主为 Wayland 会话，Chromium
  系默认自选 wayland ozone，与 CEF 的 X11 嵌入窗口路径不兼容且会挂起。
- CEF 以 `no_sandbox=true` 运行（chrome-sandbox setuid 未配置，产品打包按 M5 评估）。
- Electron 需 `--disable-gpu --no-sandbox`：X11 ozone 下其 GPU 进程段错误（exit 139）
  并级联拖垮渲染；禁用 GPU 后稳定（软件光栅化）。此为 **Electron 在本环境的稳定性
  缺陷证据**，记入冻结结论（不外推为所有环境结论，产品打包的 GPU 开关策略 M5 定）。
- 桥延迟测量的解释力不受显示后端影响：往返路径为 renderer ↔ 壳主进程的进程内
  IPC 通道，不经合成器/GPU。

### 3.5 未执行项（Tauri）与补跑条件

| 项 | 状态 |
| --- | --- |
| Tauri 2.x 桥延迟 / 内存 / 载荷 / UI 加载 / 窗口嵌入 | **未执行** |

- 原因（2026-09-21 实测）：PoC 环境无 Rust 工具链（rustc/cargo 缺失，
  static.rust-lang.org 不可达），且 `webkit2gtk-4.1-dev` / `libgtk-3-dev` 无法
  经 apt 安装（sudo 需密码）。Tauri 侧结论由系统运行时证据（webkit2gtk 2.52.6
  存在于 Ubuntu 24.04、栈体积 §2.3）+ 结构性分析（DEC-006 冻结节）承载。
- 补跑条件：具备 Rust stable 工具链 + `libwebkit2gtk-4.1-dev` + `libgtk-3-dev` 的
  Linux 环境，按 `harness/bridge-latency.html` 的 `window.__TAURI__` 桥检测与
  §2 方法学复跑；负责人 Mirage 维护者；触发：DEC-006 冻结结论被重开评审时。

## 4. 结论对冻结的输入

1. 桥延迟、内存：CEF 与 Electron 无决定性差异 → 不构成互斥依据。
2. 体积：CEF ≈559 MB vs Electron 296 MB——CEF 约多 263 MB，是既定代价
   （DEC-006 影响节已预告 100–200 MB 量级），换无 Node 运行时的供应链面收敛。
3. 集成面：CEF 需 scheme handler 层（已验证）；Electron 无，但其壳编排语言为 JS，
   与产品 C++ 主栈相悖（见 DEC-006 备选封存理由）。
4. 嵌入路径：CEF 原生窗口嵌入实测可行，对 M5 Overlay/合成场景有路径价值。
5. 兼容：两壳 GLIBC_2.25 基线宽松；Tauri 的系统 WebKitGTK 版本碎片化仍为主要
   结构性风险（DEC-006）。

冻结决定与备选否决理由封存：见 [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)
2026-09-21 修订节；壳二进制进入锁定与 SBOM 的机制：
[shell-binary-locking.md](../supply-chain/shell-binary-locking.md)。
