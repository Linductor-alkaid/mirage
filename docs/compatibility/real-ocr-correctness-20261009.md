# M7-06 真实 OCR 字符与位置正确性复验

> 状态：公开语料字符与位置门禁 4/4 通过；PR #35 已合并、pin 已升级并全量复验；M7-06 Completed（Linux 首交付）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 依据：[DEC-057](../decisions/DEC-057-real-ocr-composition.md)、
> [首增量验证](real-ocr-composition-20261009.md)、
> [MIRADOR-20261009-001/002/003](../dependency_feedback/mirador.md)

## 背景

首增量四类公开语料 0/4 精确命中（`Submit task` 读出 `cY` 等），定位为
[MIRADOR-20261009-001](../dependency_feedback/mirador.md#mirador-20261009-001)
resize 权重缺陷并已提 [mirador#35](https://github.com/Linductor-alkaid/mirador/pull/35)。
本轮以修复分支构建复跑，发现并连带修复另外两处上游缺陷：连通域 `min_x/min_y`
从不更新（[002](../dependency_feedback/mirador.md#mirador-20261009-002)）与
PP-OCR 参考预处理偏离参考部署（[003](../dependency_feedback/mirador.md#mirador-20261009-003)）。

## 构建状态（重要边界）

本轮验证在**本地叠加态**执行：mirador 子模块 HEAD 仍为 pin `fb0dc3f`
（`dependencies.lock.json` 与 gitlink 未动），工作树叠加 mirador#35 三个提交
（`0527166`/`93c3bda`/`668459b`）的未提交内容。这不是授权升级；上游合并并授权
升级 pin 后须全量重建复跑。Mirage 侧 `tests/integration/ppocr_pipeline_test.cpp`
合成 rec 模型 kernel 同步为 16×16（匹配动态宽度契约），随 M7 在途工作树保留。

## 上游修复内容

1. `resize_area` 权重表按目标索引缓存（首提交 `0527166`，原 001 条目）。
2. 连通域洪泛填充逐像素维护 `min_x/min_y`（`93c3bda`）：种子只是最上行最左
   像素，低行更靠左时组件与文本框被截断——english 框从 72 起而非 24 即此因。
3. PP-OCR 预处理对齐参考部署 `ppocrv5.cpp`（`668459b`）：det 限长缩放（不放大）
   + /32 画布 pad 114 + 阈值 0.3/0.6 + BGR 平面序；rec 保纵横比缩放到高 48、
   行宽动态（上限 rec_width）、无填充；`x/127.5-1` 不变（ImageNet 已由转换图
   内 Scale 层承接，见 `tests/manual/prepare_ppocrv5_reference.py`）。

## 验收证据

[验收脚本](../../tests/manual/ppocr_quality_acceptance.py)新增位置门禁：解析
REGION 行，要求恰好 1 个文本区域且其框包含 PIL `textbbox` 渲染真值（容差 3px）。

| 样本 | 期望/实测 | 渲染真值框 | 检测框 | 置信度 | 文本 | 位置 |
| --- | --- | --- | --- | --- | --- | --- |
| english 480×180 / 32 px | Submit task | (28,67)-(206,93) | (24,60,186,40) | 0.992 | ✓ | ✓ |
| chinese 480×180 / 32 px | 提交任务 | (28,65)-(156,97) | (23,61,136,43) | 0.984 | ✓ | ✓ |
| small 480×180 / 16 px | 保存文件 | (28,60)-(92,76) | (25,56,70,25) | 0.985 | ✓ | ✓ |
| mixed-2x 960×360 / 2× | 提交 Submit | (56,124)-(326,172) | (48,114,290,68) | 0.971 | ✓ | ✓ |

脚本退出 0，report 在本轮 `/tmp` 输出目录（含样本/清单/字体 SHA-256）。资源与
字体来源同[首增量](real-ocr-composition-20261009.md)（同一份私有副本清单，
manifest SHA 记录于报告）。

```bash
python3 tests/manual/ppocr_quality_acceptance.py --binary build/ocr-debug/tests/ppocr_pipeline_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output <输出目录>
```

## 回归与覆盖

- mirador 独立 debug ctest 50/50（含新增顶行内缩组件回归，旧实现失败值
  `{8,2,2,8}` 与预测一致）；ppocr smoke 40 ok PASS（integrations opt-in）。
- Mirage ocr-debug 全量 ctest 55/56；ASAN+UBSAN `ppocr_pipeline_test`
  63 checks / 0 failure。唯一失败 `native_agent_integration_test` 是仓库内一处
  与本组无关的**既有在途未提交改动**（Messages 方言 fallback 选项数，测试期望
  1 项、实现返回 3 项），不属本验证范围，未处置。
- native-debug 56/57，唯一失败同上；resize 探针两模式退出 1/0 符合
  [复现说明](../dependency_feedback/repro/README.md#mirador-常量缩放复现m7-06)。
- 全部由 Independent-Verification-Agent 独立执行并回报证据。

## 剩余边界

- 2× 样本仍是像素缩放，不代表宿主 DPI 变换；样本为 Noto Sans SC 渲染而非真实
  桌面截屏；彩色 UI 文本的 BGR 序、多行/旋转文本、真实业务表单未验。
- OCR 权重分发的许可证据未闭合（[资源审计](../supply-chain/real-ocr-resources-20261009.md)），
  资源保持外部实验目录，不进入仓库或发布包。
- 未执行 Windows/Wayland 验收；上游 TSAN 不适用（单线程同步 API 预处理变更）。

## 升级后正式 pin 复验（同日）

PR #35 CI 全绿（14/14 检查，含 clang-format/clang-tidy 与 msvc/ndk 矩阵）后经
维护者授权合并（merge commit `ae8cf87`），工作分支删除；维护者授权升级 pin，
gitlink 与 [lock](../../dependencies.lock.json) 同步至 `ae8cf87`（v0.3.0-121）。
Independent-Verification-Agent 全量复验：native-debug/native-release 57/57、
ocr-debug 56/56；四语料 4/4（observed/bounds 与上表一致，置信度 0.971-0.992）；
ASAN+UBSAN 与 TSAN 各 63 checks / 0 failure；[resize 探针](../dependency_feedback/repro/README.md#mirador-常量缩放复现m7-06)
在正式 pin 归档上默认模式 exit 1、固定模式 exit 0；mirador 全树
clang-format 178 文件 0 违例。本轮叠加态与正式 pin 结果逐位一致。
M7-06 行验收条款（样本、坏权重/身份/预算/超时、缓存命中以 backend 调用计数
证明）全部闭合，工作项转 Completed（Linux 首交付）；上述剩余边界由 M7-07
（SoM/视觉点击）、M7-10（宿主 DPI/平台）与资源审计承接。
