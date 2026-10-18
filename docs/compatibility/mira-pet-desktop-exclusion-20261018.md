# M8-06：桌面工具整窗排除首增量（2026-10-18）

> 关联：M8-06，M8-05/07/12 的限定回归
> 决策：DEC-103
> 工作树：`feat/pet-desktop-exclusion`，基线 `aa09e25` 加本轮未提交变更
> 负责人：Mirage 维护者

## 范围与环境

会话桌面工具和 workflow desktop atoms 在观察、输入、后置核验期间排除受管桌宠的
整个窗口；无桌宠租约同样阻止期间新建 pet。只有 ready 受管进程的服务端 XRes PID、
_NET_WM_PID 与协议标记一致，且窗口唯一时才修改 shape。角色、气泡、尾部、阴影及
输入区域共用 bounding shape；不向截图补画像素、不放宽第三方遮挡和既有输入守卫。
至多 16 份重叠平台租约，最后释放才恢复；取消/异常/拒绝/身份变化/关闭明确结算。

Linux 7.0.0-38-generic x86_64，Ubuntu GCC 13.3.0，CMake 3.28.3，Ninja，C++20。
Release 产品启用 pet/native/PP-OCR；sanitizer 使用既有非 native presets。
依赖未变：Mira `a29374a`，内嵌 kairo `32027c2`，Mirador `cb45a10`，EUI `63117f7`。
本机原无 libXRes 开发头，从 Ubuntu noble `libxres-dev 2:1.2.1-1build1` 提取到既有
用户 sysroot，链接系统 `libXRes.so.1`；CI 增加同名开发包，不引入另一份 pinned 依赖。

## 命令

```bash
cmake --preset native-release \
  -DX11_XRes_INCLUDE_PATH=/home/linductor/.local/mirage-sysroot/usr/include \
  -DX11_XRes_LIB=/usr/lib/x86_64-linux-gnu/libXRes.so.1
cmake --build --preset native-release -j 3
ctest --preset native-release -j 2
cmake --build --preset native-release --target mirage-format-check mirage-boundary-check

git diff --check
python3 -m py_compile tests/manual/pet_preview_acceptance.py
python3 tests/manual/pet_preview_acceptance.py \
  --build build/native-release \
  --output /tmp/mirage-m8-native-exclusion-fixed-20261018 \
  --exclusion-probe build/native-release/tests/pet_exclusion_test
```

对 `asan`、`ubsan`、`tsan` 分别执行带相同 XRes include/lib 参数的 configure；构建
`pet_exclusion_test pet_exclusion_tools_test harness_desktop_tools_test tray_runtime_test`。
测试选择与 TSAN 运行方式：

```bash
ctest --preset asan -R '^(pet_exclusion(_tools)?_test|harness_desktop_tools_test|tray_runtime_test)$'
ctest --preset ubsan -R '^(pet_exclusion(_tools)?_test|harness_desktop_tools_test|tray_runtime_test)$'
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --preset tsan \
  -R '^(pet_exclusion(_tools)?_test|harness_desktop_tools_test|tray_runtime_test)$'
```

## 结果

两次原生进程结果的结构化副本见[证据 JSON](data/mira-pet-exclusion-native-20261018.json)。

本轮先完成专项，再扩大到 Runtime 接线和完整 Release 套件；后续结果在下文记录。

- Release 首轮关联回归 12/12（含两个隔离 fixture），覆盖桌面 atoms、workflow 视觉
  knowledge、harness、托盘生命周期、pet 状态/动画/气泡与稳定性守卫；随后加入无桌宠
  启动竞态门禁及容量/身份篡改负向用例，按最终工作树重建复跑，不将旧结果替代新用例。
- `pet_exclusion_test` 通过真实 Xvfb root 像素与 XQueryPointer 验证底层目标重新可见、
  输入命中及头顶/角色区同时排除；覆盖新帧/位置尺寸变化、PID 属性伪造、重复窗口、
  重叠租约、16 份容量与第 17 份拒绝、取消、形状/身份变化、原 shape 恢复、窗口销毁、
  跨 Executor 上下文与缺失 XRes/Wayland 拒绝。
- `pet_exclusion_tools_test` 覆盖拒绝时不调用 handler、前后形状核验、异常/执行中取消/
  已取消/已超时操作、恢复失败的明确不确定结果、Executor 准入异常、非桌面与无 pet
  嵌入路径，以及实际 workflow registry 的观察 atom 接线。
- harness 的真实 ModelLayer 脚本核对每轮九次工具调用各自持有/释放租约，权限 gate 在
  隐藏范围内，连续两轮累计 18 次收敛；既有前后核验、focus/epoch/权限拒绝继续回归。
