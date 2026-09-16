# Mirage 前端设计规范与信息架构

> 状态：Accepted（依据 [DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)）
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 调研依据：[Agent Harness 与 RPA / Workflow 前端设计经验](../research/2026-09-16-agent-harness-rpa-frontend-survey.md)
> 上位文档：[《Mirage：Linux - Windows 桌面端设计方案》](Mirage%EF%BC%9ALinux%20-%20Windows%20%E6%A1%8C%E9%9D%A2%E7%AB%AF%E8%AE%BE%E8%AE%A1%E6%96%B9%E6%A1%88.md)
> 第 13/17 节（本文细化并修订其 UI 组织口径）
> 关联决策：[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)（技术路线与
> 分发）、[DEC-012](../decisions/DEC-012-ipc-event-subscription-and-wire-schema.md)
> （IPC 事件面）
> 适用范围：`ui/` 全部前端形态（M1.5 浏览器形态、M3 壳内形态、M5 产品化形态共用
> 本规范的 token、组件与信息架构）

## 1. 定位与设计原则

Mirage 前端的产品叙事：**首先是 agent harness，其次是 workflow**。会话（chat）是
用户与 agent 协作的默认入口；workflow 是与之一级的编排能力，不是附属视图。二者在
统一应用壳中并存，共享一套风格、一套证据形态（桌面观察快照）与一套安全模型。

派生原则（编号供文档引用）：

- **P1 Harness 优先**：应用落地页是会话；任何任务的发起、观察、批准与回滚都应能
  在会话流内完成。
- **P2 Workflow 同级**：工作流库/编辑器/运行是一级导航，与会话平级；会话内可生成、
  引用、跳转工作流，编辑器内可发起"让 agent 修复"。
- **P3 证据先行**：桌面观察快照（语义 + 视觉）、元素引用、步骤验证结果是一等运行
  数据，贯穿会话活动卡、运行详情与编辑器步骤详情；JSON 只是降级视图。
- **P4 安全执行内嵌**：计划/执行分离、打断式批准、检查点回滚内嵌在消息流与运行
  时间线中，不是独立管理页。
- **P5 插槽化扩展**：侧栏区块与设置分类为 Skill / MCP / Provider 预留可注册插槽，
  不硬编码。
- **P6 草稿与运行态分离**：agent 或人对工作流的修改先落草稿，显式发布才影响运行；
  发布动作需要人工确认。
- **P7 双端一致**：M1.5 浏览器形态与后续 CEF 壳形态使用同一 token 与组件规范，
  不出现两套视觉语言。

## 2. 统一风格与设计规范

### 2.1 设计基调

**专业、安静、证据先行。** 低饱和中性底色 + 单一强调来源；状态色（成功/警告/危险/
信息）只用于状态语义，不做装饰。界面密度中高（桌面生产力工具），圆角中等，阴影
极轻，以 1px 边界划分区域。

基调由**主题**承载：默认主题「晨蓝」以品牌蓝为唯一强调色（本节基准值即该主题的
值）；产品提供多套风格化内置主题供用户选择（机制、清单与约束见 §2.6）。任何主题
都必须保持本节的状态语义封闭与对比度门槛——主题改变的是气质，不是语义。

### 2.2 设计 Token（三层）

三层架构：**primitive（原始值）→ semantic（语义角色）→ component（组件变量）**。
语义层命名与 shadcn/ui CSS 变量约定兼容（`--background`/`--primary`/`--radius`…），
未来组件框架定案（DEC-006 决策 6，M5）若选择 React + Tailwind + shadcn，token 可
1:1 映射；当前 vanilla TS 实现直接消费同一批 CSS 自定义属性。

主题形态：`light` / `dark` / `follow system` 三态，默认跟随系统。以下为规范基准值
（实现时以此为准，微调需经设计评审并更新本节）。

