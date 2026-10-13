# M8：桌宠资源、GPU 呈现与隐藏退出证据

> 状态：In Progress
> 日期：2026-10-13
> 负责人：Mirage 维护者
> 工作项：M8-02/03；M8-04/05 后续增量
> 基线：`86b6778` + `feat/m8-pet-resource-loading` 范围化工作树

## 实现与边界

资源加载使用桌宠进程已有 Executor 的可取消有限任务。一项 future、一项最新目标；
旧结果被消费和释放后才接纳新目标。UI 保有 generation、播放器时钟与 GPU 引用，
读取每 64 KiB、解码每帧检查停止；WebP 内部线程显式关闭。源文件运行时重验 SHA，
摘要复用 Mira；区间、路径、常规文件、8 MiB 源、512×512 与 96 帧/96 MiB 结果
均检查。2 秒应用请求期限通过 Executor 的取消请求与 UI `TimedOut` 共同取证；
不把 queued soft timeout 当作运行期强制终止。

原始 RGBA 通过 EUI 公开外部 GPU 图像接口采样。私有 UI Adapter 只更新一份
512×512 RGBA8 纹理，主图像分配为 1 MiB；同一 UI 上下文依序上传、修改 revision、
绘制。EUI 按 fence 保留资源，onShutdown 清除应用引用。驱动、字体、framebuffer、
解码器内部缓冲与库本身另计，1 MiB 不是整个进程/GPU 峰值承诺。

最小化回调保留 EUI 原回调，并在外部 UI owner 线程暂停动画 timer、取消加载、
释放 UI 解码结果；回调不做文件 I/O 或 GPU 导入。恢复的首个 UI 更新清除旧图，
加载最新状态并沿原时钟推进。隐藏时健康探测继续工作，通过独立 X11 连接把关闭
请求送回 UI 事件循环；隐藏退出不等待动画。输入释放与业务运行时关闭顺序保持
既有 Runtime owner 契约。

## 缺陷与处置

| 编号 | 实际问题 | 处置与证据 |
| --- | --- | --- |
| `BUG-20261013-001` / `EUI-20261013-001` | pinned EUI RGBA/BGRA 转码循环只写每行首像素，导致角色透明 | [EUI 台账](../dependency_feedback/eui-ledger.md)，只使用公开 GPU 导入接口；依赖保持 Open |
| `BUG-20261013-002` | 条件表达式混用 string/字面量产生临时 string，三个 string_view 悬空，真实状态落到静态 fallback | 两侧显式使用 string_view，引用 UI 所有的 Status；窗口像素与持续帧推进验证 |
| `BUG-20261013-003` | 尚未启动或已回收的宠物进程继续走 kill(0)，误发给调用者进程组 | 每次信号前取得正 child PID；缺席/失败启动/已回收/重复停止/单实例/正常停止回归 |

EUI 最小字节复现（公开 API，不涉及 Mirage/WebP/GPU）：

```bash
cat > /tmp/m8-eui-rgba-probe.cpp <<'CPP'
#include <eui/image.h>
#include <iostream>
int main() {
    eui::ImageFrame frame;
    auto input = std::make_shared<const std::vector<std::uint8_t>>(
        std::initializer_list<std::uint8_t>{255,0,0,255,0,255,0,255});
    frame.pixels = input; frame.width = 2; frame.height = 1; frame.stride = 8;
    std::vector<std::uint8_t> output;
    const bool ok = frame.convertToRgba8(output);
    std::cout << "convert_ok=" << ok << " preserves_rgba=" << (output == *input) << '\n';
    return ok && output != *input ? 0 : 1; // 0 表示复现该已知缺陷，非能力验收通过
}
CPP
c++ -std=c++20 -Ithird_party/eui-neo -Ithird_party/eui-neo/include \
  /tmp/m8-eui-rgba-probe.cpp third_party/eui-neo/core/render/image_stream.cpp \
  -o /tmp/m8-eui-rgba-probe
/tmp/m8-eui-rgba-probe
```

