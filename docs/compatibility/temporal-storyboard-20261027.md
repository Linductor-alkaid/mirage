# 有界事件帧时间拼图验证

> 状态：Completed（下列本机契约验证；M9-24 保持 In Progress）
> 日期：2026-10-27（Asia/Shanghai）
> 负责人：Mirage 维护者
> 工作项：M9-24；关联 M9-23
> 决策：[DEC-120](../decisions/DEC-120-temporal-storyboard-observation.md)
> 基线：`d30502a` 之上的本地工作树，分支 `feat/continuous-semantic-binding`

## 范围与环境

Linux x86_64，GCC 13.3.0，CMake 3.28.3；Mira `68a8bf17`，Mirador `2a9f4830`。
Debug/sanitizer 构建的 PP-OCR/YOLO 关闭，native-release 两者开启。测试仅用合成
历史预览和脚本化环境，未操作用户桌面或调用在线模型；本轮不改 pinned 依赖。

新增 `desktop.vision.storyboard` 复用 2–4 张 feed 事件预览，320px 格位、最多
640×640、真实观察时间排序；保留源/时间/缺失信息。实际模型输入消费才 ACK，
已消费拼图不重复供图；源 epoch 或权限变化拒绝推理输入，不签发动作引用。
同时修正 OCR 提示优先复用 semantic_scene、媒体门控是启发式和视频内容证据边界。

## 命令和结果

```sh
cmake --build build/debug -j 4
ctest --test-dir build/debug -R 'perception|desktop_image_supply|desktop_atom_toolset|local_semantic_workflow|provider_contract|desktop_observation|atspi_scoped_snapshot|visual_scene|fake_visual_backend|continuous_capture|significance_gating|model_layer' --output-on-failure

cmake --build build/asan --target perception_storyboard_test desktop_image_supply_test -j 3
ctest --test-dir build/asan -R '^(perception_storyboard_test|desktop_image_supply_test)$' --output-on-failure
cmake --build build/ubsan --target perception_storyboard_test desktop_image_supply_test -j 3
ctest --test-dir build/ubsan -R '^(perception_storyboard_test|desktop_image_supply_test)$' --output-on-failure
cmake --build build/tsan --target perception_storyboard_test desktop_image_supply_test -j 4
setarch x86_64 -R ctest --test-dir build/tsan -R '^(perception_storyboard_test|desktop_image_supply_test)$' --output-on-failure

cmake --build build/native-release --target mirage-native -j 4
cmake --build build/native-release --target mirage-service -j 4
```

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| Debug 初轮工具/拼图专项 | 4/4，15.69s（含 setup/cleanup） | 新拼图和已有供图套件 |
| Debug 相关回归 | 17/17，27.79s（含 setup/cleanup） | 感知、快照、对象场景、供图 |
| ASAN 专项 | 4/4，22.53s（含 setup/cleanup） | 无 ASAN 报告 |
| UBSAN 专项 | 4/4，21.05s（含 setup/cleanup） | 无 UBSAN 报告 |
| TSAN 专项 | 4/4，56.54s（含 setup/cleanup） | 沿用既有 setarch；无 TSAN 报告 |
| native-release 前端与服务 | 两个目标构建退出 0 | 未运行 GUI/模型或生成安装包 |
| 公开头边界 | 67 头、0 违规，保留 DEC-096 两处既有缝 | 未新增公开第三方类型 |
| 本轮新增文件/已格式化修改点 | clang-format dry-run 通过 | 既有代码块不混入无关排版 |
| git diff --check | 通过 | 本地工作树 |
| 本地文档链接 | 325 处、0 缺失 | 本轮研究/决策/示例/验证及相关设计和计划 |

`perception_storyboard_test` 用四个不同颜色帧逐像素检查时间顺序、保比例缩小、
letterbox 和空格；验证 2/3/4 帧、容量、去重、无放大、过期/未来时间、源/尺寸
改变、无事件关联、时间顺序歧义和取消。供图套件验证注册、缺帧、单张模型输入、
文本/外部 owner 不消费、源切换拒绝、实际消费逐帧 ACK、重复供图抑制、纯文本
模式不暴露工具；这条工具路径环境 capture/input 计数均为 0。

开发期首次编译发现测试错用 Mirador 的 `.ok()` 风格及 string/c_str 类型；已修正。
首轮执行发现 schema 的 `uniqueItems` 不在 pinned Mira 的公开支持子集，导致工具
注册失败；移除该关键字，保留 handler 侧显式去重拒绝，后续上述测试通过。
这些失败轮次不计入通过结果。

全仓 `mirage-format-check` **未通过**：首个失败为 HEAD 已有
`apps/service/host.cpp:292–293` 格式；本轮没有修改该文件。没有为刷绿进行无关
格式化。`desktop_perception.cpp` 和已有 focus 测试的旧排版也保持原样；新增文件
和实际新增块按 clang-format 排版。此项由 Mirage 维护者在独立格式清理范围补跑。

## 验收限制

不证明真实视频理解、完整内容覆盖、token 下降、低端 CPU/RSS 或 10Hz tracking。
未执行 Windows、真实 Portal、在线模型、多图/拼图/ROI 配对实验；负责人 Mirage
维护者，待固定模型、可复位媒体任务和目标平台就绪后按 M9-12/M9-24 补跑。
没有音频或播放器时间输入。M9-23 的 OCR 分频、ObjectTracker、damage 和唤醒
仍未交付，不能以本轮打包工具替代这些要求。

本轮保留开始前的记忆/会话治理文档修改，不归为视觉实现；未提交、推送、安装
或重启产品。最新源码必须由新构建的前端/服务启动才生效。
