# M7-07 在线模型编号选择与 Canvas 动作闭环

> 状态：Verified（MiniMax / Linux 私有 Xvfb 范围；M7-07 整体 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-055](../decisions/DEC-055-desktop-image-supply.md)、[DEC-058](../decisions/DEC-058-scoped-visual-marks.md)、[DEC-059](../decisions/DEC-059-visual-text-postconditions.md)、[DEC-060](../decisions/DEC-060-geometric-visual-marks.md)

## 环境与路径

Ubuntu 24.04 / Linux 7.0.0-34-generic x86_64、GCC 13.3.0、CMake 3.28.3、C++20。
基于 c786df7 / feat/m7-conversation-desktop-tools 的在途 M7 工作树，未提交。
Mira c75378b、Mirador ae8cf87、kairo ef821dc、eui-neo a7e625f、opt-in ncnn e54f7b1
均未升级或修改；完整 pin、外部模型/字体摘要和公共样本回执见
[数据归档](data/online-visual-canvas-20261010.json)。

使用既有 MiniMax-M3 / https://api.minimaxi.com / anthropic.messages.v1 / adaptive
配置，经实际 ModelLayer、Mira Gateway/Provider/SocketHttpTransport/SecretRef 执行；
没有 Provider override，不自行拼 HTTP。只有私有 Xvfb 的公开绘图上传模型，未上传
用户桌面。模型清单开启 geometry_marks、移除 templates，编号映射中的 template_id
全部为空，候选没有文字提示；位置和答案不进入 prompt。prompt 只给固定 Canvas
window_id、目标图案、期望业务文本与工具顺序。没有把地面真值注入模型响应或 click。

窗口为 480×180，原点 (20,20)，Xvfb 520×220，标题始终 Canvas，无 a11y。公共
图案是两只黑方框：一条白斜线和白交叉 X。真实 Mirador 几何/fusion/SoM 生成编号；
编号图经 PNG artifact/source/supplier 进入模型。模型执行 mark→click→verify；
真实 XTest 产生完整按下/释放。测试 oracle 只核对私有窗口事件落点；正确点击正例
才绘制 Task saved，负例不重绘。已有 Screen/PP-OCR 后观察和宿主终态复验决定
是否通过，窗口标题不作业务证据。

Canvas 事件每次至多处理 32 项，在现有 harness 的 Executor 有限任务内、OCR 观察
前及任务末尾处理；没有新线程、timer 或队列。既有三十秒 owner、五秒图/引用与
五秒观察期限保持，未因在线模型放宽。future 被消费，ModelLayer/视觉管线在非
worker owner 关闭，最后 shutdown(true)。默认 ctest 不联网；在线模式仅由显式
私有驱动开启。凭据只进入子进程环境，传输通过 SecretRef 解析；日志/报告拒绝含
当前凭据的输出，模型思考不写入报告。

## 实际结果

| 样本 | 模型选择 | 实际输入 | 新观察与终态 | 总耗时 | 事件年龄* |
| --- | --- | --- | --- | --- | --- |
| cross-right | 2 号，右侧交叉框 | 一次按下/释放，零错点 | verify 与终态 OCR verified，ok | 16,806 ms | 3,902 ms |
| cross-left | 1 号，交叉框交换到左侧 | 一次按下/释放，零错点 | verify 与终态 OCR verified，ok | 11,366 ms | 4,177 ms |
| unchanged | 1 号，左侧交叉框 | 一次按下/释放，零错点 | 两次 unverified，宿主 failed，最终回答为空 | 11,825 ms | 3,108 ms |

3/3 选号与闭环通过，两个正例和一个失败门禁负例。每例仅一个 mark、一个 click、
一个只读 verify，backend 计数 1→2→3→4；点击只用当次 scope/epoch/编号。
每例 supplied_input_items=4、dropped=0、degradations=0：这个计数包含图片与
后条件元数据，不能解释为四张图片。首版在线 CANVAS 摘要把该计数误标为 images，
最终已修正为 supplied_items；归档保留 LIVE 的明确字段。

*事件年龄是从测试 mark 回调分析结束到私有 Canvas 处理按下事件的近似间隔，
包含 PNG/模型/动作处理，不是产品内部精确引用年龄或延迟基准。动作实际通过已有
五秒新鲜度门禁。三个样本不构成在线推理延迟、稳定成功率或性能保证。

