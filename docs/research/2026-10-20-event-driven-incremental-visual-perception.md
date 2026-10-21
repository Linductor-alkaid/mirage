# 低资源连续感知：增量视觉状态与按需语义理解

> 状态：Completed（原始资料与 pinned API 核对；非实现或性能验收）
> 日期：2026-10-20
> 负责人：Mirage 维护者
> 关联：DEC-091、DEC-109；M9-05/06/07/08/11/12/13/18

## 结论与证据边界

按维护者要求，将“轻量级增量视觉状态 + 事件驱动语义理解 + 按需 VLM 推理”补入
[DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)。
高频低成本观测维持状态，变化驱动结构更新，任务相关事件或歧义触发低频语义推理。
静态画面、动画和重复目标不必反复进入模型；必要的关键文字重验、外围巡检和
输入守卫仍保留。这里是工程选择，研究没有直接证明 Mirage 的端到端资源收益。

本轮回读下列作者论文/官方仓库，不下载模型、不训练、不执行推理。公开基准的
GUI/移动端或多模态成绩不能外推 Mirage 低端 Linux/Windows 桌面体验；量化支持
也不能直接证明内存、延迟和 grounding 质量可接受。

## 研究事实与工程借鉴

| 原始来源 | 已核对的研究事实 | Mirage 借鉴与限制 |
| --- | --- | --- |
| [UI-Hawk，EMNLP 2025](https://aclanthology.org/2025.emnlp-main.920/) | 历史感知视觉编码器处理屏幕序列，强调当前截图及文字动作历史以外的历史屏幕信息 | 用有界事件与关键证据窗口保留时间上下文；不引入其完整编码器/训练方案，不将移动端结果当桌面验证 |
| [ShowUI 论文](https://arxiv.org/abs/2411.17465)、[作者仓库](https://github.com/showlab/ShowUI) | UI 连通图引导视觉 token 选择以减少冗余，并交错组织视觉、语言与动作历史 | 借鉴结构与相关性驱动的输入压缩；其 token 选择在模型内，不能等同 Mirador 对象快照、增量传输或现成的跨帧状态协议 |
| [GUI-Eyes 论文](https://arxiv.org/abs/2601.09770) | 经强化学习学习是否及如何调用裁剪/缩放工具，以粗探索和精定位分阶段主动感知 | Mira 根据任务选择区域与观察升级；不要求引入 RL 训练、奖励函数或原模型，不能据 grounding 结果保证事件检测质量 |
| [MiniCPM-o 4.5 技术报告](https://arxiv.org/html/2604.27393v1) | 视觉 Resampler 压缩特征 token；Omni-Flow 以时分复用思路组织同一时间窗的输入与输出 | 参考紧凑视觉表示、时间窗分组与有界交错推进；不引入 MiniCPM 模型或将其列为后端选型 |

视觉压缩另核对 [MiniCPM-V 4.5 官方归档](https://github.com/OpenBMB/MiniCPM-V/blob/main/docs/minicpm_v4dot5_en.md)：
其中 3D-Resampler 对多帧联合压缩属于模型侧 token 机制。不能把该机制的压缩率
直接归为 MiniCPM-o 4.5 当前运行模式的承诺，更不能换算成 Mirage 采集/拷贝、
CPU 或 RSS 的节省量。可借鉴压缩时间窗口信息的思路，优先在模型前去除无关
帧/区域，状态层仍保留真实时间、覆盖和缺失信息。

## MiniCPM-o 4.5 专项核对

2026-10-20 按维护者澄清补读技术报告 §2、§3.1～3.3，以及官方模型仓库的配置与
实现。此前将其列为本地后端候选超出了本次意图，现撤回；本节只讨论机制参考。

### 视觉 token 压缩：模型内的学习式重采样

[报告 §2](https://arxiv.org/html/2604.27393v1#S2) 明确每个视觉切片经 SigLIP 编码后
由 Resampler 将 1024 个特征 token 压到 64 个，即 16 倍 token 压缩。
该数字针对切片特征，不是整段视频或桌面链路的资源压缩比。

源码复核固定于官方模型仓库 revision `d59117d19ddb8a28a62f1b5f4e3b34ac48d510a4`：

- [config.json](https://huggingface.co/openbmb/MiniCPM-o-4_5/blob/d59117d19ddb8a28a62f1b5f4e3b34ac48d510a4/config.json)
  的 `query_num=64`、`patch_size=14` 与紧凑视觉表示一致。
- [modeling_minicpmo.py](https://huggingface.co/openbmb/MiniCPM-o-4_5/blob/d59117d19ddb8a28a62f1b5f4e3b34ac48d510a4/modeling_minicpmo.py)
  中 `get_vision_embedding` 先调用视觉编码器，再调用 `self.resampler`；
  `Resampler` 使用可学习 query、二维位置编码及 cross-attention，输出固定数量
  embedding。高分辨率多个切片分别占用表示预算，不是任意整屏恒为 64 token。

工程推论：压缩应在送入高成本推理前减少冗余表示，但模型内 embedding 压缩
依赖编码器、权重及下游 LLM 的联合兼容性。不能在 Mirador 随意生成 64 个向量，
再送给任意 VLM；只裁剪 ROI 或生成对象差分也不能宣称已经实现同一种 Resampler。
本轮可落地的参考是：Mirador 复用未变证据、筛选相关区域，Mirage 对送模的概览、
ROI、关键帧及结构状态分别设预算；Mira 复用模型已有的视觉输入能力。若将来研究
真正的特征 token 重采样，应在相应模型适配/模型能力层单独立项，不能扩张本次
文档增量为引入 MiniCPM 权重、编码器或训练流程。

### 时分复用：时间对齐、显式分组和交错推进

[报告 §3.1～3.3](https://arxiv.org/html/2604.27393v1#S3.SS1) 的 Omni-Flow 将同一
时间窗的视觉、音频和输出组成 `g_k=[v_k; a_k; o_k]`，再按时间串行连接；每窗先
消费新输入，再生成输出，无输出时可为 listen。它是模型序列组织和训练机制，
不是 OS 并行线程或普通 timer 的别名。报告消融中 1 秒窗优于 0.2/0.1 秒，显式
边界及独立的听/说控制有益；这是原实验条件下的取舍，不设为 Mirage 默认频率。

上述固定 revision 的 `MiniCPMODuplex.streaming_prefill/streaming_generate`
以 `<unit>` / `</unit>` 组织输入和生成，并有 listen/speak 控制 token，佐证
“分组—先接收新信息—再输出”的组织方式。此处只静态阅读，没有导入执行源码。

工程推论：Mirage 可借鉴时间窗组织方式，将捕获区间、已提交状态基线、实体变化、
动作回执及反馈水位归入显式有界的时间单元，由 Mira 在需要时消费。时间单元内
仍保留各项真实时间、缺帧/覆盖和版本，不把异步到达的旧结果强行对齐成同时事实。
观察持续更新，模型有界推进；“是否升级理解”与“理解什么”分开，普通变化合并
到下个可消费单元，屏障立即处理。窗长由任务延迟与资源预算验证，不机械照搬。

此映射只参考 TDM 的分组/交错原则。普通请求式 VLM 不因此获得生成期间注入
新视觉 token 的能力；没有公开流式接口时保留下轮供给与版本重验，不能声称已
实现 Omni-Flow 全双工。调度继续使用 kairo，不新增私有调度器或固定周期模型轮询。

## pinned 能力与职责映射

核对 Mira `a29374a`、Mirador `cb45a102` 的公开文档/头文件及各自集成卡片。

| 责任方 | 现有可复用能力 | 本次拟补契约 |
| --- | --- | --- |
| Mirador | `PerceptionSession::analyze_change/run_ocr/run_detector/fuse/latest_snapshot`、ROI/cache、融合与会话内 stable ID、Experimental ObjectTracker | 确认/发布双基线下的跨帧实体与几何关系更新、带时间/覆盖的事件生命周期和选择性更新；不声称完整协议已交付 |
| Mira | 环境 `ObservationRequest` 的组件/年龄要求、Observation 的 provenance/时间窗/同步质量、现有任务与模型控制面 | 任务焦点、重要性及观察策略由任务层表达，Adapter 校验接入；完整注意协议待冻结 |
| Mirage | VisualScene 单 owner 增量对象与单标签绑定、授权采集/动作水位及既有观察通路 | 采集/识别/发布分频，kairo 调度，事件日志预算、模型升级去重及反馈关联；不重写通用视觉算法 |

核对入口：

- [Mirador session 公共头](../../third_party/mirador/include/mirador/perception_session.hpp)、
  [fusion](../../third_party/mirador/include/mirador/fusion.hpp)、
  [ObjectTracker](../../third_party/mirador/include/mirador/object_tracker.hpp)、
  [融合与快照卡片](../../third_party/mirador/docs/skill/mirador-integration/references/fusion-and-snapshots.md)。
- [Mira Observation API](../../third_party/mira/docs/api/environment-observation.md)。
- [kairo 通信卡片](../../third_party/mira/third_party/kairo/docs/skill/kairo-integration/references/communication.md)：
  latest 配置/帧、完整快照、逐条事件按各自语义选择公开原语；回调/CommStats 需接入现有诊断。
- [当前 VisualScene 契约](../design/visual-scene-incremental-state.md)：目前不保留事件历史，
  未接 native/IPC/连续帧；不能把拟补契约写成现有状态层 API。

`analyze_change` 比较上一次分析输入，并不证明 OCR 已提交；`fuse` 的 stable ID
也仅限会话内。现有融合提供几何关联，不证明已具备完整关系图的跨帧生命周期。
连续协调诉求沿用
[MIRADOR-20261019-001](../dependency_feedback/mirador.md#mirador-20261019-001)，
不因文档需求直接修改 pin、另建跟踪算法或预设上游类名。

## 交付顺序与验证

### Mirage 当前实现与首要优化

2026-10-20 按端侧低负载定位核对自研代码，区分现成设施与未接通的节省路径：

| 仓库入口 | 当前事实 | 对本次方案的约束 |
| --- | --- | --- |
| [VisualScene](../../desktop/observation/include/mirage/desktop/visual_scene.hpp) | 无像素、同步单 owner；默认 256 对象/512 KiB 逻辑存量，已有增量/单标签绑定 | 在此基础上接状态，不另起语义引擎；逻辑预算不等于进程 RSS |
| [VisualSessionHost](../../integration/mirador/src/visual_session_host.cpp) | 已有 kairo worker/LatestMailbox；变化分析后仍分别按请求开关调用 OCR/detector | 首先接通昂贵阶段的实际门控；不能把有变化报告当作已省识别，缓存命中须用 backend 调用数证明 |
| [视觉 pipeline](../../integration/mirador/src/visual_observation_pipeline.cpp)、[PP-OCR 装配](../../integration/mirador/src/ppocr_pipeline.cpp) | 请求配置控制 OCR/检测；产品 OCR 设 Refresh，detector 也用 Refresh；PP-OCR 和配置的 YOLO 在 pipeline 创建时加载 | 现有正确性策略不能靠粗指纹替代；须先形成可信 ROI/版本/年龄规则，再减少刷新，不将“按需调用”写作“按需加载/卸载” |
| [DesktopImageTurn](../../integration/mira/src/desktop_image_supply.hpp)、[ModelLayer](../../integration/mira/src/model_layer.cpp) | 已有帧/图像预算、artifact 及任务模型 input supplier/provider 路径 | 复用供给和任务推理，优先减少重复供图与 PNG/像素工作，不新增常驻编码器或第二个理解请求 |

这些结论来自静态调用链核对，未运行性能测试。落实低负载时，MiniCPM 的借鉴应
收敛为两项：在现有视觉/供给路径中去冗余，按已有事件时间与版本在消费时分组。
后者不要求固定周期构建时间单元，更不要求每个时间窗都调用模型。没有观察需求
时停止生产；有任务时按变化、证据年龄及动作需要分配工作，不把持续感知变成
OCR/YOLO/tracker 全开。空间关系只维护任务需要的局部依赖，避免新增全量关系图。

首阶段先证明无推理 GPU 的感知路径可用；常驻内存、CPU/唤醒、峰值与用户应用
响应共同受[资源门禁](../design/m9-resource-and-progress-policy.md)约束。无消费者、
静态等待、局部变化和突发过载分别与现有按需观察配对，证明本地负担与任务收益
的净变化，不能以模型 token 减少推定低负载产品目标已经达到。

### 后续顺序

先交付增量基线与资源上限（M9-05/06/07），再验证稳定身份与视觉事件（08/18）；
任务注意、对象语义绑定和按需 VLM（13/18）在此基础上扩展，已有基础绑定可并行。
当前快照及有界日志服务本地上下文；跨会话长期视觉记忆需另行冻结保留、预算及
失效策略，不作为当前 M9 的常驻存储。

M9-11/12 记录静态/动画/滚动/相似对象/遮挡/弹窗/跳帧/迟到结果下的实际调用数、
ID 交换、漏事件、事件延迟、完成率及资源；日志淘汰与源重置须可见，VLM 去重、
取消、视觉表示预算、时间单元合并及输出版本重验分别测试。预算与阈值在对照前登记，不能只凭返回条目或 token
减少证明收益。未执行上述运行、sanitizer、低端设备与模型验证；负责人 Mirage
维护者，在对应接线、目标设备/模型与授权桌面就绪后由 M9-12 补跑。
