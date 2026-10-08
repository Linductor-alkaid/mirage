# 窗口呈现与持续拖拽验收

> 日期：2026-10-08
> 状态：Validated（Linux X11/XWayland），Mirage CI/上游评审与其他平台开放
> 工作项：M6-35；负责人：Mirage 维护者
> 依据：[DEC-033](../decisions/DEC-033-native-agent-frontend.md)

## 结论与责任边界

持续拖拽整窗黑闪主要来自 Mirage 自绘 grip 每帧直接设置位置/尺寸，未使用 EUI 已
提供的系统原生缩放。真实 XWayland/GNOME 合成器下，尺寸变化到新内容提交之间能
读到整个新尺寸空表面。改为系统原生缩放接管后，维护者明确确认“拖拽顺畅且没有
黑色频闪”。该结论来自实际物理鼠标对照，不以合成 XTest 输入模拟原生合成器 grab。

EUI 另有可独立复现的首帧过早显示、更新后仍以旧尺寸提交缺陷。框架修复不能单独
消除上述 Mirage 频闪。中文 [issue #93](https://github.com/sudoevolve/EUI-NEO/issues/93)
已按真实桌面后续结果修正责任边界；[PR #94](https://github.com/sudoevolve/EUI-NEO/pull/94)
仅修复框架生命周期，尚未合并。

## 版本与实现

旧 pin：fd9c1a983e74d2a85afc3b156274afd4d8532c14；新 pin：
a7e625fecc1db1c1acb4850a14fb785e20600041。
新 pin 保留 #91/#92，增加 #94 的两个功能修复及参数缩进修正；OpenGL、许可证及依赖不变。
Mirage 的八方向映射、原生接管/回退标记、初次 show 防护均在应用私有窗口 Adapter。

## 环境与复现

Ubuntu 24.04.5 LTS、Intel Core Ultra 5 225H、Intel Arrow Lake-P、GCC 13.3、
CMake/Ninja。真实桌面为 GNOME Wayland + XWayland，内容缩放 2；私有 Xvfb 的
软件渲染和 DPI 1 无法代表该环境。脚本只启动独立 XDG/DBus 产品实例，无模型调用。默认 Xvfb 模式直接
configure 尺寸，验证 renderer 失效；host 自动 grip 模式用于旧路径对照，
原生合成器接管必须使用 manual-drag 物理输入。

```bash
python3 tests/manual/native_frame_presentation_acceptance.py \
  --build build/native-release --output /tmp/mirage-frame-acceptance
python3 tests/manual/native_frame_presentation_acceptance.py \
  --build build/native-release --output /tmp/mirage-frame-compositor-before --host-display
python3 tests/manual/native_frame_presentation_acceptance.py \
  --build build/native-release --output /tmp/mirage-frame-compositor-physical \
  --host-display --manual-drag
```

真实合成器的原生 grab 使用物理输入；XWayland XTest 输入不能替代 Wayland seat 的
真实拖拽。原生接管后不能以 bare Xvfb 中无窗口管理器的 grip 行为证明系统缩放。

## 实际证据与失败

1. 未修复 EUI 的 320×240→480×360 探针稳定报告旧尺寸提交和待重绘被清除。
2. 仅修复框架后的私有显示首次测试：38 次启动样本、60 次慢速 grip 采样及最终边缘
   无黑色像素，窗口复用/关闭通过。此证据不代表真实桌面频闪已经消失。
3. 维护者反馈仍频闪；确认运行进程 inode 与新构建相同，排除测试旧进程。真实桌面
   提高采样到每次位移 5 次、约 2ms 间隔，共 300 次；grow 76、shrink 73 次中心
   为黑色，四个采样位置同时为空。数据：/tmp/mirage-frame-compositor-before/results.json。
4. Mirage 改为 EUI 系统原生缩放后，维护者在独立诊断窗口物理拖拽约 10 秒，明确
   回答“拖拽顺畅且没有黑色频闪”。诊断采样因缩小期间 XGetImage 的 BadMatch 中断，
   不计作自动采样通过；脚本已对该采集竞态记录并继续采样，不伪造数量。
5. EUI 独立 Release 四组合各 34/34；新探针 Debug ASAN+UBSAN 通过。
6. Mirage 全量 Release 首轮 51/52，事件订阅压力测试两项终态断言失败；降低并行
   构建负载后仅重跑该测试 1/1 通过。保留初次失败，不写首次全量通过。
7. 基于 origin/master 的独立工作树，只包含本依赖升级与应用修复，完整配置/构建，
   Release 全量 ctest 52/52（49.49s）；日志 /tmp/mirage-frame-clean-{configure,build,tests}.log。
8. 最终私有 Xvfb renderer 诊断直接 configure 尺寸：启动 365、resize 300 次采样
   均无黑色中心；最终边缘、窗口复用与整体关闭通过。此模式没有验证 compositor grab。
   数据 /tmp/mirage-frame-clean-presentation/results.json。
9. 最终托盘真实生命周期与最小化/恢复独立测试 24 项全部通过：
   /tmp/mirage-frame-clean-tray/lifecycle-results.json。

首帧 map/swap 不是窗口系统原子操作；直接读取未合成的表面仍可能在两者之间获得
黑色像素。本修复缩短/规范准备阶段，不承诺任意平台、驱动或直接读回均绝对无黑帧。

## 最终复跑

```bash
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset native-release -j 4
ctest --preset native-release
python3 tests/manual/tray_runtime_acceptance.py --build build/native-release \
  --output /tmp/mirage-frame-clean-tray --window-manager
```

依工程规范 10.7，依赖升级为独立 [PR #79](https://github.com/Linductor-alkaid/mirage/pull/79)，
应用修复堆叠在其分支上；不包含原工作区的 Mira/kairo 迁移。

## 开放验证

- Windows 原生窗口、回退方向与最小化恢复：负责人维护者；具备 Windows 环境后
  使用 native preset 与实际鼠标验收，未执行不勾选完成。
- 上游 #94 [CI 四组合](https://github.com/sudoevolve/EUI-NEO/actions/runs/37791651376)
  全部成功；Review 与合入由维护者跟进，合入后同步正式 pin 并删除分支说明。
- 真机多 DPI/驱动、其他合成器和原生 Wayland：目标环境可用后补验；不作跨平台保证。
