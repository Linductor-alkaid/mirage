# 视觉工具与键鼠输入能力核对

> 状态：Completed（公开 API 与本仓库接线分析；不代表产品能力验收）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 后续：[M7 计划](../plans/m7-visual-tools-and-input.md)、[DEC-052 提案](../decisions/DEC-052-visual-tools-and-input.md)

## 结论与核对基线

下一阶段应交付当前原生会话中的真实桌面工具闭环：按需观察、定位、经权限判定执行
键鼠动作、重新观察并提供可核验结果。先完成结构化观察与安全输入，再接标准图片、
真实 OCR 和 Set-of-Mark（SoM，候选区域编号图）。复用现有 M2/M3 的 Provider、
引用解析和感知会话，扩展当前 Mira ConversationLoop 的工具接线；不重新实现 Agent
循环，也不将本轮转成 RPA 工作流开发。

本次只核对锁文件、公开头、依赖文档、相关测试及 Mirage 接线，不核验网上最新版本。

| 仓库 | 核对 commit | 意义 |
| --- | --- | --- |
| Mirage | `a722f383b004236017a7e5460ce61a9002f5dfe8` | 调研起点；已有两个未跟踪 UI 取证文件未纳入本次修改 |
| mira | `bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5` | 调研时与锁一致；后续授权升级至 698fc77，见升级审计 |
| mirador | `fb0dc3f85393b77845de8bffeab4201dae97c5d9` | 与锁一致；含 M7 实验跟踪能力，但转正门禁尚未通过 |
| kairo | `ef821dcca05bbf9038409a2ff9acc5ed729936bc` | 经 Mira 传递交付的 v0.6.0；不另 pin |

## Mira 可复用能力与边界

| 能力 | pinned 证据 | 推进判断 |
| --- | --- | --- |
| 通用工具会话循环 | [ConversationLoop](../../third_party/mira/include/mira/conversation_loop.hpp)、[API](../../third_party/mira/docs/api/model-agent-loop.md) | 已有模型、工具、回填、预算、取消和终态；沿用当前产品入口 |
| 工具注册与执行 | [BuiltinToolRegistry](../../third_party/mira/include/mira/tool_executor.hpp) | schema 校验、暴露快照身份核验、每 OperationId 至多一次派发；工具注册本身不授予权限 |
| 模型图片和原生工具往返 | [model_contracts.hpp](../../third_party/mira/include/mira/model_contracts.hpp)、[model_tool.hpp](../../third_party/mira/include/mira/model_tool.hpp) | ImagePart 与规范 ToolCallPart/ToolResultPart 已交付；能力不能等同于产品图片接通 |
| 设备闭环 | [AgentLoop](../../third_party/mira/include/mira/agent_loop.hpp)、[环境 API](../../third_party/mira/docs/api/environment-observation.md) | 有 Observe/Act/Verify 和执行不确定性契约；不能直接替换当前通用会话 |
| 桌面运行控制 | [MiraRuntime API](../../third_party/mira/docs/api/core-runtime.md) | 继续复用 Task/Session、epoch、取消和事件；不另建任务状态机内核 |
| 混合视觉 grounding | [视觉设计](../../third_party/mira/docs/design/visual_grounding_design.md) | 文档明确是 Android 目标契约、实现未开始；不能当作已交付定位 API |

