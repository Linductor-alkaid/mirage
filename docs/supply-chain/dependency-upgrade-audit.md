# 依赖升级审计

本文件记录 Mirage 直接依赖（pinned `mira`、`mirador` 及其内嵌依赖）的每次升级审计：
版本差异、对 Mirage 接触面的影响、许可证核对与回归验证证据。依据
[工程规范](../project/project-standards.md) 第 9.1 节与第 10 节（依赖升级走独立 MR、
在 `docs/supply-chain/` 记录审计结论）。

---

## 2026-09-20：mira → cf0af75、mirador → 50f5349

- **分支**：`chore/deps-upstream-sync`（升级前两者分别 pin 在 `255e4cf`、`fbcf6c4`）。
- **动机**：上游 master 前进，跟进上游能力交付；无 Mirage 侧能力缺口驱动
  （`docs/dependency_feedback/ledger.md` 本次无新增条目）。

### 版本差异

| 依赖 | 旧 pin | 新 pin | 上游主要交付 |
| --- | --- | --- | --- |
| mira | `255e4cf` | `cf0af75` | M7 工具模块（TM0 契约重定义 DEC-042、TM1 registry lifecycle、TM2 LLM exposure projection）；内嵌 executor pin 升级 |
| mira → executor | `e2dc8ca`（v0.4.0-100） | `2ae4fc8`（v0.5.0） | 正式发布区间（`e2dc8ca..2ae4fc8`）仅含打包/发布脚本与 src 内部修复（task_monitor、lockfree_queue）；公开 `include/` 零 diff——任务级协作取消（`task_cancellation.hpp`）、TimerHandle（`timer.hpp`）、串行上下文、总量有界 admission 等在 v0.4.0-100 时已全部在 master 上 |
| mira → mbedtls | `068ff08`（v3.6.7） | 不变 | — |
| mirador | `fbcf6c4`（v0.1.0-18） | `50f5349`（v0.2.0-17） | v0.2.0：M5 display contract、X11/Windows GDI/Android MediaProjection capture adapter 示例、kDisplay 空间经 adapter transform 开放、fuzz 入口与隐私负向测试；v0.2.0 之后：M6 实验性几何区域提议与验证基准（`geometric_proposal.hpp` 为新增实验头） |
| mirador → googletest | `063de7e` | 不变 | — |

### Mirage 接触面 API 影响分析

对 Mirage 全部依赖 include 点做了逐一 diff（全仓扫描 `#include <mira/*> <executor/*>
<mirador/*>` 得到接触面）：

- **executor**（`blocking_io`、`stop_token`、`executor`、`types`、
  `serial_execution_context`、`comm/{topic,mailbox,channel,types}`）：`include/` 在
  `e2dc8ca..2ae4fc8` 区间零变化。API 风险解除。
- **mira**（`json`、`environment`、`version`、`runtime`、`core_contracts`、
  `adapters/simulator/simulator_environment`）：仅 `json.hpp` 有 25 行 diff，为纯
  格式化（换行调整），无签名变化。其余零变化。mira M7 新增的 `tool_module*.hpp`
  等新头未被 Mirage 使用。
- **mirador**（`pixel_format`、`backend_info`）：零变化。M5/M6 的新能力
  （fusion `kDisplay`、geometric proposal）均为增量 API，未触及 Mirage 现有用法。

**结论：本次升级对 Mirage 是纯指针前滚，无 API 适配需求。**

### 许可证核对

- mira：新 pin 仍无 LICENSE 文件，`dependencies.lock.json` 的 UNLICENSED 注记维持，
  待上游补齐后更新。
- executor：MIT（不变）；mirador：MIT（不变）；mbedtls：Apache-2.0 / GPL-2.0-or-later
  （不变）；googletest：BSD-3-Clause（不变）；sqlite（vendored，audit-only）不变。

### 回归验证证据

- **构建**：`cmake --preset debug && cmake --build --preset debug` 退出码 0，118 个
  目标全部通过；configure 输出 5/5 pin verified（mira `cf0af75`、mira:executor
  `2ae4fc8`、mira:mbedtls `068ff08`、mirador `50f5349`、mirador:googletest `063de7e`）。
