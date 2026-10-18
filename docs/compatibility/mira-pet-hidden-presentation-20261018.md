# M8-06：隐藏期间暂停呈现（2026-10-18）

> 关联：M8-06；DEC-103 第三增量
> 基线：`20c9c7f`，`feat/pet-desktop-exclusion` 加本轮工作树
> 负责人：Mirage 维护者
> 前序：[工具排除](mira-pet-desktop-exclusion-20261018.md)、[直接观察](mira-pet-observation-20261018.md)

## 实现与边界

前两轮排除与观察已提交为 `20c9c7f`。本轮在 pet UI owner 上采样自己的 bounding
shape，把空 shape 和最小化接入同一可见性路径。没有新增 timer/worker/队列，也没有
修改 `third_party/` 或协议字段。隐藏暂停取消原有 33ms 动画 timer、资源解码和气泡
期限任务，消费解码 future，释放解码数组，清除旧图与气泡。隐藏期间到达的新状态
仅经原有 LatestMailbox 保留最新值。

EUI 的公开动态纹理文档明确说明：ImageStream 元素绑定会保持渲染循环活跃。因此
暂停时移除该元素绑定，并去掉常驻 onFrame，在请求 compose 时推进状态。仅保留
原有 200ms 控制检查用于发现恢复/退出，允许调度抖动，不声称完全没有唤醒或硬期限。
GLFW 的 X 连接始终在 UI owner 使用，Executor control timer 只请求 UI 更新。

恢复先保持透明并重读最新状态，资源就绪后重新出现角色和拖动区。最终气泡的剩余
时间按本进程接收时刻扣除；已过期快照不复活，无需跨进程时钟或 wire 时间戳。
该计时只覆盖本地滞留，不估算未知传输延迟。短于 UI 采样的隐藏仍由服务端空 shape
保证像素排除，不宣称每次短操作都能触发节能。真实 compositor 的瞬间绘制仍待验证。

## 环境与命令

Linux x86_64、GCC 13.3.0、CMake 3.28.3、Ninja，依赖及 XRes sysroot 与前序记录
一致。native-release 启用 pet/native/PP-OCR；原生脚本仅使用已有本地 OCR，XDG/
Xvfb 1500×1000/DBus/最小 WM 全部私有，没有在线模型请求或用户桌面动作。

```bash
cmake --build --preset native-release -j3
ctest --preset native-release -j2
# 以下构建对 asan、ubsan、tsan 分别执行。
cmake --build --preset asan --target pet_bridge_lifecycle_test \
  pet_status_delivery_test pet_frame_presentation_test pet_resource_loader_test -j2
ctest --preset asan \
  -R '^(pet_bridge_lifecycle_test|pet_status_delivery_test|pet_frame_presentation_test|pet_resource_loader_test)$'
ctest --preset ubsan \
  -R '^(pet_bridge_lifecycle_test|pet_status_delivery_test|pet_frame_presentation_test|pet_resource_loader_test)$'
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan \
  -R '^(pet_bridge_lifecycle_test|pet_status_delivery_test|pet_frame_presentation_test|pet_resource_loader_test)$'
python3 tests/manual/pet_preview_acceptance.py \
  --build build/native-release \
  --output /tmp/mirage-m8-hidden-native-20261018-fifth \
  --exclusion-probe build/native-release/tests/pet_exclusion_test \
  --observe-exclusion
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check
python3 -m py_compile tests/manual/pet_preview_acceptance.py
git diff --check
```

默认 Python 已含 python-xlib/Pillow。本轮将外部测试 helper 的有界等待从 5 秒增至
12 秒，以在持有租约期间验证 owner 丢失；产品观察/关闭预算没有因此放宽。

## 验证结果

| 门禁 | 实际结果 |
| --- | --- |
| Release 全量构建及 CTest | 101/101；107.70 s |
| ASAN 专项（4 个业务测试 + 2 个隔离 fixture） | 6/6；6.63 s，无报告 |
| UBSAN 同上 | 6/6；6.53 s，无报告 |
| TSAN 同上 | 6/6；6.71 s，无报告 |
| 私有真实 pet/tray、本地 OCR 与生命周期 | 66/66 |
| 格式、公开头边界、Python 语法、diff 检查 | 通过；62 份公开头，0 新违规，两处 DEC-096 既有 seam 保留 |

`pet_status_delivery_test` 的 12 项检查覆盖部分剩余、截止边界、长时间滞留、重复
读取不延长时间、前移测试时钟、零剩余、无气泡与新请求替代。
`pet_bridge_lifecycle_test` 的 18 项检查编译**实际 Bridge 源码**，只替代窗口唤醒/
退出边界和资源 decoder；真实 Runtime/IPC、kairo timer/worker、LatestMailbox、
loader 取消与 future 消费保持在被测路径。验证暂停后低频控制仍运行、无新事件也能
恢复重读、运行中解码取消、owner 丢失、关闭幂等、关闭后无遗留 timer，以及启动失败
清理。测试进程模拟独立产品进程 owner，不作为同一产品进程多 Executor 的许可。

