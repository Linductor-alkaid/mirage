# DEC-074：任务内共享观察与应用意图重验

> **2026-10-27 失效说明：** 本决定中与常驻感知/普通动作执行冲突的条款已被替代。以 [DEC-117](DEC-117-runtime-resident-perception-and-autonomous-input.md) 的“旧决策失效范围”为准；下文冲突原文仅供历史追溯，不再作为实现或测试要求。


> 状态：Accepted
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 工作项：M7-10 第二增量、M7-14；承接 DEC-071/073

最新真实会话的共享成功返回 3200×2000 原图及 1600×1000 预览；随后
application.open_uri 因授权前的 application.list 超过五秒被拒绝，dispatched=false，
无副作用。不是共享失败或不确定输入。维护者授权继续增量开发。

1. 应用列表引用仍绑定最新 scope、session 和 environment_epoch，只将其视为最多
   60 秒的候选意图；执行前及权限等待后在调用方本地重新枚举，精确核对同一 installed
   ID 存在。过期、被新列表替换、epoch 变化、取消、枚举失败或目标消失均拒绝；不依据
   缓存派发，不自动改选另一应用，不放宽任何可执行视觉/输入引用的五秒期限。
2. ScreenProvider 增加平台无关 DesktopCaptureSession。对象由单个序列化调用方持有，
   不可跨任务共享；创建不授权，首次 capture 才启动系统 UI。默认不支持此能力。
   Linux Portal Session 只在成功 ScreenCast 帧之后保留；后续 capture 复用同一
   bus owner/session/node，重新打开 PipeWire remote 并捕获新帧，单次最长五秒。
   Screenshot 仍一次性；第一次失败、撤销或后续失败后不得自动再次授权。
3. DesktopImageTurn 持有且仅持有自己的会话，每次观察重新检查 screen.capture 权限，
   单轮最多八帧；权限拒绝、取消或自主授权代际变化在下一有界检查/任务收敛时释放，下次捕获不能重新授权。
   析构、终态和异常关闭 Session 与私有总线；不保存 restore_token。
4. ActionCoordinator 暴露平台无关 authority_epoch：普通动作只淘汰旧图片，不撤销
   当前任务的共享；takeover/resume/释放失败改变授权代际，fenced 时为零。后续观察
   必须检查原授权代际，不能跨人工接管恢复而复用旧共享。
5. 私有 GMainContext 仅在调用方 Executor 有限任务内推进，空闲期间没有新 worker、
   定时器或队列。回调保存有界结果/Closed 标志；会话仅由其调用方访问。每帧释放
   PipeWire stream/core/context/loop。取消后运行任务收敛即销毁会话；任务结束后
   不留后台共享。其他任务拥有独立会话和取消上下文。
6. 共享图仍只读，无全局坐标、输入引用或 Wayland WindowProvider；后续 RemoteDesktop
   联合会话须另验设备授权、逻辑坐标、撤销与按键释放。此增量不宣称键鼠已经接入。
7. 终态门禁保持拒绝未完成任务，但区分“未派发且无副作用的拒绝”和“派发后未验证/
   不确定副作用”。模型得到明确恢复提示；不得把前一种改成成功，不得自动重发后者。

验收：真实失败脱敏取证；模型循环跨五秒候选重验、目标消失/异常/拒绝/epoch/取消；
任务会话复用、单次授权、权限撤回/终态关闭/撤销/超时和独立 owner；X11 回归及
ASAN/UBSAN/TSAN。真实 GNOME 共享复用由维护者配合操作，未执行不标记完成。
