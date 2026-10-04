# EUI-NEO dev 原生页面验收（2026-10-03）

关联：DEC-033、M6-01/02。负责人：Mirage 维护者。

## 版本与环境

- EUI：dev，4691fc0a5c1fde6f3e22f1ac454ed87c7a17f722，Apache-2.0；
  git submodule 与 dependencies.lock.json 完整提交一致，检出无源码修改。
- Linux x86_64，GCC 13.3.0，CMake 3.28，Ninja；GLFW bundled X11/OpenGL。
- 实际桌面为 Wayland，窗口通过 XWayland，content scale=2；系统 Noto Sans CJK。
  默认逻辑 1180×800（物理 2360×1600）、最小 860×620（物理 1720×1240）。
- ZCode 参考检出：29628c9acdb81b703bbd4080c207a0e7ce5e276e；只参考设计/布局。

## 已执行验证

| 验证 | 结果 |
| --- | --- |
| native-debug、native-release：mirage-native / native_chat_model_test 构建 | 通过 |
| 默认 debug 全量构建与 CTest | 50/50 通过 |
| native debug/release 模型测试 | 通过；包含会话/消息/文本容量、草稿隔离、清空指定 ID 与 ID 不复用 |
| ASAN、UBSAN 模型测试 | 通过 |
| ASAN 原生 GUI，detect_leaks=1：输入、切换、主题、弹窗、长消息、关闭 | 进程退出 0，无 sanitizer 诊断 |
| 公共头边界 mirage-boundary-check | 48 headers，0 violations |
| git diff --check / staged --check | 通过；保留进入任务前已有改动 |

实际 GUI 验证用 X11 截图及 XTest 键鼠操作：中文建议与剪贴板粘贴、Ctrl+Enter 未发送
消息、Ctrl+N/会话切换草稿保留、清空确认与 modal 中快捷键阻断、40 行消息换行滚动、
明暗/侧栏收起/最小尺寸；拖动与右下角缩放各产生 30px 变化；最大化至工作区
3068×1936 后还原；最小化 WM_STATE=3；关闭按钮后进程正常退出。

截图为实测窗口，不是设计效果图：

- [.impeccable/review/native-light-1180.png](../../.impeccable/review/native-light-1180.png)
- [.impeccable/review/native-dark-1180.png](../../.impeccable/review/native-dark-1180.png)
- 最小明暗、侧栏收起、最大化、草稿、清空弹窗与长消息同目录 native-*.png。
- [交互证据](../../.impeccable/review/interaction-results.json)。

Release 可执行文件约 2.3 MiB；此数字只针对本机二进制，不含资源与系统动态库，
不是安装包体积、启动/内存性能或跨平台保证。当前尚未生成原生安装包。

## 限制与补跑条件

- Windows 构建/窗口/中文 IME：未执行；当前无 Windows 真机。负责人：维护者；
  补跑：MSVC + native-debug/native-release preset，窗口操作及候选输入/退格测试。
- Linux 交互式 IME 候选/退格：未自动验证；已核查上游 KeyPress/Release filtered 修复，
  但中文粘贴不能替代候选操作验收。负责人：维护者；补跑：实际 IBus/Fcitx 中文输入。
- 原生 Wayland：未实现本轮支持，不以 XWayland 结果代替。
- 两项 EUI 构建问题在 [EUI 台账](../dependency_feedback/eui-ledger.md) 记录，
  临时方案只在 CMake 集成，不修改 pinned 源码；尚未向上游发送反馈。
- 本轮没有模型连接、服务启动、托盘联动或整体运行中的退出确认，不宣称这些已验收。

## 用户反馈后的视觉细化（v2）

用户指出上轮字体偏小、文字与控件未对齐；上轮视觉 ship 不作为本轮验收依据。
关联 M6-02，同一 EUI pin 与字体，没有更改上游代码或预览业务模型。

- 单行文字与图标使用 EUI ink-center 对齐，分享完整行框；取消各自的 y 偏移。
- 侧栏 240→260、标题栏 52→60、阅读列上限 760→800；会话行 42→48。
- 导航 13→16、输入 14→18、消息 15→18、提示 11→13、空态标题 29→34（逻辑 px）。
- composer 132→160，输入区 75→96；工具条文字/按钮按 36px 行框居中。
- 长会话标题按字体实际测量宽度省略，保留模型原始标题。
- 本轮执行 native-debug/native-release GUI 目标构建、Release 模型 CTest 1/1、
  git diff --check。未重复上轮全量回归/sanitizer；本轮没有新增并发或改变会话模型。
- XTest 实测中文粘贴、建议填草稿、Ctrl+Enter 未发送消息、Ctrl+N、新建会话、主题切换、
  收起/展开、清空确认打开/Escape；正常和最小尺寸截图均为实际运行窗口。
- 截图捕获前等待状态刷新并移开鼠标；初次捕获主题未刷新/收起画面不完整，已替换为
  稳定画面，不把这些中间结果送审。

本轮证据在 `.impeccable/review/native-v2-*.png`：
[正常浅色](../../.impeccable/review/native-v2-light-1180.png)、
[正常深色](../../.impeccable/review/native-v2-dark-1180.png)、
[最小浅色](../../.impeccable/review/native-v2-light-860.png)、
[最小深色](../../.impeccable/review/native-v2-dark-860.png)、
[收起侧栏](../../.impeccable/review/native-v2-collapsed-860.png)、
[实际中文草稿](../../.impeccable/review/native-v2-draft-1180.png)、
[消息](../../.impeccable/review/native-v2-messages-1180.png)、
[最小窗口消息](../../.impeccable/review/native-v2-messages-860.png)、
[清空确认](../../.impeccable/review/native-v2-dialog.png)。
Windows 与 IME 候选输入的限制、负责人和补跑条件仍按上节。

独立复核补充：浅色 muted #707070→#6d6d6d，侧栏辅助文字对比度4.345→4.540:1。
native DESIGN.md/JSON 已同步；最后一轮草稿截图曾与弹窗状态错配，已单独重拍并重新
发起完整复核。最终证据以[完整复核](../../.impeccable/review/native-v2-final-verdict.md)
及[源码/截图摘要](../../.impeccable/review/native-v2-evidence.json)为准，早期 fix/recapture
仅作过程记录。未重复执行全量回归/sanitizer，未创建 commit/MR。
