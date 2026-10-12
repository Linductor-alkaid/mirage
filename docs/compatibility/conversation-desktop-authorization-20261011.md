# 会话内桌面授权验证（2026-10-11）

> 状态：Superseded
> 工作项：M6-38
> 决策：[DEC-078](../decisions/DEC-078-remembered-desktop-authorization.md)
> 负责人：Mirage 维护者

2026-10-12 维护者将需求扩展为三种访问模式。本草稿的两模式设计由
[DEC-080](../decisions/DEC-080-conversation-desktop-access-modes.md) 替代，
最终证据见[三模式与观察链路验收](conversation-access-and-cua-continuity-20261012.md)。
下文保留先前范围，不作为当前交付结论。

会话输入框「默认权限 / 只读模式」菜单直接显示桌面授权状态，提供授权并记住、
忘记、刷新。菜单打开只查询状态，系统共享仅由显式授权操作触发。设置页共享同一
PageState、修订号和请求 helper，仍经原 RuntimeBridge 的有界请求路径执行。
没有新增协议、持久化、第三方改动或异步生命周期；本轮访问模式不受系统授权更改。

原生测试自建私有 XDG 目录与 Xvfb，不启动 RuntimeBridge，不触发实机共享。
覆盖授权未开/记住、等待禁用、不可用、未读取、钥匙环失败重试、重复请求拒绝、
真实指针命中嵌套滚动区按钮，以及访问模式和草稿不变。明暗正常/最小窗口使用
真实 EUI OpenGL 渲染与几何检查；系统授权能力本身沿用 DEC-078 的独立证据。

实际构建、自动结果及包验证在执行后填写。真实 GNOME 跨任务/重启恢复按维护者
要求暂缓，负责人 Mirage 维护者；补跑条件是解锁桌面、启用远程控制和允许记住选择。
Windows 真机菜单未验，本次 Linux 渲染不代替其验收。
