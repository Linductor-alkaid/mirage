# 开发入口与模型保存验收

> 状态：Completed（Linux X11）
> 日期：2026-10-06
> 负责人：Mirage维护者 / Codex
> 工作项：M6-24；BUG-20261006-005；依据DEC-007/037

用户输入Key后保存仍灰的根因为已注册开发desktop条目直接启动UI，没有Runtime Service。
保存门禁要求服务连接与模型目录ACK；这次不绕过门禁，将开发条目接入既有mirage start，
注册目标构建CLI/Service/Tray/UI全部前提，保持图标与WM_CLASS。

## 实际验证

构建：`cmake --build build/native-release --target mirage mirage-service mirage-tray mirage-native -j3`。
真实GIO从生成条目冷启动：

```bash
python3 tests/manual/native_conversation_acceptance.py --build build/native-release \
  --provider /tmp/mirage-key-fixture-provider --model-settings --preset-minimax \
  --settings-only --desktop-launch \
  --xvfb /home/linductor/.local/mirage-sysroot/usr/bin/Xvfb \
  --output /tmp/mirage-desktop-boot-confirm
```

测试使用私有XDG/DBus/Xvfb与合成Key，没有预先启动Service。三个真实独立进程启动成功；
仅输入Key后保存成功，键入替换Key保存成功；持久化只有凭据引用，无明文Key。
再次无界面启动复用同一Service；未调用厂商推理。外部测试owner收养/回收分离子进程，
不会读取或修改用户配置。首次测试驱动对GIO executable引号比较错误，修正为GLib参数解析，
该失败不计作产品通过；最终完整重跑通过。

Release的persistence/native harness/native renderer/dependency lock门禁4/4通过。
私有XDG注册与用户显式注册均成功，用户条目已更新。
本轮仅启动配置/CMake/手工验证驱动变化，无C++或新并发，不重复sanitizer门禁。
公开结果保存在 `.impeccable/review/desktop-model-save-20261006/`。

## 边界

本次证据是旧三进程拓扑的入口修复；后续托盘内嵌Runtime要求另由DEC-045追踪。
Windows、Wayland、目标桌面真实托盘视觉与M6-04整体退出未由此记录验收；
负责人为维护者，需在目标平台提供桌面/通知区与相应构建补跑。
