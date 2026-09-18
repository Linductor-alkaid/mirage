# RPA 编辑器设计参考调研（工作流工程界面）

> 日期：2026-09-18 · 调研对象：影刀 RPA（重点）、OpenRPA、Node-RED、UiPath Studio、n8n
> 结论服务于 Mirage 工作流编辑器（RPA 工程范式映射到 Workflow IR v1）。
> 原始调研含来源链接，本档为设计要点归档；完整版见会话记录。

## 1. 公认布局范式（跨产品汇总）

1. **三栏 + 底栏是最大公约数**：原子动作库（分类折叠+搜索）/ 编排区 / 属性面板，底部日志。四家全部符合（Node-RED 属性以模态呈现，面板可换侧）。
2. **中区两流派**：自由节点图 vs 顺序序列。RPA 主流（影刀、UiPath Sequence）以顺序序列为默认。
3. **拖入语法三件套**：拖拽（含插入位置落点指示）、双击/点击追加到末尾、搜索后插入到选中项之后（UiPath Ctrl+Shift+T）。
4. **属性编辑时机**：选中即右侧联动（影刀/UiPath/OpenRPA）或双击模态（Node-RED/n8n）。属性分组 Common / Input / Options / Misc / Output 是行业事实标准；**通用行为（超时/错误策略/启停/注释）内建于每条指令**是跨产品共识。
5. **变量与参数**：独立参数/变量表；输入框内引用变量（chip / 表达式双态）；OpenRPA 支持输入框内 Ctrl+K 就地建变量。
6. **嵌套块**：序列流派用缩进区块 + 竖连接线 + 可折叠（非连线）；块头承载条件/循环变量。
7. **调试位**：断点（行首圆点）、调试运行与单步、当前行高亮、变量监视（影刀可暂停改值）、底栏日志、历史运行（影刀「运行结果查询 + 运行回放」与 Desktop Observation 快照回流天然契合）。
8. **运行模型**：编辑态/运行态分离，显式发布（影刀发版、Node-RED Deploy、UiPath 发布）。

## 2. 影刀要点（重点对象）

- 开发界面 = 指令区（左，常规指令按大类折叠 + 自定义指令 + 收藏/最近）+ 画布区（中，顺序序列）+ 属性区（右，选中指令参数表单）。
- 指令近千条，大类：条件判断/循环/等待/网页自动化/桌面软件自动化/鼠标键盘/Excel/数据处理/操作系统/人工智能AI/流程控制。
- 双击指令自动追加到画布末尾；画布右键可收藏/注释/折叠区域。
- 属性面板通用配置：名称/启停/注释/超时（默认 20s）/错误处理（停止流程/继续执行）。
- 调试：断点圆点、调试运行、F5/F10/F11/Shift+F11、变量监视可改值、运行后底部「输出」日志（四级、可导出）、历史运行回放（配错误截图）。
- 流程管理：应用管理列表（发版/分享/触发执行）；子流程模块复用，子流程互调。

## 3. 开源/相邻产品要点

- **OpenRPA**（Windows Workflow Foundation）：Toolbox/Snippets 侧栏（Activities 按分类拖入）+ Designer（Sequence 纵排或 Flowchart 自由画布）+ 属性框（Input/Output/Misc 三组）+ 底部 Output（Logging/Output/Workflow Instances 三段，运行实例一等公民）；Variables/Arguments/Imports 面板；F9 断点、F10 单步、暂停时全量变量倾倒、当前活动黄色边框。
- **Node-RED**：Palette（左）+ Workspace（中，节点连线）+ Sidebar（Information/Help/Debug/Config，可换侧）；节点蓝圈=未部署、红三角=配置错误；双击节点三标签（Properties/Description/Appearance）；Ctrl+点击 Quick-Add；Deploy 与编辑分离。
- **UiPath Studio**：可停靠面板族（Activities/Properties/Explorer/Data Manager/Output/调试组）；Sequence 原地纵排可折叠；属性五分组（Common/Input/Options/Misc/Output）；Ctrl+Shift+T 搜索后插入选中活动之后，模糊搜索；调试面板组 Locals/Watch/Call Stack/Immediate。
- **n8n**：节点 → operation 两级选择；节点级 Retry On Fail / On Error 通用配置；Executions 列表 "Copy to editor" 把历史运行数据拷回编辑器复跑；convert to sub-workflow 一键封装。

## 4. Mirage 编辑器的落地映射（已实现）

- 面板：顶壁挂屏/活动栏保持产品壳；工作流页与 harness 同壳（左栏 = 已保存工作流列表：分组 全部/已发布/草稿 + 搜索 + 运行/重命名/删除/导出 IR JSON）；主区 = 编辑器（顶部工具条：名称/版本/描述/IR 诊断/发布/运行；中间顺序序列；右栏三 Tab：动作库（默认）/属性（选中联动）/参数）。
- 拖入三件套：拖拽 + 缝隙插入指示线；动作卡 `+` 追加（选中指令时插入其后——UiPath 范式）；双击动作卡追加（影刀范式）。
- 属性分组 Input·参数 / Output·输出 / Options·执行条件（skipIf、loop 上限）；错误策略固定 fail-fast（IR v1 语义，未承诺的策略不暴露）。
- 原子动作目录（模拟域）：文件/命令/桌面观察/窗口与输入/剪贴板/流程控制/子流程 七类 18 项；窗口与输入及 agent 委托类标注 M2+（依赖 Platform Backend）。
- 诊断：IR 校验 fail-closed 展示（未定义参数引用、循环缺上限等逐条列出）。
- 调试/回放：断点、单步、变量监视待 M2+ 真实运行面交付后建设；运行回放对应 Mirage 的观察快照回流（RunDrawer/运行详情已具雏形）。
- 后端接口缝：`ui/app/src/state/workflow-backend.ts` 的 `WorkflowBackend` 接口（listDefs/saveDraft/publish/remove/atomCatalog/listRuns/run/cancelRun），预期 IPC 映射 `workflow.list/save/publish/delete/atom.catalog/runs/run/cancel`（设计规范 §4 前瞻依赖），UI 仅依赖接口，mock 与未来 IPC 适配器可互换。
