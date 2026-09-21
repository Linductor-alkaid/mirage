# 调研：ZCode 源码级架构与 UI 设计经验（zai-org/ZCode）

> 状态：Completed
> 日期：2026-09-21
> 负责人：Mirage 维护者
> 调研方式：源码级核实。克隆 zai-org/ZCode 公开快照至本地，按「agent 运行时、
> 通信层、桌面/Web 应用层、computer-use 与工程实践」四个方向并行通读后交叉汇总；
> 本文断言均附 ZCode 仓库内文件路径（相对其仓库根）。公开快照为单条压缩提交，
> 绝大部分测试与 CI 配置被剥离，治理规则的实际落地程度不可据此评判——全文按
> 「设计意图」读取。
> 关联决策：[DEC-005](../decisions/DEC-005-desktop-observation-contract.md)、
> [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)、
> [DEC-010](../decisions/DEC-010-m1-permission-framework.md)、
> [DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)、
> [DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)、
> [DEC-014](../decisions/DEC-014-ui-component-stack.md)
> 输出消费方：[前端设计规范 §2.3（本文触发修订）](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)、
> DEC-012 的 M2+ 消息事件面演进、DEC-005 的引用解析纪律、M3 壳产品化输入、
> 工程规范治理工具化评估
> 范围裁剪：pinned mira / mirador 已交付能力（executor 并发原语、agent 控制面、
> JSON 编解码、视觉感知算法）的实现模式**不在学习范围**，理由见 §1.2；本文只
> 固定 Mirage 自研决策面上的经验。

## 1. 调研目标、方法与范围裁剪

### 1.1 对象

ZCode 是 Z.ai 开源的 coding agent harness：Electron 桌面应用 + 浏览器工作台 +
终端 CLI 三端共享一份 TypeScript monorepo（`apps/zcode-cli` 子 workspace 承载
agent 运行时与 TUI；`packages/*` 承载 rpc / server / services / client / shared /
ui / web / desktop / zcode-cua 等）。选它作对照的原因：与 Mirage 同为
「桌面壳 + agent 运行时 + 本地服务」形态的已发货产品，其 computer-use 模块与
Mirage 的 Desktop Environment 域高度同构，且其开源约定（AGENTS.md、
architecture-policy.yaml、DESIGN.md）与 Mirage 的工程规范属同一类型，可直接
对照校准。

### 1.2 范围裁剪（Mira / mirador 职责部分不学）

按仓库 AGENTS.md 能力路由，ZCode 的以下部分对应 pinned 依赖已交付能力，本文
不提炼实现模式，仅在涉及契约面语义时引用：

| ZCode 部分 | 对应 Mirage 依赖职责 | 处理 |
| --- | --- | --- |
| 并发任务/定时/取消/进程内通信（自建 worker 池、事件循环） | executor（mira 交付） | 不学实现；只看事件分发在契约上的语义（§5） |
| turn 状态机、工具调度器、subagent 生命周期、权限决策引擎内部、上下文 compact、工作流引擎 | MiraRuntime / agent 侧 | 不学 |
| JSON 编解码与帧序列化细节 | mira/json | 不学 |
| 视觉感知算法（变化检测、OCR、SoM 渲染） | mirador | 不学；§7 只取观察契约纪律 |

纳入范围（Mirage 自研决策面）：UI 设计系统（§3）、前端应用架构（§4）、IPC 事件
协议语义（§5）、桌面壳进程工程（§6）、观察与引用解析契约纪律（§7）、工程治理
（§8）。

## 2. 仓库全景

进程拓扑（后文引用的坐标系）：Electron **main**（窗口/托盘/更新，`packages/desktop/src/main/`）
→ 每窗口一个 **host** utilityProcess（本地服务 + SQLite + agent 子进程管理，
`packages/desktop/src/host/`）→ **agent sidecar** 子进程（`app-server --stdio`，
stdio NDJSON 协议，`packages/services/src/zcode-agent/`）；另有一个常驻
**scheduler** utilityProcess（cron/闲时任务认领，`packages/desktop/src/scheduler/`）。
renderer 只经 MessagePort 与 host 通话（`packages/client/src/messageport.ts`）；
Web 形态把同一棵 UI 树接到 WebSocket（`packages/web/src/main.tsx`）。

