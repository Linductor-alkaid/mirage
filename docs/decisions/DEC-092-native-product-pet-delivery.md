# DEC-092：Native 产品完整构建与 Pet 启动诊断

> 状态：Accepted（2026-10-15 维护者要求解决 native-release 的 M8 pet 缺席）
> 日期：2026-10-15
> 负责人：Mirage 维护者
> 冻结里程碑：M8-12；正式安装、许可与偏好仍由 M8-08 承载
> 替代/被替代：冻结 DEC-084 的 native 产品携带/自动启动选择；角色、动画及 owner 契约保留

## 背景与问题

调查时磁盘 pet 可执行文件和九套素材存在，运行中的托盘仍持有替换前的二进制，
没有 pet 子进程，产品状态不含 pet carrier 字段。启动时仅探测一次文件、缺失
静默跳过；默认 native preset 未开启 pet，CI 单独传 ON，不能覆盖干净产品构建。
具体旧实例为何未接入仍待干净启动取证，不能归咎动画渲染。见
[分析记录](../analysis/session-budget-and-native-pet-regression-20261015.md)。

## 决策

1. Native 产品默认携带 M8 pet；当前无显式关闭偏好时，由托盘 Runtime owner
   自动启动。后续显式用户启停偏好由 M8-08 定义和迁移，不能在本项另存一份。
   Headless/service-only 构建继续独立，不强制引入 UI 依赖；平台不支持 pet 时
   明确能力与原因，不以虚假 running 状态掩盖。
2. 定义完整 native 产品构建入口，聚合 launcher、service、tray、native、pet、
   字体、九态 manifest/素材及配置要求的 vision bundle。native preset 显式
   设置产品能力，不依赖旧 cache、残留文件或只构建主窗口的 README 命令。
   正式 install/包载荷与聚合结果由 M8-08 同源维护。
3. 缺 pet、字体/素材、摘要错误、启动退出或 readiness 超时都要在产品状态中
   明确原因和恢复方式。区分状态投影服务可用、carrier 可用、进程 starting、
   ready/running；`pet.status` 返回任务状态不表示窗口已启动。
4. 交付前在隔离 staging 校验完整布局，从任意 cwd 启动并验证资源查找。记录
   源版本与产物摘要；运行 owner 与磁盘版本不同须可诊断。复用旧 owner 时不能
   宣称已加载新产品；需要更新 owner 时正常退出再打开，保留用户配置与会话。
5. 启动失败不终止无关 Agent 任务，不无限自动重启。保留 M8-05 独立启停、单
   owner、受管 PID、取消和关闭顺序。M8-12 不扩大素材分发许可或修改第三方依赖。

## 备选方案

| 方案 | 结论 |
| --- | --- |
| 继续把 pet 视为本机 opt-in 预览，靠缓存 ON | 无法保证 native 产品重建完整，不采用 |
| 缺失时静默忽略 | 无法区分产品缺件和用户关闭，不采用 |
| 任意文件更新时强杀 owner 并自动重启 | 破坏任务/会话与生命周期，不采用 |
| 完整构建、staging 验证、明确运行版本与诊断 | 采用 |

## 影响与风险

Native 构建增加 pet 图形/解码依赖与载荷，需检查 Linux/Windows 包布局和缺能力
行为。源码资源存在不等于已获得所有分发许可；M8-08 的许可/安装门禁保持开放。
本决策不改变透明、DPI/多屏、气泡及目标避让的既有平台验收要求。

## 验证方式

M8-12 覆盖全新目录默认 native 构建、搬迁/任意 cwd、缺件/损坏/启动失败、旧
owner 复用、ready PID 与动画/气泡、独立启停及全产品关闭。正式安装/升级/卸载
由 M8-08 验收；Xvfb 不能替代实际 compositor 或 Windows 窗口结果。

## 关联文档和工作项

- [首增量验证](../compatibility/m9-incremental-scene-and-pet-build-20261015.md)：
  默认构建、缺载体诊断与隔离 Debug 产品验收；现有用户 release 未替换，M8-12 未关闭。
- [M8 计划](../plans/m8-mira-pet.md)：M8-12、08、05、06、10、11。
- [DEC-084](DEC-084-mira-pet-process-and-animation.md)、
  [DEC-089](DEC-089-mira-pet-bubbles.md)、
  [DEC-045](DEC-045-tray-runtime-owner.md)。
