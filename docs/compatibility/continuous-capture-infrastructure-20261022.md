# M9 连续采集基础层验证记录

> 状态：Completed（本轮基础层自动化验证，非 M9 整项验收）
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M9-06/07/11/12
> 依据：[DEC-091](../decisions/DEC-091-local-semantic-perception-and-action-loop.md)、
> [DEC-109](../decisions/DEC-109-continuous-perception-attention-and-pet-coexistence.md)、
> [基础层契约](../design/continuous-capture-infrastructure.md)

## 环境与范围

基线 Mirage `01b4308`，工作分支 `feat/continuous-capture-infrastructure`，结果针对
本轮未提交工作树。Linux x86_64，内核 `7.0.0-38-generic`，GCC 13.3.0；Mira
`a29374aaf05b956cbd5ceeafc65bba4c6172675e`，Mirador
`5565436c3770b53c2230e7b7083ca48e028368b4`。未修改依赖/pin，未启用产品消费者、
重启产品或执行模型请求。平台、授权及 capture 实现继续复用 Mirage 现有边界。

新增 `ContinuousCaptureHost` 使用外部 kairo Executor 的 blocking worker、periodic
timer 和 LatestMailbox，只有显式 demand 才取帧；完整像素经共享不可变 owner
供 Mirador 消费，无额外像素复制、编码或模型工作。需求失效/异常/超时拒绝晚到
结果，关闭幂等且回调自有资源在 worker 上释放。source_sequence 与接收时间保留
各自证明范围，需求 epoch 不充当平台授权/坐标变换。

## 自动化证据

专项使用真实 kairo worker/timer 和有界 fake capture；测试 owner 配置 2～4 线程、
64 队列与 64 总量 admission。图像为 8×8 BGRA、单帧预算 256 字节、两代预算
512 字节，周期 10 ms；单帧同址断言同时覆盖 provider vector、多个消费者和
Mirador view。64 代周转检查整个受管像素存量，不构成真实桌面 soak 或 RSS 数据。

覆盖：无需求零调用/无 timer、单 worker 串行与 tick 合并、代际撤销及晚到丢弃、
消费者/Mirador 派生视图占满两代时在 provider 调用前背压、释放后恢复、超时暂停
及显式重开、provider 拒绝/异常、布局/容量/格式拒绝、worker admission 拒绝、
stop 幂等、正常与 Executor 优先关闭、callback 自有状态的 worker 析构，以及
共享帧进入既有 VisualSessionHost 后第二个静态帧跳过 OCR（实际 backend 调用 1 次）。

最终结果如下；定向 5 项含 3 个测试程序及状态隔离 setup/cleanup 两个夹具，
TSAN 的 3 项含专项程序和两个夹具。各配置采集专项均为 **166 checks、0 failures**，
visual_session 为 281 checks，budgeted 为 136 checks，均零失败。Debug 全量后
对最终有限 admission 夹具再执行相同定向组，不重复完整平台回归。

| 配置 | 实际结果 |
| --- | --- |
| Debug 全量 | 109/109，通过；292.91 s |
| Debug 最终定向 | 5/5，通过；5.92 s，采集、visual_session、budgeted 与隔离夹具 |
| native-release 定向 | 5/5，通过；5.91 s，编译启用现有 PP-OCR/bundled vision，测试不加载模型 |
| ASAN 定向 | 5/5，通过，无 sanitizer 报告 |
| UBSAN 定向 | 5/5，通过，无 sanitizer 报告 |
| TSAN 定向（子进程关闭 ASLR） | 3/3，通过，无数据竞争报告 |
| 格式/公开头边界 | 通过，65 个公开头零新增违规；保留 DEC-096 两条已登记 seam |
| 文档与差异 | 本地链接、围栏/标题、M9 唯一编号与开放状态，以及 git diff --check 通过 |

可复现命令：

```sh
cmake --preset debug
cmake --build --preset debug -j 4
ctest --preset debug
cmake --build --preset debug --target continuous_capture_test visual_session_test visual_session_budgeted_test -j 3
ctest --preset debug -R '^(continuous_capture_test|visual_session_test|visual_session_budgeted_test)$'

# 分别对 asan、ubsan、native-release 执行
cmake --preset asan
cmake --build --preset asan --target continuous_capture_test visual_session_test visual_session_budgeted_test -j 2
ctest --preset asan -R '^(continuous_capture_test|visual_session_test|visual_session_budgeted_test)$'

cmake --preset tsan
cmake --build --preset tsan --target continuous_capture_test -j 2
setarch x86_64 -R ctest --preset tsan -R '^continuous_capture_test$'

cmake --build --preset debug --target mirage-format-check mirage-boundary-check
git diff --check
```

首轮 TSAN 默认启动在测试主体执行前失败：`FATAL: ThreadSanitizer: unexpected
memory mapping`。仅对测试子进程禁用 ASLR（`setarch x86_64 -R`）后运行通过，
未改变系统全局设置，未使用 suppression；不得把默认启动的失败写作通过。
首轮 admission 负例曾将未显式初始化的 facade 误当成必拒绝；公开 facade 实际
允许惰性初始化，负例已改为重复 worker 注册的真实 admission 拒绝并复验。

## 尚未验收

M9-06/07/11/12 保持开放；本次只交付 opt-in 基础层，不勾选完整工作项。

| 未执行项 | 原因及补跑条件 | 负责人 |
| --- | --- | --- |
| native/IPC 文本优先与共享帧生产消费、权限/placement 联动 | 当前未接线；冻结 M9-05 供给与平台 revision 后在真实会话接入 | Mirage 维护者 |
| Backend 推送流按需求暂停、平台 buffer 归还及 damage | 基础层仅停止自己的取帧调用；需逐 Backend 接线与运行取证 | Mirage 维护者 |
| 静态退避、ROI/注意/事件/按需 VLM | 后续 M9-07/08/18 的策略及消费契约未交付 | Mirage 维护者 |
| 真实 4K/多源/慢消费者长期 soak 与低端设备 A/B | 尚无预登记设备/任务/阈值；按资源门禁登记后测 CPU、唤醒、RSS、拷贝和应用响应 | Mirage 维护者 |
| Windows、真实 Wayland compositor 与 pet 共存像素 | 此轮为 Linux fake 宿主及既有回归；对应机器/授权/平台实现到位后逐项实测 | Mirage 维护者 |

本轮无资源节省比例、持续 FPS、硬实时性或跨平台能力保证；两代字节断言只覆盖
宿主管理的像素分配，不含平台 buffer、额外派生转换或全产品内存。