```css
/* ============ L1 primitive ============ */
:root {
  /* 中性色阶（ cool gray ） */
  --mir-gray-0: #ffffff;   --mir-gray-25: #fbfcfe;  --mir-gray-50: #f4f6f9;
  --mir-gray-100: #e9edf3; --mir-gray-200: #dde3ec; --mir-gray-300: #c6cedb;
  --mir-gray-400: #9aa4b2; --mir-gray-500: #66707d; --mir-gray-600: #4a5361;
  --mir-gray-700: #333c49; --mir-gray-800: #232b36; --mir-gray-900: #1c2530;
  --mir-gray-950: #12161c;
  /* 品牌蓝 */
  --mir-blue-50: #eef4fe; --mir-blue-100: #dce8fd; --mir-blue-300: #8db4f7;
  --mir-blue-500: #2563eb; --mir-blue-600: #1d4fd8; --mir-blue-400d: #4c8dff;
  /* 状态色 */
  --mir-green-500: #16a34a;  --mir-green-400d: #34d399;
  --mir-amber-500: #d97706;  --mir-amber-400d: #fbbf24;
  --mir-red-500: #dc2626;    --mir-red-400d: #f87171;
  --mir-sky-500: #0284c7;    --mir-sky-400d: #38bdf8;
  /* 间距（4 基） */ /* 半径 */ /* 字号阶 */
  --mir-space-1: 4px;  --mir-radius-sm: 6px;   --mir-text-xs: 12px;
  --mir-space-2: 8px;  --mir-radius-md: 8px;   --mir-text-sm: 13px;
  --mir-space-3: 12px; --mir-radius-lg: 10px;  --mir-text-base: 14px;
  --mir-space-4: 16px; --mir-radius-xl: 12px;  --mir-text-lg: 16px;
  --mir-space-5: 20px; --mir-radius-pill: 999px; --mir-text-xl: 18px;
  --mir-space-6: 24px;                          --mir-text-2xl: 20px;
  --mir-space-8: 32px;                          --mir-text-3xl: 24px;
  /* 动效 */
  --mir-dur-fast: 150ms; --mir-dur-slow: 250ms;
  --mir-ease: cubic-bezier(0.2, 0, 0, 1);
}

/* ============ L2 semantic（light） ============ */
:root {
  --background: var(--mir-gray-50);
  --foreground: var(--mir-gray-900);
  --card: var(--mir-gray-0);
  --card-foreground: var(--mir-gray-900);
  --popover: var(--mir-gray-0);
  --primary: var(--mir-blue-500);
  --primary-foreground: #ffffff;
  --secondary: var(--mir-gray-100);
  --secondary-foreground: var(--mir-gray-700);
  --muted: var(--mir-gray-100);
  --muted-foreground: var(--mir-gray-500);
  --accent: var(--mir-blue-50);
  --accent-foreground: var(--mir-blue-600);
  --destructive: var(--mir-red-500);
  --success: var(--mir-green-500);
  --warning: var(--mir-amber-500);
  --info: var(--mir-sky-500);
  --border: var(--mir-gray-200);
  --input: var(--mir-gray-200);
  --ring: var(--mir-blue-500);
  --radius: var(--mir-radius-lg);
  /* Mirage 扩展：壳层结构色 */
  --sidebar-background: var(--mir-gray-25);
  --surface-raised: var(--mir-gray-0);      /* 浮层/抽屉 */
  --overlay-scrim: rgb(18 22 28 / 45%);
  /* Mirage 扩展：证据色（观察快照标注） */
  --evidence-highlight: var(--mir-blue-500);
  --evidence-highlight-soft: rgb(37 99 235 / 20%);
}

/* ============ L2 semantic（dark） ============ */
.dark {
  --background: var(--mir-gray-950);
  --foreground: #e6eaf0;
  --card: #181d24;
  --card-foreground: #e6eaf0;
  --popover: #1c222b;
  --primary: var(--mir-blue-400d);
  --primary-foreground: #0b1220;
  --secondary: #232b36;
  --secondary-foreground: #c9d1dc;
  --muted: #232b36;
  --muted-foreground: #8b94a3;
  --accent: #1d2a44;
  --accent-foreground: var(--mir-blue-400d);
  --destructive: var(--mir-red-400d);
  --success: var(--mir-green-400d);
  --warning: var(--mir-amber-400d);
  --info: var(--mir-sky-400d);
  --border: #262d37;
  --input: #2a323e;
  --ring: var(--mir-blue-400d);
  --sidebar-background: #151a21;
  --surface-raised: #1c222b;
  --overlay-scrim: rgb(0 0 0 / 60%);
  --evidence-highlight: var(--mir-blue-400d);
  --evidence-highlight-soft: rgb(76 141 255 / 28%);
}

/* ============ L3 component（示例，组件节引用） ============ */
:root {
  --chat-bubble-user-bg: color-mix(in srgb, var(--primary) 8%, var(--card));
  --activity-card-bg: var(--card);
  --approval-border: var(--warning);
  --step-rail: var(--border);
  --statusbar-bg: var(--sidebar-background);
}
```

约定：