与 Mirage 的形态对照：Mirage 为 CEF 多进程壳 + 独立 Runtime Service 进程
（DEC-006/DEC-007），耦合面是 Local IPC——ZCode 的「host 聚合服务 + sidecar
agent + 传输无关协议」与之同构，差异在 ZCode 的 agent 与服务同宿主机进程树。

## 3. UI 设计系统

### 3.1 设计规范即 AI 可执行约束

ZCode 根 `DESIGN.md`（537 行）自述 "meant for coding agents"，把最高优先级约束
放在文件头（`text-ui-*` 字号纪律："Treat violations of this section as
design-system defects, not stylistic preferences"），规则全部用可判定语言
（must/never/禁用值清单），例外一律 token 化（`--text-ui-2xs` 仅限工作流图轴、
`--text-mobile-input-safe` 16px 防 iOS 聚焦缩放），并在 AGENTS.md 挂"改 UI 前
必读"。强制方式是文档 + 组件默认值 + 既有用例惯性（约 2000 处 `text-ui-*`），
无 lint。Mirage 仓库根 `DESIGN.md` 已是同模式实现事实源；可借鉴的增量是
「最高优先级约束前置 + 违规定性为缺陷 + 例外必须 token 化并注明适用范围」的
写法纪律。

### 3.2 单变量字号缩放

用户字号只允许经一个基准变量缩放：`--ui-font-size`（默认 14px，钳位 12–20px，
`packages/ui/src/lib/uiFontSize.ts`），六档字阶全部 `calc()` 派生（如
`--text-ui-lg: calc(var(--ui-font-size) + 2px)`，`packages/ui/src/styles.css`
143–155 行）；注释明确禁止改 `html` 根字号，"避免根 font-size 连带缩放图标、
间距和圆角"——字号与几何（rem 基的间距/圆角）解耦。CEF 场景下壳侧只需
`ExecuteJavaScript` 设置一个 CSS 变量即可实现用户字号偏好。

### 3.3 主题机制对照

- **双轴叠加**：`.dark` 管明暗变量基座，`.theme-zai-*` 管品牌值集覆盖，两 class
  同时挂 `<html>`（`useTheme.ts` `applyTheme()`）；主题只替换语义层取值。与
  Mirage §2.6 的 `data-theme` + `data-mode` 双轴同构，互为印证。
- **偏好规范化**：localStorage 旧值经 `normalizeThemePreference` 统一收敛
  （`light→zai-light`、`dark→zai-dark`），注释点名目的是"避免旧 hook 兜底值和
  store 默认值分叉"——Mirage 多主题演进到第 3 套以上时需要同类机制。
- **跨窗口同步白名单**：仅 `theme/locale/uiFontSizePx/interfaceMode` 四个字段走
  广播频道 + 回声抑制（`packages/ui/src/store/index.ts` `BROADCAST_FIELDS`）。
  Mirage 多窗口（M5）可复用该"白名单广播 + storage 事件兜底"口径。
- **宿主差异分流**：桌面 vibrancy 保持根透明，浏览器路径才写
  `color-scheme`/`theme-color` meta，按根节点属性判定（`useTheme.ts`
  `syncBrowserThemeSurface`）——CEF 壳与浏览器开发形态双支持时可直接照搬。

### 3.4 CJK 排版（本文唯一直接修订来源）

- **等宽栈必须显式插 CJK 回退**：ZCode 的 `--font-mono` 在通用 `monospace`
  兜底前插入"Microsoft YaHei / PingFang SC / Noto Sans CJK SC"，注释点名动机：
  "Windows 的 Consolas 不含中文字形，Tailwind 默认栈最终会让中文落到宋体"
  (`packages/ui/src/styles.css` 140–142 行)。**Mirage 两处同病**：规范 §2.3
  等宽栈与实现 `--mir-font-mono`（`ui/app/src/styles.css:20`，Chivo Mono /
  Cascadia Mono 均 无 中 文 字 形）都缺 CJK 回退——遥测/参数/日志中的中文在
  Windows 会落宋体。§2.3 已随本文修订；实现侧待随前端工作项同步。
