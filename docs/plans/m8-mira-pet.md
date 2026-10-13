# M8：mira_pet 常驻状态展示

> 状态：In Progress
> 负责人：Mirage 维护者
> 所属计划：[Mirage 实施总计划](mirage-implementation-plan.md)
> 前置：M1 Runtime/IPC、M6 的 DEC-045 owner 拓扑、M7 桌面工具与动作执行缝；不要求关闭这些里程碑的全部开放门禁
> 建议发布点：Linux X11 v5 九态桌宠预览版（标签未定；启动/退出使用 v5 已确认动作）；最终动作与其他平台分别验收
> 更新日期：2026-10-13

## 目标

提供独立 `mira_pet` 进程。在 Mirage 主窗口被遮挡、最小化、关闭或退至后台时，
继续在正常桌面顶层展示 Mira 的状态、任务阶段与结果，让用户知道 Agent 在做什么。
五个运行状态按实际动作分别截取持续阶段，持续期间只循环该片段；成功/失败单次播放，避免反复举起工具、
进入姿势或恢复待机。只读状态、IPC、播放时钟和可选 Linux 本地桌宠预览已经接入；
安装载荷、目标避让、跨平台与完整实机会话仍在实施。

## 范围与非目标

范围包括独立进程、真实状态投影、Local IPC 订阅/恢复、透明置顶窗口、分段播放器、
逐状态素材处理、非抢焦/输入穿透、多屏/DPI、桌面目标避让、进程关闭/故障、设置与打包。
覆盖启动、待机、思考、看、鼠标、键入、任务成功、任务失败、退出；暂停、等待确认、
人工接管、取消和断连可先采用安全静态姿态，详细原因由主窗口和诊断表达。

第一交付点为 Linux X11 的 v5 九态素材预览。维护者已确认使用 looking/typing，
并交接确认版启动/退出。维护者已确认七态候选区间各 20 圈接缝；实际窗口效果和其他平台仍有独立验收。XWayland、原生 Wayland 与 Windows 有独立门禁。

不重建 Agent/任务运行时，不另起 Mira、不替代主窗口完整轨迹/权限确认，不新增模型
直连或角色音频，不修改 pinned 依赖，也不把生成新素材作为本轮已授权实施。

## 设计与决策依据

- [mira_pet 设计](../design/mira-pet.md)：状态、进程、播放、平台和依赖路由。
- [DEC-084 提案](../decisions/DEC-084-mira-pet-process-and-animation.md)：独立窗口与分段动画；M8-01/02/04 冻结技术契约和暂定默认值。
- [DEC-045](../decisions/DEC-045-tray-runtime-owner.md)：托盘持有 Runtime 与产品子进程。
- [DEC-049](../decisions/DEC-049-conversation-process-rendering.md)：真实过程投影、终态和快照序号。
- [DEC-029](../decisions/DEC-029-desktop-overlay-carrier.md)：现有 Overlay，与桌宠分别承担目标提示与角色展示。
- [IPC v1](../design/mirage-ipc-protocol-v1.md)、[工程规范](../project/project-standards.md)。
- pinned kairo 的 [integration skill](../../third_party/mira/third_party/kairo/docs/skill/kairo-integration/SKILL.md)，scenario → communication 路由；公开 facade 与现有 Runtime owner 优先。

## 素材交接与当前库存

v5 素材包已入库至 `apps/pet/assets/`，包含九个主 WebP 文件及 provenance.json
来源记录。已核对 README、状态 JSON 与九个主 WebP 的 RIFF/ANMF 信息，SHA256 与
[v5 库存证据](../compatibility/data/mira-pet-assets-v5-20261012.json) 完全一致。
v5 替换了 enter/exit；其余七态主文件与 v3 摘要一致。
[v3 库存证据](../compatibility/data/mira-pet-assets-v3-20261012.json) 与
[v2 库存证据](../compatibility/data/mira-pet-assets-20261012.json) 保留为历史，不覆盖。

| 产品状态 | 当前来源 | 本轮状态 | 后续验收 |
| --- | --- | --- | --- |
| 待机 | `mira_rest.webp` | 已提供 | 单独截取待机/休息持续段 |
| 思考 | `mira_think.webp` | 已提供 | 单独截取思考持续段 |
| 看 / looking | `mira_look.webp`（v3） | 维护者已确认使用 | 逐帧裁切与接缝验收 |
| 鼠标 | `mira_mouse.webp` | 已提供 | 单独截取指针操作持续段 |
| 键入 / typing | `mira_typing.webp`（v3） | 维护者已确认使用 | 逐帧裁切与接缝验收 |
| 任务成功 | `mira_done.webp` | 已提供；历史接缝已验收 | 实际全片 once，随后持有末帧 |
| 任务失败 | `mira_fail.webp` | 已提供；历史接缝已验收 | 实际全片 once 后待机，同次失败不重播 |
| 启动 | `mira_enter.webp`（v5） | 维护者已确认使用 | 单次播放/打断及真实启动取证；v5 已交接，循环裁切另验 |
| 退出 | `mira_exit.webp`（v5） | 维护者已确认使用 | 单次播放/有界关闭取证；v5 已交接，循环裁切另验 |

七个运行状态的 [候选帧区间](../compatibility/data/mira-pet-loop-candidates-v5-20261012.json)
已按单独动作选择，维护者于 2026-10-13 确认各 20 圈接缝无视觉问题。
当前运行 [manifest](../compatibility/data/mira-pet-playback-v5-20261013.json) 为五态循环、
四态 once；历史七态候选和 [验收证据](../compatibility/data/mira-pet-loop-visual-acceptance-20261013.json)
保留。真实目标窗口效果尚未验收。enter/exit 按包内 once、done/fail 按维护者单次
播放要求执行，可按状态/退出期限打断；不能用容器 loop=0 重复播放。
容器全片无限循环、预览或 README 的声明不构成接缝验收。素材已固定至
`apps/pet/assets/`（含 provenance.json 来源记录），不再依赖用户下载目录。

## 工作项

工作项在验收门禁全部满足后才勾选，局部代码落地保持 In Progress。