- 私有真实原生进程首跑 **31/32，失败**：六项新增避让检查全部通过，但既有最小化正常
  退出未满足六秒预算。日志与 JSON 留于 `/tmp/mirage-m8-native-exclusion-20261018/`。
  没有将失败记录覆盖为成功。
- 原因：隐藏 UI 无帧回调，退出中继仅在 30 秒状态同步 timer 中检查。拆出 200 ms
  Executor control timer，准入失败显式拒绝启动，teardown 先取消再排空；不增加线程。
- 修复后私有原生进程 **38/38**，正常隐藏关闭 **0.367 s**，异常 owner 失联退出
  **5.282 s**。验证实际 pet 的 XRes 身份、空整窗 shape、新帧仍隐藏、保持映射和焦点、
  恢复位置/尺寸，原 alpha/拖动/重启/独立启停/崩溃恢复/最小化与 owner 关闭也全部通过。
  原始 JSON、截图及日志位于 `/tmp/mirage-m8-native-exclusion-fixed-20261018/`。
- 补充 Runtime 真实准入路径时，首次 fixture 漏配 tray owner，随后漏关主窗口自动打开，
  导致 service.start 失败及旧测试解包中止。Release 全量首跑 97/98（仅该 fixture 失败），
  ASAN/UBSAN/TSAN 补充用例同样明确失败。补齐受管假托盘、关闭自动主窗，并在启动
  失败时保留诊断返回；产品代码没有为适应 fixture 放宽生命周期检查。修正后 Release
  定向含隔离 fixture 3/3。最终全量及三 sanitizer 复跑结果见下方。
- 公开头边界检查 62 份头文件，0 新违规，保留 DEC-096 的两处既有 seam。

## 最终工作树复跑

| 门禁 | 实际结果 |
| --- | --- |
| native-release 全量构建与 CTest | 构建成功，98/98；106.19 s |
| ASAN 专项（4 个业务测试 + 2 个 fixture） | 6/6；62.15 s，无报告 |
| UBSAN 专项（同上） | 6/6；50.31 s，无报告 |
| TSAN 专项（setarch x86_64 -R；同一选择集） | 6/6；150.99 s，无 race 报告 |
| 实际原生进程私有桌面 | 38/38；不代替真实 compositor |
| 格式、公开头边界、Python 语法、文档本地链接与 diff whitespace | 通过；0 新头边界违规、0 缺失链接 |

最终全量 Release 日志：`/tmp/mirage-m8-release-final-tests.log`；sanitizer 日志：
`/tmp/mirage-m8-{asan,ubsan,tsan}-final-confirmed-tests.log`。
首跑失败与修正过程仍保留在前述记录，不据修正后的结果抹除。

## 限制、负责人和补跑条件

本轮 M8-06 仍为 In Progress。下列项目没有因 fixture 通过而关闭：

| 未执行/未实现项 | 原因与补跑条件 | 负责人 |
| --- | --- | --- |
| 真实 compositor 重绘、完整气泡文字/OCR/Overlay/点击拖拽键入闭环、人工接管 | Xvfb root 不合成真实桌面；需空闲实桌面、可控目标与事件/视频同场景取证，确认取得隐藏后的新帧 | Mirage 维护者 |
| Wayland / Portal、Windows、多屏/物理 DPI / 全屏 | 当前实现明确拒绝 Wayland；Windows 为 unavailable 后端，需独立平台实现及目标机器运行 | Mirage 维护者 |
| 直接 IPC desktop.observe、按目标自动迁移、几何版本协议 | 本轮只接会话与 workflow 工具；需扩展 producer 边界并补直接调用、重观察与 Overlay 组合验证 | Mirage 维护者 |
| 隐藏时停止动画/滚动唤醒、长等待能耗 | 当前短时 shape 排除保持渲染以消费最新状态；需 UI 与服务端可见性协议后再取节能和恢复证据 | Mirage 维护者 |
| native Bridge 新 control timer 的 sanitizer 实进程 | 本轮 sanitizer presets 未启用 native/EUI；需完整 sanitizer 原生构建与相同隔离实进程脚本 | Mirage 维护者 |
| M8-08 正式安装/升级/卸载、许可及偏好 | 本轮只构建和私有搬迁测试，不是安装验收；按 M8-08 准备安装载荷与迁移测试 | Mirage 维护者 |
| 远端 CI、评审与目标平台发布 | 当前未提交/推送，不宣称 CI 或 MR 通过；提交后按仓库门禁验证 | Mirage 维护者 |

未重启用户的运行实例、未进行在线模型请求、未修改任何 `third_party/` 内容。

## 后续增量

直接 IPC `desktop.observe` 的排除、预算与新鲜视觉检查已在同日第二增量实施，
见[观察入口验证](mira-pet-observation-20261018.md)。上表保留首增量结束时的状态；
按目标自动迁移、几何版本、Overlay 和真实 compositor 等门禁继续开放。