- **中文按短语断行**：`.text-wrap-phrase` 启用 Chromium 的
  `word-break: auto-phrase`，避免中文模板按单字断行拆词（`styles.css` 68–79
  行）。CEF 即 Chromium，可直接使用。

### 3.5 视觉纪律补遗

- **圆角按嵌套计层**：半径取决于"可见圆角容器的嵌套深度"而非组件重要性，首个
  容器 `xl` 逐级降档；豁免清单封闭（聊天输入壳/状态浮板/Toast/品牌背板 4 处
  2xl）。写成文档规则而非逐组件硬编码，是圆角一致性的干净解法（`DESIGN.md`
  Radius 节）。
- **层级靠背景分层不靠阴影**："Background layering is usually more important
  than shadow strength"，阴影仅四级（无/边框主导/md/lg）。与 Mirage §2.2
  "阴影只两档"一致。
- **滚动条 token 化**：全局 14px、透明轨道、`background-clip: padding-box`、
  thumb 用 `--color-border`（`styles.css` 757–782 行），三平台明暗一致，可原样
  迁移。
- **平台差异经根节点 class 进 CSS**：`.platform-linux-desktop`、
  `.window-maximized` 等挂根节点，壳（C++ 侧）只负责切类，样式差异全部留在 CSS
  （`styles.css` 15–23 行；Linux 窗壳 16px 圆角包 12px 面板、最大化归零等）。

## 4. 前端应用架构

### 4.1 双缝注入：服务缝 + 平台缝

整棵 UI 树只依赖两个接口缝：`IServiceAccessor`（后端服务代理集合）与
`IPlatformService`（平台能力：目录选择/通知/更新/窗口控制，
`packages/shared/src/platform.ts:522`），经 React Context 下发
（`packages/ui/src/hooks/useServices.tsx`、`usePlatform.tsx`）。桌面入口给
MessagePort 实现、Web 入口给 WebSocket + ~160 行 stub 实现（能力降级 = stub
返回错误/null，`packages/web/src/main.tsx` `createWebPlatform`），UI 代码零
宿主分支。Mirage 已有服务缝实践（`ui/contracts` mock transport、
`WorkflowBackend` 接口缝，设计规范 §4）；**平台缝是缺的第二条**，M3 壳形态与
M1.5 浏览器形态共存时按此补齐，避免 `isDesktop` 类布尔蔓延。

### 4.2 高频事件投影用外部 store，不进组件状态库

会话时间线的唯一 reducer 是 `useSyncExternalStore` 兼容的外部投影 store
（`packages/ui/src/v4/conversationProjectionStore.ts`，1354 行）：按固定规则
消费协议帧（snapshot 原子替换、迟到帧静默丢弃、delta 应用后推进 seq）。普通
zustand store 只管低频偏好，跨窗口仅广播白名单字段。这是 DEC-014 决策 7
（"线程层以自有事件投影 store 实现"）的成熟参照实现：**流式帧不经过 React
状态库，投影收敛在数据层，组件只读投影**。

### 4.3 命令通道幂等对账（M2+ 消息面的完整样板）

发送链路（`packages/ui/src/v4/SessionPane.tsx` `handleSendText`、
`commandFactory.ts`、`pendingCommandRegistry.ts`）：

1. `commandId = uuidv7` + 持久化 `clientId`（localStorage，服务端幂等表依据）；
   CAS 类命令强制携带 `baseRevision`；
2. 本地 optimistic overlay（`markCommandPending`）→ 发送 → ACK 结算；
3. ACK 后 2s watchdog（`expectAcceptedInputProjection`）防"确认后静默"；
4. 恢复线索存 localStorage（24h TTL）但**绝不自动重放命令**——只保存客户端
   恢复依据，重放永远由用户显式触发。

