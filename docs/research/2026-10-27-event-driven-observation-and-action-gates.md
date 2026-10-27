# 最新会话的动作误拦截与事件驱动观察

> 后续：维护者已接受建议；当前实现以 [DEC-117](../decisions/DEC-117-runtime-resident-perception-and-autonomous-input.md)
> 与[常驻设计](../design/runtime-resident-perception.md)为准。下文“尚未修改/待实施”是调查时点的历史。

> 状态：调查完成；产品修改待实施（M9-22 Planned）
> 日期：2026-10-27（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-22；关联 M9-06/07/09/10/11/19/21
> 依据：DEC-056/081/083/107/110/113/115；本记录不改变现行产品默认值

## 结论

最新留存会话的两次 `arrival_target_changed` 来自 **Mirage 的共享输入 Adapter**，
不是 Mira 模型拒绝、Mirador 权限判断或系统授权拒绝。Mirador 只返回变化检测结果；
把结果变成不派发点击的硬门禁，是 Mirage 的策略。鼠标移动完成但按键尚未派发时，
Adapter 将移动的副作用上报，上层又统一映射为 execution uncertain，扩大了恢复成本。

维护者现场观察是鼠标均正确到达目标。独立合成复现也证明：按钮身份、几何和点击区域
均不改变，仅悬停配色变化就能触发当前守卫。原会话未保留到达帧及分支诊断，不能断言
两次拒绝的具体像素原因，也不能从“鼠标到位”推断 click 已经发送。

应优先删除普通自主坐标动作的启发式像素否决及强制到达取帧，将变化提示用于重规划；
按动作阶段报告真实执行情况。观察改为消费当前状态和事件日志，缺少任务所需证据时
才读取共享帧/局部图像。底层采集、识别、编码和送模型必须分别计数。

## 会话事实与证据等级

源文件为本机 state/sessions 中最后一个非空会话，创建于 **11:24:39.151**，
结束记录为 **11:26:24.225**；任务为浏览视频。源文件 SHA-256：
`4ed0f96b9676d631c93b5d36ee0e75a40bd48bfff49ce35cdc56df8da7cdf9d2`。
仅汇总计数与错误，不复制窗口列表、完整工具参数、正文或图像。

| 观测项 | 留存结果 |
| --- | --- |
| 最终状态 | failed；`dialog turn was cancelled`，不是工具失败自动终止的证据 |
| 模型请求 | 23 次，累计 97,668 ms |
| 前台 capture 计时 | 19 次，累计 994 ms；包含工具内的采集阶段，不代表全部后台/源帧数 |
| OCR 计时 | 2 次，累计 1,676 ms |
| 显式 capture 工具 | 10 次，其中 1 次 `reused_capture=true` |
| capture 变化提示 | unknown 6、none 2、partial 2；none 是阈值内无显著变化，不是像素相等 |
| 动作 | click 3、key 3、open_uri 1；没有一条 click 成功回执 |
| 其他工具 | application.list 1、window.list 1、semantic_snapshot 1、OCR 2、wait 1 |
| 工具失败 | 5/23；分类见下表 |

| 过程 part 下标（从 0 开始） | 失败 | 直接归属与含义 |
| --- | --- | --- |
| 6 | `snapshot_too_large`（4096 nodes） | Accessibility 平台快照容量，缺少有用的局部/裁剪结果；不是权限拒绝 |
| 18、36 | `arrival_target_changed` | 共享输入 Adapter，鼠标移动后、实际 click 前的到达重验 |
| 22 | `invalid_argument` | `key_name=super` 不符合当前词表；随后 `meta` 成功。应提供规范化别名/明确错误字段，无需截图纠正 |
| 44 | `interactive_image_required` | part 42 的 text_only OCR 清空原输入图像上下文；模型继续使用 part 40 的 scope，被本地契约拒绝 |

part 26 的成功 key 回执已带最新观察；part 29 的显式 capture 返回同一 scope、
SHA、source_sequence 和接收时间，并标记 reused_capture。它是确定可省的工具往返/
重复供图，不是一次新的底层采集。part 24 也报告 none；不能将所有其余截图均归为无效。

