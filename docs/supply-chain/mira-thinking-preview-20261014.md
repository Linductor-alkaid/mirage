# Mira 升级审计：实时思考预览（2026-10-14）

> 状态：Completed（公开 API 与本地消费 fixture 范围）
> 授权：维护者要求独立 worktree 修 Mira、提 PR 后完成实时气泡。
> 关联：M8-11、DEC-089、MIRA-20261014-004。

## 来源与锁定

旧 pin 为 `1befa451713e0eb635da6077410e284f7b8ea88b`，新 pin 为
`ea720a6122320a0afac9316acb0ca05838051972`，来自
[Mira PR#94](https://github.com/Linductor-alkaid/mira/pull/94) 的已推送 head。
PR 开放待评审，消费 head 不代表合并；gitlink 和 dependencies.lock.json 同步。
Mira、嵌套 kairo/mbedtls、Mirador、EUI 许可证与嵌套 pin 不变，无新增依赖。

## 差异和承载边界

新增独立 opt-in `UnvalidatedModelThinkingPreview` / `ModelThinkingPreviewSink`；
Gateway/Provider infer options 转发，正文 sink 的语义和 callback 次数不变。
Chat reasoning_content、Messages thinking_delta、Responses reasoning_text.delta
提供最多 16KiB 的最新 UTF-8 完整快照。新尝试先发送空快照；取消停止投递；
截断/drop 可观察，redacted、签名和 Responses summary-only 不暴露。
预览仍未经验证，不写进规范历史，不作为工具指令或任务成功证据。

Mirage ModelLayer 转发公开 sink，运输回调仅有界校验并投递 LatestMailbox；Runtime
串行域压缩与选择，UI 消费 IPC 完整状态。没有自研 SSE 解析或第二套并发设施。

## 实际验证

Mira Debug 全量 106/106；专项 Debug 5/5、ASAN 3/3（detect_leaks=0）、UBSAN 3/3；
format/docs/SBOM/platform-boundary/architecture 本地全部通过。CI 状态以 PR 为准。
Mirage 消费验证及未执行平台/供应商门禁见
[Runtime 与原生气泡证据](../compatibility/mira-pet-bubbles-runtime-20261014.md)。
