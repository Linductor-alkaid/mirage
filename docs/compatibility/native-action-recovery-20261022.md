# 原生动作恢复与跨工具围栏验证

> 状态：Completed（本轮源码与 Linux 受控验证；实机门禁见文末）
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M7-18/25/26、M10-07、M9-05/18
> 基线：8df4498；本轮自研工作树，依赖指针不变

## 范围与依据

最新用户会话仅原地分析，不复制用户输入、页面、截图或 shell 输出到仓库。
故障摘要：AT-SPI 已枚举窗口无法转语义快照；枚举后错误暴露激活能力；
输入迟到导致共享关闭；不确定桌面动作拦截后仍可通过 exec 派发同类副作用。
依据为 DEC-056、DEC-081、DEC-095/110 当日修订及
[原生连续感知接线](../design/native-continuous-perception-wiring.md)。

## 实现边界

- 纯 Wayland 将同一个 AT-SPI WindowProvider 传给 Accessibility Backend；
  加法 activation_supported 能力同时约束 Workflow/Harness 注册和模型能力事实。
  v1 标题映射在枚举出现同标题多个 ID 时拒绝快照，不任取首个窗口。
- exec/write/edit 在宿主 decorator 复用执行围栏；无命令词过滤/白名单。
  只读观察、read、核验及紧急释放保留。普通业务条件不足不触发执行围栏。
- Portal 保留 250 ms 迟到守卫；失败增加阶段、样本、计划/实际时间、迟到量、
  最大通知耗时及通知次数。核验/过程详情保留有界 input_diagnostic。
- 仅 input_lag 且确认输入释放时保留同一流的只读观察，backend 和 Adapter
  均禁止后续输入。capture/OCR 的 input_actionable=false；新帧不能解围栏。
  传输/释放失败、撤权、其他不明确失败继续关闭流，不自动重新授权。
- M9 仍是按需 OCR/state 消费者，不开启后台采样或额外线程/定时器。

## 方法

Linux x86_64、GCC、C++20、CMake/Ninja；Debug、ASan、UBSan、TSan 与
native-release 分开记录。沿用 kairo submit_auto/future、既有阻塞 worker、
同任务取消/关闭与隔离 fixture；不修改 pinned 依赖。TSan 仅测试进程
setarch x86_64 -R，沿用现有 GLib suppression。

定向构建目标：atspi_backend_test、portal_input_test、desktop_image_supply_test、
harness_desktop_tools_test、desktop_atom_toolset_test；产品额外构建
mirage-native-product、ppocr_pipeline_test、native_agent_integration_test。

受控回归覆盖：环境构造后枚举到语义树；两处工具目录能力；迟到前/后输入边界；
执行不确定、释放失败、跨轮撤权/接管、无副作用拒绝、正常完成、外来 task、取消/期限；
正常 exec 到达 Provider，对照迟到后 exec/write/edit 均拒绝且 exec Provider
调用为零；同一授权流仍能截图、text_only OCR 和无帧 state 查询，任务仍 failed。
受控模型使用合成帧与文本，不调用外部模型或浏览器。

## 实施中发现与修正

首轮编译暴露新增诊断局部变量 shadow 与测试 aggregate 缺 display 初始化；
均在自研代码/夹具内修正。新端到端夹具最初放在 Executor shutdown 后，
继而误用 registry 的 canonical digest 填 provider ToolCallOutput；按 pinned
公开模型契约改回序列化 payload digest。能力目录负向测试进一步发现
Harness 注册入口遗漏，已连同 Workflow 入口补齐。收尾直接 Adapter 围栏
负向夹具需按 Mira JSON 公共 API 复制 Object 后移除策略字段，不能修改 const
视图；编译检查已修正。Debug 首轮全量 109/110，visual_marks_test 指出提前撤权返回绕过规范动作
拒绝回执，导致空记录被视为已结算。已将公共 owner/取消/执行围栏准入与基础
副作用工具的 authority 检查分开：桌面动作仍交 Provider 产生可结算的规范拒绝。
最终按该负向用例补入 sanitizer/release 回归。失败日志保留，不记为通过。

最后审查补充同标题多个 ID 的双向负向回归，避免新接线把第二个窗口映射
到第一个窗口的树；平台专项单列补跑。

## 验证结果

- Debug 全量 **110/110，341.06 s**；此前首轮 109/110 的失败原因与修正见上节。
- ASan/UBSan/TSan 定向各 **10/10**，分别 224.15 / 214.68 / 416.18 s。
  覆盖窗口、输入、跨工具围栏、图像供给、视觉动作结算、Portal 和 Provider 契约；
  计数含隔离 setup/cleanup。UBSan 最终运行设置 halt_on_error=1，TSan
  仅测试进程 setarch -R，未增加 suppression。无 sanitizer/race 报告。
- native-release 产品重建，定向 **16/16，117.70 s**；含真实 ncnn 的受控
  PP-OCR、native agent、M9 producer/consumer、预算与视觉观察、基础工具及 Portal。
- 同名守卫追加后，Debug/ASan/UBSan/TSan/native-release 的
  atspi_backend_test + observation_e2e_test 各 **4/4**（含隔离夹具）；
  产品再次重建。同名两个 ID 都拒绝，唯一标题的语义树正向路径继续通过。
- 最终宿主只读探针：窗口枚举成功 7 个、不支持激活，第三个候选快照成功
  （463 节点）；仅保存计数。Firefox Snap 的 AT-SPI Cache 访问仍有 AppArmor
  限制，未把此探针当成浏览器完整控件树或交互验收。
- 五个产品可执行文件可执行且动态依赖全部解析；SHA-256 见机器摘要。
- 65 个公开头边界检查零违规，保留 DEC-096 两条既有 seam；18 个变更 C++
  文件的修改区段格式、Markdown 本地链接、git diff --check 通过。全仓格式
  仍有基线 output-limit 换行问题（model_layer.cpp / protocol.cpp），未记为通过
  或混入无关格式修改；已验证 HEAD 的同一问题。

准确构建/CTest 参数、各配置日志路径、依赖 pin、环境和二进制摘要见
[机器记录](data/native-action-recovery-20261022.json)。Debug 全量命令为
cmake --build --preset debug -j 4 与 ctest --preset debug --output-on-failure；
原始日志前缀 /tmp/mirage-recovery-。同名收尾的每个 preset 构建
atspi_backend_test/observation_e2e_test，native-release 额外构建
mirage-native-product；CTest 使用 ^(atspi_backend_test|observation_e2e_test)$。

同流保留成功路径使用合成 DesktopCaptureSession，迟到后的 OCR/state 为受控
callback；Portal fixture 覆盖既有授权、拒绝、取消与流契约，不等于真实 compositor
恢复验收。产品 PP-OCR/ncnn 的合成图像回归单列记录，不能替代真实桌面识别质量。

## 未执行与限制

真实 compositor 中本次 input_lag 的根因、真实模型新会话、视频播放、低端设备
配对性能、Windows、pet 像素共存和长时 soak 未验收。负责人 Mirage 维护者；
补跑条件为更新后的产品重启、用户授权桌面、受控任务/设备和预登记质量门槛。
本轮不重启正在运行的产品，不宣称上述门禁通过。

## 同日策略更正

维护者随后要求自主恢复，本文中“永久只读/人工复核及任务仍 failed”属于旧策略
验证结果，保留作历史证据。当前策略由 [DEC-113](../decisions/DEC-113-autonomous-action-recovery.md)
替代，见[新验收记录](autonomous-action-recovery-20261022.md)。
