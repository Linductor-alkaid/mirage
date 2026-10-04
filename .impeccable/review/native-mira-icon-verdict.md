disposition: ship

角色说明：标准 agents 路径不存在，使用 reference/degraded/finish-reviewer.md 的审核契约执行独立复核；本轮仅覆盖用户指定的 Mira 图标继承及侧栏品牌布局。未提供独立 QUALITY BAR 卡、approved comp 或 detector findings；此次是已有 code-led 原生表面的窄范围扩展，不对整页重新选方向。无 HTML detector 运行。

## persistence

pass — 根目录 PRODUCT.md 存在；apps/native/DESIGN.md、apps/native/.impeccable/design.json、assets/provenance.json 与 docs/design/native-agent-frontend.md 均记录本机最新红发蓝眼 Mira 来源、36×36 contain、x=24/y=12 图像与 x=72 字标、元数据适配及未验收的 Windows 边界。方向 FORM 保留 user-zcode-native；本轮以维护者明确指定图像为权威，不重新抽取方向。

四张指定实机 capture 全部逐一查看，画面完整、内容与主题命名一致，无缺失/空白区域：light/dark 各为 2360×1600（1180×800 逻辑，2×），min-light/min-dark 各为 1720×1240（860×620 逻辑，2×）。

原图与导入 mira.png 的字节完全相同，SHA-256 均为 e615d7e768c532e9966e97fbbaeefc4b7212ef386521bef6be680ed3d250bbed；mira-ui.png 与源图均为 1254×1254，全部 RGBA 像素一致，摘要与 provenance 相符。

## fidelity

原图显著元素独立观察：红发、单只睁开的蓝眼、浅黄色交叉发饰、蓝色圆形背景、双手指向脸颊的姿态、自然透明外缘。四张实机图均呈现这一角色，未替换为旧海报或重绘素材。

| 元素 | 判定 | 证据 |
| --- | --- | --- |
| Mira 角色与构图 | match | 四张截图左上品牌图显示红发、蓝眼及蓝色背景；RGBA 校验证明画作、构图、alpha 完整保留。 |
| 图像缩放与透明轮廓 | adaptation | 用户要求继承图标；36×36 contain 是已有桌面侧栏品牌行的尺度适配，四个视口无拉伸、裁切或人工蒙版。 |
| 图像/字标关系 | match | app.cpp 为 (24,12)/36×36 与 x=72/36px 行框；截图中图像、Mirage 字标和侧栏按钮同排，图像与文字之间有明确空隙，没有遮挡。 |
| TYPE | match | 继承原有原生系统无衬线字标，符合既有 OWN-WORLD；此次未制造装饰字效。 |
| MATERIAL | match | 用户指定的真实 raster 完整可见；元数据清理不改变任何 RGBA 像素，无向量替代或假材质。 |
| GROUND | match | 四图采样侧栏浅色 RGB(240,240,240)、深色 RGB(29,29,29)，主区浅色 RGB(248,248,248)、深色 RGB(22,22,22)，与已有 DESIGN.md 中性灰值一致；角色色彩没有扩展成控件颜色。 |

THESIS、OWN-WORLD、STORY、FIRST VIEWPORT、FORM 在此次扩展范围内持续成立：品牌可在首屏立即识别，保留对话工作台结构与 Agent 尚未连接、本地预览声明。未新增演示回复或能力宣称。

## ceiling

reached — 限于 Linux 已捕获的两个窗口尺寸及明暗主题中的侧栏图标呈现。36px 角色作为品牌识别点清楚可见，既有透明轮廓直接使用，无额外装饰、阴影或动效。此轮不要求重构对话页；原生既有字体选择属于用户确认方向。

Windows ICO 资源仅有配置和来源记录，其构建及系统显示未验收；截图不包含系统窗口图标的独立展示。tray 仍关闭，backend 仍未接入。上述能力均不包含在 ship 结论中。

## material_fixes

none — 指定源图、PNG 导入、侧栏派生图的像素保真及四个捕获视口中的品牌布局无缺失或矛盾项。

## keep

保留指定 Mira 原始角色、透明轮廓与原色、36px contain 品牌行、x=72 字标间距，以及 Windows 未验收、托盘关闭、Agent 未连接的明确边界。
