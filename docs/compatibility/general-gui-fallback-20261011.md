# 通用 GUI 回退：2026-10-11 本地验收

> 工作项：M7-15（In Progress）
> 决策：[DEC-072](../decisions/DEC-072-general-gui-fallback.md)
> 范围：Linux X11；离线模型脚本、私有 Xvfb/状态目录

## 交付行为

新增 `desktop.vision.click_region`，选择已供给截图中的矩形，绑定唯一 image_scope 与
epoch，通过 Mirador 公开变换恢复源区域，再在既有权限/动作租约内新捕获核对区域像素。
窗口范围保留前台限制；显示范围可覆盖桌面图标/面板/菜单。背景窗口的截图保持只读；
configured capture_scope 同时约束采集和动作。每次换图身份不同，失败换图退役旧来源。

供图仍五秒新鲜；已经进入模型请求的区域意图可在六十秒内由新像素核对后执行。本轮
实测等待 5.1 秒后成功，不把旧图片当作当前证明；六十秒上限由工具门禁约束，未做一分钟
墙钟过期实验。移动目标不会被追踪，目标区域改变时拒绝并要求重新观察。

新增 foreground_changed、foreground_title_equals、display_text_exists 局部条件，
分别验证前台推进、新前台准确标题及固定显示器内唯一高置信度新 OCR。后者在 OCR 前后
检查显示几何。模型指导明确快捷工具失败后的等价 GUI 路线，以及派发/不确定时先核验。
UI 证据可显示显示器目标，不新增用户配置、IPC 方法或执行 owner。

## 真实工具闭环

新夹具 Environment 故意没有 Application/Accessibility Provider，模型每一请求检查
application.launch/open_uri 均未暴露。Mira ConversationLoop 的脚本首先取得真实
Xvfb PNG，在图片中选择纯图形启动器，派发实际左键完整点击；测试界面响应松键打开
目标窗口，随后通过新窗口观察、Ctrl+L、URL 文本和 Enter 更新目标页面状态。最终
检查真实窗口标题、收到的 URL、新图片供给、没有丢图，按键/鼠标均释放。

URL 为公开夹具 `https://example.test/`。目标窗口为测试 GUI，不是 Firefox，未请求
外网、加载模型或读取用户浏览器数据。该结果证明工具链支持 GUI 路线，不证明在线模型
能自主找到图标/识别任意浏览器，也不宣称原 Bilibili 任务完成。

自动化负例包括无初始前台、窗口外目标、负原点/2×预览比例、背景截图、区域变化、
焦点/显示几何变化、无关区域变化、重复图片新 scope、没有实际供图、窗口采集范围、
owner/deadline/权限/接管、采集预算、Provider 异常、派发后取消、不确定及不重发。
只有局部条件满足才可推进；只看见界面变化不能宣称业务成功。

## 验证记录

| 检查 | 结果 |
| --- | --- |
| Debug / Native Release 全量 | 66/66、72/72 |
| ASAN+UBSAN / TSAN 定向 | 各 11/11，错误即失败；含两个状态隔离 fixture |
| 通用 GUI 合约与真实输入 | 每配置 243 checks；真实 X11 GUI-only 闭环 |
| 实际 Release / 新 DEB 解包 | 各 37/37，11 个零模型 workflow、6 次点击、两次 Runtime 正常退出 |
| 公共头 / 格式 / diff | 53 头零违规、12 个 C++ 文件和原生标签局部格式通过、diff 通过 |
| 用户配置/历史 | SHA-256 与 mtime 不变 |

最终测试计数、源文件/日志摘要、私有产品运行及包摘要见
[机器证据](data/general-gui-fallback-20261011/summary.json)。所有 CTest 使用独立 XDG
目录和 setup/cleanup；sanitizer 沿用现有构建与系统 GLib/GIO 抑制，不新增抑制。
实际 native-release 桌面入口及本地 DEB 重建；Release/解包产品继续运行零模型
OCR/模板 workflow 矩阵，保持已有本地知识路径。

首次编译的影子变量、测试结构字段/初始化顺序错误已修正。首轮定向 CTest 错误地与
同目录链接重叠，导致一个 binary permission denied；该轮不构成有效验收，后续严格
先构建再测试。旧截图测试假定仅两个工具、失败替换仍保留图片；新契约导致旧测试在
空结果上访问 front()，产生 SIGSEGV。更新目录断言、有界空结果检查，并单独验证真正
的五秒图片过期，没有放宽供图约束。

GUI 脚本第一次把 canonical object digest 当作 ToolCallOutput 的原始载荷 digest，
Mira 正确拒绝；修正测试载荷摘要，不修改上游。首轮 Debug 全量托盘夹具等待
PropertiesChanged 超时，单项复跑通过；未定位原因，不归因本轮区域工具或擅自调整
托盘超时，保留原日志和全量复验结果。

## 使用与剩余范围

用户从托盘完整退出并重新启动 Mirage，新建会话后可使用该工具。无需配置 OCR/ncnn
运行环境；原有模型识图与桌面权限仍需已启用。可请求“快捷工具不可用时，通过截图
和键鼠操作打开浏览器并进入网址，最后验证页面”。输入执行后以新页面证据判断结果。

真实用户桌面、在线模型自主选路、任意应用/菜单、多屏/非整数 DPI、Windows/原生
Wayland、移动目标重新定位和会话持久资产选择均保留未验。负责人 Mirage 维护者；
补跑条件为新版运行、目标桌面/应用及已授权识图模型准备好。M7-15 与 M7-11 整体
保持 In Progress，未提交/推送或发布，不用离线 fixture 关闭在线门禁。
