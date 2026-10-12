# 本地 OCR／模板 Workflow 验收（M7-11 第二增量）

> 状态：Linux 受控范围已验证；M7-11 整体 In Progress
> 日期：2026-10-11
> 负责人：Mirage 维护者
> 决策：[DEC-069](../decisions/DEC-069-local-visual-workflow-grounding.md)

## 已交付行为

产品 workflow 目录注册 desktop.knowledge.visual_click，复用实际 service-owned
Mirador/PP-OCR/ncnn 管线、窗口 ROI 编号、scoped click 和动作后新证据。知识保存窗口
标题、页面文字、OCR 目标或已装配模板 ID，以及预先声明的文字后条件，不保存坐标或
执行引用。唯一性、置信度、权限、焦点/标题/几何/像素、epoch/取消和输入释放均保持门禁。
首次后观察早于重绘时仅可在同一五秒预算内追加两次只读 OCR，不重复输入。

Runtime 原有周期维护任务每 500ms 派发到串行上下文并等待 future，Executor 明确允许
周期 callback 重叠。真实视觉步骤占用 worker 时，两个维护等待加 workflow 驱动和
取消监视占满默认四个 worker，导致点击后的观察与 IPC 串行派发停滞。私有进程线程栈
及已变化的 Canvas 点击计数排除了“尚未派发输入”或“正在执行 OCR”。应用层为维护
callback 设置单一 admission，异常安全释放；重叠 tick 有界结算，handler 读取最新
状态。保留 Executor 周期结果/失败、future 消费及原停止/排空顺序，没有修改依赖或
提高线程数/期限。三次慢 Provider workflow 回归覆盖同一四 worker、重叠周期、状态
查询、后观察与正常退出。

## 验证

| 验证 | 最终结果 |
| --- | --- |
| native-release 全量 | 68/68 |
| Debug 全量 | 62/62 |
| ASAN + UBSAN 定向，leak detection / halt_on_error | 10/10 |
| TSAN 定向，halt_on_error，setarch x86_64 -R | 10/10 |
| local_visual_workflow_test | 177 checks，0 failures |
| 实际 Release 产品，私有 XDG/DBus/Xvfb | 37/37 |
| 本地 DEB 解包后实际产品，同一套验收 | 37/37 |
| 公共头依赖边界 | 53 个头，0 违规 |
| 变更 C++ clang-format / git diff --check | 通过 |

fixture 覆盖 OCR/模板重用、窗口移动与编号换代、模板位置交换，以及标题/页面/目标
错误、重复与低置信度、已有后条件、坏数据、Provider 缺失、权限拒绝、像素/焦点/几何
变化、取消/超时/接管、输入异常/未释放及新后条件伪成功。直接执行真实 pinned
WorkflowRuntime：DryRun 零动作，Strict 完成，Recoverable WaitingAgent，停止后拒绝
提交与 Executor 异常消费。后观察延迟夹具确认一次点击、两次新 OCR 即可结算。

外部 owner 在私有 1000×700 Xvfb 中启动实际 mirage-tray --no-shell，模型 disabled：

- 默认视觉包无需手工 OCR 配置，发布 DryRun，Canvas 输入计数保持 0。
- OCR Save 完成后 Saved 新出现；窗口移动后同一知识重新解析，累计两次点击。
- 已有后条件、重复 Save、错页、错标题都 WaitingAgent，输入计数不增加。
- 点击无后条件改变为 WaitingAgent，不自动重发；input.inject deny 为 failed。
- 第二次 owner 装配两块公开模板资产；cross-icon 在两位置均命中正确物理区域。
- 模板目标无后条件同样 WaitingAgent；11 次 workflow 共 6 次物理点击。
- 模型始终 disabled；两次 owner 经产品退出确认排空并退出 0。

本地 DEB 已重新生成；解包后启动包内原始 mirage-tray，默认发现包内视觉资源，
无需安装到用户系统，同一套 OCR/模板验收 37/37。包内 tray 摘要与本次构建一致；
包大小及 SHA-256 记录在机器回执，不宣称用户系统已安装或运行新版本。

结果、源码摘要与测试日志摘要见[机器回执](data/local-visual-workflow-20261011.json)。
早期失败未计为通过：第一张后截图先于 Canvas 重绘；追加只读核验后，又在实际产品
暴露上述维护重叠问题，5s 和诊断用 20s 状态查询均超时。发布门禁测试曾误用 Exists
缺失信号，修正为 DryRun 可规划、实际结果可判定的谓词后再验。首轮并行 Debug 的
DBus properties_changed 等待失败；最终全量重跑通过。未禁用 ASLR 的 TSAN 起动遇到
unexpected memory mapping，最终按已有 setarch 条件运行。TSAN 沿用仅系统 GLib/GIO
抑制，Mirage 帧不豁免；未执行完整真实 OCR 权重的 sanitizer 验证。

## 使用与开放范围

发布/运行与私有产品复现命令见[操作示例](../examples/local-visual-workflow.md)。默认包
提供 OCR；应用专属模板仍需资源清单，不自动训练或从截图登记。IR 文件可本地保存，
当前版本目录为进程内投影，重启后需重新发布。截图/ROI 超预算会拒绝，大窗口和动画
仍可能被既有门禁阻止；证明的是局部 UI 后条件，不是任意业务事务成功。

原生会话自动选资产、Agent 恢复、跨重启自动资产恢复、多页 AppModel 导航仍待推进。
用户宿主、Windows/原生 Wayland、任意应用/DPI/跨屏未在本轮验证；负责人 Mirage
维护者，补跑条件为目标平台、应用初态与权限准备完成。没有操作用户桌面、改用户配置
或提交/推送；当前无需用户配合。
