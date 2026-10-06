# DEC-034：原生模型设置与通用 Agent harness

> 状态：Accepted
> 日期：2026-10-04
> 负责人：Mirage 维护者
> 冻结里程碑：M6

## 背景与决策

维护者明确本轮首先搭建 agent harness，RPA workflow暂不开始。通用 harness 接收文字，
构造上下文，调用模型，可执行显式注册的通用工具并反馈结果；文字回答即可结束，不要求
屏幕观察、设备动作或 done JSON。原生页面只有一个 Agent 会话入口。

沿用 DEC-027 Profile/网关/传输、DEC-021 session 与 MiraRuntime task 控制面。
新增 model.get/set IPC，保存既有 service.json 的 model 块并在无活动模型轮次时
立即更换模型层；失败不替换当前配置。凭据只配置环境变量名称，不落盘 API Key。

session.chat 可选 agent=true 表示通用 harness；缺省保留旧 dialog 单次推理兼容。
当前 Mira AgentLoop 是设备闭环，不能直接使用：详见 MIRA-20261004-001。暂时在
ModelLayer 单一 Adapter 内复用 ModelGateway 和 BuiltinToolRegistry 搭接有限循环；
首步仅注册 Mira 自带 wait 通用工具，后续工具由模组边界扩展。本轮不注册任何桌面工具。
工具结果有界 JSON 文本回填，不声明原生 tool-result wire 互操作能力。

前端本进程 owner 管理 Executor，IPC 外部 worker协作停止，跨到 UI 使用 executor::comm。
Runtime Service 是独立任务 owner。关闭前端停止其IPC，不关闭外部已有服务；统一
入口/托盘/活动退出确认仍属 M6-04。前端不做网络/阻塞 IPC。

## 备选与风险

不假造屏幕观察以复用设备 loop，不在 UI 调用供应商SDK，不修改 pinned。依赖交付通用
入口后移除临时adapter。provider fixture覆盖失败路径，实际供应商独立验收：
SiliconFlow文字/工具已通过，MiniMax因TLS SNI缺口暂不可用（MIRA-20261004-002），
Windows未验收。模型切换忙拒绝，事件缺失通过历史/模型恢复；草稿不冒充已应用配置。

## 验证

协议兼容、配置保存/校验/忙拒绝、无屏幕的真实工具回填、错误、取消、超时/预算、退出；
Linux 原生模型设置和会话状态画面。关联 M6-03、DEC-033、DEC-027、MIRA-20261004-001。

模型结果附带实际推理/工具执行计数，来自网关回执和工具执行边界，仅为结构化结果，
不建立额外任务监控。UI展示助手文字时移除首尾空行，保留内部换行/缩进及服务原始历史。
