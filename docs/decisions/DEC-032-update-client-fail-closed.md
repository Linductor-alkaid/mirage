# DEC-032：应用内更新器核心与更新通道 fail-closed 行为

> 状态：Accepted
> 日期：2026-09-30
> 负责人：Mirage 维护者
> 冻结里程碑：M5（`M5-12` 落地）
> 替代/被替代：无（兑现 shell-binary-locking §3 "清单格式与验签失败的
> fail-closed 行为在 M5 实现时补决策细节"）

## 背景与问题

`M5-12` 要求 Windows 应用内更新器：更新清单（版本 / URL / SHA-256 /
ed25519 签名）验签 fail closed + 原子切换与回滚。M5-11 已交付清单生产/
验签工具链与演练（packaging/update，ed25519/openssl）。未决细节（
shell-binary-locking §3 指定 M5 实现时补决策）：

1. 更新器代码归属与平台边界；
2. 传输面（是否 TLS）与信任锚；
3. 验签失败的精确 fail-closed 行为；
4. 原子切换与回滚的落盘协议；
5. "凭据经系统 keyring" 对更新器的具体含义；
6. 差分策略（DEC-006 决策 5 已定全量优先，本记录不翻案）。

## 决策

1. **代码归属 = `runtime/update`（Mirage::update 静态库）**：更新器核心
   （清单解析 / ed25519 验签 / fetch / 原子 apply）为纯 std + libcrypto，
   POSIX 与 WinSock fetch 双端口、平台边界收敛在模块内（runtime/ipc 的
   stream_posix/windows 先例同型）；CLI `mirage update check/apply` 为
   驱动入口；托盘 / 桌面壳的应用内触发面接同一核心（后续接线点）。
2. **清单 = 严格解码，全字段 fail closed**：schema 必为
   "mirage-update-manifest"、schema_version 必为 1、version/timestamp 非
   空、files 1..64 条且 name/size/sha256 全字段齐全、重复名拒绝、字节
   预算封顶（RULE-07）。任何偏离整体拒绝，无部分接受。
3. **信任锚与传输 = ed25519 清单签名 + Authenticode 双层，传输明文
   HTTP 亦可**：fetch 为最小 HTTP/1.1 GET（无 TLS、3xx 不跟随、超预算
   fail closed）。理由：通道的信任锚是清单 ed25519 签名（bit 级篡改必
   被拒）+ 安装包 Authenticode（第二层），TLS 仅是传输加密而非本通道的
   信任机制；部署侧可自行 HTTPS 前置。私钥（清单 ed25519 与
   Authenticode）只在发布机 keyring——**更新器客户端零私有凭据**：
   信任锚是随包分发的 32 字节公钥（非机密），"凭据经系统 keyring" 对
   更新器无私有凭据需求，如实声明。
4. **验签 fail closed 精确行为**：ed25519 验签（libcrypto EVP）失败 /
   签名长度非 64 / 本构建无 crypto 面（OpenSSL 缺席）→ 一律拒绝并终止
   更新（"ed25519 unavailable" 同样 fail closed，不静默降级）；清单
   schema 门在**验签之后**（先证签再校验语义，归因清晰）；逐文件
   sha256/size 复核任一失败即整体失败。
5. **原子切换与回滚 = staged 预检 + 备份 + aside/rename + 逆序回滚**：
   - 预检（触碰前）：staged 每文件存在、size 与 sha256 与清单一致，
     任一不符整体拒绝（staging 损坏永不产生部分切换）；
   - 切换：逐文件 备份旧内容到 `.mirage-update-backup/` → aside 写入 →
     rename 原子替换；全部成功才删除备份目录；
   - 任一步失败：已切换文件按逆序从备份恢复，目标目录收敛于旧状态
     （全部旧或全部新，不混合）。
6. **差分 = 不承诺**：维持 DEC-006 决策 5（全量优先，差分按实测评估），
   本更新器只消费全量安装包。

## 备选方案

- **复用 pinned TLS 传输做 HTTPS fetch**：否决。pinned openssl 适配器仅
  UNIX 构建（MIRA_WITH_OPENSSL 门），Windows 更新器反而无传输；且通道
  信任锚非 TLS（见决策 3），明文 HTTP + 双层签名已满足。部署侧可自行
  HTTPS 前置。
- **更新器并入 Runtime Service**：否决。更新需替换安装目录内的二进制
  （含 service 自身），由被更新对象自更新存在自替换竞态；独立更新器
  面（CLI/托盘触发）与被更新服务解耦。
- **MSI/MSIX**：DEC-031 已定 NSIS，不翻案。

## 影响与风险

- `runtime/update` 新库（四公共头纯 std，RULE-01；crypto 面依赖
  libcrypto）；apps/cli 新增 `mirage update` 命令组。
- Windows 更新器构建需 OpenSSL Crypto 运行时随包（M5-11 安装器补跑时
  纳入载荷）；无 OpenSSL 构建下更新器整体 fail closed（拒绝一切签名）
  ——不静默降级，如实声明。
- 更新通道为明文 HTTP 时内容可被观测（非机密载荷）；篡改被双层签名
  拒绝，如实声明。
- HTTP GET 无重定向跟随（3xx fail closed）：更新 URL 必须直连最终地址
  （CDN 直链满足），如实声明。

## 验证方式

- update_core_test（单元）：清单 fail-closed 解码矩阵（含路径安全门：
  name/version 拒绝路径分隔符与 `..`/`.`）；ed25519 round-trip + 篡改
  拒；原子切换 + 损坏 staging 拒（目标不触碰）+ 回滚收敛（混合状态不
  残留）。
- 本地全量更新演练（真实 HTTP）：check 报告通道版本；apply 落位；尺寸
  超预算拒；同尺寸篡改 sha256 门拒（目标零写入）。
- Windows 运行级演练：真实安装器 payload + 重启换新 + 回滚——维护者
  机器补跑（DEC-017 证据分级）。

## 修订记录

### 2026-09-30：M5-12 第 2 轮独立验证修复与行为澄清

- **回滚不变量精化**：原实现仅回滚"apply 前已存在"的文件；清单**新增**
  文件在某次切换失败后残留于目标目录，违反"要么全新要么全旧"。修复：
  apply 记录本次新增（无备份）文件，回滚时逆序删除之（先删新增、再还
  原备份），目标目录收敛不变量成立。
- **路径安全门（纵深防御）**：decode 拒绝 name/version 含 `/`、`\` 或
  整值为 `..`/`.`——staging/target 拼接以 path-safe 值为前提（签名密钥
  泄露场景下的路径逃逸防线；第 2 轮观察 B）。
- **IPv6 字面量不支持（如实声明）**：HTTP fetch 的 host 解析仅处理
  `host[:port]` 单冒号形态；DEC-006 决策 5 通道为 IP:port 直连前提下可
  接受，IPv6 字面量（`[::1]:port`）暂不支持——第 2 轮观察 D 留痕，
  需要时凭证据扩展。

## 关联文档和工作项

- [DEC-006](DEC-006-ui-web-frontend-packaging.md)（决策 5）、
  [shell-binary-locking](../supply-chain/shell-binary-locking.md) §3
  （本记录兑现其"补决策细节"指定）、
  [DEC-017](DEC-017-windows-backend-toolchain-and-event-loop.md)（双工具
  链与证据分级）、DEC-031（安装器）。
- 工作项：[M5 计划](../plans/m5-desktop-product.md) `M5-12`。
