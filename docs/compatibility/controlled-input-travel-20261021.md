# 固定窗口内持键移动验收（2026-10-21）

> 状态：本增量自动验证通过；M7-27 保持 In Progress（剩余平台门禁开放）
> 工作项：M7-27，关联 M7-25/26/31；依据 DEC-110/054/056/085
> 负责人：Mirage 维护者
> 基线：`4ec413a` 加 `feat/m7-27-held-pointer-travel` 工作树

## 交付范围

X11/XInput2 受控会话支持 smooth_move，可在 Shift 或鼠标左键等输入保持按下时
移动、截图、再次决策，最后显式释放。advance 升为 1.1.0，其余会话工具 1.0.0、
input_session_v1 和预算 v1 保持兼容。模型仅传全局 int32 x/y，不接收时间、路径
点数、key/button/press 混用。工具在授权后查询起点，用现有距离策略规划时长，
平台在唯一持键 worker 上重新读回真实起点并生成单调 minimum-jerk 路径。

路径限制在已绑定前台窗口及当前 X screen 内；移动前检查整条路径，执行中每段
检查焦点/几何/生命周期、物理干预、实际 held 集合、指针所在窗口及坐标读回。
逐段 XSync 使服务端收到中间输入，取消/权限换代/焦点变化或超出 soft 节奏容差
停止余段，不重放或回滚已发生位移。完整计划超过 session/operation 剩余预算时
移动前拒绝并释放已有输入；移动本身不改变 held 投影，每次消耗一个变更命令。

复用原 blocking worker、容量 8 FIFO、LatestMailbox 和 ActionCoordinator lease，
没有新线程、并发队列、调度 owner 或依赖修改。核对 Mira environment.hpp 的通用
离散输入契约与 pinned kairo by-requirement → blocking-io card、公开 worker API；
窗口内输入 owner 属于 Mirage Platform/Integration，没有依赖能力缺口。

## 环境与复现

Linux 7.0.0-38-generic x86_64，GCC 13.3.0、CMake 3.28.3。验证源码在
`/tmp/mirage-m7-27-validation` 的隔离 worktree；Mira `a29374a`、kairo `32027c2`、
Mirador `5565436`、eui-neo `63117f7`，全部通过锁校验。X11 使用测试独立创建的
Xvfb，不向用户桌面发送输入；未安装、启动或重启产品。

原工作树 Mirador 在任务开始前已处于 `cb45a10`，与锁定的 `5565436` 不同，直接
构建被依赖门禁正确拒绝。本轮保留原子模块与锁文件，创建独立的 Mirador worktree
用于验证，Mira/eui-neo 只读复用。没有关闭依赖校验，也没有将现有差异纳入本次修改。

初次隔离同步遗漏了自研差异，曾运行基线；新增工具版本/golden 检查随即暴露
隔离目录仍是旧实现，基线通过不计入本增量证据。补齐源文件后逐文件 SHA-256
对账并更新隔离文件时间，确保 Ninja 重新编译。新测试另修正 uint64→int64 的显式
转换，以及焦点变化后 X11 可能通过指针焦点向旧窗口投递 KeyRelease 的夹具假设；
实际键状态必须释放、不得重放的断言保留，产品守卫没有放宽。

从上述隔离目录运行（其他开发环境先保证 submodule 与 lock 一致）：

```sh
cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_ENABLE_PET_PREVIEW=OFF
cmake --build --preset debug -j 3
ctest --preset debug --output-on-failure -j 4
```

ASan/UBSan 使用相同的 configure 选项，将 preset 换为 asan/ubsan，并构建、执行：

```sh
cmake --build --preset asan -j 2 --target controlled_input_session_test controlled_input_x11_test input_session_contract_test smooth_move_schedule_test desktop_action_policy_test post_action_verification_test
ctest --preset asan --output-on-failure -j 2 -R '^(controlled_input_session|controlled_input_x11|input_session_contract|smooth_move_schedule|desktop_action_policy|post_action_verification)_test$'
```

