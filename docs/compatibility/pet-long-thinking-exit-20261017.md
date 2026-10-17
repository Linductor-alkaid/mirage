# 长思考预览导致桌宠退出：修复与验证

> 日期：2026-10-17
> 负责人：Mirage 维护者
> 工作项：M8-05 / M8-11 / M8-12
> 缺陷：BUG-20261017-001
> 设计依据：DEC-084、DEC-089；既有气泡规范化及协议失败关闭契约
> 源基线：`7b61f27`，叠加本轮工作树修改；同时存在的 native 输入性能修改未纳入本缺陷范围

## 现场与根因

用户报告最新 native-release 会话中主程序仍在运行，桌宠自行退出。现场托盘 PID
762767、native PID 762788 均存活，`product.control/status` 返回 `active_work=0`、
`pet_pid=0`、`pet_state=failed`，诊断为 `pet process exited unexpectedly; start it
again to recover`。产品由桌面入口启动，子进程 stdout/stderr 指向 `/dev/null`，没有
保留下原始退出诊断；不把下述复现声明为现场唯一退出原因的直接日志证明。

最新持久化会话 `047bf445eccb495d8eadef76a2eeb03d` 的一段思考文本为 3332 字节。
只读提取到权限 0600 的临时文件，用原版 `bubble_line` / `valid_bubble` 对每个流式
前缀重放，得到 **227 个非法前缀，首个出现在 2055 字节**。正文不复制进仓库。

`bubble_line` 会规范化换行/空白并保留至多 2048 字节的 UTF-8 尾部；尾部窗口恰好
从词间空格开始时，返回前导空格。`valid_bubble` 要求文本再次规范化后保持不变，
因此拒绝该结果。Runtime 仍把此气泡作为 `pet.updated` 广播，桌宠 SessionClient
按既有失败关闭契约将其视为协议错误，停止 IPC worker；UI 在断连五秒后主动退出。
主服务仍在运行，与用户观察一致。

## 修改

尾部截断完成后去掉窗口开头遗留的分隔空格，保持规范化幂等。字节预算、UTF-8
边界、状态 schema、非法外部帧的失败关闭语义和 Executor 生命周期均沿用既有契约。
这是 Mirage 的气泡文本处理错误，不是 Mira / Mirador / kairo 能力缺口。

- `pet_bubble_test` 遍历 2176 个合成预览前缀，检查投影状态及 wire 解码，另覆盖
  ASCII/中文截断边界和零预算；不把用户思考内容写入测试。
- `pet_status_integration_test` 通过 fixture provider 的真实公开 thinking sink、
  Runtime 投影、Topic 和 IPC 广播发送长预览，验证通知与快照可解码，之后继续
  正文、结算、取消、期限、失败和 shutdown 路径。
- 修复前新增单元回归 4 项失败，集成回归 4 项失败；日志分别在
  `/tmp/mirage-pet-bubble-before.log`、`/tmp/mirage-pet-integration-before.log`。

## 已执行验证

```bash
cmake --build build/native-release --target mirage-native-product \
  pet_bubble_test pet_status_integration_test pet_status_test pet_animation_test \
  session_client_test tray_runtime_test -j 2
ctest --test-dir build/native-release \
  -R '^(pet_bubble_test|pet_status_integration_test|pet_status_test|pet_animation_test|session_client_test|tray_runtime_test)$' \
  --output-on-failure
```

完整产品构建成功；CTest **8/8 通过**，包括六项实际测试及 setup/cleanup，15.53 秒。
日志：`/tmp/mirage-pet-release-build.log`、`/tmp/mirage-pet-release-tests.log`。

ASAN 构建并运行 `pet_bubble_test` / `pet_status_integration_test`，**4/4 通过**
（含 setup/cleanup），5.90 秒，无 sanitizer 报告。命令为
`cmake --build build/asan --target pet_bubble_test pet_status_integration_test -j 2`
及 `ctest --test-dir build/asan -R '^(pet_bubble_test|pet_status_integration_test)$'
--output-on-failure`；日志为 `/tmp/mirage-pet-asan-{build,tests}.log`。

最新会话文本再次逐前缀重放：**3332 字节、0 个非法前缀**。另将重放程序和
`runtime/pet/src/{bubble,status}.cpp` 一同以 `-fsanitize=address,undefined
-fno-omit-frame-pointer` 编译运行，退出 0，无 sanitizer 报告。临时工具为
`/tmp/mirage-pet-text-replay.cpp`，原始文本仅保留在本机临时目录。

实进程探针复用 `tests/manual/pet_transport_timeout_probe.py` 的私有 Xvfb/DBus、
WM fixture、临时 XDG 和有界 mock，临时副本在完成 `pet_ready` 一秒后将实际
重放程序生成的 2055 字节前缀规范化结果加入通知气泡。未调用模型或操作用户桌面。

| 投影文本来源 | 桌宠结果 | 观察时长 | 最大事件序号 |
| --- | --- | --- | --- |
| 原版 `bubble_line` | 正常退出码 0，提前退出 | 6.196 秒 | 21 |
| 修复后 `bubble_line` | 完整观察窗口内存活 | 22.003 秒 | 431 |

原版探针日志明确为：`IPC connection lost: service sent an undecodable frame:
response is missing a non-negative 'id' / invalid pet status`。此处前半句来自尝试
按 response 解码事件，后半句为实际事件校验错误。结果与日志在
`/tmp/mirage-pet-bubble-{before,after}-probe/{result.json,pet.log}`，临时驱动为
`/tmp/mirage-pet-bubble-probe.py`。两个探针使用同一个实际 release 桌宠二进制，
仅替换服务端发送的投影结果，以隔离定位服务端文本规范化错误。

## 本机产品更新

完成验证后再次以 `mirage-native-product` 聚合目标构建成功；随后读取真实产品
`active_work=0`、`exit_pending=false`，经 `product.control/quit` 正常关闭旧实例，
使用相同桌面入口的 `mirage start --shell … --tray …` 启动更新产物。
新托盘 PID 774023、native PID 774044、pet PID 774045；桌宠 readiness 为 running，
前端 readiness 为 true，原有七个持久化会话 ID 全部保留。重启结果在
`/tmp/mirage-pet-live-restart.json`，实际产物摘要在
`/tmp/mirage-pet-release-artifacts.json`。没有自动发起付费模型请求或重跑用户桌面任务。
重启 47.810 秒后再次查询，桌宠仍以相同 PID 774045 运行，已跨过 30 秒健康刷新
周期；这是本机待机存活验证，不冒充重新执行完整真实模型任务的验收。

## 验证边界与剩余项

没有新增生产并发路径；本轮未增加线程或改动 pinned 依赖。私有 mock 生存验证
不代表硬件 GPU、真实 compositor、无限时长、Windows 或原生 Wayland 验收。
没有再次执行此前存在失败的完整 `pet_preview_acceptance.py`；隐藏退出预算、
重连与跨平台门禁继续由 M8-05/12 跟踪，负责人为 Mirage 维护者，具备对应环境并
完成相关独立修复后按原验收命令补跑，不能据本缺陷修复关闭这些门禁。
