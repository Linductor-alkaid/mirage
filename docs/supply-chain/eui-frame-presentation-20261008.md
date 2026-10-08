# EUI 首帧与 drawable 尺寸修复审计

- 日期：2026-10-08；工作项：M6-35；负责人：Mirage 维护者。
- 授权：维护者要求定位窗口黑闪、提交中文 issue、修复并提 PR；DEC-033 保持 UI-only 边界。
- 反馈：EUI-20261008-001；中文 [issue #93](https://github.com/sudoevolve/EUI-NEO/issues/93)、[PR #94](https://github.com/sudoevolve/EUI-NEO/pull/94)，尚未合并。
- 旧 pin：`fd9c1a983e74d2a85afc3b156274afd4d8532c14`。
- 新 pin：`a7e625fecc1db1c1acb4850a14fb785e20600041`；维护者 fork 的 `fix/window-frame-integration` 可达。
- 差异：旧 pin 保留上游 #91 原生移动/缩放与 #92 透明窗口；追加上游 #94 的两个功能修复与一处参数缩进修正。
  `1f0c955` → 集成 `1a5381e`，`ef8661c` → 集成 `27f292d`。另有参数缩进修正 `8818129` → 集成 `a7e625f`。7 个文件，+278/-21。
- 无新依赖、无新增嵌套 pin，Apache-2.0、OpenGL 与 UI-only 消费边界不变。

## 行为与调用方影响

窗口参数新增默认保持可见的 visible；App runner 主/子窗口显式隐藏创建，首次有效绘制后、紧接 present 前显示。
帧节拍等待后重读尺寸；update 后重新核对宽高、DPI 和指针比例，变化时零时间重排一次，不重复消费输入。
若再次失效则本次不提交，保留重绘并唤醒。onStart 期间窗口尚未显示；应用恢复窗口不得提前抢占首次显示。
无新增线程、业务并发或调度器。

320×240 在 update 后调整到 480×360 的真实窗口探针，在旧代码稳定报告旧尺寸提交、待重绘丢失。
修复后测试覆盖首次显示顺序、idle、更新期尺寸变化、DPI/指针比例变化、二次失效和零尺寸恢复。
首帧 map/swap 不是原子操作，直接读未合成表面仍可能在二者间读到黑色像素，不能承诺任意平台绝对无黑帧。

## 已执行验证与责任边界

上游 dev 分支修复 8818129（功能提交 ef8661c）：独立 Release GLFW/OpenGL、SDL2/OpenGL、GLFW/Vulkan、SDL2/Vulkan
四组合配置、全量 fixture 构建和 ctest 均各 34/34；新 frame_presentation_probe 的 Debug ASAN+UBSAN 通过。
命令使用私有 Xvfb，未关闭 leak 检测。四种 empty_window 实际窗口执行并捕获初始图像。
这些结果验证框架路径，不等同于真实合成器的持续拖拽验收。

仅升级该依赖后，真实 GNOME Wayland/XWayland 上 Mirage 仍有 149/300 黑色中心采样。
主要持续拖拽问题为 Mirage 自绘 grip 未使用已有 beginWindowResize，逐帧直接设置几何。
应用侧另行修复；维护者在独立诊断窗口物理拖拽确认顺畅且无黑闪。
不能把本依赖 PR 单独称为该症状的完整修复。

原工作区 Release 首轮 51/52，事件订阅压力测试两项终态断言失败，降低负载后重跑 1/1 通过。
基于 origin/master 的独立工作树（本依赖 pin 与应用修复组合）完整配置/构建、Release 全量 ctest 52/52；
日志 /tmp/mirage-frame-clean-{configure,build,tests}.log。原生最终程序化 resize 诊断启动 365、resize 300 次采样均无黑色中心，
仅代表私有 Xvfb renderer 路径。最终托盘生命周期 24 项检查全部通过（/tmp/mirage-frame-clean-tray/lifecycle-results.json）。
托盘生命周期、最小化/恢复和同进程复用通过；最终原生 UI 两项测试 2/2 通过。
Windows/原生 Wayland：负责人维护者，目标环境可用后使用实际鼠标补验；上游 PR #94 的 [CI 四组合](https://github.com/sudoevolve/EUI-NEO/actions/runs/37791651376) 全部成功；评审仍开放。
