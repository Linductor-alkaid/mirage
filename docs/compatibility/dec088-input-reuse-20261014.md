# DEC-088 能力 1：输入复用身份缝消费与 Mirage 侧跨轮计数

> 日期：2026-10-14
> 工作项：DEC-088 Phase 1 Step 1.1.3；MIRA-20261014-005
> 负责人：Mirage 维护者
> 状态：自动化验证通过；真实任务对照实验待执行

上游 mira PR#95（维护者授权合并，merge `9e7b729d0861577b470d45385bdd08301bd1226d`，
上游 CI 24/24）交付了 ConversationLoop 的可选宿主身份缝。本记录覆盖 pin 消费与
Mirage 侧集成：

- `chore(deps)`：mira 子模块指针与 `dependencies.lock.json` 同步升至 9e7b729；
  配置门禁校验 gitlink == lock == checkout，`libmira_core.a` 含
  `mira::InputReuseMeta` 符号。
- `feat(integration)`：持久 `DesktopImageSource` 以内容 digest + 桌面 epoch 持有
  跨 dialog turn 的连续复用计数（互斥锁保护；相同像素重拍按 DEC-055 复用同一
  digest，计数延续；像素或 epoch 变化清零）。`DesktopImageTurn::input_reuse_identity`
  在与 `supply()` 相同的 owner/取消门控下解析，仅对本轮真正携带截图的请求计数；
  model_layer 的 `invoke_loop` 接线，无图配置回调返回 nullopt，请求装配保持字节不变。
  循环侧从第三次连续相同供给起在 User envelope 末尾渲染 Internal 权限的系统提示，
  `ModelInputSupplied` 事件与 `ConversationLoopResult::supplied_input_reuse_annotations`
  可观察。

执行结果（Debug 全新构建 + 独立验证代理复核，含 eb20108 基线对照）：

- `desktop_image_supply_test`：945 checks 0 失败（基线 910，+35 全部来自新增
  `input_reuse_identity()` 用例：单 capture 三次请求计数 0/1/2、跨 turn 相同内容
  重拍延续为 3、纯文本请求跳过、错 owner 返回 nullopt、换 ROI 内容重置为 0）；
  UBSAN（halt_on_error=1）同 945/0。
- `native_agent_integration_test` 406 checks 0 失败；`native_chat_model_test`
  412/0；`ipc_protocol_test` 469/0；`ipc_protocol_golden_test` 1552/0。
- pin 采纳使续写锚点措辞变化：`harness_desktop_tools_test` 两处旧断言
  （"Continue it exactly where it stopped"）更新为锚点措辞
  （`Continue from: "…"`，第二次续写锚点为最近响应文本），修正后 824 checks
  0 失败；基线上该测试通过、回归由 pin 采纳引入，属同步更新而非产品缺陷。
- `native_conversation_view_test` 26394 checks 0 失败（独立前端配置
  MIRAGE_ENABLE_NATIVE_FRONTEND=ON）。
- 四个改动源文件 clang-format --dry-run --Werror 干净。

未执行与已知限制：

- `portal_stream_retention_test` 在本机因缺 pipewire 头文件无法编译；在基线
  eb20108 同样复现，属环境/既有问题，与本研究无关（远端 CI 通过）。
- 真实桌面任务的对照验收（第 3 轮请求含复用提示、有/无标注思考 token 差异
  ≥10%）未运行；负责人：Mirage 维护者；补跑条件：真实会话与对照诊断。
- 能力 5（preflight 余量暴露与图片降级）未实施；能力 2/3（Phase 2）未实施；
  该缝为 run 调用方单线程路径，未单独跑 TSAN。
- 本记录不构成真实模型行为或性能承诺。
