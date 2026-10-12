# 桌面视觉与输入管线改进：守卫区域化、截图降耗、Wayland 区域捕获与观察节奏（2026-10-12）

> 状态：Completed（Linux 全量与 sanitizer 回归；真实 GNOME Wayland 在线验收与产品会话往返量化见边界）
> 负责人：Mirage 维护者
> 工作项：[M7-16](../plans/m7-visual-tools-and-input.md)、M7-17、M7-18、M7-19
> 依据：[DEC-083](../decisions/DEC-083-target-region-stability-reverification.md)（修订 DEC-072/077 逐像素承诺）、DEC-074/077

## 背景与动机

DeepSeek 验收会话（2026-10-12）暴露三类问题：

1. **像素守卫过敏感**：`desktop.session.click` 被 `target_pixels_changed` 拒绝仅因
   应用启动动画/通知滑入目标矩形边缘（DEC-077 的零容差逐字节比较）。模型被迫
   "拒绝→重截屏→重判"往返。
2. **全图截图消耗过大**：desktop PNG 是未压缩 stored-DEFLATE（4.8MB 为与内容无关的
   下限，base64 后 ~6.4MB/请求）；Wayland 只有 source=desktop。
3. **观察节奏缺乏纪律**：模型在确定步骤之间反复全图截屏。

参考业界 CUA 方案（OpenAI CUA 的按需观察与 wait、UI-TARS/Agent S2 的 SoM 与
粗到细区域裁剪、Mobile-Agent 系的变化检测门控），结合 pin 内已有的 mirador
`detect_change`/`patch_fingerprint` 原语与 AT-SPI 几何来源实施如下。

## 交付

### M7-16 守卫区域化（DEC-083）

`integration/mira/src/desktop_stability_guard.hpp`（新，header-only）：
`assess_target_stability` 分层判定——目标矩形外扩 8px 边距环内变化忽略；
`detect_change`（padded 裁剪）kNone 放行、kGlobal 拒绝；kPartial 与核心区相交时以
32×32 灰度指纹相似度 ≥0.98 判主体未变放行；任何错误/不可测/空裁剪 fail-closed
拒绝。接线 `desktop_shared_input.cpp`（Wayland 共享输入）与 `desktop_visual_tools.cpp`
（X11 编号指针，按 mark bounds 包围盒）。`desktop.vision.click_region` 保持逐像素语义
（保守参照实现，DEC-072）。

### M7-17 截图降耗

`desktop_png.cpp`：自适应逐行滤波（None/Sub/Up/Average/Paeth min-SAB）+ 自研确定性
DEFLATE（fixed Huffman + 32KiB 窗 LZ77，15-bit 哈希链 32 候选上限，取消检查有界）；
不可压缩内容回退 stored 块——输出绝不劣于旧编码器。实测 UI 类内容 4.8MB → KB 量级
（uniform 130x、gradient 113x、photoish 1.62x、噪声 1.00x 回退）。大 ROI（长边 >1600）
应用与 display/window 相同的预览缩放（`origin_/scale_` 元数据保持精确映射），
≤1600 保持 1:1。工具版本 1.4.0 → 1.5.0。

### M7-18 Wayland ROI 与窗口几何近似

`capture_desktop` 接受 `source=roi`（共享监视器像素 x/y/w/h）与 `source=window`
（source_id 经 `desktop.window.list`），从守卫/观察共用的 PipeWire 新鲜帧**本地裁剪**：
零授权增量、零额外流、每捕获仍只计一帧预算。窗口几何经新增的 AT-SPI WindowProvider
门面（`AtspiBackend::open_window_provider`，枚举 frame/window/dialog 的屏幕坐标 extents；
纯 Wayland 时 `LinuxDesktopEnvironment::window()` 回退到它）；与 X11 的 framebuffer
裁剪语义一致（含遮挡内容）。窗口矩形与共享监视器相交裁剪，空交集 fail-closed；
AT-SPI 不可用时 `window()` 为 null，`source=window` 显式
`UnsupportedCapability`。Portal 两个 schema 放开三源（observation + interactive）。
`shared_input` 坐标映射改为 `origin_x/y` + `desktop_per_image_*` 元数据驱动——
ROI/窗口供给的图像坐标经原点/比例还原到共享监视器像素后再进入守卫与派发。

### M7-19 观察节奏