实际输出 `convert_ok=1 preserves_rgba=0`；完整输出字节为
`0,255,0,255,0,0,0,0`。pinned EUI 为 `a7e625fecc1db1c1acb4850a14fb785e20600041`。

## 自动化命令

Debug 的相关六个测试目标连同状态 fixture 共 8/8 通过；新增加载器测试覆盖正常
完成、异常、拒绝、快切合并/迟到结果、请求超时、隐藏取消/恢复与 shutdown。
`pet_frames_test` 91 checks，真实解码九态并验证 alpha、损坏/摘要、非法路径/区间、
尺寸预算、中途取消、帧数/时长 mismatch 和真实 96 MiB 最大状态的 future 交付。

```bash
cmake --build --preset native-debug --target mira_pet mirage-tray \
  pet_resource_loader_test pet_frames_test frontend_process_test \
  pet_animation_test pet_status_integration_test tray_runtime_test -j 4
ctest --test-dir build/native-debug -R \
  '^(pet_resource_loader_test|pet_frames_test|pet_animation_test|pet_status_integration_test|tray_runtime_test|frontend_process_test)$' \
  --output-on-failure
```

资产测试可独立于 UI 构建运行。各 sanitizer 使用以下配置与对应 build/test：

```bash
cmake --preset asan -DMIRAGE_ENABLE_PET_ASSET_TESTS=ON \
  -DMIRAGE_PET_ASSET_SOURCE_DIR=/home/linductor/下载/mira_pet_v5_桌宠素材包 \
  -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset asan --target pet_resource_loader_test pet_frames_test \
  pet_animation_test frontend_process_test -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan \
  -R '^(pet_resource_loader_test|pet_frames_test|pet_animation_test|frontend_process_test)$' --output-on-failure
```

UBSAN 使用相同 `ubsan` preset/目录与 `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`；
TSAN 使用 `tsan` 与 `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`
并经 `setarch x86_64 -R ctest ...` 关闭本机 ASLR。实际 ASAN/UBSAN 各 6/6 通过；
TSAN 同一组合 6/6 通过（启用 setarch 的 ASLR 限制仍保留）。这些 sanitizer
证明加载/解码/进程适配，未把未运行的完整 GUI sanitizer 计为通过。

## 窗口实测与限制

```bash
python3 tests/manual/pet_preview_acceptance.py --build build/native-debug \
  --output /tmp/mirage-m8-pet-20261013-final
```

实际为 Linux 7.0.0-38 x86_64 / GCC 13.3.0、libwebpdemux 1.3.2、EUI OpenGL、Xvfb/Mesa 软件渲染，
私有 DBus/SNI/ICCCM peer。peer 在独立 X server 上拥有 compositor selection 并
redirect subwindows，以读取真实 RGBA framebuffer；它不合成桌面 root，不能当作
实际桌面 WM/compositor 验收。脚本复制桌宠、字体与九态资源到临时产品目录，从 `/`
启动实际托盘与桌宠；pet 运行不依赖原下载目录，也不发生模型请求。

首轮 GPU import 实测 12/12 通过：源资源迁移、独立受管 PID、角色区域可见像素、
透明角、最小化/恢复不换进程、隐藏时正常关闭和意外 owner 失联退出。参考基线：
可见时 CPU 26.33%（一核），隐藏 0.33%；VmHWM 189792 kB、VmRSS 186388 kB；
隐藏关闭 0.267 秒、隐藏异常失联退出 6.438 秒。软件渲染、单个 idle 场景的样本不
代表硬件 GPU 性能、九态峰值或低功耗保证。首轮工件保留在
`/tmp/mirage-m8-pet-gpu-import/`。