- [ ] `M8-01` **In Progress**：冻结真实显示状态与最小只读 IPC 契约。核对 Mira/现有
  Adapter 的开始/结束事件，区分模型工作、观察、真实输入、核验与终态；提供服务端
  聚合快照、多任务选择、epoch/revision、能力探测和断连/溢出恢复。同步 IPC schema、
  golden vectors、公开头与决策。验收：真实阶段与 terminal reducer、竞态、容量及
  旧客户端兼容；pending 工具、工具重试和模型自称成功不得错误触发动作/终态动画。
- [ ] `M8-02` **In Progress**：九态 manifest 与资源验证实现已完成：源摘要、原始
  时长、半开区间、画布/锚点、intro/loop/outro 定义、once 与七态 20 圈时钟单测；
  运行时拒绝路径/区间/尺寸预算、摘要错误、损坏与中途取消，真实九态解码保留 alpha。
  运行工件采用原始 WebP + 显式 RGBA 帧，安装布局沿现有产品的 `bin/assets/pet/`，
  字体沿 `bin/assets/`。**接缝已验收**：维护者确认七态各 20 圈无视觉问题；
  实际 done/fail 改为 once。**现场进展**：维护者确认单屏 GNOME XWayland 中角色
  alpha、完整下半身可见；状态条按后续要求移除。跨目标窗口和其他平台的效果仍需
  M8-04/10 验证。
  素材许可和实际安装载荷由 M8-08 交付，不以本地构建复制代替安装验收。
- [ ] `M8-03` **In Progress**：播放器与异步加载实现已完成：纯时钟驱动、相同状态
  不重播 intro、loop/once、快切/安全状态/卡顿跳帧；复用现有 Executor 的单片
  可取消加载，保留/消费 future，仅保留最新目标，generation 拒绝迟到结果；读取/
  解码与缓存有界，2 秒请求期限、失败/拒绝、运行中取消与 shutdown 均有自动测试。
  最小化停止动画 timer 并取消加载，恢复重新加载最新状态、从首帧推进。
  已开始动画至少完成 intro 加一轮 loop / 全片 once 后才普通切换；只采用最新状态，
  加载期间已过时或首轮期间已结束的动作不补播，安全状态仍立即打断。
  失败 once 后自动进入待机，消费 owner/source 身份，通知刷新及隐藏恢复不重播。
  **现场进展**：维护者确认实际任务状态/成功失败动画与主窗口一致，状态切换无透明
  空帧；单屏 XWayland 可见渲染已通过。**剩余验收**：GPU/完整进程峰值预算、
  结果全片 once 的逐帧实机证据；
  其他隐藏形式在 M8-06 接入避让后验收，未用最小化结果冒充覆盖全部隐藏方式。
- [ ] `M8-04` **In Progress**：交付 Linux X11 的非抢焦透明置顶窗口与平台能力诊断。
  使用既有 UI 公共 API，必要 OS 适配仅在 Platform 边界；实现透明区域穿透、有限
  用户交互、工作区约束及位置/DPI 恢复。验收：实际 compositor/WM 下主窗口遮挡、
  最小化、关闭、切换应用、全屏、点击穿透及多屏；置顶请求不代替实际可见性证明。
  已接入最小化暂停/恢复，私有 X11 framebuffer 角色像素/alpha/实际帧推进已通过；
  维护者确认单屏 XWayland 中角色可拖动、主窗口关闭后持续显示且不抢焦；私有
  X11 fixture 验证透明角穿透与实体拖动。拖动/正常退出保存位置，重启按工作区恢复；
  窗口仅显示角色，并设置 Dock/任务栏与 pager 排除标记。全屏、多屏/DPI、原生 Wayland 和
  Windows 仍未验收。
- [ ] `M8-05` **In Progress**：提供托盘拥有的单实例桌宠生命周期。核验产品角色/PID/
  IPC owner，补启动 bootstrap 时序、重复入口复用、独立启停、故障诊断与正常退出
  有界收尾。验收：主窗口关闭不影响桌宠/任务，关闭或崩溃桌宠不影响 Mira，宿主失联
  停止陈旧动作并退出，取消/输入释放与 owner shutdown 不等待动画。
  已补隐藏关闭/异常失联、缺席 PID 信号回归、托盘独立启停/恢复入口、所属 PID
  readiness、10 秒 bootstrap 期限与产品故障状态；私有实进程 31/31。
  维护者确认主窗口关闭后桌宠仍显示且不抢焦，托盘独立停止/启动桌宠后任务和主窗口
  保持正常。完整 Agent 输入释放实机会话与其他平台门禁仍待 M8-07/10 取证，
  保持 In Progress；启停偏好持久化另属 M8-08。
- [ ] `M8-06` **Planned**：闭合桌宠与桌面输入/感知的冲突。身份严格核验、目标占位
  自动避让或有界隐藏、几何确认后重观察，与 Overlay 和既有安全守卫共存。验收：
  角色覆盖点击/拖拽/键入目标及截图/OCR 时不误点、不误识别、不放宽外来遮挡；
  避让失败与不可用区域明确拒绝，人工接管及时停动作并恢复可见状态。
- [ ] `M8-07` **Planned**：形成 Linux v5 九态预览的产品门禁。沿现有真实会话执行
  思考 → 看 → 鼠标/键入 → 看/核验 → 成功，以及工具可恢复失败、最终失败、暂停/
  确认、取消、接管；保留背景主窗口与桌宠同场景视频/截图及事件摘要。验收：核心
  故障注入与 ASAN/UBSAN、跨上下文/关闭 TSAN 门禁通过；启动/退出单次播放、期限
  打断与确认素材边界单列。
- [ ] `M8-08` **Planned**：交付可重复安装的桌宠载荷与外观设置。登记启停/大小/
  位置偏好、格式迁移，随原产品入口启动；受控素材工件来源/许可/摘要、离线构建和
  安装资源查找一致。验收：Linux 安装/升级/卸载、缺资源诊断、偏好恢复，安装环境
  不存在原下载目录时仍可运行；安装范围与代码运行范围分别记录。
- [ ] `M8-09` **In Progress**：完成启动/退出动作调整与最终九态素材收口。v5 已交接，
  looking/typing/enter/exit 已获维护者确认；源版本与摘要已核对。负责人：Mirage 维护者。
  仍需 M8-02/03/07 的相关裁切、单次播放、打断及真实状态切换验证，
  不将文件到位标作运行验收完成。
