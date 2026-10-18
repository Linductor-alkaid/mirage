# 参数校验错误毒化健康共享会话的事故与修复

> 日期：2026-10-18
> 负责人：Mirage 维护者
> 关联：DEC-074（任务内共享观察）、DEC-015（fail closed）、DEC-076（OCR 诊断）、
> DEC-085（会话韧性）、wayland-capture-failclosed-20261014、
> b004ed2/20c9c7f（桌宠排除与捕获栅栏）
> 状态：主因修复已实施并通过独立验证；part#14 疑点已分析归档（结论见专节，
> 诊断增强与 ASAN 现场复现为后续项）

## 事故摘要

2026-10-18 21:32 的会话（session `ab2c8dd3…`，任务"给 QQ 好友小星星点赞"）中，
interactive Portal 共享会话在任务中段被一个**纯参数校验错误**销毁：

1. 模型已成功建立 interactive 会话并完成 dock 点击、QQ 前置、搜索框点击
   （part#1/#7/#9/#12 全部成功，流正常出帧，`source_sequence` 205→398→530）。
2. `desktop.vision.ocr` 读取整 5 秒超时返回 `native_ocr_expired`（part#18）。
3. 模型改为尝试 ROI 小图观测，首次误带 `interaction=true`（part#19），被
   InvalidArgument 拒绝——这本身是正确的校验。
4. **capture 工具外层 wrapper 把该错误当作会话致命失败**，执行
   `native_session_.reset(); native_failed_ = true;`，健康会话被销毁。
5. 此后所有 capture（part#20/#22）返回 "native sharing ended or failed; … no
   reauthorization in this task"，并触发三连相同失败熔断（part#27）。由于
   全部 `desktop.session.*` 输入都要求绑定最新可交互画面做校验依据，视觉观测
   丧失即意味着任务不可继续，以"桌面视觉读取失败或被拒绝"失败。

会话记录 `~/.local/state/mirage/sessions/ab2c8dd394504c73b8d6d2227f8a861c.json`
与 20:14 构建的 `build/native-release/apps/mirage-service`（特征字符串比对确认
与 HEAD 集成层一致）均证实。

## 根因

`integration/mira/src/desktop_image_supply.cpp` 的 `desktop.screen.capture`
wrapper 对任何错误结果做无差别会话毒化；`native_ocr` 内部 capture 失败路径
同样无差别毒化。而 DEC-074 第 2/3 条界定的"不得重新授权"范围是**权限拒绝、
取消、授权代际变化与共享路径（授权/流/输入通道）失败**——参数校验错误、帧
预算耗尽、发布前 epoch 竞态从未被授权为会话终结条件。wrapper 的实现把
fail-closed 过度应用到了"模型可自行纠正的输入错误"上：一次拼写级失误即把
可恢复的拒绝升级为任务级视觉能力丧失。

## 修复

1. **wrapper 仅守异常**（desktop_image_supply.cpp）：删除"任何错误结果即
   reset 会话"。会话存亡由失败现场决定：`capture_desktop`/`shared_input`
   内部的授权撤回、权限拒绝、capture 平台失败、`new_source_frame_required`、
   输入失败且可能有副作用等路径本来就各自毒化（保持不变，fail-closed 不削弱）；
   wrapper 只补 catch 兜底。
2. **`native_ocr` 透传错误**：内部 capture 失败不再无差别 reset，毒化决策
   交回 `capture_desktop` 的自身路径。
3. **工具描述澄清**：interactive 描述显式写明 "roi and window captures reuse
   the established sharing session and never take an interaction flag; only
   source=desktop does"，降低模型复发该参数错误的概率。
4. **测试注释同步**（tests/integration/desktop_image_supply_test.cpp）：原文
   "Any capture failure (including expected InvalidArgument rejections) makes
   the handler retire the session" 描述的正是本缺陷行为，改写为新语义。

## part#14 疑点（resolve 专家通道结论）

同会话 part#14 的搜索框点击派发成功，但毫秒级之后的动作校验
（`observe_shared` 首行 `!native_session_ || !input_supported()`）返回
`shared_session_unavailable`；约 2-4 秒后 part#16 的 capture 又在同一会话对象
上成功且 `input_actionable=true`。专家分析（指令级反汇编部署二进制 + GLib
信号投递实验）确证：

