# M7-04/05：桌面 PNG 与逐请求图片输入验收

> 状态：Completed（Linux / PNG / 三方言 fixture / MiniMax 合成截图范围）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-04/05](../plans/m7-visual-tools-and-input.md)
> 决策：[DEC-055](../decisions/DEC-055-desktop-image-supply.md)
> 工作树：`a722f38` 上的 `feat/m7-conversation-desktop-tools`，未提交；保留既有 M7-01/02/03 改动

## 交付范围

Screen Provider 的 BGRA8 帧经 Adapter 包装为标准 RGB8 PNG，记录 display/window/ROI
来源、全局原点和 1:1 变换。每次先授权 screen.capture；4 次/轮，raw/encoded 各
8 MiB、5 秒、仅最新一张。4K 整图明确拒绝，无静默缩放。标准 PNG 存入 Mira
MemoryArtifactStore；ArtifactSource 通过 kairo LatestMailbox 接收单轮 store，核验
身份、MIME、敏感级别、字节数与 digest。替换/结束时 erase，墓碑也受单轮次数限制。

Mira `ConversationLoop.set_model_input_supplier` 将当前敏感图和非可信来源文本作为
User ModelInputItem 加入下一请求，原生 ToolCall/ToolResult 对应关系保持。Guard 在
Provider 前核验 task/session/task_epoch、图像实际存在及年龄/环境 epoch；必需图片
缺失或供给 drop/degrade 时失败，不以纯文本继续冒充看图。过期/动作换代只供给重拍
提示。Provider 和 loop 异常收敛为安全失败；任务提交仍由既有 Executor 管理。

模型设置增加严格布尔 supports_images，默认 false；model.get/set、目录/持久化、
启动/重装和原生逐模型“识图”开关贯通。只有显式启用才注册显示器枚举与截图，
read_only 的截图仍经过权限。普通过程只保留元数据，不存像素/base64 或密钥。

## 环境与依赖

Linux x86_64、GCC 13.3.0、C++20；native-debug/release 含 EUI 原生前端和私有 Xvfb。
ASAN 构建同时启用 address,undefined，`detect_leaks=0`，不声明 LeakSanitizer 验收。
TSAN 在 `setarch x86_64 -R` 下运行，不以默认 ASLR 映射失败作为产品竞态证据。
Mira 固定 `698fc77c151fa93b1b52ea4dd25cb95a3a60bee5`；嵌套 kairo/mbedtls、Mirador、
EUI pin 不变，见[授权升级审计](../supply-chain/mira-image-supply-20261009.md)。

## 最终验证

| 验证 | 结果 | 证明范围 |
| --- | --- | --- |
| native-debug 全量 | 55/55，无 skip | 当前工作树完整回归 |
| native-release 全量 | 55/55，无 skip | 当前工作树完整回归 |
| 联合 ASAN/UBSAN | 5/5 | 图片、harness、Runtime 集成、完整动作、持久化 |
| TSAN 图片 fixture | 112 checks，通过、无竞态报告 | 新 source/供给与生命周期路径 |
| 独立 PNG 回读 | 30,000 像素通过 | RGB、padding、CRC/Adler、多 DEFLATE 块；未调用生产编码逻辑解码 |
| 公共头/格式 | 53 headers、0 violations；格式通过 | 私有 pinned 类型未泄漏 |
| 原生 renderer | 21,605 checks、0 failures | 开关命中、保存、活动模型与目录保持，浅/深色渲染 |
| Impeccable detector | 0 findings | 原生设置页局部改动；不作 web 特性保证 |
| MiniMax-M3 在线 | 2/2 | 真实 Screenshot tool → ImagePart → Messages Provider → 识图回答 |

精确命令（在仓库根执行，renderer 的工作目录另列）：

```bash
cmake --build --preset native-debug -j3
ctest --preset native-debug
cmake --build --preset native-release -j3
ctest --preset native-release
cmake --build --preset asan --target desktop_image_supply_test harness_desktop_tools_test native_agent_integration_test action_contract_test persistence_test -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan -R '^(desktop_image_supply|harness_desktop_tools|native_agent_integration|action_contract|persistence)_test$'
cmake --build --preset tsan --target desktop_image_supply_test -j2
setarch x86_64 -R build/tsan/tests/desktop_image_supply_test
python3 tests/manual/desktop_png_decode_check.py build/native-debug/tests/desktop-image-fixture.png
cmake --build --preset native-debug --target mirage-boundary-check mirage-format-check
mkdir -p /tmp/mirage-image-settings-render-20261009
(cd build/native-release/apps/native && /home/linductor/mirage/build/native-release/tests/native_conversation_view_test /tmp/mirage-image-settings-render-20261009)
/home/linductor/.agents/skills/impeccable/scripts/impeccable detect --json apps/native/app.cpp
```

