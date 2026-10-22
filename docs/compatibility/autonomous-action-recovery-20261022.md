# 自主动作恢复验证

> 状态：Completed（本轮源码与 Linux 受控验证；实机及通道重建保持开放）
> 日期：2026-10-22
> 更新日期：2026-10-23（跨日验证）
> 负责人：Mirage 维护者
> 工作项：M9-20、M9-09/11/12、M7-25/26、M10-07
> 基线：e4d5285；依赖指针不变

## 范围

按 [DEC-113](../decisions/DEC-113-autonomous-action-recovery.md) 纠正普通执行故障默认
人工复核的策略。受控模型从失败回执继续，通过现有 Mira Continuous 循环、当前
观察及独立操作恢复，不创建私有线程、队列、循环调度或修改 pinned 依赖。

- 执行 uncertain 保留；recovery_status 单独报告 observation_required、reobserved、
  replanned 或 condition_observed。旧固定条件未匹配不能作为 depends_on。
- 独立 ApplicationProvider 路径可绕过与旧点击相关的观察门禁，但仍需自身当前
  应用观察/权限。它不会清掉旧执行不确定或证明页面加载/播放。
- 新观察解除继续规划的阻塞，独立操作后的当前观察允许任务按目标结算，旧失败
  可进入有界归档。当前观察已足够时可直接作答，无需额外副作用；旧条件保持未匹配。
  未知规范回执保持完整性错误，不冒充普通恢复。
- Portal 与 Adapter 共用 InputRecovery 策略。已释放 input_lag 等待严格更新源帧，
  连续第三次迟到停用该路径，完整输入成功清零故障 episode；不重放输入。
- exec/write/edit 恢复后可用，但使旧桌面结算检查点失效。接管、撤权、取消、
  未确认释放与旧 scope 继续拒绝，受影响路径不可用时返回具体诊断。

## 验证方法与结果

Linux x86_64、C++20/CMake/Ninja；Debug 全量与各 sanitizer/native-release 分开验证。
计数包含隔离 setup/cleanup；脚本不调用外部模型、浏览器或用户实际桌面。

| 配置 | 最终结果 | 总时间 |
| --- | --- | --- |
| debug | 110/110 | 338.90 s |
| asan | 10/10 | 166.07 s |
| ubsan | 10/10 | 148.35 s |
| tsan | 10/10 | 304.80 s |
| native-release | 14/14 | 72.62 s |

专项覆盖动作策略、实际装饰注册表、规范历史/归档、Portal 时序、视觉动作、过程预算。
release 额外覆盖真实 ncnn 的合成 PP-OCR、native Agent、连续采集及感知消费者。
受控模型验证两条路径：迟到 → 三次提前 exec 拒绝及 write/edit 拒绝 → 新观察 →
exec → 新图输入 → 当前观察后结算；以及迟到 → 新观察 → 直接作答，执行/条件仍 uncertain。
同一授权 session 创建/关闭各一次，M9 text_only OCR/state 各调用一次，旧 scope 不派发。
另覆盖第三次连续迟到仅停用该输入路径、成功完整输入重开故障 episode。

迭代阶段旧断言和未消费替代图像的夹具曾失败，修正测试消费链，没有削弱原图像守卫。
TSAN 长会话在并行构建期间曾触及原 300 s 期限；同一二进制单独重跑 192.92 s 通过。
上述日志保留，最终版本的完整专项结果以上表为准，未放宽期限或新增 suppression。

66 个公开头边界零违规，沿用 DEC-096 两条既有 seam；14 个变更 C++ 文件的修改区段
格式与 git diff --check 通过。全仓格式检查仍在基线已有的 model_layer.cpp output-limit
换行处失败，已对 base_commit 复现，未记为通过。Markdown 本地链接及 JSON 校验另留机器计数。
五个产品可执行文件动态依赖全部解析，native-release 已重建，未启动或重启产品。

准确命令、日志、源码/二进制摘要与依赖 pin 见[机器记录](data/autonomous-action-recovery-20261022.json)。

## 限制与补跑

真实 Wayland input_lag 根因、在线模型长任务、目标网站导航/播放、低端设备资源、
Windows 未运行；负责人 Mirage 维护者，补跑条件为更新产品重启、受权桌面及
预登记任务/设备。受控帧和 PP-OCR fixture 不代表 compositor 端到端验收。

释放失败/传输断开的自动通道重建尚未交付：现有公开平台契约无法在本次变更中
可靠证明残留输入已经释放，不伪造恢复成功。M9-20 保持 In Progress，后续补齐
通道恢复及实机记录。当前 Mira ConversationLoop 在模型主动回答时结束；本轮以
供给/提示及工具状态让模型自主恢复，没有新增终态前宿主续接循环。
