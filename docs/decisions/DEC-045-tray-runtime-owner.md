# DEC-045：托盘进程持有Runtime与前端生命周期

> 状态：Accepted
> 日期：2026-10-06
> 负责人：Mirage维护者
> 工作项：M6-25 / M6-04
> 依据：维护者明确要求将Agent服务移入托盘，前端不能脱离托盘启动。

## 决策

产品常驻进程为mirage-tray，内嵌既有RuntimeService/Mira，RuntimeService仍是该进程
唯一Executor owner。IPC/Agent任务/托盘呈现与有界动作派发复用其Executor；前端保持
独立进程和独立UI Executor，经IPC调用Runtime。mirage-service保留为无产品托盘的
开发/测试/CLI宿主，其hello不能通过产品前端准入。

统一入口mirage start先复用已在线托盘；否则启动托盘并等待真实平台注册及Runtime
就绪，再由托盘启动前端。无通知区/注册失败时整次启动失败且不打开前端。前端创建
窗口前校验hello中的tray能力，IPC连接失去宿主后关闭窗口。重复打开复用单个前端
并激活窗口；窗口关闭不结束Agent或托盘。托盘“退出”检查活动任务/Agent轮次/
Workflow，存在活动时打开前端退出确认，取消保持工作，确认后按既有取消/shutdown
顺序关闭Runtime并回收前端。系统信号/宿主故障走紧急收敛，无交互确认。

产品动作通过附加IPC请求传递，v1保持兼容；状态快照是退出确认与窗口激活的事实源，
通知经既有Topic广播。平台进程创建/回收封装在Platform Backend，公开契约不暴露
平台或Executor类型，所有异步泵由Runtime owner管理。队列与就绪等待均有界，失败
可观察。设置文件、API Key引用、会话持久化和已保存模型格式不变。

## 影响与验证边界

此决策替代DEC-007/030中产品Tray仅作为独立Service客户端的部分；开发headless
Service契约保留。M6-24历史三进程证据不作为新拓扑通过依据。Linux私有真实DBus/
SNI/X11验收与故障注入在本机执行；Windows/Wayland/真实通知区呈现需维护者目标
机器补跑，尚未取证不得声称已完成跨平台或安装包验收。


## 2026-10-06 最小化恢复补验

BUG-20261006-006：EUI 最小化时跳过 compose，不能将产品恢复仅放在 UI 状态消费中。
FrontendProcess.open 在既有子进程路径直接经 Platform Backend 请求恢复：Linux
有界枚举 EWMH 客户端并严格匹配所属 PID，向 WM 发用户打开请求；无 WM 时使用
所属顶层窗口的 X11 map/raise。Windows 查找所属 PID 的顶层窗口并请求异步恢复。
所有工作仍为 Runtime 串行上下文有限任务，不新增 UI 定时器/线程或修改 EUI。
未映射的初始子进程保留首次 map；预算超限/平台请求失败有诊断。焦点仍受 WM/系统
规则约束，恢复请求不等同于未经核对的实际焦点 ACK。
