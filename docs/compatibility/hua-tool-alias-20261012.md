# HUA Chat Completions 工具名拒绝修复（2026-10-12）

> 状态：In Progress（HUA 修复与本机更新已验证；共享工作树的全量视觉回归未通过）
> 负责人：Mirage 维护者
> 工作项：M6-41
> 依据：[DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)

## 失败与根因

维护者要求检查最新 Mirage 会话并修复。最新会话 `ca6174e7…` 首轮失败，记录
`model call failed: provider rejected the request as invalid (provider code: invalid_request_error)`。
当前模型为 HUA / `gpt-6.1-sol`，`https://huablog.org/v1`，方言为
`openai.chat-completions.v1`。检查仅提取错误、时间及必要配置，不版本化用户正文和凭据。

使用系统钥匙环中的既有 HUA 凭据发出公开合成请求：纯文本 HTTP 200，工具声明名
`desktop.window.list` HTTP 400 / `invalid_request_error` / `Upstream request failed`，
唯一改成 `desktop_window_list` 后 HTTP 200。同步和流式（minimal reasoning）各一轮
对照均得到相同结果。最初 urllib 客户端被边缘返回 403 / 1010，正式对照使用 curl。
Key 仅在诊断进程内存与 curl stdin 中传递，未打印、落盘或放入命令参数。

既有名称适配只对官方 DeepSeek Messages 启用；HUA 请求直接发送 Mirage 的点号
名称。因此失败归属 Provider Adapter 的名称兼容，与模型列表、认证或桌面动作无关。

## 修复与验证

复用既有适配器并改名为私有 `tool_alias_provider.*`。Chat Completions 配置统一使用
名称往返映射，不绑定 HUA 主机地址；其他 Messages 和 Responses 保持原选择。
规范名称及 ToolId 留在 Mira Registry/ConversationLoop 中，发往服务端的声明和
assistant 回放使用别名，返回调用按别名与原 ID 还原。冲突、非法/超长名称和回放
冲突在调用 Provider 前拒绝，不放宽工具解析和权限边界。无 pinned 代码修改。

新增五种 ModelLayer 配置回归：HUA Chat、其他 Chat、官方 DeepSeek Messages、
其他 Messages、HUA Responses。每种均经真实 Mira loop/registry 执行一次合成
`desktop.window.list` 并完成第二轮，检查服务端名称、assistant 回放、工具结果和
原始用户可见名称。补验非法名称、64 字符上限及回放碰撞拒绝。
Chat 图片供给 fixture 同步使用服务端别名，最终记录仍使用规范名称。

环境：Ubuntu 24.04.5 LTS / x86_64 / GCC 13.3.0；工作树基于 `adfe65b`，包含
此前 DeepSeek 修复和同期未提交的目标稳定性修改。证据摘要记录本轮源文件指纹。

```bash
cmake --build --preset native-release --target native_agent_integration_test desktop_image_supply_test mirage mirage-service mirage-tray mirage-native -j 4
ctest --test-dir build/native-release -R '^(native_agent_integration_test|desktop_image_supply_test)$' --output-on-failure
cmake --build --preset debug --target native_agent_integration_test -j 2
ctest --test-dir build/debug -R '^native_agent_integration_test$' --output-on-failure
cmake --build --preset asan --target native_agent_integration_test -j 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan -R '^native_agent_integration_test$' --output-on-failure
cmake --build --preset ubsan --target native_agent_integration_test -j 2
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ubsan -R '^native_agent_integration_test$' --output-on-failure
cmake --build --preset native-release -j 2
ctest --test-dir build/native-release --output-on-failure
```

`native_agent_integration_test` 在 Debug、Native Release、ASAN、UBSAN 均通过；
Debug/ASAN/UBSAN 各含 setup/cleanup 为 3/3。最初测试 fixture 编译因漏填
`annotations_digest` 被 Werror 拒绝，补齐后通过，不能将第一次构建记为成功。

真实 HUA 对照还强制调用 `desktop_window_list`，服务端 HTTP 200，返回同名
tool call；回填合成空窗口结果并回放 assistant 后 HTTP 200 / `stop`。未发送用户
窗口数据。隔离实际 `mirage-service` 的两轮 minimal 合成请求均 `ok`，原用户
session-state SHA 保持不变；该服务测试中模型没有执行所请求的 wait，不能宣称
产品端实际工具往返。另一个默认思考合成请求曾达到 60 秒阶段超时，后续请求成功；
合成文件读取请求出现 20 秒阶段超时。网络稳定性不由本次名称修复承诺。

## 全量限制与本机交付

Native Release 全量 71/76，通过的 HUA 路径与失败项分开记录。失败为
`gtk_pointer_effects_test`、`gtk_pointer_scaled_test`、`local_visual_workflow_test`、
`desktop_image_supply_test`、`visual_marks_test`。前三类视觉/区域重验断言在同期
DEC-083 目标稳定性修改后不再满足；两个 GTK 用例单独重跑仍失败，具体包括
`stale_observation` 拒绝及动作后验证断言。本轮未修改这些稳定性实现或放宽断言。
负责人 Mirage 维护者；补跑条件为完成同期目标稳定性实现/测试，再重建并跑全量。

已执行
`cmake --build --preset native-release --target mirage mirage-service mirage-tray mirage-native mirage-native-register-desktop -j 2`，
刷新本机四程序、资源和桌面入口，入口校验通过。结束时无用户 Mirage 进程，后续
从应用菜单打开即可加载修复。用户原失败任务未自动重跑；未改模型配置/思考默认值、
用户历史或凭据，未提交/推送。由于全量门禁未通过，本轮没有覆盖此前已验证 DEB；
该包仍是上轮版本，HUA 修复交付于本机 Native Release 构建。

结果与源文件/程序/测试日志摘要见 [summary.json](data/hua-tool-alias-20261012/summary.json)。
