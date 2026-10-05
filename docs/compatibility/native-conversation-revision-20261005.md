# 会话选段引用与最后一轮编辑：Linux验收

> 日期：2026-10-05
> 工作项：M6-11（Linux首步）
> 决策：[DEC-039](../decisions/DEC-039-conversation-selection-and-revision.md)
> 负责人：Mirage维护者
> 环境：Linux x86_64；C++20/CMake；pinned EUI-NEO dev 4691fc0a、Mira 13485151、内嵌Executor 2ae4fc89；无依赖升级或修改。

## 实现与验收边界

复制图标缩小为28×26px，只在正文/动作区悬停或键盘聚焦时显示。正文16/24，标题20/18/17，
块间8px、消息间14px；气泡取消边框并缩小内边距。移除整条引用按钮，拖选同一消息的实际
渲染文字后弹出“引用选段”，保留来源与独立引用实例。普通中文与跨Markdown粗体可选。
最后已终结用户输入显示编辑入口，composer出现32px提示条；取消恢复原草稿，原历史直到接纳都保留。

服务以replace_turn_id验证末轮目标；提交Mira Task与Executor后，在PhaseGate释放前保存
替换后的历史，失败恢复原记录并拒绝。实际ModelProvider请求证明旧用户输入和旧回复被排除，
更早轮次保留；不是只删除UI气泡。ACK与pending/终态事件均包含replaces_turn_id，UI处理
重复ACK、迟到旧回复与历史重同步。外部工具副作用不撤销；旧终态Task保留审计。

重启历史通过稳定产品ID继续编辑/续聊，首次harness请求用MiraHost.open_session打开当前
运行会话，有界一对一映射仅由串行服务访问。运行Task/context使用运行ID；列表显示对应
状态，删除/关闭释放映射目标；不扩展旧桌面task.submit。

## 已执行验证

| 验证 | 命令/范围 | 结果 |
| --- | --- | --- |
| Debug构建 | cmake --build --preset native-debug -j4 | 通过 |
| Debug完整回归 | ctest --preset native-debug --output-on-failure | 50/50通过 |
| Release构建 | native-release：mirage-native、mirage-service、四项相关测试 | 通过 |
| Release相关回归 | native_chat_model_test / ipc_protocol_golden_test / native_agent_integration_test / native_conversation_view_test | 4/4通过 |
| ASAN | 同四项；默认泄漏检查，未禁用 | 4/4通过，无sanitizer诊断 |
| UBSAN | chat model / IPC golden / native agent integration | 3/3通过，无sanitizer诊断 |
| TSAN | setarch x86_64 -R ctest --preset tsan，同UBSAN三项 | 3/3通过，无race诊断；禁用ASLR兼容当前TSAN映射 |
| 原生渲染/交互 | native_conversation_view_test，真实EUI Runtime/GLFW/OpenGL布局；自身Xvfb窗口 | 31 checks，0 failures；悬停离开隐藏、无整条引用、中文/粗体/滚动后拖选、弹层不盖首行、真实选段引用、编辑/取消、背景取消 |
| 公共头边界 | cmake/BoundaryCheck.cmake | 49 headers / 0 violations |
| 格式 | cmake/CheckFormat.cmake + clang-format | 通过 |
| Git差异 | git diff --check | 通过 |

补测长消息发现滚动transform与布局frame的坐标差异；Adapter以公开press bounds推导
偏移与缩放，按实际滚轮事件推进并等待框架惯性结束后，逐行不同的正文断言引用正确行。
修正后复跑受影响UI目标：Debug 31 checks，Release 1/1、ASAN 1/1（含真实渲染）通过；服务与协议代码保持已验证结果。

纯ChatModel测试覆盖UTF-8边界、反向选择、重复/分段引用及实例删除、预算、编辑取消原草稿、
替换末轮与用量重置、旧事件不复活、重复ACK不抹掉新用量、旧历史不覆盖新快照。
Wire golden保留旧缺省canonical并新增替换请求/ACK/事件、空/非字符串/超长目标拒绝。
服务夹具使用真正Mira ModelProvider/ModelLayer请求，读取TextPart wire文本，断言新输入与
早期上下文存在、旧输入与旧末轮专属回复不存在；覆盖过期目标、活动拒绝、写盘故障保留、
执行中取消、缺用量、超时、Executor拒绝/异常、shutdown、重启编辑/续聊/列表状态/删除。
故障注入的持久化错误与model-driver失败日志是预期结果；夹具仅有明确命名的虚构API Key。

## 可复跑截图与设计复核

```sh
cd build/native-debug/apps/native
../../tests/native_conversation_view_test /tmp/mirage-conversation-qa-20261005
```

截图初始会话显式标注“合成测试会话 · 不连接模型”，通过公开框架事件在自己创建的窗口中测试交互，
读取自身GL framebuffer生成PPM，再无损编码PNG；不操作用户日常桌面，不依赖操作系统输入注入。
同一测试覆盖1180×800和860×620逻辑窗口、明暗、常态/hover、用户选段、Markdown选段、
编辑提示。不是运行中ZCode截图，也不是本轮在线供应商调用。

- [常态1180](../../.impeccable/review/conversation-20261005/user-1180.png)
- [悬停动作](../../.impeccable/review/conversation-20261005/hover-actions.png)
- [首行选段浮层](../../.impeccable/review/conversation-20261005/selected-excerpt.png)
- [Markdown选段](../../.impeccable/review/conversation-20261005/markdown-selection.png)
- [编辑最后输入](../../.impeccable/review/conversation-20261005/editing-last-input.png)
- [最小浅色](../../.impeccable/review/conversation-20261005/minimum-light.png) / [最小深色](../../.impeccable/review/conversation-20261005/minimum-dark.png)
- [正常深色](../../.impeccable/review/conversation-20261005/normal-dark.png)

本轮采用impeccable degraded finish-reviewer/documenter角色内联替代，未使用独立子agent，
不称为独立评审。初始fix指出首行浮层遮挡和旧设计记录；修正后只评分这两项，均resolved，
disposition=ship限于该修正范围。[初始复核](../../.impeccable/review/conversation-20261005/finish-review.md)、
[评分](../../.impeccable/review/conversation-20261005/finish-verdict.md)、
[文档提取](../../.impeccable/review/conversation-20261005/documentation.md)。四份DESIGN/JSON保留
既有中性色系、品牌及composer规则，只更新本轮实际值和选择/编辑语义；无新品牌raster。

## 参考与未执行项目

ZCode源码29628c9a：MarkdownSelectionTooltip、useTextSelection及ConversationRowView，
官网[页面内容](https://zcode.z.ai/cn)可读取；浏览器导航多次超时，运行中ZCode像素比较未完成。
仅以公开实现与页面内容为交互参考，不宣称截图复刻。EUI选择缺口记录
[EUI-20261005-005](../dependency_feedback/eui-ledger.md)，Adapter不修改上游。

- 跨消息选择、拖选自动滚动、链接打开：当前未实现，产品限制，不标记验收完成。
- Windows窗口/IME与选择：未执行；原因当前Linux；负责人维护者；补跑条件Windows native preset及真实鼠标/IME环境。
- 高DPI/跨屏、真实中文IME、本轮系统桌面键鼠交互和供应商在线重发：未重新验证；负责人维护者；补跑条件对应桌面与模型环境。
- live ZCode像素对照：未执行；原因浏览器超时且此前Wayland截图权限拒绝；负责人维护者；补跑条件可访问ZCode窗口的浏览器/截图表面。
- 整体入口/托盘活动退出确认仍在M6-04，本项不改变其状态。
