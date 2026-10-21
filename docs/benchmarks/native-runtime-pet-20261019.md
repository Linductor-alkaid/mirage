# Native、OCR 与 pet 性能取证

> 日期：2026-10-19
> 状态：Linux 私有环境验证完成；真实桌面/Windows 门禁开放
> 负责人：Mirage 维护者
> 工作项：M6-47 / M7-24 / M8-13
> 依据：[DEC-108](../decisions/DEC-108-rendering-and-exact-observation-reuse.md)
> 基线：`58a02e3` 加开始工作时已有的 `fix/observation-efficiency` 未提交改动

## 环境与方法

Linux 7.0.0-38-generic、x86_64、Intel Core Ultra 5 225H、GCC 13.3.0，
CMake `native-release`，EUI GLFW/OpenGL 与私有 Xvfb。未锁频/绑核；软件渲染
数据不能代表真实 GNOME、硬件 GPU、高 DPI 或 Windows。未调用远程模型、修改
模型配置或重启用户运行实例。pinned 依赖未修改。

输入基准复用 `native_input_performance_test`：1180×800、0/40 轮历史、64/4096/
16000 B 草稿、多段和单段、每 case 5 次预热及 60 次采样。固定同一字体资源目录，
前后程序交替顺序 AB/BA/AB；汇总三轮各自 p50/p95 的中位数。通过 `/usr/bin/time`
记录每个进程的最大 RSS，包含六个 case 的字体、布局、驱动及预热工作集，不把帧
缓存字节数当整进程内存。编译、sanitizer 和其他测试结束后才采样。

基线输入二进制在首次重建前保留为 `/tmp/mirage-native-input-before-20261019`；
复现时先保存基线，再对两版运行：

```bash
cd build/native-release/apps/native
/usr/bin/time -f '%M' -o <rss-kib> <benchmark-binary> [--unbroken]
```

pet 使用同一 `pet_preview_acceptance.py` 的真实 tray/pet 进程、RGBA framebuffer、
排除/恢复、拖动与退出场景。CPU 是 `/proc/<pid>/stat` 差值除墙钟，单核口径；RSS/
HWM 来自 `/proc/<pid>/status`。保留源分辨率、播放区间和帧数，未用降画质取得收益。

```bash
python3 tests/manual/pet_preview_acceptance.py --build build/native-release \
  --output out/performance-20261019/after/pet \
  --exclusion-probe build/native-release/tests/pet_exclusion_test
```

## 结果

### 输入整帧耗时

单位 ms，依次为三轮 p50/p95 的中位数。整帧包括输入事件、compose、update 与
render+glFinish，不能将总耗时写成单独的 GPU 绘制时间。

| 草稿 / 历史轮数 | 多段基线 | 多段优化 | 单段基线 | 单段优化 |
| --- | --- | --- | --- | --- |
| 64 B / 0 | 1.65 / 2.64 | 1.98 / 2.91 | 1.71 / 3.61 | 2.06 / 3.52 |
| 4096 B / 0 | 1.54 / 2.36 | 1.72 / 2.71 | 3.98 / 6.03 | 3.95 / 5.71 |
| 16000 B / 0 | 1.59 / 2.66 | 1.58 / 2.81 | 7.15 / 10.06 | 7.17 / 10.14 |
| 64 B / 40 | 15.45 / 25.81 | 3.96 / 7.13 | 14.95 / 20.86 | 5.10 / 6.35 |
| 4096 B / 40 | 15.42 / 21.91 | 4.49 / 6.74 | 16.21 / 23.48 | 6.51 / 8.77 |
| 16000 B / 40 | 16.18 / 21.71 | 4.19 / 6.74 | 20.18 / 27.11 | 9.62 / 13.31 |

收益集中在长历史：16 KB 多段 p50 降约 74%，单段降约 52%。无历史的短输入
绝对变化约 0.2–0.4 ms，部分变慢；没有证明该路径加速，不宣称所有 case 均改善。

| 40 轮 / 16 KB 阶段 | 多段基线 p50/p95 | 多段优化 p50/p95 | 单段基线 p50/p95 | 单段优化 p50/p95 |
| --- | --- | --- | --- | --- |
| compose | 8.46 / 11.77 | 1.14 / 2.11 | 11.61 / 17.13 | 5.81 / 7.52 |
| update | 4.31 / 6.27 | 0.57 / 1.16 | 4.27 / 6.05 | 0.92 / 1.54 |
| render+glFinish | 3.47 / 5.63 | 2.43 / 3.69 | 3.75 / 5.19 | 3.36 / 4.28 |

| 同进程六个 case 的峰值 RSS（三轮中位数） | 基线 | 优化 |
| --- | --- | --- |
| 多段 | 174044 KiB（169.96 MiB） | 166384 KiB（162.48 MiB），减少 4.4% |
| 单段 | 176472 KiB（172.34 MiB） | 169288 KiB（165.32 MiB），减少 4.1% |

### Pet

初次优化进程采样：可见待机动画 CPU 20.33% → 5.00%，RSS 171284 → 161000 KiB，
HWM 223200 → 161000 KiB；私有验收 57/57。测量后补充了失败时移除图像绑定与
拖动区、设备归属保护，正常播放策略不变；最终二进制采样另列在下表。

| 最终二进制，私有 X11 待机动画 | 基线 | 优化 |
| --- | --- | --- |
| 可见 CPU（单核口径） | 20.33% | 5.00% |
| 可见 RSS | 171284 KiB | 161644 KiB |
| 进程峰值 HWM | 223200 KiB（217.97 MiB） | 161644 KiB（157.86 MiB） |
| shape 暂停 CPU | 2.00% | 2.67% |
| 隐藏 CPU | 0.67% | 0.33% |
| 排除释放到恢复可见 | 9.05 ms | 6.15 ms |
| 隐藏正常关闭 | 0.417 s | 0.217 s |
| 隐藏 owner 失联退出 | 5.247 s | 5.231 s |
| 私有进程验收 | 57/57 | 57/57 |


