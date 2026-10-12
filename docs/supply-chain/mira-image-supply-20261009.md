# Mira 图片供给入口升级审计

> 日期：2026-10-09
> 状态：Completed（Linux 依赖升级与回归；后续图片验收独立链接）
> 负责人：Mirage 维护者
> 工作项：[M7-05 前置](../plans/m7-visual-tools-and-input.md)
> 反馈：[MIRA-20261009-001](../dependency_feedback/mira.md#mira-20261009-001)

维护者明确授权依赖升级。Mira 从
`bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5` 固定到
`698fc77c151fa93b1b52ea4dd25cb95a3a60bee5`，来源仍为
`https://github.com/Linductor-alkaid/mira.git`。gitlink 与
`dependencies.lock.json` 同步；没有修改第三方源码。

差异为八个文件：ConversationLoop 公共头与实现、新图片供给测试及其注册、API/决策/
计划文档。[PR#86](https://github.com/Linductor-alkaid/mira/pull/86) 提供逐请求的
`set_model_input_supplier`，默认限制 8 items / 8 MiB，含供给/drop/degrade 诊断与计数。
新增公开类型与结果字段，所有消费者重编译；现有未安装 supplier 的文本/工具循环沿用
原路径。接口、上游 CI 和宿主责任见[更新核对](../compatibility/mira-image-supply-review-20261009.md)。

Mira AGPL-3.0、许可证路径与版本号 0.1.0 不变；kairo `ef821dcca05bbf9038409a2ff9acc5ed729936bc`
（v0.6.0/MIT）、mbedtls `068ff080b369adfac81509f9b57b2afabaf82dc5`、SQLite 分发不变。
Mirador/EUI 与其嵌套 pin 不变。没有新增第三方包、二进制、线程、队列或调度器。
MIRA-20261008-002 的 GCC 警告临时措施仍需上游另行交付，本次不满足移除条件。

## 验证与验收边界

本次验证范围为 Linux 宿主的依赖锁、公开 API、新上游供给 fixture，以及现有 Mirage
文本/工具、输入取消与关闭回归。实际执行结果如下；没有验证 Mirage 图片端到端。

M7-04 编码 artifact、M7-05 ArtifactSource/图片能力/supplier 宿主接线与真实模型识图
仍待实施；本次升级不能证明模型已收到桌面图片。反馈保持 Accepted，完整图片验收后
才记 Resolved。Windows/Wayland 真机与真实识图未执行，负责人为 Mirage 维护者；
在对应平台和识图配置具备后按 M7-04/05/08 补跑。

## 本机复现命令

```bash
cmake --preset native-debug
cmake --build --preset native-debug -j3
ctest --preset native-debug
cmake --preset native-release
cmake --build --preset native-release -j3
ctest --preset native-release
c++ -std=c++20 -fsyntax-only -DMIRAGE_EXPECT_MODEL_INPUT_SUPPLIER=1 -Ithird_party/mira/include -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
cmake -S third_party/mira -B build/mira-image-supply-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMIRA_BUILD_TESTS=ON -DCMAKE_CXX_FLAGS=-Wno-useless-cast
cmake --build build/mira-image-supply-debug --target mira_m3_conversation_input_supply_test -j3
ctest --test-dir build/mira-image-supply-debug -R '^mira_m3_conversation_input_supply_test$' --output-on-failure
cmake --preset asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF
cmake --build --preset asan --target harness_desktop_tools_test action_contract_test atspi_backend_test element_target_executor_test desktop_atom_toolset_test native_agent_integration_test -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan -R '^(harness_desktop_tools|action_contract|atspi_backend|element_target_executor|desktop_atom_toolset|native_agent_integration)_test$'
cmake --preset tsan -DMIRAGE_ENABLE_NATIVE_FRONTEND=OFF
cmake --build --preset tsan --target harness_desktop_tools_test action_contract_test -j2
setarch x86_64 -R build/tsan/tests/action_contract_test
setarch x86_64 -R build/tsan/tests/harness_desktop_tools_test
cmake --build --preset native-debug --target mirage-boundary-check mirage-format-check
git diff --check
```

上游供给测试在独立、忽略的构建树运行，不启用 Mirage 主构建图中关闭的依赖测试；
GCC 警告设置沿用 MIRA-20261008-002 已登记的临时措施。ASAN 预设在已有 cache 中
同时启用 UBSAN，命令给出其实际标志；leak 检查关闭，不声称已验证内存泄漏。
TSAN 使用受控 ASLR，结果不包含默认 ASLR 环境下的兼容性承诺。

## 已执行结果

Linux x86_64/GCC 13.3.0：native-debug、native-release 全量各 54/54，均无 skip；
公开头正向探针、上游图片供给
fixture 1/1 通过；联合 ASAN/UBSAN 相关 6/6 通过；TSAN 的 action_contract 55 checks、
harness_desktop_tools 422 checks 各 0 failures，无竞态报告。公共头 53 个/0 violations，
格式检查通过，`git diff --check` 退出 0；锁文件和全部直接/嵌套 pin 校验通过。
Debug 全量用时 67.16 秒、Release 56.48 秒，ASAN/UBSAN 6 项用时 29.70 秒；
这些是本机测试时长，不是性能承诺。升级保持在工作树，暂存区为空；提交/MR 按工程
规范 10.7 与桌面工具实现独立评审，既有 `.impeccable` 未跟踪文件未触碰。

2026-10-09 后续实施：M7-04/05 在相同 pin 上接通 PNG artifact/source 与每请求图片，
三方言 fixture 与 MiniMax 合成截图在线 2/2。新增 source/encoder 均为 Mirage Adapter，
未引入额外依赖或修改第三方源码；最终工作树回归与范围见
[图片供给验收](../compatibility/desktop-image-supply-20261009.md)。本文件的 54/54
仍为单独依赖升级增量证据，不替代图片增量最终 55/55。
