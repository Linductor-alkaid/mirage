# 思考通道、任务预算与菜单锚点（2026-10-12）

> 状态：自动验证通过（Linux 本地）；原用户 Wayland 任务仍待实机复验
> 工作项：M6-26 / M6-38、M7-07 / M7-10
> 决策：[DEC-082](../decisions/DEC-082-reasoning-default-and-harness-budgets.md)、[DEC-080](../decisions/DEC-080-conversation-desktop-access-modes.md)
> 负责人：Mirage 维护者

## 最新记录及修复

只就地分析最新相关轮次，未把用户历史、图像或 API Key 保存到仓库。
两次默认档的 reply_text 为作答计划，没有结构化 ThinkingPart；后续一轮有正常正文
和独立思考。任务轮次以 `turn budget exhausted before a final answer` 失败：7 次模型
调用，包含截图/OCR、一次像素变化拒绝（未派发）、重新观察后的两次成功点击。
它尚未调用文字输入或 Enter，不能据此认定键鼠输入传输失败。

旧宿主给已知 Messages 思考型号 65536 整环预算，每次开启思考至少 8192，Mira 要求
额外一次请求信封，所以只能推进 7 次。现在整环信封支持 16 次请求，仍限制每次生成、
32 次工具、零恢复；不会自动重发可能已执行的动作。整任务默认 10 分钟、单次请求
仍默认 2 分钟，纯对话只使用单次请求期限。取消、异常、准入和关闭仍经既有 Runtime
Executor / Mira ConversationLoop，不改 pinned 依赖或自建第二个循环。

MiniMax-M3 默认从省略 thinking 改为显式 Adaptive，开启档相同，关闭档仍 Disabled；
其他模型默认保留。正文只取 MessageOutput，ThinkingPart 进入既有折叠过程。提示要求
直接作答，不按中文关键词删除正文。新默认可能增加思考用量，用户仍可明确关闭。
官方说明见 [MiniMax Messages API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)。

原任务还要求向 Mirage 自身输入并发送。当前活动会话的按钮为停止，Enter 拒绝新
提交；模型提示补充此事实，符合用户目标时使用另一空闲会话，否则说明限制。
没有放开同一活动会话的忙门禁，也未宣称真实自输入/发送已通过。

## 菜单与参照

只保留对话、视觉、完全访问三个选项。菜单仍为 280×120，左对齐访问按钮，底部
距按钮顶部 4px。参照只读 ~/ZCode，提交 29628c9acdb81b703bbd4080c207a0e7ce5e276e：
V4ComposerModeControls 的 side=top / sideOffset=4，以及 DropdownMenuContent
默认 align=start；不把整个 composer 顶部当锚点。其余菜单保留原位置。

私有 XDG/Xvfb 原生 OpenGL 渲染 24,009 检查零失败，实际指针切换、三项菜单、草稿/
偏好保留、等待和运行门禁均通过。1180×800 浅色、860×620 深色一次成组视觉检查：
[浅色](data/reasoning-and-menu-anchor-20261012/composer-three-modes-light.png)、
[小窗口深色](data/reasoning-and-menu-anchor-20261012/composer-three-modes-small-dark.png)。
Impeccable 源码检测一次无报告项；测试不连接用户 Runtime 或发起系统授权。

## 模型与生命周期验证

脚本化 native Agent 334 检查零失败，包括纯对话默认通道、9 次不同工具调用后
第 10 次请求给出最终答复、显式开启多轮、跨越单请求期限但未超过任务期限、独立
单请求/整任务超时、16 轮耗尽，以及既有取消、异常、拒绝与关闭。断言每次生成
额度仍为 8192（关闭夹具为 512），整环额度不进入单次生成参数。

ASAN、UBSAN、TSAN 定向各 6/6（四个业务测试及状态 setup/cleanup）：native Agent、
Runtime Service、桌面工具、图片供给。ASAN detect_leaks=0:halt_on_error=1，UBSAN
halt_on_error=1:print_stacktrace=1，TSAN setarch x86_64 -R；无新增 suppression。
Debug 全量 70/70、Native Release 全量 76/76。先完成构建，夹具修正后全量重跑。
最终日志 /tmp/mirage-dec082-{debug,native}-full-verified.log；定向 sanitizer 日志
分别为 asan-test-verified、ubsan-test、tsan-test。

真实 M3 服务合成算术复验三档 3/3：正文均精确为 42，默认/开启有独立思考，关闭
没有思考。基线三档也能正确作答，因此默认错误尚未稳定复现，不能把缓解策略写成
已证明的供应商根因修复。探针仅使用现有 SecretRef 的只读解析，私有配置只复制白名单
模型元数据与不透明引用；不读取明文 Key、不复制用户历史、不调用桌面工具。只保存
状态、长度和匹配布尔值，不保存返回正文或思考原文。可复现脚本：
`python3 tests/manual/model_reasoning_acceptance.py --output /tmp/reasoning-summary.json`。

首次全量及 ASAN 的 native Agent 夹具有两条失败：旧超时断言混淆单调用与整任务
取消；多轮 wait 测试使用了不开放该工具的 read_only。分别改为独立验证两种期限、
在完整工具模式验证多轮，未放松权限或取消断言。第一次截图命令 cwd 错误导致缺字体，
已在 apps/native 工作目录重跑，字体与图标断言全部通过。中间编辑命令也因错误 cwd
未找到源文件，使用绝对路径后已应用。原失败日志 /tmp/mirage-dec082-{native-full,
debug-full,asan-test,ui}.log 保留。

## 交付及开放门禁

DEB 已重建，out/packages/mirage_0.1.0_amd64.deb 四个包内 ELF 与 Release SHA-256
一致。未安装、启动/重启用户产品、提交或推送；本机进程扫描时无 Mirage 产品进程。
尚需维护者使用新版，在真实 Wayland 下复测原任务、默认档直接对话以及系统授权恢复。
真实共享授权按之前“先完成自动验证”要求暂缓；M7-07 / M7-10 整体保持 In Progress。
负责人 Mirage 维护者，补跑条件为安装/启动新版、可操作解锁桌面并授权共享。

[机器摘要](data/reasoning-and-menu-anchor-20261012/summary.json)保存最终测试、探针和包校验。

同日 Git 整理已将代码保存到 6703251；同期工具选路提示经维护者授权合并核验，
Debug 70/70、Native 76/76、sanitizer 选路各 5/5 再通过并重建包。此前表格与
安装包 SHA 是原轮次快照，最终 Git/包事实见[交付记录](m7-git-delivery-20261012.md)。
