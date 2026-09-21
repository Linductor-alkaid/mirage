# Verification Scripts

> Playwright + headless Chromium 自动化跑视觉验证。

## 为什么用全局 playwright-core

沙箱里 npm playwright (1.62.0) 跟 npm playwright-core (1.63.0) 版本不一致；直接
装 chromium-headless-shell 又要重新下载 ~100MB。

折中：沙箱里已经有 `/root/.cache/ms-playwright/chromium-1243/chrome-linux/chrome`
（playwright-core 1.63.0 下载过），脚本里直接 import
`/usr/local/lib/node_modules/playwright/node_modules/playwright-core/index.mjs`
并把 `executablePath` 指向缓存里的 chromium。

## 用法

```bash
# 1. 启动 vite dev server（窗口 1）
cd ui && npm run dev -w @mirage/app

# 2. 跑截图脚本（窗口 2）
node scripts/shoot-world.mjs    # 默认视角 + 时间序列
node scripts/shoot-detail.mjs   # 网格扫描点击 + 详情面板
```

输出：

- `shot-overview.png` — 4 team zones + 28 agents + meeting hub 俯览图
- `shot-detail.png` — 点中 Arch-01 后右侧 AgentDetailPanel