- [ ] `M8-10` **Planned**：逐环境验证 XWayland、原生 Wayland 与 Windows。
  分别核对置顶/透明/穿透、非激活、跨屏、目标避让、进程关闭与包安装；缺窗口层级
  能力的环境明确限制和产品降级，由维护者决定后续平台方案。验收：目标机器运行
  证据，不以 X11 或交叉编译结果冒充其他平台通过。

## 拆分与建议交付顺序

1. M8-01 与 M8-02 明确契约和素材；精确区间通过逐帧评审后进入播放器实现。
2. M8-03/04 完成渲染与窗口 PoC，M8-05 接入既有 owner；v5 启动/退出用于九态预览。
3. M8-06/07 先证明不干扰真实桌面操作及状态正确，再用 M8-08 交付安装预览。
4. M8-09 收最终九态，M8-10 按目标环境单独收口；各自保留未完成状态。

建议 MR 按“状态/IPC → 素材与播放器 → 平台窗口与进程 → 避让/产品/包装”拆分。
开发分支按仓库 `type/subject` 规则；具体实现开始时再创建，不混入当前 M7 工作树改动。

## 风险与阻塞

| 风险/阻塞 | 处置与负责人 | 解除或验收条件 |
| --- | --- | --- |
| 启动/退出已交接确认 | 维护者已交接 v5 调整版；M8-09 工作项，不阻塞 v5 预览 | 单次播放/打断及循环裁切验收到位 |
| WebP 帧控制已验证，供给与 GPU 预算未收口 | M8-02/03 原始 WebP + 显式 RGBA 帧；复用既有 libwebpdemux | 补真实 compositor/GPU 与视觉门禁；许可/安装由 M8-08 收口 |
| 显示阶段缺少实时起止缝 | M8-01 复用公开事件/Adapter，unknown 如实降级 | 不读私有状态，不虚构阶段；确属依赖缺口时登记反馈 |
| 置顶窗挡目标/污染截图 | M8-06 严格身份、避让/隐藏后新观察 | 不放宽输入守卫，拒绝路径与多屏可复现 |
| 平台层级或透明能力不满足 | M8-04/10，维护者提供目标环境 | 实机运行与限制记录；无能力时不标完成 |
| 子进程/资源关闭竞态 | M8-03/05/07，owner 持有 handle/future | 拒绝、异常、取消、超时、断连、shutdown 均显式结算 |
| 全片帧资源占用高、空闲持续唤醒 | 有界加载/纹理，隐藏停止 timer | 内存上限/资源释放及 CPU/GPU/切换延迟基线取证 |

## 测试矩阵

| 门禁 | 场景与证据 | 关联工作项 |
| --- | --- | --- |
| 状态与多任务 | 真实起止、pending/running、alias、观察核验、工具重试、终态幂等、取消/接管、显示归属 | M8-01/07 |
| IPC/恢复 | 能力缺席、快照/通知竞态、seq 缺口、Topic 溢出、旧 epoch、健康探测、重连、schema/fuzz 预算 | M8-01/05 |
| 动画 | 每运行状态不同区间、原始 duration、intro 一次、loop 至离开、outro 可打断、20 圈接缝、enter/exit once 不循环、调整版本 | M8-02/03/09 |
| 资源失败 | 文件损坏/缺失、路径逃逸、越界区间、过大帧、缓存满、加载异常/拒绝/取消、迟到 future | M8-02/03 |
| 窗口真实效果 | 背景/最小化/关闭主窗仍可见，透明/穿透、无抢焦、拖动、DPI/屏幕移除、全屏 | M8-04/10 |
| 桌面闭环 | 桌宠覆盖动作目标、截图/OCR、Overlay 共存、避让失败、重新观察、外来遮挡保持拒绝 | M8-06/07 |
| 并发与进程 | 正常完成、任务异常、提交拒绝、执行中取消、超时、shutdown、托盘故障/桌宠崩溃/重复启动 | M8-03/05/07 |
| sanitizer | ASAN/UBSAN 常规；TSAN 与关闭/背压故障注入 | M8-07 |
| 供给/包装 | 离线工件、来源/许可/摘要、无下载目录、设置迁移、Linux/Windows 安装更新与卸载 | M8-08/10 |

命令按实施时真实新增目标/ctest 名称记录，不先编造测试名和通过数。单测不能替代实机
可见性/焦点、动画接缝和目标避让门禁。性能测量在固定设备与场景记录基线后再冻结目标。

## 退出条件

- [ ] 九态来源、状态映射与七个运行状态独立循环区间冻结；v5 九态通过播放验收，启动/退出完成 once/打断验收。
- [ ] 主窗口遮挡/最小化/关闭期间状态、阶段和归属正确；失败、取消、接管和断连不显示陈旧自主动作。
- [ ] 桌宠独立进程由在线托盘 owner 持有，单实例、准入、崩溃/失联/正常退出及 Executor 关闭均闭合。
- [ ] 已声明平台实测置顶、alpha、非抢焦、穿透、DPI/跨屏及桌面动作/感知避让，不能用其他平台证据补勾。
- [ ] 测试矩阵的适用自动化、真实场景、sanitizer 和故障注入均通过并留可复现证据。
- [ ] 资源预算、供给许可与摘要、包装/升级/偏好迁移、设计/IPC/决策/使用说明同步。

v5 九态 Linux 预览可作为局部交付，但不关闭跨平台整体 M8；发布说明须列
未验平台与已知置顶限制。

## 验证记录

### 2026-10-12：需求立项与本地库存核对

- 范围：建立 M8、DEC-084 与独立设计，同步总计划和架构设计入口；无产品实现、素材
  导入或动画裁切。工作树已有 M7 未提交修改，本轮在其基础上做范围化文档增量。
- 基线：`adfe65b005547e8c13a9df3523e2f589afade25d` 工作树；本地 Linux 源目录，
  日期按维护者 Asia/Shanghai 环境 2026-10-12 记录。运行 Python 3 标准库读取容器。
- 实际核对：README/JSON/预览、七个主动画的文件大小、画布、ANMF 帧数/时长、ANIM
  循环数与 SHA-256；七个均 512×512、50～74 帧、loop=0。原始数据见库存 JSON。
  精确全片时长与 README 理想 fps 的近似值不同，播放器须用原始 duration。
