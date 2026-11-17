# 工作流图与调用：文档交接

> 日期：2026-11-16
> 范围：M15-21 / DEC-150；既有原生 EUI Operate 表面的普通扩展
> 状态：当前能力说明已同步；最终限定修正评分由根代理补充

## 依据与核对

按本轮提供的 `impeccable/reference/document.md` 普通扩展流程，读取并对照
[PRODUCT](../../../PRODUCT.md)、[根 README](../../../README.md)、
[原生 README](../../../apps/native/README.md)、
[原生界面设计](../../../docs/design/native-agent-frontend.md)、
[DEC-150](../../../docs/decisions/DEC-150-workflow-graph-composition.md)、
[方向与证据包](finish-packet.md)、[首评](finish-review.md)及
[本轮验证记录](../../../docs/compatibility/workflow-graph-calls-20261116.md)。
用户关于独立工作流按 ID 动态调用最新发布目标、发布前拒绝调用环路的纠正是本轮文档依据；
不把早期固定版本/父子所属设想保留为现行契约。

源核对覆盖 [app.cpp](../../../apps/native/app.cpp) 的工作空间/既有 Run 控制接线、
[workflow_canvas.hpp](../../../apps/native/workflow_canvas.hpp) 的轨道图/检查器/输入/段提取、
[workflow_editor_model.hpp](../../../apps/native/workflow_editor_model.hpp) 的草稿与导航生命周期、
[integration 公共投影](../../../integration/mira/include/mirage/integration/workflow_document.hpp)
及其[实现](../../../integration/mira/src/workflow_document.cpp)、
[MiraHost](../../../runtime/mira_host/src/mira_host.cpp) 的动态调用快照与发布前环路检查。
另外核对[编辑器测试](../../../tests/desktop/native_workflow_editor_test.cpp)和
[服务组合测试](../../../tests/runtime/workflow_composition_service_test.cpp)的 CAS、旧服务只读、
目标更新、活动调用不重放、直接/间接环路、深度预算、取消与标记复位用例。
本交接只读这些源码/日志，没有重新执行产品或测试。

## 与既有系统比较

比较对象为 [原生 DESIGN](../../../apps/native/DESIGN.md)、
[根 DESIGN](../../../DESIGN.md)与[sidecar](../../design.json)，均保持原文件。

| 既有约束 | 本轮扩展及核对结果 |
| --- | --- |
| 中性明暗、Noto Sans SC、Phosphor、现有 Palette 与原生控件 | 图、侧栏切换和检查器复用相同文字/控件 helper 与色彩角色；没有新增字体、图像、装饰动效或 token。 |
| 平面容器、有限圆角、文字状态、最小860×620窗口 | 节点容器对应真实 IR 步骤；轨道/回环为语义几何。明暗、常规/最小和收起侧栏截图保留原系统；本轮不重做聊天。 |
| 可操作状态与失败须可信 | 多行 JSON、显式中文/空 placeholder、只读旧服务、过期写入禁用与保留草稿有源码依据；发布前可见当前服务目录保留边界。资产 hover 对比度经单色补正和限定评分确认；本表不构成完整产品认证。 |
| 原有显式 Run/恢复与服务事实源 | 独立运行/恢复视图保持；Run、恢复尝试、Lesson 分别确认，单 Run 一次与绑定模型至服务重启继续有效。 |
| Mira/kairo 分层与有界生命周期 | integration 公开标准库投影复用 Mira IR；动态目标经 kairo DoubleBuffer 发布不可变 handler。图不创建私有执行器或并行状态机。 |

新图采用主区可用宽度与独立图/检查器滚动，是该工作空间的布局；不把它覆盖成聊天阅读列
或系统通用布局规则。没有新视觉世界或获批系统替换，因此不再生成 DESIGN 或 sidecar。
既有方向契约中 Font Awesome 与 native DESIGN 的 Phosphor 记载差异、sidecar 旧叙述元数据
属于本轮之外的历史漂移，保持原状。

## 已检查的证据与范围

核对第一批修正后的 [captures.json](captures.json) 全部26个 SHA-256；独立打开以下五图：

- [常规浅色图](native-workflow-graph-1180-light.png)：侧栏工作空间、节点选择、多行参数及条件检查器。
- [最小调用](native-workflow-call-860-light.png)：最新调用标记与直接进入按钮分离。
- [常规深色有界回环](native-workflow-loop-1180-dark.png)：轨道/回环、未选资产底色、空可选失败处理与保留提示。
- [最小深色收起侧栏](native-workflow-graph-860-dark-collapsed.png)：图/检查器及返回对话入口。
- [最小参数错误](native-workflow-graph-error-min-dark.png)：本地 JSON 错误可见，输入保留。

读取 `build/m15-workflow-graph-review-*-test.log`：Debug 功能8/8，ASAN/UBSAN 含 renderer
各9/9，TSAN 功能8/8；`build/m15-workflow-graph-review-capture.log` 记录40657 checks、
0 failures。TSAN 沿既有 setarch 环境路径，不包含 GPU renderer。上述是现有日志事实，
不等于 documenter 独立重跑，也不能证明在线模型、真实桌面、Windows或完整 M15。
截图均为 Linux Xvfb/OpenGL 产品渲染器展示的合成状态；无新 raster 资产，无网络、模型调用
或用户产品启动。

首轮限定评分 F2–F5 resolved、F1 partial 的记录保留在
[第一轮评分](finish-verdict-round1.md)。资产状态文本统一 p.text 后重捕同26图；根代理
复核最终 captures.json 的全部尺寸/SHA，最终 renderer 为40657 checks、0 failures，
日志 `build/m15-workflow-graph-hover-capture.log`；格式/边界通过。
[最终限定评分](finish-verdict.md) 为 ship：剩余 F1 resolved（明暗 hover 13.30:1/12.04:1），
F2–F5 保持上一轮 resolved。它只关闭原五项清单，不是新的整表面或完整产品认证。
最后单色补正未重复未受影响的 sanitizer 矩阵；产品 UI/服务/CLI 已构建，无用户服务启动。

## 文档改动与保留

仅编辑 PRODUCT、根 README、原生 README、原生界面设计及本交接。四份当前说明改为
侧栏左上会话/工作流主入口；M15-20 的运行/显式恢复能力及旧布局历史保留，并说明
DEC-150替代入口。同步图与节点编辑、连续段提取、独立调用/返回、expected_head CAS、
safe_workflow_editor旧服务只读和保存/发布后的有界刷新。

明确区分工作流间调用的无环检查与图内合法有界 Control 回环；默认深度2仅为资源限制。
当前服务目录不跨服务重启保留，学习元数据持久化是独立路径。完整候选审阅、参数设计器、
目录持久化、自动恢复及完整 M15仍开放。未改设计系统/token/sidecar、代码、测试、计划、
DEC、兼容性记录或素材；已有未提交改动保留。

文档自检：每个编辑 Markdown 正文一个H1，新增相对文件链接存在，显式工作流说明锚点
存在，`git diff --check`通过，三份既有系统文件无diff。后续行为/截图修正需按最终实际结果
复核本交接；本段不替代根代理最终验收收口。
