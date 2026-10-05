# EUI Vulkan CI与Mirage依赖收尾

> 状态：Completed（Linux验证范围）；负责人：Linductor-alkaid；工作项：M6-19；依据：DEC-041及维护者明确授权。

## 修复与范围

生命周期探针无条件包含未使用的GLAD且硬编码OpenGL窗口，导致Vulkan编译失败，
删除头文件后仍有窗口/后端不匹配。本轮移除GLAD并复用公开windowRenderApi()；
onStart→首次compose→shutdown断言保留，不新增渲染实现、线程或调度器。

普通merge同步上游dev88a9ec1的既有修复，保留IME提交历史，不force-push。
本轮EUI pin为ed1deb6d242c8c1472d782d042b54fbf9a208bb2，包含独立探针修复提交。
[PR#88](https://github.com/sudoevolve/EUI-NEO/pull/88)已更新，尚未合并；维护者fork可获取SHA。
Mirage仍使用GLFW/OpenGL。上游已经修复Release无异常构建，删除库层-fexceptions临时覆写；
Mirage自研应用的Executor异常处理仍保留自己的编译选项。

## 本地验证

Linux x86_64/GCC13.3、Vulkan1.3.275、Mesa lavapipe、私有Xvfb；未操作用户桌面。
SDK缺失时只下载发行版deb到/tmp独立目录并提取；未安装系统包或改系统配置。
SDL2系统运行库2.30，临时SDK修复multiarch include与运行库symlink用于本地配置，
这些环境路径不进产品配置或仓库。

| 组合 | 本地实际结果 |
| --- | --- |
| GLFW/Vulkan Release | 全构建；25 unit + 8 probe=33/33；SDK安装/两个消费者构建及consumer1/1 |
| SDL2/Vulkan Release | 全构建；33/33；SDK安装/两个消费者构建及consumer1/1 |
| GLFW/OpenGL Debug | 更新生命周期探针构建与1/1回归 |
| SDL2/OpenGL Release | 更新生命周期探针构建与1/1回归 |
| Mirage Debug/Release/ASAN | 原生状态/真实会话渲染各2/2通过；ASAN关闭系统图形库leak检测，不声明LSAN覆盖 |

```sh
cmake -S third_party/eui-neo -B /tmp/eui-glfw-vulkan-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DEUI_BUILD_APPS=OFF -DEUI_BUILD_USER_APPS=OFF \
  -DEUI_BUILD_TEST_FIXTURES=ON -DEUI_ENABLE_INSTALL=ON \
  -DEUI_WINDOW_BACKEND=glfw -DEUI_RENDER_BACKEND=vulkan \
  -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON
cmake --build /tmp/eui-glfw-vulkan-build -j2
xvfb-run -a ctest --test-dir /tmp/eui-glfw-vulkan-build -L 'unit|probe' --output-on-failure
cmake --install /tmp/eui-glfw-vulkan-build --prefix /tmp/eui-glfw-vulkan-package
cmake -S third_party/eui-neo/tests/consumers/find-package -B /tmp/eui-glfw-vulkan-consumer \
  -DCMAKE_PREFIX_PATH=/tmp/eui-glfw-vulkan-package
cmake --build /tmp/eui-glfw-vulkan-consumer -j2
ctest --test-dir /tmp/eui-glfw-vulkan-consumer --output-on-failure
```

SDL2对应用sdl2参数并使用EUI_DEPS_MODE=auto；SDK不在系统时额外设置其prefix/Vulkan
include/library与glslang PATH。本机用小型displayfd运行器创建私有Xvfb替代xvfb-run，
显式选择lvp ICD，超时与进程关闭均有界；没有修改产品后台工作路径。
首轮本地失败（旧基线异常语法、提取SDL SDK路径/静态库回退）保留记录；分别通过同步
上游修复与修正临时测试SDK解决，不把环境修正包装成依赖产品修改。

## 远程CI与限制

EUI [run37338533567](https://github.com/sudoevolve/EUI-NEO/actions/runs/37338533567)
在ed1deb6上完成，4/4 jobs成功：GLFW/SDL2 × OpenGL/Vulkan，每项包含全构建、unit、
probe、SDK安装与安装后消费测试；确认整体conclusion=success。Mira PR#79的
pull_request run [37332976997](https://github.com/Linductor-alkaid/mira/actions/runs/37332976997)
已成功：12/12 jobs，GCC/Clang Debug/Release、Windows Debug/Release、Android两个ABI、
ASAN/UBSAN/TSAN与quality。这是PR merge配置的CI，未执行合并；同SHA push run还有
单独quality在运行，不误称两个run都完成。Mirage的Windows原生窗口/输入法仍未验证。

物理GPU、原生Wayland、其他IM/高DPI由维护者在目标环境补跑；本轮不扩大平台保证。
M6-04统一入口/托盘退出仍待后续产品工作，本轮没有开启桌面/RPA/workflow。


Git与本轮门禁：依赖分支新增dev merge和独立探针提交，已普通push更新原PR#88，
未合并PR或改写历史。Mirage同步锁/指针、删除库层临时覆写，格式/49公共头边界/差异与
凭据内容扫描通过。旧未提交截图不纳入本轮。