- 语义色禁止在组件里直接取 L1 原始值；一切经 L2 变量。
- `--radius` 全局基准 10px；控件 8px；气泡/徽标用 pill。阴影只允许两档：
  卡片 `0 1px 2px rgb(16 24 40 / 6%), 0 1px 3px rgb(16 24 40 / 10%)`，浮层再加
  `0 8px 24px rgb(16 24 40 / 16%)`。
- 状态色映射是**封闭语义表**（与 M1 冻结的状态集一一对应，组件规格见 2.5-7）。

### 2.3 排版与密度

- 界面字体：`system-ui, -apple-system, "Segoe UI", "PingFang SC",
  "Microsoft YaHei", "Noto Sans CJK SC", sans-serif`；等宽：
  `"JetBrains Mono", "SFMono-Regular", Consolas, "Noto Sans Mono", monospace`。
- 基准字号 14px；正文行高 1.6，界面行高 1.5，代码 1.45。字阶：12 / 13 / 14 / 16 /
  18 / 20 / 24。
- 数字、ID、路径、代码、token 用等宽字体。
- 密度两档（外观设置）：舒适（默认，控件高 32px，区块间距 16px）/ 紧凑（28px /
  12px）；全局字号缩放 90% / 100% / 110%。

### 2.4 动效与无障碍

- 动效两档：fast 150ms（hover/焦点/折叠）、slow 250ms（抽屉/弹层/路由过渡），统一
  `cubic-bezier(0.2, 0, 0, 1)`；`prefers-reduced-motion` 时全部降至 0ms。
- 正文对比度 ≥ 4.5:1，大字号与 UI 边界 ≥ 3:1；焦点环 2px `--ring` + 2px offset；
  全键盘可达：`Ctrl+K` 命令面板、`Ctrl+1/2/3` 切换活动栏页、`Ctrl+Tab` 会话切换、
  `Esc` 关闭浮层。
- 紧急停止按钮必须有 120ms 内的视觉反馈与二次确认，不得被浮层遮挡（P4）。

### 2.5 组件规格（核心集）

1. **ActivityBar（活动栏）**：常驻最左 56px。项 = 40×40 图标钮 + tooltip；选中态 =
   左侧 2px 品牌指示条 + 前景高亮；运行角标显示活动运行数；底部固定：主机状态灯
   （点击弹状态浮层）与设置入口。
2. **StatusBar（状态栏）**：常驻最下 26px，`--statusbar-bg`。左：Mira Host 五态 +
   服务连接；中：当前运行摘要（"执行中 · task-0007 · 步骤 2/5"）；右：默认模型、
   会话上下文用量、**紧急停止**（红色实心，二次确认）。
3. **会话条目（SessionsSidebar item）**：两行——标题（AI 摘要命名，未命名用目标
   前缀）+ 次行（相对时间 + 状态徽标）；hover 出现上下文菜单（重命名/置顶/fork/
   导出/删除）。分组：置顶 / 今天 / 7 天内 / 更早。
4. **消息组**：用户消息 = 右对齐卡片（`--chat-bubble-user-bg`，pill 化圆角 12px）；
   助手消息 = 全宽平铺 markdown（无气泡，可读性优先）；消息列最大宽度 760px 居中。
   流式渲染，光标块指示。
5. **ActivityCard（活动卡）**：工具调用 / 桌面动作 / 计划 / 工作流引用统一形态：
   标题行 = 类型图标 + 名称 + 状态徽标 + 耗时；正文默认折叠为一行摘要，点击展开
   （全局"展开全部细节"开关常驻消息流顶部）。桌面动作卡展开后首屏为
   **SnapshotFrame**。
6. **ApprovalCard（批准卡）**：打断式，等宽横幅；边框 `--approval-border`（按风险
   warning/destructive 分级）；按钮组 = 允许一次 / 本会话始终允许 / 拒绝；附范围
   说明（Provider×范围，DEC-010 语义）。
7. **StatusBadge（状态徽标）**：封闭映射——任务 progress：Idle 灰 / Active 蓝 /
   Paused 灰蓝 / Cancelling 琥珀 / Completed 绿 / Failed 红 / Cancelled 灰；步骤：
   pending 灰 / running 蓝(脉动) / ok 绿 / failed 红 / skipped 灰 / cancelled 灰；
   主机：running 绿 / starting・stopping 琥珀 / stopped 灰 / failed 红。禁止新色。