CPU 和内存均为单机有限采样；HWM 包含启动解码与稳态，不等于每片帧缓存。
隐藏开销主要来自既有控制/IPC/平台检查，本轮未证明其显著改善。

### 后端本地推理

OCR 的复用对象是同一 `DesktopImageTurn` 内已授权 PNG 的精确摘要与 epoch/authority。
固定图像连续 41 次 OCR 调用仅实际分析一次；动作改变 epoch 后重新分析；ROI 变化、
失败缓存和撤权继续受检。收益是省去重复本地推理，不证明首次 OCR 变快，也不证明
远程模型首 Token、生成速率或在线任务耗时改善。

九套源 WebP 的逐帧测试独立运行完整 WebP decoder，将压紧帧还原为 512×512 后，
逐字节比较全部 RGBA（包括透明像素 RGB）。九态缓存从原先每帧 1 MiB 减少约
19%–53%；具体字节数见本轮原始 `pet-frames.log`。保留最多 96 MiB 的源帧上限，
解码器、当前/中性画布、GPU、字体与驱动须额外计入进程峰值。

## 验证与诊断记录

初次纹理测试错误地假定 EUI 的 shutdown/Runtime 析构会立即释放所有导入描述。
现有 UI/渲染缓存可以继续持有描述，因此实现已在 UI owner 的最终 onShutdown 中
显式删除原生纹理，描述不再绘制；测试覆盖借用保活、设备前回收、重复上传复用同一
纹理、hostile unpack state 和 RGBA 回读。静态 GPU 图像不会保持 Runtime 动画。

最终测试矩阵：

| 验证 | 结果 |
| --- | --- |
| native-release 完整构建 + 全量 CTest | 103/103，66.02 s |
| ASAN（detect_leaks=1、halt_on_error=1）定向 | 9/9，55.56 s |
| UBSAN（halt_on_error=1）定向 | 9/9，49.19 s |
| TSAN（setarch -R、halt_on_error=1）定向 | 6/6，42.24 s |
| 后补 cadence 回归，Release/ASAN/UBSAN/TSAN | 各 3/3（含 setup/cleanup）；正常慢帧、重复设定、静态停止、隐藏/恢复和 owner 退出 |
| 后补失败清图/设备归属，Release/ASAN/UBSAN | 各 3/3（含 fixture），验证借用、静态、清图像素及同纹理恢复 |
| format / boundary / git diff --check | 通过；62 个公开头、0 违例，保留 DEC-096 的两个 seam |
| 正常/最小 × 明/暗四张截图 | 与已有正常输入性能截图逐字节一致 |

ASAN/UBSAN 9 项包含 7 个业务测试及两个 fixture；TSAN 为 4 个业务测试及两个
fixture。最终完整 CTest 后只补充定时/失败呈现分支，相关 target 已定向重建和复验。

初次全量回归 102/103：旧 `pet_bubble_view_test` 直接将 packed storage 当完整
画布提交，已改为 `pixels()` 还原接口。失败不覆盖，最终结果另记。

最早从仓库根运行的 UI 诊断样本未找到相对字体资源，未纳入最终配对统计；截图已
在产品字体目录重跑。早期并行编译期间的输入样本同样不纳入最终性能数字。

TSAN 直接启动遇到宿主 `unexpected memory mapping`；按仓库既有方法用
`setarch x86_64 -R` 重跑。完整 PNG 矩阵在仪器化下曾触及原测试 30 秒期限；仅将
TSAN 的测试进程期限扩为 120 秒，生产采集、OCR 和取消期限未改。

## 产物、限制与补跑

原始条件、日志、配对输入统计、pet 指标及源/二进制摘要保留在
`out/performance-20261019/`（`paired/results.json`、`baseline/pet/`、`final/pet/`、
`build-identity.json`、`compare-input.py`），生成物不提交；由 Mirage 维护者保留到本轮评审与真实
桌面验收结束。真实 compositor/物理 DPI/硬件 GPU 与 Windows 尚未运行，负责人
为 Mirage 维护者；取得相应环境后用同样负载补测输入、播放、隐藏恢复与峰值内存。
本地 UI 缩减不使任意超长单条 Markdown 或所有场景自动满足 60 FPS。

工作项整体继续跟踪于[性能维护计划](../plans/maintenance-performance-20261019.md)。

## native-release 更新与启动验收

2026-10-19，按维护者要求重新构建 `native-release`，完整 CTest 再次通过
103/103（66.04 s）。更新前没有运行中的 Mirage tray、native 或 pet；通过
`build/native-release/apps/mirage start --shell <native-release>/apps/native/mirage-native
--tray <native-release>/apps/tray/mirage-tray` 启动本次构建。

`service status` 返回 `host status: running`，`product.control` 的只读状态返回
`frontend_ready: true` 与受管理的 frontend/pet PID。两扇 X11 窗口均为
`IsViewable`；tray、native、pet 的 `/proc/<pid>/exe` SHA-256 全部与当前
native-release 文件一致。桌面启动入口已指向此构建目录，无需修改。

命令日志与进程摘要保留于 `out/performance-20261019/update-build.log`、
`update-test.log`、`update-start.log`、`deployment-identity.json`。此次为真实桌面
启动冒烟验证；不替代上述真实 compositor、物理 DPI、硬件 GPU 性能与 Windows
门禁，未发起模型请求。前文“未重启用户运行实例”描述性能采样阶段，本次另行完成
维护者要求的启动更新。
