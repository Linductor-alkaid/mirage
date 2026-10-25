# Native Release 与 DEB 补充 YOLO 模型（2026-10-23）

> 状态：Completed（Linux amd64 构建、包校验与本地真实推理）；安装实测待验收
> 负责人：Mirage 维护者
> 工作项：M7-23 本机构建交付跟进
> 依据：DEC-097、DEC-068

维护者要求将已接入的 YOLO 补入当前构建。原 native-release 已编译 YOLO 后端，
但 `MIRAGE_YOLO_MODEL_DIR` 为空，视觉清单未包含 detector。本次沿用现有离线
装配接口，将本机 `/home/linductor/mirage-yolo-conversion/model-dir` 中的
OmniParser v2 icon 模型加入构建及 DEB。param/bin 的大小和 SHA-256 与
`packaging/vision/yolo.lock.json` 一致，不修改依赖、模型锁或运行时行为。

## 复现

```bash
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON \
  -DMIRAGE_WITH_YOLO=ON \
  -DMIRAGE_YOLO_MODEL_DIR=/home/linductor/mirage-yolo-conversion/model-dir
cmake --build --preset native-release -j4
cmake --build --preset native-release --target mirage-native-product mirador_visual_review -j4
LP_NUM_THREADS=2 ctest --preset native-release -j2
cpack --config build/native-release/CPackConfig.cmake -G DEB \
  -B /home/linductor/mirage/out/yolo-package-20261023
python3 out/yolo-package-20261023/verify_package.py
```

模型目录保存在当前构建树的 CMake cache 中，后续普通增量构建继续装配；新构建树
需重新显式指定目录。模型、日志和包保留在 Git 外，原始证据位于
`out/yolo-package-20261023/`。README 已补充新构建树的模型参数与锁文件要求。

## 验证范围

完整 Release 构建与产品聚合目标已完成。DEB 解包后，四个产品 ELF、18 项 UI
资源和 17 项视觉资源与构建源逐字节匹配，动态库依赖无缺失；五个 OCR 文件和两个
YOLO 文件的大小/摘要分别匹配其 manifest 与锁文件。模型来源说明、完整 AGPL-3.0
许可证和 YOLO 锁文件随包保留。控制脚本语法、desktop entry 与 CLI version 检查通过。

真实推理使用已有 `mirador_visual_review`，通过独立 Xvfb 中的 ScreenProvider
采集本地表单与空白图，进入产品 `PpOcrPipeline::mark`。本轮从现有
`mirador_review_corpus.cjs` 去掉公共网页抓取循环，仅生成本地 fixture；其余绘制
不变，派生脚本保留为 `out/yolo-package-20261023/local-corpus.cjs`。运行命令：

```bash
/usr/bin/python3 tests/manual/mirador_review_run.py \
  --output out/yolo-package-20261023 \
  --binary build/native-release/tests/mirador_visual_review \
  --manifest out/yolo-package-20261023/verify-checked/usr/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --samples form-light,blank --results-dir packaged-detector-smoke
```

构建清单及解包清单各自执行同一组样本，检查真实 detector 调用次数、采集像素一致、
编号与引用映射、可选锚点位于其区域内和空白负例。不以这些检查宣称通用检测质量、
控件可交互性或点击成功率。

## 交付结果

完整 Release CTest **120/120 通过**，0 skip，147.22 s。构建清单与解包清单各
2/2 场景通过：每图真实调用 YOLO 一次，采集像素一致；表单发布 13 个融合区域与
13 个编号，空白图为零区域。编号、引用和可选锚点检查通过。

交付包 `out/packages/mirage_0.1.0_amd64.deb` 为 125,997,490 字节，SHA-256：
`fc928ac34471214aadf8e0af8b847c355692db9a0eb26d28e0dc634591d14ede`。
相邻 `.deb.sha256` 验证通过。原 58,562,748 字节包备份在
`out/yolo-package-20261023/previous-56bf9be44734.deb`。
程序、资源、模型和测试摘要见[机器可读记录](data/yolo-native-package-20261023.json)。

本轮仅更新本机构建和交付包；未安装到系统、重启用户会话或执行宿主桌面操作。
Windows、真实 Wayland 输入、多屏/DPI 和安装升级实测未执行，负责人为 Mirage
维护者，在取得对应平台及安装验收环境后补跑。本次不修改并发路径，未另跑 sanitizer；
既有 ASAN/UBSAN 结果见 [YOLO 接入验收](yolo-mirage-integration-20261016.md)。