在线执行完成后仅改诊断字段标签，并将末尾残留 Canvas 事件排空纳入同一个有限
任务；产品工具、模型/网络栈和期限没有变更。没有为这些收尾重复付费请求；最终
本地构建/fixture/sanitizer 另验。

## 复现与回归

[驱动](../../tests/manual/visual_online_acceptance.py)读取已有审核过的外部模型清单与
私有 provider 配置，生成公开样本。必须先按[模型资源审计](../supply-chain/real-ocr-resources-20261009.md)
准备权重/字典，不提供自动下载或分发包。

```bash
cmake --build --preset ocr-debug -j3 --target x11_visual_marks_test
python3 tests/manual/visual_online_acceptance.py --provider /home/linductor/mira/docs/model_provider/minimax --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-online-20261010
```

每例最多三十秒 owner、父进程九十秒护栏，无自动重试。失败继续保存 selection、
tool 状态、输入计数、供给统计与终态，所有样本未通过时返回非零；只有被拒绝而
没有实际点击，不足以通过负例。使用不同输出目录复跑，避免覆盖历史证据。

收尾重建后的最终构建、回归与静态检查结果：

| 组合 | 实际结果 |
| --- | --- |
| native-debug 完整构建/测试 | 60/60，0 failure，27.51 s |
| native-release 完整构建/测试 | 60/60，0 failure，22.39 s |
| ocr-debug 完整构建/测试 | 59/59，0 failure，24.01 s |
| ASAN/UBSAN 定向（post_action/visual_text/visual_marks/x11_visual_marks） | 4/4，0 failure，8.06 s |
| TSAN 定向（同上，`setarch -R`） | 4/4，0 failure，8.71 s |
| 离线 Canvas 回归：几何 / 文字 / 模板 | 3/3、5/5、4/4 |
| 模板 Canvas ASAN/UBSAN 复跑 | 4/4 |

本增量 C++ 文件 clang-format --dry-run --Werror 通过；`git diff --check` 与暂存区
检查通过。ocr-asan/ocr-tsan 为已有本地构建目录，均启用 MIRAGE_WITH_PPOCR；ASAN
开启 detect_leaks=1，UBSAN/TSAN 开启 halt_on_error=1，TSAN 经进程级 `setarch -R`
运行。完整 pin、外部模型/字体摘要、逐样本回执与时间线见
[数据归档](data/online-visual-canvas-20261010.json)。

复现完整构建/测试：

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug -j3
cmake --build --preset native-release -j3
ctest --preset native-release -j3
cmake --build --preset ocr-debug -j3
ctest --preset ocr-debug -j3
cmake --build build/ocr-asan -j2 --target post_action_verification_test visual_text_verification_test visual_marks_test x11_visual_marks_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -j1 --output-on-failure -R '^(post_action_verification_test|visual_text_verification_test|visual_marks_test|x11_visual_marks_test)$'
cmake --build build/ocr-tsan -j2 --target post_action_verification_test visual_text_verification_test visual_marks_test x11_visual_marks_test
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -j1 --output-on-failure -R '^(post_action_verification_test|visual_text_verification_test|visual_marks_test|x11_visual_marks_test)$'
```

## 剩余门禁与负责人

本记录只闭合该模型/方言和公共受控 Canvas 样本的在线视觉选号，不宣称任意 Agent
规划或用户业务验收。用户宿主、自由轮廓图标、跨主题/尺度、高分辨率预采集、DPI、
Windows/原生 Wayland 与完整权重 TSAN 期限复跑仍未完成。当前执行环境报告
XDG_SESSION_TYPE=wayland（DISPLAY=:0），私有 Xvfb 结果不替代原生 Wayland 授权、
权限撤销或真实宿主验证。维护者在相应平台/资源/权限具备时补跑；宿主验收需限定
公共测试窗口及其截图范围，继续通过正确的 Platform Backend 能力。

完整权重 TSAN 沿[既有开放记录](visual-text-canvas-20261010.md)，在原五秒观察/
三十秒 owner 期限复跑；本轮定向 sanitizer 不替代业务门禁。外部权重分发许可沿
资源审计。未新增依赖缺口、pin、线程设施、提交/推送或 MR。
