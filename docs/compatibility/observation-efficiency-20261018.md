# 最新会话的观察节奏、桌宠恢复与累计帧上限修复

> 日期：2026-10-18
> 状态：实现与本地回归完成；真实 GNOME / 在线业务验收开放
> 负责人：Mirage 维护者
> 工作树：`fix/observation-efficiency`（基于 `fix/capture-session-poison`，未提交）
> 依据：DEC-107；M7-10 / M7-14 / M7-19 / M8-06

## 会话证据与根因

只读检查 `~/.local/state/mirage/sessions/ab2c8dd3….json` 最新重试，记录于
2026-10-18 23:08:44（Asia/Shanghai）。原文、私有推理、截图和联系人信息不入库。
原始记录保留在用户本机；复现时从 SessionStore 的最新 `chat_turns` 读取工具序列和
`timings`，不使用文件修改时间推断回合顺序。

- 最新重试 33 次模型调用（181280 ms），采集阶段 31 次（3381 ms），OCR 3 次
  各 5000 ms。任务最终失败，回复提到 `shared_frame_budget_exhausted`。
- 共享输入的前置守卫和后置观察也占用原累计 32 帧；只读路径为 8 帧，X11/SoM/区域
  路径为 4 次。帧计数是终身额度，持续任务不能恢复。
- `exclude_pet_during` 包住 handler 和 verification，授权、PNG、OCR 与后置验证全程
  隐藏；OCR 每次耗尽五秒时，桌宠也保持隐藏。
- pet 的可见性回调在隐藏和恢复两侧清空 decoded/player、安排透明画面，并取消解码，
  让短暂撤下放大成重新解码期间的角色空白。
- 成功动作后已经采集后置帧，但只返回摘要，没有下一步 `image_scope`；模型继续调用
  capture。五秒规划画面淘汰与 OCR “obtain a new observation” 错误提示进一步诱导重截。
- 部分按键被 `invalid_argument` 拒绝，后续又出现 `interactive_image_required`；
  已派发鼠标动作缺少可见进展，但 `shared_frame_observed` 仅证明源帧与输入释放。
  不据此断言 QQ 已取焦、点赞成功或指针传输根因已修复。

## 实现

1. 删除各截图路径的累计次数上限，以 `native_started_` 表达共享生命周期，保留单帧
   8/32 MiB、单次有界等待、取消、权限/authority 与源帧水位。保留最新 artifact 替换回收，
   不是扩大或无限累积历史缓存。
2. 增加平台无关 `CapturePresentation` / `capture_presented` 取帧边界。Portal 授权、
   OpenPipeWireRemote、首次流协商/预热时桌宠可见；预热像素不发布。隐藏后读取源时间
   水位，取到自有像素后验证/恢复，再编码和 OCR。输入外层租约在 handler 返回即释放，
   不再包住 postcondition。窗口/语义读取不撤下桌宠；生产 OCR pipeline 和直接 IPC
   visual observation 在实际采集阶段使用租约。未知 compositor 仍附显式未排除证据。
3. pet 隐藏时停止动画/气泡唤醒、取消尚未完成的解码，保留唯一一份已解码 clip 和最后
   画面；恢复仅重新上传。仍是既有单 clip 有界内存，停进程时清理，未增加 worker/timer。
4. Runtime 开启近期观察复用，嵌入默认仍逐次采集。相同来源、epoch、权限和五秒内图像
   可直接返回 `reused_capture=true`；权限检查仍先执行，原时间和引用寿命不延长，验证回执也沿用像素的原始接收时间。
   原有交互目标意图的 60 秒重验窗口内保留历史规划图，明确标为非新鲜验证证据；
   输入始终新取帧做目标守卫，动作后始终要求更晚源帧。
5. 动作回执附 `observation`（新 image_scope/epoch 等），下一步可直接使用。采用 pinned
   mirador `detect_change`（不使用 fingerprint early exit）提供 `scene_change` 提示，
   同来源同几何才比较；提示不替代输入守卫或完成证明。
6. OCR 复用最近的桌面/ROI/窗口预览，返回对应 `source_kind`；先选小 ROI 可减少 OCR
   计算与图像 token。相同摘要和 epoch 的分析失败只实际调用一次，不重复耗尽五秒。
   超时提示改为使用已有画面，取消“必须重截图”的误导。工具/宿主指引同步，不承诺
   未在线测量的 token 降幅。

## 验证

所有测试均为本次工作树构建，未修改 pinned 依赖。最终产品通过 `mirage-native-product`
聚合重建。原始文本日志与二进制 SHA-256 存于
`out/observation-efficiency-ocr-20261018/logs/` 和同目录 `build-identity.json`，均不入库；
由 Mirage 维护者保留至该增量真实 GNOME 验收及评审结束。