- 复现库存核对：在源目录执行下面的只读命令；不运行包内自带 `webpmux.py`。

```bash
python3 - <<'PY'
import hashlib, json, pathlib, struct
root = pathlib.Path.home() / '下载' / 'mira_pet_v2_H3连续动作'
for path in sorted(root.glob('mira_*.webp')):
    data = path.read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WEBP'
    pos, durations, canvas, loop = 12, [], None, None
    while pos + 8 <= len(data):
        kind = data[pos:pos+4]
        size = struct.unpack_from('<I', data, pos+4)[0]
        chunk = data[pos+8:pos+8+size]
        if kind == b'VP8X':
            canvas = [int.from_bytes(chunk[4:7], 'little')+1,
                      int.from_bytes(chunk[7:10], 'little')+1]
        if kind == b'ANIM':
            loop = int.from_bytes(chunk[4:6], 'little')
        if kind == b'ANMF':
            durations.append(int.from_bytes(chunk[12:15], 'little'))
        pos += 8 + size + (size % 2)
    print(json.dumps({'file': path.name, 'bytes': len(data), 'canvas': canvas,
        'frames': len(durations), 'duration_ms': sum(durations),
        'frame_duration_ms': sorted(set(durations)), 'container_loop_count': loop,
        'sha256': hashlib.sha256(data).hexdigest()}))
PY
```

- 文档检查：Python 3 标准库检查五份相关 Markdown 的 257 个相对链接、一级标题、
  围栏与尾随空白，核对十项唯一编号/未勾选状态、库存 JSON 和七份原素材摘要；通过。
  `git diff --check` 退出码 0。此结果仅证明文档和库存一致，不代表产品测试通过。
- 未执行：逐帧 loop 区间选择、动画接缝/alpha 验收、代码构建/ctest/sanitizer、X11/
  Wayland/Windows 的窗口与桌面任务验收及包装。原因：本轮为计划，程序与正式素材
  工件尚未实施；负责人：Mirage 维护者；补跑条件：对应工作项实现、最终素材及目标
  平台可用后，按上述矩阵执行并在各条记录准确命令与结果。所有实施清单仍未完成。

### 2026-10-13：M8-02/03 播放器与状态投影单测完成

- 范围：实现 `AnimationPlayer` 核心逻辑、九态 manifest、状态投影与 IPC 集成，
  补充单元测试和集成测试；不包含资源加载、窗口渲染、真实透明效果和桌面避让。
- 基线：工作树在 M7 未提交修改基础上增加 M8 pet 模块。
- 实际实现：
  - `runtime/pet/include/mirage/runtime/pet/animation.hpp`：九态 manifest（v5 九个
    状态、原始帧时长、0-based 半开区间、画布/锚点）、`AnimationClip`（intro/loop/outro
    区间、once 语义）、`AnimationPlayer`（状态机驱动、注入时钟、安全降级）。
  - `runtime/pet/src/animation.cpp`：播放器实现（相同状态不重播 intro、loop 仅播
    manifest 区间、按原始 duration 推进、时钟倒退保护、快速状态切换、静态后备）。
  - `runtime/pet/include/mirage/runtime/pet/status.hpp` + `src/status.cpp`：从 IPC
    `PetStatusSnapshot` 计算显示状态（思考/观察/输入/成功/失败/取消/接管映射、
    terminal reducer、pending 工具不触发动作、归属和 epoch 幂等）。
  - `tests/runtime/pet_status_test.cpp`：状态映射单测（9 个场景、竞态、多任务）。
  - `tests/runtime/pet_animation_test.cpp`：播放器单测（注入时钟、intro/loop/outro、
    七个循环状态各 20 圈接缝、once 不循环、快速状态切换、安全降级、时钟倒退保护）。
  - `tests/runtime/pet_status_integration_test.cpp`：端到端集成测试（真实会话、
    deadline 超时 vs 用户取消、工具重试、多任务、取消竞态、IPC 发布）。
  - 修复 `runtime/service/src/runtime_service.cpp:180`（`settle_dialog_turn`）：
    超时显示为 `task_failure` 而非 `cancelled`（根据 `record.cancel_requested`
    而非 `completion.cancelled` 设置 `record.was_cancelled`）。
  - 修复 `integration/mira/src/desktop_shared_input.cpp:259`（`finish_native_publish`）：
    M7-19 guard 帧发布参数顺序错误（`source_sequence` 和 `epoch` 调换）。
  - 修复 `integration/mira/src/desktop_action_verification.cpp:440-460`：
    `target_pixels_changed` 错误的 `refreshed_image_scope` 字段未出现在 receipt 顶层。
    为 `failure_receipt` 添加 `refreshed_scope` 参数，从 provider 的 `safe_message`
    中提取并写入 receipt 顶层（与 `error_code` 同级），使 model 可直接读取。
    同时提升内嵌 JSON 截断上限至 256 字节，为顶层字段留出空间。
- 验证结果：
  - 全套 7 项测试 100% 通过：`pet_status_test`（59 checks）、
    `pet_animation_test`（59 checks）、`pet_status_integration_test`（933 checks，
    含超时 vs 取消、工具重试、多任务、IPC 发布）、`desktop_image_supply_test`（891 checks，
    验证 `refreshed_image_scope` 字段出现在 receipt 顶层）、`native_agent_integration_test`
    （406 checks）、`tray_runtime_test`（152 checks）、`runtime_service_test`。
  - UBSAN 构建通过，`git diff --check` 退出码 0。
- M8-02 已完成：v5 九态 manifest（SHA/区间/锚点）、intro/loop/outro 定义、
  once 语义、七个循环状态 20 圈接缝单测、looking/typing/enter/exit 源确认（维护者 v5）。
- M8-02 待完成：区间/路径/尺寸预算负例（需资源加载器）、alpha 真实渲染验收（需窗口）。
- M8-03 已完成：`AnimationPlayer` 核心实现、相同状态不重播 intro、loop 仅播 manifest
  区间、按原始 duration 推进、注入时钟单测、快速状态切换、安全降级、once 不循环、
  时钟倒退保护。
- M8-03 待完成：真实透明渲染、CPU/GPU/缓存预算、资源异常和提交拒绝（需异步加载器）、
  迟到资源/帧卡顿（需 Executor 集成）、隐藏/恢复与停止（需窗口生命周期）、
  冻结帧资源布局（需确定解码方案：WebP 或离线 PNG）。