最新追加同一 idle 状态持续帧像素差门禁，并先按 `hello.tray` 等待 SNI readiness；
上述 final 命令实际 13/13 通过。CPU 可见 26.33%、隐藏 1.00%，VmHWM 190420 kB、
VmRSS 186412 kB；隐藏关闭 1.571 秒、隐藏异常失联 6.386 秒。两次采样的关闭时间
受健康 timer 相位影响；均在 owner 预算内，不能把 0.267 秒作为普遍承诺。

工件在命令所示临时目录：`pet-results.json`、`pet-rgba.png` 与进程日志，可用脚本
再生，不随 Git 发布。未读取凭据或用户窗口；资源使用许可与正式包仍由 M8-08 取证。

初次失败保留事实：系统 Python 缺 Xlib，后改用现有 Miniconda Python；裸 Xvfb
正确拒绝无 compositor 的透明窗口；弱的全窗口 alpha 断言仅证明文字区域，强化
角色区域断言后发现 EUI 转码缺陷；调试过程中确认 string_view 生命周期缺陷。另有
测试驱动误用 `shutdown`，已改为规范操作 `service.shutdown`，随后一次启动 readiness
竞态通过先等待 `hello.tray` 修正。TSAN 真实解码初轮
触发生产 2 秒请求期限，资源测试改用显式 15 秒仪器化预算；生产期限保持 2 秒，
超时语义另由注入时钟测试证明。未改写失败为成功。

当前范围 clang-format 与公共头边界通过、`git diff --check` 通过。仓库整体 format
检查在未修改的 `runtime/ipc/include/mirage/runtime/ipc/protocol.hpp` 的既有注释对齐
失败，保持为基线门禁限制；不把范围检查冒充整个仓库 format 通过。

## 尚未关闭的门禁

- 七态 20 圈目视：维护者于本轮明确尚未验收，M8-02 保持 In Progress。
- 真实 WM/compositor 可见性、焦点、全屏、穿透、多屏/DPI与其他隐藏方式；目标避让/
  OCR 排除仍属 M8-04/06/07，XWayland/Wayland/Windows 单列 M8-10。
- 硬件 GPU/完整进程九态峰值与正式性能预算、完整 GUI sanitizer；对应设备/配置可用
  后由维护者按 M8 矩阵补跑，不以本机软件渲染或 headless sanitizer 代替。
- 许可、偏好与正式安装/升级/卸载由 M8-08 交付；当前只是迁移的预览载荷。

实现与验收状态分别维护，见 [M8 计划](../plans/m8-mira-pet.md) 与
[设计](../design/mira-pet.md)。

## 同日后续：上游修复消费与独立进程控制

