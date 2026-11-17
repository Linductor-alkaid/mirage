# Mira Workflow 学习依赖升级审计（2026-11-14）

> 工作项：M15-17；负责人：Mirage/Mira 维护者
> 授权：维护者明确同意补齐 Mira 并升级 Mirage pin

## 来源与变更

旧 pin：be44e36044eaf8e5ab09bc0a22f134a3c8545111。
新 pin：2c42627a23da83c6d04825c28163836f9f747ec3，来自 Mira 的
feat/bounded-memory-storage 功能分支，已普通推送，未合入 master 或创建 release/tag。

- [88c6836](https://github.com/Linductor-alkaid/mira/commit/88c68362887e9e9dc6a28ef7b2e79051afc8ba58)：
  DEC-111/M31-13 为 SqliteMemoryStore 增加可选独占文件长度总量接纳、维护预留、
  容量快照和重开计量；使用 SQLite 公共 VFS 接口复用实际 I/O/WAL/事务，不新增线程
  或存储引擎。预算含数据库/WAL/SHM/journal，Mira 作为库的默认 0 仍保持旧行为。
- [e1eab33](https://github.com/Linductor-alkaid/mira/commit/e1eab33567be93c046525051da688d9805d1cc29)：
  DEC-112/M14-08 增加显式 Text/JsonObject 决策协议、生成/思考预算与同源本地 schema
  parser，严格模式仍默认，工具授权、patch/权限/epoch 校验和取消路径保持原承载。

- [232c9d1](https://github.com/Linductor-alkaid/mira/commit/232c9d10f6bc0102566057cf601e71773fc5310f)：
  修正 Windows CI 发现的文件身份拼写差异，白名单改用原生 SQLite VFS xFullPathname。
- [2c42627](https://github.com/Linductor-alkaid/mira/commit/2c42627a23da83c6d04825c28163836f9f747ec3)：
  修正架构检查器按绝对路径排除嵌入式仓库的问题，补三种父路径回归；人工恢复既有
  sqlite_memory_store 文件规模债务，未提高预算或新增豁免。

Mirage 通过 integration 的共享 Store owner 默认开启 Workflow 元数据学习；不开启
截图采集，不在产品层复制配额库。直接 Host 的 Session 默认、产品固定本地用户 Agent
scope 和明确关闭方式见 DEC-146。Mira 的 provider 异常/多轮回执反馈仍保留，单 Run
恢复仍限一次；本升级不冒充完整 M31 或上游已修复 MIRA-20261112-002/003。

## 依赖与许可证

仅 Mira gitlink 与 dependencies.lock.json 的 Mira commit/说明/日期同步变化。嵌套
kairo 10e870bcb94b109a1a1f32eab8ef0d3d9f01055b、mbedtls、SQLite 以及独立 mirador、
eui-neo 的锁定版本均保持不变；未新增权重、生成资产或第三方运行时。许可证沿原
Mira AGPL-3.0 / kairo MIT / 既有 SQLite 与 TLS 审计，无新增许可证来源。

## 验证

公开 API 与依赖自带指南已核对；Mira 原恢复测试、独立文件长度测量的容量测试与
Mirage 服务/持久化/视觉/CLI 闭环在最终四配置各 15/15 通过，0 skip、无 sanitizer 报告。架构、平台边界、
Mira 379 文件格式与 Mirage 67 公共头边界检查通过。嵌入式架构检查漏扫已由
2c42627 修正，实际扫描为 16 项既有债务、0 stale、0 新违规；原先漏扫结果不计入验收。新 pin 最终矩阵与
[在线证据](../compatibility/workflow-learning-20261114.md) 由 M15 记录。

[最终 Mira CI](https://github.com/Linductor-alkaid/mira/actions/runs/43784628663) 在
2c42627 上 **12/12 job 通过**，包括 quality、Linux GCC/Clang Debug/Release、Windows
Debug/Release、Android 两 ABI 与 ASAN/UBSAN/TSAN。首轮 Windows 容量 open 失败、
次轮架构检查失败及修正如上，未通过轮次均保留。Windows 的 Mirage 产品设备运行、
真实断电、跨层归档/回收和物理块峰值仍未验收。
