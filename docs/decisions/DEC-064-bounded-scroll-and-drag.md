# DEC-064：有界滚动与完整拖拽

> 状态：Accepted（维护者授权按调整后顺序推进开发）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-09](../plans/m7-visual-tools-and-input.md)

## 能力路由与首交付

复用 Mira 工具注册/执行和现有动作后验证；复用 Mirador 已交付的编号/区域，
不自行判断控件语义。Linux X11 输入映射留在 Platform Backend，有限动作在
已有 Executor 调用任务内推进，不新增线程、调度器或等待模型的持键任务。
首交付为 Linux X11 受控视觉路径；Windows/原生 Wayland 独立补验，缺能力不
暴露新工具，M7-09 全范围不得提前标 Completed。

## 契约

- InputProvider 增加独立 opt-in 的 scoped scroll/drag 完整动作。请求携带目标
  窗口 ID、观察时窗口几何和已解析的全局像素起点/终点，公开类型不含平台类型。
  默认返回 unsupported_platform，旧 complete_actions_supported 不代表新能力。
- `desktop.vision.scroll` 使用当前 visual_scope、mark、window_id、epoch，附
  horizontal_steps / vertical_steps；离散轮步正数为右/下，负数为左/上。
  每轴 -16..16、两轴绝对值总和 1..16，不承诺像素滚动量或平滑滚动。
- `desktop.vision.drag` 同时提供 mark 和 destination_mark，二者必须属于同一
  快照、完全位于当前前台窗口/供给范围。仅左键，起终点不能相同，欧氏距离
  至多 2048 像素，固定 16 段线性路径，无持续按住/续接接口。
- 单次完整动作最多 1000 ms，并服从更短的 Task deadline。预算含平台步骤，
  超时/取消仅协作收敛，清理不因截止而跳过；不承诺 OS 往返的硬实时上界。
- 沿用五秒引用、原四次采集和同范围逐像素重验；原始像素、窗口/显示器几何、
  焦点、权限、owner/epoch 不符合即拒绝。两引用解析在同一个 registry 锁中
  完成，不用嵌套单引用回调，以防换代混合及自锁。
- 同一 ActionCoordinator lease 覆盖全动作，任何成功或有副作用的尝试使旧
  epoch 不再用于后续输入。滚动/拖拽后的旧引用必须重新观察，无隐式续期。
- Backend 在移动/轮步边界检查窗口存在、几何、前台和指针下窗口归属；禁止
  预先已按住的 X11 core 1–5 号按钮或 Ctrl/Alt/Shift/Super 修饰键参与本动作。
  core 查询不提供扩展按钮按住状态，不宣称覆盖扩展物理设备。仅释放本动作尝试按下的按钮；取消、
  takeover、焦点变化、超时或失败后仍执行释放和同步。失败发布结构化回执，
  释放不确认则 fence owner；不重试不确定输入。
- 两工具进入 DEC-056/059 的 mutation 验证路径，提前固定后条件，派发与业务
  成功分开；readonly 验证和宿主终态另取新观察。不重绘负例不得报告完成。

## 验证与限制

私有 Xvfb 验证双轴轮步、拖拽顺序/终点与最终释放；验证非法预算、窗口移动/
焦点/遮挡、物理按住、运行中取消/超时/异常探针、takeover、提交拒绝和关闭。
工具组合覆盖换代、跨 scope/边缘起终点、权限拒绝、动作失败后门禁、释放失败
fence 与新验证。Debug/Release、ASAN/UBSAN、跨上下文关闭路径 TSAN 均留证。
既有慢模型在线项、完整权重 TSAN 业务期限、任意 DPI/跨屏与目标平台不由此
替代。X 查询与注入不是原子事务；拖拽路径途中视觉变化不重新执行 OCR，也不
以可见外观推断拖拽业务结果。

首增量的实际结果与补跑条件见
[滚动与完整拖拽验收](../compatibility/bounded-scroll-and-drag-20261010.md)。

## 原生应用验收后的 X11 步进修正

GTK3 原生重排列表验收发现：16 段零间隔 XTest motion 被应用合并，drag-begin
虽建立，但 release 到达时未完成目标协商，drag-failed 且列表未改变。完整
轨迹/释放是输入派发证据，不能替代实际应用效果。

拖拽的 16 个运动请求使用 XTest 自身的 8 ms delay 参数，让独立应用有机会
处理前一步。它是本次同步平台动作的输入交付参数，不创建 Mirage 延时任务、
私有 timer、sleep 或新 worker；全部往返仍在原 Executor 有限调用任务中，
预算、取消/焦点/归属检查继续在每步前后执行。清理 release 始终零延迟，
不会因节拍、取消或截止延后。该间隔不是任意应用完成拖放协商的保证。

平台 delay 语义依据 [X.Org XTEST 文档](https://www.x.org/releases/X11R7.5/doc/Xext/xtestlib.html)，
应用效果与取消收敛由第二增量独立取证，不追溯修改首轮机械输入证据。

带节拍后，GTK 的原生 drag icon 会成为 QueryPointer 的顶层 child，触发归属
检查提前释放。曾评估 ShapeInput 命中解析，但实际应用仍被拒绝；未将尝试
保留在实现/构建中，也不凭窗口类型、标题或 PID 绕过归属检查。原生列表
XDND 拖放仍未交付，第二增量仅将其作为安全拒绝负例；正例使用 GtkPaned
原生分隔条位移，直接读取实际控件状态，不把两者等同。

第三增量的后续归属契约见 [DEC-065](DEC-065-x11-drag-helper-ownership.md)。
第二增量拒绝结果作为历史证据保留；当前可选 XRes 路径的原生列表正/负效果见
[独立验收](../compatibility/x11-native-dnd-20261010.md)。
