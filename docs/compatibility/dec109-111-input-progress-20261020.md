# DEC-111：目标文本值契约（2026-10-20）

> 状态：平台无关首个契约通过回归；真实平台与工具接线开放
> 工作项：M7-32；负责人 Mirage 维护者
> 基线：e63c8de + feat/desktop-input-policy 工作树

## 实现与边界

### DEC-111 / M7-32 平台无关目标值首个契约

新增 AccessibilityProvider 独立 opt-in 的 read_text_value SPI，默认显式不支持；
不从 set_text、树可用性或系统平台推断支持。一次 verify_text_value 仅向 Provider
传递确切 scope/ref 与预算，不传预期值；要求同一目标、本次调用内的新单调采集
区间、完整/已提交/非受保护的有效 UTF-8 值。大小写、空格、换行、顺序严格比较，
空值有效。预期值与正文各最多 4096 字节，整个调用最多五秒且受更短预算/取消约束。

结果 v1 区分 matched/mismatched/verification_unavailable，只含身份、来源、
时间及固定原因码，不保留实际或期望正文，不回传平台异常/错误中的敏感文本。
只读重验重新读一次，无轮询、OCR、复制、输入或模糊放行；不能清除输入执行未知、
任务、权限或 epoch 围栏。

此阶段没有启用 AT-SPI/UIA 真实读值：可靠控件/窗口 incarnation 与焦点、IME 提交、
权限及任务/epoch 接线尚未完成。没有注册 text_value_equals wire 条件或改变默认
业务验证。M7-32 保持 In Progress，复制事务 M7-33 与选路/真实应用 M7-34 保持
Planned，不以纯比较器测试冒充跨平台目标文本验证完成。

## 验证

本节结果来自包含 DEC-109/110 同期增量的完整工作树，非独立平台验收。
精确读值纯比较器 115 检查零失败；Debug 全量 103/103，native-release、ASan、
UBSan 定向各 10/10（含夹具）通过。异常、身份、时间、保护/提交状态、取消、
期限和字节边界均通过受控 Provider 覆盖。完整构建命令与相关增量的共同证据
在后续文档同步中保留；Windows、真实控件读值与工具条件接线未执行。
