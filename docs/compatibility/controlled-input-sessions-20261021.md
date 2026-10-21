# 受控持键与完整长按首个接线（2026-10-21）

> 状态：In Progress（首个接线及自动验证通过，M7-27 剩余平台和实机门禁开放）
> 工作项：M7-27，关联 M7-25/26/31；依据 DEC-110/054/056/085
> 负责人：Mirage 维护者
> 基线：bdaefdd + feat/desktop-input-policy 工作树

## 实现范围

X11/XInput2 开放固定前台窗口中的受控 key_down/up/tap、button_down/up/tap，
可在持键期间调用截图或等待模型。`desktop.input_session.begin/advance/observe/release`
为独立 1.0.0 工具族；holding 与普通输入的释放失败回执分开，不据此证明业务成功。
开始需要新鲜 window id/action epoch，随后绑定 task/session/task_epoch、宿主会话 id、
窗口与几何、authority、state version 和连续 command sequence。scope 不热切换；
模型不能用自报 held keys 清理输入。重复/迟到指令不重放，终态不能恢复活动。

保持预算 v1：最多四个持有输入、64 个变更命令、15 秒 idle、60 秒 total。命令可
刷新 idle，观察/截图不能；total 始终递减。正常长按只表达 press=long，由 Desktop
纯函数在本地种子上生成 1080～1240 ms，不开放通用时间参数。X11 会话中的单次
长按小于既有 1500 ms 完整 press 上限；普通 tap 保持原自然停留。鼠标按钮仅作用于
当前指针所在的绑定窗口，不移动指针；字符键不隐式合成 Shift，组合需显式安排。
持续 down、OS 自动重复和显式 tap 不混用，不宣称已控制或验证各应用的重复行为。

Portal 的 `desktop.session.click/key` 单独升为 4.1.0，接受可选 press=normal|long，
默认行为不变。完整长按依旧在同次动作内结束，点击只能 click_count=1；点击保留
默认自然接近、到达像素重验和同一 lease，不能把 long 标记传入内部 Move 阶段。
完整计划在剩余预算不足时于按压前拒绝，取消/失败照原路径清理。后置观察/验证在
释放结算之后进行。DesktopCaptureSession 必须独立声明 timed_press_supported；
旧 capture/input 能力不隐含长按能力，拒绝不会封禁仍可用的普通点按。

## 生命周期与边界

复用 pinned Mira 的运行控制面、公开 ModelInputSupplier 与 kairo blocking worker /
MpscChannel / LatestMailbox，未修改依赖。Mira 不提供桌面按键 owner 或平台输入
会话，此部分属于 Mirage Desktop/Platform 与 Integration 的适配职责，不登记
依赖缺口。接入前核对 pinned kairo 的 by-requirement、scheduling 和 blocking-io
指南及公开 worker/comm 头文件。无自建线程、队列或计时 owner。

一个 ModelLayer 复用一个注册 worker，在无会话时 stop/join；下一次明确准入用
新的 Core 和同一 worker 的新 run。命令 FIFO 容量 8、RejectNewest，状态投影使用
LatestMailbox，绑定切换也通过 LatestMailbox。普通输入共享 ActionCoordinator
单写者 lease；截图不占写入 lease。core 状态与平台 driver 仅由一个 worker 写入。
condition variable 只负责唤醒 comm 数据面，回执/命令没有平行队列。所有入队请求
均有消费的 future，关闭/拒绝和在途入队竞态必须结算。release 入队失败时走 stop /
join 的清理通道，不能丢掉释放请求。平台 driver 在派发前再校验词表、能力和预算。

父取消、接管、权限策略更换、deadline、平台错误、模型异常/拒绝及任务终结均主动
释放。policy.set 在新策略生效前推进 authority 并取消活动 lease；旧授权不可继续。
普通 desktop_busy 是可修正的所有权冲突，不代表工具能力丢失。release/observe
不受失败跟踪的整工具黑名单拦截；真实后端缺失仍按既有能力失败策略处理。
成功作答但未明确结束输入会话不能判为干净完成；若原模型调用已失败，追加清理
结果而保留原始错误。完整按压成功也不代表文本值或应用目标已达成。

X11 driver 拥有独立 Display 连接；核对固定窗口的焦点、几何及生命周期事件、
实际键/按钮读回状态，并监测 XI2 raw 物理输入。发现物理干预进入全局 takeover；
同键竞争或事件预算耗尽时不向归属不明的输入盲发 up。按实际拥有顺序逆序清理；
释放确认后清空 held 投影，无法确认则保留 dirty fence，resume 不能抹去。驱动
不改变系统键盘布局/重复配置，不释放准入前已按住的用户输入。

平台调用与 4 ms 唤醒切片均为 soft、协作式等待，不承诺 X 服务卡死、设备失联或
调度压力下的绝对释放时限。CancelToken 的跨上下文 probe 必须线程安全，捕获的
owner 必须存活到 join；实际服务路径使用任务停止状态，测试故障开关使用原子值。

