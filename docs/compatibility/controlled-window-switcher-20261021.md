# 显式窗口切换器验收记录（2026-10-21）

> 状态：本增量自动验证通过；M7-27 保持 In Progress
> 工作项：M7-27，关联 M7-25/26/31；依据 DEC-110/054/056/085
> 负责人：Mirage 维护者
> 基线：`4ec413a` 加 `feat/m7-27-held-pointer-travel` 累计工作树

## 交付与约束

新增独立 `desktop.window_switch.begin/advance/observe/release` 1.0.0 工具族，
回执为 `window_switch_v1`。本轮唯一实际验证的 profile 是 X11/XInput2 2.1+ 上
的 Openbox 3.6.1。Provider 默认为不支持，X11 以双向 `_NET_SUPPORTING_WM_CHECK`、
管理器 `_NET_WM_NAME=Openbox` 和根窗口 `_OB_VERSION=3.6.1` 判定；factory
和每次 driver guard 重新核对 live 身份，不能沿用过期 capability 开始输入。
这些属性是兼容性识别，不是桌面管理器真实性的安全认证，也不证明自定义快捷键配置。

begin 要求新的前台窗口 id/epoch、桌面级 `window.activate` 和 `input.inject`
授权（resource=`window_switcher`），宿主持有 Alt。advance 仅接受 forward 或
backward，分别由宿主规划 Tab 或 Shift-down/Tab/Shift-up，消耗 1/3 条命令；
begin 的 Alt-down 也消耗一条。模型不能传键、指针、路径或时长。沿用本地 key
cadence、64 条命令、15 s idle/60 s total、单写者 lease、任务/授权/设备守卫。
倒选所需命令/idle/操作 deadline 预算在首个按键前检查；中途失效仍由 session
逐步检查并清理。observe 和截图不续期，新的 host state 必须用于下一次 advance。

Openbox 在 Alt 持有期间显示 popup、选择候选，但保持原窗口焦点；释放 Alt 后
才提交选择。因此继续保留固定原窗口焦点/几何守卫，不放宽成允许任意跨窗输入。
窗口受限任务不注册该工具族，两个工具族不能相互推进或释放；固定窗口会话拒绝
跨观察组合 Alt-Tab。共享原有一个 kairo blocking worker、容量 8 FIFO 和
LatestMailbox，没有新的线程、队列、定时器或生命周期 owner。

release、取消、超时、接管和失败清理都可能提交当前选窗；不承诺撤回选择。
回执只说明输入执行/释放状态，`verified=false`，必须在释放后重新观察前台窗口。
输入归属未知时仍保留 dirty fence，不会为完成选窗而绕开既有设备来源规则。

本轮先核对 pinned Mira 的离散输入契约和工具/模型公开 API，以及 kairo 集成
router → by-requirement → blocking-io card、worker stop/join API。窗口切换器
是 Mirage 产品/平台适配，不重复实现依赖能力；没有依赖缺口或 pinned 代码变更。

## 实际平台与模型链证据

新增 [controlled_window_switch_test.cpp](../../tests/platform/controlled_window_switch_test.cpp)，
在测试私有 Xvfb 启动真实 Openbox，创建三个自有窗口，使用仓库内
[最小 WM 配置](../../tests/platform/data/openbox-switcher.xml)。测试没有向用户真实
桌面发送输入；WM 是独立测试子进程，由 fixture RAII 回收，不属于产品运行路径。

- 正选、正选、反选：每步读回 Alt 仍按下、Shift/Tab 已释放，前台仍为原窗口；
  全 display 截图与打开 popup 前的实际像素不同。observe 后仍持有，release 后
  实际 `_NET_ACTIVE_WINDOW` 变为预期中间窗口，回执没有声明业务验证成功。
- 固定 [schema/golden](../../tests/integration/data/window-switcher-v1.json) 验证工具
  版本、精确参数集、回执 schema/held keys 和两项授权 resource。无效方向、额外
  key/x/duration/press、零 sequence/version 均无新输入；重复序列不重放。
- 取消、Human Takeover、authority 换代、撤销 window.activate、意外焦点变化、
  WM 身份属性删除均使会话终结并释放，迟到命令不恢复。窗口限定任务无工具暴露；
  input.inject 拒绝及 live 版本改为 9.9.9 都在按下前失败。
- Alt-down 后执行 61 次正选，剩余 2 条命令；反选在 Shift-down 前拒绝并释放，
  command_sequence 保持 62。固定窗口会话持 Alt 后尝试 Tab 被平台边界拒绝。
  同一个 Tools 对象释放后改用固定输入族，恢复 input_session_v1，worker 注册数不增长。
- 实际 `ModelLayer` + 脚本 `IModelProvider` 走完整链：新前台观察 → begin → 正选
  → display 截图 → 正选 → display 截图 → 反选 → release → 新前台观察 → 回答。
  模型请求确有 ImagePart，截图期间真实 Alt 仍按下，下一步使用最新 host state；
  最终实际窗口正确。该证据为可重复的模型工具接线，不是在线自主模型选窗验收。

