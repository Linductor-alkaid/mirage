# 原生会话目标最终复验

> 状态：Completed（Linux X11范围）；负责人：Linductor-alkaid。
> 日期：2026-10-06；工作项：M6-20；依据：DEC-041与维护者既有依赖修复/PR授权。

## 目标与实际交付

沿用ZCode会话结构：附件/权限在左侧，上下文/模型/思考/发送靠右；正文和输入14EM、
22px正文行距，模型宽度按文字测量并限96–180px。已有布局没有另起一套视觉风格。
参考源码为ZCode main 29628c9的ChatPromptEditor和ConversationTimeline，官方站点
2026-10-06可访问；未把官网宣传页截图当实际工作台的像素对照。

真实Release原生窗口中，IBus/libpinyin候选相对窗口位于(400,378)，换行后移到(400,402)。
确认“你好”与“世界”、Shift+Enter换行均保留本地草稿，不提前创建服务会话。
实际模型请求经Service/Mira产生流式预览；等待显示低幅三点和真实单调计时，规范终态
替换预览并冻结用时，历史回复不编造时长。上下文弹层显示供应商输入Token与配置预算。

本轮修正长回复体验：在底部跟随新内容；上翻时停止跟随，完成时保持阅读位置；滚回底部
或点击输入栏上方的箭头恢复跟随，箭头可键盘聚焦后Enter/Space操作。新一轮恢复跟随。
补修窗口适配层的空闲退出：设置GLFW关闭标记后投递空事件，避免当前帧随后进入等待。
这是前端进程退出，不改变外部Service/托盘的M6-04生命周期契约。

## EUI滚动缺口与Git

EUI ed1deb6的scrollView.offset在重新compose后被Runtime忽略；原生DSL请求已到末尾，
实际文本与滚动条仍停在原位置。登记EUI-20261006-001；不通过产品层重建私有状态绕过。
独立修复ff1e7572132a7ff8fc70c8e52f7778fce4dd976f记录上次配置值，接纳新的程序位置，
保留onChange回写与未控配置的惯性，并使实际viewport失效重绘。已普通push更新
[上游PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)，未合并；gitlink与锁同步。
不改变Mira、嵌套Executor、许可或产品GLFW/OpenGL选择。

## 实际验证

| 验证 | 结果 |
| --- | --- |
| EUI ui_state | 1/1通过；新请求/回写/未控惯性/内容增长与缩短 |
| Debug / Release / ASAN / UBSAN | 各模型/真实会话渲染2/2通过；ASAN关闭系统图形库leak检测，不声明LSAN |
| Mirage Release | 模型与真实会话渲染2/2通过；渲染138 checks零失败 |
| 实际窗口完整驱动 | 私有Xvfb/DBus/IBus，真实Qwen/Qwen3.5-4B；当前ff1e757：222次提前预览、最终1145bytes、历史一致、正常退出 |
| 依赖锁 / 格式 / 边界 | lock 1/1；CheckFormat；49公共头零违规 |
| 上游EUI当前head CI | [run37348553548](https://github.com/sudoevolve/EUI-NEO/actions/runs/37348553548)；ff1e757上四项后端/测试/SDK消费全部成功 |
| 视觉复核 | 实际窗口正常/最小、明暗、候选、等待/流式/用时/上下文；长回复另用原生渲染夹具复验 |

原生回归新增实际阅读区域像素比较，排除箭头/输入栏，不仅检查DSL请求参数。
还验证阅读时预览和终态保位、恢复底部、键盘入口及程序关闭唤醒。候选与真实模型驱动为
显式选择的手动验证，不接入自动CI、不会读取默认凭据或操作用户桌面。

```sh
cmake --build --preset native-release --target mirage-native mirage-service native_chat_model_test native_conversation_view_test -j2
ctest --test-dir build/native-release -R '^(native_chat_model_test|native_conversation_view_test)$' --output-on-failure
python3 tests/manual/native_conversation_acceptance.py --build build/native-release \
  --provider "$HOME/mira/docs/model_provider/siliconflow" \
  --output /tmp/mirage-conversation-live --xvfb Xvfb
```

手动驱动需要python-xlib、Pillow、DBus、IBus/libpinyin及系统PyGObject；本机Xvfb在用户
sysroot，通过--xvfb显式选择。模型Key仅进入测试Service子进程环境，普通配置/证据不含Key。
--no-captures保留功能验证、停止保存截图。所有等待有界，失败会清理自建服务与窗口；外层测试超时取消其专有进程组。
语法/帮助及显式缺失provider的拒绝/清理路径也已检查，未回退读取用户默认模型。

首轮功能验收已经通过输入/流式/历史，但退出等待失败；窗口适配补上唤醒后，完整驱动
退出0。截图取自修复前的同一视觉代码真实请求，最后完整驱动使用当前pin ff1e757，统计另存live-results.json；
两次不同请求不混为同一次测量。另一次直接运行夹具未设置资源工作目录导致字体失败，
没有计入通过结果；复跑使用CMake配置的apps/native工作目录。

## 证据与范围

实际窗口截图：[候选](../../.impeccable/review/conversation-finish-20261006/ime-first.png)、
[多行候选](../../.impeccable/review/conversation-finish-20261006/ime-multiline.png)、
[等待](../../.impeccable/review/conversation-finish-20261006/waiting-later.png)、
[流式](../../.impeccable/review/conversation-finish-20261006/streaming-live.png)、
[完成与用时](../../.impeccable/review/conversation-finish-20261006/completed-live.png)、
[上下文](../../.impeccable/review/conversation-finish-20261006/context-live.png)、
[最小深色夹具](../../.impeccable/review/conversation-finish-20261006/minimum-fixture-dark.png)。
新依赖pin的最小窗口与长回复由夹具验证，明确与实际模型截图分开。
夹具：[阅读保位](../../.impeccable/review/conversation-finish-20261006/stream-reading-light.png)、
[实际返回底部](../../.impeccable/review/conversation-finish-20261006/stream-following-light.png)。

Linux X11为本轮范围。Windows、原生Wayland、物理高DPI/其他IM未验，负责人维护者在目标
环境补跑；完整托盘/服务退出与RPA/workflow不属于本次会话目标。没有新增并发路径，
既有Executor timer和IPC所有权保持不变；上游尚未合并，不宣称发行交付或全平台保证。

Git交付：codex/native-agent-workbench；窗口唤醒独立提交0059dc3，会话滚动、依赖锁、
手动驱动与验收记录另作范围化提交。原有未提交截图保留，依赖PR尚未合并。
