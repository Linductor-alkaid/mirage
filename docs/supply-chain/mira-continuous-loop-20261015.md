# Mira 持续循环能力 pin 升级审计

> 状态：In Progress
> 日期：2026-10-15
> 授权：维护者明确要求“ci 全绿，合并并更新 mirage 的 pin”。
> 工作项：M9-02 的依赖消费子项；M9-03 产品接线另行实施。
> 所属计划：[Mirage 实施总计划](../plans/mirage-implementation-plan.md)

## 来源与变更范围

旧 pin：`560da5ec7cab3ae4a0129c047b86ed57ae18501f`。
新 pin：`a29374aaf05b956cbd5ceeafc65bba4c6172675e`，来自
[Mira PR #99](https://github.com/Linductor-alkaid/mira/pull/99) 的授权 squash 合并。
合并前 head `23a53b7` 的 push/pull_request 两套矩阵 **24/24 SUCCESS**，
涵盖 GCC/Clang Debug/Release、Windows、Android、ASAN/UBSAN/TSAN 和 quality。

升级独立分支 `build/mira-continuous-loop-pin` 基于当前开发分支
`feat/m8-pet-resource-loading` 的 `4da3fd6`，MR 以该分支为 base；不把当前工作区
尚未提交的 M9/pet/视觉状态层实现纳入此次依赖变更。当前工作区同步同一个 pin。

## 契约差异与消费

- 上游 DEC-056 提供显式 Continuous 模式、Request scope（model contract 2.0）、
  提交式历史压缩、完整最近工具/thinking 轮保护、有界历史及逐轮记录窗口。
- 累计诊断使用饱和 uint64；持续模式的恢复/续写按连续故障段限制，成功结算工具轮
  后重置段内额度。Finite、有限配置的零值和默认 v1 请求保持兼容。
- Mirage 显式选择 Finite，保留现有 16 模型轮/32 工具策略。原 DialogCompletion
  的 32 位诊断字段在 Adapter 饱和转换，避免新上游计数隐式窄化或回绕；不改变
  现有产品/IPC 契约。M9-03 启用 Continuous 前须同步扩大产品计数与对应协议。
- 本次只交付依赖前置，不宣称 Mirage 原始长任务终止问题已经修复；产品策略、
  请求预算、完整上下文和无进展判断仍由 M9-03/04 承载。

## 供应链与许可证

旧新 Mira 区间只有其自研实现、公开头、测试和文档变化；未改变 `.gitmodules`、
第三方 gitlink 或许可证文件。Mira 仍为 AGPL-3.0。

| 嵌套依赖 | 本次前后 pin |
| --- | --- |
| Mira → kairo | `32027c247dd4b9280d7c995454b732bac0db9095`（v0.7.0） |
| Mira → Mbed TLS | `068ff080b369adfac81509f9b57b2afabaf82dc5` |

Mirador、EUI、googletest 和 binary artifacts 锁定保持不变，无新增依赖或模型下载。
submodule 与 dependencies.lock.json 同步；本机从已有 checkout 复用对象，未修改
依赖源码。隔离 checkout 初次选用本机 Mirador 开发仓库缓存时缺少目标 commit，
随后从主 Mirage 的已验证 pinned checkout 补齐该对象，checkout 到锁定 commit。

## 验证与验收

- [x] 离线 pin/嵌套 pin 校验及 Linux Debug 全量回归。
- [x] 从消费侧实际链接的新 Mira 库验证持续循环契约。
- [x] 适用 consumer sanitizer、native 编译与静态门禁。
- [ ] 依赖 MR、反馈台账与当前工作区的计划状态同步。

完整命令、结果和平台限制在本节追加；未执行项不标为通过。


2026-10-15：Linux x86_64 / GCC 13.3.0 / CMake 3.28.3 验证完成。

- 隔离升级分支：`cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
  -DMIRAGE_WITH_MIRA_MBEDTLS=OFF`、`cmake --build --preset debug -j6`、
  `ctest --test-dir build/debug --output-on-failure -j4`：**82/82 通过**（96.99 s）。
- 当前工作区（包括已有 M9/pet 改动）相同离线配置，`cmake --build --preset debug
  -j4`、同一 ctest 命令：**84/84 通过**（66.15 s）。这组工作区额外改动未纳入
  独立升级 MR，纯升级基线以上一组为准。
- 当前工作区 ASAN/UBSAN：`cmake --preset <asan|ubsan>
  -DMIRAGE_FETCH_DEPENDENCIES=OFF`，构建 harness_desktop_tools_test、
  desktop_context_builder_test、native_agent_integration_test（`-j3`）。
  ctest 正则 `^(harness_desktop_tools_test|desktop_context_builder_test|native_agent_integration_test)$`，
  `--output-on-failure`；ASAN_OPTIONS=`detect_leaks=1:halt_on_error=1`、
  UBSAN_OPTIONS=`halt_on_error=1:print_stacktrace=1`：分别 **5/5 通过**
  （39.41 s / 25.84 s；含 2 个隔离环境 setup/cleanup，实际行为测试 3 个）。
  CMAKE_CXX_FLAGS 分别包含真实 address / undefined instrumentation。
- Native：`cmake --preset native-debug -DMIRAGE_FETCH_DEPENDENCIES=OFF`，
  `cmake --build build/native-debug --target mirage-native-product -j6` 通过；
  mirage、mirage-service、mirage-tray、mirage-native、mira_pet 均构建。
  原 `-j2` 编译由本次工作主动中断后改为 `-j6`，不是代码构建失败。
- 隔离分支 `mirage-format-check`、`mirage-boundary-check` 通过（58 公开头，
  0 违规）；变更文件 clang-format、git diff --check 通过。当前工作区已有 dirty
  测试的格式问题不带入升级 MR。新增/编辑文档相对链接与围栏检查通过。台账的持续循环早期草稿曾与 UTF-8
  续写项重用 001，本次统一为 002 并更新解析状态，保留 PR#98 的原 001 记录。

消费侧持续契约探针直接使用新 pin 自带测试源和本次 Mirage 构建出的静态库，
未复用旧 Mira binary；在主工作区根执行：

```bash
c++ -std=c++20 -g -Ithird_party/mira/include -Ithird_party/mira/tests \
  -isystem third_party/mira/third_party/kairo/include \
  -Ibuild/debug/third_party/mira/third_party/kairo \
  third_party/mira/tests/m3/m3_continuous_conversation_test.cpp \
  -o /tmp/mirage-mira-continuous-pin-probe \
  build/debug/third_party/mira/libmira_core.a \
  build/debug/third_party/mira/libmira_simulator_adapter.a \
  build/debug/third_party/mira/libmira_net_transport.a \
  build/debug/third_party/mira/third_party/kairo/src/libkairo.a \
  build/debug/third_party/mira/libmira_core.a \
  build/debug/third_party/mira/third_party/kairo/src/libkairo.a -latomic -lrt -pthread
/tmp/mirage-mira-continuous-pin-probe
```

编译及运行 exit=0：1,025 轮/1,024 工具、同一 task/epoch、compactor 输入最多
5 条/提交历史最多 3 条/结果窗口 4 条、无历史 operation 墓碑，兼容有限/零值、
请求准入/序列化/溢出、完整签名工具组、恢复/续写段、取消/期限/拒绝/shutdown
均由 pinned 源断言验证。

限制：本次未执行 Mirage Windows/原生 Wayland/真实模型长任务运行，也未重跑
Mirage consumer TSAN（没有新增并发路径；上游 PR#99 的 TSAN 已全绿）。负责人
为 Mirage 维护者；补跑条件分别为升级 MR 的 Windows CI、目标 compositor 与
M9-03/12 接线验收。本次没有安装、发布或替换正在运行的 native-release。
