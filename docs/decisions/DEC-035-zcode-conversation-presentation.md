# DEC-035：原生会话页对齐 ZCode

> 状态：Accepted
> 日期：2026-10-04
> 关联：M6-05、DEC-033/034

维护者要求整个会话页、上下文展示与用户输入完全参考 ZCode。以公开 main
29628c9acdb81b703bbd4080c207a0e7ce5e276e 的 ConversationTimeline、ConversationRowView、
ConversationComposer、ChatPromptEditor、composer toolbar 和 Zai 主题为具体依据。
运行中的 ZCode 使用 Wayland，GNOME ScreenshotWindow 返回 AccessDenied，未绕过授权；
不能将源码对齐描述成已完成运行中 ZCode 的像素对照。

会话采用右对齐用户气泡、无卡片 Agent Markdown、复制/引用动作；草稿居中最大672px，
会话列按主区宽度在864px启用96px留白，最大896px。空态取消旧建议列表；输入为
16px、12px内边距、16px圆角，随内容增高，底部工具顺序为加号、模式、模型、上下文、发送。
继承 Mira 身份、可调侧栏、现有设置与窗口控制。设置不在本轮重做。

上下文引用是用户明确选取的消息文字快照，最多4条、总8KiB，连同草稿的提交仍受16KiB
wire上限约束。引用进入普通User输入，经现有RuntimeBridge/Executor发送；不读本地文件、
不开放桌面能力，不自建网络/任务状态机。ACK只清除本次引用，发送后的新草稿/引用保留。
上下文面板展示已载入历史、引用数量与文字容量；Token用量尚无服务事实源，不填假百分比。
模式对应现有agent=true/false。模型入口显示已应用模型，并进入现有设置；不假造模型目录。

Markdown复用EUI公开MarkdownBuilder及既有md4c，不实现第二套解析器，不修改依赖。
其当前公共组件没有文字选择/链接点击回调：整条复制可用，精细框选与链接打开保留明确限制。
UI均在主线程；剪贴板经私有GLFW窗口Adapter，IPC并发路径不变。

验收：Debug/Release构建，引用容量/会话隔离/ACK保护测试；正常/最小窗口明暗、
Markdown/长输入/上下文/发送停止与草稿保护实机取证；独立视觉复核与native DESIGN同步。

实机发现公共MarkdownBuilder会在连续汉字间添加4px gap。按EUI-20261004-004
记录问题，在单一markdown_adapter.hpp边界通过公开DSL压缩原文实际相邻的CJK段；
保留上游换行和高度预算，格式边界/显式空格不压缩，不引用私有解析类型。
引用实例有独立单调ID，ACK清除实例而非源消息ID，删除后重新引用也不会被迟到ACK清除。

引用展示采用ZCode ConversationSelectionReferenceChip的数量pill与可检查弹层，
显示真实原文和源消息角色/ID，可逐条移除；最多4条/8KiB的既有预算不变。
