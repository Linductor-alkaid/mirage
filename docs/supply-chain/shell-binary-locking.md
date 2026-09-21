# 壳二进制与前端依赖的锁定、SBOM 与更新通道机制复核（DEC-006 M3-06）

> 状态：Evidence（M3-06 机制复核完成；产品级落地随 M5）
> 日期：2026-09-21
> 关联：[DEC-006](../decisions/DEC-006-ui-web-frontend-packaging.md)（决策 3/5/6 冻结）、
> [壳 PoC 基线](../benchmarks/shell-poc-baselines.md)、
> [依赖升级审计](dependency-upgrade-audit.md)、工程规范 §9.1/§10.5/§11

## 1. 现状与缺口

现有锁定机制（工程规范 §9.1）覆盖 **git submodule** 形态：`dependencies.lock.json`
登记 source / commit / license，configure 期由 `cmake/MirageDependencies.cmake`
校验。壳（CEF / Electron）二进制与 npm 前端依赖是 **非 submodule 工件**，存在
两类缺口：

1. 二进制来源（CEF 发行包、Electron 预编译二进制）无 commit 可 pin，必须以
   **版本 + URL + 完整性摘要**锚定；
2. npm 依赖树（前端工具链、框架运行时）需要 npm 自己的锁文件 + 完整性摘要
   （`package-lock.json` 的 `integrity`）与仓库级登记联动。

本复核给出一套与现有机制同构的最小扩展，其中 **工件 pin 形态已在 PoC 中实测**
（`ui/shell-poc/versions.lock.json`：URL + size + SHA1 + license + upstream index，
并以此完成一次完整下载-校验-构建循环）。

## 2. 机制设计（M5 落地时进入 `dependencies.lock.json` schema v2）

### 2.1 壳二进制工件 pin（CEF，冻结选型）

```json
{
  "name": "cef",
  "kind": "artifact",
  "version": "152.0.8+g1ce985c+chromium-152.0.7977.134",
  "channel": "stable",
  "platforms": ["linux64", "windows64 (M4)"],
  "url": "https://cef-builds.spotifycdn.com/cef_binary_<version>_<platform>.tar.bz2",
  "size_bytes": 674894043,
  "sha1": "add0a51f7333bc660e8e3bafd998e0122568f7d8",
  "upstream_index": "https://cef-builds.spotifycdn.com/index.json",
  "license": "BSD-3-Clause（libcef）；内嵌 Chromium/Blink 组件清单见 chrome://credits",
  "consumption": "构建期下载并校验 SHA 后解包；configure 失败于摘要不匹配"
}
```

要点：与 submodule 条目同处一个锁文件、同一次 configure 校验（`RULE-06` 的
"configure 校验失败即构建失败"语义延伸到工件摘要）；SHA 值取自 CEF 官方
index.json 并以本地 `sha1sum` 复核（PoC 已复核一致）。

### 2.2 npm 前端依赖（`ui/` workspace）

- 锁定事实源：`ui/package-lock.json`（npm 锁定，含全部传递依赖 `integrity`），
  CI 以 `npm ci` 安装，禁止裸 `npm install` 漂移。
- 仓库级登记：`dependencies.lock.json` 增一条 `"kind": "npm-tree"` 概要条目
  （name=`ui-frontend`、lockfile 哈希、Node 版本下限），把 npm 树纳入审计视线；
  许可证清单由 `license-checker` 类工具在发布门禁生成。
- Electron 类**带预编译二进制的 npm 包**如被采用（本次冻结未采用，见 §4），其二
  进制完整性与镜像来源必须额外登记（PoC 实测其 postinstall 走 GitHub releases，
  网络受限环境需 `ELECTRON_MIRROR` 显式登记——多一个镜像来源治理面，是壳选型
  供应链考量之一）。

### 2.3 SBOM

