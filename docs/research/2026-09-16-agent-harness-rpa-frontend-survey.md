# 调研：Agent Harness 与 RPA / Workflow 前端设计经验

> 状态：Completed
> 日期：2026-09-16
> 负责人：Mirage 维护者
> 调研方式：公开资料（官方文档、GitHub README/docs、官网、发布博客）经并行网络调研
> 汇总；断言均可溯源到所附 URL，未做源码级核实（区别于
> [架构调研](2026-09-16-rpa-agentic-automation-architecture-survey.md)的源码核实方法）
> 关联决策：[DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md)、
> [DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)
> 输出消费方：[《Mirage 前端设计规范与信息架构》](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)

## 1. 调研目标与方法

Mirage 产品方向确定：**首先是 agent harness，其次是 workflow**，二者同级存在、统一壳
承载。本调研为前端信息架构与视觉规范提供输入，分两路：

1. Agent harness 前端：opencode、pi、DeepSeek（官方 App + 开源 DSH）、Claude
   Code / Claude 应用、Cline、Goose Desktop——回答"会话中心如何组织、消息流如何
   呈现、设置如何分类、视觉风格趋同到哪"。
2. RPA / Workflow 前端：n8n、Node-RED、UI.Vision RPA、OpenRPA、Robocorp Control
   Room、Dify、Langflow——回答"workflow 编辑器选画布还是序列、运行监控如何分层、
   桌面证据如何展示、人机协作如何打断"。

方法约束：每条关键断言附来源；不复制实现细节，只提炼可进入 Mirage 设计规范的模式。

## 2. Agent Harness 前端结论

### 2.1 逐对象要点

**opencode（SST）**：终端三段式（消息流 + 底部输入），会话/主题/模型全部经命令
对话框；会话四件套齐全（resume/new/compact/export + undo/redo 绑定文件回滚）。
主题系统最完整：角色化语义 token（primary/accent/error/warning/success/info + 三级
background + 三级 border），每色 `{dark, light}` 双值即一主题双模式。
（https://opencode.ai/docs/themes/、https://opencode.ai/docs/tui/）

**pi（earendil-works/pi）**：自我定位 minimal harness（薄核 + 可组合）。会话是
JSONL 树（id+parentId），支持原地分支 /fork /clone；状态栏常驻 token/费用/上下文
占用；工具调用行内渲染、`Ctrl+O` 折叠；工具结果"给 LLM 的文本 + 给 UI 的结构化
数据"双通道契约；流式期间增量渲染 diff。
（https://github.com/earendil-works/pi 、https://mariozechner.at/posts/2025-11-30-pi-coding-agent/）

**DeepSeek**：开源 harness「DSH」（`npx @deepseek-ai/dsh web`，"Everything is a
Plugin"）为三区结构——左侧栏（新会话 + 工作区双区）+ 中央内容 + 右侧栏（插件注册
Tab、文件树/预览）；长会话"过程折叠 + 轮次导航"；左右侧栏都是标准化 UI 扩展位，
外观本身即插件。官方 App 则是经典左抽屉会话列表 + 三态主题。
（https://github.com/deepseek-ai/deepseek-harness 、https://www.ithome.com/1/000/715.htm）

**Claude Code / Claude 应用**：会话选择器范围渐进扩大（本项目 → 全机）+ 搜索 +
AI 自动命名（冲突加后缀）+ `/branch` 就地分支；设置全量分组 = 权限（allow/ask/deny
+ defaultMode）、模型、Hooks、MCP 名单、记忆、沙箱、外观、plugins/skills、遥测，
三级作用域 user/project/local。Claude 应用左栏 = 会话列表 + Projects。
（https://code.claude.com/docs/en/sessions 、https://code.claude.com/docs/en/settings-reference）

**Cline / Goose Desktop**：Cline 的 Plan/Act 双模 + 每步 Checkpoint（Compare/
Restore 三选项）是安全执行的最佳范式；Goose Desktop 三视图（Sessions/聊天/设置）
+ 设置五类（Provider/Extensions/Appearance/Permissions/Behavior），会话支持重命名/
置顶/删除确认/导出，扩展可在会话内渲染交互式 UI。
（https://docs.cline.bot/features/checkpoints 、https://block-goose.mintlify.app/guides/desktop-app）

### 2.2 跨产品共性（harness 通用模式）

1. GUI 形态默认"左栏会话列表 + 搜索 + 分组"；终端形态退化为对话框/状态栏，底层
   同为"会话列表 + resume"。
2. 会话对象四件套：重命名（AI 自动命名 + 冲突后缀）、置顶/分组、删除带确认、导出；
   进阶为树状分支/fork（pi、Claude）与 compact。
3. 工具调用统一"默认折叠的行内卡片 + 全局/逐条展开开关"；流式期间尽早渲染。
4. 设置收敛为六类：常规/行为、外观（三态主题）、模型/Provider、权限/审批、
   扩展/MCP/插件、数据与记忆。
5. 安全执行三件套：Plan/Act 分离、逐步审批 + auto-approve 规则、检查点回滚。
6. 状态栏常驻成本与上下文指标（token、费用、模型、thinking 档位）。
7. 扩展收口两种：文件式扩展（pi）或插件注册 UI 插槽（DSH 左右侧栏、Goose 内嵌 UI）。

## 3. RPA / Workflow 前端结论

### 3.1 逐对象要点

**n8n**：自由节点画布；搜索优先加节点（`N` 键）；节点卡 = 图标+名称+状态+摘要；
执行监控两级：Executions 列表（状态/时间过滤）→ 逐节点输入输出；双语义重试
（当前已保存版本 vs 原始版本）；Debug in editor 把执行数据钉回编辑器复现。
（https://docs.n8n.io/build/understand-workflows/understand-executions/debug-executions/）

