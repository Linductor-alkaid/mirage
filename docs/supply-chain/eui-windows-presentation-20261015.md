# EUI Windows 透明呈现升级审计

> 日期：2026-10-15
> 负责人：Mirage 维护者 / Codex
> 授权：维护者明确允许定位为上游问题后提交 Issue、修改上游并按贡献流程提交 PR。
> 工作项：BUG-20261014-007；[维护计划](../plans/maintenance-windows-presentation-20261014.md)

## 锁定与范围

旧 pin `89c2d0f1db41514e668c43f97f613ee9e3b9b700` → 新 pin
`bcfaa2ead595077a3edb0b8cc3a20f4142fb4afd`，来源仍为维护者 fork
Linductor-alkaid/EUI-NEO。由两个独立提交组成：

- `f052729`：SDL2 非 X11 move/resize helper 的既有 false fallback 移出错误的外层条件编译；
  [Issue #110](https://github.com/sudoevolve/EUI-NEO/issues/110)、[PR #111](https://github.com/sudoevolve/EUI-NEO/pull/111)。
- `bcfaa2e`：bundled GLFW Win32 WM_PAINT 初始化透明窗口的 DWM 重定向表面 alpha；
  [Issue #109](https://github.com/sudoevolve/EUI-NEO/issues/109)、[Draft PR #112](https://github.com/sudoevolve/EUI-NEO/pull/112)。
  #112 依赖 #111，前置提交尚未合入上游；未执行合并或改写已发布历史。

直接依赖、许可证、嵌套 pin、资源与默认 OpenGL 消费方式不变。新增追踪内容为小型回归与说明，
不含运行时依赖、生成物、媒体或本机工具。Mirage 的 externally supplied GLFW 来自这份 bundled
源码，因此实际消费 Win32 修复；Linux XIM source/hash 不变。锁文件与 gitlink 同步更新。

## 根因与最小修复

维护者确认从开始菜单打开时黑角间歇出现，上游 transparent_window_viewer 的真实桌面外圈也黑。
实际 GL 角像素读回 RGBA=0,0,0,0，glGetError=0；问题位于独立的 DWM 重定向表面。
GLFW 3.4 的 WM_PAINT 只投递 damage，没有初始化表面 alpha。

采用 jeweg 在 [glfw/glfw#2815](https://github.com/glfw/glfw/pull/2815) 提议的透明窗口专用
PatBlt(BLACKNESS) 修复，保留 damage/repaint 和不透明窗口处理。源码及说明保留归属。
不是窗口区域裁剪、色键或关闭缓存。曾尝试的 GL 默认缓冲 clear 未稳定解决问题，已撤销；
临时读回日志不在提交或安装包中。

## 本机验证

Windows 11 build 26200 / x64 / i9-12900H / NVIDIA RTX 3070 Ti Laptop GPU
（32.0.15.9174，另有 OrayIddDriver）；MSVC 19.44.35214，SDK 10.0.26100。

| 独立目录 | Release 构建 | 完整 CTest（36 项） |
| --- | --- | --- |
| build/eui-glfw-opengl | 通过 | 35 passed / 1 skipped / 0 failed |
| build/eui-sdl2-opengl | 通过 | 34 passed / 2 skipped / 0 failed |
| build/eui-glfw-vulkan | 通过 | 34 passed / 2 skipped / 0 failed |
| build/eui-sdl2-vulkan | 通过 | 34 passed / 2 skipped / 0 failed |

configure：`cmake -S third_party/eui-neo -B build/eui-<window>-<render> -G "Visual Studio 17 2022" -A x64
-DEUI_WINDOW_BACKEND=<glfw|sdl2> -DEUI_RENDER_BACKEND=<opengl|vulkan> -DEUI_BUILD_APPS=OFF
-DEUI_BUILD_TEST_FIXTURES=ON -DEUI_ENABLE_TRAY=OFF -DEUI_ENABLE_MODULES=OFF`。
随后 `cmake --build <目录> --config Release --parallel 6`、`ctest --test-dir <目录> -C Release --output-on-failure`。

Vulkan 初次 configure 因缺少 SDK 失败；不把该次失败记为通过。最终明确提供本地临时工具：
官方 Vulkan-Headers v1.4.328（9a0f3099）、官方 glslang 16.6.0、系统已安装 vulkan-1.dll
及通过 MSVC lib 从导出表生成的 import library；分别经 Vulkan_INCLUDE_DIR、Vulkan_LIBRARY、
Vulkan_GLSLANG_VALIDATOR_EXECUTABLE 传给 CMake。未安装 SDK/驱动、未改系统运行库。

platform_dialog 是原有 opt-in skip；transparent_cache_probe 在非 Win32 GLFW/OpenGL 组合
明确以 77 返回 N/A。skip 不算覆盖通过。最终回归修改已在四组合重建，相关 probe 定向补跑；
GLFW/OpenGL 最终完整测试耗时 23.85 s。

同一台机器的 negative/positive：恢复旧 Win32 procedure 后，初始化计数 0、退出 1；
恢复修复并强制更新源码时间戳重建后，计数 1，六帧透明边缘匹配自有背景、退出 0。
像素验证之外，MSVC probe 转发并观察真实 GDI 初始化，避免初始已清零的分配让旧实现偶然通过。
其余组合回归场景不适用，保持明确 skip。

四组合相关 fixture 已实际启动并检查；受影响的 GLFW/OpenGL 外圈由黑变为可见桌面/图标。
SDL2 的 false transparency 保持原契约；GLFW/Vulkan fixture 的 GL-current-context capture
不构成 Vulkan alpha 授权证据，不声明新的 Vulkan/SDL2 透明保证。只验证本次 Windows 环境，
其他 GPU/显示器由维护者在对应机器补跑同一 fixture 和 probe。

## 2026-10-15：安装版黑角复发，修补范围更正

维护者安装上一轮包后再次报告四角黑色。Program Files 的 mirage-native.exe 与该包
SHA256 均为 `8DE1DF014AFCFFFF12DE1AF5DD227590DBD2D2CF615D849FD319B3DD3FB71CF6`，
排除运行旧版本。前述 probe/fixture 的通过只证明受测路径中初始化生效，不能证明目标
机器间歇性 alpha 问题全面解决。上游 Issue #109 / Draft PR #112 应保留未解决状态，并
撤销 closes 声明；本次不再改变 EUI pin 或把 Mirage 的固定圆角策略下沉至通用 GLFW。

[GLFW #2815](https://github.com/glfw/glfw/pull/2815) 仍未合入；另一个使用者的
[最小样例反馈](https://github.com/Wixely/CupriFace/issues/212) 记录 WM_PAINT 清零确已执行
仍为黑色，与本机现象相容，不能据此确定 Oray 或 NVIDIA 是本机因果来源。
Mirage 在单一私有 Windows 窗口适配边界加产品轮廓保护：12 logical px 与 EUI 绘制半径
保持一致，尺寸/DPI 更新时重设 Windows region，最大化移除 region。轮廓外不参与桌面
合成，从而不依赖其 alpha 是否正确；这不是通用透明背景修复。代价是轮廓为整数像素
裁剪，部分 DWM 系统阴影行为会变化。移除条件：上游 alpha 在同一目标机器冷启动、恢复、
跨 DPI、缩放缓存帧重复验证后稳定，再删除该产品保护。关键轮廓/恢复测试见维护计划。
