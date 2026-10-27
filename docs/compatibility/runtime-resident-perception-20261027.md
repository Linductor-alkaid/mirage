# Runtime 常驻感知与自主输入验收（2026-10-27）

> 状态：Completed（所列本机接线与验证；M9-22 继续 In Progress）
> 负责人：Mirage 维护者
> 工作项：M9-22；关联 M9-06/07/09/10/11/21
> 决策：[DEC-117](../decisions/DEC-117-runtime-resident-perception-and-autonomous-input.md)
> 基线：`f42d878`；分支 `feat/continuous-semantic-binding`；本地工作树，保留既有未提交修改

## 本轮行为

既有连续 capture → Mirador/PP-OCR → 有界对象/事件管道由 Runtime 长期持有。
任务结束仅释放独立租约，源继续采样；新任务读取当前状态及接入后的历史。
已有系统授权且产品 ScreenCapture=Allow 时静默预热，后台不能发起新授权窗口。
单任务 ACK/ROI/取消不改变其他消费者，输入权限也不跨任务继承。

三条普通指针路径（共享桌面、编号目标、ROI）取消像素/到达截图否决；权限、
几何、引用、取消/接管和释放检查保留。共享回执区分移动、press 派发和 release。
text_only OCR 保留已见输入意图并停止重复供给旧图；普通连续动作不强制后置帧。
显式后条件依旧跨过实际 raw 水位取得证据，输入完成本身不证明业务成功。

`vision.state` 读取已有状态；`vision.wait` 在有界 Topic 上等待事件、gap、取消或
超时，不触发 capture/OCR/编码/模型调用。背景仍采集图像，变化门控决定是否 OCR，
没有声称事件可替代图像输入，也没有交付平台 damage 或静态零取帧。

## 覆盖与口径

- resident 单测：无任务时继续采样、最新帧复用、独立消费者、只读禁止输入、撤权、
  重建、terminal stop、worker admission 拒绝、前台降级、异常与在途 shutdown。
- 模型/工具接线：悬停/重绘继续派发、OCR 后连续输入、super 别名、几何/权限/取消/
  接管/释放失败、后条件水位、window capture_scope 隔离和事件等待零额外取帧。
- 静态对象：源仍推进但 OCR 静态跳过时保留 advisory 对象；无来源进展仍过期；
  原分析年龄保持真实，不能延长旧成功证据。
- Portal fixture：无恢复 token 的 silent capture 返回 desktop_authorization_required，
  不产生新的 CreateSession。fixture 不等于真实 compositor 长任务验收。
- 回执 v4、修改工具 v5；UI/会话投影兼容历史 v2/v3 与 v4，不能把已执行显示成已验证。

40 次默认点击夹具的源 capture 计数从旧路径 121 降至 41；这是初始图加每次动作
由夹具显式请求的反馈，不是在线任务统计。无后条件 ROI 动作夹具不增加 capture；
后台持续采样另计。不能据此宣称模型 token、实际误点率或完成率收益。

## 私有显示与真实 OCR

在隔离 Xvfb / D-Bus / XDG 目录运行实际 Linux capture、生产 ResidentDesktopPerception
及 PP-OCR，未操作用户桌面。命令：

```sh
/usr/bin/python3 tests/manual/continuous_perception_acceptance.py --binary build/native-release/tests/continuous_perception_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --output /tmp/mirage-m9-22-private-ocr.json
```

原生 Release 开启现有 PP-OCR/YOLO 构建选项，后台 detector 仍默认关闭。测试期间
113 帧、58 次分析、55 次跳过、0 分析失败、0 drop；释放首任务后重接看到 115 帧，
`resident_continued_after_task=true`。0 VLM、0 PNG 编码。事件峰值 18512 B，预览峰值
7864320 B，测试进程峰值 RSS 529860 KiB；包含 OCR 模型，不能当长期产品资源平台期。

| 提示持续时间 | 样本 | 检出 | 消失前确认 |
| --- | --- | --- | --- |
| 100 ms | 6 | 2 | 0 |
| 500 ms | 6 | 6 | 5 |
| 1500 ms | 6 | 6 | 6 |

