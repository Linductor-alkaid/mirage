# 工作流持久目录：文档交接

> 日期：2026-11-17
> 基线：26899ba；范围：DEC-151 / M15-22
> 状态：当前能力说明已同步；F1修正后的最终证据与限定评分由根代理补充

## 依据与源码核对

按本轮提供的 `impeccable/reference/document.md` 普通扩展路径，核对
[PRODUCT](../../../PRODUCT.md)、[根 README](../../../README.md)、
[原生 README](../../../apps/native/README.md)、
[原生界面设计](../../../docs/design/native-agent-frontend.md)、
[DEC-151](../../../docs/decisions/DEC-151-durable-workflow-catalog.md)、
[方向与证据包](finish-packet.md)与[首评](finish-review.md)。

源码核对覆盖 [目录存储接口](../../../runtime/persistence/include/mirage/runtime/persistence/workflow_catalog.hpp)、
[RuntimeService](../../../runtime/service/src/runtime_service.cpp) 的持久提交/启动恢复、
[ServiceConfig](../../../runtime/service/include/mirage/runtime/runtime_service.hpp) 的默认值与独立目录覆盖、
[IPC能力](../../../runtime/ipc/include/mirage/runtime/ipc/protocol.hpp)、
[编辑投影](../../../apps/native/workflow_editor_model.hpp)与
[画布提示](../../../apps/native/workflow_canvas.hpp)。
另外读取[服务存储测试](../../../tests/runtime/workflow_catalog_service_test.cpp)中重启、草稿/发布
分离、更新、环路/删除拒绝、写失败、导入、损坏/超限和启动零FileProvider动作断言。
文档交接没有执行模型、网络、产品或另行重跑测试。

当前实现以独立 `capability_confirmed` 标记区分未知与已确认服务，再由 `durable_catalog`
决定持久提示；`safe_workflow_editor` 只控制安全编辑门禁。初始和失效状态清除能力确认。
这与文档中的三态提示一致，不能以“编辑不支持”推断“持久状态未知”。

## 当前事实与历史保留

四份当前说明已同步为默认保存各ID的编辑head和最后成功发布定义。启动开放IPC前检查
目录/摘要/依赖/DAG，依赖优先重新DryRun/发布并恢复head；独立ID调用最新发布目标语义
保持。保存草稿不撤销发布目标，保存不等于验证发布；本地未保存修改不在恢复范围。
不恢复Run、活动任务、模型尝试或历史发布版本，不自动运行Strict任务或重放桌面动作。

默认文件为session state根的 `workflows/workflow-catalog.json`，
`workflow_state_directory` 独立覆盖目录，`persist_workflow_state=false` 显式关闭。
学习元数据仍由独立Mira SQLite存储管理。损坏/超限/依赖或重新验证失败时启动失败并保留
文件，写盘失败不确认成功、不替换原调用目标；提交后激活异常要求重启且新工作流操作
失败关闭。容量与权限沿DEC-151，不把定义恢复包装成任务恢复。

M15-21当时的服务期目录事实作为历史保留，并明确由M15-22扩展；当前未完成列表不再
把本限定目录持久化写成未来工作。完整候选审阅、自动恢复、Windows及完整M15仍未因
此交接关闭。既有M15-20显式Run/恢复和绑定模型限制不变。

## 与既有系统比较及证据

比较 [native DESIGN](../../../apps/native/DESIGN.md)、[根 DESIGN](../../../DESIGN.md)和
[sidecar](../../design.json)：本轮只改能力驱动的保留说明/保存确认，继承中性明暗Palette、
Noto Sans SC、Phosphor、轨道图、独立检查器滚动及原生控件。没有新视觉世界、字体、
色彩token、图像或装饰动效，系统文件不需要再生成。既有图/聊天及无关历史漂移未修。

核对交接时[截图清单](captures.json)中的五份SHA-256；独立打开
[常规浅色持久提示](native-workflow-durable-1180-light.png)、
[最小深色持久提示](native-workflow-durable-860-dark.png)和
[最小旧服务提示](native-workflow-graph-860-light.png)。保存限定词、未保存修改状态及旧服务
边界使用既有辅助文字层级，布局与图/检查器保持。截图是Linux隔离Xvfb/OpenGL产品
渲染器的合成状态，只证明文字/布局，不证明真实桌面或持久化行为。

读取已有 `build/m15-catalog-accept-test.log`、`build/m15-catalog-{asan,ubsan,tsan}-test.log`：
Debug10/10、ASAN/UBSAN各11/11、TSAN10/10通过；`build/m15-catalog-render.log`记录
41301checks、0failures。以上是F1最终修正前已有结果，不能替代该修正后的受影响复验。
首评为`fix`，仅要求F1未知能力提示；最新源码已增加独立确认状态，但第六张未知态截图、
最终日志和限定verdict仍由根代理补充，不提前宣称`ship`。限定评分即使resolved也只覆盖
该修正，不作为全表面、Windows、在线模型、用户桌面或完整M15认证。

## 写入边界与自检

只编辑PRODUCT、根README、原生README、原生界面设计及本报告。未改DESIGN、tokens、
sidecar、代码、测试、计划、DEC、兼容性记录或素材，既有未提交内容保留。
五份编辑Markdown正文各一个H1；新增相对链接与显式锚点有效，`git diff --check`通过；
三份既有系统文件无diff。根代理完成F1最终重捕/限定评分后，需据实际证据更新本报告的
截图数量/hash与验证结果；本交接不提前关闭该视觉门禁。


## 根代理最终收口（2026-11-17）

最终六图清单/hash已验证，新增未知状态图已实际打开；原五图与首轮像素相同。renderer
41426检查、0失败，Debug受影响6/6、ASAN/UBSAN各5/5、TSAN4/4；产品/格式/68头边界
通过。全矩阵原结果仍有效。首评F1经独立确认状态、失效重置和回归断言修正，
[限定评分](finish-verdict.md)为ship/F1 resolved，仅评分这一项。
当前说明的历史M7条目也指向M15-22定义目录扩展；专项设计/示例/两级计划/DEC151及
[验收记录](../../../docs/compatibility/workflow-catalog-persistence-20261117.md)已同步。
原文待补处保留交接时状态，以上为最终结果；未扩充Windows/在线/全M15认证。