- 未执行：资源加载器、窗口渲染、透明穿透、置顶、桌面避让、真实会话端到端、
  X11/Wayland/Windows 实机验收、包装安装。原因：M8-02/03 为播放器与状态投影，
  窗口和资源加载由 M8-04/05/06 承载；负责人：Mirage 维护者；补跑条件：窗口系统、
  资源加载器和真实桌面任务集成后按矩阵执行。M8-02/03 工作项保持 In Progress。

### 2026-10-13：v5 素材入库至项目目录

- 范围：将 v5 素材包从 `~/下载/` 固定至项目 `apps/pet/assets/`，验证 SHA256 一致性，
  添加 provenance.json 来源记录，更新 M8 计划文档。
- 实际操作：
  - 创建 `apps/pet/assets/` 目录并复制九个 v5 WebP 文件。
  - 验证复制后的 SHA256 与 v5 库存文件完全一致（9/9 匹配）。
  - 创建 `apps/pet/assets/provenance.json` 记录素材版本、来源、库存路径和验证状态。
  - 更新 M8 计划素材交接段落：从"留在原目录"改为"已固定至 `apps/pet/assets/`"。
- 验证结果：
  ```bash
  cd apps/pet/assets && sha256sum mira_*.webp | sort
  # 输出与 v5 库存的 9 个 SHA256 完全匹配
  ```
- 后续工作：M8-08 安装布局需要将这些文件打包进 `bin/assets/pet/`；当前为源码树固定，
  不替代安装验收。

### 2026-10-12：v3 素材交接与使用状态更新

- 依据：维护者确认当前 v3 typing/looking 使用；启动与退出动作临时使用、后续待调整。
  当前源目录更新为 `~/下载/mira_pet_v3_桌宠素材包/`；保留上文 v2 库存与历史限制。
- 实际核对：README、状态 JSON 与九个主 WebP 的容器尺寸、帧数/时长、循环数和
  SHA-256；looking/typing 的摘要变化、enter/exit 新增、其余五态主文件摘要未变。
  looking 为 96 帧/9600 ms，typing 为 74 帧/5254 ms；enter 为 54 帧/4482 ms，
  exit 为 52 帧/5200 ms。九个容器均 loop=0，enter/exit 的 once 必须由播放器控制。
  [v3 原始库存](../compatibility/data/mira-pet-assets-v3-20261012.json) 独立保存。
- 状态同步：M8-09 从 Blocked 改为 Planned，原缺素材阻塞解除；当前九态均可用于
  预览规划。M8-02/07 改为九态，looking/typing 不再列待替换，启动/退出保留调整项。
  M8、全部实现工作项和精确循环区间仍未完成，不把源动作确认等同于播放验收。
- 复现：上文只读 Python 容器检查命令将 `root` 改为
  `pathlib.Path.home() / '下载' / 'mira_pet_v3_桌宠素材包'`；摘要复核命令如下。

```bash
python3 - <<'PY'
import hashlib, json, pathlib
record = json.loads(pathlib.Path(
    'docs/compatibility/data/mira-pet-assets-v3-20261012.json').read_text())
root = pathlib.Path.home() / '下载' / 'mira_pet_v3_桌宠素材包'
for row in record['files']:
    assert hashlib.sha256((root / row['file']).read_bytes()).hexdigest() == row['sha256']
assert len(record['files']) == 9 and not record['missing_states']
print('v3 source hashes: 9/9')
PY
```

- 未执行：逐帧解码/alpha、循环裁切/接缝及 once 实际播放、程序构建/平台运行。
  原因：本轮为素材状态与规划更新；负责人：Mirage 维护者；补跑条件：实施 M8-02/03/07，
  再按既有矩阵执行。启动/退出最终动作确认仍由 M8-09 承接。
- 文档与库存验证：Python 3 标准库核对五份 Markdown 的 261 个相对链接、标题/
  围栏/空白、十项 Planned 编号与未勾选状态、九份源摘要和容器帧时长，通过；v2
  库存保留。`git diff --check` 退出码 0；不构成动画/产品运行通过证据。

### 2026-10-12：v5 素材交接与 M8 pet 状态投影修复

- 依据：维护者交接 v5 素材包（`~/下载/mira_pet_v5_桌宠素材包/`），`mira_enter.webp`
  和 `mira_exit.webp` SHA 均与 v3 不同，维护者已确认作为正式使用版本，M8-09
  阻塞项解除。其余七个动态文件 SHA 与 v3 一致。
- 实际核对：九个 WebP 的容器属性（512×512、loop=0、帧数/时长/SHA-256），
  enter 为 54 帧/4482 ms，exit 为 52 帧/5200 ms，与 v3 帧时长一致但内容已替换。
  [v5 库存](../compatibility/data/mira-pet-assets-v5-20261012.json) 独立保存，v3/v2
  保留历史。
- 代码修复：同期发现并修复两个独立 Bug：
  1. `runtime/service/src/runtime_service.cpp:180` — `settle_dialog_turn` 用
     `record.cancel_requested`（用户主动取消）替代 `completion.cancelled`（超时与
     取消均为 true）设置 `was_cancelled`，使 deadline 超时显示 `task_failure` 而非
     `cancelled`。`native_agent_integration_test` 中 `last_caller_cancelled` 语义不变，
     deadline 仍触发 cancel 信号给 provider；变的是 pet 投影状态。
  2. `integration/mira/src/desktop_shared_input.cpp:259` — `finish_native_publish`
     调用的 `source_sequence` 与 `epoch` 参数顺序调换（M7-19 guard 帧发布修复）。
  3. `integration/mira/src/desktop_action_verification.cpp:445` — canonical 回执
     中内嵌错误 JSON 的截断上限从 128 字节提高到 256 字节，使 `refreshed_image_scope`
     字段不被截断（M7-19）。
- 自动验证：Debug 构建 7 项核心测试全部通过（pet_status_test、
  pet_status_integration_test、native_agent_integration_test、tray_runtime_test、
  runtime_service_test、ipc_protocol_test、ipc_protocol_golden_test）；
  `ctest` 7/7，0 failures。
- 未执行：逐帧解码/alpha、循环裁切与接缝验收、实机播放、M8 程序实现。
  负责人：Mirage 维护者；补跑条件：实施 M8-02/03 后按既有矩阵执行。
