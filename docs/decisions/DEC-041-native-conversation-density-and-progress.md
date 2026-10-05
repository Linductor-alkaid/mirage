# DEC-041：会话密度、右侧工具组与执行反馈

> 状态：Accepted；日期：2026-10-05；关联：M6-15/16/17/18。

维护者要求上下文与模型右对齐、输入法候选框跟随光标、视觉收紧、流式回复与等待动效/用时。
沿用ZCode现有会话结构、Noto Sans SC与中性色；正文/输入采用ZCode的14EM基准，收紧过大的侧栏、
标题与设置控件，附件/权限留左侧，上下文/模型/思考/发送构成右侧工具组。
等待状态只显示一行轻量活动标记、状态与真实单调时钟用时，不重复“正在处理”正文。
活动刷新使用既有Executor owner的timer并消费关闭路径，不自建线程。

Mira修复接入选择3716dbf（PR#76）复核：ConversationLoop承接无屏幕harness，规范工具part
替换私有回填；嵌套Executor仍为2ae4fc8。其后的master已迁移kairo，与当前强制Executor
约定不一致，本轮不静默迁移。SNI/版本修复按实际复验更新台账，不把issue关闭当完成。

## 已确认依赖边界与拟修复面

EUI dev 123f0c5仍有Linux空实现：core/platform/ime_bridge.c的set_cursor_rect不执行定位，
GLFW XIC固定PreeditNothing，公共API不暴露XIC。拟议上游最小修改：在GLFW/EUI平台层
增加XIM位置更新入口，协商PreeditPosition，向XNSpotLocation传入窗口内光标行底位置，
保持现有焦点/过滤/提交路径；窗口移动不叠加全局坐标。不在产品层读取GLFW私有布局。

Mira公开ProviderInferOptions/InferOptions没有增量预览sink，take_last_preview只在infer返回后
可用，ConversationLoop固定非流式；Chat Completions路径拒绝stream。拟议上游最小修改：
公开有界UnvalidatedModelPreview投递回调、ConversationLoop请求选项透传，并由Mira提供
Chat Completions SSE归约。下游只通过executor::comm传递UI预览；预览不持久化、不触发工具，
终态以规范响应取代，取消/失败/重试丢弃相应临时预览。不用模拟打字冒充网络流式。

2026-10-05维护者已明确授权上述依赖修复，要求分别提交上游PR。修复提交须可独立审查，
Mirage同步gitlink与锁文件；不修改嵌套Executor、不迁移kairo。
