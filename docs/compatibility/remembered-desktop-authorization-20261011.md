# 记住桌面授权验证（2026-10-11）

> 状态：In Progress
> 工作项：M7-10 第六增量
> 决策：[DEC-078](../decisions/DEC-078-remembered-desktop-authorization.md)
> 负责人：Mirage 维护者

## 实现边界

会话输入框的权限菜单直接支持授权并记住、忘记授权与刷新；桌面工具页同步状态。
入口修正 M6-38 的独立证据见[三模式与观察链路验收](conversation-access-and-cua-continuity-20261012.md)。
Runtime 启动仅加载布尔偏好，授权管理经唯一
Executor 的有限可取消任务执行，一个管理请求在途；异常重新抛给 Executor，future
保留并进入 shutdown。控制面仍可响应 Hello，损坏配置拒绝改写，模型/视觉配置保留。

Portal v2 持久选项仅在 RemoteDesktop.SelectDevices，组合 ScreenCast.SelectSources
无持久字段。系统钥匙环保存单次令牌，使用前删除旧值，Start 验证设备/尺寸并保存新值。
授权管理不等首帧，流仍按任务创建/关闭。只读恢复会话始终不开放输入。忘记禁用复用、
推进授权代数并删除本机令牌；旧运行操作协作取消，空闲旧流在下次推进或结束关闭。

## 自动证据

Debug 全量 69/69，Native Release 全量 75/75。ASAN、UBSAN、TSAN 定向均 9/9，
其中 7 个业务测试加私有状态 fixture 的 setup/cleanup。ASAN detect_leaks=0:halt_on_error=1；
UBSAN halt_on_error=1:print_stacktrace=1；TSAN 使用 setarch x86_64 -R 与原有 GLib
suppression，无新增 suppression 或 race 报告，合计 164.47 秒。Portal 请求期限仍为
30 秒，聚合夹具使用原有 120 秒、TSAN 180 秒期限。

隔离 Portal 60 场景：原 50 加持久授权成功、缺令牌、恢复轮换、设备拒绝、写入失败、
只读恢复、v1 不发授权、运行中取消、超时、预先取消。模拟流不接触用户图像或输入。
令牌状态单测 23 checks，IPC golden 1374 checks，持久配置 300 checks，均零失败。
Runtime 授权设置检查保存合并、重启加载、管理请求忙、异常、策略拒绝、控制面响应和
shutdown 中协作取消；未来真实系统操作仍受既有 Executor 关闭路径约束。

私有产品验收 37/37，11 次无模型 workflow、六次 Canvas 点击、默认 PP-OCR/ncnn、
Runtime 干净退出。私有设置 UI 的浅色、深色、800×650 滚动、刷新与退出检查通过；
其 X11 授权状态为不支持，不冒充 Wayland 已记住状态或真实授权。安装包已重建到
`out/packages/mirage_0.1.0_amd64.deb`，四个包内可执行文件 SHA-256 与最终 Release
一致；未安装系统。用户配置/历史六份 SHA-256 未变，git diff --check 通过。

初次全量 Portal 回归在重建期间新增场景读到旧测试二进制而失败；最终先完成构建，
再完整回归通过。第一次私有视觉产品测试在前轮终态事件后立即发起下一轮遭拒；
单独复跑 37/37，首次日志未保留错误响应字段，具体是启动回执超时还是终态到释放的边界尚不能定因；
不据单独成功复跑宣称这一并发负载问题已修复。负责人 Mirage 维护者，补跑条件为
并行负载下保留私有 fixture 的 workflow.run 错误响应与启动事件。原始失败与
最终日志保留 `/tmp/mirage-dec078-*`，未覆盖为“首次全部通过”。

复现：

```sh
ctest --preset debug -j 2 --output-on-failure
ctest --preset native-release -j 2 --output-on-failure
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --preset asan -j 2 --output-on-failure -R '^(portal_authorization_test|portal_screen_fixture|runtime_service_test|ipc_protocol_golden_test|persistence_test|desktop_image_supply_test|portal_input_test)$'
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset ubsan -j 2 --output-on-failure -R '^(portal_authorization_test|portal_screen_fixture|runtime_service_test|ipc_protocol_golden_test|persistence_test|desktop_image_supply_test|portal_input_test)$'
setarch x86_64 -R ctest --preset tsan -j 2 --output-on-failure -R '^(portal_authorization_test|portal_screen_fixture|runtime_service_test|ipc_protocol_golden_test|persistence_test|desktop_image_supply_test|portal_input_test)$'
/home/linductor/miniconda3/bin/python3 tests/manual/desktop_authorization_ui_acceptance.py --build build/native-release --output /tmp/mirage-dec078-ui-final
/home/linductor/miniconda3/bin/python3 tests/manual/local_visual_workflow_acceptance.py --build build/native-release --output /tmp/mirage-dec078-product-final
cmake --build build/native-release --target package
```

机器摘要见 [summary.json](data/remembered-desktop-authorization-20261011/summary.json)。

## 待实机门禁

维护者本轮明确要求「稍后再验，先完成自动验证」，因此未发起真实系统共享或控制授权。
本机 RemoteDesktop 属性为 v2，仅证明接口可用，隔离 fixture 不证明 GNOME 已保留授权。
负责人 Mirage 维护者；补跑条件为桌面解锁、系统允许远程控制与记住选择、钥匙环可读写。
补验需第一次在设置授权后关闭共享会话，两个独立任务恢复，再完整退出所有 Mirage
进程并重启恢复，确认没有重复弹窗且令牌轮换；不输入键鼠内容。随后验证系统停止共享/
撤销或显示器变化后状态与重新授权，不复用旧截图或引用。稳定产品 AppID 也在该门禁
核实，若系统不接受恢复，则继续修平台映射，不能以 mock 成功关闭此项。

静态画面交帧、Unicode、其他 compositor/Windows/多屏及真实浏览器业务保持 DEC-077
后续门禁；本次不扩大已验范围，无依赖 pin 变更，不改用户配置、历史或钥匙环。
