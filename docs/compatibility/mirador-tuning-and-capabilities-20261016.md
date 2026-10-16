# Mirador 产品参数调优与其他能力复核

> 状态：Completed（M7-21 产品参数、M7-22 本轮可用接口复核交付；保留未测模型范围）
> 日期：2026-10-16
> 负责人：Mirage 维护者；实施/视觉复核：Codex
> 依据：M7-20、DEC-060/068 的本轮修订；Mirador pinned 公开接口与 integration cards
> 基线：[第一轮逐图复核](mirador-visual-review-20261016.md)
> 数据：[参数、逐图结论、资源与能力原始结果](data/mirador-tuning-and-capabilities-20261016.json)

## 产品结果

产品默认 OCR 清单 `rec_width` 320→640，height=48、det_side=960、权重/字典和
预算不变；短行保持动态宽度。产品 mark 路径的线段参数设为 min_length=16、
deviation_tolerance=4，其余参数不变。参数由已有公开入口承载，未修改 pinned
依赖。显式自定义 OCR manifest 不被重写。构建树中的包与 mirage-service 已更新；
没有替换系统安装包或重启用户当前服务。

同一批 11 张真实采集/网页/视频图重新执行正式管线，逐像素采集一致 11/11，质量
从 **5/11→7/11**。这不是代表真实分布的准确率。

| 证据 | 调优前 | 产品调优后 |
| --- | --- | --- |
| 独立长行 | 5/8 精确文本 | 8/8 |
| 中英表单 | 21/21 | 21/21 |
| 暗色代码 | 12/17 | 16/17，`}`→`1` 仍错 |
| 四个矩形 | 2/4 | 4/4；额外三角是合法闭合候选 |
| 宿主正文几何碎片 | 5 | 0；OCR 6→10 区，正文仍有错漏 |
| 视频三帧/空白 | 各 4/4 文本 / 0 区域 | 保持通过 |

公共多语言页、Python 页、暗色代码及宿主正文仍 Fail；不能把宽度改善当作模型
全面正确。几何场景的 OCR 两行及加号本轮正确，但加号框仍偏大。新页面
`build/mirador-visual-review-20261016/review-tuned.html` 可切换原始默认、仅 OCR
宽度对照、产品最终参数，并展示跟踪正负例。旧基线图和结论没有覆盖。

## 参数取舍与消融

在 geometry/form-light/host-crop 三画面上跑 72 个“画面×参数”组合，调整 line
最短长度、合并距离、闭合端点距离和拟合残差。只增大合并/端点距离没有恢复漏掉的
轮廓；无模型 300×180 图上的 150×80、2px 空心矩形默认输出 0 线段，问题已经在
detector 阶段。residual=2.5 恢复该纯图，真实抗锯齿图需 4。

只放宽 residual、min_length=8 时复杂正文出现两次明确的 1024 线段预算拒绝；
没有提高容量或吞掉失败。组合 min_length=16/residual=4 恢复四矩形并减少文字
碎片；小于 16px 的线段更易丢失，不能宣称小图标全覆盖。圆/开放折线没有据此
新增检测承诺。方案和权衡已写入 DEC-060；线段复现引用 MIRADOR-20261016-001。

单进程观察（含模型装载/Executor，非稳定性能基准）：

| 场景 | width=320 wall / RSS | width=640 wall / RSS |
| --- | --- | --- |
| 表单 | 2.516s / 337876 KiB | 1.206s / 361632 KiB |
| 长行 | 0.885s / 337244 KiB | 1.325s / 372088 KiB |
| 宿主裁剪 | 0.750s / 270124 KiB | 1.021s / 302704 KiB |

RSS 增加约 23–34 MiB。wall 受装载/调度及同时构建影响，不据此宣称加速或满足
普遍 5 秒保证；宽度上限不是没有资源代价。已保留 `/usr/bin/time` 的单进程 max
RSS 与 wall 原始记录；没有单独扩大产品输出/缓存预算。

## 其他能力

以下使用实际表单像素，平移、局部修改和文字替换为独立已知变换。没有把 backend
空返回或 scripted 输出当作视觉识别。融合单元的文本/模板是明确已知证据，用于
验证融合与身份契约，不宣称再次独立识别了标签。

