# Mirage UI（M1.5 并行轨道）

产品 UI（Web 技术栈，[DEC-006](../docs/decisions/DEC-006-ui-web-frontend-packaging.md)）
与 runtime 核心并行开发的工程承载。当前状态见
[M1.5 里程碑计划](../docs/plans/m1.5-ui-parallel-track.md)。

## 结构

- `contracts/` — 协议 v1 wire 契约的 TypeScript 镜像（消息类型、编解码、framing、
  事件订阅语义）+ mock transport。纯 TS、零运行时依赖、框架无关；与 C++ 端
  （`runtime/ipc`）的一致性由共享 golden vectors 测试锁定（`M1.5-01`）：事实源
  [mirage-ipc-protocol-v1.md](../docs/design/mirage-ipc-protocol-v1.md)，向量
  `../tests/runtime/data/ipc_protocol_golden.json`，任一端漂移即测试失败。
- `app/` — 前端应用（Vite + React 19，组件栈见
  [DEC-014](../docs/decisions/DEC-014-ui-component-stack.md)）。视觉世界为
  「任务控制台」（Mission Console），设计系统实现事实源：仓库根
  [DESIGN.md](../DESIGN.md) 与 `.impeccable/design.json`。
- `app/src/theme/` — 三层设计 token 与主题系统（M1.5-08，规范 §2）：L1 原始值
  （`primitives.ts`，含 `--mir-dur-*`/`--mir-lamp-*` 动效分层）与 6 套内置主题
  （默认「任务控制台」，light=白班 / dark=夜班）的 light/dark L2 值集
  （`themes.ts`）以 TS 为单一事实源，入口注入生成 CSS；`ThemeManager` 负责
  `data-theme`/`data-mode` 挂载、即时切换、localStorage 持久化与跟随系统。
  组件与样式只消费语义 token，私定颜色以测试清零（style-scan）。
- `app/src/state/` — `store.ts`（HarnessStore：路由、会话、契约任务快照、审批、
  事件面，React `useSyncExternalStore` 绑定）+ `harness-mock.ts`（模拟域：会话
  叙事 / 演示审批 / 工作流，全部有界并标注「模拟」）。
- `app/src/shell/` — 统一壳：壁挂屏 WallDisplay（状态动词 + 蓝线任务剖面 + 灯阵）、
  ActivityBar、StatusBar（cue 栏 + 紧急停止）、命令面板（Ctrl+K）、批准中心、Toast。
- `app/src/views/` — 会话页（签派栏 SessionsSidebar、线程流 ThreadView、Composer
  对话/执行双模式、观察台 Observer：运行时间线 + 观察流 trigger-lock + 上下文
  占用；工作流以 WorkflowCallCard 呈现为 agent 可调用的工具）、工作流页（与
  harness 同壳：左栏工作流列表 + RPA 工程式编辑器——右栏原子动作库可拖入/
  属性/参数三 Tab，接口缝 `state/workflow-backend.ts`）、设置八类、资源占位。
  RPA 范式调研归档：`docs/research/rpa-editor-design-reference.md`。

### M1.5-04 视图迁移映射（功能等价）

Workspace 提交表单 → Composer 执行模式（目标 + 步骤构建器，`task.submit` 语义
不变）；Tasks 列表 → 会话侧栏分组 + 会话条目状态徽标；Execution 详情 → 观察台
（运行时间线直连 `task.inspect` 快照）与线程内工具调用卡；外观设置 → 设置·外观
（主题库卡片预览 + 明暗三态）。

## 开发命令

在 `ui/` 目录下（npm workspaces）：

```bash
npm install          # 安装依赖（Node >= 22）
npm run check        # 两包 tsc --noEmit
npm test             # vitest 单测（contracts 编解码 / mock / 事件语义 + app 主题、
                     #   store、mock 域、模拟器与 style-scan）
npm run build        # vite build（字体本地打包，无网络资源）
npm run dev -w @mirage/app   # 启动 Vite dev server（默认 mock transport）
```

## Mock 约定（仅 mock，非真实服务行为）

- mock 以 M1 真实服务可观察 wire 行为为模板：五态 host、progress 名称、稳定错误
  形状、每订阅 `seq` 单调递增、有界队列 drop-oldest + `events.overflow`。
- `process.execute` 步骤的 `arg` 以 `fail:` 开头时，该步骤在模拟执行后失败
  （exit code 1），用于演示错误视图。
- 会话内权限请求卡（`approve:` 前缀参数触发）与快照卡为 `app/src/state/`
  模拟域的**演示语义**（M2+ 权限/消息事件面前），界面标注「模拟」，不影响
  协议层任务走向；会话/工作流管理面尚无 IPC（设计规范 §4 前瞻依赖）。
- `eventsCapability: false` 选项可模拟不支持事件订阅的服务端，用于验证前端
  降级轮询路径（`M1.5-05`）。
