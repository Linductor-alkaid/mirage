# DEC-086：Windows 屏幕采集分层升级（DXGI Desktop Duplication → Windows.Graphics.Capture → BitBlt）

> 状态：Accepted
> 日期：2026-10-14
> 负责人：Mirage 维护者
> 工作项：M7-10（Windows 平台推进）
> 触发：维护者明确指示（2026-10-14 会话目标：Windows 工具端屏幕采集最优先
> DXGI，参考实现 NAW 仓库），兑现 [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)
> 决策 6 预留的采集升级路径

## 背景与问题

DEC-017 决策 6 冻结 GDI `BitBlt` 为首版采集（显示 / 窗口 / ROI 统一走
`GetDC(NULL)` + `BitBlt`），并把 Windows.Graphics.Capture / DXGI Desktop
Duplication 列为升级路径，触发条件为性能需求或维护者授权。2026-10-14 维护者
明确触发：采集分层顺序定为 **DXGI Desktop Duplication 最优先（高性能硬件加速）、
Windows.Graphics.Capture 回退（支持多程序同时捕获）、BitBlt 最终回退**，并给出
参考实现（NAW 仓库 `ScreenCaptureWindows`）。

升级需要先定案五个问题：

1. 并发纪律：DXGI 复制接口与 WGC 帧池的生命周期如何放入 DEC-017 决策 3 的
   "同步、有界、不建线程" 的 Provider 形态；WGC 的 FrameArrived/DispatcherQueue
   事件面是否引入。
2. 采集会话归属：复制会话 / 捕获会话是跨调用持有还是逐调用新建；陈旧帧与
   M7 动作后重验的"新观察"契约冲突风险。
3. 坐标空间：DPI 虚拟化下 GDI 坐标是逻辑像素，而 DXGI/WGC 锁定物理像素；
   两套空间的混用会静默错位裁剪。
4. 平台现实：混合显卡 + 间接显示驱动（IddCx，如向日葵 OrayIddDriver）+ 远程
   工具占用下，复制会话可能超时、被占（`E_ACCESSDENIED`）或交付占位帧
   （`LastPresentTime == 0 && AccumulatedFrames == 0` 的全黑图像）。
5. 平台面：WinRT/COM 依赖面（DEC-017 决策 4 的 MTA 纪律、导入库、双工具链
   头文件面）与帧格式（HDR 桌面为浮点格式，旋转输出为未旋转纹理）。

## 决策

新增私有采集引擎 `platform/windows/src/screen_capture_backend.{hpp,cpp}`
（Pimpl，零平台类型进公共头，RULE-01），`Win32Backend` 的 `capture_display /
capture_window / capture_roi` 全部委托给它。分层顺序与边界如下：

1. **分层顺序（Auto 链）**：DXGI Desktop Duplication → Windows.Graphics.Capture →
   GDI BitBlt。每层失败如实记录原因并落入下一层；最终错误的 message 前缀
   逐层失败链，错误码取最后一层，调用方语义不变（DEC-009 词表）。
2. **逐调用无状态会话**：复制会话（`DuplicateOutput` → `AcquireNextFrame` →
   `ReleaseFrame`）与 WGC 会话（`CreateForMonitor` → free-threaded 帧池 →
   `StartCapture` → `TryGetNextFrame` → `Close`）均逐调用新建、调用内关闭；
   跨调用仅持有 D3D11 设备（按输出所属适配器缓存，LUID 变更重建）与一块
   staging 纹理（按尺寸/格式复用）。新会话首帧即完整桌面，不存在跨调用陈旧
   帧，与 M7 "新观察" 门禁兼容；会话丢失只损失一次调用。
3. **无线程、无事件循环**：不注册 FrameArrived，不用 DispatcherQueue——WGC 逐
   调用轮询 `TryGetNextFrame`（有界 1.5 秒预算、15ms 切片、切片间观察取消）；
   复制路径用有界 `AcquireNextFrame` 超时。要求 free-threaded 帧池
   （`IDirect3D11CaptureFramePoolStatics2::CreateFreeThreaded`，Windows 10
   1809+）；缺失时该层如实不可用（dispatcher 绑定的 `Create` 需要消息循环，
   违反 DEC-017 决策 3）。全部引擎调用仍由 `Win32Backend` 既有互斥锁串行化，
   调用上下文 = 消费方 Executor blocking worker，不新建任何线程（RULE-03）。