调查基线为 HEAD `f42d878774383f8f2991c9b4066110a78c7ee126` 加既有未提交修改；
Mira `68a8bf17b30af246b722fbb548c033de82efe26e`、Mirador
`2a9f4830f4727498ed4cbad568a1d3ec357ece7e`。当前 native 进程路径指向
build/native-release，启动于 11:24:14，磁盘 native 二进制修改于 01:21:24。
未取得当时服务的完整请求供给/加载映射，不能将当前源码的每个分支推定为会话实际路径。

## 拦截链路

相关代码：
[共享输入](../../integration/mira/src/desktop_shared_input.cpp)、
[像素守卫](../../integration/mira/src/desktop_stability_guard.hpp)、
[动作核验](../../integration/mira/src/desktop_action_verification.cpp)。

1. `shared_input` 校验任务、权限、scope、epoch、参数和图像上下文。
2. 首先 `capture_presented`，比较旧图与当前目标裁剪；kChanged 则
   `target_pixels_changed`，此分支会发布刷新图，且声明无输入副作用。
3. click/scroll/drag 先执行 Move。成功后再 `capture_presented`，要求更新 source_sequence，
   并将**旧规划图**与到达帧比较；不是比较目标身份或点击接收者。
4. 到达阶段把采集失败、无效布局、尺寸变化、水位不前进、取消或像素变化合并到
   `arrival_target_changed`（取消独立码）。因此这一个错误码无法准确定位原因。
5. 返回 `side_effect_may_have_occurred=moved`；此处还没调用实际 click。
   verification 对 effects=true 的错误统一赋 uncertain，并要求恢复观察。
   “鼠标确实移动过”和“按钮是否已经点击不确定”没有独立字段。

`assess_target_stability` 用外扩 8 px 的裁剪作为 detect_change 输入；其 kGlobal
是**裁剪内** changed-block ratio 达到 0.5，不能解释为全桌面发生场景切换。
kGlobal 在主体相似度检查之前直接拒绝；kPartial 仍要求主体 32×32 灰度相似度 ≥0.98。
这种统计不能判定是否仍为同一按钮，既可能拒绝合法换色，也可能漏掉低差异替换。

DEC-083 已记载 10 月 12 日 Dock 动画导致同类误拒；10 月 23 日验收再次记载
arrival_target_changed。说明目前的测试覆盖小幅闪烁，却没有解决一般悬停/预览变换。

## 无效取帧的接线原因

- [capture_desktop](../../integration/mira/src/desktop_image_supply.cpp) 只在同参数、
  同 epoch、未要求动作后水位、已有图像且小于 5 秒时复用。模型推理跨过该期限就取新图；
  ROI/整屏切换也不命中相同 capture key。无实质变化不自动延长此窗口。
- 普通共享输入始终前置取帧；指针动作到达后又取帧。模型在本次所有 session 动作中
  显式请求 shared_frame_observed，成功后还会取后置帧。后置验证不是所有动作的默认硬要求，
  工具描述已允许省略 verify_kind，但本次模型没有选用。
- text_only OCR 在 clear() 后重新取帧分析，且退休原图像动作上下文；新 OCR 文本又不授权
  动作。这使“先读文字再点击”重新依赖 capture，part 42→44 就是实际实例。
- [连续采集宿主](../../integration/mirador/src/continuous_capture_host.cpp) 默认每 250 ms
  尝试采样；[串行包装器](../../integration/mirador/src/continuous_desktop_session.cpp) 的
  前台 capture 仍直接访问 raw session，没有消费后台 latest 帧的共享入口。
  静态分析可以跳过 OCR，但门控发生在采集及缩放之后，不能据此声称减少了源取帧/拷贝。
- [供给](../../integration/mira/src/desktop_perception.cpp) 已自动传入有限事件、已读摘要和
  semantic_scene，但明确是 historical、not input references。日志能减少重复理解，
  不能在现契约下替代共享输入所需上下文。text_only OCR 的 continuous 字段来自显式分析
  companion（background_updates=false），不能用它断言后台整个未运行。
- [PipeWire 适配](../../platform/linux/src/portal_pipewire_capture.cpp) 已分开处理 cursor
  metadata，并保留 latest；当前只协商 Header/Cursor，尚未利用 VideoDamage。
  latest 返回 ImageFrame 值副本；同一 source_sequence 的应用层读取仍可能复制像素。
  source_sequence 的跨度是流中有效像素 buffer 序号，不是模型截图数或应用 copy 次数。

