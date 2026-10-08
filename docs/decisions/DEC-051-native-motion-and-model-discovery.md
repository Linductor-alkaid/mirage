# DEC-051：原生过渡与模型目录发现

> 状态：Accepted
> 日期：2026-10-08
> 负责人：Mirage 维护者
> 冻结里程碑：M6
> 工作项：M6-33
> 替代/被替代：无

## 背景与问题

侧栏开合、主题切换目前瞬时跳变；模型服务页虽有逐模型 `supports_reasoning` 持久化字段，
却没有编辑控件。用户必须手填模型 ID，无法从选定 Base URL 获取供应商目录。

## 决策

- 原生 UI 使用有界的 220ms 侧栏宽度渐动与 240ms 主题色插值。侧栏内容置于裁剪视口，
  展开/收起时沿同一水平轴移动，主阅读列随显示宽度连续定位；拖动调宽仍直接跟手。
  外观页提供“减少动态效果”，直接到达终态；偏好保持于当前UI进程。
  帧唤醒由 UI 进程现有 RuntimeBridge 的 Executor timer 管理，动效结束立即恢复原刷新周期或停用。
- 模型服务页对当前选中模型提供思考能力开关。Anthropic Messages 继续使用 DEC-046
  中按模型 ID 核验的选项；OpenAI 方言以逐模型 `supports_reasoning` 声明控制
  `minimal/low/medium/high`。声明不构成供应商能力自动探测，最终调用仍由模型服务端确认。
- IPC v1 增加显式 `model.list` 请求与 `model_ids` 响应。请求携带服务身份、已编辑的
  origin/prefix/方言和可选一次性 API Key；响应只回最多 256 个去重 ID，不回 Key。
  未传 Key 时，只可复用与保存目录中服务身份、地址和方言完全匹配的凭据。
  UI 只在用户点击“获取模型”时发起请求，结果是可选择的候选，不自动保存或改写配置。
- UI目录请求使用独立的SessionClient与Executor blocking worker；常规会话请求使用
  Executor SerialExecutionContext保持提交顺序。两个路径均保留future并在退出时排空。
- Runtime Service 通过 Executor 可取消任务调用 pinned Mira 的 SocketHttpTransport；
  有两个并发请求上限、每个候选 10 秒、2 MiB 响应上限、零跳转。依次尝试
  Base URL 对应的 `/models` 与 `/v1/models`；已知 Anthropic 兼容子路径先剥离再尝试
  `/v1/models`。识别 `data[].id` 和 `models[].slug/id`。
  请求失败、认证失败、无可解析列表、任务拒绝和关闭均显式结算。

## 备选方案

未选 UI 进程直接下载：系统钥匙环的已存 Key 由服务持有，直接请求会跨越凭据边界。
未选自动导入全部模型：目录可能很大，现有持久化上限为 12 个，用户需明确选择。
未引入 EUI async/network，也未更改 pinned 依赖。

## 影响与风险

可用性依赖供应商开放兼容模型列表端点；部分厂商的私有端点或特定路径无法通过通用候选发现，
仍可手填模型 ID。Mira 传输层的私网端点保护保留，不因目录获取放宽。
服务进程退出时取消并排空目录任务。Linux真实OpenRouter公开目录已验证；Windows原生动效需对应环境补验。

## 验证方式

IPC 编解码与非法 Key、无匹配凭据拒绝；原生模型编辑与渲染；Executor 提交、取消、
关闭及主题/侧栏反复切换的实际窗口检查。详见 M6-33 验收记录。

## 关联文档和工作项

[M6 计划](../plans/m6-native-frontend.md)、
[原生设计](../design/native-agent-frontend.md)、
[IPC v1](../design/mirage-ipc-protocol-v1.md)。