`desktop_image_supply_test` 覆盖三个方言的真实 canonical ImagePart 和 wire 编码、
显示器发现、window/ROI/负原点、显式未缩放、拒绝前未采集、4K/次数超限、重复同图
替换、5.1 秒退役、owner/task_epoch/旧图/缺图、取消、晚到/几何/epoch 变化和 handler/
Provider 异常。关闭后 submit_auto 拒绝且零截图；同步入口只用于已经获准的 caller。
现有 harness/action 套件继续覆盖运行中取消、超时、释放、拒绝与 Runtime shutdown。

## 在线证据与失败记录

使用配置 MiniMax-M3 / `https://api.minimaxi.com/v1`，Messages 方言、adaptive reasoning。
由真实 ModelLayer 生产 Provider/SocketHttpTransport 执行，未使用 Provider override。
Fake Screen Provider 仅返回公开合成像素，不上传用户桌面。512×256 白底、顶部随机
六位字符、底部有色矩形；答案不在 prompt 中，模型须实际调用 capture 后回答。

| 样本 | 图中内容 | 实际回答 | captures / tool_calls / model_steps | supplied / dropped / degraded |
| --- | --- | --- | --- | --- |
| 1 | 4LNYDY / blue | 4LNYDY / blue | 1 / 1 / 2 | 1 / 0 / 0 |
| 2 | 5VAK0L / green | 5VAK0L / green | 1 / 1 / 2 | 1 / 0 / 0 |

本机原始合成图与脱敏结果在 `/tmp/mirage-desktop-images-20261009-adaptive`；复跑生成
新的随机样本，使用现有受控凭据文件，密钥只进入子进程环境，不打印或写入证据：

```bash
python3 tests/manual/desktop_image_supply_acceptance.py --provider /home/linductor/mira/docs/model_provider/minimax --probe build/native-debug/tests/desktop_image_supply_test --output /tmp/mirage-desktop-images-acceptance
```

历史失败保留：首次默认 disabled reasoning/英文指令未触发工具，回答空代码，验收
失败（`/tmp/mirage-desktop-images-20261009`）；中文指令重试遭 TLS handshake 失败、
0 截图（`/tmp/mirage-desktop-images-20261009-retry`）。改为 adaptive 后两例通过，
不宣称任意思考设置都会调工具。在线成功后追加了 Guard 身份字段、异常收敛与显示器
发现，最终 fixture/sanitizer 全部复跑；没有为这些补强重复付费在线请求。

本地迭代：追加 Provider 异常 fixture 时发现异常可穿过网关，debug 全量曾 54/55，
补齐安全异常边界后最终 55/55。初版关闭测试绕过 Executor 直接调用同步入口，不能
检验准入；修正为 shutdown 后 submit_auto/future 拒绝，最终零截图。不删除这些失败
记录，也不把第一次 Release 或旧 sanitizer 结果当作最终工作树证据。

## 限制与后续

完成范围为 Linux/PNG、三方言离线 fixture 和一个明确配置的 MiniMax 在线互操作。
JPEG、真实用户桌面的模型识图、在线 Responses/Chat 厂商、Windows/Wayland 端到端
没有本轮证据。负责人 Mirage 维护者：目标平台/授权与厂商配置就绪后按 M7-10/兼容性
矩阵补跑；不扩大源码支持声明。X11 采集及取消能力沿用既有组件验收，不构成桌面
业务操作成功证明。Xlib 调用的协作式取消限制仍在。

M7-08 继续负责强制后观察及业务证据；M7-06/07 负责真实 OCR 和 SoM/视觉引用。
M7 整体仍 In Progress。`MIRA-20261009-001` 在 Mirage 台账收口 Resolved（本范围），
不修改上游 issue 状态；没有新增第三方源码补丁或并发设施。工作树未提交、未创建 MR。