8. **SnapshotFrame（观察快照框）**：16:9 容器、圆角 8px、1px 边界；元素高亮框用
   `--evidence-highlight` 40% 叠加 + 标签 chip；配套"引用"标签页（元素树 / 属性 /
   JSON 三视图同步，OpenRPA Selector 窗口模式）。
9. **StepCard（工作流步骤卡）**：垂直序列，左侧序号轨道 + 类型 chip（ToolCall 蓝
   `--primary` / Navigate 天蓝 `--info` / Verify 绿 `--success` / Control 琥珀
   `--warning`）；卡体 = 标题 + 参数摘要 + 前置条件徽标 + 最近一次运行状态点；
   hover 出现拖柄、编辑、"让 agent 修复"。Control 回跳渲染为括弧（loop head 到
   当前步）。
10. **Composer（输入区）**：底部固定；顶行 = 模式切换（对话 / 执行 segmented）+
    模型选择器；主体多行自适应（3–8 行）；支持粘贴截图、`@` 引用；右下发送；
    运行中变为 停止（非紧急）+ 状态指示。
11. **RunDrawer（运行抽屉）**：右侧 360px 可折叠抽屉；垂直时间线（节点 = 步骤），
    实时观察流缩略图；点击步骤展开 SnapshotFrame 与输入/输出。
12. **CommandPalette（命令面板）**：`Ctrl+K` 居中弹窗；命令分组（导航/会话/工作流/
    设置/诊断）；搜索优先，最近使用置顶。
13. **设置表单**：左分类导航 200px + 表单区 max-width 640px；每组一张卡片；破坏性
    操作（清除记忆、删除数据）红字 + 二次确认。

### 2.6 主题系统（多风格，用户可选）

产品支持多套风格化前端主题，用户在 设置 → 外观 中选择。主题是**同一语义 token
契约的多套值集**——组件与布局不感知主题（P7 不变），主题只替换 L2 语义层的取值，
必要时微调 L1 的半径/阴影幅度。这正是三层 token 架构的设计意图：语义层是稳定
契约，主题是可插拔的值集。

**机制**：

- 主题以 `data-theme="<id>"` 挂载在根节点，明暗以 `data-mode="light|dark"`（或
  跟随系统）叠加；一个主题必须同时提供 light / dark 两套完整语义值（一对双模式
  值集即一个主题，与 opencode 主题 schema 同构）。
- 切换主题/模式只替换 CSS 自定义属性，不重载界面、不改动组件代码；切换即时生效
  并持久化（跟随系统时监听系统变更）。
- 组件与视图**禁止**取用语义层之外的颜色；新增主题不需要改任何组件（验收硬门槛）。

**主题清单 schema**（内置主题与未来外挂主题包同构；外挂主题包属 P5 插槽，M5+
评估，不进 M1.5 范围）：

```text
Theme {
  id: string                      // 稳定标识，如 "mirage-dawn"
  name: string                    // 展示名
  light: SemanticTokenValues      // 全量 L2 值集（含状态四色与证据高亮）
  dark: SemanticTokenValues
  radius?: { sm, md, lg }         // 可选：圆角幅度微调
  shadow?: { card, overlay }      // 可选：阴影幅度微调
}
```

**内置主题（M1.5-08 交付，共 5 套；具体色值在实现时按本节门槛校准并评审）**：

| id | 名称 | 气质 | 强调色系 |
| --- | --- | --- | --- |
| `mirage-dawn` | 晨蓝（默认） | 专业、安静；本文 §2.2 基准值 | 品牌蓝 |
| `mirage-nordic` | 冷杉 | 冷灰蓝、低刺激长会话 | 冰蓝 |
| `mirage-ember` | 暖沙 | 暖褐低饱和、暖色办公环境 | 赭橙 |
| `mirage-matcha` | 抹茶 | 豆沙绿强调、护眼浅色 | 苔绿 |
| `mirage-ink` | 玄墨 | 近无彩高对比极简，强调色退化为黑白 | 黑 / 白 |

**验收门槛（每套主题强制）**：

1. 语义 token 全量覆盖：L2 清单中的每个变量在 light/dark 两套中都有定义，缺一
   即构建失败（以测试锁定）。
2. 对比度：正文 ≥ 4.5:1、大字号与 UI 边界 ≥ 3:1；状态四色在两套模式下相互可区分
   （不依赖形状之外的补充说明即可分辨成功/失败/等待）。
3. 状态语义封闭映射不变（§2.5-7）：主题只换色值，不换语义。
4. SnapshotFrame 证据高亮在截图底色上可见（≥ 3:1 对比 + 描边）。
5. 切换主题/模式时布局零位移（token 只影响颜色/圆角/阴影时不产生回流差异）。