- 文档检查：`git diff --check` 退出码 0；不构成动画或产品运行通过证据。

### 2026-10-12：v5 分段候选与首批播放器实现

- `tools/audit_mira_pet_v5.py` 以标准库核对 9 份源 WebP 的 SHA-256、RIFF 长度、
  512×512 画布、ANIM loop=0、ANMF 帧数与原始整数毫秒时长，并验证 0-based 半开
  区间；`--preview-dir` 可用 Pillow 输出各循环段 GIF 供人工查看，不改源文件。
  实际执行 `python3 tools/audit_mira_pet_v5.py --source-dir
  /home/linductor/下载/mira_pet_v5_桌宠素材包 --preview-dir /tmp/mira-pet-preview`，
  9/9 摘要及区间通过。候选区间见
  [manifest](../compatibility/data/mira-pet-loop-candidates-v5-20261012.json)。
- `runtime/pet` 增加纯时钟播放器：同一 owner/显示状态/工作 ID 的新 revision 不重播
  intro，新工作可重新进入；7 个持续段仅重复各自 loop；v5 enter/exit 到末帧后停止，
  安全状态立即退到静态待机帧和独立文字。它不管理线程、计时、资源或纹理。
- 修正状态投影的阶段用时：并行活动数变化只增加 revision，不重置当前所选阶段的
  `elapsed_ms`；加入回归断言。
- Debug `pet_status_test`、`pet_animation_test`、`pet_status_integration_test` 已通过，
  后者含真实 Runtime/IPC 的完成、失败、用户取消、deadline、接管和关闭路径。
  当前 headless 状态刷新由 `pet.status` 读取驱动；托盘用既有 500 ms 产品维护周期。
- 未执行：20 圈人工接缝、素材输出工件、真实解码渲染、独立窗口/进程、自动避让、
  ASAN/UBSAN/TSAN、X11/Wayland/Windows 实测及包装。原因：对应工作项尚在实施；
  负责人：Mirage 维护者；补跑条件：M8-02～08 对应组件落地并取得目标环境。

### 2026-10-12：可选 Linux 桌宠预览与退出链路

- 预览构建选项 `MIRAGE_ENABLE_PET_PREVIEW=ON` 接受维护者本机 v5 源目录，CMake
  对九份 WebP 逐个核 SHA-256 后复制到构建目录。普通构建默认关闭；正式安装资源
  尚未引入，不能从用户下载目录发布。libwebpdemux 解码当前片段并提供 RGBA 帧；
  `pet_frames_test` 实际解码九态，播放器按各自候选区间选帧。
- 托盘 owner 启动独立 `mira_pet`，入口核对 tray、pet.status 能力及自身 PID；窗口
  经 EUI/GLFW 请求透明、置顶与鼠标穿透。GNOME Wayland 宿主的 XWayland 窗口实测
  `240×274`，`_NET_WM_STATE_ABOVE`，`WM_HINTS.input=False`，启动前后 root
  `_NET_ACTIVE_WINDOW` 保持 `0xe00004`。这仅证明该组合的窗口属性与焦点，
  compositor 截图无法可靠读取 XWayland 透明窗口像素，未据此宣称画面可见/alpha
  或全屏置顶通过。
- Linux 托盘正常关闭时经平台信号请求桌宠一次性播放 5200 ms 的 v5 exit；
  外部 Runtime owner 最多等待 6 秒，再走强制回收。隔离 socket 与状态目录实测
  托盘退出码 0、关闭耗时 5.49 秒，无残留桌宠进程；输入焦点仍在原窗口。异常
  断连目前显示“连接中断”并在 5 秒后退出，重连与崩溃恢复仍待实施。
- Dialog 的模型请求与工具 running 边界驱动 `pet.updated` 即时投影；注册工具名
  映射观察、鼠标和键入，未识别工具降级“执行中”。当前映射表示工具 handler
  已开始，授权/目标重验仍可能随后拒绝；真实输入派发点与工作流子阶段还需
  M8-01 补齐，不将此作为真实九态链路通过证据。
- 候选接缝的缩至 128×128 RGBA 平均通道差值依次为 rest 1.05、think 1.38、
  look 0.64、mouse 1.89、typing 0.64、done 1.03、fail 0.96；仅供定位明显跳帧，
  不能替代原尺寸 20 圈人工观看与实机播放。
- 20 圈接缝预览工件已生成：`tools/audit_mira_pet_v5.py` 新增 `--preview-loops N`，
  对七个 loop 状态在原 512×512 画布把各自循环段重复 20 圈合成 mp4（浅灰底合成，
  按原始毫秒时长的精确分数帧率，如 typing 1000/71）。实际执行
  `python3 tools/audit_mira_pet_v5.py --source-dir
  /home/linductor/下载/mira_pet_v5_桌宠素材包 --preview-dir
  /tmp/mira-pet-preview-20 --preview-loops 20`：九态 SHA 与区间审计通过；
  ffprobe 复核七个文件帧数（rest 720、think 220、look 1340、mouse 220、
  typing 480、done 160、fail 260）与时长全部一致。工件在临时目录、可再生、
  不入仓库；目视验收由维护者执行，通过前 M8-02 的接缝门禁保持未验收。
- Debug 运行 `ctest --test-dir build/native-debug -R
  '^(pet_status_test|pet_animation_test|pet_frames_test|pet_status_integration_test|tray_runtime_test|ipc_protocol_golden_test)$'
  --output-on-failure`，含状态隔离 setup/cleanup 共 8/8 通过。集成测试在没有托盘
  500 ms 维护周期时仍收到 thinking 的 `pet.updated`，确认即时边界。ASAN 同范围
  去掉仅 native 的 `pet_frames_test`，`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`
  下 7/7 通过（已重建并补跑即时事件断言与 owner 收尾改动）；UBSAN 同范围
  `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` 下 7/7 通过，并已重建复核。
  TSAN（preset tsan，同五个目标）在 `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`
  下经 `setarch $(uname -m) -R` 关闭 ASLR 后 7/7 通过，LastTest.log 无任何
  ThreadSanitizer 报告；直接运行因本机内核 ASLR 布局触发
  "FATAL: ThreadSanitizer: unexpected memory mapping" 全体失败，属环境限制，
  已注明，不作为被测代码缺陷证据。
