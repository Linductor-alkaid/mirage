# DEC-106：不支持避让时保留视觉能力

> 状态：Accepted
> 日期：2026-10-18
> 负责人：Mirage 维护者
> 冻结里程碑：M8-06
> 细化：DEC-103 / DEC-105 的能力降级默认值

## 决策

维护者明确要求：不支持的 compositor 保留截图/OCR 等视觉能力，不强行隐藏桌宠。
避让是可选平台能力，不能因为没有已适配的隐藏方式而禁用原有桌面 Provider。

平台在修改任何窗口前判定支持情况。未知 Wayland compositor、没有可识别的
XWayland WM、未编译对应 backend 时返回显式未排除的被动租约，不发隐藏/移动/
恢复请求，不提高 Portal 源帧时间水位。切换到未排除模式时清除过期排除水位。
Runtime 继续持有受管进程启动门禁，生命周期与取消保持可见；任务使用原来的
截图/授权/参考解析和动作守卫，不赋予桌宠像素特别输入权限。

PresentationLease.excluded() 区分排除成功与被动模式。工具的对象结果附加
pet_presentation={excluded:false,reason:unsupported_compositor,pixels_may_include_pet:true}；
直接 desktop.observe 在既有 environment_state 中注明未排除，不新增 IPC 字段。
模型与工具证据不得把这些截图称为已经去除桌宠的干净帧，不遮罩或合成底层内容。

只有预先探测的能力不支持才降级。已支持 backend 上的身份校验失败、撤下失败、
超时、取消、帧水位失败、恢复失败及提交拒绝仍返回原错误，不吞掉故障或在窗口
状态不确定时继续操作。保留 supported GNOME 与纯 X11 已有避让路径。

## 验证与限制

私有 WM 验证未知 compositor 的窗口/WM hints/帧水位不变、被动租约取消及幂等；
工具注册链、直接观察与 Runtime IPC 验证未隐藏时仍能使用 Provider，且证据标注
未排除。原权限拒绝、身份错误、超时及恢复失败回归保持。M8-06 的真实 compositor
验收继续开放，不以协议夹具冒充目标桌面通过。