- **该状态翻转在顺序执行语义下不可达**。`input_supported()`/`reusable()` 的
  全部分量单调（interaction/devices 为会话常量，`cast` 只能建立或复位且
  `attempted` 阻止重建，`closed` 只能 false→true，generation 只增）；派发成功
  到首行求值之间的调用窗口内不存在任何写点；Closed 信号投递依赖
  `g_main_context_iteration`，窗口内无迭代点，且派发内最后一个迭代点若投递
  则派发本身必失败——机制级排除。
- **最可能因果（H1）**：part#14 是全会话唯一一次在窗口动画中进行中执行的
  click（part#13 模型记录 QQ 窗口 "wobbling state (rotated)"），guard capture
  刚触发桌宠栅栏的 `pw_stream_set_active(false→true)` 重激活；mutter 侧瞬态
  经某条未识别的进程内数据竞争通路（UB，待 ASAN 现场复现定位）使首行读到
  `cast` 指针的瞬时失真视图。"自愈"因此成立：真实状态从未变坏，是读取失真
  而非状态翻转。
- 次要假设 H2：20:14 构建为提交前工作树产物（part#19 缺陷当晚才修复可证），
  不排除其中另一个时序敏感中间态；但四个关键函数指令级核对与 HEAD 一致，
  可能性低。外部瞬断（H3）在该代码下没有合法表现路径，只能作为 H1 触发器。
- part#14 未致命：模型随后 part#16 成功重观测。任务的死因是 part#19 的
  wrapper 过度毒化（本修复）。

### 后续行动建议（未实施，记录备查）

1. `observe_shared` 的 `shared_session_unavailable` receipt 追加会话分量快照
   （`DesktopCaptureSession` 扩展 `health_snapshot()`：session_present /
   reusable / cast_present / closed / revoked / failed），复发时一条日志即可
   裁决"读取失真"与"真实状态坏"。属公开接口扩展，需按规范同步设计文档。
2. `reusable()` 失败分量细分错误码（cast 空 → `shared_session_released`，
   closed → `shared_session_closed_by_portal`）。
3. 用 `build/asan/apps/mirage-service`（与部署 release 同一工作树产物）在
   真实桌面重放"动画窗口上点击"场景，关闭 H1 最后一公里。
4. 不实施：observe 失败自动重试或经 restore token 静默重建会话（DEC-074/
   DEC-015 fail-closed 禁区）；把此类瞬态升级为可重试输入错误（派发已发生，
   重试会引入重复点击）。

## 测试与验收

- 独立验证（Independent-Verification-Agent，2026-10-18）**通过**。新增
  `native_session_poison_discipline()`（tests/integration/desktop_image_supply_test.cpp
  :1112-1308，38 条断言，main 注册于 :2759），覆盖：
  - 核心回归：interactive 会话成功建立后，事故原始非法组合
    `{source:"roi",…,interaction:true}` 返回 InvalidArgument，会话不销毁、不重建
    （`created==1 && closed==0`），随后正确参数 ROI capture 成功；
  - ROI 越界/缺字段拒绝后 capture 仍可用；
  - fail-closed 保持：平台 capture 失败后仍毒化，后续 capture 返回
    "native sharing ended or failed"，且不再触碰 provider、不重建会话；
  - 发布前 epoch 竞态（"invalidated before publication"）返回 Cancelled 但会话
    存活，后续 capture 成功；
  - OCR 内部 capture 同一竞态透传 Cancelled 后会话存活，后续 OCR 与 capture 成功。
- 修复后树：desktop_image_supply_test 直接运行 1286 checks 全过；ctest 回归
  desktop_image_supply / post_action_verification / visual_observation_loop /
  desktop_atom_toolset / pet_exclusion_tools **7/7 通过**；相关 5 个目标强制重编
  exit 0、零警告。
- 修复前敏感性证明：以 HEAD（修复前生产代码）+ 新测试的独立 worktree 全新构建
  运行，测试在第 1176 行（核心回归的"会话未毒化"断言）失败
  `desktop image check failed: 1185`，与事故机理（健康会话被参数校验错误销毁）
  完全一致，证明测试对旧缺陷敏感。
- 未执行：ASAN/TSAN 树（本次改动不涉及跨上下文状态或生命周期顺序变化，
  毒化路径均为同线程顺序逻辑；后续如触碰并发路径再补跑）。

## 影响范围

- 仅 `desktop.screen.capture` / `desktop.vision.ocr` 失败结果与共享会话生命周期
  的耦合关系；Portal 截屏、输入派发、授权与桌宠排除路径的行为不变。
- 行为变化：校验类/预算类/发布竞态类拒绝后，任务内共享会话继续可用；共享路径
  真实失败后的 fail-closed（不重新授权）语义与事故前完全一致。
