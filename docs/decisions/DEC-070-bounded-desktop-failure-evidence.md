# DEC-070：有界桌面拒绝回执与可诊断参数校验

> 状态：Accepted
> 日期：2026-10-11
> 工作项：M7-14 第二增量
> 冻结里程碑：M7
> 替代/被替代：无（补充 DEC-068）
> 依据：维护者要求继续分析最新 Mirage 会话失败；DEC-068 未派发拒绝可修正的既有契约
> 负责人：Mirage 维护者

## 背景与问题

最新产品会话默认截图/OCR 已运行，但 window_focused 激活在派发前因参数校验
被拒绝。历史隐藏条件正文，仅保留散列，不能据此断言具体哪个字段值错。现有拒绝错误
携带完整验证对象，超过 pinned Mira ToolExecutor 512 字节 safe_error_summary 上限后
截断成不完整 JSON，宿主无法确认自己的 action_id，错误封锁后续动作。这是 Mirage
回执装配错误，不能认定 Firefox/平台无法激活，不修改 pinned 依赖。

## 决策

所有已建记录的失败向 canonical 工具链返回最多 512 字节的完整 JSON：本轮 action_id、
status、verified=false、dispatched、side_effect_may_have_occurred、input_released、
reason 及有界无条件正文 message。参数校验明确字段名；完整 verification 由同轮
宿主记录单独投影给产品过程事件/历史，及 action.verify / evidence supplier 提供。
未知/伪造/其他轮次/派发后无新证据或不确定输入继续封锁；不依赖失败摘要中的自称。
Provider 的结构化错误提取有界 message/error_code，不嵌套复制长 JSON。会话收尾
使用同一 canonical/宿主记录判定，保留旧失败卡片，但已纠正且验证的动作不会因旧卡片
再次判失败；最后仍拒绝或未验证的动作不能结算成功。

无关 expected_* 的空字符串视为未声明，适配模型填写所有可选字符串的情况；非空
无关条件、字段类型错误、缺少相应条件仍拒绝。window_focused 只检查目标焦点，
window_title_equals / semantic_node_exists / visual_text_exists 各自要求既有对应字段。
禁止静默丢弃非空条件、降低置信度或放宽期限。历史记录保留被隐藏条件的字段名和空值
结构，不复制条件正文；已保存旧历史不迁移、不重写。

编号失败区分 capture / analysis / publication / environment 阶段，返回有界稳定代码与
处理建议；大屏原始编号采集继续 8 MiB、1:1、五秒预算，明确要求 ROI。空编号成功
不是输入框可定位的证明；截图的 image_pixels 必须按来源比例映射到 desktop_pixels，
不得从空编号虚构目标。OCR 实际 canonical 回填包含区域正文，产品历史故意只存计数。

## 备选方案与影响

扩大依赖摘要、关闭 canonical 失败门禁或丢掉失败卡片都不能建立可信恢复证据。
选择在 Mirage Adapter 内缩小错误回执，并沿现有产品 Part 投影完整证据。无新增并发
路径或生命周期 owner；旧历史不迁移。真实输入结果不确定时仍可能需要用户接管。
收尾审计发现旧 Runtime 测试有默认持久化目录未隔离，补齐所有 Linux CTest 的
XDG_STATE_HOME / XDG_CONFIG_HOME / XDG_DATA_HOME。每个测试目录独立，setup/cleanup
fixture 清理本构建的测试状态，保留既有 sanitizer 环境。同一构建目录的两次 CTest
调用应串行；不同构建目录与单次 CTest 并行测试互相隔离。

## 验证方式

验收覆盖真实 ToolExecutor 的 512 字节边界、拒绝→核验→新观察→修正→成功链、空可选
字段与非空错配、未知/截断/伪造/不确定负例、产品过程证据投影、编号预算失败和 ROI
修正。真实用户在线业务任务单独验收，不能以 fixture 通过声明浏览器任务完成。

## 关联文档和工作项

M7-14 第二增量；[验收记录](../compatibility/desktop-rejection-recovery-20261011.md)、
[M7 计划](../plans/m7-visual-tools-and-input.md)、[桌面动作边界](../security/desktop-actions.md)。