| 能力 | 实测结果 | 产品消费与限制 |
| --- | --- | --- |
| 变化检测 | 同图 kNone、平移 kPartial、换暗色场景 kGlobal；16×16 修改全图漏检，threshold=1 仍提前退出；75×75 局部裁剪可检出 | 粗筛可用，不能独立证明局部文本/目标不变；现有新 OCR/动作局部守卫保持原规则 |
| 真实 OCR 结果缓存 | backend calls 1→1→2→3→4；依次为首次、同像素新帧、Refresh、revision 变化、ROI 变化。命中返回完全相等结果；取消/超时无额外调用 | pinned session 可复用；产品 OCR 的 Refresh 是刻意的新鲜度策略，本轮没有擅自改成缓存复用 |
| VisualIndex | 保存 Save patch 同位/平移后精确层命中，空白替换无命中；1 byte 索引拒绝插入 | 需要先定位 patch，不是全屏模板扫描；不推断语义身份 |
| 融合 / 稳定 ID | OCR+template 合成一个 source_mask=18 区域；重复帧保留 ID；新场景的新证据换 generation，旧 generation 被拒绝 | 初次诊断错误复用了旧证据，修正为新场景证据后复验；没有将场景像素变化误说成自动身份换代 |
| 全局位移 | 真实 +12/-8 像素，64px thumbnail 估计 +15.625/-10.625，confidence≈0.607 | 符合缩略图量化，不能当精确点击坐标；本轮未接产品运动补偿 |
| Experimental ObjectTracker | 默认 PSR=5：原位/移动 NCC=1、PSR=4.580/4.759，但 outcome=None；PSR=4 对两正例 Strong，位移精确 +12/-8；空白和不同文字替换仍 None；取消拒绝 | 一类 UI patch 的参数对照；没有启用产品 tracker、没有降低通用安全阈值，也不算实验契约转正 |
| 语义 detector / NanoTrack 深度增强 | 本轮无已登记模型权重，未验 | 库有 SPI/参考 integration；不能用传统模板结果冒充真实模型质量。负责人维护者，补跑需指定模型、字典/类别、来源许可和预算 |

原位/平移模板索引都命中 exact，是正确裁剪到相同对象后的像素相等，不证明搜索
定位能力。tracker 的验证器只产生证据，没有在本轮伪造 Tracking/Uncertain/Lost
完整产品生命周期；目标移除/不同文字不绘制已确认标记。

## 上游反馈

已提交并回读 Open：[mirador #36](https://github.com/Linductor-alkaid/mirador/issues/36)。
反馈时 upstream master 与 pin 都为 `ae8cf87581c1ebe393a67374d9977a38354d0911`。
包含完整无模型 C++ 复现、72 组消融结论、现有参数解决部分和资源代价、细描边
回归矩阵、精细变化模式建议、OCR/PSR 校准方法及可验收清单。台账
[MIRADOR-20261016-001](../dependency_feedback/mirador.md#mirador-20261016-001桌面质量参数与精细变化门控建议)
为 Proposed：不将应用参数问题冒充缺少 API；显式禁止 dHash 提前返回属于待讨论
扩展，已知 ROI 的现有复核是可用边界。上游修改/升级仍须单独授权。

## 复现与验证

环境/工作树同 M7-20：Ubuntu 24.04.5、GCC13.3、HEAD dbd99110，有其他任务未提交
改动；期间其他工作将 HEAD 推进至 `aa7b39b3aed4b3d75025621c5412cac6cb4ad1a2`。
本轮没有创建 commit 或将他人改动纳入提交。脚本/小型摘要入库，图片/视频/模型在忽略的
build 产物目录。本轮新增 `mirador_capability_review.cpp` 与无模型 rectangle repro。
复跑 screen runner 会从独立 browser corpus 生成已知平移/修改/替换样本。

```bash
cmake --preset native-release
cmake --build --preset native-release --target mirador_visual_review mirador_capability_review mirador_outline_repro ppocr_pipeline_test visual_marks_test mirage-service -j 2
/usr/bin/python3 tests/manual/mirador_review_run.py --output build/mirador-visual-review-20261016 --binary build/native-release/tests/mirador_visual_review --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --results-dir tuned-geometry
build/native-release/tests/mirador_capability_review build/mirador-visual-review-20261016 build/mirador-visual-review-20261016/geometry-sweep geometry
build/native-release/tests/mirador_capability_review build/mirador-visual-review-20261016 build/mirador-visual-review-20261016/capabilities capabilities
build/native-release/tests/mirador_capability_review build/mirador-visual-review-20261016 build/mirador-visual-review-20261016/cache cache build/native-release/share/mirage/vision/ppocrv5-mobile-v1
build/native-release/tests/mirador_outline_repro
```

实际执行：产品全图 11/11 运行/像素通过；72 消融记录 70 成功/2 明确拒绝；真实
OCR 缓存和正负模板/跟踪 probe 成功。初始 probe 的 Result/字段调用编译错误已
修正；第一次扩展 residual 消融遇到预算错误后改为逐变体记录，未将中断当完成。
Release 产品服务构建通过；`ppocr_pipeline_test`、`visual_marks_test`、
`bundled_vision_resources_test` 三目标通过（含 fixture 5/5）。新增 2px 空心矩形
产品回归要求可选轮廓区域、负原点坐标及移除后的零标记，防止仅参数常量测试；
初次将 ink 外边界当作拟合线段边界导致断言失败，按公开 tight_bounds 端点约定
改为 2px 边缘/4px 尺寸容差，未改变算法结果；最终复验见计划记录。
ASAN/UBSAN 的三个视觉目标构建并通过（各含 fixture 5/5），OCR OFF，不代表真实
模型 sanitizer。HTML 实际浏览器检查参数切换/区域裁剪和页面错误；逐图查看标注。

未执行 Windows/多屏 DPI、语义 detector 和深度 tracker 模型、真实模型 sanitizer、
代表性性能/准确率基准。负责人 Mirage 维护者；相应平台/模型/校准语料具备后补跑。
这批结果没有关闭 M7-07/M7-10 的既有开放项。
