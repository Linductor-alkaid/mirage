# DEC-069：本地 OCR／模板目标 Workflow

> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-11 第二增量
> 依据：维护者授权继续 M7-11；DEC-067/068 与已交付 Mirador 感知、编号动作契约

## 背景与决策

2026-10-15 扩展：[DEC-091](DEC-091-local-semantic-perception-and-action-loop.md)
将本地定位/一次动作/后条件能力复用到 native 会话，M9-05/09 跟踪；以下“仅
workflow 注册”保留为历史交付边界，新 native 能力未实施前不宣称已暴露。

语义目标首增量已可零模型执行；Canvas 和无 AT-SPI 的界面仍需本地视觉目标。
追加仅 workflow 注册的 desktop.knowledge.visual_click，不暴露给原生模型会话。
知识为 schema=1、精确 window_title、page_text、target_kind（ocr/template）、target、
expected_text；各字符串有界 UTF-8，禁止保存坐标、编号、执行引用或 scope。
页面身份是当前前台窗口标题及窗口内唯一 OCR 页面文字；不将视觉文字冒充应用进程身份。

每次调用创建短期 DesktopImageTurn，复用既有 mark、编号 click 与 DesktopActionVerification。
以新前台窗口几何限定整个窗口 ROI，精确 OCR 目标及页面证据置信度至少 0.8，模板证据
至少 0.9；模板 ID 只在已有 Mirador VisualIndex 资源命中时可用。所有同名证据计入
歧义，即使其中一项低置信度；唯一且可见的目标才可点击。expected_text 在新前观察中
必须不存在。页面身份文字与后条件必须不同；OCR 目标与后条件也不同。

原始窗口 ROI／PNG 沿用既有 mark 的 8 MiB 与 128 候选预算；大窗口超限明确拒绝。
模板/文字是定位事实，是否执行由本地知识及现有 input.inject 权限决定，不宣称可交互语义。
取证、解析和动作有五秒共同上限，动作仍有一秒上限；后观察在同一 owner 上限内。
取消、接管 epoch、焦点、窗口标题/几何、像素一致性与 registry scope 全部复用既有门禁。
后条件要求新 OCR 调用/新 scope、唯一 expected_text 和仍唯一的 page_text；旧证据不能结算。
仅当首次新后观察报告 condition_not_matched 时，允许最多两次追加只读 OCR 核验，共享
原五秒 owner 上限与本地三次后观察上限；输入只执行一次，页面/权限/取消等失败不重试。
返回含 knowledge_digest 与 evidence 的 canonical JSON 字符串，兼容现有 Mira 标量谓词。
派发后的无证据/异常/取消为 ExecutionUncertain，调用者不得自动重发。

Runtime 用既有 service-owned pipeline/context 装配 workflow 工具。无 OCR、mark、
scoped reference、Window/Screen、complete Input 或 ActionCoordinator 时不注册。
无新任务设施、线程、队列、版本库、模型请求或依赖修改；Mira 承载 workflow 生命周期。
实际产品长视觉步骤暴露已有产品维护 timer 的重叠等待：四个默认 worker 被 workflow
驱动、取消监视及两个维护等待占满，后观察与 IPC 的串行派发无法入场。Executor 的
公开周期契约允许重叠，这是应用装配问题。维护 callback 加单一 admission 标志与
异常安全释放，已有 callback 仍消费 submit_on future；重叠 tick 直接结算，不排队
保存旧产品状态。串行 handler 读取最新状态，Executor 保留周期完成/失败事实源。
既有 TimerHandle 的停止、取消、排空顺序不变，不提高线程数或动作期限。
资产文件发布/运行沿用现有 IPC/CLI；原生会话选择、自动恢复、跨重启资产恢复与多页导航
继续属于 M7-11 后续范围。模板知识不自动生成或学习模板资源。

## 验收

相同知识在窗口移动/编号换代后重复执行仍定位新目标；OCR 和模板正例、低置信度、
重名、错页、已有后条件、坏数据、授权拒绝、像素变化、失焦/标题变化、过期/取消/接管、
动作异常/未释放及后观察假成功均验证。Mira DryRun 零动作、Strict 新证据完成、
Recoverable 明确 WaitingAgent；在私有实际安装产品运行公开 Canvas OCR workflow。
真实模板装配能力单独核对；未执行环境/权重 TSAN/其他平台不得标为完成。
