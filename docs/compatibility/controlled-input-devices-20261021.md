# 受控输入的设备来源与拓扑守卫（2026-10-21）

> 状态：本增量自动验证通过；M7-27 保持 In Progress（硬件/平台门禁开放）
> 工作项：M7-27，关联 M7-25/26/31；依据 DEC-110/054/056/085
> 负责人：Mirage 维护者
> 基线：`4ec413a` 加 `feat/m7-27-held-pointer-travel` 累计工作树

## 问题与修正

既有 X11 持键连接向服务器声明 XI 2.0，却依赖 raw sourceid 判断输入归属。
[X.Org XI2 协议](https://www.x.org/releases/current/doc/inputproto/XI2proto.txt)
的 RawEvent 节明确：sourceid 从 XI 2.1 才有定义；其他客户端 active grab 期间
持续收到 raw 事件的语义也从 XI 2.1 提供。仅用已有按键读回无法分辨两个来源
同时按住同键，抓取期间丢失 raw 反馈可能使清理错误地视其为本任务独占。

本增量要求受控持键最低 XI 2.1；主 X11 Provider 按实际版本协商结果声明能力，
每个持键 driver 再独立检查。旧版本/查询失败不开放持键，普通完整输入能力保持
独立。tool/receipt/budget 版本没有改变，advance 仍为上个增量的 1.1.0。

driver 先按 [XISelectEvents](https://www.x.org/archive/X11R7.5/doc/man/man3/XISelectEvents.3.html)
订阅 master raw 与全部设备的 HierarchyChanged/DeviceChanged，再冻结当前
ClientPointer、配对 master keyboard 和两项 XTEST slave 的 id/use/attachment/
enabled 信息。正常 SlaveSwitch 通知继续交给 raw 来源守卫判断；拓扑变化终结
当前会话。原路由仍可靠时只释放本任务已知输入；路由被移除/禁用/重挂接、能力
改变、来源缺失或事件预算不足时，归属不确定的输入不发 up，保留 held 投影和
dirty fence。迟到命令不能恢复会话，resume 不能擦除不确定释放。

继续复用一个 kairo blocking worker、容量 8 FIFO、LatestMailbox 和 ActionCoordinator。
没有新增任务生命周期、线程或队列。核对 pinned kairo by-requirement → blocking-io
card、公开 worker/stop/join API 及 Mira environment.hpp 的离散输入契约；这是
Mirage 平台输入归属的适配修正，没有依赖能力缺口或 pin 修改。

## 证据分类

新增 `controlled_input_devices_test`，全部在测试私有 Xvfb 中执行：

- 实际服务器事件：Xvfb keyboard/mouse slave 经测试输入 API 产生非 XTEST source
  的真实 XI raw 事件，覆盖键/按钮 × 同输入/不同输入 × 无/有其他客户端 active
  grab 共 8 场景。同输入竞争保持实际按下、held 投影和失败围栏，不盲发 up；
  非重叠输入只清理本任务 Shift/左键，外部 Ctrl/右键仍保持按下。
- 实际拓扑事件：通过 [XIChangeHierarchy](https://www.x.org/archive/X11R7.5/doc/man/man3/XIChangeHierarchy.3.html)
  添加 master，ready/holding 会话均结束；原注入路由未变时确认释放，不热切换。
- 实际事件压力：在受管 worker 的同步 probe 中投递 160 对 Ctrl down/up，形成
  至少 320 个 raw 事件的确定积压；超过每次检查的 256 事件预算时保留持有 Shift
  并封锁，避免提前返回、补发或盲目清理。probe 没有私有线程或队列。
- API 故障注入：测试链接器包装 XIQueryVersion，返回 2.0 或查询失败，验证能力
  关闭、driver 按下前拒绝且普通完整输入能力仍声明。Xvfb 保护 XTEST 设备不允许
  禁用/移除，因此只在一次真实 hierarchy cookie 上包装 XGetEventData，将原
  XTEST source 标记为 disabled，验证 driver 拒绝清理未知归属并保留真实 Shift。
  这是边界元数据故障注入，未伪称实际禁用设备或运行旧版本服务器。
- 会话核心注入 input_device_changed/input_device_lost，验证干净释放与不确定围栏、
  cancelled 标记、held 投影和迟到命令；原异常/拒绝/取消/超时/shutdown 回归保留。

新夹具先在旧 XI 2.0/无拓扑守卫实现上运行，86 项检查中 14 项断言失败；随后修复
并扩展上述压力和 API 故障矩阵。初轮新实现的非重叠按钮用例发现 raw 排空与状态
查询之间的既有竞态：查询可能先看到新按下的外部按钮，错误为 physical_input_busy
而非 physical_takeover。两者都按既有策略取消并只清理本任务输入，测试保留外部
按钮、实际释放与 takeover 围栏断言，接受这两个合法错误；没有放宽同输入 dirty
fence、原始按下状态或零副作用版本拒绝断言。

## 环境与复跑

Linux 7.0.0-38-generic x86_64，GCC 13.3.0、CMake 3.28.3。复用上个增量的隔离
目录 `/tmp/mirage-m7-27-validation`，最终实现/测试逐文件 SHA-256 对账后重建。
Mira `a29374a`、kairo `32027c2`、Mirador `5565436`、eui-neo `63117f7` 均匹配锁；
本轮主工作树 Mirador 也已匹配该锁，没有修改依赖或锁文件。未向真实桌面输入，
未安装、启动或重启产品。Debug/ASan/UBSan/TSan 使用 native/OCR/pet preview OFF。

```sh
cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_ENABLE_PET_PREVIEW=OFF
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
```

ASan/UBSan 使用相同 configure 选项，将 preset 换为 asan/ubsan；定向集合为 7 个
测试，另有 2 个状态隔离夹具。下面将 asan 换为 ubsan 即为 UBSan 实际命令：

```sh
cmake --build --preset asan -j 3 --target controlled_input_devices_test controlled_input_session_test controlled_input_x11_test x11_backend_test input_session_contract_test desktop_action_policy_test post_action_verification_test
ctest --preset asan --output-on-failure -j 2 -R '^(controlled_input_devices|controlled_input_session|controlled_input_x11|x11_backend|input_session_contract|desktop_action_policy|post_action_verification)_test$'
```

TSan 同样配置，然后运行（沿用此前已确认可避免运行时 mapping 冲突的子进程配置，
不改变系统 ASLR 设置）：

```sh
cmake --build --preset tsan -j 3 --target controlled_input_devices_test controlled_input_session_test controlled_input_x11_test
setarch x86_64 -R ctest --preset tsan --output-on-failure -j 2 -R '^controlled_input_(devices|session|x11)_test$'
```

## 实际结果

| 配置 / 检查 | 实际结果 | 范围 |
| --- | --- | --- |
| Debug 全量 | 108/108，71.89 s，无 skip | 全仓构建与回归 |
| ASan 定向 | 9/9，16.76 s，无 sanitizer 报告 | 7 测试 + 2 隔离夹具 |
| UBSan 定向 | 9/9，16.95 s，无 sanitizer 报告 | 同上 |
| TSan 定向 | 3/3，13.57 s，无 race 报告；setarch -R | 3 个受控输入测试 |
| 边界 / 可选后端 / 文档 | 通过 | 65 个公开头文件零违规（2 项既有 seam）、2 个无 XInput2 单元语法、7 个 C++ 格式、391 个本地文件链接及补丁空白 |

## 剩余门禁

M7-27 保持 In Progress。真实硬件同键竞争/拔插、XTEST 设备实际失效、X 服务断连、
桌面 compositor/混合 DPI、旧 XI 服务器运行、在线模型及人工手感未执行；原因是
本轮为私有服务器与明确边界 API 故障环境。负责人 Mirage 维护者，补跑条件为对应
受控设备/服务器/桌面与模型具备。Alt-Tab 正反选窗、跨窗口拖放、Portal 跨调用持有、
Windows 和 IME 继续按专项开放；native-release 本轮未运行，不据 Debug 扩大发布
配置或平台声明。不是所有输入来源可区分的证明，也不承诺 X 服务卡死下的硬释放期限。

前序记录：[持键首接线](controlled-input-sessions-20261021.md)、
[固定窗口持键移动](controlled-input-travel-20261021.md)。

## 同日后续验证

累计实现随后接入限定 Openbox 3.6.1 的显式选窗，并按维护者要求更新完整
native-release product，Release 定向 19/19 通过（含两项隔离夹具）。命令、
范围和产物哈希见[切换器与 Release 后续记录](controlled-window-switcher-20261021.md)。
上表保留本增量当时的实际结果，真实设备、其他平台与模型门禁不据此关闭。