- **测试**（Independent-Verification-Agent 独立执行，日志 `/tmp/ctest_{debug,asan,ubsan}.log`）：
  - debug：`ctest --preset debug` 24/24 通过（unit 11 / integration 13 / protocol 2 /
    smoke 1 覆盖），31.18s。
  - ASAN：构建退出码 0，`ctest --preset asan` 24/24 通过；verbose 重跑全量输出中
    ASAN/LSAN 报告 0 次。
  - UBSAN：构建退出码 0，`ctest --preset ubsan` 24/24 通过；verbose 重跑全量输出中
    "runtime error" 出现 0 次（verbose 补跑是因为 UBSAN 默认可恢复、仅摘要模式存在
    静默报告风险）。
- **未覆盖项**：TSAN 与 release preset 未运行。本次无跨上下文状态或关闭路径的代码
  变更（API 接触面零变化），TSAN 留待下次并发相关改动时随常规 TSAN 覆盖执行。

### 审计结论

通过。submodule 指针与 `dependencies.lock.json` 同步更新于同一变更；无许可证变化、
无 API 适配、无回归。executor v0.5.0 的任务取消/定时句柄等新能力为后续 Mirage
工作项（如长任务的排队期取消）提供现成支撑，按需另行引入。

---

## 2026-09-20：mirador → 6fa92ec（v0.3.0）

- **分支**：`chore/deps-mirador-v0.3.0`（升级前 mirador pin 在 `50f5349`）。
- **动机**：M3（Mirador 视觉集成）里程碑启动，随后续 pin 进入 M3 实际消费的
  mirador 能力面；上游 v0.3.0 将 M6 实验轨道的几何区域提议契约经上游
  `DEC-018` 阶段 1 转正为兼容性承诺内契约，消除 Mirage 在 M3 消费该能力时的
  契约漂移风险。无 Mirage 侧能力缺口驱动
  （`docs/dependency_feedback/ledger.md` 本次无新增条目）。

### 版本差异

| 依赖 | 旧 pin | 新 pin | 上游主要交付 |
| --- | --- | --- | --- |
| mirador | `50f5349`（v0.2.0-17） | `6fa92ec`（v0.3.0 tag） | v0.3.0 发布簿记：`geometric_proposal.hpp` 经上游 `DEC-018` 阶段 1 从实验契约转正为正式公共契约（计入兼容性承诺；`temporal_stability` 与融合/输出模型集成属阶段 2，上游另行立项）；ubuntu-20.04（focal）容器 CI 门禁与 GCC 10.5 工具链下限登记；隐私测试容差调整。`50f5349..v0.3.0` 区间 `include/` 仅 `geometric_proposal.hpp` 注释变化（撤销 Experimental 标注，签名与类型零变化），另 tag 后 master `670617b`（README showcase 重构）未纳入 pin |
| mirador → googletest | `063de7e` | 不变 | — |
| mira / executor / mbedtls | 不变（`cf0af75` / `2ae4fc8` / `068ff08`） | — | — |

### Mirage 接触面 API 影响分析

Mirage 当前对 mirador 的接触面为 `pixel_format`、`backend_info`（
`integration/mirador` 的 `VisualBackendIdentity` 校验）。`50f5349..6fa92ec`
区间两接触面头文件零 diff。`geometric_proposal.hpp` 的变化为纯注释（契约状态
标注），M3 尚未消费该头，无适配需求。上游 ubuntu-20.04 门禁登记的 GCC ≥ 10
工具链下限低于 Mirage 自身基线（GCC 13.3，CMake ≥ 3.25），不构成约束收紧。

**结论：本次升级对 Mirage 是纯指针前滚，无 API 适配需求。**

### 许可证核对

- mirador：MIT（不变）；googletest：BSD-3-Clause（不变）。mira（UNLICENSED
  注记维持）、executor（MIT）、mbedtls（Apache-2.0 / GPL-2.0-or-later）、
  sqlite（Public Domain，vendored，audit-only）均不变。

### 回归验证证据

（Independent-Verification-Agent 独立执行，2026-09-20，Linux x64，Ubuntu 24.04，
GCC 13.3.0，工作树增量 + /tmp 冷构建交叉验证；原始日志 `/tmp/verify_v030/`。）

- **五预设矩阵**：`debug` / `release` / `asan` / `ubsan` / `tsan` configure +
  build + ctest 均 **24/24 通过、0 skip、0 Not Run**。`tsan` 直跑复现本机高熵
  ASLR 怪癖（README 注意事项，17 个测试 `unexpected memory mapping` 终止），
  `setarch $(uname -m) -R ctest --preset tsan` 24/24 通过（与 CI 运行方式一致）。
