# M7-07 模型思考配置、阶段耗时与输入前重观察

> 状态：Verified（配置与显式刷新边界；在线门禁未闭合，M7-07 整体 In Progress）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-058](../decisions/DEC-058-scoped-visual-marks.md)、[DEC-059](../decisions/DEC-059-visual-text-postconditions.md)、[DEC-062](../decisions/DEC-062-visual-capture-scope.md)、[DEC-063](../decisions/DEC-063-regional-visual-pre-capture.md)

## 问题与能力复用

[Xorg 宿主](user-host-visual-canvas-20261010.md)已补离线 3/3，在线 adaptive
正确选号 2/2，但左目标返回点击请求时引用过期。验收 harness 原来固定 adaptive。
pinned Mira 的 Messages API 明确支持 ThinkingMode::Disabled / Adaptive；现有
Mirage ModelLayer 对 MiniMax-M3 暴露 none/adaptive，已有请求映射。无需新模型
Adapter、传输、工具或依赖升级，也不把 provider 配置文件中的 medium 当作
MiniMax-M3 的受支持档位。

新增 `visual_online_acceptance.py --reasoning none|adaptive`，缺省仍 adaptive。
显式测试配置经 MIRAGE_CANVAS_REASONING 传给现有 complete_harness_turn，none
映射 Disabled；不修改产品或 provider 文件的默认值。API 测试检查所有请求的
ThinkingMode、输出预算，MiniMax-M3 的 low 在 provider infer 前拒绝。

## 诊断与重观察边界

harness 复用既有 DialogProcessSink，在同一 Executor 回合记录最多 32 条工具的
开始、结束和状态，输出 TIMINGS 与 LIVE.timings。mark_to_click_ms 是最近一次
mark 工具开始到 click 工具开始的间隔；mark_ready_to_click_ms 从 mark 完成开始。
它们不是 OS 注入原子时间；click_age_ms 仍是 fixture 收到按钮事件时相对 mark
分析完成的年龄。投影不采集思考正文、凭据或用户窗口内容，不建立并行监控系统。
异常/取消可留下 finished_ms=-1；时间记录不能替代工具和新观察成功证据。

新增组合边界测试：在输入前显式再次 mark，旧 artifact 不可取、旧 scope 不可
解析；即便编号同为 1，也按新图/作用域重新选择，唯一点击与验证可通过。相反，
如果旧像素的 click 已被拒绝，随后再次 mark 不能解除验证门禁，新 click 不派发，
终态仍失败。已有过期、取消、四次预算、权限、shutdown 等回归继续有效。
本轮没有实现自动续期或失败后的重试，也没有新增恢复状态机。

## 实际在线样本

MiniMax-M3 / anthropic.messages.v1 / none，真实 3840×2160 私有 Xvfb 上
480×180 公共窗口 ROI。样本/权重/字体、无 template_id 提示、一次输入与固定
Task saved OCR 条件同既有入口；owner 30 秒、引用/观察 5 秒、采集 4 次不变。
这不是与 adaptive 的同时受控性能基准。

首轮 0/3、选号 0/3：所有样本在端点 DNS 阶段失败，模型未取得图，工具调用和
实际输入均零。随后 DNS 探针 1.259 秒返回；不带凭据、绕过代理的直连 TLS
HEAD / 在 0.231 秒返回 HTTP 404，证明该次可达。因此按原条件补跑一次，失败
历史保留；直连探针本身不是模型 Provider 成功证据，不硬编码 IP 或增加 DNS 缓存。

| 补跑样本 | 结果 | 工具阶段与输入证据 |
| --- | --- | --- |
| cross-right | 失败，选号正确 | mark→click 3383 ms、mark 完成→click 3009 ms；一次正确按下/释放，错误输入 0；下一模型请求 DNS 超时，没有 readonly verify，宿主保持失败、回答为空 |
| cross-left | 失败，未选号 | 初始模型调用 DNS 超时；无工具与实际输入 |
| unchanged | 通过负例 | mark→click 3685 ms、mark 完成→click 3329 ms；一次正确按下/释放、错误输入 0，mark/click/verify 各一次；readonly/终态均 condition_not_matched，宿主 failed、回答为空 |

补跑验收 **1/3**（通过项是不重绘负例），正确选号 **2/3**；两个到达 click 的
样本在原五秒期限内，没有通过续期绕过检查。已证明该次 none 请求被实际模型
接受，不能据此宣称降低了多少延迟、稳定满足五秒或两个正例闭环成功。网络仍
间歇失败，未继续补真实 Xorg none 三例，也不替代此前 Xorg adaptive 证据。

## 回归与复现

五配置 native-debug / native-release / ocr-debug / ocr-asan / ocr-tsan 重建
x11_visual_marks_test、visual_marks_test、harness_desktop_tools_test，定向各
3/3；普通与 ASAN/UBSAN 真实权重 4K 文字 5、几何 3、模板 4，各 12/12。
ASAN detect_leaks=1、halt_on_error=1，UBSAN halt_on_error=1；TSAN halt_on_error=1
并经 setarch x86_64 -R。定向 TSAN 不替代此前完整权重 TSAN 的业务期限失败。
只改验收代码与文档，未重跑完整产品套件，不更改 pinned 源码/指针。

