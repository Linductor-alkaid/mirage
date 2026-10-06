# 原生侧栏与外观设置验收（2026-10-04）

关联：M6-02、DEC-033。负责人：Mirage 维护者。
范围：Linux 原生预览窗口内的侧栏调宽与设置/外观页。

## 实现与边界

维护者确认 Dock 图标已生效，继续要求可调侧栏、用设置入口替换主题快捷切换，
并将主题置于设置页外观中。实现沿用既有 ZCode 中性色、导航与设置行结构。
参考本地 ZCode 29628c9 的 SettingsPage / AppearanceSectionContent，未复制业务代码。

侧栏默认260逻辑px，拖动范围224–400px；同时保留至少520px主区，因此最小
860px窗口上限为340px。缩窗只限制呈现宽度，不覆盖用户期望值；扩窗恢复原值。
8px命中分隔线有悬停反馈和横向resize光标，点击后左右键每次调8px。
拖动用 EUI Pointer/Drag 公共回调的实际像素增量与 bounds DPI 比率换算，
不采样独立鼠标坐标作为业务事实，不新增线程、定时器或后台工作。
原生横向光标由应用私有 GLFW adapter 持有，在 onShutdown 释放。

设置页保留窗口控制与收起能力，左侧返回对话/外观分类，右侧主题设置行。
内容宽度小于600px时选项放到标签下方；浅色/深色即时应用。
齿轮入口在侧栏底部，收起时位于标题栏；Ctrl+, 打开设置，Escape 先关闭
当前模态层，再退出设置。隐藏聊天期间 Ctrl+N/Ctrl+Enter 不改变后台会话。
本次进程内记忆宽度与主题，不持久化；设置页明确提示。

## 环境与验证

Linux x86_64，GCC13.3、CMake3.28、EUI dev
4691fc0a5c1fde6f3e22f1ac454ed87c7a17f722，XWayland/OpenGL，content scale=2。

| 验证 | 实际结果 |
| --- | --- |
| native-debug/native-release mirage-native 构建 | 通过 |
| native-debug/native-release/ASAN/UBSAN 模型 CTest | 各1/1通过 |
| ASAN GUI，detect_leaks=1 | 拖动、主题、设置返回、收起/展开、资源 teardown；退出0，无诊断 |
| 鼠标调宽与方向键 | 260→320，右键+8/左键−8；拖动下限224/上限400通过 |
| 缩窗与恢复 | 最小窗口限制340，主区520；扩大后恢复请求400 |
| 设置导航与状态保持 | 浅/深切换、正常/最小/收起入口、中文草稿保持通过 |
| 快捷键和模态层 | Ctrl+N/Enter不改隐藏chat；Ctrl+,打开；Escape先modal后设置 |
| Release退出/日志 | 正常退出0，无运行诊断 |
| git diff --check / staged --check | 通过 |

调宽初次验证暴露独立鼠标采样与事件尺度不一致，改为 EUI 回调增量后重验通过；
测量脚本曾把悬停的8px区域当成边界，已改为分隔条上方的稳定背景行测量。
最后一次扩窗后的拖动取证重试单独通过；不把中间失败记录写成成功运行。

截图为实际窗口捕获：

- [正常浅色外观页](../../.impeccable/review/native-settings-appearance-light.png)
- [正常深色外观页](../../.impeccable/review/native-settings-appearance-dark.png)
- [最小浅色外观页](../../.impeccable/review/native-settings-appearance-min-light.png)
- [最小深色外观页](../../.impeccable/review/native-settings-appearance-min-dark.png)
- [调宽320px](../../.impeccable/review/native-settings-sidebar-320.png)
- [最小窗口340px侧栏](../../.impeccable/review/native-settings-chat-min-wide.png)
- [收起侧栏设置](../../.impeccable/review/native-settings-collapsed-dark.png)
- [返回后中文草稿](../../.impeccable/review/native-settings-return-draft-dark.png)
- [最终外观页](../../.impeccable/review/native-settings-final-light.png)
- [源码/截图摘要与测试结果](../../.impeccable/review/native-settings-evidence.json)
- [交互记录](../../.impeccable/review/native-settings-interaction-results.json)

独立设计提取已更新 apps/native/DESIGN.md / sidecar；独立视觉复核见
[finish verdict](../../.impeccable/review/native-settings-finish-verdict.md)。

## 未执行与限制

Windows实际窗口/输入/光标未验证；当前为Linux环境。负责人：维护者；补跑：
Windows native preset 构建，拖动DPI、主题与导航/退出实测。原生 Wayland 不在本轮支持范围。
UBSAN只跑模型用例，未运行UBSAN GUI；ASAN GUI不能替代这一项。未新增并发路径，
未跑TSAN或全量Runtime回归。未接入真实Agent、未替换安装包、未改托盘生命周期，
不承诺跨启动设置保存或系统主题跟随。本轮未创建commit/MR。

独立五节视觉复核 disposition=ship：12张指定截图与补充模态/收起状态有效，
源码、设计、哈希和行为记录一致，无material fixes；结论只覆盖本轮Linux
侧栏与设置UI。维护者当前可见的Release预览已重新启动并打开外观页。
