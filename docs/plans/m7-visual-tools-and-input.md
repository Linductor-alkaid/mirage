# M7：视觉工具与键鼠输入闭环

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M2/M3 桌面与视觉适配；M6 当前原生会话/托盘 Runtime owner
> 建议发布点：桌面工具预览版（名称与 tag 由维护者另定）
> 更新日期：2026-10-12

## 目标

用户在现有原生会话提交桌面任务，Agent 能按需取得真实观察，定位目标，经统一权限
执行键鼠输入，并用新观察给出成功证据或明确的未验证/失败结果。先交付 Linux X11
结构化工具闭环，再加入真实图片、OCR/SoM 与各平台扩展。

## 范围与非目标

范围：会话工具装配、真实视觉接线、标准图片 artifact、快照作用域引用、有界完整
输入、执行回执、取消释放、动作后观察、原生会话已有工具过程面上的证据呈现。

首增量不含 workflow 编辑器、录制回放产品面、实时控制、ObjectTracker 转正消费、
全量 MCP 产品面或模型训练。M6 Windows/Wayland/安装包待验收仍保留原状态；M7 分平台
取证，不以 X11 结果代替其他平台。完整图片附件管理不是桌面截图工具的前置。

## 设计与决策依据

- [DEC-068](../decisions/DEC-068-bundled-vision-and-recoverable-tool-rejection.md) 冻结默认视觉包与可证明无副作用的工具拒绝恢复。

- [能力调研](../research/2026-10-09-visual-tools-and-input-capability-review.md)。
- [DEC-052](../decisions/DEC-052-visual-tools-and-input.md) 与
  [DEC-054](../decisions/DEC-054-scoped-complete-desktop-actions.md) 冻结 M7-01/03 契约。
- [DEC-055](../decisions/DEC-055-desktop-image-supply.md) 冻结 M7-04/05 图片供给边界。
- [DEC-056](../decisions/DEC-056-post-action-verification.md) 冻结 M7-08 局部后条件和终态门禁。
- 既有 DEC-005/009/010/016/024/034；主设计第 5–9、12、15 节。
- [Mira 环境 API](../../third_party/mira/docs/api/environment-observation.md)、
  [模型与工具 API](../../third_party/mira/docs/api/model-agent-loop.md)。
- pinned mirador/kairo 集成指南；沿用已交付管线和 Runtime owner。

## 工作项

M7-01/03 本轮按 DEC-054 闭合 Linux X11 的契约和完整输入，M7-02 随之补齐
click/key 的结构化 fixture。后续 M7-04/05 已闭合 Linux PNG 与合成图片在线识图。
M7-08 已闭合 Linux X11 动作后新观察与局部条件门禁；真实语义表单、OCR、视觉编号
和其他平台分别验收。

| 工作项 | 状态 | 结果与边界 | 验收 | 前置 |
| --- | --- | --- | --- | --- |
| M7-01 | Completed（Linux 首交付） | 冻结工具清单、观察/引用/动作回执、权限和输入释放契约；同步主设计、DEC-052、示例与安全边界 | scope 重名、epoch 变化、权限拒绝、取消释放、部分派发失败的 fake 契约测试；schema/golden 与公共头检查 | 本调研 |
| M7-02 | Completed（Linux 首交付） | 将 Provider 工具接入现有 ConversationLoop，复用现有原子装配与工具过程事件；缺 Provider/权限不暴露或明确拒绝 | fixture 完成 observe→activate/click→key/type→observe；注册目录与实际能力一致；只读轮次不产生副作用 | M7-01 |
| M7-03 | Completed（Linux 首交付） | 完整点击/组合键/文本动作、桌面单写者、输入所有权与取消释放；显式表达不确定副作用 | 按下后取消/异常/超时/shutdown 均清理；第二会话动作拒绝；中文/换行支持与拒绝可预测 | M7-01，接 M7-02 |
| M7-04 | Completed（Linux PNG） | display/window/ROI 标准 PNG 与受控 artifact source；DEC-068 更新显示/窗口为有来源比例的只读预览，ROI 1:1 与有界保留 | 独立 PNG 解码 30,000 像素；ROI/负原点；4K/次数/年龄预算、几何/epoch/取消与权限前置 | M7-01 |
| M7-05 | Completed（Linux 合成图） | Mira 每请求 supplier、真实 ImagePart 和显式逐模型识图配置；缺图/drop/degrade 失败 | 三方言 fixture；MiniMax-M3 两张实际截图工具在线识图 2/2；取消/晚到/身份/预算/异常与关闭负向 | M7-04；PR#86 已消费，MIRA-20261009-001 Resolved（本范围） |
| M7-06 | Completed（Linux 首交付） | 产品根接真实 PP-OCR 组合 backend 和视觉 session；模型/字典/身份与缓存失效，DEC-068 原生默认固定包；公开语料字符与位置 4/4（正式 pin，见[正确性复验](../compatibility/real-ocr-correctness-20261009.md)） | 中文/英文/小字/混合 DPI 样本；坏权重/身份/预算/超时；缓存命中以 backend 调用计数证明；宿主真实桌面/DPI 与权重分发许可另列边界 | M7-01；runtime/模型供应链取证 |
| M7-07 | In Progress | 已接同源 SoM/crop/编号图、按 scope 视觉点击与后观察首增量；模板消费复用既有 session；已补固定 OCR 后条件与实际 Xvfb Canvas 5/5，显式闭合几何编号与本地模板匹配已接，双模板/位置交换/不重绘已验；实际 MiniMax 编号选择 3/3；供给范围化（DEC-062 capture_scope）、4K 小窗口 ROI 预采集/同范围重验和 owner 局部 OCR 已验；真实 Xorg 人工离线 3/3、权限/焦点门禁 2/2，在线闭环 1/2、选号 2/2，引用过期负例安全拒绝，第三例未运行 | 标记与同源图对应；换代/同名旧编号拒绝；窗口移动/负原点/缩放；纯 Canvas 控件点击闭环；宿主遮挡/权限/坐标变换取证 | M7-03/05/06 |
| M7-08 | Completed（Linux X11 局部条件） | 修改前固定窗口/语义条件，副作用后强制新观察和只读重验；宿主终态门禁、工具卡目标/状态/证据 | 真实 X11 激活/回显/落盘三类任务；假成功、前台/epoch 变化与不确定重发拒绝；固定 OCR 条件/Xvfb Canvas 已另验；真实语义表单/用户宿主另验 | M7-02/03；视觉场景加 M7-07 |
| M7-09 | In Progress | DEC-064 Linux X11 编号滚动/完整拖拽与机械事件已验；GTK 原生滚动/分隔条及 XRes 下列表重排正负效果已验；宿主 Xorg 2× 四类分别取证，间歇拒绝原因/其他平台仍另验 | 最大距离/次数/时长；拖拽中取消释放；滚动后旧引用拒绝；双平台真实输入 | M7-03/08；[DEC-064](../decisions/DEC-064-bounded-scroll-and-drag.md)、[DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md) |
| M7-10 | In Progress | DEC-073/074 原生共享复用；DEC-077 产品键鼠已接入；GNOME 英文状态动态画面点击/组合键/完整 ASCII URL 与 Enter/滚动/回图及接管 11/11；Windows 采集分层升级（DEC-086：DXGI→WGC→BitBlt）已交付并本机取证；静态画面、Unicode 传输、其他输入法、Windows UIPI/Unicode/多屏 DPI 与产品宿主 DPI 声明继续推进 | Windows UIPI/Unicode/多屏 DPI；Wayland 授权/拒绝/撤销/会话关闭；缺实现显式不可用 | 首增量证据；目标平台环境 |
| M7-11 | In Progress | DEC-067/069：语义与 OCR/模板知识均重新解析新目标，经既有动作/核验进入 workflow；会话选择/恢复和持久资产后续推进 | 语义首增量见[验收](../compatibility/local-semantic-workflow-20261011.md)；OCR/模板实际产品零模型与门禁见[第二增量](../compatibility/local-visual-workflow-20261011.md) | DEC-067/069 已冻结本地知识/新执行引用契约；现有 workflow，见[路线分析](../research/2026-10-10-local-grounding-and-m7-sequencing.md) |
| M7-12 | Completed（Linux 产品启用预览） | 产品根启用 Linux Provider，启动清单转发、持久化视觉设置、原生桌面工具页与权限说明；DEC-066 | 带视觉 Release 产品检查 23/23、实际原生 MiniMax OCR/mark 完成；OCR OFF 恢复 11/11；[验收](../compatibility/product-desktop-vision-20261011.md) | M7-02/05/06/08；Linux Xorg 首范围 |
| M7-13 | Completed（Linux 默认包） | DEC-068 固定已验 Mirador / ncnn / PP-OCRv5；原生默认加载与离线安装 | 源/产物锁定、默认实际 OCR、关闭恢复、坏包/缺失拒绝；两种分辨率产品各 25/25，见[验收](../compatibility/bundled-vision-and-tool-recovery-20261011.md) | 当前已验 Mirador / ncnn / PP-OCRv5 |
| M7-14 | In Progress（最新会话复验） | DEC-068 首增量已验；DEC-070 第二增量本地修复已验，原在线任务及提交评审待闭合 | 512 字节 canonical 回执、字段诊断、拒绝→新观察→已验证纠正、旧失败卡片收尾、未知/不确定负例及 ROI；见[新验收](../compatibility/desktop-rejection-recovery-20261011.md)，[首增量证据](../compatibility/bundled-vision-and-tool-recovery-20261011.md)保留 | 用户会话脱敏诊断及现有工具契约 |
| M7-15 | In Progress | DEC-072：快捷能力不可达的 GUI 路线、绑定截图区域点击与前台/显示局部条件 | 无应用/语义 Provider 的真实 X11 启动器→键盘网址→新证据；比例、像素/焦点/权限/取消与不确定负例；在线自主选路另验 | M7-03/04/05/08，复用 M7-06 OCR及 M7-11 本地知识 |
| M7-16 | Completed（Linux） | [DEC-083](../decisions/DEC-083-target-region-stability-reverification.md)：输入守卫从逐像素比较改为目标矩形主体稳定判定（边距环忽略 + 核心区相似放行 + 不确定即拒），shared_input 与 X11 编号指针路径同语义 | 稳定判定单元用例（ identical/全局/边缘环/噪声放行/主体替换/越界/确定性）；shared 与编号点击回归；真实 X11 任务不回归 | M7-07/14/15 既有守卫契约 |
| M7-17 | Completed（Linux） | 截图降耗：desktop_png 压缩 DEFLATE（fixed Huffman + LZ77 + 自适应滤波，不可压缩回退 stored，输出不劣于旧编码）、大 ROI 预览缩放（≤1600 保持 1:1）、工具描述更新 | 独立解码器扩展 fixed-Huffman 后全像素回读；多类内容压缩比；ROI 缩放断言与坐标映射；全量回归 | M7-04 |
| M7-18 | In Progress（Portal fake 全过） | Wayland Portal ROI 捕获（共享帧本地裁剪）与窗口几何近似（AT-SPI WindowProvider 门面，遮挡内容包含、语义与 X11 framebuffer 裁剪一致）；Portal schema 放开三源；shared_input 坐标映射改 origin/scale 元数据驱动 | Portal fake ROI/窗口裁剪正确性与越界/未知窗口 fail-closed；授权计数不变；AT-SPI 不可用路径；真实 Wayland 会话另验 | M7-10（DEC-074/077） |
| M7-19 | In Progress（拒绝回执链路已验） | 观察节奏：target_pixels_changed 拒绝即发布守卫新鲜帧并在回执附 refreshed_image_scope（拒绝→免重截重试）；CUA 式观察纪律写入桌面指引与工具描述 | 拒绝回执含新 scope 且下一次免重试；canonical 回执 ≤512B 语义保持；真实会话往返减少另验 | M7-14/16 |

