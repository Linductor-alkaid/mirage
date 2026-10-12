# 原生 CUA 的访问与观察方案（2026-10-12）

> 状态：Active
> 负责人：Mirage 维护者
> 关联：DEC-080、DEC-081、M6-38、M7-10

维护者要求分析最新失败，并学习其他 CUA 项目。以下是官方资料支持的能力，不据此
宣称 Mirage 或他方已经通过本机 GNOME 验证；平台细节仍需独立取证。

| 官方项目/资料 | 已说明的方案 | Mirage 本轮采用的思路 |
| --- | --- | --- |
| [Cua Driver 架构](https://cua.ai/docs/cua-driver/concepts/how-cua-driver-works) | 同一观察结合可访问性树与图像；逐级改用元素、像素、浏览器或前台动作；回执区分 confirmed、unverifiable、refused，并给出升级路线 | 以本轮注册表报告真实路线；缺窗口 API 时保留已授权的共享画面像素输入；收到输入不等于目标完成 |
| [Cua 权限](https://cua.ai/docs/cua-driver/guides/permissions) | 所有调用经过运行时授权，模式、能力清单与策略只能缩小权限 | 三个产品模式必须落实服务端路径和工具集合，不能只换按钮文案；系统授权不覆盖任务模式 |
| [Claude Computer Use](https://platform.claude.com/docs/en/agents-and-tools/tool-use/computer-use-tool) | 应用执行模型提出的截图/键鼠工具调用，返回结果，再继续模型循环 | 保持 Mira 循环与 Mirage 平台执行边界；截图、动作、重观察在同一受控生命周期 |
| [Browser Use 参数](https://docs.browser-use.com/open-source/customize/agent/all-parameters) | use_vision 可控制始终、按需或不提供截图；视觉与工具注册是明确能力配置 | 对话模式不供屏幕图/桌面工具，视觉模式可观察但不能修改，完全访问仍受权限和图引用守卫 |

本轮不引入这些项目为依赖，不复制其平台实现。建议优先修正工具闭合集合和 OCR
替代引用可见性，再解决静态 PipeWire 观察的源帧所有权；授权恢复与目标验证分别验收。
缓存语义可辅助定位，但实际点击仍需执行前重校验。外部平台支持声明不替代本项目
PipeWire/Portal、DPI、输入法和取消测试。