```bash
cmake --build --preset ocr-debug -j3 --target x11_visual_marks_test visual_marks_test harness_desktop_tools_test
ctest --test-dir build/ocr-debug -R '^(visual_marks_test|harness_desktop_tools_test|x11_visual_marks_test)$' --output-on-failure
MIRAGE_CANVAS_ROI=1 python3 tests/manual/visual_online_acceptance.py \
  --reasoning none \
  --provider /home/linductor/mira/docs/model_provider/minimax \
  --binary build/ocr-debug/tests/x11_visual_marks_test \
  --manifest /tmp/mirage-m7-host-xorg-presented-20261010/host-manifest.json \
  --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf \
  --output /tmp/mirage-m7-private-online-none-<date>
```

私有复现前须确保 MIRAGE_CANVAS_HOST/MIRAGE_CANVAS_NEGATIVE 未设置。宿主补证
则按[runbook](user-host-visual-canvas-20261010.md)明确指定 HOST/OPERATOR，追加
--reasoning none；网络稳定并且操作者就绪后执行，首败停止，不自动重复输入。
完整公共回执、阶段时间、资源摘要、回归日志摘要与网络探针见
[归档](data/model-visual-latency-20261010.json)。

M7-07 保持 In Progress。负责人为 Mirage 维护者：网络恢复后先补私有正例与
负例，全部通过再补 Xorg none 三例；原五秒/三十秒下的完整权重 TSAN、任意
DPI/跨屏、原生权限 UI、主题尺度/自由轮廓、Windows/原生 Wayland 独立验收。
本轮没有新增依赖缺口、产品行为/默认值或并发路径，未提交/推送。

## 维护者调整网络后的补证

2026-10-10，维护者报告已调试网络并授权继续，按上述原条件新增一次私有三例，
不覆盖首轮和恢复补跑的失败记录。补跑前无凭据直连探针 DNS 0.024 秒，TLS
HEAD / 在 0.167 秒返回 HTTP 404；本批模型回执没有 DNS 错误。探针和三例只
证明该次请求结果，不能证明网络长期稳定，阶段间隔也不能直接归因于模型推理。

| 样本 | 验收 | mark→click / mark 完成→click | 实际输入与验证 |
| --- | --- | --- | --- |
| cross-right | 通过正例 | 2366 / 2032 ms | 编号 2 正确；按下/释放各 1、错误输入 0；mark/click/verify 各一次，readonly 与终态新 OCR 均 verified，backend_calls=4；回合 15992 ms |
| cross-left | 失败，安全拒绝 | 7698 / 7408 ms | 编号 1 正确，但请求到达时超出五秒引用，stale_observation；无副作用、input_released=true、实际输入 0，backend_calls=1；宿主 failed、回答为空，回合 16446 ms |
| unchanged | 通过负例 | 2856 / 2525 ms | 编号 1 正确；按下/释放各 1、错误输入 0；mark/click/verify 各一次，readonly 与终态均 condition_not_matched，backend_calls=4；宿主 failed、回答为空，回合 12636 ms |

本批验收 **2/3**、选号 **3/3**，没有图片 drop/degrade、运行期取消或父进程超时。
已补右目标实际模型正例，仍缺左目标在线闭环；不重绘负例通过表示正确阻止成功
终态。supplier 条目数 4/3/4 不等于上传图片数。入口退出 1 对应未全部通过，
不是将左目标的安全拒绝计入业务成功。私有三例前置仍未通过，真实 Xorg none
三例未启动，维护者无需点击窗口。回执、公共诊断、探针与源文件/二进制摘要见
[独立归档](data/network-adjusted-online-visual-canvas-20261010.json)。

本轮未改代码或二进制；源文件摘要与第九增量一致，沿用已记录的五配置与实际
权重回归结果，没有重复执行或宣称新回归通过。完整权重 TSAN 业务期限失败仍
开放，M7-07 继续 In Progress。

下一步由 Mirage 维护者在 M7-07 补齐请求阶段取证：优先核对 pinned Mira 的
公开模型请求生命周期事件，在现有有界 harness 投影中记录请求开始/结束，
区分本地处理与模型请求占用时间。当前 7408 ms 包含模型与传输耗时，不能据此
登记为 Mira/Mirador 能力缺口，也不能声称仅修改思考配置已解决延迟。根据实际
请求证据选择配置或请求路径改进；发生具体变更后再补私有三例，全部通过才补
Xorg none 人工三例。改进仍须保留原五秒/三十秒期限、四次采集、拒绝后的失败
门禁与一次输入约束；不得隐式续期、复活已拒绝动作或重发输入。

## 推进顺序与定位结论补充

维护者随后要求定位并评估是否继续阻塞开发。已核对本地调用链和 pinned 公开
接口：慢例 mark 290 ms，标注完成后到 click 7408 ms；非流式 harness 完整
infer 响应返回后才投影工具，click 在重采集前按五秒年龄检查拒绝。当前
DialogProcessSink 没有 infer 开始事件，TransportTrace 只有进展位；上文请求
阶段取证建议需要明确诊断出口，不能由已有工具投影直接得出 DNS/TLS/推理耗时。

认可已验证受控基础阶段，M7-09 前置已满足，推进该项不依赖慢模型三例或新增
诊断出口。M7-07 仍 In Progress，原始失败与待补范围保留；本地语义目标和
workflow 组合另列 M7-11 Proposed，本轮不接入。未来复用知识仍要取得新的
执行引用，不延长旧引用或放宽动作后验证。详细路由、已承载能力与未交付边界见
[定位与顺序分析](../research/2026-10-10-local-grounding-and-m7-sequencing.md)。