- 未完成：20 圈视觉与真实任务视频、真实鼠标/键入派发时序、桌宠遮挡目标时避让与
  截图/OCR 排除、资源加载异步化及峰值 GPU 预算、设置/安装、Wayland 原生/Windows
  验证。负责人：Mirage 维护者；补跑条件：M8-01/02/03/06/07/08/10 各项实现或目标
  环境就绪。未通过的门禁仍保持 In Progress。

### 2026-10-13：M8-02/03 资源与呈现实现收口，推进 M8-04/05

- 基线：`86b6778` + `feat/m8-pet-resource-loading` 工作树；Linux 7.0.0-38 x86_64、
  GCC 13.3.0、libwebpdemux 1.3.2，依赖 pin 未修改。详细命令、范围与失败历史见
  [资源与生命周期证据](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。
- 实现：UI 不再读取/解码源文件；进程既有 Executor 管理单片可取消加载，一项 future
  与一项最新目标，generation 排除迟到结果，旧片释放后才接纳新片。运行时 SHA
  重验复用 Mira，路径/区间/文件/字节/画布/帧数/时长预算均拒绝负例。源保留 WebP，
  明确原生产品相对载荷目录与字体许可副本，asset tests 可以脱离 UI 构建。
- 实际修复：`BUG-20261013-002` 修复三个临时 string 导致的悬空 string_view；
  `BUG-20261013-003` 修复缺席/已回收 child 的 `kill(0)` 进程组信号。另确认
  `EUI-20261013-001` / `BUG-20261013-001`：RGBA/BGRA 转码未推进目标指针；
  2×1 公开 API 复现返回 true 但像素损坏。单一 UI Adapter 使用 EUI 公开 GPU
  导入/texture/revision，固定一份 512×512 RGBA8 主纹理 1 MiB，按 GPU fence 与
  onShutdown 生命周期清理；EUI 独立台账保持 Open，未修改 pinned 代码。
- 后续阶段：M8-04 最小化时取消加载、清除 UI 解码结果并暂停动画 timer，恢复后
  从最新状态与原时钟推进；M8-05 隐藏时由健康 timer 经独立 X11 连接通知 UI 正常
  关闭，跳过退出动画，异常 owner 失联同样收敛。正常/已失败启动/已回收/重复
  关闭/单实例/有界停止增加进程回归。
- 验证：Debug 六个相关目标 + 状态 fixture 8/8；ASAN/UBSAN/TSAN 同四个资源/
  播放器/进程目标各 6/6（TSAN 本机需 setarch 关闭 ASLR）。真实九态解码
  `pet_frames_test` 91 checks；private Xvfb/SNI/ICCCM/composite-redirect peer 下
  13/13：资源迁移、角色区域真实 GPU 像素/alpha、持续帧推进、独立 PID、最小化/
  恢复、隐藏正常关闭与失联退出。无模型请求，不用该 peer 冒充桌面 compositor。
- 基线指标：final 工件 `/tmp/mirage-m8-pet-20261013-final/`；可见 CPU 26.33%
  （一核）、隐藏 1.00%，VmHWM 190420 kB；隐藏关闭 1.571 秒、异常失联 6.386 秒。
  这是软件渲染 idle 样本，非九态峰值、硬件 GPU 或性能承诺。范围格式、57 个公共
  头边界、六份文档相对链接和 `git diff --check` 通过；整体格式门禁仍在未修改的
  IPC 头既有注释对齐失败，保留基线限制。
- 仍未验收：维护者已明确七态 20 圈尚未观看；真实 compositor/焦点/穿透/全屏/
  多屏与 DPI、硬件 GPU/九态完整峰值与 GUI sanitizer、避让/OCR 排除、正式许可/
  设置/包装及其他平台。负责人：Mirage 维护者；补跑条件：目视工件、目标设备与
  对应工作项就绪后按测试矩阵复跑。M8-02/03/04/05 整体保持 In Progress，
  不把实现收口和私有窗口测试等同于全部产品门禁完成。

### 2026-10-13：消费 EUI 根因修复并推进 M8-05 独立控制

维护者明确授权中文 issue、直接处理 EUI 缺陷及向 dev 提 PR。核对后确认源问题已由
上游 PR #97 修复，创建 [issue #106](https://github.com/sudoevolve/EUI-NEO/issues/106)
记录旧 pin 的下游复现；新增 stride/透明 alpha 回归
[PR #107](https://github.com/sudoevolve/EUI-NEO/pull/107)，base=dev、head=977f9ea。
四种窗口/渲染组合 Release 完整构建与本地 ctest 各 35/35，四项 CI 通过。
Mirage pin 升级至 dev 89c2d0f，与锁文件同步；恢复容量 1 的公开 ImageStream，
删除临时 FrameTexture，不重复实现上游修复。详见
[升级审计](../supply-chain/eui-rgba-upgrade-20261013.md)。

M8-05：条件式托盘菜单与 product.control 独立 start/stop/ready 已实现。平台 handle
只由串行上下文或一个保留 future 的有限 Executor 关闭任务持有；stopping 期间
status 读取缓存，IPC 不等待动画。readiness 核验当前所属 PID，未 ready 的 10 秒
期限可注入时钟验收；关闭/崩溃后明确诊断，不自动反复拉起。owner 收尾先 fence、
再消费 future 和最终回收；拒绝无托盘 owner 和共用主窗口 process handle 的配置。

升级后 Debug 八个相关测试加隔离 fixture 10/10；生命周期/协议 ASAN、UBSAN、TSAN
各 4/4。私有实进程/窗口 22/22，含实际角色 alpha/帧推进、独立启停/复用、崩溃与
显式恢复、隐藏正常关闭及 owner 异常失联退出。日志与范围见
[M8 验证记录](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。
七态 20 圈目视验收仍未进行；真实 compositor、硬件 GPU、多屏/DPI、完整真实 Agent
输入闭环和其他平台门禁保持开放，不因本轮升级勾选 M8-02/03/05。

### 2026-10-13：开启真实桌面视觉验收（待人工结果）

维护者表示当前可以视觉验证。环境核对：GNOME Wayland 会话、DISPLAY=:0 的
XWayland（3200×2000）有真实 compositor owner，真实 StatusNotifierWatcher 在线。
以隔离设置/状态目录和 `/run/user/1000/mirage-m8-visual.sock` 启动已构建产品托盘，
桌宠 PID=908484，readiness=running，active_work=0；没有发送 Agent 任务。
这次窗口属于 XWayland 实机会话，不能登记为原生 Wayland 或纯 X11 已验收。

七份候选区间 20 圈视频已核对 512×512、帧数、原始 rational fps 与总时长；源九态
摘要再次通过。验收页已在应用右侧浏览器打开，首项待机已开始播放；视频与页面
位于当前任务的可视目录 `m8-loop-review/`，本地入口 `http://127.0.0.1:18768/`。
离线灰底视频用于接缝评审，桌面上的实际桌宠用于透明/置顶/输入穿透评审。

人工反馈仍未收到：维护者的“可以验证”只代表环境具备，不等于七态或窗口验收通过。
M8-02/03/04/10 的对应门禁继续保持未勾选。视觉会话保留供维护者操作，完成后只
关闭本次隔离 owner 与预览服务，不影响其他会话。

### 2026-10-13：七态接缝人工通过；成功/失败改为 once

维护者明确反馈：“确认7个20轮播接缝视觉没有问题”，同时要求实际成功/失败只播一次。
已登记源 SHA、区间、20 圈帧数、视频 SHA 与人工确认原文；原候选 manifest 和预览
视频保留。当前运行 manifest 另存为 mira-pet-playback-v5-20261013.json：五态 loop，
enter/done/fail/exit 四态 once。成功 56 帧/4648 ms、失败 55 帧/6875 ms；播完静态
持有末帧与文字，不因新 revision 重播；新任务身份重新播放，接管/取消立即打断。

- [x] M8-02 七份候选区间各 20 圈人工接缝验收（含改为 once 的两份历史结果区间）。
- [ ] 实际 compositor/窗口效果、完整资源/GPU 峰值、结果全片单次播放实机验收、其他平台。

接缝门禁单独关闭；M8-02/03 工作项继续 In Progress，不能用本次接缝确认代替窗口、
资源许可/安装或其他平台验证。构建/单测结果见本轮兼容性记录。

### 2026-10-13：M8-01～05 单屏现场复验与缺陷修复

实际产品入口已在 GNOME XWayland 启动。维护者首次发现动画间透明空帧、HiDPI
角色下半身裁切且不能拖动；修复后明确答复“没有上述问题”。实际任务状态及
成功/失败动画与主窗口“一致”；主窗口关闭后桌宠持续显示且不抢焦、托盘单独
停启后任务与主窗口正常，两项均明确答复通过。最终 Debug 选择集 10/10、私有
X11 实进程/窗口 27/27；一次 `tray_backend_test` 信号超时后重跑通过，间歇性
仍待定位。完整证据与边界见[当日兼容性记录](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。

本轮关闭上述现场缺陷及场景验证。M8-01～05 工作项继续 In Progress：真实输入
阶段/旧客户端负例、结果全片逐帧 once、GPU 峰值、全屏/多屏及其他平台、活动 Agent
输入释放等门禁尚无充分证据；维护者在目标环境和真实任务脚本可用后按矩阵补跑。

### 2026-10-13：位置恢复、角色画布与播放规则复验

按维护者补充要求移除底部状态条，X11 窗口设置 SKIP_TASKBAR/SKIP_PAGER，拖动结束
及正常退出经 LocalStateStore 保存位置，重启按工作区恢复。播放器在资源就绪后才
计时，已开始动画至少完成首轮；加载期间被替代或首轮期间已经结束的短动作不补播。
桌宠本地启动到 Runtime 的正常交接也遵守首轮规则，安全状态仍立即打断。

私有 X11 实进程/窗口 31/31，含位置持久化/恢复、越界位置回退、无状态条与 Dock
排除标记；Debug 相关选择集 10/10。维护者对位置、无状态条、Dock 与实际任务播放
四项明确反馈“通过”。随后反馈失败结束后停在末帧；现改为失败 once 后待机，
同次 owner/source 通知与隐藏恢复不重播。播放器单测 211 项通过，真实服务仍报告
同一失败时桌宠已回到待机循环，维护者再次确认“通过”。

以上关闭本轮补充场景。具体命令、失败修复过程和平台边界见
[当日验收记录](../compatibility/mira-pet-loading-and-lifecycle-20261013.md)。
原 M8-01～05 的未执行平台、性能与输入闭环门禁仍保持开放。

### 2026-10-13：阶段提交前检查

维护者要求提交 M8 阶段更新。九份 v5 源及 provenance 进入仓库，opt-in 构建默认
消费仓库素材，摘要再次全匹配。当前锁定依赖下重新构建桌宠/托盘及相关测试成功，
Debug 10/10、私有桌面 31/31；最新播放器 ASAN、UBSAN 各 3/3（含 fixture），
各 211 项检查无异常。依赖升级与桌宠功能分别提交，保持 M8 尚未覆盖的退出条件开放；
详细复跑条件与结果见当日验收记录。

### 2026-10-13：本机 Release 启动引用更新

维护者要求更新本机 native-release 引用。以 `c33142b` 代码重建主入口、托盘、
主窗口与桌宠，启用仓库 v5 素材和桌宠目标，刷新已注册 desktop entry。Release
相关测试 5/5、私有桌面 31/31，真实启动三个进程路径均指向 native-release，
主窗口 ready、桌宠 running。本次仅更新本机构建及证据，原未覆盖门禁继续开放。

### 2026-10-14：桌宠重启不重播历史失败（BUG-20261014-001）

关联 M8-03/05：维护者报告上次任务失败后独立退出再打开桌宠，播放失败而非入场。
新进程先完成入场，首份 Runtime 终态作为显示基线，之后待机；新任务终态与安全
打断保留。移除 UI 的固定启动时间判断，门禁以素材就绪后的实际入场时钟为准。
Debug/native-release/ASAN/UBSAN 播放器各 250 项检查无失败，CTest 各 3/3（含隔离
fixture），native-release 桌宠重建成功。详细命令与未执行项见
[重启回归记录](../compatibility/mira-pet-restart-20261014.md)。M8 整体及原开放门禁
保持 In Progress，真实桌面重启目视验收由维护者在更新桌宠进程后补跑。
