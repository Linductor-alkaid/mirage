# 原生过渡与模型目录验收

> 日期：2026-10-08
> 状态：Completed（Linux 适用范围）
> 工作项：M6-33
> 依据：DEC-051
> 负责人：Codex；平台补验负责人：Mirage 维护者
> 版本：本功能分支工作树，pinned 依赖未改动

## 范围与环境

Linux x86_64，kernel 7.0.0-34-generic，Ubuntu GCC 13.3.0，CMake 3.28.3；
原生OpenGL渲染使用测试启动的X11显示服务。界面参考本机ZCode `29628c9` 的侧栏
width/transform过渡；目录交互与端点兼容参考cc-switch `5ae6ad38` 的
`ModelInputWithFetch.tsx`、`model_fetch.rs`，仅复用行为思路。

实现220ms侧栏宽度/位移与240ms主题色插值，反向切换从当前状态继续；减少动态效果
直接到达终态。思考开关保存所选OpenAI兼容模型的声明，Anthropic模型继续由服务投影
已核验选项。目录候选需显式选择和保存；不自动覆盖已配置模型。

## 验证结果

| 门禁 | 实际结果 |
| --- | --- |
| native-release完整构建 | 通过，warnings-as-errors |
| Release全量CTest | 52/52通过，16.22秒 |
| IPC golden v14 | 通过；目录请求含/不含草稿Key、目录响应、非法结构 |
| ASAN：IPC golden / native agent | 2/2通过，detect_leaks=1，无诊断 |
| TSAN：同两项 | 2/2通过，无诊断，25.91秒 |
| 原生渲染/交互测试 | 21412 checks，0失败 |
| 渲染自研编译单元ASAN+UBSAN | 21412 checks，0失败；ASAN leak关闭、UBSAN vptr关闭 |
| OpenRouter公开目录真实IPC请求 | 通过；使用synthetic Key，无推理调用；结果非空且最多256个ID |
| 格式/公开头边界/diff | 通过；52个公开头，0违规 |
| Impeccable机械检测 | changed UI targets，返回空结果 |

服务集成覆盖缺失匹配凭据拒绝、write-only Key编解码、两种目录格式、去重和无效响应；
新增目录任务已启动后协作取消、transport worker回收以及Executor关闭后拒绝。
现有异常、准入、超时、IPC队列拒绝、shutdown和会话控制回归通过。
模型编辑的实际开关点击、所选模型保存、其他模型设置隔离由原生交互测试验证。

完整渲染覆盖正常1180×800/最小860×620、明暗、收起/展开、设置页；本轮目检模型页和
最小深色外观页，无文字/控件重叠。MotionValue以可控时间验证中间值、反向连续和终态；
没有将该结果声明为物理显示器帧率基准。

## 复跑

```bash
cmake --build build/native-release -j4
ctest --test-dir build/native-release --output-on-failure -j4
cmake --build build/native-release --target mirage-format-check mirage-boundary-check
cmake --build build/asan --target native_agent_integration_test ipc_protocol_golden_test -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/asan -R 'native_agent_integration_test|ipc_protocol_golden_test' --output-on-failure
cmake --build build/tsan --target native_agent_integration_test ipc_protocol_golden_test -j4
ctest --test-dir build/tsan -R 'native_agent_integration_test|ipc_protocol_golden_test' --output-on-failure
MIRAGE_CATALOG_SMOKE=1 ctest --test-dir build/native-release -R native_agent_integration_test --output-on-failure
# 原生截图：从 build/native-release/apps/native 运行
../../tests/native_conversation_view_test /tmp/mirage-m6-33-final
```

渲染ASAN+UBSAN依照既有M6-29/30方法：从native-release的compile_commands重编渲染测试
全部五个自研编译单元（测试包含app.cpp），加`-fsanitize=address,undefined
-fno-sanitize=vptr -fno-sanitize-recover=all`，链接既有Release依赖；完整依赖插桩不在
该项声明范围。辅助脚本`/tmp/mirage-motion-sanitizers.py`与日志
`/tmp/mirage-motion-sanitizers.log`仅保留本机临时目录，非交付依赖。
截图`/tmp/mirage-m6-33-final/*.ppm`仅含合成测试数据，可通过上述命令重建。

## 失败记录与修复

早期双worker池未保持普通IPC提交顺序，会话burst回归出现12项失败；改为Executor
SerialExecutionContext后复验通过。第一次真实目录请求发现TLS factory未初始化，已改为
调用公开initialize加载系统信任库，真实目录复验通过。新增golden目录响应夹具起初漏掉
value包装导致测试失败，修正夹具后golden及52项全量回归通过。早期256KiB目录预算不足以
容纳OpenRouter约778KiB响应，现为2MiB；最多256个ID仍为明确产品预算。

## 限制与补跑条件

- Windows编译和原生交互未在本机执行：维护者在Windows native preset/真机复跑。
- Linux物理高DPI、原生Wayland与真实显示器帧耗时未测：维护者在对应显示环境复跑；
  本项不承诺刷新率或跨平台性能。
- Anthropic厂商真实鉴权目录未执行：维护者提供该服务支持的公开目录与授权Key后运行
  model.list；现有两种目录格式与方言header实现有源码/解析测试证据。
- 供应商不提供兼容端点时返回可读错误，可继续手填。Mira私网端点保护仍适用。
- 未宣称全部第三方图形依赖的LSAN/UBSAN vptr覆盖；补跑需全Debug/RTTI依赖构建。

[原生设计](../design/native-agent-frontend.md)、[IPC契约](../design/mirage-ipc-protocol-v1.md)、
[决策](../decisions/DEC-051-native-motion-and-model-discovery.md)。


补充复核：目录响应按编辑revision独立处理，避免revision与会话ID相同时，失败响应误清
会话提交/删除状态。新增失败与迟到响应隔离、候选不自动保存、实际点击添加候选测试，
原生渲染回归通过；该修复保持页面布局和wire契约。
