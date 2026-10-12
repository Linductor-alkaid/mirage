# DEC-061：本地模板资源与几何候选匹配

> 状态：Accepted（维护者授权继续推进）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 扩展：[DEC-057](DEC-057-real-ocr-composition.md)、[DEC-060](DEC-060-geometric-visual-marks.md)

## 背景与备选方案

DEC-060 已提供无文字闭合候选，但缺少产品模板资产装配；Mirador VisualIndex 已能
比较定位 patch，适合复用。继续只发布几何编号不能检验模板身份随位置变化保持；
自动捕获用户画面登记会扩大隐私/状态边界；整帧模板扫描或新增 Detector 超出本轮
需求。选择显式离线原始像素资产与已有几何候选，限制格式、容量和匹配范围。

## 决策

OCR manifest schema 1 可选 templates 数组，1..16 个；要求 geometry_marks=true。
每项只有 id、format、path、sha256、bytes、width、height。id 是唯一的 1..64
ASCII 字母/数字/点/下划线/连字符；format 固定 bgra8，尺寸各 8..256，紧密行布局，
bytes 必须等于 width×height×4。每文件最多 256 KiB，总像素最多 4 MiB。
启动时有界读取并核对精确大小/摘要，不联网、不自动截取用户画面注册、不加载图片
编解码依赖。清单和所有资源摘要参与既有 canonical revision；变更需重启 owner。

模板像素由 session 配置拥有。首次模板编号请求在现有 serial blocking worker
逐项注册到 Mirador VisualIndex，之后在符合 DEC-060 面积条件的闭合提案 tight
patch 上 probe；无整帧扫描或自研相似度/NMS。取消、期限、失败、拒绝及关闭继续
通过既有 request/future/worker 可见。普通 OCR/固定业务条件不运行模板匹配。
首次注册错误或资产身份被驱逐时整次编号失败，不用部分库冒充完整配置。

产品使用 32×32 指纹、64 KiB index，查询阈值/复用阈值 0.95，max_candidates=16，
覆盖全部产品资产，不能因查询截断隐藏 runner-up。Mirage index adapter 的 probe
也拒绝小于当前库容量的候选预算，并检查所有返回层级的强命中；不修改 Mirador query。只有唯一强命中产生模板证据；
同 patch 的任意其他强命中、一个融合区域内不同身份/不同位置的模板命中，或
融合扩大了匹配框均不发布身份，防止容器改变模板的点击 anchor。
弱/歧义结果保留几何编号，不假装匹配失败就是不可点击。

desktop.vision.mark 升至 1.1.0，编号映射增加 template_id（无匹配为空），身份与
本次 scope/ref/同源像素一起发布。OCR 与模板混合证据保留真实 OCR 文本来源及
模板 ID，但 ID 仅表示配置图像相似，不能声明按钮角色、可交互性或业务成功。
点击仍使用编号，不接受 ID 作为可执行引用；固定后条件门禁不变。

## 验证与边界

验证资源类型/尺寸/摘要/重复身份/预算拒绝、双模板区分、位置交换、弱/歧义匹配、
融合冲突、普通 OCR 无模板、裁切/换代、取消/期限/拒绝和 shutdown。
真实 Xvfb Canvas 用公开自绘图标、外部 PP-OCR、真实 Screen/XTest 与同源编号；
模板选号由可复现脚本驱动，点击后仍以新 OCR 业务文本判定，负例不得虚报成功。

不支持无几何候选的自由轮廓图标或宣称跨主题/缩放通用识别。模板资源由维护者审核，
其 ID/匹配结果不提升外部画面信任等级。宿主、在线模型、DPI/目标平台、高分辨率
预采集及完整权重 TSAN 期限复验仍按既有门禁承接。未新增依赖或修改 pinned 源码。