前文是旧 EUI pin + GPU Adapter 的当轮证据，保留失败史。维护者随后授权直接处理
EUI、提交中文 issue 和目标 dev 的 PR。最新 dev 已有 PR #97 根因修复；本轮创建
[issue #106](https://github.com/sudoevolve/EUI-NEO/issues/106)，通过
[PR #107](https://github.com/sudoevolve/EUI-NEO/pull/107) 补 stride/透明 alpha
回归（977f9ea，base=dev，四项 CI 通过，尚未合并）。根因没有重复实现。
Mirage 将 pin a7e625f 升级至 89c2d0f1db41514e668c43f97f613ee9e3b9b700，锁文件
同步；删除旧 FrameTexture，改回容量 1 的公开 ImageStream。详见
[EUI 升级审计](../supply-chain/eui-rgba-upgrade-20261013.md)。

M8-05 加入 product.control(pet_start/pet_stop/pet_ready)、可选 pet_state/diagnostic，
对应 golden v20。托盘菜单有载体才展示启停/恢复；starting/stopping 禁用，不自动
重启崩溃子进程。停止在保留 future 的有限 Executor 任务内完成，串行 handler 只
接纳；缓存 PID 避免关闭中的并发 OS handle 访问。owner 停生产者后 fence，再消费
future 和回收句柄；readiness 绑定所属活 PID，10 秒未 ready 则收尾并报告 failed。

### 本轮执行与结果

```sh
cmake --preset native-debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset native-debug --target tray_runtime_test ipc_protocol_golden_test \
  mira_pet mirage-tray tray_backend_test frontend_process_test -j 4
ctest --test-dir build/native-debug -R \
  '^(ipc_protocol_golden_test|tray_runtime_test|tray_backend_test|frontend_process_test|pet_resource_loader_test|pet_frames_test|pet_animation_test|pet_status_integration_test)$' --output-on-failure
python3 tests/manual/pet_preview_acceptance.py --build build/native-debug \
  --output /tmp/mirage-m8-pet-eui-fixed-20261013
```

- Debug 10/10（八个业务测试、两个隔离 fixture）。实际 SNI 菜单载体测试覆盖条件式
  三项布局、Start/Stop 路由与 pending 禁用，不以纯 DTO 替代平台菜单测试。
- ASAN/UBSAN/TSAN 的 tray_runtime_test + ipc_protocol_golden_test 各 4/4，含两隔离
  fixture；头部 sanitizer 命令沿用，TSAN 使用 setarch x86_64 -R。验证 bootstrap
  超时、错误 PID、拒绝/异常、重复 stop、在途 stop 与 shutdown、配置 owner/handle
  边界、崩溃诊断和恢复。另加入 HeldProvider 的在途 Agent 轮次，独立 stop 前、
  停止中、完全停止后 active_work 均保持非零，证明只退出桌宠；owner shutdown 才
  取消该轮次。完整 GUI sanitizer 仍未执行。
- 私有窗口/实进程 22/22。源码后移至私有载荷后不依赖原 cwd；实际角色区域/透明
  角与持续帧通过，停止后 tray/Mira IPC 仍在线，重启换 PID，SIGKILL 崩溃报告
  failed 与非空诊断，显式恢复再次 ready；隐藏正常关闭和异常失联均通过。
- 最新 sample：可见 CPU 28.66% 单核，隐藏 0.67%；VmHWM 191480 kB、VmRSS
  187192 kB；隐藏正常关闭 1.923 s，异常 owner 丢失 6.383 s。仅 idle + Xvfb/Mesa
  软件渲染样本，不代表九态全进程峰值、硬件 GPU 或实时性能保证。
- 四组合 EUI 首次未移除宿主 WAYLAND_DISPLAY 时，两个 GLFW probe 失败；明确
  隔离到 X11 后各 35/35。私有 Mirage 驱动同样移除 WAYLAND_DISPLAY 并指定
  XDG_SESSION_TYPE=x11。无跳测、无改断言；初次失败日志保留 /tmp/eui-rgba-*-test.log。

尚未进行的七态目视接缝、真实 compositor/焦点/全屏/多屏、硬件 GPU、实际 Agent
输入闭环和其他平台门禁仍由维护者负责提供目标环境后补跑。资源许可、正式安装与
偏好持久化继续属于 M8-08；不以本轮进程控制宣称它们完成。

本轮文档相对链接 8 文件/226 链接通过，公开头边界 57 头/0 违规，git diff --check
通过。新增/变更 C++ 段按 clang-format 检查；全仓格式门禁仍有基线的 RuntimeService
函数/条件换行及 IPC access/pet_status 注释对齐问题，未以全局格式化混入本次功能。
Windows 菜单条件式入口同步实现，但本机无 Windows 编译器/会话，Windows 编译与实机
运行保持未执行，负责人为维护者，提供 M8-10 目标环境后补跑。

## 同日后续：七态人工接缝通过与结果 once

维护者明确反馈：“成功和失败只用播放一次吧在实际场景中？确认7个20轮播接缝视觉没有问题”。
七个候选循环的 20 圈接缝门禁通过，源/区间/视频 SHA 与人工确认登记于
[接缝证据](data/mira-pet-loop-visual-acceptance-20261013.json)。灰底循环预览只覆盖
接缝，没有由此关闭 compositor/置顶/穿透/焦点、多屏或 GPU 峰值门禁。

实际 done/fail 调整为全片 once：成功 `[0,56)`、4648 ms；失败 `[0,55)`、6875 ms。
播完持有末帧，保留真实结果文字。同一 source/owner 的重复更新不重播，新 source
可再次播放；接管/取消立即抢占。五个运行状态维持已验收 loop，enter/exit 保留 once。
[运行 manifest](data/mira-pet-playback-v5-20261013.json) 另存，历史候选文件不覆盖。
离线审计默认使用运行 manifest，新增 --manifest 可重跑历史七态区间。

```sh
cmake --build --preset native-debug --target mira_pet pet_animation_test pet_frames_test pet_status_test -j 4
ctest --test-dir build/native-debug -R '^(pet_animation_test|pet_frames_test|pet_status_test|pet_resource_loader_test)$' --output-on-failure
python3 tools/audit_mira_pet_v5.py --source-dir apps/pet/assets
python3 tools/audit_mira_pet_v5.py --source-dir apps/pet/assets \
  --manifest docs/compatibility/data/mira-pet-loop-candidates-v5-20261012.json
```

Native 预览编译通过；Debug、ASAN、UBSAN 上述四项测试加隔离 fixture 各 6/6。
单测覆盖结果结束边界、长期末帧、同身份不重播、新身份再播放及安全抢占；实际九态
解码和 once bounds 通过。九态运行/历史 manifest 源摘要核对均通过。C++ format 与
差异空白检查通过。纯 UI clock/Clip 策略没有新增并发路径，本轮未扩跑 TSAN/全 GUI
sanitizer。当前实时视觉 owner 的快照显示 pet_state=stopped，未自动重启用户已关闭的
桌宠；新版已编译，下次显式启动即使用一次结果播放。完整结果全片的实机视觉验证仍
独立开放，七态历史循环接缝确认不是它的替代证据。

## 同日现场启动与 M8-01～05 回归（人工验证中）

基线为 `86b6778` 加 `feat/m8-pet-resource-loading` 工作树，Ubuntu GNOME Wayland
会话中的 XWayland，单显示器 eDP-1 3200×2000。执行
`cmake --preset native-debug -DMIRAGE_FETCH_DEPENDENCIES=OFF` 及
`cmake --build --preset native-debug --target mirage mirage-tray mirage-native mira_pet -j 4`
成功；通过 `env -u WAYLAND_DISPLAY XDG_SESSION_TYPE=x11
./build/native-debug/apps/mirage start --wait 20` 启动实际产品入口。
托盘 PID 931648、主窗口 PID 931669、桌宠 PID 931670；`hello` 报告 tray 与
pet_status 能力，`product.control(status)` 报告 `frontend_ready=true`、
`pet_state=running` 且桌宠 PID 与窗口 `_NET_WM_PID` 一致。X11 窗口为 240×274，
请求 `_NET_WM_STATE_ABOVE`、`WM_HINTS input=False`；当时活动窗口为 ChatGPT，
不是桌宠。以上仅证明启动与窗口属性，实际置顶、透明、穿透、焦点及关闭主窗后的
持续显示仍由维护者在本次会话中目视验证。

九态离线审计 `python3 tools/audit_mira_pet_v5.py --source-dir apps/pet/assets`
全部 SHA 匹配。八个 M8 相关测试连同两个隔离 fixture 的首次 Debug 回归为 9/10：
`tray_backend_test` 在等待 `PropertiesChanged` 时超时（第 762 行），其余通过。
单独重跑该测试 3/3（含 fixture），随后原十项命令重跑 10/10：

```bash
ctest --test-dir build/native-debug -R \
  '^(ipc_protocol_golden_test|tray_runtime_test|tray_backend_test|frontend_process_test|pet_resource_loader_test|pet_frames_test|pet_animation_test|pet_status_integration_test)$' --output-on-failure
```

首次信号超时保留为待定位的间歇结果，不以重跑通过抹除。维护者正在现场验证
M8-01 的真实状态映射、M8-02/03 的结果 once 与 GPU/窗口播放、M8-04 的 compositor/
焦点/穿透及 M8-05 的主窗关闭和托盘独立启停。结果未回填前，M8-01～05 仍为
In Progress；原生 Wayland/其他平台、多屏与实际 Agent 输入闭环不由本次单屏
XWayland 会话替代。

## 同日现场缺陷修复与人工复验

维护者初验报告：“两个动画播放间会有空白帧透明；窗口只有上半身，下半身看不见；
期望能够拖动桌宠”。检查实际 XWayland framebuffer 和 WebP 解码帧后，定位到
HiDPI（2×）下固定逻辑尺寸造成图像向下偏移，切片切换时显式清透明帧，以及整窗
鼠标穿透阻止拖动。修复在 `apps/pet/app.cpp` 和 X11 `pet_window` 边界：按实际
framebuffer 计算图像/状态条布局；切片间优先使用已缓存待机帧；将输入区域限定为
角色实体和状态条，并让两者可拖动，透明角仍穿透。仅状态安全降级且尚无中性帧时
允许清图；隐藏时仍清除解码与 UI 帧。

重新构建 `mira_pet` 成功，重新启动实际产品入口；现场截图显示完整角色和状态条，
X11 ShapeInput 区域排除透明角、包含角色与状态条。维护者复验答复“没有上述问题”，
对应透明空帧、下半身裁切和拖动三项。维护者对实际任务状态/成功失败动画与主窗口
一致性的答复为“一致”；另对“关闭主窗口后桌宠继续显示且不抢焦”和“托盘单独
停止、再启动桌宠时任务和主窗口仍正常”明确答复“两项都通过”。这些均为本次单屏
GNOME XWayland 会话的人工观察，不外推到其他 compositor 或平台。

最终源码重建后执行：

```bash
python3 tests/manual/pet_preview_acceptance.py \
  --build build/native-debug \
  --output /tmp/mirage-m8-pet-drag-20261013-final
```

私有 Xvfb/ICCCM fixture 的实进程/窗口检查 27/27，通过下半身像素、状态条、透明角
ShapeInput、实体拖动、角色帧推进、独立启停/复用、崩溃恢复、隐藏退出和 owner
失联。`pet-results.json` 记录可见 CPU 27.99%（单核口径）、隐藏 CPU 0.67%、
VmHWM 239688 kB、VmRSS 187608 kB、隐藏正常退出 1.921 秒、失联退出 6.383 秒。
该 fixture 根窗口未合成，不能用这些数值证明硬件 GPU 或真实桌面 compositor 的
峰值。首次 `tray_backend_test` 信号超时仍作为间歇问题保留。
最终重建后的同一 Debug CTest 选择集 10/10，`git diff --check` 通过。现场产品
随后经 `product.control(pet_stop)` 达到 stopped，再由 `pet_start` 达到 running；
桌宠新 PID=942333，主窗口 PID=938736 始终 ready，重启时无活动任务。

本轮已关闭以上具体现场缺陷和场景，但 M8-01～05 工作项仍为 In Progress：
M8-01 的负例/旧客户端及真实输入阶段矩阵、M8-03 的 GPU 峰值和结果逐帧 once、
M8-04 的全屏/多屏/DPI 与原生 Wayland/Windows、M8-05 的完整 Agent 输入释放
场景尚无对应实机证据。负责人为 Mirage 维护者；具备目标环境及真实任务脚本后按
[M8 测试矩阵](../plans/m8-mira-pet.md)补跑，未执行项不得据本次答复标为完成。

## 同日补充要求：位置、角色画布、Dock 与首轮播放

维护者提出四项要求：重启继承退出前的位置；取消底部状态条，仅显示角色动作；
每个已开始动画至少播完一轮，错过的动作不再补播；桌宠不作为左侧 Dock 独立应用。
实现归于 M8-03/04：240×240 角色画布、X11 SKIP_TASKBAR/SKIP_PAGER、实体拖动；
拖动结束与正常退出使用既有 LocalStateStore 原子保存 `pet-window-position.txt`，
64 字节读取上限，位置恢复按现有显示器工作区约束，移除屏幕后回退主屏。
未新增任务队列、线程或定时器。诊断仍来自真实 IPC/日志，九态之外的安全状态共用
静态姿态，具体原因由主窗口说明。

播放器资源就绪才开始计时，普通切换在 intro 加一轮 loop 或全片 once 之后采用
届时仍有效的最新状态。未开始、仍在解码的动画可以被最新状态替代；首轮期间
已经结束的动作不入队、不补播。安全状态、真实 Runtime owner 变更与退出可立即
打断，本地启动动画到 Runtime 的正常交接保持首轮。此为显示策略，Runtime 阶段
事实与业务输入控制没有延迟。

初次构建因缺少 Xatom 头导致 XA_ATOM 未声明，补头后重建通过。首轮私有窗口检查
在 Dock 标记处失败，定位为最小 WM fixture 在 MapRequest 上清空 `_NET_WM_STATE`；
fixture 改为保留已有状态，仅增删 HIDDEN 后复跑通过。真实 GNOME XWayland 窗口
随后同样观察到 SKIP_TASKBAR、SKIP_PAGER、ABOVE，位置从 (2769,1571) 恢复到同一
坐标，240×240 framebuffer 中角色完整且无状态条。该恢复首次保留旧运行实例的
实际坐标，之后由产品自身保存。

执行命令：

```bash
cmake --build --preset native-debug --target mira_pet pet_animation_test -j 4
ctest --test-dir build/native-debug -R \
  '^(ipc_protocol_golden_test|tray_runtime_test|tray_backend_test|frontend_process_test|pet_resource_loader_test|pet_frames_test|pet_animation_test|pet_status_integration_test)$' --output-on-failure
python3 tests/manual/pet_preview_acceptance.py \
  --build build/native-debug --output /tmp/mirage-m8-pet-behavior-20261013-final
```

Debug 选择集 10/10、私有实进程/窗口 31/31。新增检查覆盖角色单独画布、Dock/pager
属性、拖动保存、独立重启位置恢复和极端越界坐标约束。私有软件渲染可见 CPU 26.66%
（单核口径）、隐藏 CPU 0.33%、VmHWM 177604 kB、VmRSS 175900 kB，隐藏正常退出
1.922 秒、owner 失联退出 6.427 秒；不据此宣称硬件 GPU 峰值或多屏保证。
维护者对四项实际窗口/任务复验答复“通过”。

### 失败动画结束后回到待机

维护者随后报告失败状态长期保持最后一帧，要求回到空闲。失败全片 once 结束后
播放器现转入 rest，并保留已消费的 owner/source 身份；同次失败通知及隐藏恢复
不重播，下一任务失败可重新播放。此修改不清除 Runtime 失败结果。
最终播放器测试 211 项通过，覆盖首轮、五态各 20 圈、加载期间过时动作、相同状态
不重播、启动交接、安全打断、失败到待机及下一次失败。
现场新桌宠 PID=953098；服务仍报告同一 task_failure 且无活动任务，10 秒后实际
framebuffer 已为待机姿态，后续 0.7 秒截图有待机帧推进。维护者对“失败 once 后
待机且同次不重播”再次明确答复“通过”。最终 `git diff --check` 通过。

原生 Wayland/Windows、多屏/全屏、硬件 GPU 峰值及真实 Agent 输入释放等原门禁
未在本轮补跑；负责人 Mirage 维护者，取得目标环境和任务脚本后按 M8 矩阵补验。

## 同日阶段提交前复跑

维护者要求将本轮 M8 更新提交。提交前基线为同分支已存在的 `f59211c`；其锁定
Mira 为 `bb397387ae467a7d84e44ba4b117a2685c8d5431`、kairo 为
`32027c247dd4b9280d7c995454b732bac0db9095`，本轮 M8 仅提交已授权的 EUI pin 升级。
九份 v5 源与 provenance 纳入 `apps/pet/assets`，opt-in 预览/素材测试默认从此目录
供给，消除对个人下载目录的要求；正式安装/素材再分发门禁仍归 M8-08。

按仓库源重新 configure（`MIRAGE_FETCH_DEPENDENCIES=OFF`，
`MIRAGE_PET_ASSET_SOURCE_DIR=/home/linductor/mirage/apps/pet/assets`）并重建桌宠、托盘
和八项相关测试目标，通过；九态源审计摘要全部匹配。Debug 选择集再次 10/10。
使用 `cmake --build --preset asan --target pet_animation_test -j 2`、同命令的 `ubsan`
preset 及相应 CTest
选择集，两种 sanitizer 各 3/3（播放器及 setup/cleanup）；播放器各 211 项检查，
无 sanitizer 报告。本轮纯播放规则未新增跨上下文路径，既有生命周期 sanitizer
证据保留，未据此宣称全 GUI sanitizer 或其他平台完成。

重建后私有桌面命令同前，输出目录 `/tmp/mirage-m8-pet-commit-20261013`，31/31。
本次软件渲染 VmHWM 224112 kB、VmRSS 172356 kB，可见/隐藏 CPU 26.66%/0.33%，
隐藏正常退出 1.973 秒、owner 失联 6.437 秒；这些仍仅为该 fixture 的记录。
全部构建、截图与运行日志留在 build 或 /tmp，提交仅含实现、源素材、测试和文档。

## 同日本机 native-release 引用更新

按维护者要求，本机 `native-release` 以代码提交
`c33142b28a08923b64931d690e4ec76caa3e5a91` 重新构建主入口、托盘、主窗口和桌宠。
配置启用 `MIRAGE_ENABLE_PET_PREVIEW=ON`，源素材为仓库 `apps/pet/assets`。
首次 configure 被旧缓存中的已删除 `build/ocr-debug/_deps/mirador_ncnn-src` 阻断；
改用已有 `build/native-debug/_deps/mirador_ncnn-src` 后通过，其 HEAD 和 clean 状态
已核对为 Mirador 锁定的 `e54f7b1f88434e1d844ea0551b880a1cfb079ce1`。
没有变更依赖 pin 或产品代码。

执行 `cmake --build --preset native-release --target mirage-native-register-desktop
mira_pet pet_animation_test pet_frames_test pet_resource_loader_test -j 4` 成功；
桌面入口重新注册至 `~/.local/share/applications/org.mirage.native.desktop`，与生成入口
逐字节一致，Gio 的 Exec/Icon 均解析到 native-release。CLI 版本仍为 Mirage 0.1.0 /
Mira core 0.1.0；本次更新的是其代码构建。`build/native-release/local-build.json`
记录代码提交及四个二进制 SHA-256，属于忽略的本机构建工件。

Release 三项 M8 测试及 setup/cleanup 5/5；私有桌面检查 31/31，输出目录为
`/tmp/mirage-native-release-pet-20261013`。按注册入口的相同参数启动后，托盘
PID=1087001、主窗口 PID=1087020、桌宠 PID=1087021，`/proc/<pid>/exe` 全部位于
native-release，frontend_ready=true、pet_state=running、active_work=0。
真实启动范围为本机单屏 GNOME XWayland；其他原有 M8 平台与性能门禁保持开放。