2026-10-10 顺序调整：认可 M7-07 已验证的受控 X11 基础能力阶段，保留该项
In Progress 与慢模型在线/完整权重 TSAN/其他平台等未完成范围。下一主线为
M7-09，其 M7-03/08 前置已完成，不以在线模型逐次在五秒内返回作为准入条件。
先冻结滚动和完整拖拽契约，再做对应实现与真实输入验收。进一步模型请求计时
作为诊断增量按需补充；M7-11 先保留 Proposed，不构成本批开发前置。

本轮准入：维护者先授权 M7-02，随后明确授权闭合 M7-01/03；DEC-054 冻结作用域
和释放契约。首验平台为 Linux X11/AT-SPI2，Windows/Wayland 完整输入与目标平台
运行仍由 M7-10 独立负责；结构化 fixture 不代替 M7-08 的业务验收。

建议分批评审：M7-01 → M7-02/03/08 的结构化 X11 闭环 →
M7-04/05/06/07/08 的受控视觉基础 → M7-09 完整滚动/拖拽。
M7-07 在线增强和 M7-10 目标平台继续按各自门禁补证；M7-11 先设计后决定准入。
图片 API 反馈和模型包评估可与结构化闭环并行推进；这指工作排序，不要求并行 Agent。

## 首批工具与后续行为

M7-15（In Progress）：DEC-072 通用 GUI 回退；截图区域输入、显示/窗口上下文与
界面推进条件，禁用应用/URI 快捷能力验证视觉→键鼠→新证据闭环。Linux X11 首验，
真实用户在线自主选路、其他平台和移动目标重新定位独立补证。负责人 Mirage 维护者。
维护者已授权准入；复用既有 Executor owner、Mira 循环和 Mirador 公开变换/OCR，
不改 pinned 依赖或用户数据；验收标准见 DEC-072。

本轮结构化 wire 清单/schema 已由 DEC-054 和 golden 冻结，完整清单、预算、示例见
[安全契约](../security/desktop-actions.md)与[验证矩阵](../compatibility/scoped-complete-input-20261009.md)。
图片工具按 DEC-055 已交付；set_text、视觉引用与受控坐标仍是后续工作边界：

- observe：结构化前台窗口/语义树/指针/显示器信息，按需请求视觉；返回 observation ID、
  scope/epoch、捕获时间、来源质量与支持能力。图片工具返回同源 artifact 元数据。
- activate / set_text：语义动作优先，target 必须绑定观察身份。回退到输入需明确记录。
- click：ref/scope 目标优先；受限坐标回退必须绑定截图与 display 变换，不接受无来源点。
- key：一个完整按下/释放组合，不将独立 key-down 作为默认工具。
- type_text：明确焦点、文本预算与 Unicode 支持；不得静默切剪贴板或丢弃字符。
- screenshot / crop：仅对识图能力和逐轮图像输入均已接通的模型开放；artifact 元数据
  本身不能冒充模型已看图。ROI 限制面积、递归细化深度与调用数。

输入执行与业务验证分开：一次点击派发成功只说明平台接受输入；工具提供新观察和
验证结果，不能仅凭模型回答、画面变化或固定 sleep 声称任务成功。

## 生命周期、容量与失败约束

Runtime Service 持有工具上下文、桌面环境、视觉 session、artifact store 和动作租约。
有限步骤由 Executor 任务承载并消费 future；每源视觉 session 沿用 blocking worker。
模型 handler 可在现有 Executor 任务内做有界步骤，阻塞调用须保持取消出口；不得
引入额外线程、私有队列/定时器。跨上下文按消息/最新值/广播语义使用 comm。

取消由 Task owner 同时进入 Mira OperationContext 与 Provider CancelToken；不依赖
只在工具入口检查取消。cleanup 对本动作持有输入幂等，不能因为 token 已取消而拒绝
释放。任务、观察、图片/模型轮次、引用和输入租约均有明确预算；数值由 M7-01 冻结。
超时/拒绝/背压/降级与 Unknown 回执必须可观察，不自动重新执行未知副作用。

关闭：停止生产者→取消任务并阻止新动作→释放本动作按键/鼠标→回收视觉/平台 worker→
结算有限 future→外部非 worker owner `shutdown(true)`。运行中软超时不保证强制终止。

## 风险与阻塞

| 项目 | 影响与处置 | 负责人/解除条件 |
| --- | --- | --- |
| MIRA-20261009-001 | PR#86 消费、artifact/source/supplier 与下游/在线复验完成；本地 Resolved | Mirage 维护者；上游 issue 状态未操作，沿用最后回读 Open |
| 模型包/runtime | 参考实现非默认产品依赖，权重/字典不存在时明确不可用；YOLO 是否适用桌面尚待实测 | 维护者；锁、许可、摘要、离线资源、真实样本与成本证据 |
| 引用重名与换代竞态 | scope/ref 同锁边界已由视觉点击消费；旧编号/换代拒绝已验，宿主坐标变换仍待验 | 维护者；M7-07/M7-10 宿主变换验收 |
| Unicode/焦点/用户接管 | X11 当前键盘映射限制，焦点改变可能导致输入落错窗口 | 维护者；语义输入与后备方案取证，接管后阻止动作并重新观察 |
| Wayland/Windows | 当前宿主不提供所有目标平台与授权环境；不可扩大 X11 声明 | 维护者；目标环境执行 M7-10 |

## 测试与退出条件

- [x] 首批结构化契约与权限冻结；无第三方类型泄漏，无重复循环/感知基础设施。
- [ ] 正常完成、handler 异常、提交拒绝、运行中取消、超时、shutdown 六类路径通过；
  追加晚到响应、释放失败、部分输入、同 scope/ref 重名、前台/epoch 变化、背压和并发会话。
- [ ] Debug/Release、ASAN/UBSAN；涉及跨上下文状态与关闭的 TSAN/故障注入通过。
- [ ] 真实语义表单：定位→聚焦→输入→提交→可观察回显；不是只用 fake Provider。
- [x] 无 a11y 的文字界面（Linux X11 受控 Canvas）：真实 OCR→引用→点击→新观察验证；
  四类文本正例和画面不变负例 5/5，范围见[验收记录](../compatibility/visual-text-canvas-20261010.md)。
- [ ] 无 a11y 的 Canvas/图标界面：图片/SoM→编号→点击→新观察；固定样本，记录
  错点/拒绝/恢复与耗时，不设未经基准支持的性能承诺。Canvas 文字、无文字闭合按钮与本地双模板/位置交换闭环已取证；实际 MiniMax 无身份提示选号/换位/不重绘 3/3 已取证；无轮廓图标、其他模型与用户宿主待验。
- [ ] 真实目标窗口取消/紧急停止/关闭后无本动作遗留按键或鼠标按下状态。
- [x] 图片方言 fixture 与本轮实际启用的 MiniMax 分别取证；截图/文本敏感载荷不进入普通日志。
- [ ] 平台矩阵、供应链、设计/决策/计划/示例/协议证据同步；不以 skip 关闭目标平台项。

M7-01～05、08 的适用验收已通过；上面的未勾选项是 M7 整体剩余门禁，不能把本轮
单元/fixture 与 X11 组件证据当作真实业务表单、全视觉或跨平台验收。

## 验证记录

2026-10-09：本轮仅完成公开 API/产品接线分析与 Proposed 计划。
调研起点 Mirage `a722f38`，依赖 pin 与锁一致；Linux x86_64/GCC 13.3.0 的图片 API
形状探针编译退出 0，命令见调研。Python 文档校验检查本轮 7 份 Markdown 的唯一
一级标题、代码围栏及 176 个相对链接，退出 0；`git diff --check` 退出 0，
`git diff --cached --stat` 为空，未提交。工作分支 `docs/visual-input-roadmap`。
没有实施上述工具、下载权重、升级依赖或运行产品验收，全部实现项保持 `[ ]`。
负责人 Mirage 维护者；DEC-052/M7-01 冻结后按各项前置与测试矩阵补跑。