这对 Mirage M2+ 消息/命令面（设计规范 §4 前瞻依赖）是可直接采纳的语义集。

### 4.4 数据层资源治理

- 连接按 workspaceKey 租用 + 引用计数 + **30s keep-warm**：关面板只退订视图，
  会话在 agent 侧照跑（`packages/ui/src/v4/sessionDataLayer.ts`、
  `workspaceConnectionRegistry.ts`）。
- 终端 xterm/PTY 所有权上移到模块级单例，组件卸载只物理搬移 DOM 不杀进程，
  workspace tab 真正关闭才批量回收（`packages/ui/src/terminal/sidePaneTerminalSessionRegistry.ts`）。
- 工具卡渲染 = 注册表（identity 分流）+ 统一卡壳（折叠状态持久、auto-open、
  失败 tooltip）+ raw JSON 兜底卡，新增工具只加一个 renderer 文件
  （`packages/ui/src/ToolCallBlocks/resolveRenderer.ts`、`ToolLayout.tsx`）。
- 回放/分享 = 纯 re-export 导出桶 + 只读时间线组件，同一套时间线在静态数据上
  装配（`packages/ui/src/replay.ts`、`ConversationShareReadonlyTimeline.tsx`）
  ——在线/分享/回放三种数据源共用渲染层，测试与截图回放成本极低。
- 已知债务：核心文件过度集中（`SessionPane.tsx` 4834 行、
  `ConversationTimeline.tsx` 1963 行、`WorkspaceShellLayout.tsx` 1974 行，均带
  豁免注释自认"先收口"）。借鉴分层思想时应把启动编排、滚动协调这类胶水拆薄。

## 5. IPC 事件协议演进输入（DEC-012 的 M2+ 消息事件面）

Mirage DEC-012 已冻结：事件是通知、`seq` 单调、跳跃/溢出/重连必须 resync、
快照是事实源。ZCode v4 协议（`packages/shared/src/zcode-protocol-v4/`）是同
一问题更进一步的形态，可作为 M2+ 消息流事件集设计时的**语义选项库**：

- **封闭增量操作集**：下行增量只有五种——`row.appended` / `row.upserted` /
  `row.removed(fromRowId 起全删)` / `row.delta`（仅流式态行）/ `state.updated`
  （键级整体替换）。刻意**不做** row.inserted/moved/字段级 JSON patch：表达不了
  的变化一律发 snapshot resync，压缩客户端错误面（`delta.ts` 53–69 行，
  `apply.ts` 是唯一裁判纯函数）。
- **行模型三规则**（`rows.ts` 1–3 行注释）：每行自包含（渲染任一行不需要其他
  行）；结构变化换整行、文本增长用 append；**turn 是行上的标签不是容器**。
  `rowId` 会话内单调、永不复用、是事件日志的确定性纯函数。快照 = 状态区 +
  **尾窗 rows**（`snapshotTailWindowRows: 60`），更早历史走游标分页
  （`rowsRange`，`beforeRowId`，单页上限 200）。
- **水位与恢复**：订阅携带 `base = {logEpoch, seq}`；服务端判
  `floorSeq <= base.seq <= currentSeq` 则从保留窗重放 `(base.seq, current]`，
  否则发全量快照（`conversation-topic-publisher.ts`，resumable 判据）。
  客户端纪律：**断档不猜**——`fromSeq !== seq` 即同订阅 resync（single-flight，
  带超时升级 forceSnapshot），订阅失效即 fresh connect 自愈，再不行 fail-closed
  进错误态（`packages/ui/src/v4/conversationProjectionStore.ts` 846、886–897 行）。
- **黄金不变量**：`apply(s, coalesce(ds))` 与逐条 apply 逐字节一致，且
  「快照(尾窗) + 续流 ≡ 全量重放」——恢复路径被一条不变量直接覆盖，无需单独
  验证（`conversation-topic-publisher.ts` 7–10 行注释；一致性由形式化模型背书，
  `packages/formal-proof/src/model.ts`）。Mirage 落地时可将同款不变量写进
  golden vectors。
