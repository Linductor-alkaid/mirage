# 原生控件密度对照验收

> 状态：Completed（Linux X11）；日期：2026-10-07；负责人：Codex；关联：M6-29 / DEC-047。

维护者指定[ZCode官网](https://zcode.z.ai/cn)的应用UI设计，要求收紧Mirage会话及设置中偏大的控件。
官网应用展示已通过独立headless浏览器截图核实（本机`/tmp/mirage-zcode-website-full-20261007.png`）。
具体尺寸以既有固定源码`29628c9acdb81b703bbd4080c207a0e7ce5e276e`作可复现参考：
[Button](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/components/ui/button.tsx)
默认28px、icon-md 28px、lg 32px；
[styles](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/styles.css)
UI基准14px。官网没有完整设置表单展示，设置尺寸结合既有公开源码对照，不推定官网最新版本完全相同。

## 修改

| 控件 | 修改前 | 修改后 |
| --- | --- | --- |
| 输入栏添加/发送 | 36px，17px图标 | 28px，14px图标 |
| 权限/模型/思考工具 | 32px高 | 28px高 |
| 上下文圆环 | 20px | 16px |
| 普通菜单 | 320px宽，40px行 | 280px宽，32px行 |
| 外观/模型页标题 | 24px | 20px |
| 服务标题 | 18px | 16px |
| 主题按钮 | 116×40px，16px字 | 88×28px，14px字 |
| 模型保存/取消/添加 | 32px高 | 28px高 |

正文/输入14px、模型输入32px、引用/用量详情320px及24px详情内缩进保留。
沿用现有EM修正及业务事件，未改变第三方、IPC、Executor或数据格式。

## 验证

```bash
cmake --build --preset native-release --target native_conversation_view_test -j 2
ctest --test-dir build/native-release -R '(native_conversation_view_test|native_chat_model_test)' --output-on-failure
(cd build/native-release/apps/native && ../../tests/native_conversation_view_test /tmp/mirage-density-after)
clang-format --dry-run --Werror apps/native/app.cpp tests/desktop/native_conversation_view_test.cpp
git diff --check
```

Release构建成功，相关测试2/2通过；原生渲染479 checks、0 failures。
新增验收覆盖1180×800与860×620、明暗菜单边界、思考最后选项完整显示，
主题按钮面板内包含与真实指针点击。既有长模型名、滚动、会话悬停、模型草稿、
删除确认、Key输入及保存/取消回归全部通过。不连接模型、不发起付费请求。
人工检查同一批次的会话、外观、模型、权限/附件/思考菜单截图，未发现重叠或截断。
机械设计检查无发现；格式及diff空白检查通过。

补充ASAN/UBSAN：从Release的compile_commands重编本渲染测试全部五个自研编译单元，
链接现有Release依赖；使用`-fsanitize=address,undefined -fno-sanitize=vptr
-fno-sanitize-recover=all -fno-omit-frame-pointer -O1`，479 checks、0 failures，
无ASAN/UBSAN诊断。脚本为`/tmp/mirage-density-sanitizers.py`，日志为
`/tmp/mirage-density-sanitizers-final.log`。这是自研目标的限定插桩验证，不替代全依赖消毒器构建。
首次含vptr检查的运行报告无RTTI对象类型诊断，保留在`/tmp/mirage-density-sanitizers.log`；
EUI Release在其CMake中明确使用`-fno-rtti`，因此vptr检查未计入通过声明。
完整Debug依赖的vptr验证未执行，负责人Codex；补跑条件为全Debug/RTTI依赖构建可用。
未修改第三方配置或抑制其他消毒器失败。

本机原始截图与日志保留于`/tmp/mirage-density-{before,after}`及
`/tmp/mirage-density-{build,after}.log`；仅包含合成数据，由Codex保留至本项评审结束。
对照图为本聊天visualizations目录的`mirage-control-density.png`。
关键截图SHA-256：

- 会话：`4ef37f23b4abdec4846055bba163406f7e0d641b8ea0397e655961cc3aeb640e`。
- 模型浅色：`9dd91d4701ff8ec65908d54857a6b39d55f91579ed729afa40fb87def7470858`。
- 窄窗口思考：`8878f1d4162d91a4bc8cc1b69691b25862ffb9716f1ba319431fd075ce32dbc0`。

## 验证边界

本次验证Linux X11原生渲染与指针事件，不宣称像素级复刻。
Windows/Wayland/高DPI显示未执行，负责人Codex；补跑条件为对应窗口、字体与显示环境可用。
当前用户已打开的进程需要重新打开窗口以加载新二进制，不主动丢弃其未发送草稿。
