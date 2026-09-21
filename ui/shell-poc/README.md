# Mirage 壳选型 PoC（M3-06 / DEC-006）

> 状态：Evidence collected（2026-09-21）
> 事实源：[壳 PoC 基线报告](../../docs/benchmarks/shell-poc-baselines.md)、
> [壳二进制锁定与 SBOM 机制复核](../../docs/supply-chain/shell-binary-locking.md)、
> [DEC-006](../../docs/decisions/DEC-006-ui-web-frontend-packaging.md)

本目录是 DEC-006 壳选型冻结（M3-06）的 PoC 承载，**不是产品代码**：CEF / Electron
两壳以同一延迟 harness、同一本地资产测试物（`ui/app/dist` 生产构建）做同口径取证；
产物不进入产品默认构建（DEC-006 第 6 条）。

## 结构

- `harness/bridge-latency.html` — 三壳共用延迟 harness（CEF / Electron / Tauri 按全局
  对象自动探测桥）。方法学：每样本 = 50 次连续 await 往返的均值（µs），规避 Chromium
  非 crossOriginIsolated 页面 performance.now 100µs 量化；warmup 20 样本丢弃，64B /
  4096B 两种请求载荷各 400 样本；原生侧 handler 打 enter/leave 时间戳；统计与原始
  样本经桥回传壳进程落盘。
- `cef/` — CEF 壳（pinned `152.0.8+g1ce985c+chromium-152.0.7977.134` stable）：
  独立 CMake 工程（`-DCEF_ROOT=<解包的 CEF 发行包>`）；浏览器进程创建自有 X11 窗口
  并以 `SetAsChild` 嵌入浏览器（窗口嵌入证据：`window_embed.children` 非空）；桥 =
  CEF message router（`window.cefQuery`）；本地资产经 `http://mira.local` scheme
  handler 供给（`file://` 的 ES module 资产被 CORS 拦截，见基线报告 §3.3）。
- `electron/` — Electron 壳（pinned `44.4.3`）：`main.js` + contextBridge preload，
  桥 = `ipcRenderer.invoke`；`file://` 直接加载（Electron 对 file:// 模块放行）。
  运行开关 `--disable-gpu --no-sandbox`（XWayland 下 GPU 进程崩溃的规避，见基线
  报告 §3.4）。
- `measure/collect_cef.sh` / `collect_electron.sh` — 单壳采集：运行壳 + 进程树
  `VmRSS` 采样（500ms）+ 结果落盘。
- `measure/memory_sampler.py` — 进程树 RSS 采样器（三壳同口径）。
- `measure/summarize.py` — 从原始样本独立重算百分位，与壳内统计交叉核对。
- `versions.lock.json` — PoC 物料版本锁（URL + SHA1/integrity + 许可证）。
- `results/` — 原始结果 JSON（提交为证据；`*-latency.json` 含 800 原始样本/壳）。

## 复现

前置：Node ≥ 22、cmake ≥ 3.21、ninja/make、X display（XWayland 即可）、
`ui/app/dist`（`npm run build -w @mirage/app`）。

```bash
# 1) CEF：下载 pinned 发行包并校验（版本/SHA1 见 versions.lock.json）
curl -L -o /tmp/cef.tar.bz2 "https://cef-builds.spotifycdn.com/cef_binary_152.0.8%2Bg1ce985c%2Bchromium-152.0.7977.134_linux64.tar.bz2"
sha1sum /tmp/cef.tar.bz2   # add0a51f7333bc660e8e3bafd998e0122568f7d8
tar xjf /tmp/cef.tar.bz2 -C /tmp
cmake -B cef/build -S cef -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT=/tmp/cef_binary_152.0.8+g1ce985c+chromium-152.0.7977.134_linux64
cmake --build cef/build --target poc_cef -j$(nproc)

# 2) Electron
(cd electron && npm install --no-audit --no-fund && node install.js)  # 网络受限时:
#   ELECTRON_MIRROR=https://registry.npmmirror.com/-/binary/electron/ node install.js

# 3) 采集（各壳 harness 延迟 + ui 资产加载 + 进程树内存）
measure/collect_cef.sh harness results/cef
measure/collect_cef.sh ui results/cef ../../../ui/app/dist/index.html
measure/collect_electron.sh harness results/electron
measure/collect_electron.sh ui results/electron ../../../ui/app/dist/index.html

# 4) 汇总交叉核对
python3 measure/summarize.py results/cef-latency.json results/electron-latency.json
```

## 结果速览（2026-09-21，本机）

| 指标 | CEF 152.0.8 | Electron 44.4.3 | Tauri 2.x |
| --- | --- | --- | --- |
| 桥往返 64B p50 / p95 | 102 / 342 µs | 98 / 196 µs | 未执行（补跑条件） |
| 桥往返 4KiB p50 / p95 | 142 / 362 µs | 112 / 232 µs | 未执行（补跑条件） |
| 进程树 RSS 峰值 | ≈1.05–1.08 GiB | ≈0.89–0.91 GiB | 未执行 |
| 引擎载荷（发布口径） | ≈560 MB（strip 后） | 296 MB | ≈0（系统 WebKitGTK ≈135 MB） |
| 产品 UI 本地加载 | ✓（经 scheme handler） | ✓（file:// 直载） | 未执行 |
| 原生窗口嵌入 | ✓（SetAsChild X11 子窗口） | ✓（BrowserWindow 自管） | 未执行 |

结论与冻结决定见 [DEC-006](../../docs/decisions/DEC-006-ui-web-frontend-packaging.md)
2026-09-21 修订节。
