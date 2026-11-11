# 最新视频会话问题修复验收

日期：2026-10-29～30；负责人：Mirage维护者。基线 b7408d5，同一工作树的无关
记忆设计/反馈改动未纳入。本次按 [DEC-137](../decisions/DEC-137-session-evidence-and-task-purpose.md)
落实 [会话复盘](../analysis/m9-video-session-comparison-20261029.md) 的三个问题。

## 修复与证据

- 新红测在工具结果经过验证回填后越过40KiB，连续发布 valid_parts 检查失败；
  构建成功、测试退出1，复现过程快照停更的必要条件。统一收敛后同脚本60次
  完成＋最新失败均可发布；96行拥挤条件仍保留最新16KiB结果，重复收敛稳定。
  真实 DesktopActionVerification 的 owned receipt 回填也覆盖分类预算边界，
  大结果保留可解析验证摘要。原64KiB/96行/40+16+8KiB限额没有增加。
- AT-SPI私有D-Bus桌面实际遍历：同代两个子树一次返回7节点，独立根、唯一引用和
  父索引正确。组合总预算6节点拒绝；重复/缺失/无来源/过期根、取消均拒绝且保留
  原表；成功后旧 scope 动作仍拒绝。未缓存跨代对象、未自动重放失败动作。
- 可控 ModelLayer 覆盖产品必须声明用途：漏声明的合法截图请求在采集前拒绝，
  capture=0、终态失败；navigation 无 feed 也可声明并完成一次实际fixture采图，
  回答附范围披露。导航可升级内容，内容不能降级。产品省略 auto_storyboard 时
  使用自动缓存拼图，仍经实际 Completed 与 assess。片段成功，未评估、完整视频、
  音频要求、来源失效/取消按原门禁失败/取消。旧手动嵌入默认兼容。
- 受控两帧脚本的自动产品流程仍为3次模型调用、0次显式视觉取图工具、1张拼图，
  供图额外capture/input均0；手动片段流程4次模型调用、1次拼图工具。仅此脚本
  计数，不外推线上成本、语义质量或自然任务收益。

## 环境与准确命令

Linux 7.0.0-38-generic x86_64；Ubuntu GCC13.3.0、CMake3.28.3、Ninja。
依赖pin与锁不变。Debug/TSAN无原生前端，ASAN/UBSAN沿缓存启用原生前端；这些
配置PPOCR/BUNDLE_VISION=OFF。native-release为Release、原生前端与
PPOCR/BUNDLE_VISION=ON。平台用例为私有D-Bus/Xvfb/受控provider，没有读取用户
桌面媒体、访问在线模型、启动新任务或重启服务。

```bash
cmake --build build/debug -j2
ctest --test-dir build/debug --output-on-failure
cmake --build build/asan -j1 --target conversation_budget_test atspi_scoped_snapshot_test desktop_atom_toolset_test desktop_image_supply_test post_action_verification_test visual_marks_test runtime_service_test perception_storyboard_test tool_alias_provider_test harness_desktop_tools_test native_agent_integration_test
ctest --test-dir build/asan -R '^(conversation_budget_test|atspi_scoped_snapshot_test|desktop_atom_toolset_test|desktop_image_supply_test|content_visual_supply_test|post_action_verification_test|visual_marks_test|runtime_service_test|perception_storyboard_test|tool_alias_provider_test|harness_desktop_tools_test|native_agent_integration_test)$' --output-on-failure
cmake --build build/native-release -j1 --target mirage-native-product conversation_budget_test atspi_scoped_snapshot_test desktop_atom_toolset_test desktop_image_supply_test post_action_verification_test visual_marks_test runtime_service_test perception_storyboard_test tool_alias_provider_test harness_desktop_tools_test native_agent_integration_test
cmake -DROOT_DIR="$PWD" -P cmake/CheckFormat.cmake
cmake -DROOT_DIR="$PWD" -P cmake/BoundaryCheck.cmake
```

最终顺序复验仅将测试正则改为：

