# M6-23：模型设置真实交互验收

> 状态：Completed（Linux X11 本地范围）
> 日期：2026-10-06
> 负责人：Mirage维护者
> 依据：[DEC-044](../decisions/DEC-044-model-editor-interactions.md)、[M6计划](../plans/m6-native-frontend.md)

## 问题与修复

本轮实际启动Release原生应用及Runtime Service，通过XTest操作鼠标、键盘和剪贴板，观察窗口像素及Service ACK。全部进程使用私有Xvfb、DBus、IBus、系统钥匙环与临时XDG目录，使用公开合成Key，不读取或修改用户现有模型配置，不发起推理。代码追踪和状态回归补充了实际窗口发现的问题，登记为BUG-20261006-002。

| 问题 | 现在的行为 |
| --- | --- |
| 删除其他模型重置选中模型的预算/思考参数，捕获旧索引可能删除相邻条目 | 先保存选中模型参数，按稳定ID删除，保留选择；重复事件幂等 |
| 取消编辑跳到活动服务 | 按当前服务/模型ID恢复ACK副本；离线也能取消 |
| 添加模型重新启用已禁用服务 | 继承服务开关；空服务添加首个模型后仍须明确启用 |
| 带首尾空白的重复ID进入目录，未确认ID被保存遗漏 | 规范化ID、拒绝重复/空白ID；添加与取消是明确操作，保存提示完成内联编辑 |
| 脏草稿切换无反馈，Escape直接离开内联编辑 | 切换/预设/新增/返回/外观提供恢复选择，Escape先取消内联添加 |
| 服务删除即时生效，没有确认目标 | 展示服务名，取消不写入，确认只删除该服务 |
| 菜单外部点击不收起，菜单可能受表单裁剪 | 菜单移至表单外，外部点击/Escape收起，两个菜单互斥；新建/切换/取消清除过期提示 |
| 开关看似可用但空模型时无反应，重复按键触发多次操作 | 实际禁用空服务开关，加载/保存/弹窗期间阻止编辑，只在Press激活 |
| Key粘贴首尾空白导致拒绝且无说明 | 仅清理粘贴首尾空白；内部非法字符明确提示，隐藏状态仍只有掩码 |
| 刷新保护草稿时连ACK目录也被忽略 | 始终接纳ACK目录，单独保护草稿；保存失败保留输入，重连后可继续保存 |

## 证据

真实窗口完成35/35检查，包括Key保存/替换/显示/移除、全部三种协议、开关、名称/地址/预算校验与修正、空服务保存与首个/最后模型增删、未保存导航和删除确认。停止私有Service注入断线：保存失败保留Key草稿，离线取消恢复ACK，磁盘配置不变；重启Service后通过页面刷新重连，保留新Key草稿并成功保存。关闭测试窗口退出码0。

驱动自身修正了省略默认值的JSON字段读取、模型行数变化后的预算坐标以及焦点变更的光标像素差异。中途断言/脚本失败不计通过；最后完整重跑退出码0，35项均通过。额外真实renderer取证验证正常/最小明暗弹窗，属于主Agent内联复核，未冒称独立评审。

持久证据：[35项结果](../../.impeccable/review/model-settings-20261006/audit-results.json)、[参数草稿](../../.impeccable/review/model-settings-20261006/audit-budget-before-delete.png)、[切换恢复](../../.impeccable/review/model-settings-20261006/audit-dirty-navigation.png)、[删除确认](../../.impeccable/review/model-settings-20261006/audit-delete-provider.png)、[断线保留](../../.impeccable/review/model-settings-20261006/audit-offline-save.png)、[最终状态](../../.impeccable/review/model-settings-20261006/audit-final-state.png)、[最小浅色](../../.impeccable/review/model-settings-20261006/model-recovery-minimum-light.png)、[最小深色](../../.impeccable/review/model-settings-20261006/model-recovery-minimum-dark.png)、[复核记录](../../.impeccable/review/model-settings-20261006/verification.json)。

| 验证 | 实际结果 |
| --- | --- |
| Debug / Release renderer、native harness、persistence | 各3/3，renderer 324 checks / 0 failures，harness 173，persistence 287 |
| ASAN / UBSAN renderer | 各1/1，无sanitizer诊断 |
| C++格式 / 公共头边界 / Git差异 / Python语法 | 通过；49个公共头0违规 |
| Impeccable detector | 单次执行返回[]；C++覆盖有限，以实际交互和像素为准 |

复跑命令：

```sh
cmake --build build/native-debug --target mirage-native native_conversation_view_test mirage-format-check mirage-boundary-check -j3
ctest --test-dir build/native-debug -R 'native_conversation_view_test|native_agent_integration_test|persistence_test' --output-on-failure
ctest --test-dir build/native-release -R 'native_conversation_view_test|native_agent_integration_test|persistence_test' --output-on-failure
cmake --build build/asan --target native_conversation_view_test -j2
ctest --test-dir build/asan -R native_conversation_view_test --output-on-failure
cmake --build build/ubsan --target native_conversation_view_test -j2
ctest --test-dir build/ubsan -R native_conversation_view_test --output-on-failure
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-minimax --settings-audit --expect-fixed --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-settings-delivery-20261006
```

驱动需要Python Xlib/Pillow、Xvfb、IBus、gnome-keyring及gdbus。fixture目录的`config.toml`使用`model="MiniMax-M3"`、`model_provider="fixture"`及`model_providers.fixture`的`base_url="https://api.minimaxi.com/v1"`、`wire_api="chat"`；`auth.json`只填公开占位值`{"OPENAI_API_KEY":"fixturekey"}`。settings-audit只执行设置路径，不进入聊天提交；原脚本不带此参数的路径仍可能调用模型。

## 边界与待补跑

不改依赖pin、业务协议、凭据存储或Executor路径；编辑状态在UI主线程，IPC仍由既有RuntimeBridge管理。无新增跨上下文状态，不重复跑TSAN或声明跨平台性能。CI原生job补齐CURL开发包/Xvfb，并纳入renderer回归；远端结果随后记录。Windows既有MSVC告警失败仍独立存在，Windows/Wayland/物理高DPI/屏幕阅读器由维护者在对应环境补跑。本轮未验证厂商鉴权/连通性，未增加模型发现或连接探测，不声明完整ZCode 1:1。Impeccable报告的既有design.json漂移未作为本次交互修复的副作用迁移；M6整体保持In Progress。
