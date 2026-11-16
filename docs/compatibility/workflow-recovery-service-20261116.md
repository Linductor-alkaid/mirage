# Workflow 产品显式恢复接线验收（2026-11-16）

> 状态：Completed（Linux 受控 IPC/CLI 增量；不关闭 M15）
> 负责人：Mirage 维护者
> 工作项：M15-19；依据：[DEC-148](../decisions/DEC-148-service-workflow-recovery.md)
> 基线：7b5bc28 上的本工作树改动（验证时未提交）；随后按维护者要求本地提交，未推送

## 范围与环境

Ubuntu 24.04.5 LTS / x86_64、GCC 13.3.0、CMake 3.28.3 / Ninja、clang-format 18.1.8。
沿用 debug/asan/ubsan/tsan 离线配置，MIRAGE_FETCH_DEPENDENCIES=OFF，native/OCR 关闭。
Mira 仍为 2c42627a23da83c6d04825c28163836f9f747ec3，kairo/Mirador/eui pin 和锁文件均未修改。
本轮只用隔离状态目录、受控桌面文件 Provider 与脚本模型；未访问用户文件、keyring、
真实桌面或在线模型，也未启动/重启用户正在使用的产品服务。

实际 RuntimeService 通过 Unix IPC 接受 start/status/cancel/lesson。ModelLayer 沿同一
Provider/Gateway/profile 工作，先通过 infer_mutex 即时独占预留；Host 继续使用 Mira 的
编排、patch 校验、实际 atom 续跑、学习及审计。没有第二模型栈、私有恢复 worker 或依赖修改。

## 命令与结果

各预设执行（Debug -j4，三个 sanitizer -j3）：

```bash
cmake --build --preset PRESET --target mirage mirage-service workflow_recovery_service_test runtime_service_test ipc_protocol_test ipc_protocol_golden_test workflow_recovery_host_test workflow_learning_host_test mira_host_test model_profile_endpoint_test -j JOBS
ctest --preset PRESET -R '^(workflow_recovery_service_test|workflow_recovery_cli_test|ipc_protocol_test|ipc_protocol_golden_test|runtime_service_test|workflow_experience_cli_test|workflow_recovery_host_test|workflow_learning_host_test|mira_host_test|model_profile_endpoint_test)$' --output-on-failure
# TSAN 的 ctest 命令前加：setarch x86_64 -R
cmake --build --preset debug --target mirage-format-check mirage-boundary-check
```

Debug、ASAN、UBSAN、TSAN **各 12/12 通过，0 skip**：10 个业务门禁 + 2 个状态隔离
fixture；无 sanitizer 报告。各配置 Host/recovery Host/learning Host/model endpoint/
codec/golden/runtime service 分别为 **224/600/282/43/568/1856/731** 个检查、0 失败。
recovery service 因有界轮询的断言次数变化，四配置分别为 **120/122/122/128** 个检查、0 失败。两个 Python 门禁分别验证新增恢复 CLI 和既有候选文件导出/导入/重启链。
日志在 build/m15-service-PRESET-build.log、-test.log，各 CTest LastTest.log 保留检查计数。
格式检查通过；67 个公开头，0 新违规，DEC-096 两条既有 seam 保留。
最终 11 份变更 Markdown 的 629 个本地路径链接、单一一级标题/代码围栏核查通过；
与 HEAD 逐项比较 345 个既有 golden 向量完全不变，git diff --check 通过。

## 已验证行为

- 模型预留前无绑定导出；预留期间直接 dialog/harness 均拒绝、调用数为零；模型忙时
  即时拒绝预留，已接纳的 inference future 被消费；释放后可恢复预留，关闭后拒绝。
- 实际服务：缺失文件步骤进入 waiting_agent，显式恢复提议补丁，实际文件 atom 成功
  续跑并 completed；单独 lesson 返回稳定 ID/digest，再写幂等。修复与后续普通聊天
  共用同一 Provider，恢复调用无工具授权、Text、2048 输出 tokens。
- 聊天进行中恢复拒绝；恢复进行中聊天拒绝；同/不同 Run 的重入/并发拒绝不增加模型
  调用；结算后聊天正常。绑定前 model.set 可用，绑定后即使取消/结算也明确要求重启，
  带写 Key 请求在配置/凭据写入前拒绝。此项为产品预览契约，非无缝模型热更新保证。
- need_user、坏 JSON、provider 异常均安全结算，无修复动作及正文泄漏；失败 Run 不
  能写成功 Lesson。取消尝试、取消 Run 与关闭分别覆盖；关闭测试不释放脚本 gate，
  由真实取消上下文解除阻塞并排空。实际固定 30 秒期限结算 aborted，Run 仍 waiting_agent，
  没有修复动作或额外模型调用，后续同 Run start 拒绝。
- golden v27 新增 4 请求、3 响应、8 负向向量，原有所有 canonical 字符串逐项保持。
  codec 补 nil/错类型/缺字段/非法动作/ordinal/outcome/错误 ID/digest 等拒绝。
  CLI 检查响应类型及 Run/action 对应关系、旧服务拒绝、用法、坏载荷与 /dev/full 输出失败。

## 开发期失败与修正

首次构建因新变量遮蔽及测试接口/局部变量错误失败，修正后重建。首次独立服务夹具发布
因缺少发布门禁所需 DryRun policy 拒绝，补齐允许集合，没有放宽产品发布门禁。
首轮 6 项矩阵 **4/6**：关闭夹具重复调用一次性 run()，以及 CLI 未 flush 导致 /dev/full
误报成功；分别修正夹具生命周期和实际输出失败处理。扩充后的首轮 8 项矩阵 **7/8**：
超时断言误写 deferred-to-host，实际行为与既有 Host 契约一致为 aborted；按公开结算
语义修正断言，没有改超时实现。随后 8/8、最终四配置各 12/12 均通过。最终验收使用
最终代码重建的产物，不使用这些失败构建/旧 binary 作为证据。

## 限制与后续

M15 继续 In Progress。每 Run 一次、并发一次、跟踪 32、30 秒、thinking=none、0 决策
修复与 2048 tokens 为固定产品配置；首次绑定后切换模型须重启服务。资源预留在
start/status/cancel 或后续聊天消费结算时释放，无后台轮询；无客户端时已完成尝试最多
保留一个预留，服务关闭负责最终排空。

M15-08/09 的 native 呈现、自动恢复、真实应用变化/接管、收益和 Windows 实跑未执行；
M15-04/06 的长期视觉工件配额/删除/跨存储恢复仍未完成。负责人 Mirage 维护者，在相应
产品入口、平台与预算契约就绪后补跑。既有受控真实模型证据见
[长期元数据与在线验收](workflow-learning-20261114.md)，不能替代本轮产品入口在线实测。