原生脚本验证整窗 shape 空、清图、CPU 下降、恢复后角色与拖动区重载、焦点/几何
保持，两次当前 OCR 文本、连续恢复动画帧、拖动、独立启停、崩溃恢复，并分别验证
最小化/仍映射但空 shape 两种隐藏状态的正常关闭和 owner 丢失。最后持有的租约
面对已消失窗口明确结算，未让新一代窗口出现或遗留受管进程。

最终夹具单核 CPU：可见采样 **18.99%**，shape 暂停 **1.67%**，恢复可见 **20.66%**；
最小化 **0.67%**。shape 暂停时正常关闭 **0.370 s**，owner 丢失退出 **5.456 s**；
最小化对应 **0.270 s / 5.184 s**。这是 Xvfb/软件渲染下的单次隔离观测，CPU 条件
仅作同机相对回归，不替代硬件功耗、GPU 或真实桌面性能验收。

原始目录、截图与日志在 `/tmp/mirage-m8-hidden-native-20261018-*`；仓库保留
[结构化结果](data/mira-pet-hidden-presentation-native-20261018.json)。

## 失败记录与修正

- Bridge 专项首次编译漏显式链接 persistence，因头路径缺失失败。补齐目标依赖后
  构建通过，未以 native 的间接头路径掩盖问题。
- first：14/15。帧清空但 CPU 25.99% → 25.00%，节能失败；移除常驻 onFrame。
- second：14/15。CPU 18.99% → 10.66%，仍未过同机减半门禁。核对 EUI 公开文档，
  移除隐藏时的 ImageStream 元素绑定，不修改依赖或伪造低负载数据。
- third：30/31。CPU 19.99% → 2.67% 已通过，但夹具在回复后立即读取拖动区，未等
  UI 采样恢复及资源加载。补充有界就绪等待；保留回复前 shape 恢复与焦点断言。
- fourth：34/35。拖动恢复通过，动画检查把排除前帧与重置后的帧相比，可能恰为同一
  帧。改为恢复后连续两帧比较并保留截图，继续要求可见动画确实变化；没有放宽动画门禁。
- fifth：最终 **66/66**，包含增加的 mapped-shape 两组退出检查。

## 保留的门禁

M8-06 保持 **In Progress**，本轮只关闭 X11 隔离条件下隐藏暂停的实现与回归。

| 未执行项 | 原因与补跑条件 | 负责人 |
| --- | --- | --- |
| 真实 compositor、整组气泡/Overlay 与完整输入/接管 | 需可控真实桌面、在线或脚本任务及同场景事件/视频，验证隐藏新帧和恢复瞬间；本轮只显示公开 OCR 文字 | Mirage 维护者 |
| 原生 EUI/GLFW/OpenGL sanitizer 实进程 | 三 sanitizer 覆盖实际 Bridge，但窗口边界为 fixture；需完整原生 sanitizer 构建及同一私有进程脚本 | Mirage 维护者 |
| 硬件功耗/GPU、物理 DPI/多屏/全屏与其他平台 | 软件 X server 的 CPU 不提供这些结论；按目标设备、显示器与 Wayland/Windows 独立实现补跑 | Mirage 维护者 |
| 自动迁移、几何版本、正式安装与许可 | 按 M8-06 后续几何优化和 M8-08 验收，当前仍为整窗策略 | Mirage 维护者 |
| 远端 CI 与 MR | 本轮本地提交，未 push；CI 目标已加入新测试，推送后执行远端门禁 | Mirage 维护者 |

## CU 真实桌面验收尝试（2026-10-18）

维护者要求通过 CU 验证真实桌面；当前代码为 `dcb9113`。调用
`cua.getState()` 得到 `apps: []`，仅有无打开标签的 MCP Apps 与应用内浏览器。
当前 CU 配置禁用原生 computer API，且 `cua.listWindows` 未提供。未取得原生窗口、
桌面截图或键鼠入口，不能确认当前桌面上的 Mirage 版本，也未执行避让、焦点、输入、
Overlay 或 compositor 验收。此前 66/66 仍仅为隔离 Xvfb 结果。

负责人：Mirage 维护者。补跑条件：为当前会话接通目标 Linux 原生桌面的 CU 连接，
能够枚举 Mirage/pet 和可控测试窗口并取得截图、执行输入。接通后核对运行版本，
使用公开测试内容验证覆盖目标、观察/输入期间排除、恢复与焦点、气泡/Overlay 共存
及人工接管，保留同场景视觉与事件证据。M8-06 保持 In Progress，真实桌面门禁未完成。
