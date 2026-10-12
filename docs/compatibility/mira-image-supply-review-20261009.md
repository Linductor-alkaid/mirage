# Mira 图片供给入口更新核对

> 状态：Completed（上游核对与 Linux pin 回归；图片端到端待验）
> 日期：2026-10-09
> 负责人：Mirage 维护者
> 工作项：[M7-05](../plans/m7-visual-tools-and-input.md)，[MIRA-20261009-001](../dependency_feedback/mira.md#mira-20261009-001)

## 更新事实

首次核对时 Mirage pin 为 bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5。上游
[PR #86](https://github.com/Linductor-alkaid/mira/pull/86) 已合并为 d5e6673，最新
master 为 [698fc77c151fa93b1b52ea4dd25cb95a3a60bee5](https://github.com/Linductor-alkaid/mira/commit/698fc77c151fa93b1b52ea4dd25cb95a3a60bee5)
（合并后仅补 CI 文档）。此次差异为八个文件：ConversationLoop 公共头/实现、一个
新测试、测试注册及 API/决策/计划文档；没有其他模型方言、依赖、许可证或构建能力变化。
kairo 仍 ef821dcc、mbedtls 仍 068ff080，Mirador/EUI 不在本次变化中。

已回读 GitHub PR merged/merge SHA、master commits、issue #85 与两次 CI jobs。
[pull_request CI](https://github.com/Linductor-alkaid/mira/actions/runs/37876824953) 和
[push CI](https://github.com/Linductor-alkaid/mira/actions/runs/37876820201) 各 12/12 jobs
success，包含 Linux GCC/Clang、Windows、Android、ASAN/UBSAN/TSAN 和 quality。
上游记录 Linux 105/105；该计数为上游证据，没有在 Mirage 宿主重新执行。#85 仍 Open，
上游明确将真实识图/下游消费留给 Mirage，不能因 PR 合并将本地反馈记为 Resolved。

## 公开契约与宿主责任

- set_model_input_supplier(ModelInputSupplier, ModelInputSupplyOptions) 在 run 前接线；
  每次请求装配前同步调用一次，工具结果后/下轮推理前可供给标准 ImagePart。
- 默认每请求 8 items / 8 MiB；只接受 User 的 Text/Image/File，禁止 Secret 图片和
  工具/思考 part。供给内容不进入 tool-history 回放，重试重新调用 supplier。
- 供给错误/异常及非法选项降级，不阻塞纯文本 loop；超预算条目丢弃并计数。
  ModelInputSupplied/ModelInputSupplyDegraded 和结果中的 supplied_input_items、
  supplied_input_dropped、supply_degradations 是验收事实源。
- Task/session/epoch 的正确性由宿主 callback 核验；callback 必须有界、不阻塞。
  上游不提供截图、编码、权限、坐标变换或宿主图片保留策略。
- 三方言复用既有标准图片映射；Messages 仅接受 ImageDetail::Auto。无 image_input
  能力的 profile 显式失败，不能从 hook 存在推导模型已识图。

当前 ModelLayer 的 production provider/gateway artifacts 参数均为 nullptr，配置尚未
声明识图能力。仅升级 pin 不会自动打通图片：M7-04 必须先发布可解码的标准编码 artifact，
M7-05 注入 source、能力和 supplier。所需图被丢弃/供给降级时，Mirage 必须显示未看图/
失败或未验证，不把继续回答当成图片已消费。保留同图以供重试，淘汰旧图和迟到帧，不能
在第一次 callback 中无条件清空。上述策略属于 Adapter/Runtime，无须私有模型循环。

## 本机 API 探针

Linux x86_64 / GCC 13.3.0，原 pin 的负向探针和新 master 的正向探针均编译退出 0。
首次核对的新头由 git archive 导出到临时目录，只核对类型/签名；当时没有 checkout、修改第三方源码
或更新 gitlink/lock，也没有执行模型/桌面动作。复现如下：

```bash
snapshot_dir=$(mktemp -d /tmp/mirage-mira-image-api-XXXXXX)
mkdir "$snapshot_dir/old"
git -C third_party/mira archive bfcb8e77f8092b14a73cf4f1951f7d6123b5f9d5 include > "$snapshot_dir/old.tar"
tar -xf "$snapshot_dir/old.tar" -C "$snapshot_dir/old"
git -C third_party/mira archive 698fc77c151fa93b1b52ea4dd25cb95a3a60bee5 include > "$snapshot_dir/include.tar"
tar -xf "$snapshot_dir/include.tar" -C "$snapshot_dir"
c++ -std=c++20 -fsyntax-only -I"$snapshot_dir/old/include" -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
c++ -std=c++20 -fsyntax-only -DMIRAGE_EXPECT_MODEL_INPUT_SUPPLIER=1 -I"$snapshot_dir/include" -Ithird_party/mira/third_party/kairo/include docs/dependency_feedback/repro/conversation-image-api.cpp
```

## 消费与验收安排

已授权升级至 698fc77，gitlink/lock、供应链审计同步，现有结构化/纯文本回归通过。
后续实现 M7-04 → M7-05 的合成 artifact/下一轮 ImagePart 与
三方言 fixtures、drop/degrade/取消/迟到/身份失配。最后使用已声明识图能力的真实模型
取证。M7-08 的业务验证继续独立推进，M7-06/07 的 OCR/SoM 不由此次上游更新代替。
首次核对时尚未获得依赖升级授权，pin 保持 bfcb8e7；随后维护者明确授权本次升级，
已同步到 698fc77。图片 API 上游缺口已经交付，本地条目 Accepted，
完整下游验收后才能 Resolved。

## 授权升级记录

维护者于本会话明确授权依赖升级，Mira gitlink/lock 已更新至 698fc77；实际 pinned
公共头的正向 API 探针编译通过。构建、回归和平台边界见
[升级审计](../supply-chain/mira-image-supply-20261009.md)。M7-04/05 的图片接线尚未实施。

授权升级最终验证：native-debug/release 各 54/54（无 skip），联合 ASAN/UBSAN 6/6，
TSAN action_contract/harness 各 55/422 checks、0 failures、无竞态报告；上游图片供给
fixture 1/1、53 个公共头边界和格式检查通过。MIRA-20261009-001 继续 Accepted。

2026-10-09 后续消费完成：M7-04/05 已接标准 PNG、ArtifactSource 与每请求 ImagePart，
三方言 fixture、MiniMax 实际截图工具在线两例通过。本地反馈改 Resolved（Linux 合成图
范围），最终回归与迭代失败见[下游验收](desktop-image-supply-20261009.md)。前述
“尚未接线”仅描述上游更新/依赖升级阶段，不改写历史证据。
