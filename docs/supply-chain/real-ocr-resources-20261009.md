# M7-06 OCR runtime 与实验模型资源审计

> 状态：装配审计完成；模型包未获发布验收
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 依据：[DEC-057](../decisions/DEC-057-real-ocr-composition.md)

## Runtime 锁定与消费

Mirador pin fb0dc3f 不变；按其 integrations/deps.lock.json 复用 ncnn
`e54f7b1f88434e1d844ea0551b880a1cfb079ce1`（20260526，BSD-3-Clause），
[源码](https://github.com/Tencent/ncnn/tree/e54f7b1f88434e1d844ea0551b880a1cfb079ce1)。
根 dependencies.lock.json 的 audit_only 记录消费条件和本审计；没有第二份独立 pin。
MIRAGE_WITH_PPOCR 默认 OFF，默认图不获取/链接 ncnn；ON 启用 pinned 参考 backend。
模型权重/字典始终显式路径输入，不下载、不进入 Git、不随产品发布。

本次 FetchContent 带出的 git 子模块 glslang fe88f421038e1bb0a25cd5c1b2dfe505db82d08f、
pybind11 d03662f0984f652b60e7ddce53d3868002275197 只存在构建树；Vulkan/Python 均关闭，
不进入本次编译/链接目标。发布 opt-in 产物时须带 ncnn BSD 通知和当前依赖完整 notices；
本轮没有创建发布包。Debug 未 strip service 文件：默认 117130824 字节，opt-in
248107408 字节；构建配置其他选项不同，这仅是本机文件大小记录，不是发布体积保证。

## 实验模型与字典

离线测试源：[nihui/ncnn-android-ppocrv5](https://github.com/nihui/ncnn-android-ppocrv5/tree/671ac4a72299a86ddee160131ba88fed748df425)，
精确 commit `671ac4a72299a86ddee160131ba88fed748df425`。使用其 mobile det/rec param/bin
和 ppocrv5_dict.h（18383 项，加末尾 ASCII 空格，CTC blank 留给 class 0）。
源仓库无顶层 LICENSE；JNI/字典头注明 BSD-3-Clause，二次转换权重的可分发许可证据
尚未闭合，因此仅为外部实验资源，不能把代码头许可当作权重分发结论。

原始 SHA-256：

- det param：358f459680ae0e7a73e477469e529ce116f68c629019ec7a0b6457d2d9117934
- det bin：857a96bc963725105b78a178dfcc3c0c3db1a7b9eef32244367b2cb105ccf60b
- rec param：f52a6586ac3338d8c350db0c9f3c55ff2ecd3763327f9bc7f0efc091f8a63e74
- rec bin：49d9907a55ba20fa6637f9f788f66ab00793bc8ce57a733a09dbe86b9a2e3db0
- 字典头：bb7397a57300fccbf86cf527ad199b12a0a2e3ccec8afc636af2f9ee695bbd11

[离线适配脚本](../../tests/manual/prepare_ppocrv5_reference.py) 不训练/下载：检测 param
新增输入归一化 Scale，输入/输出 blob 改为 x/out；识别输出移除无权重 Softmax（SPI
已从 logits 计算 confidence），reshape 为 classes×1×timesteps。训练权重保持，
det 仅前置 24 字节 affine 参数。模型原路径使用 RGB→BGR，固定居中黑边与原动态
宽度也有差异；彩色输入及这些预处理差异尚未验收，不把脚本产物称为可发布模型包。

适配后清单（实验资源一致性，尚未质量验收）：

| 资源 | 字节 | SHA-256 |
| --- | --- | --- |
| det_param | 24827 | `7e0aaccc1da4181ce793536f2bb6e8ee854204997974b6be4a26cce4769be2dc` |
| det_bin | 2357240 | `75acc15cc388e52c6278e7b77c9af1345292ad5405ee1c57f853dcfa3770b462` |
| rec_param | 20029 | `cc0165c63b9017f287e56c1c2073d57184fd8777053c6e14163f3d32637adfdf` |
| rec_bin | 8242276 | `49d9907a55ba20fa6637f9f788f66ab00793bc8ce57a733a09dbe86b9a2e3db0` |
| charset | 74014 | `e5f8ca61ba03d3a247d06b013119982fa6de2bd48a846018b67bca57ffc56de1` |

model_id=PP-OCRv5-mobile-ncnn-mirador-contract-v1；det_side=960、rec_width=320、
rec_height=48。manifest 文件 SHA-256
`0210022d7f85dc0f7e0f8caebf1b5e5a9af89db49c87c25ab877d3a39e9ce6a4`。
身份摘要含上述完整配置和资源声明；启动按每文件大小/摘要核对并加载已核验私有副本。
维护者必须先审核清单来源；自声明 SHA-256 只保证一致性，不能证明发布者可信。

## 验收门禁与下一步

[固定语料脚本](../../tests/manual/ppocr_quality_acceptance.py) 使用公开中英文文本与
项目 Noto Sans SC 字体，不含用户屏幕/凭据。真实 backend 精确匹配 0/4；
[缩放缺陷](../dependency_feedback/mirador.md#mirador-20261009-001) 已登记，未修改 pin。
实测失败、复现命令和成本/平台限制见[兼容性证据](../compatibility/real-ocr-composition-20261009.md)。

负责人 Mirage 维护者：取得上游缩放修复、授权升级后，重新审核模型色序/归一化/
padding/解码契约、许可和资源摘要，补跑四类真实样本及宿主 DPI/真实桌面变换，再决定
批准固定模型包和精确缓存策略。没有批准的权重随发布包分发，M7-06 不标 Completed。


## 窗口局部预采集配置（2026-10-10）

[M7-07 / DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)为 schema 1
增加可选 `"roi_capture": true`。缺省 false；字符串、整数、null 和 object 在启动
资源 staging 前拒绝。该配置参与 canonical revision，修改后须重启 owner；权重、
字典、尺寸与许可证状态不变。开启后显式 mark crop（含 capture_scope 默认窗口）
直接由 ScreenProvider 采集同范围像素，使用 Mirador 的 ROI→全局变换；无 crop
仍整屏。管线另提供 owner 显式 refresh_region 观察方法，不从模型参数改变固定
业务后条件的范围。配置示例是在上述完整、已审核清单顶层加入该布尔字段，不能
以单个开关代替资源摘要核验。实际 4K 小窗口取证见
[局部采集验收](../compatibility/regional-visual-capture-20261010.md)；整屏预算仍
8 MiB，没有缩放或失败后的整屏回退。

## 2026-10-11 默认包补充审计

DEC-068 / M7-13 沿用同一 nihui 源提交和已验转换哈希。发布方 PaddlePaddle 的
两个模型卡明确声明 Apache-2.0，pinned nihui README 明确给出 PaddleX official_models
来源及转换步骤；字典 BSD-3-Clause 单独归属，不再以字典许可证推断权重许可证。
本条为后来补齐的证据，不改变本文 2026-10-09 当时的未核实事实。固定资源与 notices
现可进入产品安装布局；详细来源、修改声明、锁文件和局限见
[默认视觉包审计](bundled-vision-20261011.md)。
