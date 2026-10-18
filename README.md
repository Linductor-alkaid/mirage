# Mirage

[![ci](https://github.com/Linductor-alkaid/mirage/actions/workflows/ci.yml/badge.svg)](https://github.com/Linductor-alkaid/mirage/actions/workflows/ci.yml)

Mirage 是基于 [Mira](https://github.com/Linductor-alkaid/mira) 构建的 Linux / Windows
桌面端产品：为 Mira Agent Harness 提供完整的 PC 运行环境、桌面交互能力与产品界面，并
以 [Mirador](https://github.com/Linductor-alkaid/mirador) 作为视觉基础设施处理桌面
截图与局部图像的 OCR、目标检测、几何结构与视觉 Cache。

设计文档：[docs/design/Mirage：Linux - Windows 桌面端设计方案.md](docs/design/Mirage：Linux%20-%20Windows%20桌面端设计方案.md)。

已采用 EUI-NEO 原生前端；旧 TS/CEF 与 Web 调试桥已按 DEC-037 退役。构建并打开：
`cmake --preset native-release`，
`cmake --build --preset native-release --target mirage-native-product`，
`./build/native-release/apps/mirage start`。
托盘内嵌 Agent Runtime，持有独立前端子进程；窗口关闭保留任务，托盘退出有活动工作时
要求确认。无在线托盘不能打开前端，Linux 需要 SNI 通知区宿主。模型设置与无屏幕通用
harness 已接入。Linux 会话已接结构化桌面工具；模型设置开启“识图”后，可逐次授权
display/window/ROI 截图并将 PNG 送入下一轮模型输入，见
[M7 验收范围](docs/compatibility/desktop-image-supply-20261009.md)。
桌面修改后强制新观察，按动作前声明的窗口/语义/OCR 文本后条件核验；未验证或不确定结果
不能进入成功终态。Linux X11 的激活、文本回显与提交落盘已取证，见
[动作后核验](docs/compatibility/post-action-verification-20261009.md)。
真实 OCR/SoM 已交付 Linux 首增量；在线模型选号、宿主与 Windows/安装包验收继续推进，见
[原生前端说明](apps/native/README.md) 与 [DEC-045](docs/decisions/DEC-045-tray-runtime-owner.md)。

## 仓库结构

```text
mirage/
├── apps/          # 可执行入口：cli、service、tray、native（EUI）
├── runtime/       # Mira Host、Runtime Service、IPC、持久化、权限
├── desktop/       # 跨平台 Desktop Environment（Provider 接口与 Observation）
├── platform/      # Linux（AT-SPI2 / X11 / Wayland）与 Windows（UIA / Win32）后端
├── integration/   # pinned mira / mirador / mcp 的适配边界
├── tests/         # 测试（自研轻量 harness，ctest 标签驱动）
├── third_party/   # pinned 依赖：mira、mirador、UI 专用 eui-neo（git submodule）
└── docs/          # 设计、决策、计划、规范与验证记录
```

依赖方向固定为 `apps/ui -> runtime -> desktop -> platform`，`integration` 是 pinned
依赖的唯一边界层。规则详见根 [AGENTS.md](AGENTS.md) 与
[docs/project/project-standards.md](docs/project/project-standards.md)。

## 构建

依赖以 pinned submodule + `dependencies.lock.json` 锁定，configure 时自动校验
commit（[DEC-001](docs/decisions/DEC-001-dependency-pinning.md)）：

```bash
git submodule update --init --recursive   # 或依赖 configure 时的自动同步
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/apps/mirage --version
```

可用预设：`debug`、`release`、`asan`、`ubsan`、`tsan`（sanitizer 覆盖源码树内构建的
依赖目标）。注意：内核 ASLR 熵较高时 TSan 可能报 `unexpected memory mapping`，用
`cd build/tsan && setarch $(uname -m) -R ctest` 运行即可（见
[M1 验证记录](docs/plans/m1-mira-host.md)）。离线/CI 只校验模式：
`cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF`。

X11 Backend 测试（M2-02 起）需要本机有 Xvfb：`sudo apt install xvfb`，或无 root
时将发行包提取到用户前缀并导出 `MIRAGE_XVFB=<路径>`（定位顺序
`$MIRAGE_XVFB` → `$PATH` → `~/.local/mirage-sysroot/usr/bin/Xvfb`；缺失时
`x11_backend_test` 响亮失败而非跳过，[DEC-015](docs/decisions/DEC-015-linux-backend-dependencies-and-event-loop.md)）。

Linux 桌宠与桌面工具的整窗避让（M8-06，
[DEC-103](docs/decisions/DEC-103-pet-desktop-exclusion.md)）需要 `libxres-dev`、
`libxext-dev` 和支持 X-Resource 1.2 / Shape 的 X server。缺少这些能力时，运行中的
桌宠会使相关桌面工具明确拒绝，不能以标题或 PID 白名单绕过；未启动桌宠的嵌入不受影响。
本增量仅验证 X11，原生 Wayland/Portal 和 Windows 保留独立门禁；这些环境如需使用
桌面工具，可先从托盘停止桌宠，避免呈现排除能力缺席导致拒绝。
直接 IPC `desktop.observe` 也执行排除与恢复；配置 pet owner 的视觉请求需要启用
本地 OCR 管线，在排除后新采集，仅有缓存视觉注册表时明确拒绝。

Windows Backend 编译门禁（M4-01 起，[DEC-017](docs/decisions/DEC-017-windows-backend-toolchain-and-event-loop.md)）：
在 Linux 主机上用 MinGW-w64 交叉构建平台子集——无 root 时按用户前缀引导
（`apt-get download g++-mingw-w64-x86-64` 及其依赖 + `dpkg -x` 到
`~/.local/mirage-mingw`），然后：

```bash
export MIRAGE_MINGW_PREFIX=~/.local/mirage-mingw
cmake -S . -B build/win64-cross -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
      -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build build/win64-cross --target mirage_desktop mirage_platform win32_backend_test
```

交叉构建只证明「可以编译」（工程规范第 7 节证据分级）；运行取证来自 CI 的
MSVC windows 作业（platform 子集 + 可运行测试）与维护者 Windows 机器。
MSVC 为产品主工具链，MinGW-only 的代码不被接受。

## 运行 Runtime Service 与 CLI（M1）

后台 Runtime Service 经 Local IPC 服务于 CLI / GUI（[DEC-007](docs/decisions/DEC-007-local-ipc-and-runtime-service.md)）：

```bash
mkdir -p /tmp/mirage-demo && printf hello-mirage > /tmp/mirage-demo/note.txt
./build/debug/apps/mirage service start \
    --read-root /tmp/mirage-demo           # 拉起 mirage-service 并等待就绪
./build/debug/apps/mirage service status
TASK=$(./build/debug/apps/mirage task submit \
    --goal "read a file and run a shell command" \
    --read /tmp/mirage-demo/note.txt --exec "printf hello-mirage" | cut -d' ' -f2)
./build/debug/apps/mirage task list
./build/debug/apps/mirage task inspect "$TASK"
./build/debug/apps/mirage task cancel "$TASK"   # 运行中任务可随时取消
./build/debug/apps/mirage service shutdown
```

M1 任务由 Service 内的宿主侧驱动循环按提交的有序 steps（`--read` / `--exec`）确定性
推进。文件读取强制 `--read-root` 读范围（未声明时拒绝一切读取，越界/symlink 逃逸
fail closed），命令执行有长度/时长/输出预算并支持协作取消；范围、预算与取消语义见
[DEC-009](docs/decisions/DEC-009-provider-scope-budget-cancellation.md)。

Desktop Permission 判定（`RULE-05`，[DEC-010](docs/decisions/DEC-010-m1-permission-framework.md)）
在每一步动作副作用前生效，默认策略为读取与执行放行、写入拒绝。可用
`service start --perm CAPABILITY=allow|confirm|deny`（可重复）调整，配
`--confirm allow|deny` 选择确认挂点结果（默认拒绝，确认 UI 属 M5）：

```bash
./build/debug/apps/mirage service start \
    --read-root /tmp/mirage-demo \
    --perm process.execute=confirm --confirm allow
```

被拒绝的步以 `permission_denied` 结算并在 `task inspect` 中显示
`perm=denied` / `perm=confirmation_rejected`；放行的步显示 `perm=allowed` /
`perm=confirmed`。M1 拓扑仍限开发与测试用途
（[DEC-008](docs/decisions/DEC-008-m1-environment-binding-and-reference-providers.md)）。

### 本地配置与任务恢复状态（M1-07）

Mirage 自有状态经 `runtime/persistence` 落盘
（[DEC-011](docs/decisions/DEC-011-m1-local-state-persistence.md)，schema v1
JSON + 原子写入）：

- **任务恢复状态**：`task-recovery.json`，默认在
  `$XDG_STATE_HOME/mirage/`。任务结算与停机时写入快照，服务重启后历史任务
  仍可 `task list` / `task inspect` 观察（对已恢复任务 `task cancel` 以
  `invalid_state` 拒绝）。`service start --state-dir DIR` 换目录，
  `--no-recovery` 关闭。
- **本地配置**：`service.json`，默认路径 `$XDG_CONFIG_HOME/mirage/`（M1 仅
  显式加载）。`service start --config PATH` 以文件为基线，命令行旗标逐项
  覆盖；文件损坏时启动失败（fail closed），恢复文件损坏时降级运行并告警。

持续集成见 [.github/workflows/ci.yml](.github/workflows/ci.yml)：格式与公共头
边界检查 + `debug` / `release` / `asan` / `ubsan` / `tsan` 预设矩阵（TSan 在
runner 上以 `setarch -R` 关闭 ASLR 运行），依赖以 verify-only 模式在
configure 阶段校验锁定。

## 开发流程

计划与进度见 [docs/plans/mirage-implementation-plan.md](docs/plans/mirage-implementation-plan.md)，
架构与产品默认值决策见 [docs/decisions/](docs/decisions/)，依赖（mira / mirador）问题反馈见
[docs/dependency_feedback/ledger.md](docs/dependency_feedback/ledger.md)。提交与 MR 规范、
验证证据要求见 [docs/project/project-standards.md](docs/project/project-standards.md)。

### 默认 OCR 与编号图（M7-13 / M7-14）

Linux 原生产品的 `native-debug/release` 预设默认封装已验证的 Mirador、ncnn CPU 和
PP-OCRv5 mobile 资源，首次启动自动加载，无需填写模型清单或安装推理环境。资源仅在
构建期按固定提交和哈希获取，运行期离线使用；包内包含模型、字典和许可证。
自定义资源可在「设置 → 桌面工具」保存并重启托盘，显式空清单保持关闭。headless
开发服务仍用 `mirage-service --ocr-manifest PATH` 显式启用。默认增加
`desktop.vision.ocr`，并为 visual observation 接入单显示器 PP-OCR 管线。
正式 pin 的公开语料字符/位置复验 4/4，见
[正确性记录](docs/compatibility/real-ocr-correctness-20261009.md)；默认包供应链见
[资源审计](docs/supply-chain/bundled-vision-20261011.md)，资源格式见 [DEC-057](docs/decisions/DEC-057-real-ocr-composition.md)。

支持图片的模型同时获得 `desktop.vision.mark`，同源编号图可按全局像素 ROI 裁剪。
允许桌面修改且 Backend 支持完整输入时获得 `desktop.vision.click`：使用本轮 scope、
编号、epoch 和窗口 ID，点击前核对新截图、几何与前台，动作后强制新观察。
编号不声明交互语义；严格像素核对会拒绝动画。契约见
[DEC-058](docs/decisions/DEC-058-scoped-visual-marks.md)，范围见
[验证记录](docs/compatibility/scoped-visual-marks-20261010.md)。

明确未派发的参数拒绝可以在重新观察后修正；错误回执会指出字段名，并保留可解析的
action_id。历史中的失败卡片不会把后续已验证的纠正再次判失败。编号超预算时按提示
缩小 ROI；空编号不能作为目标。未知或不确定输入仍需核验/接管，详见
[恢复验收](docs/compatibility/desktop-rejection-recovery-20261011.md)。

Linux X11 Backend 另支持 `desktop.vision.scroll` / `desktop.vision.drag`（M7-09）：
滚动使用当前编号及两轴离散步数，总量最多 16；完整左键拖拽使用同 scope 起点/
终点编号，最多 2048 像素、16 段、1 秒，取消或超时仍释放自有按钮。继续要求
新截图重验和固定后条件；派发不等于任务成功。首增量及其他平台限制见
[验收记录](docs/compatibility/bounded-scroll-and-drag-20261010.md)。
原生 GTK 滚动与分隔条效果已[另验](docs/compatibility/gtk-pointer-effects-20261010.md)；
原生列表 XDND 已在系统 XRes 可用时通过[正负验收](docs/compatibility/x11-native-dnd-20261010.md)。
缺少 XRes 时安全拒绝；用户 Xorg 的滚动、重排、禁用与取消已分别
[取证](docs/compatibility/user-host-gtk-pointer-20261011.md)，间歇守卫拒绝原因与其他平台仍待验。

动作可预先声明 `verify_kind=visual_text_exists` 与 `expected_text`，随后用新截图/OCR
核验目标窗口内唯一文本命中（confidence ≥ 0.8）；`desktop.action.verify` 只读重验
原条件，不重新输入。受控真实 Xvfb Canvas 四类文本与画面不变负例 5/5，契约见
[DEC-059](docs/decisions/DEC-059-visual-text-postconditions.md)，范围见
[Canvas 验收](docs/compatibility/visual-text-canvas-20261010.md)。用户宿主与无闭合
轮廓图标装配仍待验；本地模板与在线选号增量见下文。

无文字闭合区域可在清单中显式设置 `"geometry_marks": true`（缺省关闭），让
`desktop.vision.mark` 复用 Mirador 几何提案生成编号。只接受面积不超过整帧 1/4 的
几何候选，不赋予按钮/交互语义；普通 OCR 和业务后条件仍使用原路径。受控 Xvfb
两个无文字目标与不重绘负例已验，见
[DEC-060](docs/decisions/DEC-060-geometric-visual-marks.md)和
[几何编号验收](docs/compatibility/geometric-visual-marks-20261010.md)。
可另在清单中配置审核过的 `templates`，以本地 BGRA8 patch 匹配闭合几何候选。
编号映射中的 `template_id` 表示唯一外观匹配；点击仍使用当次 scope/编号。受控 Xvfb
双模板、位置交换与不重绘负例已验，配置见
[资源说明](docs/supply-chain/visual-template-resources-20261010.md)，边界见
[DEC-061](docs/decisions/DEC-061-geometric-template-resources.md)和
[模板验收](docs/compatibility/geometric-template-resources-20261010.md)。
MiniMax-M3 已实际从公共编号图区分两个图案，位置交换和不重绘门禁 3/3，见
[在线 Canvas 验收](docs/compatibility/online-visual-canvas-20261010.md)。候选没有模板
身份提示；不重绘时宿主拒绝成功回答。无闭合轮廓图标、用户宿主、跨主题/尺度与
其他在线模型仍待验。


高分辨率显示器上的小窗口可在清单显式设置 `"roi_capture": true`（缺省关闭）：
窗口范围或 mark crop 在采集前生效，编号、全局坐标和点击前像素重验使用同一 ROI。
owner 可为后条件装配局部 OCR，保留物理显示器身份与原预算；默认全屏路径不变。
契约见 [DEC-063](docs/decisions/DEC-063-regional-visual-pre-capture.md)，
[4K 局部验收](docs/compatibility/regional-visual-capture-20261010.md)单独记录证据。
该范围不声明全屏 4K、任意 DPI、跨屏窗口或原生 Wayland 支持。


2026-10-11（M7-12）：产品托盘已显式探测 Linux 桌面 Provider；新增桌面工具设置、
持久化 OCR 清单与统一启动器传递。DEC-068 后普通 native 产品默认带固定视觉包；首次启用、
坏资源恢复和平台范围见[原生使用说明](apps/native/README.md#桌面工具预览m7-12--linux-xorg)。
实际原生 MiniMax-M3 OCR/编号图任务已完成，产品检查 23/23；完整结果见
[产品启用验收](docs/compatibility/product-desktop-vision-20261011.md)。

2026-10-11（M7-11 首增量）：产品 workflow 目录新增 `desktop.knowledge.activate`，
IR 保存应用/页面/目标语义，执行时重新解析引用、权限判定、一次激活并验证新后条件，
已知步骤无需模型请求。可通过现有 CLI/IPC 发布运行；原生会话选知识
与自动跨重启恢复仍在后续范围。见[示例](docs/examples/local-semantic-workflow.md)。

2026-10-11（M7-11 第二增量）：workflow 新增 `desktop.knowledge.visual_click`，
保存窗口标题、页面文字与 OCR/模板目标，每次获取新证据、唯一解析、一次点击并验证
新 OCR 后条件。Linux 默认视觉包可直接承载 OCR；应用模板沿用已校验资源清单。
已知步骤无需模型请求；原生会话自动选择、恢复及跨重启资产恢复仍待推进。
见[操作示例](docs/examples/local-visual-workflow.md)与[验收](docs/compatibility/local-visual-workflow-20261011.md)。

2026-10-11（M7-13 / M7-14）：固定视觉包默认启动、32 MiB 高 DPI 采集与 1600 边只读
预览、4096 节点语义树及明确无副作用拒绝恢复已接入。观察和工具参数仍需符合范围、
权限与时效；截图不生成 @e 引用，不确定副作用保持封锁。结果与实际限制见
[验收](docs/compatibility/bundled-vision-and-tool-recovery-20261011.md)。

会话应用入口（DEC-071）：有桌面工具权限时，可从 `desktop.application.list` 的最新 scope /
epoch 调用 `desktop.application.launch` 或 Linux `desktop.application.open_uri`。后者将一个
HTTP(S) URL 交给指定已安装应用，包括已运行的浏览器。`application_running` 仅验证进程
存在；网页与业务结果仍需新窗口列表和页面观察。Linux 默认钥匙环若为 GNOME 临时
session 集合，新保存的 Key 改用已存在的持久 login 集合；旧临时 Key 需在新版设置中
重新保存一次。不存在的旧凭据清理成功，锁定/歧义/服务失败仍有明确诊断。

### 通用 GUI 回退（DEC-072 / M7-15）

快捷应用/网址工具不可达时，会话可利用截图区域点击、完整键盘组合和文本输入继续操作。
`desktop.vision.click_region` 不需要 OCR 编号：选择已供给图片中的区域，显式绑定窗口或
显示器上下文，执行前重新捕获该区域核对源像素；支持桌面图标、面板及纯图形控件。
五秒供图新鲜度保持，已供图意图最长六十秒且必须本地重验，旧图不能证明操作后的结果。
窗口/显示坐标由产品按图片比例转换，无来源坐标仍不可用。例子见
[GUI 操作示例](docs/examples/gui-fallback.md)。线上自主选路和任意应用范围需独立验收。

### 原生 Wayland 观察（DEC-073 / M7-10 首增量）

Linux 默认桌面后端按会话选择：Wayland 使用 ScreenCast/PipeWire，用户在系统窗口选择
一个显示器并点击“共享”，本轮任务可复用共享读取最多八张新图，任务结束即关闭。仅缺 SDK 或授权前确认接口不存在时
使用 Screenshot；失败不重复弹窗。产品包带运行库依赖，用户无需配置 OCR/ncnn 或 SDK。
识图模型可取得图片，文本模型可使用默认 OCR；现有 screen.capture 权限仍生效。

工具按实际能力开放 `desktop.screen.capture({"source":"desktop"})` 和
`desktop.vision.ocr({})`（DEC-075）：图片与 OCR 文字只读，不生成
显示器/窗口 ID、可执行坐标、区域点击或 XTest 输入；原生 Wayland 的键鼠需要后续
RemoteDesktop 会话增量。每轮最多一次授权，每次新图重新检查权限，取消/超时/共享撤销明确反馈；授权结束后
起算供图期限。GNOME 3200×2000 实机单帧已取得，Screenshot 保存成功但 URI 回传失败
的负例保留，见[验收记录](docs/compatibility/wayland-portal-observation-20261011.md)。

DEC-074 已验证 GNOME 一次授权后的连续观察及接管关闭；应用列表候选在执行前与
权限等待后本地重验，等待共享或模型响应不再受原五秒候选期限限制。见
[任务内共享验收](docs/compatibility/task-scoped-wayland-observation-20261011.md)。

DEC-075 将默认 PP-OCR/ncnn 接到当前任务的新共享图像，首次请求一次系统共享，
后续与截图共用八帧预算；无需显示器或窗口 ID。文本框采用预览图像像素，不能用于点击。
被 Mirage 遮挡时需用户移开或最小化窗口，再重新观察。见
[原生图像 OCR 验收](docs/compatibility/native-shared-frame-ocr-20261011.md)。

DEC-076：密集文字的原生 OCR 返回完整区域前缀，`truncated=true` 表示未覆盖全部
文字，不能据此断言页面没有某段文字；仍保持 16 KiB 总预算。共享流超时关闭会话，
错误中提供有限阶段诊断；下述 DEC-077 增加任务内原生 Wayland 键鼠。

### 原生 Wayland 键鼠（DEC-077 / M7-10 第五增量）

Agent 在第一张 `desktop.screen.capture` 中传 `source=desktop, interaction=true`，
经 Mirage 的读屏/输入权限和系统 RemoteDesktop 授权，共享单显示器及键鼠。
后续 `desktop.session.click/key/type_text/scroll` 使用已供模型的 image_scope、
environment_epoch 和图内目标矩形；前置新采集重验像素，后置新图回流。只读任务省略
interaction，不请求输入；同任务不能变更授权模式或在失败后再次弹授权。
输入支持组合键、最多 4096 字节 printable ASCII 和有界滚动，不暴露持续按下接口。
非 ASCII 明确拒绝，不把 GNOME 丢弃 Unicode keysym 的 D-Bus 成功当成输入完成；
输入法对 ASCII/Enter 的处理仍需要据新图核对。静态画面未交帧时有界失败关闭。
工具卡“局部已核验”仅说明释放和新观察（或同源文字命中），页面业务仍需模型据新图核对。
GNOME 锁屏禁止创建会话，需要用户解锁；不改变锁屏/系统安全配置。
共享前需打开“允许远程交互／远程控制”，只共享显示器会返回键鼠权限缺失。
本机英文输入状态下完整 URL、Enter、点击与滚动实测通过；中文输入法兼容仍待验。
本增量不提供 Wayland 窗口/焦点查询、拖拽、多显示器跨屏或任意动态 DPI 保证。
验收状态见 [Wayland 键鼠验收](docs/compatibility/wayland-task-input-20261011.md)。

### 记住 Wayland 桌面授权（DEC-078）

从托盘完整退出旧进程并重新启动后，点击会话输入框内的访问模式按钮，选择「完全访问」以申请并记住键鼠授权；
需要重试时在设置页的「桌面授权」中点击「授权并记住」。系统共享窗口中开启远程控制并允许记住选择。偏好保存在本机配置，
单次恢复令牌仅保存在系统钥匙环；后续任务及重启创建新会话尝试恢复。
「忘记授权」停止复用并删除本机令牌；系统仍可撤销授权或要求重新选择显示器。
未记住授权或系统拒绝恢复时仍需授权。当前自动验证已覆盖恢复协议；真实 GNOME 跨任务、跨进程免弹窗
验收按维护者要求暂缓，见[验收记录](docs/compatibility/remembered-desktop-authorization-20261011.md)。

### 会话访问模式（DEC-080）

输入框菜单仅包含三个模式选项，选择后收起；桌面授权管理位于设置页。
对话模式只讨论；视觉模式可观察屏幕/OCR，不能修改；完全
访问可使用已授权的桌面操作。完全访问选择与键鼠授权联动，视觉首次实际读屏时
由系统请求共享；授权与任务运行时先完成或停止，再切换模式。已记住的系统授权
不覆盖模式限制，已有偏好继续保留。模型每轮获得实际工具集合，OCR 刷新后的
可交互引用与画面一起回流。原生静态读屏可复用最新源帧，动作验证仍要求新增源帧。

### 默认思考与任务预算（DEC-082）

MiniMax-M3 的默认档显式开启独立思考通道，正文直接作答，思考放在折叠过程里；
可选择「关闭」，其他模型默认行为保持。已知 Messages 思考型号最多 16 次模型调用、
32 次工具执行；单次模型请求默认 2 分钟，Agent 整任务独立默认 10 分钟，仍可停止。
会话访问菜单左对齐访问按钮并在其上方展开，间隔 4px。最新验证见
[思考通道、任务预算与菜单锚点](docs/compatibility/reasoning-and-menu-anchor-20261012.md)。
