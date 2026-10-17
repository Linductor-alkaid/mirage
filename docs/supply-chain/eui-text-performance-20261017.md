# EUI 文字测量性能升级审计

> 状态：Accepted（本机验证完成；上游PR待评审，Windows实机另验）
> 日期：2026-10-17
> 负责人：Mirage维护者
> 工作项：M6-42 / EUI-20261017-001
> 依据：DEC-033/040/041；维护者明确授权先更新最新dev、本地修复并验证通用收益，再提交issue/PR。

## 版本与来源

- 原Mirage pin：`bcfaa2ead595077a3edb0b8cc3a20f4142fb4afd`。
- 先切换、构建及建立基线的最新上游dev：`ad7e9e721a850fe2e53b980e39bfc2aa6dc56d2f`。
- 独立性能修复：`b59b6e907ac6ff26ffb3ed8ea1c08b3f570f6a88`，
  [issue #113](https://github.com/sudoevolve/EUI-NEO/issues/113)、
  [PR #114](https://github.com/sudoevolve/EUI-NEO/pull/114)，base=dev。
- 最终Mirage集成pin：`63117f77caac0d17f3d51e81afe00f1dd893de4f`，维护者fork的
  `perf/native-text-integration`。在性能分支上合并原Windows修复
  `f052729`/`bcfaa2e`，保留 #110/#109 及既有Windows透明测试。
  性能PR没有混入这两项窗口修改，也没有合并上游PR或改写已发布历史。
- `dependencies.lock.json` 与gitlink同步；mira/mirador及其嵌套pin不变。

原pin到最终pin净变化仅为文字测量实现、对应测试/文档/原始统计，以及最新dev已存在的
软件渲染TODO文档。没有新依赖、资源、生成代码、二进制或许可证变化。EUI公开头文件
没有变化。原Windows源代码/测试在最终树中保留，不由Linux测试推断Windows实机已验收。

## 缓存边界和行为

每个固定字号 `FontFace` 内512槽直接映射表，保存字形编号、未缩放advance和height，
约6KiB/face；碰撞覆盖单槽，无动态增长、淘汰扫描或额外owner。随字体移动，随原字体
owner销毁。只缓存成功加载，Tab/组合字符保留早返回；彩色fallback按调用方主字体高度
重新钳制，绘制仍显式加载FreeType slot。没有新线程、后台任务或调度设施。

## 实际验证

Ubuntu24.04.5 x86_64、Linux7.0.0-38、Core Ultra5 225H、30GiB RAM、GCC13.3、
CMake3.28.3；Release -O3。私有Xvfb，Mesa25.2.8 llvmpipe LLVM20.1.2。

独立性能提交的四种 GLFW/SDL2 × OpenGL/Vulkan Release完整构建、测试各36/36，
安装SDK消费者各1/1。保留Windows修复后的最终集成树四组合复验各36通过、1个
Windows-only `transparent_cache_probe` 明确skip；不把skip作为Windows成功证据。
各矩阵使用独立TMPDIR/XDG，首轮共享图片缓存临时文件造成的一项失败保留于验证记录。

公共测量接口两种字体、6场景、两种物理字号、3轮交错A/B：72组坐标/UTF-8索引
指纹逐位一致，独立FreeType oracle通过；原始144条统计随上游PR提交。常见短文、
长文、Emoji约10–75倍测量加速；3000个不同汉字工作集基本持平。缓存带来小幅RSS
增加，原始内存数据与边界均已记录，不声称零成本或任意工作集加速。

Mirage另接入EUI公开 `ScrollViewBuilder::contentKey`，避免未变化的历史在输入时
重复构造两个测量UI。精确长度编码的key包含消息/part正文、状态、展开、字体、缩放
和视口；64KiB上限，超限退回原自动测量。组件原有4键缓存保持有界，不使用散列碰撞
可能产生的过期几何，也不引入第二套布局/字体实现。

前后输入路径统计、Release/ASAN/UBSAN和截图证据见
[原生输入性能验证](../compatibility/native-input-performance-20261017.md)。

## 复现入口

```bash
cmake -S third_party/eui-neo -B build/eui-perf/glfw-opengl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DEUI_BUILD_APPS=OFF -DEUI_BUILD_USER_APPS=OFF \
  -DEUI_BUILD_TEST_FIXTURES=ON -DEUI_ENABLE_INSTALL=ON \
  -DEUI_WINDOW_BACKEND=glfw -DEUI_RENDER_BACKEND=opengl \
  -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON
cmake --build build/eui-perf/glfw-opengl -j 2
mkdir -p /tmp/eui-perf-isolated/tmp /tmp/eui-perf-isolated/cache
TMPDIR=/tmp/eui-perf-isolated/tmp XDG_CACHE_HOME=/tmp/eui-perf-isolated/cache \
  xvfb-run -a ctest --test-dir build/eui-perf/glfw-opengl --output-on-failure
```

其余组合替换窗口/渲染后端和独立构建/临时目录；完整日志位于
`/tmp/eui-perf-*-isolated-test.log`、`/tmp/eui-perf-*-consumer-test.log`、
`/tmp/eui-integrated-*-{build,test}.log`。上游CI与最终输入统计在性能验证记录中补充。
