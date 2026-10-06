# BUG-20261006-003：首次连接前预设选择

> 状态：Completed（Linux X11 本地范围）
> 日期：2026-10-06
> 负责人：Mirage维护者
> 依据：M6-23、[DEC-044](../decisions/DEC-044-model-editor-interactions.md)

维护者报告预设无法点击。实际Release应用启动在尚不存在的私有Service socket上，点击OpenAI后名称与内容未变化；正常连接时11个预设可切换。根因为DEC-044统一加载门禁同时禁用了只需本地数据的预设入口，记录为BUG-20261006-003。

现在首次目录加载前允许鼠标/键盘选择、切换预设并显示连接提示，字段编辑与保存仍须等待ACK。预览不产生未保存修改，不写入Service。有效ACK后保持选择：若已有该服务，采用其保存的地址、模型与凭据引用；否则转为可编辑草稿。重连期间已有未保存草稿、保存中和弹窗期间的保护继续生效。修改仅涉及UI主线程状态及既有请求入口，无新增并发、凭据格式、依赖pin或公开协议。

真实Release应用与Service在私有Xvfb/DBus/IBus/钥匙环和临时XDG目录执行；只使用公开合成Key，不发起推理、不读取或修改用户配置。初次断线、离线预设切换、字段门禁、启动Service后刷新、保存配置保留和11个预设/未保存导航共27/27项；正常连接共23/23项。两次完整驱动退出码0。测试驱动第一次像素断言将焦点光标变化误判为Key编辑；排除光标列并配合源码输入状态断言后完整重跑通过，中途失败不计通过。

| 验证 | 结果 |
| --- | --- |
| Debug / Release renderer、native harness、persistence | 各3/3；renderer新增门禁/预览/ACK回归，341 checks / 0 failures |
| ASAN renderer | 1/1，无sanitizer诊断 |
| 格式 / 公共头边界 / Git差异 / Python语法 | 通过，49个公共头0违规 |
| Impeccable detector | 单次[]；C++覆盖有限，以实际窗口与状态断言为准 |

证据：[修复前失败](../../.impeccable/review/preset-selection-20261006/before-offline-results.json)、[修复前窗口](../../.impeccable/review/preset-selection-20261006/before-blocked.png)、[断线及恢复27项](../../.impeccable/review/preset-selection-20261006/offline-results.json)、[正常23项](../../.impeccable/review/preset-selection-20261006/online-results.json)、[离线预览](../../.impeccable/review/preset-selection-20261006/offline-preview.png)、[正常选择](../../.impeccable/review/preset-selection-20261006/online-selected.png)。实际窗口为1180×800，内联像素复核，无独立Agent评审。

复跑：

```sh
cmake --build build/native-debug --target mirage-native native_conversation_view_test mirage-format-check mirage-boundary-check -j3
ctest --test-dir build/native-debug -R 'native_conversation_view_test|native_agent_integration_test|persistence_test' --output-on-failure
ctest --test-dir build/native-release -R 'native_conversation_view_test|native_agent_integration_test|persistence_test' --output-on-failure
ctest --test-dir build/asan -R native_conversation_view_test --output-on-failure
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-selection-only --offline-start --expect-fixed --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-preset-offline
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-selection-only --expect-fixed --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-preset-online
```

环境与fixture配置沿用[模型设置验收](model-settings-interactions-20261006.md)。需要先构建对应测试二进制；preset-selection-only不会提交聊天或保存用户输入，offline-start仅通过公开IPC在私有Service生成两条合成已保存服务，用于证明重连不会覆盖它们。

前一提交fa8f192的CI（run 37446382775）Linux debug/release/ASAN/UBSAN/TSAN、原生EUI与格式/边界均成功；Windows既有MSVC告警失败仍存在。本次新提交远端结果另验，不声明通过。Windows、Wayland、物理高DPI和屏幕阅读器由维护者在对应环境补跑；本轮无新增跨上下文状态，不重复TSAN/UBSAN。M6整体保持In Progress。