- **pin 校验**：五个预设 configure 输出逐行一致，5/5 verified——mira
  `cf0af75`、executor `2ae4fc8`、mbedtls `068ff08`、mirador `6fa92ec`（本次
  新 pin）、googletest `063de7e`，与 `dependencies.lock.json` 完全一致。
- **sanitizer verbose 复跑**：asan（`detect_leaks=1`）ERROR / leak 报告 0；
  ubsan（`print_stacktrace=1`）`runtime error` 0；tsan（`setarch -R` +
  verbose）WARNING 0。
- **冷构建交叉验证**：/tmp 全新目录从 v0.3.0 configure + build + ctest 一次
  通过（24/24），排除增量产物掩盖断裂；`geometric_proposal.cpp`（本次上游
  唯一 `include/` 变更头）以 v0.3.0 源码在五预设 sanitizer 标志下显式编译
  链接全部成功。
- **接线事实（升级前即如此，非本次引入）**：Mirage 顶层以
  `EXCLUDE_FROM_ALL` 引入 mirador 且唯一消费点为 `integration/mirador` 的
  `mirador::core` 链接，`mirador::image` / `cache` / `geometry` / `fusion` /
  `render` 模块目标不在默认构建依赖图内；上游本次调整的 `tests/privacy`
  容差亦在 Mirage 构建面之外（`MIRADOR_BUILD_TESTS` 被 FORCE 关闭，设计
  如此，上游 CI 自行覆盖）。二者随 M3 集成工作项接入并验证。
- **观察项（非本次回归）**：`ubsan` 首跑 `bridge_relay_test` 偶发失败一次
  （fd 收敛窗口内瞬时 +1；同二进制复跑 10/10、debug 10/10、ubsan 套件 3/3
  全过，定性为并行负载下时序敏感的既有 flaky，与 mirador 无关——该测试属
  M1.5 devbridge 代码，本次未触碰）。负责人：维护者，随 M3 期间常规矩阵
  继续观察，必要时单独排查。

### 审计结论

通过。submodule 指针与 `dependencies.lock.json` 同步更新于同一变更；无许可证
变化、无 API 适配、无回归；冷构建证明整树可从 v0.3.0 从零编译。上游
`DEC-018` 阶段 1 转正使几何区域提议契约计入上游兼容性承诺，为 M3 的
`M3-04` 消费提供契约稳定性前提；`mirador::geometry` 等模块目标的显式链接
与运行时行为回归由 M3 工作项（`M3-02` / `M3-04`）承接。

---

## 2026-09-20：mirador → fff7f15（v0.3.0-2，docs-only）

- **分支**：`chore/deps-mirador-integration-skill`（升级前 mirador pin 在
  `6fa92ec`，即 v0.3.0 tag）。
- **动机**：上游在 v0.3.0 之后新增下游集成 skill
  （`docs/skill/mirador-integration/`：1 个路由 `SKILL.md` + 12 张按需加载
  参考卡，卡片代码经上游对照 v0.3.0 编译运行验证）与 README showcase 重构
  （含中文版）。M3（`M3-01` / `M3-02` 起）是 pin 的首个大规模消费面，按
  AGENTS.md「先使用 pinned 依赖自带的资源、按其路由说明加载」的纪律，将
  skill 纳入 pin 使其可在检出位置原位读取（skill adoption 卡推荐路径）。
  无 Mirage 侧能力缺口驱动（`docs/dependency_feedback/ledger.md` 本次无
  新增条目）。

### 版本差异

| 依赖 | 旧 pin | 新 pin | 上游主要交付 |
| --- | --- | --- | --- |
| mirador | `6fa92ec`（v0.3.0 tag） | `fff7f15`（v0.3.0-2） | `6fa92ec..fff7f15` 全部为 docs：`670617b` README showcase 重构 + 中文版；`fff7f15` 新增 `docs/skill/mirador-integration/`（路由 + 参考卡）。**`include/` 零变化**（`git diff --name-only` 过滤 `docs/`、README 后为空） |
| mirador → googletest | `063de7e` | 不变 | — |
| mira / executor / mbedtls | 不变（`cf0af75` / `2ae4fc8` / `068ff08`） | — | — |

### Mirage 接触面 API 影响分析