## 验证范围

- 会话核心：holding/释放投影、原状态机迁移、观察不续期、总期限、64 命令预算、
  重复/迟到版本、容量 8 压力、入队拒绝、运行期与 idle 取消、异常、接管、权限
  换代、平台守卫、释放失败封锁及 worker 重用，不新增无界注册项。
- 私有 Xvfb：真实 Shift 按下读回、持有期间截图、长按时长、按键/按钮释放、既有
  按键占用拒绝、运行期取消与焦点改变；通过真实 ModelLayer/BuiltinToolRegistry
  执行 begin/advance/capture/release，以及模型未释放、协议失败、模型调用停滞时
  取消和权限换代。不重放按键，结束后实际键状态为空。
- Portal Notify 协议夹具：长按点击/Shift 的独立事件与耗时、预算不足零事件拒绝、
  按下后取消仍 up、多击 long 拒绝；共享工具检查图片必须实际供给、能力独立声明、
  long 参数传递、错误类型与零输入负例。协议夹具不是 compositor 实机证明。
- 原有普通输入、条件验证、图像供给、权限、接管、取消与生命周期回归保留。

### 实际结果

本轮最终源码完成以下验证；定向集合为 13 个测试及 2 个状态隔离夹具，不等同于
全部平台用例。Debug 全量包含 106 项。

| 配置 / 检查 | 实际结果 | 范围 |
| --- | --- | --- |
| Debug 全量 | 106/106，104.87 s | 全仓回归，包含新增受控输入用例 |
| ASan 定向 | 15/15，70.63 s | 会话、真实 Xvfb、Portal、图像、权限与验证回归 |
| UBSan 定向 | 15/15，65.76 s | 同上 |
| native-release 定向 | 15/15，32.34 s | 同上，完整 product 构建完成 |
| 公开头文件边界 | 65 个头文件，0 违规 | 2 项既有 DEC-096 seam 保持备案 |
| 可选后端语法检查 | 通过 | 不定义 XInput2 的 session fallback 与 X11 stub；不是平台运行验收 |
| 格式与补丁 | 通过 | 28 个 C++ 文件变更范围 / 新文件检查及 git diff --check |

构建使用现有 debug / asan / ubsan / native-release preset；native-release 的
`mirage-native-product` 及定向测试目标均已更新，`apps/mirage --version` 正常退出。
未安装、启动或重启产品。复跑命令（从仓库根目录）：

```sh
cmake --build --preset debug
ctest --preset debug --output-on-failure
cmake --build --preset native-release --target mirage-native-product
cmake --build --preset debug --target mirage-boundary-check
```

三个定向配置分别使用 `ctest --preset asan`、`ctest --preset ubsan`、
`ctest --preset native-release`，附加 `--output-on-failure -j2 -R`，正则为：

```text
^(controlled_input_session|controlled_input_x11|portal_input|desktop_image_supply|input_cadence|harness_desktop_tools|post_action_verification|desktop_action_policy|permission|task_permission|permission_ipc|input_session_contract|provider_contract)_test$
```

对应测试目标名为上述名称追加 `_test`；先用各自 build preset 构建这些目标。
未执行 TSAN；跨上下文新增路径使用容量压力、取消/关闭、异常和释放失败故障注入
验证，没有据此宣称无数据竞争证明。

验证中修正了测试接线：公开 worker API 的拒绝用例改为重复名称准入拒绝；图像
夹具补齐真实 supply/validate_request 与 `source_id`，并断言 ImagePart 实际进入
模型请求；Portal 夹具补齐 stopped 回调。保留无输入拒绝、按下后取消仍释放、
原始模型协议错误及新鲜观察代际守卫，没有放宽产品准入条件。可选后端语法检查
首次漏配 desktop/process include 路径，补齐后通过，没有更改源码绕过检查。

## 未执行门禁

本轮不向用户真实桌面发送输入，不重放聊天任务、不发送消息，不启动/重启产品。
真实物理设备的同键竞争/断连、X 服务故障、混合 DPI/合成器、在线模型任务和
人工手感未验；未据此宣称所有竞争消除或硬实时。系统切换器/Alt-Tab 的受控焦点
转换、持有中移动、Portal 跨调用持键、Windows、IME 和硬件 HID 路线继续开放。
M7-27/31 保持 In Progress/开放；负责人 Mirage 维护者，平台实现和受控应用/设备
具备后按专项计划补跑。原先会话的 SSE 根因仍缺失败流，不在本轮宣称修复。


收尾边界：已知完整按压计划必须放入剩余 session/operation 预算，否则按下前拒绝。
执行中的协作停止同时检查父取消和 Executor worker stop token，不能只在空闲循环
检查。已确认释放的已知失败按 DEC-056 的既有策略，在释放代际及之后的新桌面
观察到达后才允许恢复结算；旧代际截图不能退休失败，取消/接管与释放 uncertain
不通过该路径消除。当前会话状态在截图后进入 awaiting_feedback，但不续期。