- 工程规范 §11 已要求"SBOM（SPDX/CDX）由构建目标再生成"。壳相关 SBOM 来源：
  - CEF：发行包内 `LICENSE.txt` + `chrome://credits` 清单（Chromium 内嵌第三方组
    件）→ 发布流水线导出为 SPDX 子文档，与主 SBOM 关联；
  - npm 树：`package-lock.json` → CycloneDX npm 生成器；
  - Mirage 自研：源码 SPDX 主文档。
- 发布门禁（§10.5）核对：SBOM 可重复生成、许可证清单完整、与锁文件一致。

## 3. 更新通道签名与差分策略复核（DEC-006 决策 5，M3 复核项）

| 通道 | 签名方案（复核结论） | 差分 / 全量策略（复核结论） |
| --- | --- | --- |
| Linux（.deb，apt 仓库唯一更新路径） | 仓库级 GPG 签名（`Release.gpg`/`InRelease`），用户导入 Mirage 签名 key；签名的 key 管理与发布机隔离 M5 落实 | 安装包全量（apt 无内建二进制差分；repo 元数据 pdiff 由 apt-generate 处理）；CEF 载荷 ≈560 MB 使全量 debs 增量升级流量可感知但可接受，后续可评估 `apt-ftparchive` 之外的 delta 机制，非承诺 |
| Windows（exe 安装包 + 应用内更新器） | 双层：更新清单（版本、URL、SHA-256、ed25519 签名）+ Authenticode 签名的安装包二进制；更新器先验清单签名与哈希再执行，含原子切换与回滚（DEC-006 决策 5 既有承诺不变） | 首版全量安装包；CEF 无内建差分机制，差分更新（如 courgette/bsdiff）M5 按真实流量成本评估，不提前承诺 |
| 通用 | 更新通道与签名验签代码属 Runtime Service/更新器，凭据经系统 keyring（AGENTS.md 纪律）；清单格式与验签失败的 fail-closed 行为在 M5 实现时补决策细节 | — |

复核结论：暂定默认值（Linux apt 唯一路径、Windows 签名应用内更新器）**经受住
壳选型复核，予以冻结**；差分策略记录为"全量优先、差分按 M5 实测评估"，不声明
未验证的增量收益（RULE-08）。壳选型（CEF vs Electron）对更新通道的影响：
CEF 无内建更新框架，Windows 更新器为自研（与决策 5 原设想一致）；Electron 的
Squirrel 生态虽有内建差分，但不足以抵消 §4 的否决理由。

## 4. 对备选壳的供应链视角（佐证 DEC-006 冻结）

- **CEF**：单一上游 CDN（spotifycdn.com，官方 index.json 含 SHA1），版本节奏跟随
  Chromium stable；供应链面 = 一个二进制工件 + 其 Chromium 内嵌组件 SBOM。冻结
  默认值，机制见 §2.1。
- **Electron（封存）**：二进制经 npm postinstall 拉 GitHub releases（镜像治理面
  额外），Node 运行时 + npm 依赖树整体进入 SBOM/审计范围；每次 Chromium 更新随
  Electron 大版本整体迁移。供应链面与 Mirage C++ 主栈双语言成本使其在桥延迟、
  内存无优势的前提下（基线报告 §3.1–3.2）不改变默认值。
- **Tauri（封存）**：引擎不进供应链（系统 WebKitGTK），但由此换来"渲染行为由各
  发行版决定"的一致性风险；且 PoC 未能在本环境完成取证（补跑条件见基线报告
  §3.5）。供应链优势不抵消一致性与双语言成本。

## 5. 验证与限制

- 本复核的工件 pin 形态、SHA 校验、下载-构建循环经 PoC 实测（2026-09-21，
  `ui/shell-poc/versions.lock.json` + 基线报告 §1 环境）；npm 树登记与 SBOM 生成
  为 M5 门禁实现项，本文件只冻结机制设计，不声明已实现。
- `dependencies.lock.json` schema v2 的实际扩展（含 validator 改动）在壳真正进入
  产品构建（M5）的同一变更中落地，保持"未锁定二进制不进默认构建"（DEC-006 第
  6 条）在过渡期继续成立。