`shared_input` 的 `target_pixels_changed` 拒绝现在把守卫已取得的新鲜帧直接发布为新
观察（提取的 `finish_native_publish`），canonical 回执新增可选
`refreshed_image_scope` 字段——模型下一跳免重截直接重试，消除"拒绝→重截→重试"
乒乓。`model_layer` 桌面指引追加 CUA 式观察纪律（结果不可预测的动作后捕获、
优先小 ROI/窗口裁剪定位、复用最新图、等待页面就绪）。

## 验证

| 验证 | 结果 | 证明范围 |
| --- | --- | --- |
| 编码器独立回读（开发自检） | uniform/gradient 位级一致（zlib 膨胀 + 独立 unfilter） | DEFLATE 位流与滤波正确性 |
| 压缩比 | uniform 130x / gradient 113x / photoish 1.62x / 噪声 1.00x（stored 回退，768,523 = 旧 stored 上界） | 消耗目标与最坏情形不劣化 |
| M7-17 独立解码器（扩展 fixed-Huffman + 五种滤波逆变换，与生产零共享逻辑） | desktop_png_decode_check：7 内容矩阵全像素一致，native-debug 与 ASAN 双跑；zeros 141.7x / gradient 103.3x / checkerboard 96.9x / 条纹 84.6x / 噪声 == stored 上界公式 | 压缩位流无损性与回退上界 |
| M7-17 大 ROI 预览 | 1920x1080 ROI → 1600x900 预览，origin/per-image/scale 元数据精确；≤1600 ROI 保持 1:1 | 降耗与坐标映射 |
| M7-18 Portal ROI/窗口 | 8 项 fake 场景全过：裁剪像素级正确（独立解码器验证）、部分相交裁剪、完全在外/未知窗口/无 window provider fail-closed、interaction 组合拒绝、schema 三源 + v1.5.0、`created==1 && captures==1` | 零授权增量、一帧预算、诚实失败 |
| M7-19 拒绝即发布 | changed_refresh 模式：守卫帧一帧发布、回执含可读 `refreshed_image_scope`、新 scope 免重截重试成功、回执 ≤512B | 拒绝→重试乒乓消除 |
| M7-16 稳定判定单元 | 20 checks 0 failures（native-debug 与 ASAN 双跑） | identical/漂移放行/结构替换拒绝/边距环/微噪声阈值/越界与非法输入 fail-closed/确定性 |
| M7-16 接线回归 | desktop_image_supply 891 checks（21 shared 模式 + native_region_sources 8 项）、visual_marks 743、local_visual_workflow 177、post_action_verification 578 + 真实 X11 任务 4/4、X11/gui 集 9/9 | 全部接线与 click_region 逐像素语义无回归 |
| 全量 native-debug | **76/76** | 完整回归 |
| ASAN/UBSAN 目标集 | desktop_image_supply / stability_guard / post_action_verification / png_decode_check / visual_marks / portal_screen 全过；native_agent_integration 单跑通过、组合负载间歇 | sanitizer 干净性 |

已知测试侧遗留：`native_agent_integration_test` 在 ASAN 并行负载下间歇失败——脚本化 provider throw 场景触发 pinned ModelGateway 熔断器（3 次连续失败 / 5s 冷却），预算路径场景与 throw 场景共用 RuntimeService 时熔断冷却与恢复语义竞速（本轮已加有界熔断探测循环缓解；独立 RuntimeService 隔离为后续测试重构项）。

精确命令（在仓库根执行）：

```bash
cmake --build --preset native-debug -j4
ctest --preset native-debug --output-on-failure          # 76/76
./build/native-debug/tests/desktop_image_supply_test      # 891 checks
./build/native-debug/tests/visual_marks_test              # 743 checks
./build/native-debug/tests/desktop_stability_guard_test   # 20 checks
./build/native-debug/tests/post_action_verification_test  # 578 checks + 真实 X11 4/4
cmake --build --preset asan -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --preset asan -R 'desktop_image_supply|desktop_stability_guard|post_action_verification|desktop_png_decode_check|visual_marks|portal_screen' --output-on-failure
```

## 边界与后续

- AT-SPI 窗口枚举依赖应用暴露 accessibility（GNOME 桌面普遍可用，非全部）；
  多显示器下窗口矩形与共享监视器取交集，跨屏窗口被裁剪或显式拒绝（不猜测偏移）。
- NanoTrack 跟踪本轮未接（相似度+空间分析已覆盖；移动主体按变化拒绝，保守正确）。
- JPEG 预览编码不在本轮（supply-chain 审核另议）。
- 真实 Wayland 会话（GNOME）的 ROI/窗口捕获在线验收、真实会话往返减少的量化
  对比，在目标平台环境另行执行。
