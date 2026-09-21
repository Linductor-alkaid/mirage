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

## 视觉验证

受限于沙箱网络带宽（Playwright Chromium 下载需 ~187MB），本里程碑未在 CI 内做
Playwright 截图。手工验证步骤（开发机）：

```bash
# 终端 1
cd ui
npm run dev -w @mirage/app
# 浏览器打开 http://localhost:5173/#/world
```

期望：

1. 进入 `#/world` 后，页面显示「加载组织世界…」约 200-400ms，然后出现
   `<canvas>` 与顶部 HUD（FPS / Agents / t=…s / 模拟）。
2. 默认主题现代办公室风格；底部 28 个 agent 占位（4 Team × 7 Agent 区域 +
   中央 meeting hub）。
3. 模拟器启动后约 6 秒出现第一次 `collaboration.started`：三个 agent 头顶
   出现 status badge、meeting space 上方出现蓝色连接线。
4. 点击任意 agent → 右侧出现 AgentDetailPanel（角色 / 团队 / 视觉状态 /
   当前任务 / 当前活动）。
5. 左键 = orbit；右键 = pan；滚轮 = zoom；hover 切换 cursor（pointer vs grab）。

## 已知遗留

- 没有下载完整 chromium 截图；CI 视觉验证留给 M12+ 接入。
- Timeline UI / 历史回放控件尚未落地；WorldClock + ReplayEngine 已就位，
  M12 接入。

## 提交 hash

```
git log feat/m4-world-projection ^master --oneline
```

7 个 commit（M0 文档 → M1-M5 实现 → M6-M8 联动 → M9 增量 → M10 性能 → M11 回放基础）。