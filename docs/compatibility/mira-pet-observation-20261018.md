# M8-06：直接 IPC 观察的桌宠排除（2026-10-18）

> 关联：M8-06；DEC-103 第二增量
> 工作树：`feat/pet-desktop-exclusion`，基线 `dc0d044` 加首增量和本轮未提交变更
> 负责人：Mirage 维护者
> 前序：[桌面工具整窗排除首增量](mira-pet-desktop-exclusion-20261018.md)

## 范围与契约

直接 `desktop.observe` 与会话/workflow 工具共用受管桌宠呈现租约。handler 已在
Runtime serial owner 内，直接准入以避免 submit/get 自等待；没有新增线程、队列或
调度器，没有修改 pinned 依赖。语义、窗口和指针观察也在排除内组装，核验和显式
恢复完成后才发送响应，失败不返回成功观察。

有 pet owner 的视觉请求必须由运行中的视觉管线在排除后重新采集，缓存注册表不能
证明先排除再捕获。即使 pet 当前停止，只有缓存时仍以 `unavailable` 拒绝。没有 pet
owner 的嵌入保留原注册表投影路径。权限在隐藏前判断，拒绝不触碰桌宠。

统一五秒协作预算覆盖权限、采集和组装；运行期取消/超时也释放租约。清理不受取消
打断，预算不代表强制终止平台调用。使用 refresh 的确切视觉代次；组装/恢复期间
ActionCoordinator epoch 或视觉注册表代次变化则拒绝，避免混用其他 producer 结果。
wire 字段和协议版本未变，错误仍为 `unavailable` / `permission_denied`。

## 环境与复现

Linux x86_64、GCC 13.3.0、CMake 3.28.3、Ninja。native-release 启用 pet/native/
PP-OCR，使用已有本地 PP-OCRv5-mobile 资源；sanitizer 使用既有非 native presets。
依赖及 XRes 开发 sysroot 与首增量一致。未连接在线供应商、未触碰用户桌面或运行实例。

```bash
cmake --build --preset native-release -j3
ctest --preset native-release
ctest --preset native-release \
  -R '^(pet_observation_test|observation_face_test|runtime_service_test|pet_exclusion_test|pet_exclusion_tools_test|tray_runtime_test)$'

# 对 asan / ubsan / tsan 各构建以下三个测试。
cmake --build --preset asan \
  --target pet_observation_test runtime_service_test observation_face_test -j3
ctest --preset asan -R '^(pet_observation_test|runtime_service_test|observation_face_test)$'
ctest --preset ubsan -R '^(pet_observation_test|runtime_service_test|observation_face_test)$'
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan \
  -R '^(pet_observation_test|runtime_service_test|observation_face_test)$'

python3 tests/manual/pet_preview_acceptance.py \
  --build build/native-release \
  --output /tmp/mirage-m8-observation-native-20261018-final2 \
  --exclusion-probe build/native-release/tests/pet_exclusion_test \
  --observe-exclusion
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check
python3 -m py_compile tests/manual/pet_preview_acceptance.py tests/manual/x11_wm_fixture.py
git diff --check
```

手动驱动使用带 python-xlib/Pillow 的默认 Python（本机 miniconda3）。它创建独立
XDG、Xvfb 1500×1000、DBus 和最小 ICCCM peer，将真实 tray/pet/resources 搬到临时
目录运行。`--observe-exclusion` 为测试 peer 启用 X server 自动合成，使根窗口可捕获；
这不是正式桌面 compositor。服务启动时显式指定本地 OCR manifest，不下载模型。

## 覆盖

新增 `pet_observation_test` 既检查同步观察边界，也通过真实 IpcClient/RuntimeService
连接验证语义观察、缓存视觉拒绝、失败后连接可用、stop/start 后无遗留启动门禁以及
shutdown。fake pipeline 只替代视觉生产者，不替代 IPC 路径。

覆盖正常完成、许可拒绝、无管线/未运行管线、无租约准入、准入拒绝/异常、前后核验
失败、capture/provider 异常、恢复失败/异常、已取消、运行期取消、入场前和运行期超时、
组装与恢复期间 epoch/视觉代次变化，以及不配置 pet 的旧嵌入兼容。私有源头没有
新建异步任务；沿用的 Runtime 接纳/关闭路径同时回归。

真实原生脚本在 pet 下方放置受管公开画布，连续改写“任务已保存”和“测试已完成”。
驱动在 IPC 等待中直接观察空 bounding shape，并逐像素比较底层画布；服务返回的
OCR 文本和视觉代次必须随当前文字更新，前台窗口为测试画布。回复到达后确认桌宠
已恢复、仍映射、位置/尺寸未变且没有抢走画布焦点。原拖动、重启、崩溃恢复、隐藏
关闭与 owner 丢失回归继续执行。