ConversationLoop 只有 `set_event_store`、`set_tool_registry` 与 `run(spec, context)` 的
公开入口；AgentLoopSpec 输入是文字 goal，没有 ModelInputItem 列表或逐轮输入供给缝。
BuiltinToolHandler 返回 `Result<JsonValue>`，ToolResultPart 的结果也为 JSON。
因此返回截图 ArtifactRef 元数据不能自动使模型看到截图，base64 塞入工具 JSON 也不能
替代规范 ImagePart。此需求登记为
[MIRA-20261009-001](../dependency_feedback/mira.md#mira-20261009-001)，已提交
[Mira #85](https://github.com/Linductor-alkaid/mira/issues/85)，待上游交付。
ModelGateway 直接调用能表达图片，但在 Mirage 内搭建第二个 model/tool 循环会重复依赖能力。

AgentLoop 是另一可评估入口，但当前
[MiraEnvironmentBinding](../../integration/mira/src/mira_environment_binding.cpp) 的
`execute()` 明确拒绝，`discrete_input=false`，`interrupt()` 没有输入释放工作；现有桌面
动作走宿主 Provider。直接切换会同时引入设备决策 schema、输入编译、验证器和产品
会话语义迁移，不适合作为当前首个增量。ModelDoneVerifier 头注释亦明确用于脚本测试，
不能用它证明真实业务完成。

## Mirador 可复用能力与边界

| 能力 | pinned 证据 | Mirage 消费方式 |
| --- | --- | --- |
| 变化检测、OCR/检测结果缓存 | [PerceptionSession](../../third_party/mirador/include/mirador/perception_session.hpp) | 沿用一图像源一 session 一串行上下文，按需运行而非持续全屏推理 |
| 图像与坐标处理 | [transform](../../third_party/mirador/include/mirador/transform.hpp)、[crop](../../third_party/mirador/include/mirador/crop.hpp)、[resize](../../third_party/mirador/include/mirador/resize.hpp) | 复用裁剪/缩放和变换；保存截图来源、ROI 原点、缩放与 display 映射 |
| 证据融合与身份 | [fusion](../../third_party/mirador/include/mirador/fusion.hpp)、[stable ID](../../third_party/mirador/include/mirador/stable_id_tracker.hpp) | 按 DEC-016 维持语义观察与视觉观察边界；不在本轮改变融合策略 |
| SoM | [set_of_mark.hpp](../../third_party/mirador/include/mirador/set_of_mark.hpp)、[集成卡](../../third_party/mirador/docs/skill/mirador-integration/references/set-of-mark.md) | 渲染返回 mark→stable ID/anchor；不调用 VLM，不执行点击；编号必须与快照绑定 |
| 几何提议与模板索引 | [geometric proposal](../../third_party/mirador/include/mirador/geometric_proposal.hpp)、[visual index](../../third_party/mirador/include/mirador/visual_index.hpp) | 复用现有 M3 适配；几何矩形不等同于可交互控件 |
| 真实 OCR / detector 参考实现 | [integrations 边界](../../third_party/mirador/integrations/README.md)、[PP-OCR 公共头](../../third_party/mirador/integrations/ocr_ppocr/ppocr_backend.hpp)、[YOLO 公共头](../../third_party/mirador/integrations/detector_yolo/yolo_backend.hpp) | 有代码可复用，opt-in 构建引入 pinned ncnn；模型/字典必须显式提供，核心默认构建不包含 |
| 跨帧 ObjectTracker | [上游 M7](../../third_party/mirador/docs/plans/m7-cross-frame-object-tracking.md) | M7-10 go/no-go 与 M7-11 SPI 冻结未完成；保留 POST-04，不作为点击正确性的依赖 |

PP-OCR 分检测、识别与组合后端；只接检测会得到无文字的框。YOLO 接口存在也不代表
已有适用桌面控件的模型、标签或识别质量。先评估组合 OCR，再以真实无文字图标任务
决定是否引入 UI detector；本轮不训练模型、不编写另一套 OCR 后处理、NMS 或 SoM。
参考后端新增 runtime 的锁定、许可证、体积、离线交付和模型摘要需单独供应链取证。

SoM 区域按背景像素解释。DEC-016 视觉 bounds 是全局桌面坐标，不能直接画到窗口/ROI
截图；必须先变换到截图局部空间，再把点击 anchor 还原到对应 display。跨屏负原点、
窗口移动、DPI/缩放与屏幕拓扑变化均需拒绝旧映射。

## Mirage 已交付与当前缺口

| 位置 | 当前事实 | 下一步 |
| --- | --- | --- |
| [ModelLayer](../../integration/mira/src/model_layer.cpp) | 每轮仅建 wait 注册表；prompt 明确无桌面工具；provider 与 gateway 的 artifact source 均为 nullptr | 注入共享工具装配面、权限缝和受控 artifact source；模型能力不足时不暴露图片工具 |
| [DesktopAtomToolset](../../integration/mira/src/desktop_atom_toolset.cpp)、[DEC-024](../decisions/DEC-024-desktop-atom-toolset.md) | 13 个工作流原子，含 type_text；捕获、元素动作、指针工具明确挂账 | 抽取复用 handler/spec 装配，不将进程执行、应用终止等全部默认暴露给会话 |
| [M3](../plans/m3-mirador-integration.md) | fake / identity 后端验证了采集→分析→引用→注入→观察 | 在产品 composition root 接真实 session；真实 OCR/VLM 质量另验 |
| [InputProvider](../../desktop/environment/include/mirage/desktop/input_provider.hpp) | key press/release、type_text、move、button 与位置查询；没有 scroll、drag 或 release_all 契约 | 提供有界完整动作、输入所有权和释放；滚轮作为后续契约增量 |
| [X11 输入](../../platform/linux/src/x11_backend.cpp) | XTest；文本须能映射到当前键盘布局，逐字符计划预校验；UTF-8 合法不代表可输入中文/换行 | 先明确支持范围；中文优先 Accessibility set_text，其他路径需验证恢复/剪贴板权限 |
| [Win32 输入](../../platform/windows/src/win32_backend.cpp) | SendInput，文本 Unicode 路径和部分失败清理存在 | 补真实 Windows 普通/高权限窗口、混合 DPI、取消/释放验收 |
| [VisualReferenceRegistry](../../desktop/environment/src/visual_snapshot.cpp) | 每次 publish 复用 @v1…；resolve 只有 ref 参数 | 动作必须绑定 session/observation/scope/epoch；同名旧 @v1 不得命中新快照 @v1 |
| [ScreenProvider](../../desktop/environment/include/mirage/desktop/screen_provider.hpp)、[binding](../../integration/mira/src/mira_environment_binding.cpp) | 原始 BGRA，默认采集预算 8 MiB；binding artifact 为 image/x-bgra8888 | 真实模型需标准 PNG/JPEG 编码；4K 原始帧约 31.6 MiB，不能沿用默认预算宣称支持 |

不能把 InputOutcome.ok 当作目标应用已完成操作。输入发出后可能只进入系统队列，
部分失败、超时或取消也可能已有副作用；应显式区分副作用前拒绝、已派发与结果不确定，
重新观察后再验证。未知状态不自动重发点击/文本。

## 建议开发顺序

1. 冻结工具、引用 scope、权限和动作回执；补取消、释放与桌面独占的失败用例。
2. 当前会话接结构化 observe、语义 activate/set_text、完整 click/key/type_text。
   每次副作用后提供新观察；先用真实 AT-SPI/X11 受控窗口完成表单闭环。
3. 消费标准图片 artifact 和 Mira 的逐轮结构化输入扩展；模型实际识图为独立门禁。
4. 接真实 PP-OCR 组合后端与 SoM，验证无障碍缺失的文字/Canvas 任务，再评估 detector。
5. 补 scroll/drag、Windows 及原生 Wayland；每个平台按实际能力单独验收。

实时控制、持续追踪和 workflow 编辑器不进入首个增量。首轮以有界离散动作验证可用性；
变化检测/缓存仅优化感知成本，动作授权和引用新鲜度由 Mirage 保证。

## 本轮分析验证

2026-10-09：Linux x86_64 / GCC 13.3.0；公开 API 编译探针
[conversation-image-api.cpp](../dependency_feedback/repro/conversation-image-api.cpp) 使用：

```bash
c++ -std=c++20 -fsyntax-only -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
```

退出 0，证明 ImagePart 在模型契约可表达，而 BuiltIn handler/ToolResultPart 为 JSON。
结合 ConversationLoop 完整公共声明与上游
[工具往返测试](../../third_party/mira/tests/m3/m3_conversation_loop_test.cpp) 核对入口限制。
该探针不是图片工具运行测试；本轮没有模型调用、真实桌面动作、模型下载、依赖升级或
全量构建。产品验收由 M7 逐项补跑，负责人 Mirage 维护者。

## 后续依赖消费

2026-10-09：Mira PR#86 交付逐轮模型输入 supplier，维护者随后明确授权升级至
698fc77。本文保留初始调研基线；当前版本与回归范围见
[图片供给升级审计](../supply-chain/mira-image-supply-20261009.md)。M7-05 上游 API 阻塞
已解除，artifact/source 和真实图片消费仍由 M7-04/05 验收。