测试开发时修正了三个夹具问题：Openbox 参数应为 `--config-file`；启动完成后需
等待各 MapRequest 被真实 WM 管理；取消的 BuiltinToolRegistry 可在外层返回
Cancelled，测试 helper 只将这一返回转成失败记录。未因夹具失败放宽产品守卫。
完整模型脚本另按生产消息前缀解析 host state，避免使用截图前已过期的版本。

## 测试用 WM 来源

环境为 Linux 7.0.0-38-generic x86_64、GCC 13.3.0、CMake 3.28.3。Openbox
来自当前 apt 配置的 Ubuntu noble 官方归档，使用 `apt-cache show` 的 SHA-256
逐包核对。只解包到 `/tmp/mirage-openbox-fixture/root`，没有安装系统软件，
没有增加产品依赖、修改锁文件或把二进制加入仓库。

| 包 | SHA-256 |
| --- | --- |
| openbox 3.6.1-12build5 amd64 | `bd27fc0ce0b98ecf29a3e4987744b59590c71f87b0f1bc02175228f155b22c71` |
| libobrender32v5 3.6.1-12build5 amd64 | `832c02836f8b0fb06111cf96827ef90f233d28b63d6f34cda20930b33af74009` |
| libobt2v5 3.6.1-12build5 amd64 | `82cb85897e7f0386db66e4a3e0f8442286e6b5b52fade9032f3dbd9de6ee32ff` |
| libimlib2t64 1.12.1-1.1build2 amd64 | `305659a4956d4b97db0ee088d0dc71ee068504d7610bbde025729ed9d5b78f29` |

前三项主体许可证为 GPL-2+（文件级 BSD/GPL-3+ 例外见包内 copyright），imlib2
为包内 copyright 所载 imlib2-license；仅测试进程使用。下载时配置代理返回 502，
以该命令的 `Acquire::http::Proxy=DIRECT` 重试
成功，没有改动全局代理配置。运行 wrapper 如下（已有系统 Openbox 时直接指定
其绝对路径即可）：

```sh
#!/bin/sh
export LD_LIBRARY_PATH=/tmp/mirage-openbox-fixture/root/usr/lib/x86_64-linux-gnu
export XDG_DATA_DIRS=/tmp/mirage-openbox-fixture/root/usr/share:/usr/share
exec /tmp/mirage-openbox-fixture/root/usr/bin/openbox "$@"
```

## 复跑配置

沿用 [持键移动记录](controlled-input-travel-20261021.md) 的隔离验证目录
`/tmp/mirage-m7-27-validation`，最终实现/测试逐文件 SHA-256 与主工作树对账后
构建。Mira `a29374a`、kairo `32027c2`、Mirador `5565436`、eui-neo `63117f7`
匹配锁。新验收依赖真实 WM，默认 OFF；明确 ON 时缺 Openbox 会配置失败，不以
静默 skip 声称通过。所有 preset 均 native/OCR/pet preview OFF。

```sh
cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_ENABLE_PET_PREVIEW=OFF -DMIRAGE_BUILD_X11_SWITCHER_ACCEPTANCE=ON -DMIRAGE_OPENBOX_EXECUTABLE=/tmp/mirage-openbox-fixture/openbox
cmake --build --preset debug -j 3
ctest --preset debug --output-on-failure -j 4
```

ASan/UBSan configure 使用相同选项，把 preset 换为 asan/ubsan。实际定向命令
如下，将 asan 换为 ubsan 即是 UBSan；8 个测试及 2 个状态隔离夹具：

```sh
cmake --build --preset asan -j 3 --target controlled_window_switch_test controlled_input_devices_test controlled_input_session_test controlled_input_x11_test x11_backend_test input_session_contract_test desktop_action_policy_test post_action_verification_test
ctest --preset asan --output-on-failure -j 2 -R '^(controlled_window_switch|controlled_input_devices|controlled_input_session|controlled_input_x11|x11_backend|input_session_contract|desktop_action_policy|post_action_verification)_test$'
```

TSan configure 同上，仅为 ctest 子进程关闭随机地址布局以避免已知 runtime mapping
冲突，不改系统 ASLR：

```sh
cmake --build --preset tsan -j 3 --target controlled_window_switch_test controlled_input_devices_test controlled_input_session_test controlled_input_x11_test
setarch x86_64 -R ctest --preset tsan --output-on-failure -j 2 -R '^(controlled_window_switch|controlled_input_devices|controlled_input_session|controlled_input_x11)_test$'
```

## 最终结果

