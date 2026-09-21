# M4-World Verification

> M4 计划：[../plans/m4-world-projection.md](../plans/m4-world-projection.md)

## 自动化验证

每个 milestone 落地时，下述命令必须全部通过：

```bash
cd ui
npm run check    # TypeScript 严格模式
npm test         # vitest（574/574 通过）
npm run build    # vite production build
```

构建产物（M10 之后）：

```
dist/assets/index-*.js           516 KB │ gzip: 177 KB   ← 不含 three.js 的主 chunk
dist/assets/WorldPageLazy-*.js   575 KB │ gzip: 147 KB   ← 异步加载的 Three.js chunk
dist/assets/index-*.css           50 KB │ gzip:   9 KB
```

## 视觉验证（Playwright + headless Chromium）

沙箱里通过 `scripts/shoot-world.mjs` / `shoot-detail.mjs` /
`shoot-time.mjs` 调 Playwright 自动化跑一遍，截图保存在本目录：

| 截图 | 说明 |
|---|---|
| `shot-overview.png` | 俯览视角（点过「俯览」按钮）；4 个 team zone + 中央 meeting hub；28 agent 占位 |
| `shot-detail.png` | 点击 Arch-01 后右侧 AgentDetailPanel；标题 / 角色 / 团队 / 视觉状态 / 当前任务 / 当前活动 / 镜头聚焦按钮 |

自动化脚本：

```bash
# 终端 1：dev server
cd ui && npm run dev -w @mirage/app

# 终端 2：截图（用 global playwright-core，绕开 python 版本差）
node scripts/shoot-world.mjs   # 俯览 + 时间序列
node scripts/shoot-detail.mjs  # 网格扫描点击 + 详情面板
```

期望：

1. 进入 `#/world` 后，页面显示「加载组织世界…」约 200-400ms，然后出现
   `<canvas>` 与顶部 HUD（FPS / Agents / t=…s / 模拟 / Hover ID）。
2. 默认主题现代办公室风格；底部 28 个 agent 占位（4 Team × 7 Agent 区域 +
   中央 meeting hub）。
3. 模拟器启动后若干秒出现第一次 `collaboration.started`：三个 agent 头顶
   出现 status badge、meeting space 上方出现蓝色连接线。
4. 点击任意 Agent → 直接弹出右侧 AgentDetailPanel（角色 / 团队 / 视觉状态 /
   当前任务 / 当前活动）。
5. 左键 = orbit；右键 = pan；滚轮 = zoom；hover 切换 cursor（pointer vs grab）。

## 已知遗留

- 软件 WebGL（SwiftShader）在 headless 下只有 ~5-6 FPS；真实浏览器 GPU
  上通常 60+ FPS，运动细节更明显。本里程碑没有把 demo 录像录出来，
  仅以静态截图佐证。
- Timeline UI / 历史回放控件尚未落地；WorldClock + ReplayEngine 已就位，
  M12 接入。

## 提交 hash

```
git log feat/m4-world-projection ^master --oneline
```

9 个 commit（M0 文档 → M1-M5 实现 → M6-M8 联动 → M9 增量 → M10 性能 → M11 回放基础 → 视觉验证）。