# Mira Messages 思考修复 pin 审计

> 日期：2026-10-07
> 状态：Completed（Linux 集成工作树验证）
> 负责人：Mirage 维护者

旧 pin 7795e13cd6b8169f4936016c169702c2c60e876c → 新 pin
fbc644be2fabfaa2f8257e579fbc1d368537224c，来源保持
https://github.com/Linductor-alkaid/mira.git；[上游 PR#81](https://github.com/Linductor-alkaid/mira/pull/81)
基于 Messages PR#80，未合并。维护者已授权修复依赖并要求提交 PR。

新增规范 ThinkingMode/ThinkingPart、XHigh/Max effort 及 profile 声明；Messages 编码和有界
同步/SSE 解码支持 signed/redacted thinking，工具循环回填完整 assistant 内容。消费者需
同步重编译；模型能力策略由宿主声明，无协议自动回退。未引入 legacy budget_tokens、
between_tools、服务端工具或新的并发设施。当前主干 kairo 基础需上游合并前另行复验。

Mira AGPL-3.0 及 LICENSE 路径不变；Executor 2ae4fc8985af8962e08e3282a9330de5445d0d10、
mbedtls 068ff080b369adfac81509f9b57b2afabaf82dc5、sqlite 分发、其余 nested pins 均未变。
Mirador/EUI pin 与许可证不变；没有新增依赖包、二进制、网络下载或凭据。

Mira Debug canonical/schema/dialect/SSE/gateway/anthropic/conversation_loop 7/7 和 architecture
通过。组合 Mirage 会话思考/空恢复集成工作树下：Release 全量 52/52，ASAN/UBSAN 相关
各 3/3，TSAN native_agent_integration 208 checks（setarch -R），真实 MiniMax-M3 开关/预览
与 adaptive wait 工具循环成功。本 pin MR 与应用接入分开；上述组合验证不是 pin-only
分支独立测试或当前 kairo/Windows 验证。完整产品证据在后续会话接入 MR 中交付。

`dependencies.lock.json`、submodule gitlink 同步，构建的依赖锁校验通过。