## 实际结果

| 门禁 | 实际结果 |
| --- | --- |
| native-release 全量构建 / CTest | 构建成功，99/99；216.70 s |
| 最终定向回归（6 个业务测试 + 2 个隔离 fixture） | 8/8；17.07 s，含新增运行期预算用例 |
| ASAN（3 个业务测试 + 2 个隔离 fixture） | 5/5；13.63 s，无报告 |
| UBSAN（同上） | 5/5；13.34 s，无报告 |
| TSAN（同上） | 5/5；15.86 s，无报告 |
| 私有真实 pet + tray + 本地 OCR | 48/48；两次文字和确切新代次均验证通过 |
| 格式、公开头边界、Python 语法、diff 空白检查 | 通过；62 份公开头，0 违规，保留 DEC-096 两处既有 seam |

全量 Release 后只补充测试中的运行期预算用例并同步格式/注释，再完整重建和执行
上述最终定向回归；产品行为未再改变。最终 `pet_observation_test` 为 68 个检查，
Release 和三种 sanitizer 均通过。CI 的 native 目标/选择列表已加入该测试，本地通过
不代替远端 CI。

完整结构化结果和两次公开 OCR 响应见
[证据 JSON](data/mira-pet-observation-native-20261018.json)。失败尝试及 run5 成功记录
均保留；最终通过的是 final2，不把中途 28 条 check 通过的失败运行记作成功。

## 过程中的失败与修正

- 首次用 `/usr/bin/python3` 启动缺少 python-xlib，尚未执行业务验证；改用本机已具备
  依赖的默认 Python，没有安装或修改系统包。
- run2/run3 均为 17/18：空 shape 已生效，但根窗口采到黑色。最小 WM peer 原来只做
  manual Composite redirect，没有 compositor 绘制根窗口。新增仅供该 opt-in 场景的
  automatic redirect；没有修改产品捕获逻辑或放宽像素比较。
- run4 的两次 OCR 和恢复检查均通过，随后拖动等待超时（28 条已执行 check 全通过，
  整体运行仍失败）。旧脚本取动画输入 shape 的首个细条，随帧变化可能失效。改取较大
  角色区域，并在按下前确认 X server 实际命中 pet；run5 完整 48/48。
- 扩大前台窗口断言时，第一版使用 override-redirect 画布，产品窗口 Provider 按既有
  规则拒绝，响应 `unavailable: not_found`；保留该失败。改为由测试 WM 接纳的普通
  窗口，继续验证前台标题，未更改或豁免产品窗口守卫。

最终隐藏正常关闭 0.267 s，owner 失联退出
5.228 s，均符合既有隔离脚本预算。CPU/RSS 仅作为
夹具观测值保存在 JSON，自动合成与软件渲染不提供硬件能耗/性能验收结论。

原始目录及日志位于 `/tmp/mirage-m8-observation-native-20261018*`；仓库只保留结构化
公开测试证据，不纳入截图、运行日志或编译产物。

## 尚未完成的门禁

M8-06 仍为 **In Progress**。本轮关闭直接 IPC 观察入口的实现与隔离验证，以下项目
继续保持开放：

| 项目 | 原因与补跑条件 | 负责人 |
| --- | --- | --- |
| 真实 compositor、完整气泡/Overlay 与输入/接管闭环 | 需空闲真实桌面和可控目标，同场景视频/事件证明 compositor 已绘制隐藏后新帧；自动 X server 合成不替代该证据 | Mirage 维护者 |
| Wayland/Portal、Windows、多屏/物理 DPI/全屏 | 当前只交付 X11 排除；按各平台实现及目标机器补跑，不将 XWayland 当 Portal 保证 | Mirage 维护者 |
| 按目标自动迁移、几何版本与隐藏节能 | 本轮使用整窗短时隐藏；需后续可见性/几何契约与功耗、恢复测试 | Mirage 维护者 |
| native/EUI 实进程 sanitizer | 本轮三种 sanitizer 覆盖 Runtime、IPC 和假 pipeline；完整原生 sanitizer 构建及实进程脚本另行执行 | Mirage 维护者 |
| 远端 CI、MR、安装/升级/卸载和许可 | 本轮未提交/推送，CI 测试清单已加入新测试；发布与 M8-08 门禁另行执行 | Mirage 维护者 |