- **coalesce 语义保持合并**（`coalesce.ts` 1–10 行注释即规范）：相邻同键 delta
  拼接；upserted 吞同 row 更早 delta；**removed 是屏障，任何合并不跨越**；合并
  规则与服务端 flush 管线共用同一份纯函数。另有 services 层 1.5s/96 条时间窗
  coalescer（`zcodeSessionEventCoalescer.ts`）做吞吐降频——**正确性合并在协议
  层（纯函数），吞吐合并在传输层（时间窗），两层分工不共享代码**。
- **背压分层纪律**：丢弃/降级发生在通道层、进 `send()` 之前；已接 send 的帧
  绝不丢。与 DEC-012 决策 5（drop-oldest 有界队列 + overflow 显式事件）同向，
  可作为其 M2+ 扩展口径。
- **两档 delivery profile**：桌面 30ms flush 全量流式；Web/回放 150ms 且仅文本
  流式（工具进度降频）（`core.ts` 34–59 行）。profile 变量禁止出现在客户端代码。
- **反面清单**（Mirage 已规避或应继续规避）：RPC channel `call(command: string)`
  弱类型、服务签名漂移编译期不报错（`rpc/src/channels.shared.ts`）；新增服务要
  手工同步 4–5 处；新旧协议长期并存（旧协议 ~257 导出"待整体删除"）；单文件
  5646 行服务巨石。DEC-012 的「schema 文档事实源 + golden vectors 双端锁定」
  路线确认为正确方向，扩展事件集时必须继续同变更内三处同步。

## 6. 桌面壳与进程工程（M3 产品化输入）

壳 PoC 已冻结 CEF（DEC-006，2026-09-21）。以下为壳从 PoC 走向产品时的进程
工程要点，全部出自 ZCode 已发货实现：

- **壳薄核独立**：main 只管窗口/托盘/更新/deep link；服务聚合在独立 host 进程；
  agent 是 host 的子进程（sidecar）。崩溃隔离边界清晰：host 退出是"fail-hidden
  权威边界"，main 立即按 host 已死处理，不依赖垂死进程补发状态
  （`packages/desktop/src/main/desktopHostProcess.ts` 597–614 行）。
- **孤儿防护**：main 强杀 host 前必须留足其清理子进程树的时间——强制
  `forceKillDelay ≥ 3.5s`，否则 agent 子进程被 init 接管成孤儿
  （同文件 646 行）。Mirage 杀 Runtime Service（其下可能挂桌面动作子进程）时
  同理。
- **健康检查 = 请求 watchdog，不做 ping**：默认请求超时 3 分钟，超时即认定
  协议链路不可信、淘汰整棵进程树；控制面请求（cancel 类）豁免
  （`packages/services/src/zcode-agent/zcodeAgentProcessManager.ts` 48、
  1276–1311 行）。
- **代际 fencing**：每次重启递增 `generation`，旧代际的迟到 ACK/帧整体作废；
  "重启"事件只在真实 spawn 后发，防"失败启动→假重启→重连"自激风暴
  （同文件 1137–1145 行）。DEC-012 事件面引入重连语义时需要同类标记。
- **数据目录自举顺序**：最早启动阶段先读用户设置中的数据目录，再初始化
  logger/crash reporter，避免日志与崩溃转储分裂两套路径
  (`packages/desktop/src/main/desktopEarlyDataBaseDirBootstrap.ts`)。Mirage M5
  设置页提供数据目录迁移时按此顺序。
- **优雅退出的进程树口径**：先关 stdin（对端以 EOF 为正常退出边界，等待有限
  时长）→ SIGTERM → SIGKILL 兜底，每阶段独立 deadline，Windows 用绝对 deadline
  保证落在壳的退出预算内（`zcodeStdioTransport.ts` 128–207 行）。
