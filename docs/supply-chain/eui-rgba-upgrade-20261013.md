# EUI RGBA 转码修复升级审计（2026-10-13）

> 状态：Completed（本轮升级与适用回归；物理平台门禁见范围限制）
> 授权：维护者明确要求按管理流程创建中文 issue、直接处理 EUI 缺陷、PR 合入目标为 dev。
> 关联：M8-02/03/05、EUI-20261013-001、BUG-20261013-001。

## 版本与来源

- 旧 pin：a7e625fecc1db1c1acb4850a14fb785e20600041，维护者 fork 的窗口特性整合版本。
- 新 pin：上游 dev 的 89c2d0f1db41514e668c43f97f613ee9e3b9b700，v0.6.1 release preparation。
- 核对时发现根因已由 [PR #97](https://github.com/sudoevolve/EUI-NEO/pull/97) 的
  f8caff1 合入 dev；本轮消费既有修复，不再制造重复实现。
- 中文下游 [issue #106](https://github.com/sudoevolve/EUI-NEO/issues/106) 记录最小复现、
  使用旧 pin 的影响、stride/alpha 回归和迁移计划。[PR #107](https://github.com/sudoevolve/EUI-NEO/pull/107) 以 dev 为 base（head 977f9ea），四项 CI 均通过，尚未合并。

## 差异与边界

旧 pin 到新 pin：12 文件，207 行新增、890 行删除。核心变化为 RGBA/BGRA 目标指针
每列推进、已有多像素回归、Slider 移除越过宿主 Overlay 的 hit zIndex；窗口透明查询
只有声明/定义位置调整，先前 move/resize、透明 framebuffer 与 frame-size 修复保留。
发布清理删除未注册的 vcd_viewer 演示及其说明，未删 Mirage 消费的 API。

Apache-2.0 及 LICENSE 无变化；无新增依赖、媒体、生成物或嵌套 pin。公开 ABI/API
没有本轮新增契约。Mirage 仅通过 submodule gitlink + 锁文件升级，不将依赖源码带入
主仓库提交，也不把产品并发/播放策略下沉 EUI。

本轮上游贡献仅给现有 image_stream 单测增加 35 行，测试 RGBA/BGRA 3×2、stride16、
padding、straight alpha、输出缩短和 ImageStream submit/consumeLatest。新增追踪内容 1764 字节；新增自有源码
少于 500 行/100 KiB，不触发 CONTRIBUTING.md 的事前体积审批门槛。

## 验证与迁移

- 回归独立编译：当前修复版返回 0；缺列推进、忽略行 stride、强制 alpha255 三种故障
  注入返回 1。测试使用显式 if/return，Release 的 NDEBUG 不会移除新增检查。
- 四种前后端组合均使用独立目录、Release、EUI_BUILD_TEST_FIXTURES=ON；配置与完整
  构建与全量 ctest 各 35/35。首次 GLFW 测试受继承的宿主 Wayland 环境影响；明确
  移除 WAYLAND_DISPLAY、设置 XDG_SESSION_TYPE=x11 后四组合全量通过。没有跳过
  探针或修改其断言。
- Mirage 删除临时 FrameTexture/OpenGL 上传，恢复容量 1 的公开 ImageStream，保留
  不可变 RGBA 帧与透明替换。私有 framebuffer / 实进程 22/22，实际角色区域、
  alpha、持续帧、最小化恢复、独立启停/崩溃恢复和隐藏关闭通过。
  证据目录 /tmp/mirage-m8-pet-eui-fixed-20261013；详见
  [M8 验证记录](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。
- 全量 GUI sanitizer、真实 compositor、硬件 GPU 与跨平台视觉门禁仍独立开放。

所有生成日志/故障注入源码/构建目录位于 /tmp/eui-rgba-*，不进入仓库。

### 四组合复跑命令

```sh
cmake -S /tmp/eui-rgba-stride-alpha -B /tmp/eui-rgba-build-glfw-opengl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DEUI_WINDOW_BACKEND=glfw -DEUI_RENDER_BACKEND=opengl \
  -DEUI_BUILD_APPS=OFF -DEUI_BUILD_USER_APPS=OFF -DEUI_BUILD_TEST_FIXTURES=ON
cmake --build /tmp/eui-rgba-build-glfw-opengl -j 2
xvfb-run -a -s '-screen 0 1500x1000x24 -nolisten tcp' env -u WAYLAND_DISPLAY \
  XDG_SESSION_TYPE=x11 LIBGL_ALWAYS_SOFTWARE=1 \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEODRIVER=x11 \
  ctest --test-dir /tmp/eui-rgba-build-glfw-opengl -C Release --output-on-failure
```

其余三个独立目录为 /tmp/eui-rgba-build-sdl2-opengl、glfw-vulkan、sdl2-vulkan，按名
替换窗口和渲染选项；每个目录执行了上述配置、完整构建与全量测试。测试贡献追踪净
增 1764 字节。Linux 7.0.0-38-generic / Ubuntu 24.04，Intel Core Ultra 5 225H，
32 GB RAM，GCC 13.3.0；图像 probe 使用 Mesa 软件渲染/lavapipe，不声明物理 GPU 验收。