Mirage 现有接触面（`integration/mirador` 的 `mirador::core`：`pixel_format`、
`backend_info`）在区间内零 diff。skill 内容为使用指引（quick-start、帧与
坐标空间、后端 SPI、会话与缓存、融合与快照、SoM、几何、视觉索引及场景 /
需求 / API 索引），不改变任何公共头契约；其中与 M3 契约相关的增量信息
（`mirador::fusion` 为会话管线的最高层链接目标、`kDisplay` 证据参与融合时
`FusionOptions::display_transform` 必填、stable id 仅 session 内唯一且以
`is_generation_current` 判定失效、视觉索引 `query` 非 const 等）将由
`DEC-016`（`M3-01`）吸收并落 Mirage 侧契约。

**结论：本次升级对 Mirage 是纯指针前滚（docs-only），无 API 适配需求。**

### 许可证核对

- mirador：MIT（不变）；googletest：BSD-3-Clause（不变）。mira（UNLICENSED
  注记维持）、executor（MIT）、mbedtls（Apache-2.0 / GPL-2.0-or-later）、
  sqlite（Public Domain，vendored，audit-only）均不变。skill 文档随 pin
  一并以 MIT 接收，不复制进 Mirage 自有文档树（原位读取，升级时随 pin
  刷新）。

### 回归验证证据

- **pin 校验**：`cmake --preset debug` configure 通过，5/5 pin verified
  （mira `cf0af75`、executor `2ae4fc8`、mbedtls `068ff08`、mirador `fff7f15`
  （本次新 pin）、googletest `063de7e`），与 `dependencies.lock.json` 一致。
- **未覆盖项**：debug/release/asan/ubsan/tsan 构建与 ctest 矩阵本次未重跑。
  理由：区间零 `include/` 变化，构建与测试的输入（编译单元、链接目标）
  相对前一 pin（v0.3.0，五预设 24/24 全绿的取证见上一条目）逐字节不变，
  重跑不产生新信息；矩阵随 `M3-01` 的常规验证在其工作树上继续覆盖。

### 审计结论

通过。submodule 指针与 `dependencies.lock.json` 同步更新于同一变更；
docs-only 增量，无许可证变化、无 API 适配。`docs/skill/mirador-integration/`
自本 pin 起为 Mirage 侧 mirador 集成工作的按需路由资源（AGENTS.md 纪律），
其建议与 pinned 公开头冲突时以头文件契约为准。

---

## 2026-09-26：mira → 1348515、mirador → fb0dc3f（v0.3.0-80）

- **分支**：`chore/deps-upstream-sync-20260926`（升级前两者分别 pin 在
  `cf0af75`、`fff7f15`）。
- **动机**：上游 master 前进，跟进上游能力交付；无 Mirage 侧能力缺口驱动
  （`docs/dependency_feedback/ledger.md` 本次无新增条目）。
- **环境**：本机取证首次覆盖双依赖同时前滚（Windows 11 x64，MSVC 19.44
  BuildTools，Visual Studio 17 2022 生成器）；sanitizer 矩阵由 CI（Linux）
  双面确认。

### 版本差异

| 依赖 | 旧 pin | 新 pin | 上游主要交付 |
| --- | --- | --- | --- |
| mira | `cf0af75` | `1348515`（38 commits，无新 tag） | M23 Stage W4 记忆晋升、M24 Stage W5 上下文策展 fork/merge、M25 host 集成轮、M26 DEC-037 Stage T1 时间策略契约冻结、DEC-040 TR0/TR1/TR2（稳定工具引用、skill 发布生命周期、运行时接线）、DEC-039 MCP 工具模块接纳、DEC-043 架构基线门禁 |
| mira → executor | `2ae4fc8`（v0.5.0） | 不变 | — |
| mira → mbedtls | `068ff08`（v3.6.7） | 不变 | — |
| mirador | `fff7f15`（v0.3.0-2） | `fb0dc3f`（v0.3.0-80，无新 tag） | M7 跨帧目标跟踪 Experimental 轨道（DEC-019/DEC-020）：M7-01..M7-05 `object_tracker.hpp` 池模型与跟踪管线、M7-06 证据融合状态机、M7-07 全局运动补偿、M7-08 级联重检测与身份复核、M7-09 合成目标跟踪 A/B/C/D 基准与阈值校准；新头 `shift_estimation.hpp` |
| mirador → googletest | `063de7e` | 不变 | — |

### Mirage 接触面 API 影响分析

对 Mirage 全部依赖 include 点逐一 diff（全仓扫描 `#include <mira/*>
<executor/*> <mirador/*>` 得到接触面）：

