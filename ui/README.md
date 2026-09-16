# Mirage UI（M1.5 并行轨道）

产品 UI（Web 技术栈，[DEC-006](../docs/decisions/DEC-006-ui-web-frontend-packaging.md)）
与 runtime 核心并行开发的工程承载。当前状态见
[M1.5 里程碑计划](../docs/plans/m1.5-ui-parallel-track.md)。

## 结构

- `contracts/` — 协议 v1 wire 契约的 TypeScript 镜像（消息类型、编解码、framing、
  事件订阅语义）+ mock transport。纯 TS、零运行时依赖；与 C++ 端
  （`runtime/ipc`）的一致性由共享 golden vectors 测试锁定（`M1.5-01`）。
- `app/` — 前端应用（Vite + TypeScript，vanilla TS 视图层）。组件框架按计划在
  实现评审时定选；Vite/包管理器均为开发期暂定值，不构成 DEC-006 决策 6 的定案。
- `app/src/theme/` — 三层设计 token 与主题系统（M1.5-08，设计规范 §2）：L1 原始值
  （`primitives.ts`）与 5 套内置主题的 light/dark L2 值集（`themes.ts`）以 TS 为
  单一事实源，入口注入生成 CSS；`ThemeManager` 负责 `data-theme`/`data-mode`
  挂载、即时切换、localStorage 持久化（key `mirage.appearance`）与跟随系统。
  组件与样式只消费语义 token，私定颜色以测试清零。
- `app/src/components/` — 基础组件规格（StatusBadge 封闭色映射、StepCard、
  ActivityCard、ApprovalCard/SnapshotFrame 占位、Composer）。

仓库初始化时的 `ui/workspace`、`ui/tasks`、`ui/execution` 等占位目录对应产品视图
划分；M1.5-04 阶段这三个视图以模块形式落在 `app/src/views/`（`workspace.ts` /
`tasks.ts` / `execution.ts`）；M1.5-08 增补 `appearance.ts`（`#/settings/appearance`
外观设置页：主题库 + 明暗模式 + 组件占位预览）。其余占位目录仍留待后续里程碑
（workflow/overlay 属 M5 `SCOPE-06` 范围；统一壳迁入见 M1.5-07）。

## 开发命令

在 `ui/` 目录下（npm workspaces）：

```bash
npm install          # 安装依赖（Node >= 22）
npm run check        # 两包 tsc --noEmit
npm test             # vitest 单测（contracts 编解码 / mock / 事件语义 + app 主题与外观）
npm run build        # tsc + vite build
npm run dev -w @mirage/app   # 启动 Vite dev server（默认 mock transport）
```

## Mock 约定（仅 mock，非真实服务行为）

- mock 以 M1 真实服务可观察 wire 行为为模板：五态 host、progress 名称、稳定错误
  形状、每订阅 `seq` 单调递增、有界队列 drop-oldest + `events.overflow`。
- 唯一的 mock 专属约定：`process.execute` 步骤的 `arg` 以 `fail:` 开头时，该步骤
  在模拟执行后失败（exit code 1），用于演示错误视图。真实服务不会产生该行为，
  该约定不得出现在任何真实 IPC 路径。
- `eventsCapability: false` 选项可模拟不支持事件订阅的服务端（hello 无 `events`
  能力、subscribe 拒绝），用于验证前端降级轮询路径（`M1.5-05`）。