- **任务认领多进程安全**：scheduler 多进程认领用 SQLite `BEGIN IMMEDIATE`
  事务内原子 `UPDATE running 0→1`；10 分钟僵尸认领回收兜底进程崩溃丢内存态；
  misfire 5 分钟宽限，错过的一次性任务直接终结不补跑（不给用户 Surprise）
  (`packages/services/src/session/automationRepo.ts` 39–40、689 行起)。与
  Mirage 未来 off-peak/定时任务面相关。

## 7. 桌面观察与引用解析纪律（DEC-005 / Platform Backend 契约面）

ZCode computer-use 模块与 Mirage「Semantic + Visual 快照 → ElementReference /
VisualReference → 动作解析」高度同构，其契约纪律可直接固定为 DEC-005 的 M2+
演进输入（视觉算法面归 mirador，不在范围）：

- **三通道观察**：认证截屏帧（帧完整性认证元数据 + 内联 200KB 上限、超出走
  引用）+ 无障碍结构化状态（`get_app_state` 返回 `state_id` +
  `elements[*].index`，动作用索引定位）+ 应用身份快照（pid→identity）
  （`packages/zcode-cua/frame-contract.d.ts`、`apps/zcode-cli/packages/bootstrap/src/zcode-protocol-v4/cua-app-snapshot.ts`）。
- **坐标纪律**：坐标必须是整数像素且落在返回 raster 的 width/height 内；**禁止
  拿显示器 bounds 当像素坐标**（`node-repl-host/src/tool-contract.ts` 工具描述
  原文）。VisualReference 解析应写进契约。
- **防过期观察**：截屏媒体失效时把图像替换为文本"先重新截屏，禁止发送坐标
  目标"（`core/src/runtime/helpers/official-cua-media.ts`）——观察过期即作废、
  强制重观察，正是 Mirage「恢复前重新观察」语义（AGENTS.md Takeover 节）的
  工程化形态。
- **身份解析不猜测**：目标应用按 pid 精确解析；按名称/bundle 仅在唯一匹配时
  可用，注释明确"不能猜测具体进程身份"（`cua-app-snapshot.ts`）。
- **权限状态结构化四态**：`granted/stale/denied/unknown`，平台权限探测独立成
  服务、stale 态提供重启恢复路径（`packages/zcode-cua/request-access-contract.d.ts`、
  `broker.d.ts`）。Mirage Platform Backend 的 Linux/Windows 权限面（如
  Wayland 截图授权、Windows UIA）按四态建模，不用布尔。
- **敏感能力默认关闭 + 显式开启**：computer-use 插件不声明 defaultEnabled，
  需用户在设置页显式开启，长注释记录理由；子代理上下文中禁用
  (`bootstrap/src/app/official-plugin-definitions.ts` 328–365 行)。与 DEC-010
  权限框架的默认拒绝取向一致。

## 8. 工程治理与 AI 协作

- **架构策略机器可执行**：`architecture-policy.yaml` 声明模块 id/roots/依赖
  （requires）/公共入口（publicEntrypoints）/分层方向（layerOrder）/owner，
  全局规则 `maxFileLines: 400 / maxContractLines: 300 / maxPublicMethods: 12 /
  forbidCycles / forbidDeepImports`；检查器用 TS AST 真解析 import 与契约方法数
  （`scripts/architecture/`，约 600 行）；已接受违规以 sha256 指纹存
  `.architecture-baseline.json`，**CI 永不自动刷新基线**（只允许人工
  `baseline:update`）；例外支持带过期日；`--changed` 模式对改动文件做反向依赖
  闭包检查。配套 `.agents/skills/architecture-governance/` 教 AI 代理"改动前跑
  `architecture:check --changed`、用 `architecture:context <module>` 生成有界
  阅读包"。Mirage 工程规范目前是纯文档，这套「声明 + AST 检查 + 指纹基线 +
  过期例外」是 C++ 侧（libclang 或 include-what-you-use 级工具）可等价实现的
  演进候选，成本收益在 M3 后评估。
- **基线新鲜度闸门**：开工前脚本检查工作树落后远端即失败
  (`scripts/check-workspace-freshness.mjs`)，注释记录事故："曾落后 main 140
  提交导致在旧架构上开工"。Mirage 可作为工程规范 10 节（Git 纪律）的补充
  建议。