- **mira**（`artifact_store`、`environment`、`json`、`version`、
  `adapters/simulator/simulator_environment`）：`cf0af75..1348515` 区间
  `include/` 五个接触面头文件**零 diff**。上游新增/修改的 11 个公共头
  （`temporal_policy`、`tool_module_mcp`、`tool_reference`、`tool_skill`、
  `context_working_context_fork`、`context_working_context_promotion` 新增；
  `agent_loop`、`context_working_context`、`memory_consolidation`、
  `workflow_events`、`workflow_runtime` 修改，合计 +1701/-4）均为纯增量，
  未被 Mirage 引用。CMake 层新增编译单元进入 `mira_core` / `mira_workflow`
  源列表，`Mira::core` 目标接口不变。
- **executor**：嵌套 gitlink 不变（`2ae4fc8`），接触面头文件零风险。
- **mirador**：接触面传递引入的唯一变更头是 `stable_id_tracker.hpp`
  （`fusion.hpp` / `perception_session.hpp` 均 include 它）：`advance()`
  新增第 4 个带默认值的 `std::span<const ConfirmedAssociation>` 参数
  （M7-06 DEC-010 门控直通），空关联列表复现既有行为逐位不变，源码兼容；
  Mirage 源码未调用 `advance()`，无适配点。新头 `object_tracker.hpp`
  （+1621）与 `shift_estimation.hpp`（+167）为独立 Experimental 契约，
  未被 Mirage 接触面头传递引入。直接接触面（`pixel_format`、
  `backend_info` 等）零 diff。
- **构建图变化**：上游新增 `src/fusion/object_tracker.cpp` 进入
  `mirador::fusion`、`src/image/shift_estimation.cpp` 进入
  `mirador::image`——两目标在 Mirage 依赖图内（`integration/mirador` 链接
  `mirador::fusion`，PUBLIC 拉入 `mirador::image`/`cache`），属本次升级
  实际新增的编译与链接面，已由下述全树构建验证。

**结论：无 API 适配需求；新增编译面（object_tracker / shift_estimation）
经全树构建验证通过。**

### 许可证核对

- mira：新 pin 仍无 LICENSE 文件，`dependencies.lock.json` 的 UNLICENSED
  注记维持，待上游补齐后更新。
- executor：MIT（不变）；mirador：MIT（不变，新头随 pin 以 MIT 接收）；
  mbedtls：Apache-2.0 / GPL-2.0-or-later（不变）；googletest：
  BSD-3-Clause（不变）；sqlite（vendored，audit-only）不变。

### 回归验证证据

（本机 Windows 11 x64，MSVC 19.44 BuildTools，Visual Studio 17 2022 生成器
x64，2026-09-26；增量构建树 `build/windows`。）

- **configure**：`cmake -S . -B build/windows -A x64
  -DMIRAGE_FETCH_DEPENDENCIES=OFF` 通过，5/5 pin verified——mira
  `1348515`、executor `2ae4fc8`、mbedtls `068ff08`、mirador `fb0dc3f`
  （本次新 pin）、googletest `063de7e`，与 `dependencies.lock.json` 一致；
  CEF 工件与前端 lockfile 门禁复核通过（ATL 警告为既有环境噪音，非本次
  引入）。
- **构建**：`cmake --build build/windows --config Debug` 退出码 0，含
  mirador::fusion（`object_tracker.cpp`）与 mirador::image
  （`shift_estimation.cpp`）新编译单元及 mira_core/mira_workflow 新增源
  在 MSVC `/utf-8` 下全量编译链接通过；仅既有 LNK4199 DELAYLOAD 链接
  警告（apps/desktop，非本次引入）。
- **测试**：`ctest --test-dir build/windows -C Debug` **23/23 通过、0 失败**
  （unit 16 / integration 7 / platform 6 / protocol 1 / smoke 2 标签覆盖），
  79.15s。
- **未覆盖项**：asan / ubsan / tsan 预设本机不可运行（预设使用 GCC 风格
  `-fsanitize` flag，MSVC 不支持），随推送触发 CI（Linux）矩阵双面确认；
  release 预设本次未跑，本次无优化路径代码变更。

### 审计结论

通过（CI 双面确认待推送后补充）。submodule 指针与 `dependencies.lock.json`
同步更新于同一变更；无许可证变化、无 API 适配、Windows 全树构建与 23/23
测试通过。上游 M7 目标跟踪 Experimental 契约（`object_tracker.hpp`）与
mira 的 DEC-040 工具引用层均为后续 Mirage 工作项的候选能力，按需另行
引入消费。
