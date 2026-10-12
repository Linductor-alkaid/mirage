# 默认视觉运行包审计（2026-10-11）

> 状态：Active
> 决策：DEC-068；工作项 M7-13；负责人 Mirage 维护者

## 固定版本与来源

Mirador ae8cf87581c1ebe393a67374d9977a38354d0911 保持不变；ncnn 20260526 /
e54f7b1f88434e1d844ea0551b880a1cfb079ce1 由 Mirador integrations 交付，静态链接 CPU
OCR 后端，不另 pin 第二份 ncnn。资源来源 nihui/ncnn-android-ppocrv5
671ac4a72299a86ddee160131ba88fed748df425；五个源文件及五个产物的 SHA-256 和大小
见 [锁文件](../../packaging/vision/resources.lock.json)。模型大文件仅存在于构建缓存及
安装包中。Mirage 不修改 pinned 依赖，也不打包未验的 YOLO / NanoTrack 权重。

构建期仅获取精确 commit 下五个文件，逐个验证 SHA-256 / TLS；断网构建可指定
MIRAGE_VISION_SOURCE_DIR 使用同一锁定源目录。tools/prepare_vision_bundle.py 复用已验
检测归一化 affine、识别 logits / CHW 转换和字典末尾空格，转换后再次验证固定哈希。
默认清单启用现有 geometry_marks / roi_capture；训练参数与 2026-10-09 已验版本一致。
运行期不需要下载、Python、Paddle、ONNX、Android 或手工模型目录。

## 许可证证据

[PaddlePaddle 检测模型卡](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det) 和
[识别模型卡](https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec) 均由发布方明确声明
Apache-2.0（2026-10-11 核实）。[pinned 转换 README](https://raw.githubusercontent.com/nihui/ncnn-android-ppocrv5/671ac4a72299a86ddee160131ba88fed748df425/README.md)
列出从 PaddleX official_models 对应模型导出 ONNX 和转换 ncnn 的步骤；转换权重归属
据此关联至上述官方模型，未对上游导出做训练权重逐层数值证明。

字典头声明 Copyright (C) 2025 THL A29 Limited / BSD-3-Clause。模型权重的 Apache
许可依据是模型发布方声明，不来自字典代码许可。ncnn 自带 BSD-3-Clause，Mirador
自带 MIT。安装布局带 Apache / BSD / MIT 全文、NOTICE 修改与归属声明以及锁文件和
conversion.json。默认包源码流程公开可复现；最终发行仍遵守整个 Mirage / Mira 的
既有许可证与源码交付规则，此项不是创建 release 的授权。

## 实际验证

源损坏（每个文件）、转换哈希偏移均拒绝；离线重建逐字节一致，13 checks。普通配置
获取固定五文件也已成功（7.7 s）。默认包真实 OCR 四语料文字/位置 4/4。产品默认加载、
安装布局、显式关闭和坏包验证的最终结果见
[默认包与会话修复验收](../compatibility/bundled-vision-and-tool-recovery-20261011.md)。
