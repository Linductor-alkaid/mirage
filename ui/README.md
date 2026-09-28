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
- `app/src/state/` — `store.ts`（HarnessStore：路由、会话（DEC-025 起接
  `session.*` 契约路径，标题为展示层派生）、契约任务快照、观察流缓冲、
  桌面状态观察（DEC-026 `desktop.observe` 按需快照）、事件面，React
  `useSyncExternalStore` 绑定）+ `workflow-backend.ts`
  （DEC-023 IPC 适配器；DEC-026 起含 `workflow.get` 定义读取）+
  `workflow-ir.ts`（编辑器模型 ↔ IR v1 双向换算点）。
  模拟域（`harness-mock.ts`）自 M5-06 退役：会话页不再有演示叙事。
- `app/src/shell/` — 统一壳：壁挂屏 WallDisplay（状态动词 + 蓝线任务剖面 + 灯阵）、
  ActivityBar、StatusBar（cue 栏 + 紧急停止）、命令面板（Ctrl+K）、批准中心
  （M5-07 接线异步确认面，现为占位面板）、Toast。
- `app/src/views/` — 会话页（签派栏 SessionsSidebar：`session.list` 快照 + 派生
  标题分组；线程流 ThreadView：user/outcome 接会话面 + 步骤卡接任务快照；
  Composer 执行模式 + 对话模式降级呈现（模型循环接入前禁用，DEC-025 决策 3）；
  观察台 Observer：桌面状态面板（DEC-026 `desktop.observe` 按需快照，视觉组件
  显式请求）+ 运行时间线（任务快照）+ 观察流（session.turn/output/message
  事件尾随，trigger-lock））、工作流页（与
  harness 同壳：左栏工作流列表 + RPA 工程式编辑器——右栏原子动作库可拖入/
  属性/参数三 Tab，接口缝 `state/workflow-backend.ts`（M5-05 起为 DEC-023 IPC 适配器；M5-06 第二轮起经 `workflow.get` 跨会话回读定义，IR 双向映射见 `state/workflow-ir.ts`）、设置八类、资源占位。
  RPA 范式调研归档：`docs/research/rpa-editor-design-reference.md`。

### M1.5-04 视图迁移映射（功能等价）

Workspace 提交表单 → Composer 执行模式（目标 + 步骤构建器，`task.submit` 语义
不变）；Tasks 列表 → 会话侧栏分组 + 会话条目状态徽标；Execution 详情 → 观察台
（运行时间线直连 `task.inspect` 快照）与线程内工具调用卡；外观设置 → 设置·外观
（主题库卡片预览 + 明暗三态）。

## 开发命令

在 `ui/` 目录下（npm workspaces）：

```bash
npm install          # 安装依赖（Node >= 22，engines 声明见 package.json）
npm run check        # 两包 tsc --noEmit
npm run lint         # eslint（M5-01 定案的 lint 工具链：ESLint 10 +
                     #   typescript-eslint 8 + eslint-plugin-react-hooks 7）
npm test             # vitest 单测（contracts 编解码 / mock / 事件语义 + app 主题、
                     #   store、mock 域、模拟器与 style-scan）
npm run build        # vite build（字体本地打包，无网络资源）
npm run dev -w @mirage/app   # 启动 Vite dev server（默认 mock transport）
```

工具链定案（M5-01，DEC-006 决策 6）：npm + Vite + React 19 + vitest + ESLint
复核确认，无迁移。供应链耦合：`ui/package-lock.json` 的 sha256（LF 规范化
内容，与提交 blob 一致）登记于仓库根 `dependencies.lock.json`（schema v2
`frontend` 条目），**每次 configure 重算比对**——改动前端依赖必须在同一变更
中更新该哈希，否则构建失败（CI 以 `npm ci` 安装防漂移）。

## 真实服务联调（M1.5-03 dev bridge）

`mirage-devbridge` 在浏览器 WebSocket 与真实 `mirage-service` 的 Unix socket 之间
做双向帧透传（DEC-012 决策 6）：一条 WebSocket binary 消息恰承载一个完整帧
（含 4 字节长度前缀），载荷逐字节不改写；帧编解码复用
`contracts/src/framing.ts` 的 `makeFrame`/`tryExtractFrame` 语义。