Mira 的公开 ModelInputSupplier 在每轮组装时同步调用一次，本身不截图；宿主已有事件
注入入口。不能在 supplier 中阻塞等待事件，也无需为这次改进重写 Mira 状态机。
Mirador 已有变化筛查、session cache、稳定 ID、连续事件；按 pinned integration 的
session-and-caching 卡核对。本调查没有确认需要修改依赖的能力缺口。

## 外部实现对照（2026-10-27 回读）

| 项目与一手来源 | 核实的机制 | Mirage 应用位置及限制 |
| --- | --- | --- |
| [Playwright actionability](https://playwright.dev/docs/actionability) | Stable 指连续两帧包围盒一致；Receives Events 检查动作点命中接收者；press 没有点击那组检查 | 用元素身份/几何/接收者判断已知目标；不能将 DOM 能力推定为所有桌面均具备，也不应替换成另一套低置信硬门禁 |
| [Playwright MCP snapshots](https://github.com/microsoft/playwright.dev/blob/main/mcp/snapshots.mdx) | 动作返回结构化可访问树；可局部深度限制/检索；图像用于需要视觉细节的场景 | 平台语义/当前对象状态与动作回执优先，避免每步整图；本次 4096 节点失败说明需局部观察回退 |
| [Browser Use 参数](https://docs.browser-use.com/open-source/customize/agent/all-parameters) | use_vision=auto 仅按请求使用视觉；该文档默认值仍为 True | 借鉴按需视觉机制，不误称所有项目已默认零截图，也不把少送图片等同于少取底层帧 |
| [TigerVNC XDesktop](https://github.com/TigerVNC/tigervnc/blob/master/unix/x0vncserver/XDesktop.cxx) | 探测 XDamage；有扩展时按损伤处理，没有才轮询屏幕变化；光标独立处理 | X11 Backend 发布有界 dirty-region/source revision；不能把 XDamage 方案套到 Wayland |
| [WebRTC PipeWire](https://github.com/webrtc-mirror/webrtc/blob/main/modules/desktop_capture/linux/wayland/shared_screencast_stream.cc) | 协商 VideoDamage，累积变化区域；latest frame Share；光标单独更新；缺损伤信息退回整图更新 | Wayland Backend 可选利用元数据，消费者共享最新帧；现成实现仍可能做全帧复制，不能据此声称零拷贝或从不截图 |
| [UFO AppAgent pipeline](https://github.com/microsoft/UFO/blob/main/ufo/agents/processors/app_agent_processor.py) | 数据收集组合截图与控件信息，随后 LLM/动作 | 支持混合观察思路；这份实现本身不是“只在事件时才截图”的证据 |

这些来源是机制对照，未在本机安装或运行其基准，没有跨项目成功率/耗时比较。

## 建议实施契约与顺序（M9-22）

### 1. 普通自主动作执行与判断分离

普通坐标动作不再因像素相似度低、kGlobal/unknown 或无法证明稳定而否决，不再默认
执行“移动→截图→相似度门禁→点击”。继续在已有动作 owner 下完成有界自然移动及
一次按下/释放；取消、撤权、接管、越界、源几何不可映射和释放失败仍是明确执行条件。
若上层任务显式要求强核验，单独声明其能力与条件，不把它叠到每一个普通动作。

回执分开 approach_completed、press_dispatched、release_confirmed、observed_effect；
Move 成功、press 未调用时明确 press_dispatched=false，不伪装成点击可能已经发生。
派发异常/释放失败保留实际 uncertainty。捕获失败按采集原因报告；不对不确定 click
自动重放。键盘操作校验焦点/接收端信息，不用鼠标目标像素决定按键是否允许。

这会改变 DEC-081/083/110 的现行默认值，实施前需同步决策、安全契约和负向测试。
本记录不直接关闭旧门禁，也不把“新帧到达”算成用户业务目标成功。

### 2. 当前状态/事件日志先于截图

把只读观察与执行上下文分开：text_only OCR/历史日志查询不因其结果不含图片就
清空整个执行上下文；真实权限/源/目标变化分别失效受影响引用。支持从当前平台语义/
有版本的对象状态解析目标；历史预测或旧语义标签自身不产生权限。

默认消费 source epoch、scene revision、对象 incarnation、事件 cursor 和 gap；
文本事件能回答问题时直接继续。连续无新相关事件时不请求新图片或额外 VLM；
等待状态依赖时走有界、可取消的本地条件等待，不能把定时 wait+capture 暴露给模型轮询。
Gap/reset、目标需视觉细节、明确业务核验和任务相关变化可触发一次 ROI/关键帧升级。
成功动作回执及现有已读摘要直接成为后续输入，避免 part 26→29 的重复查询。

### 3. 分层减少底层工作

推荐路径为 Platform 变化通知/最新帧 → Mirador 变化与对象事件 → 当前状态及有界日志 →
Mira 按需理解/动作；读图从同一 source frame 派生，而不是每个消费者独立采集。
Mirador 是图像消费者，单靠“等 Mirador 事件才取任何帧”会循环依赖；应先利用平台
damage/语义事件减少采样，再减少识别、编码及模型供图。无平台事件时明确保留自适应
低频巡检及漏检边界，不能将事件缺席宣称为已经证明桌面无变化。

先统一前后台最新帧访问及按需 ROI/编码，再按 Backend 能力接 XDamage/VideoDamage；
缺失 damage 与明确空 damage 必须区分。累计损伤直到相应消费者水位，跳帧不能丢区域。
光标移动、时钟/背景动画不应让模型每次重看整屏；目标相关性和稳定版本由已有状态层处理。
继续使用现有 kairo owner、worker、mailbox 与取消/排空路径，不新增私有线程/队列。
等待/平台事件的具体调度设计实施时按 pinned kairo router/card 冻结。

### 4. 验收与尚未运行项

| 场景 | 必须观察的结果 |
| --- | --- |
| 同按钮悬停、预览动画/纯移动 | ordinary click 一次派发；不再 arrival 像素拒绝；回执阶段正确 |
| 参数别名/非法参数 | super 可规范化或准确报 key_name 错误；纠正参数无需 capture |
| OCR→点击、状态查询→点击 | 只读工具不以“没有附图片”使输入上下文凭空消失；真实目标失效仍准确 |
| 静态桌面、多次读取状态 | source frame 未更新时没有重复识别/编码/模型图片；物理 copy 单独计数 |
| 无关动画/单个相关变化 | 无关变化不唤醒 VLM；相关变化只更新所需 ROI，并保留事件水位 |
| 动作后状态依赖 | 一次新观察/所需条件即可；不再无条件 capture+OCR+verify+capture |
| 取消/撤权/接管/释放异常 | 生命周期闭合，未发送与部分发送不混淆，日志 gap/reset 可恢复 |

必须分别记录 producer buffer、application acquire/copy 字节、analysis/OCR/detector、
PNG/供图、LLM 请求/图片数，以及动作误拒/误点、任务完成率、CPU/RSS、延迟分布。
当前尚未运行修改后的配对 A/B、真实 Portal/Windows、在线业务及低端资源验证；
负责人为 Mirage 维护者，补跑条件为 M9-22 的决策/实现完成并启动受控测试会话。

## 本轮执行的验证

环境：Linux x86_64，GCC 13.3.0；不操作用户桌面、不重放原任务、不重启服务。

1. 对最新非空 session JSON 做只读提取，按工具名/状态/错误码计数，并核对复用图的
   scope、SHA、sequence、received_at。以上计数来自留存记录，不是完整底层遥测。
2. `ctest --test-dir build/debug -R '^desktop_stability_guard_test$' --output-on-failure`：
   已有 Debug 二进制 1/1 业务测试及 2 个隔离 fixture 通过；没有重建全部产品。
3. 编译并运行[独立合成探针](data/desktop-hover-gate-probe-20261027.cpp)：

   ```bash
   c++ -std=c++20 -I desktop/environment/include -I desktop/process/include -I third_party/mirador/include docs/research/data/desktop-hover-gate-probe-20261027.cpp build/debug/third_party/mirador/libmirador_image.a build/debug/third_party/mirador/libmirador_core.a -o /tmp/mirage-hover-gate-probe
   /tmp/mirage-hover-gate-probe
   ```

   输出 `same_button_same_hitbox_hover_palette: rejected`、
   `crop_change_class=2 changed_area_ratio=0.9375`，退出 0。
   退出 0 表示当前误拒机制复现，**不表示缺陷已经修复**。
   夹具内身份/位置/图形均固定，只改悬停调色板；不推断真实 UI 的误拒率。

本轮仅增加调查、复现资源与计划追踪，没有修改生产代码、依赖或门控默认值。