4. **占位帧防御（实测驱动）**：混合显卡 + 间接显示驱动的桌面上，新复制会话
   可能连续交付零 present time / 零累积帧的全黑占位帧。引擎对占位帧做有界
   重试（释放后至多 4 次 × 100ms），并对读取结果做全零抽样兜底——占位帧
   转为该层失败（如实 "empty frame"），不是静默黑图假成功。超时、占用
   （`E_ACCESSDENIED`）、会话丢失（`ACCESS_LOST`）、旋转输出、非 Bgra8 兼容
   格式（HDR 浮点）同样逐层如实失败。
5. **WGC 像素一致性**：`IsCursorCaptureEnabled=false`（Session2，1903+）、
   `IsBorderRequired=false`（Session3，Windows 11+）尽力设置，失败保留系统
   默认——与 DXGI/BitBlt 一样，硬件光标不进桌面图像；系统边框叠加在支持的
   系统上去除。帧池固定申请 `B8G8R8A8UIntNormalized`，HDR 桌面由系统色调映射。
6. **DPI 空间门禁**：引擎逐调用测量 Provider 坐标空间与物理像素空间（线程级
   `SetThreadDpiAwarenessContext` 隔离测量并恢复）。两空间一致（进程 DPI 感知
   或宿主 100% 缩放）时 Auto 链启用 GPU 层；不一致（DPI 无感知进程 × 缩放
   桌面）时 Auto 直接走 BitBlt——BitBlt 是唯一遵循进程 DPI 虚拟化的实现，
   与 Provider 的逻辑坐标世界自洽。物理换算 `to_physical` 仅供强制层使用；
   产品宿主声明 per-monitor DPI 感知是激活 GPU 层的后续工作项（负责人维护者，
   见 M7 计划 2026-10-14 记录），本决策不改变进程级 DPI 行为。
7. **单输出边界**：DXGI/WGC 均为逐输出实现。矩形完整落在单一显示器内才可进
   GPU 层；跨屏矩形 Auto 链直接 BitBlt（唯一能组合虚拟屏幕的层），强制层如实
   拒绝（`invalid_argument`），不做静默替代。
8. **诊断强制层**：引擎 API 提供 `Tier::{Dxgi, GraphicsCapture, BitBlt}` 强制
   参数，仅供分级验收取证与诊断使用，产品代码只用 `Auto`。强制层失败不回退
   （失败的层拥有自己的错误），保证取证真实。
9. **平台面**：WinRT 激活走 `RoGetActivationFactory` + `WindowsCreateStringReference`
   （COM interop 面，不引 C++/WinRT）；每调用一个 `RoInitialize(MTA)` 域
   （S_OK/S_FALSE 配对释放，`RPC_E_CHANGED_MODE` 视为已就绪），与 uia_backend
   的 MTA 纪律同型（DEC-017 决策 4），无跨调用 WinRT 对象驻留故不设进程锚。
   链接面 `d3d11 dxgi windowsapp`（SDK 无独立 combase.lib，windowsapp 为
   WinRT 激活伞库）。MinGW-w64 头文件面按社区 WinRT ABI 头评估，MSVC 为
   主门禁（DEC-017 决策 1 不变）。
10. **采集语义不变**：Bgra8、top-down、根帧语义（窗口边界处的屏幕内容、含
    遮挡）与 DEC-017 决策 6 / M2-02 完全一致；预算拒绝、入口取消检查、
    `source_sequence=0` 等 Provider 契约不变。

## 备选方案

- **跨调用持有复制/捕获会话（NAW 形态）**：否决——会话跨调用持有会在模式
  切换 / 占用切换后进入失效态，且为凑"当前帧"需缓存旧帧，违反 M7 新观察
  语义；逐调用会话的建会话开销（数十毫秒）对截图工具语义可接受。
- **WGC FrameArrived + DispatcherQueue 专用线程**：否决——引入 Provider 私有
  线程与消息循环，违反 RULE-03 / DEC-017 决策 3；轮询首帧即满足同步截图。