UBSan 将以上两处 asan 换为 ubsan。定向集合有 6 个测试与 2 个状态隔离夹具。
TSan 配置沿用相同选项，构建两个 controlled_input 目标后执行：

```sh
cmake --build --preset tsan -j 3 --target controlled_input_session_test controlled_input_x11_test
setarch x86_64 -R ctest --preset tsan -R '^controlled_input_(session|x11)_test$' --output-on-failure -j 2
```

直接运行 TSan 曾在 main 前因 `unexpected memory mapping` 失败；使用上述仅对
测试子进程关闭 ASLR 的命令后通过，没有更改系统设置。

## 验证结果

| 配置 / 检查 | 实际结果 | 范围 |
| --- | --- | --- |
| Debug 全量 | 107/107，105.36 s | 全仓构建与回归，无 skip |
| ASan 定向 | 8/8，16.58 s，无 sanitizer 报告 | 6 测试 + 2 隔离夹具 |
| UBSan 定向 | 8/8，16.02 s，无 sanitizer 报告 | 同上 |
| TSan 定向 | 2/2，13.16 s，无 race 报告；使用 setarch -R | 受控输入生命周期与 Xvfb |
| 公开头文件边界 | 65 个头文件，0 违规 | 2 项既有 DEC-096 seam 保持备案 |
| 无 XInput2 语法检查 | 通过 | 关闭宏后的 X11 会话 fallback，不代替平台运行验收 |
| 格式 / 文档 | 通过 | 5 个 C++ 文件 clang-format、golden JSON、384 个本地链接及补丁空白 |

本增量自动用例包含：

- Xvfb 接收窗口实际收到多个单调中间 Motion，携带 ShiftMask/ Button1Mask，
  600 ms 路径完成后实际到达目标，持有期间截图成功，显式释放后实际键/按钮为空。
- 移动中取消、权限换代和焦点变化停止余段并释放；越界目标零 Motion；重复序列
  不重放。预算不足在 backend apply 前拒绝，保留原 worker 异常/拒绝/取消/超时/
  shutdown/释放失败与容量压力矩阵。
- 实际 BuiltinToolRegistry 按 golden 核对 advance 版本、坐标边界、回执兼容和
  12 项非法参数，拒绝均不移动、不消耗命令；合法目标实际移动。
- 两条完整 ModelLayer 脚本分别执行持 Shift/持鼠标左键→移动→截图供给模型→
  释放→作答；旧长按、模型协议错误/停滞、权限换代和未显式释放负例继续覆盖。

golden：`tests/integration/data/controlled-input-travel-v1.1.json`。
工具示例与契约见 [DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)
“固定窗口内的持键移动”；首接线历史结果见
[受控输入记录](controlled-input-sessions-20261021.md)，不与本轮结果相加。

## 剩余门禁

M7-27 保持 In Progress。系统切换器/Alt-Tab 正反选窗、跨窗口拖放、Portal 跨调用
持有、Windows、真实设备同键竞争/断连、X 服务失联、compositor/混合 DPI、在线
自主模型业务任务与人工手感未执行。原因是本增量限固定窗口及隔离自动环境；负责人
Mirage 维护者，补跑条件为对应平台实现、受控应用/设备/桌面和模型具备。
本轮未运行 native-release 配置，未据 Debug 结果扩大发布配置声明。
Xvfb/脚本证据不代替这些门禁，不宣称硬实时释放或拖动业务结果成功。

## 同日后续验证

累计实现随后接入限定 Openbox 3.6.1 的显式选窗，并按维护者要求更新完整
native-release product，Release 定向 19/19 通过（含两项隔离夹具）。命令、
范围和产物哈希见[切换器与 Release 后续记录](controlled-window-switcher-20261021.md)。
上表保留本增量当时的实际结果，其余平台/真实环境门禁不据此关闭。
