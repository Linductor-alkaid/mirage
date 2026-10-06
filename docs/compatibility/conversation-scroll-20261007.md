# 会话到底部按钮验收

> 状态：Completed（Linux X11）
> 日期：2026-10-07
> 负责人：Mirage 维护者 / Codex
> 工作项：M6-27；缺陷：BUG-20261007-001；依据：DEC-041
> 基线：dcc6263，codex/conversation-scroll 的本记录所在提交

## 复现和修复

用户报告会话正文中的向下箭头点击无响应。已有测试调用 onKeyEvent 或滚动回调，没有
经过实际鼠标命中。加入原生 Runtime 指针移动、按下、松开后，修复前 378 checks 中
3 项失败：未恢复 follow_output、实际 offset 未到末尾、正文像素未移动；键盘回调仍可工作。

EUI ScrollView 滚动条的 z=1 使其整个子树的 subtreeMaxZIndex=1，而箭头根层默认 z=0。
Runtime 按同一子树层级排序绘制与命中；会话文字选择覆盖层先截获点击，按钮回调没有收到。
在 Mirage 中显式把箭头根层设为 z=2，高于滚动内容、低于已有弹层。保持位置、尺寸、图标、
悬停/按下及键盘语义；不修改 EUI、Mira、依赖锁、公开协议或并发设施。

## 实际验证

Linux x86_64/GCC/C++20、GLFW/OpenGL、私有 Xvfb，使用实际产品 compose 和 EUI Runtime，
不连接模型、没有付费请求、不读取用户正文。按支持的 1180×800 / 860×620、明暗主题
执行真实指针事件派发；点击后正文区域像素移动、offset 到达 scrollMaxOffset、恢复跟随且
箭头消失。像素采样限制于当前视口正文区域，排除箭头、输入栏及屏幕外像素。
原有流式阅读保位、终态保位、末尾跟随和 Enter/Space 行为回归保留。

| 检查 | 结果 |
| --- | --- |
| Release native_conversation_view / native_chat_model | 2/2，renderer 421 checks / 0 failures |
| ASAN 同上 | 2/2，detect_leaks=0，不宣称 LSAN |
| UBSAN 同上 | 2/2 |
| Mirage format、git diff --check、Markdown 文件链接 | 通过 |

```bash
cmake --build build/native-release --target native_conversation_view_test mirage-format-check -j 4
ctest --test-dir build/native-release -R '^(native_conversation_view|native_chat_model)_test$' --output-on-failure
cmake --build build/asan --target native_conversation_view_test -j 3
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan -R '^(native_conversation_view|native_chat_model)_test$' --output-on-failure
cmake --build build/ubsan --target native_conversation_view_test -j 3
ctest --test-dir build/ubsan -R '^(native_conversation_view|native_chat_model)_test$' --output-on-failure
```

[阅读位置](../../.impeccable/review/scroll-to-bottom-20261007/stream-reading-light.png)、
[末尾位置](../../.impeccable/review/scroll-to-bottom-20261007/stream-following-light.png)为同一产品
视图夹具的渲染帧；原生回归同时断言真实鼠标路径的正文像素变化，不能用回调调用替代。

## 本机产品加载

旧产品核对时没有运行，解锁后的 GNOME AppIndicators 提供托盘宿主。通过正式 desktop entry
的 GIO 入口启动新版，确认托盘 Runtime 和独立前端就绪；磁盘 1 个非空历史 ID 保留，运行注册表
为该历史及本次主会话共 2 个。额外新建空会话/删除均成功，磁盘仍只有非空历史，空主会话不保存。
此项同时完成此前 M6-26 因锁屏暂停的重新启动验证。
[启动元数据](../../.impeccable/review/scroll-to-bottom-20261007/desktop-start-results.json)不包含历史正文或凭据。

本轮鼠标交互为私有原生窗口真实 Runtime 验证；用户实际桌面的会话内容点击仍可自行复核，
没有模拟供应商在线回复或宣称 Windows/Wayland 鼠标验证。负责人维护者取得目标桌面后补跑；
整体 M6 保持 In Progress。
