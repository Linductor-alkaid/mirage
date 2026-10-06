# BUG-20261006-004：API Key本地草稿输入

> 状态：Completed（Linux X11 本地范围）
> 日期：2026-10-06
> 负责人：Mirage维护者
> 依据：M6-23、[DEC-044](../decisions/DEC-044-model-editor-interactions.md)

维护者报告Key输入框无法输入。此前BUG-003只放开初次连接前的预设选择，输入事件仍要求model_loaded；实际Release窗口在不存在的Service socket上键入合成Key，输入框不变化，复现BUG-20261006-004。未修改EUI或Mira依赖。

Key现为有界本地草稿：初次连接前可键入、退格、选中替换、粘贴和显隐；其他配置字段、保存和删除继续等待ACK，保存/弹窗期间禁止输入。未保存Key参与导航确认，本地取消不写入Service。首次ACK按所选预设恢复已有服务的地址、模型及凭据引用，同时保留新Key；未命名服务保留Key和新ACK目录。状态提示说明连接后才能保存，不自动提交推理或配置。

实际验收使用私有Xvfb、DBus、IBus、系统钥匙环与临时XDG目录，全部Key均为公开合成值；不读取或修改用户现有配置，不发起推理。源码回归补充空ACK、已有服务ACK、未命名草稿、保存/弹窗门禁与目录隔离。试跑中排除了焦点光标像素差异，并把保存验证从目录固定索引改为稳定provider_id：保存会把当前服务排到其他服务之后，不能据索引判断Key是否更换。修复前失败与试跑断言不计通过。

最终断线/重连路径39/39项通过，覆盖连续键入、退格、全选粘贴替换、显隐、非法粘贴保留、加载前禁止保存、导航保留/放弃、重连恢复已存连接并保留Key、显式保存后凭据引用更新、其他服务不变及11个预设。正常连接下粘贴保存与键盘替换Key也通过。两个完整驱动退出码0。截图只保留隐藏Key的状态，试验中的显隐只使用合成Key，未把真实凭据送入取证。

| 验证 | 结果 |
| --- | --- |
| Debug / Release renderer、native harness、persistence | 各3/3；349项renderer检查，0失败 |
| 最后离线提示文案变更后的renderer重跑 | Debug / Release / ASAN各1/1，均349项0失败；实际断线39项按最终二进制重跑 |
| ASAN renderer | 1/1，无sanitizer诊断 |
| 格式 / 公共头边界 / 差异 / Python语法 | 通过，49个公共头0违规 |
| Impeccable detector | 单次[]，C++覆盖有限，以实际窗口和状态断言为准 |

证据：[修复前失败](../../.impeccable/review/key-input-20261006/before-results.json)、[修复前窗口](../../.impeccable/review/key-input-20261006/before-blocked.png)、[39项结果](../../.impeccable/review/key-input-20261006/offline-results.json)、[正常保存与替换](../../.impeccable/review/key-input-20261006/online-results.json)、[离线键入](../../.impeccable/review/key-input-20261006/offline-typed.png)、[重连草稿](../../.impeccable/review/key-input-20261006/reconnected-draft.png)、[保存回执](../../.impeccable/review/key-input-20261006/reconnected-saved.png)、[复核记录](../../.impeccable/review/key-input-20261006/verification.json)。实际窗口1180×800，主Agent内联复核，不冒称独立评审。新提交远端CI另验，未声明通过。

复跑采用[现有模型设置fixture与环境](model-settings-interactions-20261006.md)：

```sh
cmake --build build/native-release --target mirage-native native_conversation_view_test -j3
ctest --test-dir build/native-release -R 'native_conversation_view_test|native_agent_integration_test|persistence_test' --output-on-failure
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-selection-only --offline-start --expect-fixed --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-key-offline
python3 tests/manual/native_conversation_acceptance.py --build build/native-release --provider /tmp/mirage-key-fixture-provider --model-settings --preset-minimax --settings-only --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb --output /tmp/mirage-key-online
```

代码仅改变UI主线程的本地事务及既有请求入口；无新增Executor任务、凭据存储机制、协议或依赖pin。当前开发前端入口仍需要独立Runtime Service（README已有启动步骤），本轮不交付M6-04整体进程入口。Windows、Wayland、物理高DPI和屏幕阅读器由维护者在目标环境补跑，不声明跨平台完成或重跑TSAN/UBSAN。