## 3. 前端信息架构（拓扑树）

### 3.1 顶层拓扑

```text
Mirage Shell（统一应用壳）
├── 活动栏 ActivityBar ──── 会话 · 工作流 · 资源(M2+) · — · 设置
├── 页面：会话 Chat（默认落地）
│   ├── 会话侧栏 SessionsSidebar（历史管理）
│   ├── 线程区 ThreadView（消息流 + 活动卡 + 批准卡）
│   ├── 输入区 Composer（模式：对话 / 执行）
│   └── 运行抽屉 RunDrawer（当前运行时间线 + 观察流）
├── 页面：工作流 Workflows
│   ├── 库视图 LibraryView
│   ├── 编辑器 EditorView（步骤序列 + 步骤面板 + 参数/策略 + 诊断 + 草稿/发布）
│   └── 运行 RunsView（列表 → 详情三联：时间线 + 快照流 + 日志/工件）
├── 页面：资源 Resources（M2+ 占位：桌面/文件/工具一览，含 MCP 工具）
├── 页面：设置 Settings（八类，见 3.4）
└── 全局层：状态栏 · 命令面板 · 通知/批准中心 · 紧急停止 · Desktop Overlay(M5)
```

### 3.2 路由表（浏览器形态 hash 路由；壳形态同构）

| 路由 | 页面 | 说明 |
| --- | --- | --- |
| `#/chat` | 会话（新建/最新） | 默认落地页 |
| `#/chat/:sessionId` | 会话 | 侧栏选中态同步 |
| `#/workflows` | 工作流库 | 卡片网格 |
| `#/workflows/:workflowId` | 编辑器 | 草稿/发布状态条 |
| `#/workflows/:workflowId/runs/:runId` | 运行详情 | 三联证据布局 |
| `#/resources` | 资源 | M2+ 占位 |
| `#/settings/:category` | 设置 | category ∈ 3.4 八类 |

### 3.3 会话页（harness 主界面）

- **SessionsSidebar**（240px，可折叠）：新建会话（默认进入执行模式）、搜索框、
  分组列表。会话对象四件套（重命名[AI 摘要]/置顶/删除确认/导出 Markdown）+ fork
  （依赖 Mira 会话树，P4 检查点落位后开放）。
- **ThreadView**：消息组 + ActivityCard 流。运行期事件（task.updated / 观察快照 /
  权限请求）作为系统行或活动卡进入消息流；长会话开启"过程折叠 + 轮次导航"。
- **Composer**：对话模式（纯交流，不产生桌面动作）与执行模式（提交目标给 agent
  执行，等价 M1 `task.submit` 语义）。
- **RunDrawer**：运行中自动展开，可手动固定。
- M1 已交付视图映射：M1.5-04 Workspace 提交表单 → Composer（执行模式）；Tasks
  列表 → SessionsSidebar 分组 + 会话条目状态徽标；Execution 详情 → RunDrawer 与
  运行详情页（工作流运行的复用组件）。

### 3.4 设置页（八类，顺序固定）

| 分类 | 内容 | 契约/依据 |
| --- | --- | --- |
| 常规 General | 语言、开机自启、托盘行为、通知、代理/网络、日志级别、数据目录 | Mirage 自有 |
| 外观 Appearance | 主题库（内置 5 套，卡片预览即时切换，§2.6）、明暗三态（浅/深/跟随系统）、密度、字号缩放、等宽字体 | 本文 §2、§2.6 |
| 模型 Models | ModelProfile 列表与表单：提供方、端点、密钥、能力位、默认/回退模型、参数、预算限额 | mira `model_profile.hpp` |
| 记忆 Memory | 作用域管理（Task/Session/全局）、条目查看/编辑/清除、整理策略开关 | mira `memory_contracts.hpp` |
| 技能 Skills | 已装技能、来源管理、启用/禁用、技能权限 | mira skills + 插槽 P5 |
| MCP | 服务器连接管理、工具清单、工具级权限（allow/ask/deny） | DEC-010 权限模型 |
| 权限 Permissions | 桌面权限策略矩阵（Provider × 范围：allow/ask/deny + 默认模式）、确认流 | DEC-010 |
| 运行时 Runtime | 服务/主机控制（启动/停止/重启）、事件日志、诊断导出、关于（版本） | DEC-004/DEC-007 |

### 3.5 工作流页