| 配置 / 检查 | 实际结果 | 范围 |
| --- | --- | --- |
| Debug 全量 | 109/109，77.68 s，无 skip | 含明确启用的真实 WM 验收 |
| ASan 定向 | 10/10，27.11 s，无 sanitizer 报告 | 8 个测试 + 2 个状态隔离夹具 |
| UBSan 定向 | 10/10，28.46 s，无 sanitizer 报告 | 同上 |
| TSan 定向 | 4/4，15.00 s，无 race 报告；setarch -R | 4 个受控输入/选窗测试 |
| 公开头文件边界 | 65 个头文件，零违规 | 2 项既有 DEC-096 seam |
| 可选平台编译 | 通过 | x11_input_session / x11_backend 无 XInput2 与 stub 语法检查 |
| 格式 / 源码 / 文档 | 通过 | 15 个 C++ 文件格式、19 个实现/测试/配置文件 SHA-256 对账、510 个本地链接、JSON/XML 解析和补丁空白 |

最终 worker 复用与 live 未知版本断言加入后重建了四种配置的新测试，再执行上述
全量/定向集合。完整 Debug 构建与所有生产目标构建在最终测试修改前已通过；随后
只有测试与文档变化。未把初始旧测试二进制或中间结果当成本表的最终证据。

## 2026-10-21：native-release 更新与提交前验证

按维护者要求在主工作树 `/home/linductor/mirage` 更新现有 native-release，保留
Release、native frontend、pet preview、PP-OCR、YOLO 与视觉资源打包 ON。依赖
使用 verify-only 配置，锁定提交均匹配；只在本地 cache 开启 Openbox 验收，不改变
仓库默认值或引入产品 WM 依赖。此次同时覆盖前序持有中移动和设备守卫累计变更。

```sh
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_BUILD_X11_SWITCHER_ACCEPTANCE=ON -DMIRAGE_OPENBOX_EXECUTABLE=/tmp/mirage-openbox-fixture/openbox
cmake --build --preset native-release --target mirage-native-product mirage-boundary-check controlled_window_switch_test controlled_input_devices_test controlled_input_session_test controlled_input_x11_test portal_input_test desktop_image_supply_test input_cadence_test harness_desktop_tools_test post_action_verification_test desktop_action_policy_test permission_test task_permission_test permission_ipc_test input_session_contract_test provider_contract_test smooth_move_schedule_test x11_backend_test -j 4
ctest --preset native-release --output-on-failure -j 2 -R '^(controlled_window_switch|controlled_input_devices|controlled_input_session|controlled_input_x11|portal_input|desktop_image_supply|input_cadence|harness_desktop_tools|post_action_verification|desktop_action_policy|permission|task_permission|permission_ipc|input_session_contract|provider_contract|smooth_move_schedule|x11_backend)_test$'
./build/native-release/apps/mirage --version
```

结果：完整 `mirage-native-product` 与 17 个定向测试目标构建通过；测试 19/19
通过（含 2 个状态隔离夹具），39.57 s，无 skip。65 个公开头文件边界零违规，
保留 2 项既有 DEC-096 seam；launcher 输出 mirage 0.1.0 / mira core 0.1.0，
正常退出。未执行 native-release 全量测试，前节 Debug 全量和 sanitizer 结果保持
原有范围。本次未启动或重启产品服务/GUI，测试输入仍仅发送到私有测试桌面。

构建产物位于 `build/native-release`，未纳入提交。更新后的实际文件 SHA-256：

| 产物（相对构建目录） | SHA-256 |
| --- | --- |
| apps/mirage | `750b1251620cd5f65333c9220e3c1b5006c68d31783a0d1d1475491aff709c19` |
| apps/mirage-service | `13cf5fa486021e6a9c27c97a1c335be86ed36f301edd52c7f9d690b7b738cda3` |
| apps/tray/mirage-tray | `b4f3335bb85826ba5f273b04dad9f359dd21b67bc0d52b023345b6314b71b4d5` |
| apps/native/mirage-native | `356653e54dec89d41f773d132841835984686d01062dd3211de4e6011a5bf071` |
| apps/pet/mira_pet | `2c8ac868ae732e028f05cfd60df03fa84d138de4914875e0a3a3db2eca194dfe` |

## 剩余门禁

M7-27 保持 In Progress。本轮只证明上述 Openbox 3.6.1/私有配置；GNOME/KDE/
其他 WM、用户自定义 Alt-Tab 行为、Portal 跨调用持有、Windows、跨窗口拖放、IME、
真实物理竞争/拔插、X 服务失联、在线自主模型与人工手感均未验收。原因是尚未
交付对应 profile 或本轮测试环境不含所需平台/设备/模型；负责人 Mirage 维护者，
补跑条件为对应实现、受控桌面/应用/设备和模型具备。native-release 已按上节补齐
产品构建与定向回归，不据私有 WM 扩大平台声明，也不承诺 X 服务卡死下的硬释放期限。

前序证据：[持键首接线](controlled-input-sessions-20261021.md)、
[持有中移动](controlled-input-travel-20261021.md)、
[设备来源与拓扑守卫](controlled-input-devices-20261021.md)。
