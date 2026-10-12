# M7-06 真实 OCR 装配首增量验证

> 状态：装配验证通过；真实质量未通过；M7-06 In Progress
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 依据：[DEC-057](../decisions/DEC-057-real-ocr-composition.md)、[M7 计划](../plans/m7-visual-tools-and-input.md)

## 交付与边界

Runtime Service 接显式 --ocr-manifest 和可选 ncnn/PP-OCR 组合；既有视觉 session 的
blocking worker 承载推理，唯一 Executor owner 不变。启动核验本地资源并从私有副本
加载；退出和失败启动先收敛模型/视觉 worker，再关闭 Executor。工厂与 kairo 类型
仅在 integration/runtime 私有实现头，公开新增配置只有路径、callback 和 refresh 开关。

Agent 动态注册无参数 desktop.vision.ocr，截图权限、期限/取消、显示器/epoch 核验、
结果预算和错误明确；规范 ToolResult 包含非可信文字和 verified=false，普通过程只
保留来源/数量。失败规范结果进入宿主终态门禁，模型回复不能覆盖。IPC visual 请求
复用同一管线；没有 SoM、视觉点击、新 UI 或 MiraEnvironmentBinding capability 扩张。

图片预算 8 MiB、每源 session cache 4/16 MiB；清单/权重/文本限额见 DEC-057。
产品选择公开 Refresh，每次 capture 重新推理。原 session 缓存用 dHash；两个不同
均匀亮度图可能有同一 fingerprint，不能把近似复用当作文字没变。计数证明相同图
两次调用 backend 两次、变化图第三次；精确复用验收仍待后续方案。

## 已执行装配检查

Linux x86_64、GCC 13.3.0；Mira 698fc77、Mirador fb0dc3f、kairo ef821dc，pin 未再升级。

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| native-debug 全量 CTest | 57/57 | 默认 OCR OFF，原输入/图片/核验/UI 回归 |
| native-release 全量 CTest | 57/57 | 默认 OCR OFF，完整回归 |
| ocr-debug 全量 CTest | 56/56 | 原生前端 OFF，真实 ncnn 组合与 Runtime 装配 |
| opt-in ppocr_pipeline_test | 63 checks / 0 failure | tiny 合成网络，中文/英文 token 契约，不证明质量 |
| opt-in ASAN+UBSAN | 63 checks / 0 failure | 地址、泄漏、未定义行为；scalar ncnn 配置 |
| opt-in TSAN | 63 checks / 0 failure | 同一真实 backend/资源/服务场景，无 race 报告；scalar 配置 |
| 既有 TSAN visual_session_test | 214 checks / 0 failure | serialized worker、admission/异常/运行中取消/期限/stop |
| 既有 TSAN runtime_service_test | 541 checks / 0 failure | owner/IPC/取消/shutdown 回归 |

合成网络测试构造 32×32 det 和 32×16 rec，真实 ncnn load/forward + Mirador det→crop→rec
组合读出“中A”。检查坏权重大小/摘要、未知 model_revision（不能伪造缓存身份）、
清单超预算、取消/期限前不捕获、显示器几何变化拒绝并清空引用、stop/关闭后提交拒绝。
工具层验证权限拒绝、handler 异常、epoch 变化、框预算与过程脱敏。
Runtime IPC + scripted model 验证 OCR 可用性/规范结果全文、普通过程摘要、拒绝/采集
失败终态、关闭；合法 OCR 装配后无效模型配置启动失败也正确停 worker。
运行中取消/慢 backend 的六类通信路径沿用既有 session 测试，未声称 ncnn forward
可中途强制打断。没有 Windows/Wayland 实测或线上 OCR 模型调用。