- **LibraryView**：卡片（名称、版本、参数摘要、最近运行状态、成功率）；新建/导入/
  导出（`workflow_definition_to_json` 语义）；模板区（M5）。
- **EditorView**：三栏——左：参数 Schema 与执行策略面板；中：步骤序列区（StepCard
  列表 + Control 回跳括弧 + 跳过条件徽标）；右：选中步骤的属性面板（参数、前置
  条件谓词、验证、错误策略）。顶部诊断条（IR 校验 fail-closed 错误逐条列出）+
  草稿/发布状态（P6：agent 修改落草稿，发布需确认，diff 预览）。
- **RunsView**：运行列表（状态/时间过滤）→ 运行详情：三联布局（步骤时间线 +
  快照流 + 日志/工件），失败步高亮；操作 = 重试（双语义：原版本重跑 / 新版本 +
  原输入重跑）、恢复（recovery）、"让 agent 修复此步"（跳回会话并携带上下文）。
- **与 IR 对齐约束**：编辑器只暴露 Workflow IR v1 语义（有序步骤、前置条件跳过、
  Control 回跳 `loop_head` + `max_iterations`、参数 `{"$param"}` 引用、谓词 DSL）；
  自由节点图、子 workflow 等属 IR 扩展位，IR 未承诺前 UI 不出现。

### 3.6 全局层

命令面板（`Ctrl+K`）、通知/批准中心（后台批准不打断当前视图）、紧急停止（状态栏
常驻 + 全局快捷键，行为对齐运行时 Takeover 语义：阻止新自主动作、收敛进行中输入、
恢复前重新观察）、Desktop Overlay（独立透明窗，M5，本文不规定其内部布局）。

## 4. 契约映射与前瞻依赖

| 前端能力 | 现有契约 | 前瞻依赖（记录为契约工作项输入） |
| --- | --- | --- |
| 会话列表/管理 | 无 | 需 IPC `session.*` 面（列表/打开/历史摘要），依托 mira `open_session`/会话树 |
| 会话消息流 | `events` 帧（DEC-012 机制） | 需消息/轮次事件与增量输出事件集（M2+ 事件扩展） |
| 执行模式提交 | `task.submit`（M1） | 已满足最小闭环；对话模式依赖消息面 |
| 工作流库/编辑器 | 无 | mira `workflow_ir.hpp`（JSON 序列化）经 IPC 暴露 `workflow.*` 面 |
| 运行监控 | `task.inspect`/`task.updated` | WorkflowRun 状态视图事件化 |
| 设置-模型/记忆 | 无 | mira `ModelProfile`/`MemoryScope` 管理面经 IPC/持久化暴露 |
| 状态栏主机态 | hello `host_status` / `host.status` 事件 | 已满足 |

原则：前瞻依赖只登记为协议演进输入（DEC-007/DEC-012 的附加扩展流程），UI 侧一律
以 transport 接口 + mock 先行（M1.5-04 模式），不阻塞视觉与交互定型。

## 5. 落地里程碑

- **M1.5（浏览器形态）**：`M1.5-07` 统一壳与信息架构重构（ActivityBar/StatusBar/
  路由表/会话侧栏骨架 + 既有视图迁入）；`M1.5-08` 设计 token、组件库与主题系统
  落地（本文 §2 全量 token 进 `ui/`，StatusBadge/SnapshotFrame(占位)/ActivityCard
  等基础组件，§2.6 主题机制与 5 套内置主题）；工作项定义见 M1.5 计划修订记录。
- **M3**：壳选型 PoC 以本文 token/布局为视觉基准（DEC-006 决策 3）。
- **M5**：Desktop Product 完整产品化（Workflow 编辑器完整版、批准中心、Overlay、
  设置全量），以本文为验收基线；前端工具链与组件框架定案时复核 §2.2 token 命名
  映射（DEC-006 决策 6）。

## 6. 变更记录

- 2026-09-16：初版。依据 DEC-013 确立 harness 优先信息架构、统一风格规范与
  workflow 结构化步骤编辑器口径。
- 2026-09-16：依据 DEC-013 修订增补 §2.6 主题系统——产品支持多套风格化内置主题
  （5 套）供用户选择，机制为语义 token 值集替换；外观设置接入主题库。
- 2026-09-16：`M1.5-08` 落地时按 §2.6 对比度门槛校准状态四色 light 值（在 §2.2
  基准上加深：成功 `#157f3d`、警告 `#b2540a`，hue 与语义封闭映射不变），主题实现
  事实源为 `ui/app/src/theme/`；dark 值维持 §2.2 基准。
