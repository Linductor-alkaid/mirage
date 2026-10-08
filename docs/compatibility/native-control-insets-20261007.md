# 原生控件文字内边距验收

> 状态：Completed（Linux X11）；日期：2026-10-07；负责人：Codex；关联：M6-30 / DEC-048。

## 问题与处理

84px思考按钮仍按EUI默认左右16px计算标签宽度；四字13px标签的文字框仅约38px，
文字绘制超出框后挤到右侧。纯文本按钮也缺少同等安全文字区域。

`apps/native/control_metrics.hpp`集中定义8px文字内边距和4px图文间隔。
`apps/native/control_button.hpp`复用EUI ButtonBuilder及其公开DSL元素，统一图文组留白、
字体测量省略及尾部箭头布局。实际宽度从已生成控件取得；不复制背景、事件或禁用状态实现。
全部产品按钮已迁移，普通/Key输入共享同一token。模型和思考工具组使用共享宽度计算，
思考按最长选项留足空间，位置由相邻尺寸及间隔推导。正文、面板和导航分组仍保持层级缩进。
未修改任何pinned依赖、公开API、IPC、存储或并发路径。

## 原生验证

```bash
cmake --build --preset native-release --target native_conversation_view_test -j 2
ctest --test-dir build/native-release -R '(native_conversation_view_test|native_chat_model_test)' --output-on-failure
(cd build/native-release/apps/native && ../../tests/native_conversation_view_test /tmp/mirage-insets-after)
clang-format --dry-run --Werror apps/native/control_button.hpp apps/native/control_metrics.hpp apps/native/app.cpp apps/native/secret_input.hpp tests/desktop/native_conversation_view_test.cpp
git diff --check
```

Release构建通过，相关测试2/2通过；原生渲染13,151 checks、0 failures。
每次compose后遍历实际DSL树检查所有按钮：文字的实测宽度不超出文字框，
图文组左右留白均8px，图标与文字间隔4px；同时检查输入文字视口左侧8px及右侧至少8px
（多行滚动条占用另计）。覆盖无图标、前图标、尾部箭头、禁用、弹窗、设置与会话状态。
全部九种思考值无省略，长模型名安全省略且不碰邻居。既有保存、取消、Key与会话回归通过。

同批截图检查1180×800 / 860×620的明暗会话、菜单、外观与模型页；思考文字/箭头右侧
留白恢复，未发现重叠或截断。机械检查无发现；格式与diff检查通过。
本机截图/日志保留于`/tmp/mirage-insets-after`和`/tmp/mirage-insets-{build,final-build,after,final-tests}.log`，
仅合成数据，Codex保留至本项评审结束。放大对比图为本聊天visualizations中的
`control-insets-comparison.png`。

关键截图SHA-256：

- 常规浅色：`d41ab5f5769949fa6ca07ae6ba8120880fead7412932a5bcef8ac360ab2788b2`。
- 窄窗长模型深色：`e1ed7e5d210cc0a408fa27c23903128ec131d394614bff4db9b1749d77ff0529`。
- 窄窗模型深色：`b0faad777554d556821ecc3affbb81d7ee54f776c975a6b251cbc911fe5fde23`。

补充ASAN/UBSAN：该渲染目标全部五个自研编译单元采用插桩，链接既有Release依赖；
共享Adapter最终版本复编后13,151 checks、0 failures，未出现消毒器诊断。
使用`-fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=all
-fno-omit-frame-pointer -O1`；脚本`/tmp/mirage-insets-sanitizers.py`，
最终日志`/tmp/mirage-insets-sanitizers-final.log`。第三方Release库未插桩，
该结果是自研目标限定验证，不替代全依赖消毒器验证。

## 验证边界

验证Linux X11实际原生渲染和指针事件；未重新打开用户进程，以保留未发送草稿。
Windows/Wayland/高DPI显示未执行，负责人Codex，补跑条件为对应字体及显示环境可用。
完整RTTI依赖的UBSAN vptr验证未执行；EUI Release关闭RTTI，补跑条件为全Debug依赖构建。
不宣称未测平台通过；不关闭整体M6。
