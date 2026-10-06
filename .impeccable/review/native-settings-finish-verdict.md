disposition: ship

输入适用性：本轮沿用用户指定 ZCode 的 code-led 原生方向，未重 roll；无 approved comp、随机世界 QUALITY BAR card 或适用于 C++ EUI DSL 的 HTML detector。独立 reviewer 按既有方向契约、原生 DESIGN 与 craft floor 评审；ZCode 参考源码仅抽样外观设置与导航，未通读其业务实现。

## persistence

pass。PRODUCT.md 存在；docs/design/native-agent-frontend.md 的 FORM 保留 `user-zcode-native`，并记录本轮可调侧栏和设置入口。apps/native/DESIGN.md 与 apps/native/.impeccable/design.json 已同步实际 Palette、224–400px 宽度、主区至少 520px、窄设置行、入口、快捷键和进程内偏好边界。code-led 无 comp-round/state/spec 要求。

指定的 12 张截图全部存在并经 view_image 检视，无黑屏、空白损坏或裁切；正常截图为 2360×1600，最小窗口为 1720×1240，分别对应 2× 的 1180×800 与 860×620。`chat-sidebar-min` 展示侧栏最小宽度，窗口保持正常尺寸，内容与名称相符。另检视 about、after-modal、chat-collapsed-dark；最新 appearance-light 已复看，返回按钮为“返回对话”。native-settings-evidence.json 中全部截图及 app.cpp/window_controls.* 的 SHA-256 与当前文件一致。

交互结果与源码相符：鼠标拖动、左右键 8px 调宽、224/400 钳制、最小窗口 340px 上限、扩窗恢复请求宽度、收起入口、整页明暗切换、设置期间聊天快捷键保护、中文草稿保留、Ctrl+, 与 Escape 模态优先。证据记录 Debug/Release 构建、四配置模型 CTest 各 1/1、ASAN GUI 退出 0 且无诊断；本 reviewer 未重跑这些测试。

验收仅覆盖本轮 Linux 原生窗口内侧栏/设置 UI；Windows、后端连接、系统主题同步、跨进程持久化与 Dock 像素不在本轮验收范围。

## fidelity

| 元素 / 承诺 | 判定 | 证据 |
| --- | --- | --- |
| TYPE | match | 系统中文无衬线、外观标题 30px、导航 16px 与说明层级继承 OWN-WORLD；无新增装饰 display 字体。截图中图标与文字共用行框，标题、按钮、说明未相互挤压。 |
| MATERIAL | match | EUI 平面容器、细描边、有限圆角与 Font Awesome 图标遵循既有系统；没有伪金属、玻璃、纹理或阴影装饰。Mira 品牌 raster 在展开侧栏清晰可见、保留比例与颜色，来源由 DESIGN/sidecar 记录。 |
| GROUND | match | 实际像素取样：浅页底 (248,248,248)、侧栏 (240,240,240)；深页底 (22,22,22)、侧栏 (29,29,29)。最小窗口主题面为纯白 / (34,34,34)，与契约中性灰角色一致，无暖奶油或蓝黑漂移。 |
| 可调会话栏 | match | 默认、320px、224px 与最小窗口 340px 截图保持品牌、会话行、底部入口完整；源码按主区 520px 下限约束，交互记录覆盖拖动、键盘与扩窗恢复。 |
| 设置入口与导航 | match | 原底部主题位为齿轮设置入口；外观导航有独立选中底色，返回对话位置明确；收起后顶栏保留设置及返回入口。 |
| 外观主题行 | adaptation | 宽主区左右排列，窄主区标签与选择上下排列。依据本轮最小窗口要求及 DESIGN 的内容宽度 <600px 规则，省略重复说明、保留即时应用声明，两种主题均无溢出。 |
| 主题选中与即时应用 | match | 浅色与深色都有填充选中态，图标和名称可辨；对应截图整页 Palette 同时变化，行为证据记录即选即用。 |
| THESIS / FIRST VIEWPORT | match | 会话栏、阅读列、底部 composer 的既有拓扑保留；设置页沿用同一列。最小窗口仍完整显示主要入口、三条建议、composer 状态与底部提示。 |
| STORY / 产品事实 | match | 预览、Agent 未连接与本地消息生命周期明确；主题声明限定“本次运行”，返回后的测试草稿保持原文，未制造 Agent 回复或持久化承诺。 |
| FORM / 品牌 | match | 继承 `user-zcode-native`，延续用户指定 ZCode 导航/设置行方向；没有未经授权的新视觉世界、空设置分类或新商业内容。 |

craft floor：本轮截图无 kicker、渐变文字、彩色侧条、硬阴影、Unicode 临时图标或装饰性材料。浅色辅助文字在页底 / 面 / 侧栏的计算对比度为 4.87 / 5.17 / 4.54，深色辅助文字在页底 / 面为 7.00 / 6.16；正文、选中标签与主题按钮均清楚可读。系统字体与静态平面处理由既有原生契约明确指定。键盘可达行为有交互证据；本组截图未单独证明所有 EUI 焦点环的绘制。

## ceiling

reached（本轮既有方向范围）。ZCode 参考的侧栏导航、设置标题/说明、细边框设置行和克制中性色已落到原生实现；窄窗口通过重新排列保持控件大小与留白。额外深度、装饰字体、密集 ornament 或页面入场动效不属于该已冻结方向的承诺。

## material_fixes

none。

## keep

保留中性明暗色阶、清楚的选中态、窄主区重排、Mira 原图品牌，以及草稿/宽度在当前进程中的连续性与明确预览边界。