**Node-RED**：三区布局（Header + 画布 + 双侧栏）；palette 分类拖拽；Debug 侧栏 =
容量上限 + 按节点过滤 + 暂停丢弃计数的消息时间线；**显式 Deploy** 区分编辑态与
运行态。（https://nodered.org/docs/user-guide/editor/）

**UI.Vision RPA**：桌面自动化用 **Selenium IDE 式命令列表而非画布**；每步目标 =
屏幕截图模板，Find 按钮运行前验证可匹配；OCR 文本目标作为图片目标的降级路径。
（https://ui.vision/rpa/x/desktop-automation）

**OpenRPA**：WF 设计器（Toolbox + 画布 Sequence/Flowchart + Properties 网格）；
录制器自动构建 Selector、元素高亮 overlay；Selector 窗口 = UI 树/选择器链/属性/JSON
四区同步编辑 + Highlight 验证。
（https://docs.openiap.io/docs/openrpa/OpenRPA-UI.html）

**Robocorp Control Room**：无画布，纯管理面；运行详情 = **日志流 + 实时屏幕画面
（Videostream）+ 工件文件**三联布局；组织级 audit log。
（https://sema4.ai/docs/automation/control-room/unattended/video-streaming）

**Dify**：五种 App 类型（Chatbot/Agent/Chatflow/Workflow…）；Chatflow 与 Workflow
共享同一画布与节点系统，差别仅在会话层——**agent 对话与 workflow 编排在同一产品
的入口关系是 App 层而非编辑器层**；Agent 节点可嵌入 workflow；v1.13 Human Input
节点 = 工作流暂停 + 自动生成审批表单 + 恢复。
（https://docs.dify.ai/en/learn/key-concepts 、https://docs.dify.ai/en/cloud/use-dify/nodes/human-input）

**Langflow**：Playground 让含 Chat Input 的 flow 边聊边测（对话侧栏 ↔ 画布同屏）；
Assistant 可从自然语言生成/修改整张 flow 图。
（https://docs.langflow.org/concepts-overview）

### 3.2 跨产品共性（workflow 通用模式）

1. 两种编辑器原型：数据集成类用自由节点画布（n8n/Node-RED/Dify/Langflow）；
   RPA 序列类用线性步骤列表（UI.Vision、OpenRPA Sequence）。
2. 节点创建一律"搜索优先"，分类分组只是二级导航；节点卡最小信息集 = 图标+标题+
   状态点+摘要参数，高级配置收进点击后的面板。
3. 监控两级层级：运行列表（状态/时间过滤）→ 运行详情（逐节点/步的输入输出日志），
   失败高亮 + 双语义重试。
4. 调试三件套：实时消息侧栏（容量+过滤+暂停）、数据回放（pin 回编辑器）、显式
   Deploy 区分草稿与运行态。
5. HITL 新范式：暂停在节点上 + 自动生成审批表单 + 恢复（Dify），取代全局停止。
6. 桌面证据：截图/录屏作为运行工件（Robocorp、UI.Vision），但无一将"结构化观察
   快照"作为一等运行数据——Mirage 的差异化空间。

## 4. 对 Mirage 的设计输入（综合结论）

1. **信息架构采用 harness 优先的统一壳**：会话（chat）为默认落地页，工作流为一级
   导航中的平等伙伴；会话侧栏承担历史管理（分组/搜索/四件套），设置收敛为
   七类（常规/外观/模型/记忆/技能/MCP/权限与运行时）。→ DEC-013、设计规范第 3 节。
2. **消息流三要素**：助手消息平铺 + 工具调用默认折叠的行内活动卡 + 打断式批准卡；
   长会话过程折叠 + 轮次导航；状态栏常驻模型/上下文/主机状态/紧急停止。
3. **workflow 编辑器选结构化步骤序列，不选自由画布**：pinned Mira `Workflow IR` v1
   即"有序步骤 + 前置条件跳过 + 受限回跳（Control 步骤）"，与 UI.Vision/OpenRPA
   Sequence 原型吻合；步骤详情以观察快照为第一载荷（快照 + 元素高亮 + 引用树/
   JSON 同步编辑，OpenRPA Selector 窗口模式）；步骤级"让 agent 修复"入口。
4. **运行监控两级 + 三联证据**：运行列表 → 运行详情（步骤时间线 + 快照流 + 日志/
   工件），失败步高亮 + 双语义重试 + 恢复（recovery）入口。
5. **草稿/发布分离**（Node-RED Deploy 模型）：agent 修改落草稿，发布需人工确认；
   会话与编辑器同屏互跳（Langflow Playground 模式：agent 改流程、人看变更解释）。
6. **扩展插槽化**：右侧栏与设置页为 MCP/Skill/Provider 预留可注册插槽（DSH 模式），
   不硬编码布局。
7. **视觉风格**：角色化语义 token + 明暗双值（opencode 主题 schema 为基准，
   shadcn 变量命名兼容）；低饱和中性色 + 单一品牌色；中高密度专业工具气质。

## 5. 与现有文档的关系

- 本文是[前端设计规范与信息架构](../design/Mirage%20%E5%89%8D%E7%AB%AF%E8%AE%BE%E8%AE%A1%E8%A7%84%E8%8C%83%E4%B8%8E%E4%BF%A1%E6%81%AF%E6%9E%B6%E6%9E%84.md)
  与 [DEC-013](../decisions/DEC-013-frontend-ia-harness-first.md) 的证据基础。
- 后端/执行模型的 RPA 经验收敛见
  [架构调研](2026-09-16-rpa-agentic-automation-architecture-survey.md)（源码核实级），
  两文互补不重叠。