```bash
# 终端 1：启动真实服务（授予一个读目录供 filesystem.read 步骤使用）
./build/debug/apps/mirage-service --socket /tmp/mirage-dev/service.sock \
    --read-root $HOME --no-recovery
# 终端 2：启动桥（默认 127.0.0.1:8787，--port 0 可取随机端口）
./build/debug/apps/mirage-devbridge --socket /tmp/mirage-dev/service.sock --port 8787
```

浏览器连接 `ws://127.0.0.1:8787/`，即可对真实服务完成 hello、订阅与任务
submit/inspect 往返（验收截图
[docs/verification/m1.5-03-browser-e2e.png](../docs/verification/m1.5-03-browser-e2e.png)）。
桥是开发期工具，不进产品安装包，SIGINT 干净停止。

## transport 选择（M1.5-05）

app 入口按 URL 参数装配 transport，视图层不感知（同一 `MirageTransport` 面）：

- 默认：mock transport（无后端依赖）。
- `?transport=bridge`：经 dev bridge 连接真实 `mirage-service`（默认
  `ws://127.0.0.1:8787/`）；`?ws=<url>` 可指定桥地址（隐含 bridge 模式）。
  连接断开后按有界退避（500ms→8s）自动重连，成功后重置事件 seq 基线并
  resync 任务快照；`contracts/src/ws-transport.ts` 实现 DEC-007 单未决请求
  纪律（请求逐条串行、30s 显式超时），事件 seq 跳跃 / `events.overflow`
  触发快照 resync（`EventSequencer`）。
- `?events=off`（仅 mock）：构造无 `events` 能力的 mock 服务，用于在浏览器
  内验收「订阅不可用 → 自动降级 `task.inspect` 轮询」路径；真实服务未广告
  `events` 能力或 `events.subscribe` 返回 `unsupported` 时走同一降级逻辑。
- Mirage 桌面壳内（M5-02）：自动选择 `DesktopBridgeTransport`
  （`contracts/src/desktop-transport.ts`）——壳的受控 CEF bridge 在页面脚本
  运行前注入 `window.mirageQuery`，其存在即选中（显式 `?transport=desktop`
  亦可）。一条 query 携带一个协议 v1 请求封装并应答一个响应封装（bridge 在
  browser 进程恢复渲染器侧关联 id）；服务事件经 `__mirageOnEvent` 钩子、
  会话丢失经 `__mirageConnectionLost` 钩子推送。壳进程侧装配见
  `apps/desktop/`（browser 进程经 `runtime/ipc` 直连 Local IPC，DEC-006：
  IPC 契约是 UI 与 C++ 的唯一耦合面）。

```bash
# 终端 3：app 以真实传输启动（Vite dev server 默认 5173）
npm run dev -w @mirage/app
# 浏览器打开 http://localhost:5173/?transport=bridge
```

## Mock 约定（仅 mock，非真实服务行为）

- mock 以 M1 真实服务可观察 wire 行为为模板：五态 host、progress 名称、稳定错误
  形状、每订阅 `seq` 单调递增、有界队列 drop-oldest + `events.overflow`；
  工作流面（DEC-023/DEC-024）与会话面（DEC-021，M5-06 起消费）同为 wire 忠实
  镜像——容量 / 字节预算拒绝、历史窗口与 `truncated`、`session.*` 事件发布点
  与真实服务一致。
- `process.execute` 步骤的 `arg` 以 `fail:` 开头时，该步骤在模拟执行后失败
  （exit code 1），用于演示错误视图。
- 会话页模拟域已退役（M5-06，DEC-025）：会话 / 消息 / 工作流全部为契约路径；
  `approve:` 演示审批与快照卡随之移除，批准中心属 M5-07 真实批准面。会话删除
  已接 `session.close`（M5-06 第三增量，DEC-026 挂账②兑现：主会话被服务端以
  `invalid_state` 拒绝并如实呈现，关闭后本地记忆随注册表事实收敛）；重命名 =
  产品别名（M5-08）、导出 = 客户端投影、fork 不立项（DEC-026 决策 5），UI 不
  呈现。
- 观察面（M5-06 第二轮，DEC-026）：mock `desktop.observe` 返回确定性模拟桌面
  数据（wire 形状与真实服务一致；视觉对按请求返回）；无事件形态——桌面状态为
  按需快照，观察流仍为会话事件尾随。
- `eventsCapability: false` / `workflowsCapability: false` /
  `sessionsCapability: false` / `observationCapability: false` 选项（对应
  `?events=off` / `?workflows=off` / `?sessions=off` 等）可模拟缺能力位的服务
  端，用于验证前端降级路径。
