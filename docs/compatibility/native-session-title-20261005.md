# 原生会话顶部标题：Linux验收

> 日期：2026-10-05；工作项：M6-14；依据：DEC-035；负责人：Mirage维护者。

参考ZCode本地源码29628c9a的WorkspaceHeaderSections.tsx:468，任务名位于顶部，
使用普通UI字号与长文本截断。来源：[固定版本](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/WorkspaceHeaderSections.tsx#L468)。
本轮遵循既有原生标题栏，无新增工作区、菜单或改名交互。

Mirage此前只有设置页展示thread.title。现始终展示当前会话标题，空值回退“新对话”，
切换/历史载入/首次消息生成标题后随当前状态重绘。新草稿仍不进入历史列表。
16EM字面、60px行框；展开侧栏时从其右侧28px起，收起时x=116px，
右端为窗口宽减146px。收起侧栏的设置页右端为窗口宽减286px，避开返回对话按钮。
实际字宽测量后UTF-8安全省略，原始标题不变；text辅助函数的None命中策略保留拖动。
没有新增并发、服务协议、标题生成规则或依赖修改。

## 已执行验证

- native-debug/native-release构建mirage-native和native_conversation_view_test通过。
- Debug native_chat_model_test通过，原生交互测试102 checks / 0 failures。
- Release与ASAN相关两项回归均2/2通过，无ASAN诊断。
- CheckFormat、BoundaryCheck（49 headers / 0 violations）、git diff --check通过。
- 前后layout detector均无发现；凭据内容扫描无命中。
- 新草稿标题/历史数量不变、首次消息标题、会话切换、长标题原值保留、窗口按钮与收起设置返回按钮不重叠均验证。

复跑：

```sh
cmake --build --preset native-debug --target mirage-native native_conversation_view_test -j4
cd build/native-debug/apps/native
../../tests/native_conversation_view_test /tmp/mirage-session-title-20261005
```

8张合成状态截图位于[session-title-20261005](../../.impeccable/review/session-title-20261005/)；
自身Xvfb/GL framebuffer渲染，无在线模型或用户桌面操作。一次批量检查覆盖1180×800历史/新草稿/切换，
860×620长标题的明暗与展开/收起侧栏，以及收起侧栏设置页；名称和控制清晰、无重叠。

## Inline finish review

当前任务内按Impeccable降级角色复核，非独立agent评审。

disposition: ship

- persistence：pass；新构建与8张当前状态截图。
- fidelity：顶部任务名称层级为match；空草稿显示“新对话”、既有窗口控件及省略规则为原生adaptation。
- ceiling：局部标题补齐完成；保持现有字号、布局和主题。
- material_fixes：无。
- keep：真实当前标题、无边框拖动、侧栏按钮和窗口控制空间。

## 文档同步与边界

当前任务内执行documenter；更新根目录及apps/native的DESIGN.md/design.json、前端设计与M6计划。

- palette：保留明暗text颜色。
- type ramp：顶部16EM，Noto Sans SC，现有500权重请求。
- named rule：60px标题行显示当前会话/设置。
- named rule：侧栏状态决定左边距，右侧预留控制空间。
- named rule：长标题仅省略显示，保留原值和拖动命中。

其他预存偏差未纳入本次修正。Windows尚未运行：当前为Linux，负责人为维护者，
补跑条件为Windows native preset及正常/最小窗口、侧栏收起和标题栏拖动验证。
UBSAN/TSAN、在线模型、后端全量回归未执行；本次仅改变私有UI展示。