执行命令（默认构建不获取 OCR runtime）：

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug
cmake --build --preset native-release -j3
ctest --preset native-release
cmake --preset ocr-debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset ocr-debug -j3
ctest --preset ocr-debug
```

联合 sanitizer 配置复用已下载的 pinned ncnn 源；NCNN_RUNTIME_CPU/AVX/AVX2/AVX512
关闭以减少 sanitizer 变体编译，不据此作生产性能声明：

```bash
cmake -S . -B build/ocr-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMIRAGE_WITH_PPOCR=ON -DMIRAGE_FETCH_DEPENDENCIES=OFF -DFETCHCONTENT_SOURCE_DIR_MIRADOR_NCNN="$PWD/build/ocr-debug/_deps/mirador_ncnn-src" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DNCNN_RUNTIME_CPU=OFF -DNCNN_AVX=OFF -DNCNN_AVX2=OFF -DNCNN_AVX512=OFF
cmake --build build/ocr-asan --target ppocr_pipeline_test -j3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/ocr-asan/tests/ppocr_pipeline_test
cmake -S . -B build/ocr-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMIRAGE_WITH_PPOCR=ON -DMIRAGE_FETCH_DEPENDENCIES=OFF -DFETCHCONTENT_SOURCE_DIR_MIRADOR_NCNN="$PWD/build/ocr-debug/_deps/mirador_ncnn-src" -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" -DNCNN_RUNTIME_CPU=OFF -DNCNN_AVX=OFF -DNCNN_AVX2=OFF -DNCNN_AVX512=OFF
cmake --build build/ocr-tsan --target ppocr_pipeline_test -j3
setarch x86_64 -R build/ocr-tsan/tests/ppocr_pipeline_test
setarch x86_64 -R build/tsan/tests/visual_session_test
setarch x86_64 -R build/tsan/tests/runtime_service_test
```

## 真实模型质量：失败证据

[资源审计](../supply-chain/real-ocr-resources-20261009.md)记录精确来源/转换/摘要和许可
未闭合项。模型/字典约 10.7 MB，资源仍在 /tmp 外部实验目录，未进入仓库或发布包。
固定文本由项目 Noto Sans SC（OFL）绘制到公开测试 PPM，通过 fake Screen Provider
运输像素，检测与识别均为真实 PP-OCRv5/ncnn；这不是实际 X11/Windows/Wayland 采屏。

| 样本 | 期望 | 实际 | 精确命中 |
| --- | --- | --- | --- |
| 英文 480×180 / 32 px | Submit task | cY | 否 |
| 中文 480×180 / 32 px | 提交任务 | calwb | 否 |
| 小字 480×180 / 16 px | 保存文件 | cw | 否 |
| 中英混合 960×360 / 2×缩放 | 提交 Submit | alsubnitwb | 否 |

0/4，脚本退出 1；[原始报告](data/real-ocr-quality-20261009.json)含样本/字体/清单 SHA-256。2×只是像素缩放，不证明宿主 DPI
坐标变换。每个分析请求完成不等于文字正确，不能据此把 M7-06 标为完成。

```bash
python3 tests/manual/prepare_ppocrv5_reference.py --source /tmp/mirage-ppocrv5-source-20261009 --output /tmp/mirage-ppocrv5-resources-logits-20261009
python3 tests/manual/ppocr_quality_acceptance.py --binary build/ocr-debug/tests/ppocr_pipeline_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-ocr-quality-final-20261009
```

source 必须是审计的精确 commit，脚本不获取权重/训练；测试器输出只用于这些公开样本。
复验须重新获取合法资源、核验摘要，再运行，不能依赖临时目录永远存在。

公开 resize_area 的全白 2×1→3×1 复现输出 128/255/255；应恒为 255。
同一裁剪原始 ncnn 读出 Submit task，Mirador letterbox 后读出 cY。
[MIRADOR-20261009-001](../dependency_feedback/mirador.md#mirador-20261009-001) Open，
本地台账已登记，未提交上游、未修改 pinned 代码。居中黑边、色序/归一化和原动态宽度
也需要兼容验收，不能声称缩放修复必然解决全部模型质量问题。

## 后续与验收责任

M7-06 保持 In Progress。维护者取得上游修复/授权升级后，补跑公开缩放探针与
Mirador image/letterbox 测试、资源许可/转换审核、四类真实语料/实际桌面 DPI、精确
缓存策略及 Mirage 全量/六类生命周期检查。之后再推进 M7-07 的 SoM/视觉点击；
当前不承诺 OCR 质量、发布体积、实时性、任意模型内存上限或跨平台保证。