2026-10-09：维护者授权提交 Mira 图片输入缺口。GitHub master 仍为 `bfcb8e7`，
公开 ConversationLoop 入口与 pin 一致；现有 issue 核对无重复，API 探针复跑退出 0。
创建 [Mira #85](https://github.com/Linductor-alkaid/mira/issues/85)，回读作者/编号/全文与
Open 状态一致；MIRA-20261009-001 改为 Open。未修改 pinned 代码、升级依赖或执行
产品验收；M7 整体保持 Proposed，实现工作项仍未完成。

2026-10-09 / M7-02：维护者授权推进，按 DEC-053 接入六个既有 Provider 工具与
共享权限、工具过程和 Provider 取消；初始模型与 model.set 使用同一装配路径。
native-debug 针对回归 3/3、native-release 全量 53/53、联合 ASAN/UBSAN 3/3，
TSAN 默认地址映射启动失败后 setarch -R 复跑通过；边界与格式检查通过。
私有 Xvfb 目标实际收到文本与逐键释放事件。范围、命令与限制见
[首增量验证](../compatibility/conversation-desktop-tools-20261009.md)。
主设计、原生会话设计与 IPC 文档已同步；依赖 pin 不变，工作分支
feat/m7-conversation-desktop-tools，未提交。完整 M7-02 仍为 In Progress，
未勾选完整点击/组合键/释放、强制后观察或跨平台验收；下一步先闭合 M7-01/03，
再将动作注册并补跑原有验收，负责人 Mirage 维护者。

本轮文档检查：12 份 Markdown / 246 个相对链接，标题与围栏检查无错误；
`git diff --check` 退出 0，暂存区为空。未触碰已有 `.impeccable` 未跟踪文件。

2026-10-09：维护者授权继续闭合 M7-01/03，均进入 In Progress。DEC-054 冻结
作用域观察、单写者、完整输入与释放失败 fence；本轮 Linux X11 为首验平台。

2026-10-09 / M7-01/02/03：Linux X11 首交付 Completed。DEC-054、公共接口、golden、
主设计、IPC、原生会话、安全边界与工具示例同步。注册八个桌面工具加 wait；fixture
跑通观察→激活→文本→语义点击→组合键→再观察。共享 lease 覆盖会话与 workflow，
旧 scope/epoch/会话、几何/前台变化、5 秒过期和预算超限明确拒绝；接管/取消/shutdown
收敛，清理失败/异常/矛盾成功回执保持 fence，并报告 shutdown 失败。

最终 native-debug 54/54、native-release 54/54（均无 skip），联合 ASAN/UBSAN 6/6
（detect_leaks=0），TSAN 的 action_contract/harness 为 55 / 422 checks、各 0 failures，
无竞态报告。公共头 53 个/0 violations、格式通过；真实 X11 的取消/超时后释放、
按钮释放、用户按住键保护、换行和中文整批拒绝已取证。精确命令、迭代失败与限制见
[本轮验证](../compatibility/scoped-complete-input-20261009.md)。工作树仍未提交，
依赖 pin/锁不变；未触碰 `.impeccable` 既有文件。

M7 整体保持 In Progress。下一项 M7-08 负责强制后观察和业务证据；M7-04/06 可先
准备截图 artifact 与真实 OCR，M7-05 仍受 MIRA-20261009-001 阻塞。Windows/Wayland
没有本轮编译/运行验收，负责人 Mirage 维护者，目标环境/授权就绪后补 M7-10；UIA scope
源码变更不等于 Windows 支持声明。真实表单/Canvas、在线模型/图片方言和用户桌面的
真实接管/关闭验证仍保留整体门禁，不以 fixture/私有 Xvfb 结果替代。

最终文档复验：15 份 Markdown / 268 个相对链接，一级标题、围栏与目标路径检查
0 错误；git diff --check 退出 0，暂存区为空。

2026-10-09 上游更新：Mira PR#86 已合并，master 698fc77 交付逐轮模型输入 supplier。
公开 API 双版本探针及上游 CI 已核对，见[更新记录](../compatibility/mira-image-supply-review-20261009.md)。
MIRA-20261009-001 改 Accepted，M7-05 的上游 API 阻塞解除，状态改 Planned；
当前 pin 尚未升级，M7-04/05 仍未实施。拟先授权消费固定 SHA 与回归，再推进标准编码
artifact/source → 每轮 ImagePart，显式处理供给丢弃/降级与迟到/身份失配，最后在线
识图复验；不能把上游默认继续纯文本的降级行为当成 Mirage 看图成功。修正工作项表中
M7-02/M7-04 前置栏误追加的状态单元格，没有改变其验收范围。

2026-10-09 授权消费：维护者明确授权依赖升级，Mira gitlink 与锁同步至 698fc77；
实际 pinned 公共头入口/签名探针通过。该增量归 M7-05 前置，回归完成后记入
[供应链审计](../supply-chain/mira-image-supply-20261009.md)，不将 M7-04/05 图片链路记为完成。

授权升级收口：M7-05 的依赖消费前置 Completed（Linux）。native-debug/release 各
54/54（无 skip），联合 ASAN/UBSAN 6/6，受控 ASLR 的 TSAN 两项 0 failures；
上游供给 fixture、实际 API 探针、公共头与格式通过。M7-04/05 仍 Planned，
下一增量按标准编码 artifact → ArtifactSource/识图能力 → 每请求 supplier 推进；
反馈继续 Accepted，不能用当前文本回归代替图片闭环验收。

2026-10-09 下一增量：维护者授权推进下一步，M7-04/05 进入 In Progress。先交付
PNG/有界内存 artifact、display/window/ROI 来源与坐标、显式识图配置、每请求供给
及退役/错误可见性，再以三方言 fixture 和已声明模型验收；不引入新的第三方依赖。

2026-10-09 / M7-04/05：按 DEC-055 完成 Linux PNG/合成图片范围，工作项 Completed。
standard RGB8 PNG → 单轮 MemoryArtifactStore/LatestMailbox source → 每请求 User
ImagePart 接通；显示器发现、权限前置、来源变换、预算/退役/身份/取消/异常与缺图
失败闭合。原生逐模型“识图”开关默认 false，保存/目录/服务启动重装保持一致。
MIRA-20261009-001 本地改 Resolved，不操作上游 issue。

最终 native-debug/release 各 55/55、无 skip，联合 ASAN/UBSAN 5/5，TSAN 图片
112 checks、无竞态；独立 PNG 解码 30,000 像素、公共头 53/0 violations、格式通过。
原生 renderer 21,605 checks/0 failures，浅深色实渲染已核对。MiniMax-M3/adaptive
两例真实截图工具在线识图 2/2，每例 captures/tool_calls/model_steps=1/1/2，
supplied/dropped/degraded=1/0/0。精确命令、迭代失败和未覆盖范围见
[图片供给验收](../compatibility/desktop-image-supply-20261009.md)。

M7 仍 In Progress；下一项优先 M7-08 强制后观察与业务证据，随后 M7-06/07 OCR/SoM。
真实用户桌面的在线识图、其他在线厂商和 Windows/Wayland 均未本轮验证，负责人
Mirage 维护者，按目标环境/授权与配置就绪条件补跑。工作树未提交，保留既有改动。

最终文档复验：20 份 Markdown / 286 个相对链接，一级标题、围栏与目标路径检查
0 错误；离线 configure 再验 lock/gitlink 通过，git diff --check 退出 0，暂存区为空。
未触碰既有 `.impeccable` 未跟踪文件；测试生成的临时文件未留在源码树。

2026-10-09 / M7-08 准入：维护者授权继续推进；按 DEC-056 冻结修改前声明的后条件、
强制新观察、只读重验与终态证据门禁。Linux X11 窗口/回显/提交为首验范围；真实
语义表单及视觉/跨平台范围仍独立取证。

2026-10-09 / M7-08：Linux X11 声明的局部后条件首交付 Completed。四个会话修改
工具固定修改前条件，强制新观察与只读 verify；未验证/不确定/规范工具失败不能
成为成功终态，不自动重发未知输入。工具卡显示目标、核验状态和证据身份/摘要/时间。
三类真实 X11 应用任务（激活、文本回显、Enter 提交落盘）3/3，没有 Fake Desktop
Provider；模型规划为确定性 fixture，不宣称在线业务模型验收。

最终 native-debug/release 各 56/56、无 skip，联合 ASAN/UBSAN 5/5；TSAN 核验
137 checks、harness 435 checks、无竞态报告；公共头 53/0 violations、格式通过。
原生 renderer 21,875 checks/0 failures，浅深色实渲染已检查，detector 0 findings。
命令、迭代失败和限制见[后条件验收](../compatibility/post-action-verification-20261009.md)。
主设计、IPC、原生会话、安全契约与 DEC-056 已同步；依赖 pin 不另变，工作树未提交。
M7 整体仍 In Progress，下一增量优先 M7-06 真实 OCR，再推进 M7-07 SoM；真实语义
表单、视觉条件和目标平台分别补验，负责人 Mirage 维护者，条件见验收记录。

最终文档复验：25 份 Markdown / 344 个相对链接，一级标题、围栏与目标路径检查
0 错误；原生设计 JSON 有效，git diff --check 退出 0，暂存区为空。测试输出保留
在构建目录/临时目录；未将其写入源码树，既有 `.impeccable` 未跟踪文件保持不动。

2026-10-09 / M7-06 首增量：按 DEC-057 接入显式 manifest、资源摘要/大小与私有副本、
Runtime 所有的 PP-OCR 组合/session、Agent OCR 工具和 IPC visual 刷新。工具权限、
超时/取消、显示器/epoch、预算、失败终态和过程脱敏闭合；工厂保持私有适配边界。
默认 Debug/Release 57/57，opt-in 56/56，真实 ncnn tiny 契约/Runtime 场景和联合
ASAN/UBSAN、TSAN 各 63 checks / 0 failure。详细命令及限制见
[装配验证](../compatibility/real-ocr-composition-20261009.md)，资源来源/许可状态见
[审计](../supply-chain/real-ocr-resources-20261009.md)。

真实 PP-OCRv5 四类公开语料精确命中 0/4；公开缩放复现确认
[MIRADOR-20261009-001](../dependency_feedback/mirador.md#mirador-20261009-001)，
Open，待上游提交，未改 pin。感知缓存的 dHash 非精确文字身份，产品暂用公开 Refresh，
不自建缓存。M7-06 保持 In Progress，真实质量/模型许可/精确缓存/宿主 DPI 尚未完成。
维护者负责上游修复与授权升级后重审模型预处理并补跑，之后再推进 M7-07。
未提交或暂存本轮改动，继承的 M7 改动与无关 .impeccable 文件均保留。

本轮最终文档检查：29 份受影响 Markdown / 391 个相对链接，标题与围栏无错误；
`git diff --check` 退出 0，暂存区为空。全部后端与模型资源保持在外部构建/临时目录，
未直接修改第三方源码。M7 总体未关闭，未把真实语料失败或未执行的平台验收记为通过。

2026-10-09 / M7-06 收尾：维护者授权对 MIRADOR-20261009-001 直接修复 mirador 并提
PR。上游 master `f0f6758` 缺陷仍在（与 pin `fb0dc3f` 同码）；根因为 `e432d51` 的
面积权重表按源行/列索引缓存，放大时共享源像素权重被最后写入者覆写。修复按目标
索引缓存连续源区间与扁平化权重，与直接公式逐位一致；commit `0527166` 随
[mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35) 提交（已回读
Open）。独立验证：新增 6 个回归在旧实现全部失败、修复后上游 ResizeArea 17/17、
Letterbox 7/7、全量 ctest 53/53，ASAN/UBSAN 通过；最小复现固定模式退出 0。
[台账](../dependency_feedback/mirador.md#mirador-20261009-001)与
[复现说明](../dependency_feedback/repro/README.md#mirador-常量缩放复现m7-06)已记
上游回执。M7-06 仍 In Progress：真实 OCR 质量、模型许可、精确缓存与宿主 DPI 须待
上游合并、授权升级 pin 并重审模型预处理后复跑；本轮未改 Mirage 侧源码与依赖 pin。

2026-10-09 / M7-06 正确性门禁：以 mirador#35 修复分支叠加态（子模块 HEAD 仍为
pin `fb0dc3f`，gitlink/lock 未动）复跑真实 OCR，公开语料**文本与位置 4/4 精确
命中**（`Submit task`/`提交任务`/`保存文件`16px/`提交 Submit` 2×，检测框全部
包含渲染真值、置信度 0.97-0.99；修复前 0/4）。复核中连带定位并修复上游两处新
缺陷：连通域 `min_x/min_y` 不随洪泛更新（[002](../dependency_feedback/mirador.md#mirador-20261009-002)，
english 框从 x=72 起即此因）与 PP-OCR 预处理偏离参考部署
（[003](../dependency_feedback/mirador.md#mirador-20261009-003)），两修复随
PR #35（现 3 提交，head `668459b`，已回读 Open）。独立验证：新增顶行内缩回归
在旧实现失败 `{8,2,2,8}`；mirador ctest 50/50、smoke 40 ok、ASAN/UBSAN
63/0；Mirage 合成 fixture 同步为动态宽度契约（16×16 kernel）。ocr-debug
55/56、native-debug 56/57 的唯一失败是仓库内与本组无关的既有在途未提交改动
（`model_layer.cpp` Messages fallback 选项数），已如实记录未处置。详见
[正确性复验](../compatibility/real-ocr-correctness-20261009.md)。M7-06 保持
In Progress：剩余为上游合并、授权升级 pin、真实桌面/宿主 DPI 复验与模型许可
闭合；本轮改动未提交，子模块处于文档化的叠加态。

2026-10-09 / M7-06 闭合：维护者授权下 PR #35 CI 全绿（14/14）后合并
（merge commit `ae8cf87`），工作分支删除；过期构建目录清理
（build/{asan,debug,tsan,mira-image-supply-debug,ocr-asan,ocr-tsan} 与
mirador 子模块 build）。授权升级 mirador pin：gitlink 与 lock 同步
（v0.3.0-121，含上游自身 M7-10~14 与 PR #35 六提交）。独立复验：
native-debug/native-release 57/57、ocr-debug 56/56、四语料文本+位置 4/4
（正式 pin）、ASAN+UBSAN 与 TSAN 各 63/0、复现探针双模式正确、格式 178 文件
0 违例。台账 001/002/003 转 Resolved（本地）。M7-06 转 Completed（Linux
首交付）：宿主真实桌面/DPI、彩色 UI 与多行文本、OCR 权重分发许可证据
（[资源审计](../supply-chain/real-ocr-resources-20261009.md)未闭合）为已登记
边界，由 M7-07/M7-10 与供应链审计承接。本轮 lock/gitlink 变更随 M7 在途工作
树保留未提交。下一增量：M7-07（SoM/crop/模板消费与无 a11y 界面的编号选择），
按计划准入先做能力调研与契约冻结。

2026-10-10 / M7-07 准入：M7-06 正式 pin 交付后，按
[DEC-058](../decisions/DEC-058-scoped-visual-marks.md) 冻结同源 SoM/crop、scope/编号、
完整可见目标和点击前新采集核对契约。首增量复用既有 worker、图片 supplier、单写者
与后观察，不重复实现模板索引；无文字图标、视觉业务条件和宿主 Canvas 门禁待验。

2026-10-10 / M7-07 首增量：同源 SoM/crop、PNG supplier、scope/编号视觉 click 与
强制后观察已接通；点击前逐像素新采集校验，过期/换代/画面/前台/几何变化拒绝。
无 a11y 的 Xvfb 自绘窗口完成实际 press/release 与标题后条件的新证据闭环；Mira
ModelLayer fixture 完成编号图→选择→点击并验证 failed 工具不能被模型“完成”覆盖。
模板 enrollment/probe 的既有融合输出通过编号路径，未增加产品图标配置或新调度器。
Debug/Release 59/59、opt-in PP-OCR 58/58；ASAN/UBSAN、TSAN 各 4/4，细节及
复现见[首增量验证](../compatibility/scoped-visual-marks-20261010.md)。M7-07 保持
In Progress：视觉业务条件、真实宿主 Canvas / 在线编号选择、无文字图标装配和高分辨率
局部采集仍待验；维护者负责对应环境补跑，在对应环境实际验证前不扩大平台声明。
本轮保留在 M7 在途工作树，未提交或推送。

2026-10-10 / M7-07 第二增量准入：按 [DEC-059](../decisions/DEC-059-visual-text-postconditions.md)
补固定 OCR 文本后条件，沿既有服务 Pipeline/worker 强制新识别，核验权限、来源身份、
窗口内唯一命中与终态门禁；实际 Xvfb Canvas 使用外部真实 PP-OCR 权重验收。

2026-10-10 / M7-07 第二增量：固定 visual_text_exists/expected_text、请求内新鲜 OCR
证据、唯一窗口内高置信度匹配和 64 次预算已接；原有取消/围栏与只读重验/终态门禁
继续有效。真实 Screen/PP-OCR/SoM/PNG/Mira 脚本选号/XTest/Canvas 重绘/新观察链路
5/5（英文/中文/16px/混排与不重绘负例），标题不变，无 a11y，不以模型文字当成功。
Debug/Release 60/60、ocr-debug 59/59；ASAN/UBSAN 与 TSAN 定向各 6/6，真实 Canvas
ASAN/UBSAN 5/5。额外完整权重 TSAN 业务复跑 0/5（取消/未验证，未扩大预算），由
维护者在保持原期限的目标环境复验。详细事实、命令和归档见
[第二增量验收](../compatibility/visual-text-canvas-20261010.md)。M7-07 保持 In Progress：
图标产品配置、用户宿主/在线模型选号、高分辨率局部采集仍待验，目标平台由 M7-10
承接。无依赖源码或 pin 新变更；本轮保留在既有 M7 工作树，未提交或推送。


2026-10-10 / M7-07 第三增量准入：按
[DEC-060](../decisions/DEC-060-geometric-visual-marks.md)接显式 geometry_marks 配置。
复用既有 Mirador 线段/闭合提案与融合/SoM，先推进无文字按钮候选；VisualIndex
仍是 patch 匹配能力，模板资源与候选消费待下一增量。真实 Canvas 验收暴露空 OCR
框和窗口容器吞并候选，应用层明确排除空文本融合与大容器编号；未改依赖算法。
实施、实际验证及剩余门禁见
[第三增量验收](../compatibility/geometric-visual-marks-20261010.md)。M7-07 保持 In Progress。


2026-10-10 / M7-07 第三增量：geometry_marks 的显式产品清单、无语义几何编号和
候选面积策略已接通；空 OCR 框不再吞并候选。真实 Xvfb 两个无文字目标分别由
mark 1/2 点击，新 OCR 固定业务条件恢复 verified；不重绘负例由宿主 failed 拦截，
3/3，通过真实权重 ASAN/UBSAN 3/3；既有文字 Canvas 回归 5/5。native-debug/
native-release 各 60/60、ocr-debug 59/59；ASAN/UBSAN、TSAN 定向各 7/7。
事实、资源/样本摘要、复现命令与期限限制归档于
[第三增量验收](../compatibility/geometric-visual-marks-20261010.md)。M7-07 仍
In Progress：模板文件/候选匹配、无轮廓图标、在线模型选号、用户宿主与高分辨率
预采集未完成。完整权重 TSAN 业务限制保留；维护者在原期限目标环境补跑。
未新增依赖反馈或 pin/源码变更；未提交/推送，保留并行 UI 与既有 M7 改动。


2026-10-10 / M7-07 第四增量准入：按
[DEC-061](../decisions/DEC-061-geometric-template-resources.md)装配审核过的本地 BGRA8
模板资产，仅匹配已有闭合几何提案。资源核验与 canonical revision 使用既有清单
路径，注册/匹配复用现有 serial worker 与 Mirador VisualIndex，不增加匹配算法或
执行器。候选预算覆盖完整库；歧义、融合身份/位置冲突和扩大框均不发布模板身份。
资源来源、容量与示例见[资源说明](../supply-chain/visual-template-resources-20261010.md)。

2026-10-10 / M7-07 第四增量：本地模板装配、全库歧义检查、几何 patch 匹配与
mark 1.1.0 的 template_id 映射已交付。真实 PP-OCR/XTest/SoM Canvas 双模板、
位置交换与不重绘负例普通/ASAN 各 4/4；原几何 3/3、文字 5/5 回归。Debug/Release
各 60/60、OCR 59/59、ASAN/UBSAN 与 TSAN 定向各 8/8；配置极端值在容量运算前
拒绝。范围、复现与公共回执见[第四增量验收](../compatibility/geometric-template-resources-20261010.md)。
M7-07 保持 In Progress；下一步优先在线模型实际编号选择与用户宿主取证，其后按
可用环境推进自由轮廓/主题尺度、高分辨率/DPI 与目标平台。维护者负责在资源与权限
可用时补证；完整权重 TSAN 仍在原期限复跑，本次定向通过不替代该业务门禁。
本轮模板工作未提交/推送，没有修改 pinned 依赖或登记新的依赖缺口。


2026-10-10 / M7-07 第五增量准入：复用 DEC-055/058/059/060 的公开模型图片供给、
同源编号、固定 OCR 后条件与闭合候选契约，扩展受控 Xvfb 在线验收入口。真实模型
仅从图中区分公共线条/交叉图案并选号，候选不附模板身份；私有 oracle 只检查输入
落点，不向模型提供答案。事件处理在既有 harness 有限任务的观察回调中有界执行，
不另建事件 worker；默认 ctest 不联网，密钥仅经子进程环境和 SecretRef 进入传输。
保持五秒图/引用、五秒观察与三十秒 owner 期限，若在线推理不能满足即保留失败
证据，不放宽产品安全预算。M7-07 保持 In Progress，成功与否由实际样本取证。


2026-10-10 / M7-07 第五增量：实际 MiniMax-M3 经既有 ModelLayer、Mira
Gateway/Provider/SocketHttpTransport/SecretRef 从无身份提示编号图选号，
cross-right / cross-left / unchanged 在线 3/3：两正例 verify 与终态 OCR
verified、宿主 ok，不重绘负例两次 unverified、宿主 failed、最终回答为空。每例
一次 mark、一次 click、一次只读 verify，实际输入仅一次按下/释放、零错点，供给
计数 4 项/0 丢弃/0 降级（含图片与后条件元数据），既有期限未放宽。在线后仅改
CANVAS 诊断标签并排空残留事件，重建后 Debug/Release 各 60/60、OCR 59/59、
ASAN/UBSAN 与 TSAN 定向 4/4，几何/文字/模板离线 Canvas 回归 3/3、5/5、4/4；
范围、复现、pin 与逐样本回执见
[在线验收](../compatibility/online-visual-canvas-20261010.md)。M7-07 保持
In Progress：用户宿主取证（当前会话报告 XDG_SESSION_TYPE=wayland，私有 Xvfb
不替代原生 Wayland 授权、权限撤销与真实宿主验证）、其他在线模型、自由轮廓、
跨主题/尺度、高分辨率预采集、DPI 与 Windows/Wayland 平台验收仍开放；完整权重
TSAN 沿既有记录在原期限复跑。未新增依赖缺口、pin 或线程设施；本轮未提交/推送。


2026-10-10 / M7-07 第六增量准入：按
[DEC-062](../decisions/DEC-062-visual-capture-scope.md)在视觉供给边界增加可选
capture_scope（窗口 id）：mark 无 crop 时供给范围即 scope 窗口几何而非整屏、越界
crop 与未聚焦 scope 窗口拒绝，click 目标必须等于 scope 窗口，OCR regions 过滤为
窗口内文本并附带 window_id。scope 为空保持 DEC-058 整屏观察契约不变；范围执行
复用既有 mark_roi 路径与 ScreenProvider 能力，不新增采集通道或线程设施。验收
harness 增加宿主入口：MIRAGE_CANVAS_HOST 连接真实显示器、MIRAGE_CANVAS_POS
窗口原点变换、MIRAGE_CANVAS_OCCLUDE 遮挡守卫负例；窗口按 id 而非标题定位，
与真实窗口相交即拒绝执行。正式宿主取证（含在线三例、权限拒绝与 Xorg 会话）
按[宿主取证 runbook](../compatibility/user-host-visual-canvas-20261010.md)补跑，
结果与回归数字待验证后记录。


2026-10-10 / M7-07 第六增量：capture_scope 供给范围化交付并验证——mark 无 crop
时供给窗口几何而非整屏、越界 crop 与未聚焦 scope 窗口拒绝，click 限 scope 窗口，
OCR 过滤窗口外文本并附带 window_id。假环境单测（负原点显示器、突变敏感性）通过；
Debug/Release 各 60/60、OCR 59/59、ASAN/UBSAN 与 TSAN 定向各 7/7；离线 Canvas
驱动 text 5/5、geometry 3/3、template 4/4；遮挡守卫负例如约拒绝。宿主入口就绪：
真实显示器/实际摆放几何/遮挡守卫/15 秒焦点宽限；XWayland 预跑四例守卫全过，
但 mutter 拒绝为合成点击移交焦点，harness fail-closed，无越界供给。正式宿主
在线取证待操作者在弹窗后物理点击 Canvas 一次，按
[宿主取证](../compatibility/user-host-visual-canvas-20261010.md) runbook 补跑。
无轮廓图标、跨主题/尺度、高分辨率/DPI、Windows/原生 Wayland 与完整权重 TSAN
门禁不变。未新增依赖缺口、pin 或线程设施；本轮未提交/推送。


2026-10-10 / M7-07 第七增量准入：按
[DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)推进局部预采集。可选
roi_capture（默认 false）让显式 mark crop 在 ScreenProvider ROI 采集前生效，
Mirador 负责 ROI→全局坐标变换；click 在相同范围逐像素重采，display/window/焦点
与固定后条件门禁保持。owner 显式 regional OCR 回调支撑高分辨率小窗口的新观察。
默认整屏行为不变，所有预算不提高。第六增量宿主物理取焦仍待操作者配合，本轮
先完成不依赖该条件的 4K 私有 Xvfb 取证与正负回归。


2026-10-10 / M7-07 第七增量：可选 roi_capture、ROI 帧范围契约、Mirador 全局
变换、mark 1.2.0 与同范围像素重验交付；owner 显式 refresh_region 提供新 OCR，
默认关闭保留整屏分析后裁剪。真实 3840×2160 Xvfb 上 (1000,700) 的 480×180
窗口局部采集为 345,600 字节，整屏 33,177,600 字节按原 8 MiB 预算拒绝，无回退
或缩放。普通/ASAN 文字 5、几何 3、模板 4，共各 12/12；三组不重绘负例保持
unverified / condition_not_matched、宿主 failed；默认模式模板 4/4、文字 5/5。
Debug/Release 各 60/60、OCR 59/59、ASAN/UBSAN 与 TSAN 定向各 8/8，0 failure。
仅增加失败回执诊断后，五配置重建 harness 与单项回归各 1/1。设计、配置审计、
安全边界与公共回执见[第七增量验收](../compatibility/regional-visual-capture-20261010.md)。

4K ROI 实际 MiniMax 在线 0/3：端点 DNS 解析超时、零输入、宿主 failed/回答为空；
第一例完成局部 mark，后续模型调用失败，未自动重试或扩大期限。完整权重 TSAN
几何首轮与单独复验各 1/3，两个正例出现 cancelled_or_expired、释放已确认；一例
只读 verify 曾通过但终态失败，没有竞态报告，仍未闭合该业务门禁。M7-07 保持
In Progress；维护者在 DNS 恢复后补跑在线三例，在原五秒观察/引用与三十秒 owner
预算目标环境补跑完整权重 TSAN。宿主物理取焦/Xorg、整屏 4K/DPI/跨屏、自由轮廓/
主题尺度与 Windows/原生 Wayland 仍各自取证。无新增依赖缺口、pin/源码变更或
并发设施；保留既有 M7/UI 工作树，本轮未提交/推送。


2026-10-10 / M7-07 第八增量准入：维护者确认能够操作宿主，按 DEC-062/063
推进实际 XWayland 用户会话取证。验收入口在任务 admission 前有界排空操作者
取焦点击，仅处理最多 32 个准备事件；物理按钮仍按下或队列未排空拒绝，不在
Agent 动作记账期过滤输入。原 scope/遮挡/前台/像素/权限与期限门禁不变，无
产品并发或依赖变更。先跑无网络的公共 Canvas，再补网络恢复后的 ROI 在线
三例。当前 Wayland/XWayland 的事实不能替代 Xorg 或原生 Wayland 验收。


2026-10-10 / M7-07 第八增量宿主取证中：操作者首次报告 Canvas 全白，定位到
验收 fixture 一次性绘制与 WM map/reparent 的时序。改为 owner 持有背景 pixmap，
映射稳定后重绘并检查自有 drawable 与公共输入逐像素相同；该回读不替代产品
ScreenProvider，也不作为模型感知输入。维护者再次确认看到了两块黑色区域，
MIRAGE_CANVAS_OPERATOR=1 中央释放事件与取焦已验，准备事件不计入 Agent 动作。
当前 GNOME Wayland/XWayland 根窗口 XGetImage 仍 io_error，backend_calls=0、
无模型输入派发；需在 Xorg 会话补跑原 root capture 路径，不能把 fixture 回读
冒充桌面采集成功。线上 DNS 探针恢复后按原条件补跑 ROI 三例，仍 0/3、选号
1/3：一例正确选号但引用过期拒绝 click，后续 DNS 失败；零实际按下/释放。
保留失败回执，不放宽引用、观察或 owner 期限。


2026-10-10 / M7-07 第八增量交付准备：保留背景绘图、自有 drawable 准备核验、
显式人工中央释放与累计 32 事件预算已接通，原五秒引用/观察和三十秒 owner 不变。
[宿主离线入口](../../tests/manual/visual_host_acceptance.py)在当前 Wayland 会话
弹窗前拒绝，Xorg 时逐例提示、首例失败即停止与归档；不会读取或传递模型凭据。
最终五配置重建 harness 与定向各 1/1，普通/ASAN 4K 文字/几何/模板各 12/12；
私有 host-mode 无人工释放在 15.049 秒拒绝，未进入 Agent。用户已确认修正后
两块黑色区域可见，但本机 root capture io_error、backend=0，Xorg 完整三例待
操作者保存工作后切换会话补跑。M7-07 保持 In Progress；网络/DNS、完整权重
TSAN、目标平台等剩余项均保留。[取证记录与操作步骤](../compatibility/user-host-visual-canvas-20261010.md)
含公共 JSON 回执；无新依赖缺口、pin/产品代码变更、提交或推送。


2026-10-10 / M7-07 第八增量 Xorg 补证：维护者已切换真实 Xorg，eDP-1
3200×2000 @ 60 Hz。离线公共 Canvas right/left/unchanged 全部 3/3，实际
root ROI、编号点击、新 OCR 与不重绘负例闭合；注入 screen.capture gate 拒绝
和第二个 managed 公共窗口取焦门禁 2/2，分析回调/模型图片/鼠标事件均零。
fixture 重绘与合成器呈现存在时序差，诊断显示 readonly 未通过而终态新 OCR
通过；现于既有 Executor 回合内最多 500 毫秒核对公共 ROI 呈现，丢弃核对帧，
产品另行采集验证，原五秒引用/观察与三十秒 owner 不变。此前失败全部保留。

实际 MiniMax 宿主闭环 1/2、正确选号 2/2（计划 3 例）：右目标唯一按下/释放、
错误输入 0，readonly/终态均 verified；左目标引用过期 stale_observation 拒绝，
副作用/按下/释放均零、宿主 failed 且回答为空，人工入口首败停止，unchanged
未运行。最终五配置 harness 定向各 1/1，普通/ASAN 真实权重 4K 文字 5/几何 3/
模板 4，各 12/12；产品与 pin 未改，没有重跑或替代完整权重 TSAN 门禁。
[Xorg 宿主记录](../compatibility/user-host-visual-canvas-20261010.md)及公共 JSON
同步。M7-07 仍 In Progress：下一增量核对 pinned 模型请求配置、测量模型
mark→click 耗时，再冻结无副作用前有界重新观察/选号契约；相关改变后补跑在线
三例，不放宽期限、不重发不确定输入。原生权限 UI、其他平台/DPI/跨屏与完整
权重 TSAN 仍分别补证；本轮未提交/推送。


2026-10-10 / M7-07 第九增量准入：沿用 DEC-058/059/062/063 与 pinned Mira
Messages ThinkingMode::Disabled/Adaptive、现有 ModelLayer reasoning_options。
验收入口改为显式可选 none/adaptive，默认仍 adaptive；仅比较公共 Canvas，
不修改产品默认模型配置、五秒引用/观察、三十秒 owner、四次采集或一次输入
约束。通过已有 DialogProcessSink 记录有界工具开始/结束与 mark→click 耗时，
诊断不采集思考正文。验证输入前显式二次 mark 退休旧图/旧 scope、重新选号和
失败不绕过终态门禁；不增加新工具、隐式续期、自动重发或并发设施。先私有
ROI 在线三例，条件通过后再提示维护者补真实 Xorg 人工三例；失败留存，不能
把低思考延迟比较当作任意模型的性能保证。


2026-10-10 / M7-07 第九增量：验收 CLI 显式 none/adaptive（默认 adaptive）、
现有过程事件的 32 条工具耗时投影已接；配置映射/不支持档位和组合重观察边界
已验。输入前再次 mark 退休旧图/旧 scope，同编号须重新选择；已拒绝的动作
不会被刷新解除失败，不增加自动续期或输入重试。五配置定向各 3/3，普通和
ASAN/UBSAN 真实权重 4K 用例各 12/12；无产品/依赖或并发路径变更。

私有 4K MiniMax-M3 none 首轮 0/3，全部 DNS 失败且零输入。DNS 与直连 TLS/HTTP
探针恢复后按原条件补跑一次，验收 1/3（不重绘负例）、选号 2/3：两个请求
mark→click 3383/3685 ms，均在五秒内，唯一按下/释放、错误输入 0；右目标后续
DNS 失败、左目标初始 DNS 失败，宿主终态都未成功。历史失败保留，不声称一般
延迟改善或网络持续恢复。[第九增量取证](../compatibility/model-visual-latency-20261010.md)
与公共 JSON 同步，真实 Xorg none 三例尚未执行。M7-07 仍 In Progress：维护者
在网络稳定后先补私有正/负三例，通过再补人工宿主；完整权重 TSAN、各目标
平台与 DPI/权限 UI 等剩余门禁不变。本轮未提交/推送。

2026-10-10 / M7-07 网络调整后补证：维护者报告已调试网络，按第九增量原 none
配置新增一次私有 4K ROI 三例，验收 2/3、选号 3/3，本批无 DNS 错误。右目标
完整新 OCR 正例和不重绘负例通过；左目标正确选号，但 mark 完成到 click 请求
7408 ms，超过原五秒期限安全拒绝，实际输入零、回答为空。私有前置未通过，
真实 Xorg none 三例未启动，维护者无需操作。既有失败历史保留，不变更产品、
测试代码、二进制或依赖，不重复执行既有回归；完整权重 TSAN 与其他目标平台
等门禁仍开放。M7-07 继续 In Progress，取证与摘要见
[追加记录](../compatibility/model-visual-latency-20261010.md#维护者调整网络后的补证)和
[独立回执](../compatibility/data/network-adjusted-online-visual-canvas-20261010.json)。

下一步负责人 Mirage 维护者：优先核对并消费 Mira 已有公开模型请求生命周期
事件，在现有有界 harness 投影补请求开始/结束时间，确定请求与本地处理耗时；
证据不足时不把 7408 ms 全部归因于推理或登记依赖缺口。根据证据实施具体配置/
请求路径改进后再补私有三例，全通过再补人工 Xorg。准入沿用 DEC-058/059/
062/063，原五秒引用/观察、三十秒 owner、四次采集、一次输入与失败不恢复
契约保持；若选择需要改变契约，须先按工程规范更新决策、设计和验收矩阵。
本轮未提交/推送。

2026-10-10 / M7 推进顺序复核：维护者要求定位延迟，并评估本地已存语义与
Mirador/Mira workflow 路线，本轮明确暂不接入新能力。公开 API 和本地调用链
核对确认：慢例 mark 290 ms，标注后到 click 7408 ms；当前非流式 harness
等待完整模型响应后派发工具，五秒年龄检查在重采集前拒绝，零输入。现有投影
不包含 infer 开始事件，TransportTrace 也无阶段耗时，不能精确归因网络或推理。

认可已验证基础阶段，M7-07 总状态保持 In Progress；在线失败回执和全部未验
门禁不改。下一主线调整为前置已满足的 M7-09，不再反复补跑同条件模型来等待
偶然低延迟。M7-11 新增为 Proposed：复用 Mirador 感知缓存/索引与 Mira 已有
workflow/App Model，分离可复用知识与当次执行引用；现有产品 workflow 承载
已存在，会话组合和本地解析仍未交付。详情见
[定位与路线](../research/2026-10-10-local-grounding-and-m7-sequencing.md)。本轮仅
分析与文档调整，无代码/依赖变更、模型请求或产品测试；未提交/推送。


2026-10-10 / M7-09 准入：维护者授权推进开发，前置 M7-03/08 已交付，按
DEC-064 冻结 Linux X11 编号滚动/完整拖拽首范围。复用现有 Executor 调用任务、
ActionCoordinator、视觉 registry 和 Mira mutation 验证；最多 16 轮步/16 段、
2048 像素拖拽、1000 ms 动作，五秒引用与四次采集保持。先实现独立 Provider
能力声明、同 scope 双引用原子解析、完整输入映射和新验证，再做私有 Xvfb
正常/取消/超时/焦点/物理按住及关闭测试。Windows/原生 Wayland、语义拖拽与
用户宿主真实产品另验；本地知识/workflow 不在本轮实现范围。

2026-10-10 / M7-09 首增量：InputProvider 独立新能力、同 scope 双引用锁边界、
X11 四向轮步/完整左键拖拽、Runtime 接线和两个 mutation 工具已交付。权限、
原图同范围重验、owner/epoch/五秒引用、单写者 lease 与固定后条件保持；最大
16 轮步、2048 像素/16 段、1000 ms，剩余引用寿命更短时缩小动作预算。取消、
takeover、超时、焦点/几何变化收敛释放；失败和释放不确认门禁不恢复/重发。
私有 XTest 299 checks、Mira 正负组合 4 个场景、visual_marks 669 与双引用
action_contract 63 checks 均通过。Debug/Release 全量各 61/61，ASAN+UBSAN /
TSAN 定向各 8/8；公共头边界 53 个、变更 C++ 格式 14 个均通过。全仓格式
检查仍在未改动的 apps/native/app.cpp 报既有违例，未混入无关格式化。初轮
fixture digest/worker ID 失败与编译修正保留在
[验收记录](../compatibility/bounded-scroll-and-drag-20261010.md)。未触碰用户桌面、
未调用在线模型/下载权重、未改依赖、未提交/推送。M7-09 保持 In Progress；
Windows/原生 Wayland、真实应用/用户宿主、任意 DPI/跨屏与语义拖拽由维护者
在相应条件具备后补验，下一增量优先真实应用效果与输入收敛取证。

2026-10-10 / M7-09 第二增量准入：按 DEC-064 补系统已有 GTK3 原生滚动控件与
可重排列表的实际效果验收。独立私有 Xvfb、自有应用/公共文本，直接读取原生
adjustment 和列表顺序，避免 release 即改标题的代理成功条件。有限 driver 由
现有 Executor 管理；GTK 仅为外部测试应用，不加入产品构建/依赖锁定。覆盖
成功、不产生效果、运行中取消及取消后的应用状态/释放；若暴露输入适配问题，
先核对现有公共契约再修复，不能放宽焦点/归属、引用期限或不确定结果重发门禁。
Windows/Wayland、用户宿主、在线模型和任意应用兼容性继续独立挂账。

2026-10-10 / M7-09 第二增量：系统 GTK3 外部测试应用、opt-in CTest 与规范
视觉工具/后条件组合验收已交付。原生双轴 adjustment 改变、GtkPaned 位置
130→190 的效果正例与滚动边界、分隔条禁用、按下后取消、列表 XDND 拒绝
负例共六场景通过。候选来自控件分配的 fixture，Screen/ROI、Mirador fusion/
SoM、scope/ref、Mira registry、权限/租约/像素重验和验证均为真实路径；不
宣称 OCR 定位或在线模型推理。GTK 原生 after-paint/sync 确保初始像素准备，
产品 source 门禁未放宽。

原生列表正例最初未通过：零间隔事件合并，列表不变；8 ms 原生 XTest 步进
后，拖放图层触发 target_obscured 并安全释放。形状命中尝试已撤销，未引入
白名单绕过；列表 XDND 仍未交付，保留失败证据与负例，不因分隔条通过关闭。
X11 步进保留为有界平台输入参数，清理零延迟，不新增 worker/timer/scheduler。
Debug/Release 全量各 62/62，ASAN+UBSAN / TSAN 定向各 9/9；四配置 GTK 各
469 checks/六场景，格式/公共头/文档同步见
[原生控件验收](../compatibility/gtk-pointer-effects-20261010.md)。未改依赖、未
触碰用户桌面、未在线调用、未提交/推送。M7-09 保持 In Progress；下一增量
先冻结原生拖放接收者的安全确认契约，再补列表正例与外来遮挡/取消取证。

2026-10-10 / M7-09 第三增量准入：维护者要求继续；按 DEC-065 冻结 X11 服务端
连接归属与辅助图层下方目标的有界检查。公开 libXRes 仅为可选私有平台能力，
不以 PID/标题放行，不取消焦点/遮挡检查。先补原生列表正/负效果及中途取消、
外来图层上/下遮挡，再验证编译关闭与服务端缺扩展的严格回退。调用继续消费
Executor 有限 future，不新增并发设施；GTK 私有 Xvfb 不触碰用户桌面。

2026-10-10 / M7-09 第三增量：DEC-065 可选系统 XRes 归属解析与原生 GTK 列表
重排已交付到本地工作树；实际顺序 Alpha→Charlie 后、新后条件/只读 verify 与
终态通过。新增禁止重排、已建立原生拖放后的取消/焦点丢失、图层上下外来遮挡，
以及同连接既有图层、第二图层、重父窗口及内部 sibling/容量负例。编译关闭与
实际禁用服务器扩展两条路径均安全拒绝并释放，公共契约和核心依赖不变。
Debug/Release 全量各 62/62，ASAN+UBSAN / TSAN 定向各 9/9；四配置 GTK 各
11 场景/102 checks、X11 各 474 checks，编译关闭 GTK 95 / X11 442 checks，
运行期缺扩展 GTK 95 checks。历史失败和复现见
[原生列表验收](../compatibility/x11-native-dnd-20261010.md)。未操作用户桌面、
未调用模型/下载权重、未提交/推送；系统开发包仅解包临时 SDK，不安装系统。
M7-09 保持 In Progress；下一增量优先用户 Xorg 原生应用验收，需先准备可复现
应用/初态，再告知维护者取焦，当前无需用户操作。其他平台/任意应用/跨屏 DPI
与既有在线模型、完整权重 TSAN 门禁不关闭。

2026-10-10 / M7-09 第四增量准入：维护者要求继续，当前宿主 Xorg :0、3200×2000。
按 DEC-064/065 准备人工取焦的 GTK 原生滚动、列表正例、禁用重排负例和中途
取消四场景。先补验收 fixture 的 GDK 逻辑坐标→物理像素映射与整数 2× 私有
回归；不放宽产品输入契约。外部 Python 管理 GTK 测试进程/人工握手，C++
有限 driver 仅在操作者点击并松开后才通过原 Executor 调用路径工作，明确传递
测试窗口 ID 和管道所有权。每场景重新准备/取焦，失败即停止、只捕获自身窗口
ROI，不执行宿主焦点/外来窗口故障注入。等待配合时先完成私有验证；未实际
执行的宿主验证保持开放并记录负责人/补跑条件。

2026-10-11 / 第四增量回归修复准入：ASAN/UBSAN 发现 harness 旧 expired fixture
20 ms 期限可能在模型请求开始前耗尽，随后无条件 front() 访问空列表。修正仅
限测试：有限任务开始时设测试期限，授权点有界等待至该期限，显式断言已进入
input.inject 门禁；空请求先报失败并完成外部 teardown。产品期限与拒绝语义
不变，保留失败日志并重跑四配置相关测试。

2026-10-11 / M7-09 第四增量：维护者逐窗口人工松键，真实 Xorg 3200×2000、
GTK scale=2 下滚动、列表重排与禁用负例在 round2 通过，首段拖动后取消另于
cancel4 补证通过；不是同批连续 4/4。四场景均核对新观察、原生状态与物理/
owner 释放；取消 begin=1、cancelled、settle=false，测试窗口已全部关闭。
间歇 stale_observation、50 像素取消前 target_obscured 的原因仍未隔离；原
拒绝/释放和失败不重发契约保持。宿主测试入口、整数 2× 坐标、状态管道交接
与失败像素范围诊断已交付；私有 50 像素故障注入继续保留。旧 expired harness
空列表访问修正；ASAN 首轮失败与 TSAN 并行观察超时均单独归档。
最终 Debug/Release 全量各 64/64、ASAN+UBSAN / TSAN 定向各 11/11（TSAN
串行），四配置 GTK 默认/2×/人工交接各 102/102/71 checks，X11 各 474 checks，
公共头 53 个零违例，格式/Python/diff 通过。完整结果与失败历史见
[宿主 GTK 验收](../compatibility/user-host-gtk-pointer-20261011.md)及其 JSON。
未调用在线模型、修改 pinned 依赖、提交或推送。M7-09 保持 In Progress；
后续先用自身 ROI 诊断隔离间歇拒绝，再补较晚取消与重复宿主稳定性证据，
Windows/原生 Wayland、任意应用/DPI/跨屏和已有在线/完整权重 TSAN 门禁不关闭。

2026-10-11 / M7-09 第五增量准入：维护者要求继续，优先隔离宿主间歇拒绝。
先补测试边界的有界 X11 命中诊断（最多四次采样、每次 128 根子窗口、连接
查询最多 256；仅类别/计数，不读窗口标题/PID/无关像素），以及 2× 人工握手
回归和 50 逻辑像素取消补证入口。通过原有限 Executor driver 消费结果，不
新增产品线程、队列或重试，不放宽 DEC-064/065。像素变化诊断补独立可复现
负例；按证据再决定是否需要修改产品。宿主仅在操作者配合时启动自身窗口，
失败即停；未实际执行的宿主诊断不得记为完成。

2026-10-11 / M7-09 第五增量：测试边界有界命中分类、已知文字颜色变化负例、
2× 松键/继承管道交接及 --diagnostics/--late-cancel 入口已交付。真实 Xorg
维护者再次人工取焦/松键，50 逻辑像素后取消 14 checks/0 failure；GTK
begin/drop/end 各 1，cancelled、列表顺序不变、按钮/owner 释放、settle=false。
根子窗口 95→98，较晚运动时命中同连接 InputOutput override-redirect 图标；
清理 drop 不代表业务成功。一次补证未复现原间歇拒绝，新增查询影响时序，
原因与重复宿主稳定性仍开放。本轮不修改产品后端或核心依赖。
最终 Debug/Release 全量各 65/65、ASAN+UBSAN / TSAN 串行定向各 12/12，
四配置 GTK 默认/2×/1×握手/2×握手各 112/112/79/79 checks、X11 各 549 checks。
Release 编译关闭 XRes 两项 2/2（GTK 105 / X11 475），恢复 ON 五项 5/5，
公共头 53 个零违例、3 个 C++ 格式、2 个 Python 编译与 diff 通过。机械
分类 oracle 首轮 549 checks/1 failure 已修正，原失败日志保留。完整记录见
[拒绝诊断与取消补证](../compatibility/x11-pointer-diagnostics-20261011.md)及 JSON。
自有宿主窗口已关闭，无需进一步人工操作；未调用模型、提交/推送。M7-09
保持 In Progress，下一步需捕获间歇失败的分类与同 ROI 变化区域，再按证据
修正；其他平台/任意应用/DPI/跨屏、OCR/在线模型及完整权重 TSAN 门禁保留。

## 2026-10-11 M7-12 准入

维护者授权将现有 M7 能力用于产品。DoR：工具与管线已交付，缺口限于产品配置与启动；
DEC-066 冻结持久化后重启生效、CLI 优先和现有服务拒绝语义。复用 Mira JSON /
ConversationLoop、Mirador PP-OCR 和既有 Runtime IPC owner；新增配置属于 Mirage 职责，
不修改 pinned 依赖、不新增并发路径。退出需同步协议/原生设计/操作说明及实际验证证据。

2026-10-11 / M7-12：Linux 产品启用预览闭合。修正产品根 Provider 未启用遗漏，新增
vision.get/set、本地清单合并持久化、原生桌面工具页、重启状态与空覆盖恢复，独立视觉
预设已构建。实际 mirage→tray→native 私有 Release 检查 23/23，原生 MiniMax-M3 轮次
ok、OCR/mark 均 complete，只读任务未产生桌面修改；OCR OFF 构建负例/恢复 11/11。
实际产品全屏 OCR 识别公开 Canvas，最小窗口明暗主题已检。普通 Debug/Release 全量
各 65/65，收尾定向各 5/5；新视觉 Release 61/61、最新服务失败场景 1/1；ASAN+UBSAN
与 TSAN 串行定向各 5/5。读写失败、旧配置合并与坏文件拒绝闭合，协议 golden v15、
格式/Python/diff 和公共头新增暴露检查通过。原生脚本接纳/终态 oracle 失败及 Debug
整屏 OCR 期限失败保留，未归因成模型延迟，未放宽五秒门禁。证据见
[产品启用验收](../compatibility/product-desktop-vision-20261011.md)。
本轮未修改 pinned 依赖、用户配置、提交/推送；私有产品进程已清理。M7-12 仅关闭
Linux 入口，M7-07/09、完整权重 TSAN、其他平台与任意应用/DPI/跨屏保留；M7-11 仍
Proposed。下一步在具体 Xorg 应用中验证实际业务任务结果与停止/接管，负责人 Mirage
维护者，补跑条件为目标应用初态、权限和人工配合准备完成；当前不需用户操作。

2026-10-11 / M7-11 准入：维护者明确要求推进。已核对 pinned WorkflowRuntime/IR、
AppModel/ScreenStateProvider、Mirador SemanticSnapshot/VisualIndex 与既有 Desktop 原子。
DEC-067 冻结本地语义首增量；复用既有 workflow Executor 驱动和版本库、临时引用、
动作 owner 与新观察核验，不改依赖、不新建任务知识版本事实源。先完成受控语义目标闭环，
其后接 OCR/模板及会话选择；跨重启资产恢复和多页导航另验。

2026-10-11 / M7-11 首增量：DEC-067 本地语义原子接入实际产品 workflow 目录；IR 保存
目标语义，执行时新引用唯一解析、权限/单写者/前台/几何/标题/取消门禁、一激活与新证据
闭合。成功回执为 canonical JSON 字符串，适配 Mira 标量 step_result Contains；DryRun
不派发且不宣称已验证。接管发生于 Provider 内时，成功返回也不能覆盖取消。
实际 Mira Strict 完成、Recoverable WaitingAgent、取消/幂等/关闭已验；125 checks，
Debug/Release 全量各 66/66，ASAN+UBSAN / TSAN 定向各 6/6，实际产品 GTK/AT-SPI2
两种构建各 20/20，均不加载模型/OCR。示例与失败历史见
[本地 workflow 验收](../compatibility/local-semantic-workflow-20261011.md)。M7-11 保持
In Progress：OCR/模板、原生会话选择/自动恢复、跨重启资产恢复和多页导航留在后续增量；
无依赖修改、用户配置变更或提交/推送，本轮无需用户操作。

2026-10-11 / M7-13、14 准入：维护者要求默认封装视觉能力并修复两次产品会话。
已定位原生高 DPI 截图超限、浏览器树 512 节点拒绝、默认无 OCR 与 canonical 失败
误封锁；DEC-068 冻结新默认值及有界恢复。供应链沿用已验证转换和公开许可证据，
不修改 pinned 依赖，不新增并发路径，私有 fixture 与离线安装验证负责证明变更。

2026-10-11 / M7-13、14 交付：两次历史按工具错误脱敏取证；截图预览/预算、
大语义树/合作取消、无副作用拒绝恢复、最新只读视觉结果及 X11 标题编码已修复。
原生 native-release 实际入口已重建，固定模型、字典、转换与许可证随安装布局交付；
源五文件及转换产物锁定，无运行时下载。Release 67/67、Debug 61/61、
ASAN+UBSAN 12/12、TSAN 9/9；实际私有安装产品两分辨率各 25/25，OCR 质量 4/4，
资源重建/损坏负例 13 checks。证据、失败历史、已验版本与本地 DEB 摘要见
[验收](../compatibility/bundled-vision-and-tool-recovery-20261011.md)。
用户配置保持，原两次在线业务任务尚未重跑；M7-07/09/11/10 的开放范围不改动。
全仓格式目标在本轮未编辑 app.cpp 中失败，本增量 18 个 C++ 文件格式通过；
相关实现仍未提交/推送，不据本地包宣称发布完成。后续优先真实业务复跑和本地
OCR/模板 grounding，模型延迟/宿主 DPI/其他平台与完整权重 TSAN 由维护者在目标
环境具备后补跑。

2026-10-11 / M7-11 第二增量准入：维护者继续授权。DEC-069 冻结本地 OCR／模板
精确目标、窗口标题加唯一页面文字身份、动作前不存在及动作后新 OCR 条件。复用既有
mark/编号 click/后观察与 Mira WorkflowRuntime；无新增调度设施，不修改依赖。
本增量先交付 workflow CLI/IPC 原子与私有实际 Canvas 证据，原生会话入口、资产恢复
及多页导航保持后续。负责人 Mirage 维护者；实际模板资源、宿主/DPI 和其他平台分别验收。

2026-10-11 / M7-11 第二增量：DEC-069 本地 OCR/模板原子进入实际产品 workflow 目录，
每次新页面/目标唯一解析，一次输入，新 OCR 后条件；只读重验吸收首次截图先于重绘，
不重发输入。实际产品定位并修复已有维护 timer 重叠占满四 worker 的观察/IPC 等待。
Release 68/68、Debug 62/62，sanitizer 定向各 10/10，视觉 fixture 177 checks；实际
Release 默认 OCR 与公开模板换位共 37/37，本地 DEB 解包同样 37/37，零模型，
每套两次 Runtime 正常退出。受控范围
已闭合，M7-11 保持 In Progress；后续推进原生会话资产选择/恢复和持久资产装配，见
[第二增量验收](../compatibility/local-visual-workflow-20261011.md)。

2026-10-11 / M7-14 第二增量准入：最新用户会话已确认默认截图/OCR 成功，激活在
参数校验阶段未派发；完整失败验证对象在 Mira 512 字节错误摘要中被截断，导致错误
封锁修正。DEC-070 冻结有界 canonical 拒绝与独立产品证据、字段级诊断及编号阶段
错误。所属增量 In Progress，先复现真实截断链，再验证恢复与不确定负例；不改用户
历史/配置或依赖，不宣称原 Bilibili 任务已完成。

2026-10-11 / M7-14 第二增量本地验收：DEC-070 canonical 错误完整 JSON ≤512 字节，
产品验证证据独立投影；字段诊断、空可选条件兼容与编号阶段/ROI 提示已接通。真实
Mira ToolExecutor/X11 验证拒绝后核验、新观察与激活纠正，收尾不再受旧失败卡片误判。
Release 68/68、Debug 62/62，ASAN+UBSAN / TSAN 定向各 9/9；4 个真实 X11 任务，
实际 Release 与本地新 DEB 解包产品各 37/37。首轮失败及缓存恢复已记入
[验收记录](../compatibility/desktop-rejection-recovery-20261011.md)。最新会话暴露原恢复
验收不足，M7-14 从首增量 Completed 重新打开为 In Progress；本地修复已验，真实用户
在线任务与提交评审未闭合，不改写首增量历史。M7-11 整体与其他开放门禁保持。

2026-10-11 / M7-14 收尾隔离补验：发现旧 Runtime 夹具未隔离默认状态目录，用户历史
元数据发生变化，最新诊断轮次保留；不删除新增较早会话，不覆盖历史。所有 Linux CTest
补齐每测试独立 XDG state/config/data 与 setup/cleanup，旧 sanitizer 环境保留。
最终 Release 70/70、Debug 64/64、sanitizer 定向各 11/11（均包括 2 个隔离 fixture）；
隔离复验期间用户历史文件 SHA-256/mtime 稳定，四个构建测试目录均回收。产品和包的
业务回归仍各 37/37。证据纳入同一 DEC-070 验收，未把元数据变更隐去。

2026-10-11 / M7-14 第三增量准入：维护者报告重启凭据与清理提示、最新任务未完成。
DEC-071 冻结幂等清理、准确读取诊断与应用启动/HTTP(S) 网址交付。活动密钥存在且
最新模型请求成功，其他引用缺失；最新会话缺少应用入口并激活无关窗口。所属增量
In Progress，使用私有钥匙环/desktop entry 与 canonical 会话验收；不修改真实凭据、
配置或历史，在线业务验证由 Mirage 维护者重启新版本后补跑。

2026-10-11 / M7-14 第三增量本地验收：DEC-071 幂等清理、持久 login 写入和会话应用
启动/HTTP(S) 交付已接通。只读 metadata 确认活动 Key 在临时 session；旧 Key 需新版
重新输入保存一次。Release 71/71、Debug 65/65、sanitizer 定向各 9/9，实际产品和
新 DEB 解包各 37/37，真实配置/历史摘要和 mtime 不变。首次金样失败、GVFS TSAN
警告及隔离模块复验、包夹具字体路径错误均记录在
[第三增量验收](../compatibility/credential-and-application-uri-20261011.md)。新二进制已进入
原 native-release 桌面启动入口；未安装系统、提交或推送，用户在线网页任务尚未复跑。
M7-14 / M7-11 整体保持 In Progress；真实业务与其他平台/Secret Service、完整 GVFS
sanitizer 验收由 Mirage 维护者在目标环境补跑。

2026-10-11 / M7-15 本地首增量：DEC-072 截图区域点击和 GUI 回退指导已进入默认会话
工具链。无 Application/Accessibility Provider 的真实 X11 启动器→Ctrl+L→URL→Enter
→新截图闭环，每配置 243 checks；背景窗口只读、窗口外目标/比例/像素/owner/权限/
取消/不确定门禁已验。五秒供图保持，5.1 秒模型等待后由区域新截图核对执行成功；
不宣称移动目标追踪或在线自主选路。最终 Debug 66/66、Native Release 72/72、
ASAN+UBSAN / TSAN 各 11/11，实际 Release/新 DEB 解包零模型 workflow 各 37/37；
用户配置/历史 SHA 与 mtime 不变，四个包内二进制与本地构建一致。首轮夹具问题、
截图空结果 SIGSEGV 与托盘超时均保留，见
[验收](../compatibility/general-gui-fallback-20261011.md)。默认启动入口和本地包已重建；
没有安装系统、提交/推送。M7-15 / M7-11 整体仍 In Progress：维护者重启新版后补
真实浏览器/模型选路和业务目标，其他平台、任意应用/DPI/跨屏及会话资产分别验收。

2026-10-11 / M7-14、15 最新宿主会话诊断：11:49“再试一次”执行 16 个模型步骤，
URI 派发仅核验 application_running，Firefox 窗口缺席，四次截图 io_error 后第五次
采集预算拒绝，最终 turn budget exhausted。当前 GNOME Wayland / 原生 Wayland
Firefox 的 1×1 根窗口 XGetImage 只读探针复现 BadMatch。BUG-20261011-001 记录
能力判定过宽、错误细节丢失和重复等待/采集；建议先闭合平台能力诊断与有限恢复，
再以 Xorg 复验实际页面，原生 Wayland 仍由 M7-10 承载。见
[诊断](../compatibility/gui-session-failure-20261011.md)。本轮没有业务代码变更或宿主
输入，修复与在线业务目标未验，M7-14 / M7-15 状态保持 In Progress。

2026-10-11 / M7-10 首增量（DEC-073）：默认原生 Wayland 使用授权的 ScreenCast/PipeWire
单帧观察，Mira 只暴露 source=desktop，只读图片没有输入坐标或 image_scope；每轮一次
系统授权，失败保留原因，取消/超时/撤销关闭 Session。GNOME Screenshot 实际已保存
3200×2000 PNG 却未返回 URI的负例保留；ScreenCast 实机 Platform 7 checks、实际
Native Release 工具/1600×1000 预览/Mira ImagePart 17 checks 通过。Debug 全量
67/67、Native Release 73/73，ASAN/UBSAN/TSAN 定向各 7/7；末轮资源清理与 41 场景
Portal 夹具复验见[验收](../compatibility/wayland-portal-observation-20261011.md)及机器证据。
实际 Release/DEB 解包 OCR/workflow 各 37/37，用户配置/历史 SHA 与 mtime 保持。
产品构建与打包强制含 PipeWire 能力，最终用户无需开发 SDK 或 OCR/ncnn 环境配置。
未安装系统、提交/推送；M7-10、M7-11 整体 In Progress，RemoteDesktop 键鼠、持续会话、
逻辑坐标/多屏/DPI、其他 Wayland 桌面与在线业务仍由 Mirage 维护者在对应增量补验。

## 2026-10-11：M7-10 第二增量 / M7-14 共享后恢复

最新用户会话 `86ad31ab18972fb6fe032a4a273d44f4` 的共享已成功，失败来自随后
application.open_uri 的旧应用观察；dispatched=false、side_effect=false。不归因为
用户操作共享失败。DEC-074 冻结最多 60 秒的应用候选本地重验，以及一个任务内最多
八张新图的共享会话；权限/取消/人工接管/失败/终态关闭。只读图仍无输入坐标。

真实 GNOME 一次系统共享，首帧和等待 6.1 秒/动作换代后的新帧均成功，1600×1000
预览、3200×2000 来源；接管/恢复之后旧会话拒绝并关闭，22 checks。此证据只覆盖
观察与生命周期，不证明原 Bilibili 任务或 Wayland 键鼠完成。最终测试与构建证据见
[第二增量验收](../compatibility/task-scoped-wayland-observation-20261011.md)。M7-10、
M7-14、M7-15 保持 In Progress；后续 RemoteDesktop 的设备权限、逻辑坐标和释放路径
继续开发，不能把只读屏幕像素作为可执行全局坐标。

## 2026-10-11：M7-10 第三增量 / M7-14 原生 OCR

最新 turn `478f15f14c380204f39827833ce916b4` 已成功派发 URI、连续截图，仍两次
OCR 失败；根因是调用 configured-display refresh，Portal 无全局 display ID。
DEC-075 冻结新共享图像经已有 PP-OCR/ncnn 的只读分析，无窗口/显示器 ID。文本模型
也注册 OCR，不供 ImagePart；识图模型取得同源图。职责属于 Mirage Adapter 接线，
无需上游修改。预算/权限/取消/异常/恢复/终态保持已有 owner 和 Executor 路径。

本轮实现已进入验证；真实 GNOME OCR 需维护者配合一次共享，自动回归/产品包验证
完成后同步[验收](../compatibility/native-shared-frame-ocr-20261011.md)。M7-10/14/15
保持 In Progress，RemoteDesktop 键鼠与原浏览器在线业务仍需下一增量。

本地验收：Debug 67/67、Native Release 73/73，sanitizer 五项定向+状态隔离各 7/7，
真实 ncnn 的 Runtime 无显示器 OCR 接线成功；实际 Release/DEB 产品各 37/37。
真实 GNOME OCR 的一次共享待维护者配合，未执行项未关闭；详细限制与机器摘要见
[第三增量验收](../compatibility/native-shared-frame-ocr-20261011.md)。

2026-10-11 / M7-10 第四增量准入：新会话 DEC-075 默认 OCR 已有真实成功，
超额图像返回 output budget exceeded，最新轮另有 portal_timeout。按 DEC-076
输出显式截断的完整区域前缀，补取图阶段诊断并纠正模型引用/网址恢复说明。
预算问题可自动闭合；实机超时仍需阶段证据和维护者共享补验，整体 In Progress。

M7-10 第四增量实机复验：维护者三次共享配合。首次 probe 31 checks/1 failure
定位 detector 候选 cap，第二轮 41/1 在并行回归负载下分析过期。等待全部回归/
构建结束后的单独探针 58/58，一次共享三次默认 ncnn OCR 各 106 区域、同源供图
与接管关闭通过。原 portal_timeout 未复现，诊断已补，不能关闭实机稳定性/输入/
在线业务退出条件。见[验收](../compatibility/native-ocr-budget-and-capture-diagnostics-20261011.md)。

M7-10 第四增量最终本地验收：Debug 67/67、Native Release 73/73，sanitizer 各
7/7，Release/DEB 产品各 37/37；用户状态 SHA 保持，包内四个二进制匹配最终
Release。预算问题修复，原共享流超时仅追加诊断；工作项保持 In Progress。

## 2026-10-11：M7-10 第五增量 / Wayland 键鼠准入

维护者明确要求补 Wayland 键鼠。DEC-077 冻结同一任务 RemoteDesktop + ScreenCast
会话、逻辑坐标、完整动作释放与图像绑定工具；复用 pinned Mira/Kairo/Mirador。
状态 In Progress；自动验证和 GNOME 授权实机验收后记录事实，未执行项不标完成。

第五增量实机补证：关闭控制开关会返回 devices=0，未派发任何输入；维护者确认后
开启系统控制、保持英文输入，result-14 为 11/11。公共原生 Wayland 动态窗口经六次
产品工具动作，完整 ASCII URL 含句点且 Enter 精确提交，点击/滚动、连续回图、
非 ASCII 无副作用拒绝和接管关闭通过。输入法切换及 spinner 是实测条件；静态画面
取图、Unicode 文本传输、其他平台及真实浏览器任务仍由维护者在目标环境补验。
Debug 全量 68/68、Native Release 全量 74/74，ASAN/UBSAN/TSAN 定向各 7/7；实际
Release 默认 ncnn/PP-OCR 的零模型 workflow 产品验收 37/37，DEB 四个可执行文件
匹配最终 Release。TSAN Portal 聚合夹具 120 秒超时两次，仅该配置测试期限改为 180 秒后
123.96 秒通过；产品/请求期限未改。用户状态六份 SHA 不变，完整限制见
[本轮验收](../compatibility/wayland-task-input-20261011.md)。未提交/推送，M7-10 不关闭。

## 2026-10-11：M7-10 第六增量 / 记住桌面授权

维护者授权推进设置中一次授权、后续任务及重启恢复。按 DEC-078 实现系统持久授权、
专用钥匙环令牌、设置状态/忘记入口与任务隔离；不建立永久录屏会话。In Progress，
负责人为 Mirage 维护者；实机补跑条件是系统共享时启用远程控制并允许记住授权。

第六增量自动验证完成：Debug 69/69、Native Release 75/75、sanitizer 定向各 9/9
（含 setup/cleanup），隔离 Portal 60 场景、产品 37/37、设置页浅深色及小窗口检查。
DEB 四份程序与最终 Release SHA 一致，用户六份状态 SHA 未变。系统一次授权后
跨任务/重启免弹窗按维护者要求暂缓，状态仍 In Progress；需解锁 GNOME、允许控制
并记住选择后由 Mirage 维护者补验。见[验收记录](../compatibility/remembered-desktop-authorization-20261011.md)。
未提交/推送，本轮无依赖 pin 改动。

## 2026-10-12：M7-10 第七增量 / 最新会话故障与 CUA 路由

In Progress，依据 DEC-081：闭合工具事实、OCR 替代引用与静态流观察链路。
最新两轮出现未暴露工具、旧 image_scope 和共享无新 buffer 超时；已有一次输入成功，
不将全部问题归为模型延迟或键鼠失败。关联 M6-38 三模式和 DEC-080 的服务权限边界。
自动与 GNOME 实机取证后分别填写结果；真实授权复验尚按维护者原安排暂缓。

第七增量自动验证完成：Debug 70/70、Native Release 76/76，ASAN/UBSAN/TSAN
定向各 8/8（六业务加隔离 fixture）；补充模型收到的工具清单事实断言通过。
OCR 替代引用链与源帧重复不能验证动作的负例经实际 Mira loop 通过，PipeWire
最新帧所有权与取消/关闭 15 检查通过。DEB 四程序 SHA 与 Release 匹配，详见
[三模式与观察链路验收](../compatibility/conversation-access-and-cua-continuity-20261012.md)。
M6-38 Linux 本地完成；M7-10 仍 In Progress，GNOME 静态画面、真实浏览器与
跨任务/重启恢复按维护者要求留待实机，不冒充授权成功或业务完成。

2026-10-12 / M7-07、M7-10 新会话故障修复准入：DEC-082 冻结默认 M3 思考通道、
单次/整任务时间与输出预算分离。历史默认正文异常暂不能在线稳定复现；7 轮提前
终止可由宿主预算精确解释。以公开 ConversationLoop 修复并补大于 7 轮的回归；
M7-10 真实授权及原任务保留未验状态，不修改依赖。

2026-10-12 / M7-07、M7-10 会话故障修正：DEC-082 修复已知 Messages 思考型号仅
7 次模型调用的宿主预算，支持 16 次、32 工具、零恢复；单请求默认 2 分钟与任务
默认 10 分钟分离。默认与显式开启两种 9 工具/10 调用任务完成，跨单请求期限的
整任务继续推进、两类超时/取消/异常/拒绝/关闭及 16 轮耗尽已验，native Agent
334 检查零失败。真实合成三档 3/3，Debug 70/70、Native 76/76，ASAN/UBSAN/TSAN
各 6/6。活动 Mirage 会话不能自发送的提示已补，忙门禁保留。原用户 Wayland 任务
与记住授权恢复仍由维护者安装/启动新版后补跑，未发起真实授权；整体 In Progress。
见[验收与开放门禁](../compatibility/reasoning-and-menu-anchor-20261012.md)。

2026-10-12 / M7-15、M7-10 工具提示选路增量：依据本机 Mirage 会话回执与
[DEC-072 选路澄清](../decisions/DEC-072-general-gui-fallback.md)，将键鼠设为完全访问
模式的常规 GUI 路线，按本轮工具能力和当前步骤选路，说明可见窗口可用有来源点击
取焦；去除对 `window.list/front` 的无条件要求，并强调 OCR/截图刷新后的新引用。
仅修改宿主与工具说明及提示回归，真实用户浏览器任务的在线自主选路仍待复验；
负责人 Mirage 维护者，补跑条件为可用的目标 Wayland 桌面、屏幕/输入授权和新版
服务进程。M7-15、M7-10 保持 In Progress。
Debug 定向构建 `desktop_image_supply_test`、`harness_desktop_tools_test`、
`gui_fallback_test` 通过；`ctest --test-dir build/debug --output-on-failure
-R '^(desktop_image_supply_test|harness_desktop_tools_test|gui_fallback_test)$' -j2`
为 5/5（含状态隔离 setup/cleanup）。新增模型请求断言覆盖无窗口 API 但有点击工具时
的常规键鼠指引、点击取焦说明和 OCR 换代引用；只读请求不宣称键鼠已开放。
`git diff --check` 通过。本次没有执行真实模型的原浏览器任务；上述定向测试不证明
在线模型必然自主选路，也不替代 M7-10 的目标 Wayland 授权与端到端验收。
随后 `cmake --build build/native-release --target package -j3` 成功生成 DEB；Native
Release 同组三项测试及隔离 setup/cleanup 为 5/5。包内 `usr/bin/mirage-service`
SHA-256 与新构建程序一致（`9e99b1b5d261276b433e592e0173b7c454e2f30c57672e90b9be4bd0f9397bcf`），
程序包含新选路指令。已将包同步到 `out/packages/mirage_0.1.0_amd64.deb`，源/目标
SHA-256 均为 `468e8c0701a71a800fcbde818498ded98e4b245021e8e990bfdddef2490d64ab`。
未安装或重启运行中的 Mirage；使用新提示须安装并正常重启新版程序。真实浏览器任务
及 GNOME 授权复验仍由 Mirage 维护者在目标桌面执行，M7-10/15 保持 In Progress。

2026-10-12 Git 交付：维护者要求整理提交，并明确授权将同期选路修改一起核验。
依赖、桌面、平台、原生 UI 与 Runtime 分组提交，代码终点 6703251；最终组合
Debug 70/70、Native 76/76、ASAN/UBSAN/TSAN 选路各 5/5，源码稳定性与包内
四程序校验通过。本地提交不关闭既有真实平台/原用户任务门禁，未推送或合并。
完整范围与证据见[Git 交付记录](../compatibility/m7-git-delivery-20261012.md)。

## 2026-10-14：M7-10 Windows 采集分层升级（DEC-086）

维护者明确触发 DEC-017 决策 6 预留的采集升级路径：Windows 屏幕采集最优先
DXGI Desktop Duplication，Windows.Graphics.Capture 回退（支持多程序同时捕获），
BitBlt 兜底；参考实现 NAW `ScreenCaptureWindows`。[DEC-086](../decisions/DEC-086-windows-tiered-screen-capture.md) 冻结逐调用无状态会话、无线程轮询
（free-threaded 帧池，无 DispatcherQueue）、占位帧防御（零 present time 重试 +
全零抽样兜底）、DPI 空间门禁与单输出边界；采集契约（Bgra8 根帧、预算、取消）
不变。

交付：私有引擎 `screen_capture_backend`（Pimpl，零平台类型出 src/），
`capture_display/window/roi` 全部委托；链接面新增 `d3d11 dxgi windowsapp`。
白盒分级测试 `win32_capture_tier_test` 对 Auto/DXGI/WGC/BitBlt 各自完成双色
fixture 像素正确性（窗口 ROI 三轮 + 全显示器绝对坐标探针）、逐层入口拒绝与
跨屏强制层拒绝；GPU 强制层按可用性探针门控（DEC-017 决策 9 跳过纪律）。

本机取证（真实 Windows 11，RTX 3070 Ti Laptop + OrayIddDriver 三屏混合 DPI，
128% 缩放）：MSVC Debug 全树配置 + platform/tests 构建零错误；
`win32_capture_tier_test` 174 checks / 0 failures（连续三次，含一次强制 DXGI
经占位帧重试取得真帧、两次 DXGI 如实失败后 Auto 落 WGC 取得正确像素）；
既有 `win32_backend_test` 回归 127 checks / 0 failures。复制会话占位帧/超时
为本机环境事实（IddCx 虚拟显示 + 远程工具占用），Auto 链按设计吸收，强制
DXGI 的像素断言在其不可用时以记录在案的条件跳过，待无 IddCx 干扰的环境补跑。

开放边界：Windows UIPI/Unicode/多屏 DPI 与产品宿主 per-monitor DPI 声明
（激活 GPU 层于缩放桌面的前置）仍属 M7-10 后续，负责人 Mirage 维护者；
Wayland 授权/撤销与会话关闭继续按既有记录推进。本轮未修改 pinned 依赖、
未调用在线模型；提交于 feat 分支，CI windows 门禁（MSVC 全树 + 测试）随
MR 复验。