| 验证 | 实际结果 |
| --- | --- |
| `cmake --build --preset native-release -j 6`；`ctest --test-dir build/native-release -j 4 --output-on-failure --no-tests=error` | 102/102，65.92 s |
| 最后补充“复用证明保持原图接收时间”后，重建产品及相关测试；定向运行 harness、post_action_verification、visual_marks、gui_fallback、desktop_image_supply、pet_exclusion_tools、pet_observation、pet_frame_presentation、portal_stream_retention | 11/11（9 个业务测试 + 2 个 fixture），14.06 s |
| ASAN 上述五个截图/呈现专项 | 7/7（含两个 fixture），14.46 s |
| UBSAN 同上 | 7/7，13.81 s |
| `TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan` 定向同上 | 7/7，24.56 s，无 sanitizer 报告 |
| `mirage-format-check` / `mirage-boundary-check`；`git diff --check` | 通过；62 个公开头、0 违例，保留 DEC-096 已记录的两个 seam |
| 新增 Markdown 相对链接检查 | 通过 |

五个 sanitizer 专项为 `desktop_image_supply_test`、`pet_exclusion_tools_test`、
`pet_observation_test`、`portal_stream_retention_test`、`pet_frame_presentation_test`；
用各 preset 重建这些 target 后，`ctest --preset <preset> -R
'^(desktop_image_supply_test|pet_exclusion_tools_test|pet_observation_test|portal_stream_retention_test|pet_frame_presentation_test)$'
--output-on-failure --no-tests=error`。

- 新增回归：80 次同源请求只采集/隐藏一次；40 次连续输入由回执驱动，1 次初始帧 +
  80 次守卫/后置帧，只创建一个共享会话；超过原 4/8/32 上限；ROI 后 OCR 不重截整屏；
  同像素分析失败缓存；权限撤回不能命中缓存；历史规划图明确标注。
- 恢复/取消/异常/无租约/恢复失败、未排除标识、验证代次更换、源帧重复与共享关闭继续
  回归。既有过期、参数错误不毒化会话测试保留。
- 私有 Xvfb + ICCCM fixture、真实 tray/pet 进程与 RGBA framebuffer：57/57。
  脱敏结果位于 `out/observation-efficiency-20261018/pet-results.json`，生成物不提交。
  排除释放到角色及拖动区域恢复可见为 7.61 ms；可见 CPU 21.99%、shape 隐藏 2.33%
  （单核口径）；隐藏正常关闭 0.215 s，owner 丢失 5.278 s；shape 隐藏正常关闭
  0.367 s，owner 丢失 5.296 s。均为单次本机软件渲染样本，不是显示刷新保证。
- 最终产品另跑真实本地 PP-OCR + 自动合成根窗口的私有原生验收：
  `python3 tests/manual/pet_preview_acceptance.py --build build/native-release
  --output out/observation-efficiency-ocr-20261018 --exclusion-probe
  build/native-release/tests/pet_exclusion_test --observe-exclusion`，**66/66**。
  两次 IPC 观察逐像素取得桌宠下方变化的画布并识别对应文字，返回前恢复且不移动/取焦。
  本轮恢复可见 7.16 ms，可见/shape 隐藏 CPU 为 20.99%/2.00%；隐藏正常退出
  0.269 s、owner 丢失 5.246 s；shape 隐藏正常退出 0.367 s、owner 丢失 5.147 s。
  仍是私有 X11 软件渲染样本，不外推为 GNOME 的显示刷新保证。
- 初次回归保留：旧测试仍要求累计额度拒绝、要求无截图语义读取隐藏 pet，按新契约更新；
  新长任务夹具最初未设置 proposal.has_side_effects，被 registry 如实拒绝，已修正夹具；
  小型图像不适合变化检测的夹具已改为合法 64×32。没有放宽生产守卫。
- TSAN 直接执行首先因 `unexpected memory mapping` 未能启动；按仓库已有方式用
  `setarch x86_64 -R` 重跑，7/7 通过。
- 私有原生脚本误带内部 `--session` 时被隔离门禁拒绝，随后使用公开入口创建私有
  Xvfb/D-Bus/XDG 目录完成测试；未绕过隔离门禁或操作真实桌面。

## 限制和补跑

真实 GNOME Wayland 的单帧观感、Portal 时延、鼠标目标传递与在线任务成功未验。
负责人为 Mirage 维护者；重启到本工作树 native 产品后，在真实 GNOME 共享中测试
静止画面、慢 OCR、ROI、连续超过 32 帧的输入和完成条件，记录隐藏时延和实际 token。

当前 GNOME 实现依赖撤下窗口、合成源帧、恢复的跨进程往返。只能缩短到必要采集区间，
不能宣称物理显示严格只缺一帧。Portal 输入按 [官方 RemoteDesktop 契约](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)
使用流逻辑坐标；本次未因会话中“点击无进展”而猜测修改坐标换算。真实焦点/目标行为
仍须单独实测。M7/M8 相关工作项保持 In Progress，不把私有 X11 结果写为 GNOME 或
Windows 通过；运行中的用户服务未重启。