- **术语表模式**：根 `CONTEXT.md` 是插件商店域的 DDD 词汇表，每个术语带定义 +
  `_Avoid_`（禁用叫法），AGENTS.md 要求改该域前必读。Mirage 桌面域术语
  （观察快照/引用/Provider/Takeover）密度已够，值得同类固化。
- **治理不落地的样子**（反例，来自同一仓库）：策略文件里仅 2 个模块
  `managed: true`，其余 `managed: false`；某模块声明的 `contract.ts` 公共入口
  并不存在；多处巨石文件以 `eslint-disable max-lines` 自我豁免。教训：**治理
  规则若不从最少模块开始真实闭环（含基线与例外流程），声明本身就退役成文档
  装饰**。

## 9. 对 Mirage 的设计输入（固定清单）

| # | 输入 | 落点 |
| --- | --- | --- |
| I-1 | 等宽字体栈显式 CJK 回退（Consolas/Cascadia/Chivo 均无中文字形） | 设计规范 §2.3（本次已修订）；实现侧 `--mir-font-mono` 待前端工作项同步 |
| I-2 | 中文正文短语断行 `word-break: auto-phrase`（CEF 即 Chromium） | 设计规范 §2.3（本次已修订） |
| I-3 | 字号缩放经单一基准变量 calc 派生，禁改根字号 | 设计规范 §2.3（本次已修订） |
| I-4 | 平台缝（IPlatformService 等价物）与服务缝并列注入，宿主差异 stub 化 | 设计规范 §4 / M3 壳工作项 |
| I-5 | 消息事件面采用封闭增量操作集 + 行模型三规则 + 尾窗快照 + 游标分页 | DEC-012 M2+ 扩展（未决，届时开工作项） |
| I-6 | 水位 `{epoch, seq}` + 断档不猜 + 同订阅 resync + 「快照+续流 ≡ 全量重放」写进 golden vectors | DEC-012 M2+ 扩展 |
| I-7 | 命令幂等对账：uuidv7 commandId + 持久 clientId + CAS baseRevision + ACK watchdog + 不自动重放 | M2+ 消息面工作项输入 |
| I-8 | 壳产品化：孤儿防护强杀延迟、请求 watchdog 健康判定、代际 fencing、数据目录自举顺序、进程树三段退出 | M3/M5 壳工作项输入（DEC-006 后续） |
| I-9 | 观察契约纪律：坐标限返回 raster 内、过期观察强制重观察、应用身份不猜测、权限四态建模 | DEC-005 M2+ 演进 / Platform Backend 工作项 |
| I-10 | 高频事件投影用外部 store；跨窗口同步白名单广播 | 前端实现约定（DEC-014 决策 7 的实现参照） |
| I-11 | 架构治理机器化（依赖/入口/分层/规模上限 + 指纹基线 + 过期例外）与基线新鲜度闸门 | 工程规范演进候选（M3 后评估） |
| I-12 | 滚动条 token 化、平台差异根节点 class、多窗口主题偏好规范化 | 前端实现约定（随 DESIGN.md 演进） |

## 10. 与现有文档的关系

- 本文是[前端设计规范 §2.3 修订](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  （I-1/I-2/I-3）的证据基础；与
  [2026-09-16 前端调研](2026-09-16-agent-harness-rpa-frontend-survey.md)
  互补——该文覆盖公开资料级的前端信息架构，本文覆盖源码级的协议语义、进程
  工程与设计系统实现。
- I-5/I-6/I-7 是 DEC-012 已冻结纪律（通知语义、seq resync、快照事实源）的
  M2+ 扩展选项，**不构成对 DEC-012 的变更**；届时以工作项 + 决策修订流程进入。
- I-9 与 DEC-005 已冻结的 schema v1.0 不冲突，属引用解析纪律与权限状态建模的
  补充输入。
- 源码快照位置：`/tmp/ZCode-study`（调研工作副本，不入库）。