```text
^(conversation_budget_test|desktop_image_supply_test|content_visual_supply_test|post_action_verification_test|visual_marks_test|tool_alias_provider_test|harness_desktop_tools_test)$
```

UBSAN/TSAN专项构建和正则同ASAN，替换目录；TSAN使用
`setarch x86_64 -R ctest ...`。native-release测试正则同上。配置之间构建可以并行，
最终测试在构建完成后串行执行，防止资源争用误触生产路径deadline。

## 结果

专项开发回归最终6/6（19.74秒）；早期失败包含新工具版本断言、成功终态新增行、
测试工具名/参数错误及测试执行器关闭后误排新用例，均已修正；生产预算和权限
未放宽。初次全量Debug为119/121（371.94秒）；旧Runtime脚本漏用途声明与
终态行数断言失败，同时发现零桌面Provider环境多暴露用途工具，已按真实能力
限制产品启用。迁移脚本后harness专项通过，native持久化断言再修正后3/3通过。
Windows scoped 请求增加明确拒绝，Windows构建/运行未执行。

完整矩阵（截断顺序复核前，源码摘要
`85b458b850a4c89f2d2d6086c2b2d8beecb2c53adf241a68d0fa4c7566619d0d`）：

| 配置 | 结果 | ctest耗时 |
| --- | --- | --- |
| debug | 121/121，无跳过 | 349.35秒 |
| asan | 14/14，无跳过 | 154.30秒 |
| ubsan | 14/14，无跳过 | 136.07秒 |
| tsan | 14/14，无跳过 | 367.19秒 |
| native-release | 14/14，无跳过 | 55.66秒 |

收口复核新增实际 ConversationCapture::finish_tool → owned verification → publish
回归，发现调用入口先截断JSON会使摘要回填失效；新增红测失败（14.22秒），改为
先投影、再在publish统一收敛后4/4通过（15.55秒）。本次没有改变公开API或增加
预算。对受影响的工具结果、日志、输入/供图和生命周期做五配置定向复验；未变更
的AT-SPI/工具schema/纯内容元数据与native持久化路径沿上表，不重复扩大测试。

最终源码24文件摘要：
`dfaf1b08d927e07ac12f6c1b2dfda06a582fc6a3740181d6ac70b474ad8e084e`。
最终定向复验：

| 配置 | 结果 | ctest耗时 |
| --- | --- | --- |
| debug | 9/9，无跳过 | 74.26秒 |
| asan | 9/9，无跳过 | 123.78秒 |
| ubsan | 9/9，无跳过 | 109.75秒 |
| tsan | 9/9，无跳过 | 322.69秒 |
| native-release | 9/9，无跳过 | 37.43秒 |

五配置最终构建全部成功，源码摘要在执行前后保持一致；格式检查通过，公开边界
67头零违反，DEC-096既有两项seam保留。没有扩超时或跳过测试。

完整产品已重新构建成功，五个目标由聚合目标确认；托盘SHA256为
`441a311d1694ac22af43813789103d534c2c5e22cb18e8c422ec217ae7dfc94f`，
包含 task_purpose_required 新门禁。没有重启已运行进程。
原始日志前缀 `/tmp/mirage-dec137-`，冻结manifest与日志副本放在ignored目录
`out/m9-session-evidence-fix-20261030`；本机保留30天后由维护者清理，不入提交。

## 保留门禁

原会话没有保留 stderr 拒收诊断，本次已复现并修复符合其边界的代码缺陷，不能
据此排除原会话还存在其他无效投影条件。

真实同模型/同起点视频任务的理解正确性、调用/token/CPU/RSS对照未跑；M9-29/30/33
完整退出仍开放。用途声明不是独立意图分类器，navigation 披露不能证明模型正确
理解了用户需求；完整视频播放时间覆盖、声音证据仍未交付。负责人Mirage维护者，
补跑条件为下次正常启动新产品进程后，记录真实映像摘要并执行固定任务对照。
Windows、Wayland/Portal及5/10Hz目标硬件精度/性能门禁不由本机fixture替代。
