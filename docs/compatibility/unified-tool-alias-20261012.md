# 三方言模型工具名称适配（2026-10-12）

> 状态：In Progress（适配定向验收通过；整包与真实供应商门禁保持开放）
> 负责人：Mirage 维护者
> 工作项：M6-41
> 依据：[DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)、
> [供应商约束调研](../research/2026-10-12-provider-tool-name-contracts.md)

## 范围和行为

维护者在调研后明确要求开始适配。复用私有 Provider Adapter，对 pinned Mira 已支持的
Chat Completions、Anthropic Messages 和 Responses 三种方言统一应用
`[A-Za-z][A-Za-z0-9_]{0,63}` 模型侧名称。点号和连字符转为下划线；首字符不是
ASCII 字母时添加 `tool_`，上限包含此前缀。非法、超长和映射碰撞在网络调用前明确
拒绝，不截断或静默改名。该策略为多个入口的保守共同约束，不声称每家服务都有相同
的名称规则，也没有新增 Gemini、Bedrock 等原生协议。

工具声明、强制工具选择、assistant 工具回放及模型返回名称往返一致。返回调用必须
同时匹配本次声明的名称和 ToolId，否则以不可重试的 ProtocolViolation 拒绝，不形成
可执行提案。Registry、权限、执行与用户记录仍使用规范名称及原 ID。

Host 自有闭合工具列表、指令与工具说明同步使用模型侧名称；只替换独立的已声明工具
名 token。用户文本、历史正文、参数、结果、摘要及关联 call ID 不改写。不新增并发
路径，不修改 pinned 依赖或公开接口，不迁移用户配置。同步更新模拟 Provider，使其
按实际声明名称返回工具；实际 Registry 断言继续使用规范名称。

## 环境和可复现验证

Ubuntu 24.04.5 LTS / x86_64 / GCC 13.3.0，基于 `adfe65b` 的共享未提交工作树。
包含既有 DeepSeek 修复及同期 DEC-083 桌面视觉修改。构建采用仓库 native-release、
debug、asan、ubsan presets；源码和日志摘要见
[summary.json](data/unified-tool-alias-20261012/summary.json)。

```bash
cmake --build --preset native-release -j 3
./build/native-release/tests/tool_alias_provider_test
ctest --test-dir build/native-release --output-on-failure
cmake --build --preset debug --target tool_alias_provider_test native_agent_integration_test -j 3
ctest --test-dir build/debug -R '^(tool_alias_provider_test|native_agent_integration_test)$' --output-on-failure
cmake --build --preset asan --target tool_alias_provider_test native_agent_integration_test -j 3
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan -R '^(tool_alias_provider_test|native_agent_integration_test)$' --output-on-failure
cmake --build --preset ubsan --target tool_alias_provider_test native_agent_integration_test -j 3
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ubsan -R '^(tool_alias_provider_test|native_agent_integration_test)$' --output-on-failure
cmake --build --preset debug --target ppocr_pipeline_test x11_pointer_actions_test -j 3
ctest --test-dir build/debug -R '^(ppocr_pipeline_test|x11_pointer_actions_test)$' --output-on-failure
```

新增协议用例使用 pinned 三方言 mapper 的实际编码和响应解析，普通/stream 编码标志
各一组，共 121 项检查通过。覆盖声明/说明、强制选择、回放、返回解析、真实 Mira
resolve_tool_calls、ID 和正文不变、名称边界/碰撞、非法返回拒绝、错误/trace/重试提示
转发。stream 用例验证请求标志及最终响应解析，不构成在线 SSE 分片传输或 reducer
验收。七种 ModelLayer 配置经过真实 Mira loop/registry 两轮往返，检查模型侧名称与
用户可见规范名称、Host 列表与用户/历史文本边界。Debug、ASAN、UBSAN 定向各
4/4 通过（两个业务测试及 setup/cleanup）。修正漏用别名的 PP-OCR/X11 fixture 后，
Release 与 Debug 追加定向各 4/4 通过。

早期新测试 fixture 曾因枚举和 JSON API 使用错误编译失败，修正后 Release 又暴露
测试对缺省 stream 字段的空指针访问，补齐 null 检查后通过。首轮全量 73/78，包含
两个遗漏改名的模拟 Provider；修正后重新运行全量，保留先前失败日志，不把第一次
运行记为通过。

## 整包门禁与本机更新

最终 Native Release 全量 **75/78**，耗时 214.49 秒，失败为
`gtk_pointer_effects_test`、`gtk_pointer_scaled_test`、`desktop_image_supply_test`。
GTK 两项在直接 Registry 桌面动作的 stale_observation、拖动及动作后验证断言失败；
图像供给在 `shared_contracts("normal")` 的直接 Registry 阶段得到
`stale_or_invalid_target`，尚未进入 ModelLayer 测试。因此这些失败不构成模型名称
适配通过证据，也不能通过放宽断言关闭。初次 Debug 全量为 70/72，随后修正 X11
模拟 Provider 并定向通过，未另跑 Debug 全量。M6-41 整体验收保持 In Progress。
同期桌面稳定性和裁剪相关失败由 Mirage 维护者在对应实现/夹具一致后重建并补跑全量。

已执行以下本机刷新；目标均构建成功，desktop-file-validate 与 CLI 版本检查通过：

```bash
cmake --build --preset native-release --target mirage mirage-service mirage-tray mirage-native mirage-native-register-desktop -j 3
desktop-file-validate /home/linductor/.local/share/applications/org.mirage.native.desktop
./build/native-release/apps/mirage --version
git diff --check
```

桌面入口指向本仓库 `build/native-release`，下次从应用菜单启动会使用统一适配。
结束时没有运行中的用户 Mirage 进程，未自动启动产品或修改用户会话。整包门禁未
通过，本轮保留此前已验证的 DEB，其不包含本次适配。未提交、推送或修改 pinned 代码。

本轮未新增真实供应商调用、未重跑用户原任务。此前 HUA 受控对照仍见
[HUA 记录](hua-tool-alias-20261012.md)，不能将其扩大为全部供应商互操作保证。
Windows、每个实际供应商、在线 SSE 与用户原任务由 Mirage 维护者在具备对应环境和
凭据后补验。无新增并发路径，未另行运行 TSAN；已有生命周期测试由全量回归覆盖。
