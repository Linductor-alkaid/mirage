# M7 积累变更 Git 交付（2026-10-12）

> 状态：本地 Git 交付完成（推送/评审与真实平台门禁未关闭）
> 工作项：M6-26 / M6-37 / M6-38 与 M7 已交付增量
> 负责人：Mirage 维护者
> 分支：feat/m7-conversation-desktop-tools
> 整理前 HEAD：c786df7e047f03864f03f3bb1facd413f5111c03

维护者要求将已交付开发纳入 Git。工作树包含多轮 M7 的代码、测试、已授权依赖升级
和验收记录；按职责拆分，不把整个积累塞进含义不明确的一次提交。各层中间提交
未单独构建，最终组合状态是本次验证边界；真实平台/原用户任务开放门禁不因提交而关闭。

| 顺序 | 内容 | 提交 |
| --- | --- | --- |
| 1 | 排除测试驱动字节码 | 1675464 |
| 2 | 已授权 Mira/Mirador pins、资源锁与依赖反馈/审计 | 5296409 |
| 3 | 桌面输入/引用契约、Mirador 视觉组合和对应夹具 | e3e9018 |
| 4 | X11/Wayland 平台、Portal/PipeWire、授权和凭据后端 | a0b637c |
| 5 | 会话三模式、授权联动、最近更新排序及原生菜单 | 393670a |
| 6 | Runtime/IPC、Mira 桌面工具与 workflow、任务预算/默认思考 | 6703251 |
| 7 | 设计、计划、兼容性证据及本交付记录 | 本记录所在提交 |

整理期间检测到另一任务修改同一工作树的工具/宿主提示与回归。维护者明确答复
“有修改，没关系一起核验提交”。将该 M7-15/M7-10 选路提示增量纳入最终组合验证，
不把此前的全量结果冒充覆盖新增内容。

提交者使用仓库已有本人 user.name / user.email，未覆盖身份。未改动第三方实现；
三个依赖工作树干净，gitlinks 与 dependencies.lock.json 一致。Bytecode 用 .gitignore
排除，无关 Impeccable 检查产物与四份原始短测试输出在本机保留并本地排除。
未提交模型权重、构建目录、DEB、运行日志或明文凭据。12 张合成/私有 Xvfb UI 验收
PNG 共处于兼容性证据中，无大于 1 MiB 的新增文件。

合成产品会话的原始 IPC 回执改为状态/工具计数摘要；源记录仅保存在本机
/tmp/mirage-git-native-history-original.json。不将正文、完整工具图引用和原始会话
过程当作版本化运行日志。其他用户故障证据保持其既有最小化摘要；凭据模式扫描
未发现实际 Token/Key。既有历史证据中的“未提交”描述原验收时状态，本记录是后来
Git 交付的事实源。

最终组合重建后 Debug 全量 70/70、Native Release 全量 76/76；ASAN、UBSAN、TSAN
选路回归各 5/5（desktop_image_supply_test、harness_desktop_tools_test、gui_fallback_test
及 setup/cleanup）。此前 6/6 生命周期 sanitizer 是上一轮快照，本次 5/5 明确覆盖
同期新增提示回归，不扩大成全部平台保证。78 个待交付源码文件的 SHA 在整个验证
期间一致；代码提交 6703251 保存该组合。最终 DEB 已同步到 out/packages，四个
可执行 ELF 与 Native Release 一致；[摘要](data/m7-git-delivery-20261012/summary.json)
记录源码、测试日志摘要及最终安装包 SHA。git diff --check 和提交前索引检查通过。
文档交付后的工作树将以 git status --porcelain 复核，忽略项是保留的本机产物。
本轮不推送、不合并，不重启/安装用户产品，也不发起真实桌面授权。
