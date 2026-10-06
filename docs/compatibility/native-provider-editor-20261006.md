# 原生侧栏与模型服务编辑验收（2026-10-06）

> 工作项：M6-21，Linux X11范围 Completed
> 依据：[DEC-042](../decisions/DEC-042-native-provider-editor.md)
> 负责人/复核：本轮实现 Agent（同一 Agent 内联复核，非独立评审）

## 对照与实现

参考固定为 ZCode `29628c9acdb81b703bbd4080c207a0e7ce5e276e`，不是凭官网宣传图推测表单。

| ZCode源码/行为 | Mirage本轮映射 |
| --- | --- |
| [SectionLayout](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/settings/model-provider-section/SectionLayout.tsx)：224/56px分栏，详情24px内缩进 | 相同服务导航宽度/详情内缩进；窄窗口保留图标栏 |
| [Navigation](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/settings/model-provider-section/Navigation.tsx)：32px行、图标与文字 | 32px服务行，按服务ID去重，选中/hover/键盘激活 |
| [ProviderCardSections](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/settings/model-provider-section/ProviderCardSections.tsx)：标题/开关/更多菜单、连接、Key、模型 | 服务标题内编辑与重命名/删除、启用开关、Base URL、API格式、Key、模型列表/添加/删除、上下文/思考配置 |
| [ProviderApiFormatSelect](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/settings/model-provider-section/ProviderApiFormatSelect.tsx)：API下拉 | 已接入的Chat Completions/Responses下拉，显示请求路径 |
| [InlineEditableProviderCard](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/settings/model-provider-section/InlineEditableProviderCard.tsx)：编辑/权威保存/错误保留 | 独立编辑副本，成功ACK更新目录/应用状态，失败保留名称/模型/Key；显式保存及取消 |
| 空服务可编辑名称后保存 | 无目录时本地“未命名服务”，无模型也可保存为停用；成功ACK后更新左侧名称 |

侧栏行外缩进20px，品牌/导航/历史图标中心x=44、文字x=64，返回/新建使用同一轴。
服务与模型分别呈现；provider_id/provider_name为产品元数据，旧配置兼容。
模型身份独立于可修改名称及供应商模型ID；同服务共享连接和Key。
删除最后一个服务以显式空数组持久化，不再保留旧目录。

## 实际执行

| 验证 | 结果 |
| --- | --- |
| native-debug / native-release，构建产品前端/服务与三项相关测试 | 各3/3通过：native_conversation_view_test、native_agent_integration_test、persistence_test |
| ASAN（detect_leaks=0:halt_on_error=1）同三项 | 3/3，无ASAN诊断；不构成LSAN验收 |
| UBSAN（halt_on_error=1:print_stacktrace=1）同三项 | 3/3，无UBSAN诊断 |
| TSAN构建；setarch x86_64 -R执行harness与persistence | 170 + 287 checks，0 failures，无TSAN诊断 |
| 实际EUI/GLFW/OpenGL原生渲染与控制回调 | 203 checks，0 failures；正常/最小、明暗、空服务、保存后名称、分组、多模型、协议下拉、重命名键盘、失败保留/取消、模型删除/编辑预算与身份保留 |
| 故障注入 | 配置文件写入障碍拒绝整个分组更新和新Key；旧服务名/引用/目录保留，新Key回滚；原有活动拒绝、取消/超时/关闭测试继续通过 |
| dependency_lock_gate_test、CheckFormat.cmake、公共头边界 | 1/1；格式通过；49 headers / 0 violations |
| Git diff检查、凭据内容扫描 | 通过；无实际凭据或大于5MiB文件进入本轮提交；三个submodule不变 |

原始本机日志为`/tmp/mirage-provider-final-<配置>-{build,tests}.log`，
TSAN为`/tmp/mirage-provider-tsan-{build,tests}.log`。

### 真实窗口与模型

手动驱动器`tests/manual/native_conversation_acceptance.py --model-settings`启动私有Xvfb、DBus、
IBus和gnome-keyring：把私有daemon的default alias指向其未锁定session collection，
不接触用户桌面、已有会话或用户钥匙环。使用维护者授权的本机SiliconFlow模型配置，
真实API Key仅经UI遮蔽输入与系统Secret Service存储，不经环境变量绕过本轮新配置流程。

验证流程：空配置 -> 未命名服务 -> 输入名称/Base URL/Key -> 添加真实模型 -> 保存 -> 左侧名称更新
-> 刷新仍已配置 -> 普通配置磁盘只有credential_ref -> 回到会话 -> 真实模型回复。
当前Release运行结果：`modelSettingsFromEmpty=true`，Qwen/Qwen3.5-4B，241次预览，
最大/最终1251字节，终态ok且持久历史一致；中文候选(400,378)/(400,402)、等待动画、空草稿不创建会话、
空闲退出均通过。此前私有default collection不存在的失败场景确实被拒绝，界面保留全部输入并提示钥匙环不可用；
该失败截图单独保留，不算成功验收。

[原始结果](../../.impeccable/review/provider-editor-20261006/live-results.json) /
[空服务](../../.impeccable/review/provider-editor-20261006/model-empty-live.png) /
[真实保存](../../.impeccable/review/provider-editor-20261006/model-saved-live.png) /
[失败保留](../../.impeccable/review/provider-editor-20261006/keyring-failure-live.png) /
[最小深色夹具](../../.impeccable/review/provider-editor-20261006/provider-minimum-dark.png)。
`provider-*.png`是标注的原生合成夹具，`model-*-live.png`与completed-live是实际产品窗口，不能混称真实网络数据。

复跑：

```sh
cmake --build --preset native-release --target mirage-native mirage-service native_conversation_view_test native_agent_integration_test persistence_test -j2
ctest --test-dir build/native-release -R '^(native_conversation_view_test|native_agent_integration_test|persistence_test)$' --output-on-failure
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /home/linductor/mira/docs/model_provider/siliconflow --model-settings --output /tmp/mirage-provider-acceptance --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb
```

最后一条需Python Xlib/Pillow、系统IBus/libpinyin、gdbus/gnome-keyring及已授权模型凭据，产生一轮真实请求。

## 声明边界

本轮完成上述Linux功能与源码对照，不宣称完整ZCode产品或像素级1:1。
没有获得运行中ZCode窗口截图；图标库、字体和完整设置仍有差异。
ZCode的Anthropic、OAuth/账户套餐、独立模型连接测试和额外模型元数据、自动闲时保存/拖拽排序尚未接入Mirage，
不能用静态开关冒充能力。当前上下文/思考仍为列表下的原生编辑，不等同于ZCode完整元数据弹窗。
Windows/原生Wayland/高DPI未执行：负责人为维护者，在对应系统运行native preset并完成同一交互矩阵后补验。
M6整体保持In Progress，M6-04统一入口/托盘生命周期仍Planned。

Git：Runtime契约提交`e3645ae`；前端/证据以同一分支的`fix(ui): align sidebar and rebuild provider editor`提交交付。
本轮未修改依赖、未推送/合并或发布。
