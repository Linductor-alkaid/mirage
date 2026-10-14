# M8-11：实时气泡实现与验证（2026-10-14）

> 状态：In Progress（Linux 实现和 fixture 已通过，实桌面扩大门禁开放）
> 负责人：Mirage 维护者
> 依据：[DEC-089](../decisions/DEC-089-mira-pet-bubbles.md)、[设计](../design/mira-pet-bubbles.md)
> 工作树：feat/m8-realtime-bubbles，基线 3bbe72a；不包含另一个任务的 Mira 输入复用升级。

## 实现和依赖

维护者授权独立 worktree 修复 Mira 后提 PR，再完成 Mirage 气泡。Mira PR#94
开放待评审，消费 ea720a6122320a0afac9316acb0ca05838051972；嵌套依赖不变，
锁定与审计见 [升级记录](../supply-chain/mira-thinking-preview-20261014.md)。

ModelLayer 转发独立公开思考 sink；运输回调投递当前 turn 的双 LatestMailbox，
Runtime 串行域有界投影。思考保留最近≤2KiB UTF-8 单行，正文≤16KiB 提取完整短句，
500ms 合并更新；规范正文结算后保留5秒。原始 feed 在结算释放，不写入历史。
工具/失败/取消/接管清除，当前来源/请求/重试隔离，不等待动画首轮；IPC 可探测可选
bubble 与完整状态编码预算≤4KiB。转义预算不足时缩减思考行并标 truncated。

Linux 原生窗口264×322，240px角色不缩小；12px透明阴影边缘，58px上方空间。
泡身240×36、文字216×20，浅/深色沿用原系统；思考为两个圆尾、24px/s连续横滚，
对话静止并带短尾尖。文字按实际 Noto Sans SC glyph metrics 测量，减少动态效果
显示静态最新行。X11 ShapeInput 仅角色 alpha，不包含泡身/尾部；保存坐标为角色
画布锚点，拖动与退出均换算。保存位置靠近工作区上缘时下方展示并反转尾部。
新增 timer 由既有 Executor owner 管理，有限期限至多保留三组 handle/future，取消、
失败、拒绝可见，关闭排空。不新增线程/队列/调度器。

## 实际执行

- Debug 全量构建与 ctest：80/80 通过，约175秒；包含 IPC golden、真实 Runtime/
  ModelLayer/Gateway 控制流 fixture 与既有桌面/会话回归。
- ASAN、UBSAN 专项：各6/6通过（四个目标+隔离setup/cleanup）；ASAN detect_leaks=0。
  涵盖bubble、golden、投影刷新、pet_status_integration；取消、超时、失败、接管、
  owner切换、正文后迟到thinking和规范终态均有断言。
- TSAN专项6/6通过，按仓库CI既有方式 `setarch x86_64 -R ctest --preset tsan`
  关闭ASLR。首次直接启动时纯单测出现运行库 `unexpected memory mapping`，
  关闭ASLR后全部通过，没有race报告；不把前次启动失败写成通过。
- 原生 pet_bubble_view_test：生产compose helper+真实WebP帧，1×/合成2×、明/暗、
  thinking/dialog、UTF-8超宽静态替代、下方布局与消失断言通过；8张PNG由同一renderer
  输出，97 checks。合成倍率不代表物理DPI或其他平台。
- 原生截图finish reviewer初评只有对话polygon缺少size/position而不可见；一次修复后
  同四张light/dark、1×/2×对话图尾尖均连接泡身指向头部，限定verdict为ship。
- Public header boundary：58个头，0违规。改变的C++文件clang-format18.1.8通过。
  全仓format-check遇到基线desktop_action_verification.cpp既有格式漂移，未扩大范围修复。
- 静态公开API探针通过；git diff --check通过；依赖gitlink与lock一致。

日志与截图保留在本次工作环境的 /tmp/mirage-bubbles-* 和聊天visualization目录，不进入Git。
原生捕获是在private Xvfb中，不替换或重启用户当前桌宠。

## 原生窗口回归

私有Xvfb/ICCCM peer运行relocated tray+独立pet，核验实际RGBA、非激活、ShapeInput、
拖动/锚点保存、隐藏、独立启停、crash恢复和owner退出。首次运行因thin开发构建无OCR
而失败，脚本改为只在存在bundle时传manifest。随后发现退出保存外框导致重启角色偏移，
统一保存画布锚点后补跑，32项全部通过；non-activation记录activated=0、上方70px输入区
完全穿透、独立stop/restart恢复锚点、crash恢复、隐藏owner优雅退出和异常丢失均通过。
隐藏shutdown约1.92秒、owner丢失退出约6.42秒；软件renderer可见CPU28.66%单核、隐藏0.67%，
仅为fixture观察值，不作为硬件GPU性能保证。脚本与结果为tests/manual/pet_preview_acceptance.py、
/tmp/mirage-bubbles-private-x11/pet-results.json。

## 设计系统记录

documenter角色按degraded说明内联执行：No changes to root DESIGN.md/.impeccable/design.json。
检查DESIGN.md、native typography.hpp、bubble_view.hpp、bubble_presentation.hpp及原生截图。
调色板：沿用白/#202020、#222222/#eeeeee中性表面与文本。
字体：Noto Sans SC，13px/20px，沿用1.448EM修正与实际glyph测量。
布局：既有240px角色保持，气泡为一个固定单行附加表面。
形状：18px思考pill+双圆尾、12px对话泡+短尾，限制在桌宠模块。
运动：默认24px/s，增量不重启；减少动态效果遵守用户偏好。
未canonize或修复：全局设计文档既有结构/规则漂移不属于普通模块扩展；桌宠局部几何与运动
在气泡设计中记录，不推广为全产品系统规则。

## 开放门禁

在线供应商端到端流、真实桌面compositor alpha、物理DPI/多屏、复杂桌面背景、
Windows/Wayland-native、小工作区不能容纳的隐藏、整组目标/OCR避让与无障碍阶段播报
未执行/未交付；M8-06/10负责平台扩大实现和验收。当前逻辑窗口不可调整大小，工作区
启动选择上/下布局，拖动只整组约束，不声明跨屏动态重排。负责人Mirage维护者；补跑需
对应平台/在线配置与M8-06/10实现。M8-11整体保持In Progress，M8-06仍Planned。
