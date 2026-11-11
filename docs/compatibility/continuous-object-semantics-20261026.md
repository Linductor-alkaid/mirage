# 连续对象语义接线验收（2026-10-26）

> 状态：Completed（所列本机接线与验证；M9-13 里程碑继续 In Progress）
> 负责人：Mirage 维护者
> 工作项：M9-13 / M9-21；依据 DEC-091/109/115
> 基线：`f42d878`；分支 `feat/continuous-semantic-binding`

## 范围

Mirador temporal 对象 → 采集 worker 单调内容版本 → 既有 mailbox 完整快照 →
任务 owner 的 VisualScene → 有界模型投影 → desktop.vision.annotate → 后续请求
继承/挂起/撤销。首增量每对象单标签，依赖识别文字/类别；几何/纯外观、关系多字段
解释、永久记忆和新输入引用不在本轮交付声明内。未修改 pinned 依赖或新增并发设施。

默认状态 256 对象/512 KiB；每次模型投影至多 16 对象，条目约 2800 字节，总观察
块不超过 8192 字节。较长文字/类别有显式截断，已标定与未标定对象均轮转；模型
不能使用未供给引用。A→B→A、内容替换、全局布局及事件 gap 递增版本；单纯位置
变化不改内容版本。未知区域挂起、确认消失/源换代/实例替换撤销。

## 验证层次

- `perception_semantics_test`：晚到移动、伪造/未供给引用、内容变化、ABA、挂起/恢复、
  gap、源/实例、过期、非法输入和容量轮转，覆盖 93 对象不会被已有标签饿死。
- `task_perception_test`：现有真实 worker/mailbox 与受控 OCR mapper，完整对象投影
  不依赖事件筛选；原有拒绝/运行取消/超时/异常/shutdown 路径继续回归。
- `desktop_image_supply_test`：真实 ModelLayer/ConversationLoop + 脚本 provider 六轮
  标定/移动/变字/旧引用/重新标定；另验供给预算移除、owner、期限、接管与取消。
- 私有 Xvfb/GTK + 实际 Linux ScreenProvider/PP-OCR/生产连续协调器：测试脚本生成
  `task:<识别文字>` 标签，检查同身份移动继承和内容变化失效；没有调用外部模型。

## 迭代记录

开发期新增夹具曾因继承 final fixture、局部变量 shadow 失败；已修正。脚本 provider
最初用排序 JSON 摘要代替 ToolCallOutput 的实际载荷摘要，被公共 bridge 正确拒绝；
已按实际序列化字节计算。取消会作为 Result error 返回，新增 guard 断言已兼容该
公开契约，不将其误记为 handler 成功。构建中提前启动一次 CTest，二进制链接期间
显示 permission denied/Not Run；该轮不计通过。中间时间字段编辑误删 Event 字段
导致 ASAN 编译失败，已还原；中间日志保留在 out/semantic-wiring-20261026/。

真实 OCR 第一轮验证了内容失效，但移动继承检查失败：脚本只标定初始 Ready，
未对新内容重标定，且把投影轮转误算为新样本导致日志容量提前用完。脚本已按
entity 排序去重并对新内容重新标定；原 `real-ocr-1.json` 保留，不能改记通过。
第二轮四项检查通过（59 帧、10 分析、49 静态跳过、0 分析失败）；最终源码复验
与 sanitizer 结果在本记录末尾追加，不用该中间产物替代最终结果。

## 最终结果

所有最终验证使用同一冻结源码，14 个修改/新增代码与构建/脚本文件的 SHA-256
逐项复核。逻辑源清单摘要为
`2dd277aba7e299fc97ec2e832693c94472f46cde506f7c7a0953f54a6040580b`。
完整命令、环境/pin、文件/产物摘要和三轮私有显示事件/语义样本见
[结构化证据](data/continuous-object-semantics-20261026.json)。

| 验证 | 最终结果 | 实际耗时 |
| --- | --- | --- |
| Debug 完整构建及全量 CTest | 114/114，0 failed、0 skipped | 79.54 s |
| native-release service/native/验收程序构建及专项 | 8/8，0 failed、0 skipped | 11.04 s |
| ASAN 专项 | 8/8，0 failed、0 skipped | 24.77 s |
| UBSAN 专项 | 8/8，0 failed、0 skipped | 24.70 s |
| TSAN 专项 | 8/8，0 failed、0 skipped | 42.21 s |

专项各含六个业务测试和 setup/cleanup。TSAN 沿既有环境使用
`setarch x86_64 -R`，未新增 suppression、放宽产品期限或关闭测试。完整 C++ 格式、
修改的私有头文件格式、67 个公开头边界（0 违规、2 个既有 DEC-096 缝）、本地
Markdown 链接及 `git diff --check` 通过。表内耗时为 CTest 实际运行时间，不含编译。

三轮最终真实 OCR 复验均通过四项检查：存在成功标定、零标定失败、零旧标签
active、同 entity/instance 移动后标签继承。每轮 59 帧、10 次实际分析、49 次
静态跳过、0 分析失败，脚本标定各 9 次。出现内容变化时先记录 suspended，再使用
新引用标定；示例 SEARCH 从 x=99 移至 x=108 保留标签，DELETE 使旧 SEARCH 标签
挂起。每轮窗口约 15 秒，峰值 RSS 506768～507644 KiB；这不是长期产品平台期或
配对资源收益证明。验证无后台 VLM、逐帧 PNG 或真实桌面输入。

默认 Debug/三个 sanitizer 使用现有配置；native-release 保留 PP-OCR/YOLO 与既有
模型清单，后台 detector 仍默认关闭。模型循环验证使用脚本 provider，真实视觉
验证使用私有 X11 session shim，二者都不能证明在线模型会正确自主标定。

## 限制与补跑

负责人 Mirage 维护者。真实 GNOME/KDE Portal、Windows/MSVC、在线模型自主标签
质量、长期低端资源和四类候选全覆盖仍需目标环境与可复位任务集；本机私有 X11
不能代替。未安装或重启用户产品，不触碰真实业务联系人。M9-13 和整个 M9 继续
In Progress；本轮不宣称硬实时、任意短提示必达或跨应用永久身份。