100 ms 提示会漏报，检出也可能晚于消失。日志保留其 observed_unconfirmed 语义，
不能冒充当前屏幕仍存在。该短时实验只证明链路接通及任务结束后继续运行。

## 开发期失败与修复

完整回归首先发现三个旧用例仍要求像素变化必须拒绝、工具版本仍期望 v4；已按
DEC-117 改成派发正向用例，保留权限/几何/取消/释放负向矩阵。v4 回执初版漏接
会话投影解析器，导致 not_requested 展示丢失；已增加兼容并同步 v4 golden，历史
v2/v3 夹具保留。TSAN 发现两处测试时序假设：失败重试可能已有在途预算预留，
两个消费者接入游标也可能不同；分别改验预算边界及双方接入后的事件。
这些失败轮次不计为通过，没有新增 sanitizer suppression 或放宽产品期限。

## 验证结果

最终矩阵、准确命令和源/产物摘要见
[结构化证据](data/runtime-resident-perception-20261027.json)。环境为 Linux x86_64、
Intel Core Ultra 5 225H、GCC 13.3、CMake 3.28.3；native 使用现有 Release 配置。

| 验证 | 结果 | 实际 CTest 耗时 |
| --- | --- | --- |
| Debug 全量回归 | 113/114；剩余 GUI 旧断言修正后单项及 setup/cleanup 3/3 通过 | 78.65 s + 14.45 s |
| native-release 专项 | 9/9（7 个业务测试及 setup/cleanup） | 103.72 s |
| ASAN 专项 | 9/9；消费者时序断言改进后 task 专项再过 3/3 | 61.66 s + 2.61 s |
| UBSAN 专项 | 9/9；消费者时序断言改进后 task 专项再过 3/3 | 58.65 s + 2.32 s |
| TSAN 专项 | 8/9；仅消费者游标时序断言失败，修正后 task 专项 3/3 | 110.29 s + 2.87 s |

以上按真实轮次记录，不把全量失败改写为单轮全绿；每个失败用例均已修正并复验，
最终无未解决失败或 skip。sanitizer 专项共 7 个业务测试：task_perception、
perception_semantics、continuous_capture、desktop_image_supply、desktop_action_policy、
post_action_verification、visual_marks。TSAN 使用既有 `setarch x86_64 -R`。
测试完仅对回执投影及消费者测试做 clang-format 排版，另重建原生产品及复验投影相关测试。
投影专项 4/4（19.94 s）通过；最终受影响源码摘要为
`47d604bd31754022ddc4f38c2ea7b3cf0a65b36b9be3a024d0f3796de9827723`。
完整格式检查、4 个修改的私有头格式检查、67 个公开头边界（0 违规，2 个既有
DEC-096 缝）、482 个本地 Markdown 链接和 git diff --check 通过。

## 未执行验证与补跑条件

负责人均为 Mirage 维护者；M9-22 与关联里程碑保持 In Progress。

| 项目 | 当前限制 | 补跑条件 |
| --- | --- | --- |
| 真实 GNOME/KDE Portal 常驻、恢复及长任务 | 本轮仅受控 Portal fixture 与私有 X11 | 可复位的目标 compositor、已授权共享及长任务集 |
| Windows/MSVC | 本机为 Linux，未构建运行 | Windows runner 与对应 ScreenProvider/Input Backend |
| 低端 CPU/RSS/功耗与长期静态资源 | 仅短时 OCR 实验，没有配对基准 | 目标低端硬件、固定工作负载及长时间测量 |
| 在线模型误拒/误点/完成率、token A/B | 脚本 provider 不能代表真实模型 | 固定模型、可重置 GUI 任务与两版对照 |

本轮未重启用户正在运行的服务，修改随新构建的服务下次启动生效；没有升级 pinned
依赖、安装产品、合并或发布。本地工作树包含本轮开始前的修改，证据摘要按实际整体
受影响源码记录，不把此前修改归为本轮新增。