- **Auto 链忽略 DPI 空间差异（GPU 层总是启用 + 物理换算）**：否决——换算在
  混合缩放下有取整与拓扑歧义，且让帧分辨率随进程 DPI 策略漂移；以 BitBlt
  保虚拟化进程行为不变，GPU 层激活与产品 DPI 声明解耦推进。
- **黑白帧重试放宽为任意内容重试**：否决——只有零 present time / 零累积帧
  与全零抽样两个可证据的信号构成"占位帧"判定，任意内容比较会把真实黑屏
  （全黑终端等）误判为失败；真实黑屏由下一层等值返回，语义不损。

## 影响与风险

- `win32_backend.cpp` 移除内联 BitBlt 实现（原地成为引擎末层）；platform
  Windows 链接面新增 `d3d11 dxgi windowsapp`。
- 该机实测（2026-10-14，RTX 3070 Ti Laptop + OrayIddDriver 三屏混合 DPI）：
  复制会话占位帧 / 超时为环境事实，Auto 链如实落 WGC 取得正确像素；强制
  DXGI 在重试窗口内亦可取得真帧。远程工具（向日葵）占用与 IddCx 驱动的
  存在使复制会话可靠性成为部署环境事实，DEC-086 分层即为该现实的承载。
- WGC 系统设置拒绝时返回 `permission_denied`（可区别于 `io_error`）；系统
  边框在 Windows 10 保留（Session3 缺失），已记录为平台差异。
- 风险：MinGW-w64 WinRT ABI 头版本差异可能需要编译期探针；当前 CI windows
  门禁为 MSVC，MinGW 交叉构建仅编译门禁，暴露时按工具链事实补决策。

## 验证方式

- 白盒分级测试 `win32_capture_tier_test`（真实交互桌面）：Auto / DXGI /
  GraphicsCapture / BitBlt 各自完成双色 fixture 像素正确性（窗口 ROI 三轮
  重复 + 全显示器绝对坐标探针）、逐层入口拒绝（预算 / 取消 / 退化矩形）、
  跨屏强制层拒绝与 Auto-BitBlt 组合。GPU 强制层以可用性探针门控，环境不可
  用时按 DEC-017 决策 9 纪律大声记录跳过条件。
- Provider 回归 `win32_backend_test` 不变通过（127 checks）。
- MSVC 全树构建 + windows 测试子集（CI 同款门禁）。

## 变更记录

- 2026-10-14（M7-10 首交付）：决策随实现落地；本机取证见 M7 计划
  2026-10-14 记录。
- 2026-10-14（M7-10 宿主 DPI 声明）：开放边界中的产品宿主 per-monitor DPI
  声明落地。平台公共入口 `declare_host_dpi_awareness()`（PMv2 优先，幂等，
  prior/unchanged 如实报告；Linux 无进程级声明返回 unavailable），
  `run_service_host`（mirage-service 与产品 tray 的共同宿主入口）在参数解析
  后、任何窗口创建前声明并记录生效模式。宿主感知把显示器/窗口枚举、采集像素
  与 SendInput 注入坐标统一于物理像素空间（无感知宿主的 GetSystemMetrics
  虚拟化值与 `MOUSEEVENTF_VIRTUALDESK` 物理归一化在混合 DPI 下系统性错位），
  并按本决策的空间门禁解除缩放桌面上 GPU 层的禁用。tier 测试改用同一入口并
  新增逐显示器物理几何取证；DPI API 的实测语义修正（awareness 枚举无 v2 值、
  旧入口返回值在现代入口之后不可信）见 M7 计划同日验证记录。

## 关联文档和工作项

- [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md) 决策 3/4/6
  （并发纪律、COM 模型、首版采集与升级路径触发）。
- [M7 计划](../plans/m7-visual-tools-and-input.md) M7-10 工作项与 2026-10-14
  验证记录。
- 参考实现：NAW `ScreenCaptureWindows`（分级顺序与占位帧现象的独立先例；
  本决策在其基础上改为逐调用无状态会话 + DPI 门禁 + 占位帧证据化）。
