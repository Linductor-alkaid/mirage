# 原生前端长会话性能与 EUI dev 消费

> 日期：2026-10-23
> 负责人：Mirage 维护者
> 工作项：M6-48 / M6-49 / M6-50（沿用 DEC-108）
> 状态：已授权的上游修复 PR #117 和 Linux 消费已实现；最终取证见文末，目标平台门禁保留

## 原因与修复范围

原前端虽然裁掉离屏控件，仍对每条历史调用 Markdown estimateHeight。EUI 已有公共
文档/inline 布局缓存，但文档缓存仅 8 项；多条不同正文反复估高会淘汰文档并触发解析，
并不是缺少通用 Markdown 缓存。另一个问题是精确 ScrollView contentKey 超过
64 KiB 后返回空，导致每帧额外执行两次整个历史的测量回调。

本次保留 EUI 解析/排版与 ScrollView，用当前会话逐消息的精确源快照复用计算后的
几何及过程段高度；紧凑 revision 作为 contentKey，历史超过 64 KiB 仍可命中。
输入/主题/视口高度变化复用原几何；字体/缩放、正文/过程/展开/失败及时序变更按
实际输入失效。消息顺序变化也使总高度 key 失效。80 条、8 MiB 源快照预算、每条
两种宽度，超预算退回原测量；未新增线程、队列、异步路径或文字排版实现。

EUI 升级另见[审计](../supply-chain/eui-dev-20261023.md)。#111/#114 此前已消费，
本次新增文字图集延迟分配/已用矩形上传来自 upstream 068e192，不把已有缓存收益
再次算作升级新增收益。

## 固定环境与方法

Linux 7.0.0-38-generic、Ubuntu GCC 13.3.0、CMake 3.28.3、Intel Core Ultra 5
225H，Release -O3；1180×800、私有 Xvfb、Mesa llvmpipe、Noto Sans SC 2.004
OTF（SHA256 faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9）。
与当前应用使用同一字体目录，不调用模型、不读取真实会话或配置。

基线为开始时 HEAD e966b13 的 app.cpp 与原 EUI 63117f7。为了保持相同负载，
将本次新基准源码链接到原 app.cpp/原 EUI 静态库，源码中相对 include 解析为原
绝对路径；其他静态库及编译/链接 flags 相同。原始 baseline-app.cpp、
baseline-benchmark.cpp、编译/链接 argv 与静态库副本保存在本轮 out 目录，避免
只比较原基准中反复使用相同回复、无法覆盖 8 项文档缓存淘汰的情况。

`native_input_performance_test --long-history`：40 轮，39 条各约 8 KiB 的不同
Markdown 回复（每轮独立标记）、40 条各 8 KiB 折叠思考，最新回复保持短文以观察
旧历史开销。分别输入 64/4096/16000 B 草稿，每 case 5 次预热、60 次采样；随后
新建 pending 轮，65 次变化预览测流式整帧。首轮插入会按既有 80 条上限淘汰最旧轮。
另保留原 0/40 轮短历史、多段/单段基准，检查常规路径。

整帧包括事件、compose、update、render+glFinish；不是 GPU 单独绘制时间，也
不含供应商推理。RSS 来自每进程 /usr/bin/time 的最大值，包含字体、驱动、预热。
未锁频；不宣称硬件 GPU、真实 compositor、高 DPI、Windows 或任意单条超长回复
达到 60 FPS。物理交互门禁负责人 Mirage 维护者，取得环境后同负载补跑。

## 验证记录

- 首轮 identical-history 诊断样本受到另一工程并行编译影响；非配对样本出现明显
  慢化，保留 before-long / geometry-long 原始日志，不纳入最终性能结论。
- 一次 baseline 截图从仓库根启动，缺少相对字体路径，退出非零；随后从产品字体
  目录重跑成功，84 张截图在 before-views/，只采用修正后的结果。
- Release 几何首轮回归 3/3（包含 fixture）通过。
- 最终依赖的 ASAN（detect_leaks=1:halt_on_error=1）5/5，44.03 秒，涵盖会话
  视图、chat model、GPU 纹理及两个隔离 fixture，无 sanitizer 报告。
- 边界检查：66 个公开头、0 违例、2 个 DEC-096 已记录 seam；本次变更文件 format 与 diff
  空白检查通过。一次检查误用 REPO_ROOT 参数得出 0 headers，已按 ROOT_DIR
  重跑，只采用实际扫描 66 个头的结果。
- EUI fork 已推送集成 pin；尝试 dispatch ci.yml 返回 HTTP 404（fork 默认分支
  无可调用 workflow），没有修改仓库设置。不能把它写成远程 CI 成功。

UBSAN（halt_on_error=1:print_stacktrace=1）同样 5/5，20.30 秒，无报告。
最终结果见下方追加记录。

## 复现命令

```bash
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build build/native-release -j 3
ctest --test-dir build/native-release --output-on-failure
cd build/native-release/apps/native
LP_NUM_THREADS=2 /usr/bin/time -f '%M' /absolute/path/to/benchmark --long-history
LP_NUM_THREADS=2 /absolute/path/to/benchmark --long-history --unbroken
```

原始日志与生成物位于 `out/frontend-performance-20261023/`，不提交；由 Mirage
维护者保留至本轮评审与目标平台补验结束。构建和 sanitizer 时不采最终性能数据。


全仓 `mirage-format-check` 未通过：HEAD 原有
`integration/mira/src/model_layer.cpp:1571` 的换行与 clang-format 不符，`git show HEAD`
确认与工作树相同；没有将无关格式修改混入本次变更。变更文件 `clang-format --dry-run
--Werror` 与 `git diff --check` 通过。对应日志 `/tmp/mirage-final-verify-build.log`。


## 授权前阶段的配对结果（每组 p50 / p95，ms）

构建、sanitizer、截图与重跑均结束后采样；`LP_NUM_THREADS=2`，每个 profile
按 AB/BA/AB 交替三轮，表格取三轮各自 p50/p95 的中位数，不是合并全部帧的百分位。
起/终 1 分钟 loadavg 为 0.29 / 1.51，无并行编译，未停止宿主常规桌面进程。
相同 profile/草稿的几何和字体条件一致，前后二进制 SHA 记录于 paired-meta.json。

| 长历史负载 | 多段基线 | 多段优化 | 无换行基线 | 无换行优化 |
| --- | --- | --- | --- | --- |
| 64 B 草稿 | 51.33 / 70.84 | 35.22 / 39.29 | 51.69 / 67.74 | 27.07 / 29.74 |
| 4096 B 草稿 | 55.71 / 70.86 | 36.75 / 41.43 | 72.27 / 76.33 | 34.14 / 35.93 |
| 16000 B 草稿 | 69.15 / 71.76 | 38.81 / 41.33 | 75.79 / 79.75 | 34.09 / 42.83 |
| 流式预览 | 82.10 / 106.53 | 31.02 / 33.44 | 65.30 / 102.36 | 29.11 / 32.51 |

16 KB 多段整帧 p50 降 43.9%、p95 降 42.4%；流式整帧 p50 降 62.2%、p95
降 68.6%。无换行 16 KB 档 p50 降 55.0%。这些结果仍未达到 16.7 ms，不能宣称
完全消除掉帧：与视口相交的单条长 Markdown 仍完整 build DSL 控件树。公开 API
核对、最小能力建议与可验收条件已登记为
[EUI-20261023-001](../dependency_feedback/eui-ledger.md#eui-20261023-001单条长-markdown-仍完整构建可见离屏-dsl-控件)。
这是请求上游开发授权前的阶段结果；维护者随后明确授权，修复与最终消费见文末。

| 原短历史负载 | 多段基线 | 多段优化 | 无换行基线 | 无换行优化 |
| --- | --- | --- | --- | --- |
| 0 轮 / 64 B | 4.34 / 5.48 | 3.97 / 5.27 | 4.36 / 5.48 | 4.54 / 6.08 |
| 0 轮 / 4096 B | 5.28 / 5.70 | 4.78 / 6.01 | 6.49 / 7.10 | 6.63 / 7.52 |
| 0 轮 / 16000 B | 5.47 / 5.83 | 5.43 / 6.98 | 11.12 / 12.16 | 11.48 / 13.20 |
| 40 轮 / 64 B | 6.74 / 7.81 | 6.97 / 9.17 | 6.91 / 7.42 | 7.14 / 7.77 |
| 40 轮 / 4096 B | 6.93 / 9.34 | 7.12 / 12.71 | 8.24 / 8.96 | 8.38 / 10.16 |
| 40 轮 / 16000 B | 6.63 / 8.80 | 6.90 / 8.08 | 14.23 / 15.35 | 13.68 / 14.91 |

常规路径未证明一致加速，部分尾延迟更高（例如短历史 4096 B / 40 轮多段
9.34 → 12.71 ms）。不省略这些样本，不把长历史收益外推全部输入；主收益来自
超过原 key 上限、包含不同正文的长历史工作集。

| 进程最大 RSS（三轮中位，KiB） | 基线 | 优化 |
| --- | --- | --- |
| 短历史/多段 | 170740 | 166620 |
| 短历史/无换行 | 173916 | 169708 |
| 长历史/多段 | 187940 | 184424 |
| 长历史/无换行 | 189388 | 185756 |

长历史峰值 RSS 降约 1.9%，新增源快照有成本；不把图集/缓存的理论字节数当作
进程实测内存。没有分别定量隔离 Mirage 几何复用与 EUI 图集升级的全部收益。

## 最终验证与环境修复

- Release 完整构建成功；全量 CTest 首轮 117/118（285.43 s），唯一失败
  controlled_window_switch_test，单独复跑仍在 Openbox 启动 fixture 超时。
  发现已有 CMake cache 指向已丢失的 `/tmp/mirage-openbox-fixture/openbox`，
  宿主未安装 Openbox；与 EUI 无链接关系。用 Ubuntu noble 的 Openbox/
  libobt/libobrender 3.6.1-12build5 及 libimlib2t64 1.12.1-1.1build2 解包到原
  临时 fixture 目录，wrapper 仅为测试子进程设置库/主题路径，没有安装系统包、
  修改仓库代码或生产配置。恢复后该项 1/1 通过（11.12 s）；其余 117 项未重复。
  所有 118 项均有通过记录，不描述为一次全量 118/118。原失败与两次重跑日志保留。
- 最终 Release 会话截图 84 张：52 张逐字节一致，包括正常/最小 × 明暗四张。
  其余 32 张差异只在运行用时与 pulse 区域，差异 bbox/像素数保留；局部检查确认
  2.4 秒/2.3 秒与脉冲时刻差，文字、布局与滚动无差异。已做两轮有界视觉检查。
- ASAN 5/5、UBSAN 5/5、变更文件格式/空白及 66 公开头边界通过。全仓格式门禁
  仍因 HEAD 原有 model_layer.cpp:1571 的格式失败，不将其标为通过。
- 更新 docs/design、DEC-108、计划、README、依赖锁与 EUI ledger；新增自动化
  native_long_history_performance_test。没有新增并发/生命周期路径，未重跑 TSAN。
- native-release 已重建；用户现有进程未被重启。新前端构建在下次启动时使用。
  真实 compositor、物理高 DPI/硬件 GPU、Windows 与 EUI SDL/Vulkan 本轮未验证，
  由 Mirage/EUI 维护者取得目标环境后补跑，原门禁保持开放。


## 授权后的 EUI 视口构建与最终消费（M6-50）

独立 EUI 修复 [PR #117](https://github.com/sudoevolve/EUI-NEO/pull/117) 已提交，
修复 SHA c18a4ba，四项远端 backend-matrix CI 通过。最终集成 pin 19acab6，
保留 Windows bcfaa2e；锁文件/gitlink 同步，详见[审计](../supply-chain/eui-dev-20261023.md)。
应用正文及 text 过程段传入局部 viewport，选择按下时用公共完整 builder 临时
收集原 UTF-8 几何，平时只构建视口附近控件。无新线程、任务、异步解析器或调度器。

单独 EUI 组件固定宽度 420、视口 500/180、约 18 KiB 中文/Latin/emoji 段落，
TTF/CFF 各三轮 AB/BA/AB。CFF 稳态 p50/p95 95.153/96.614 → 0.739/0.793 ms，
变化追加 94.791/96.608 → 25.253/25.700 ms；节点 9003–9005 → 432。
这些是构建/布局/释放时间，不能代替下方产品整帧结果。原始 60 帧数组、RSS、
字体/二进制 SHA 和可运行同配置基准脚本随上游 PR 提交。

本阶段 Release 相关回归 5/5（含 fixtures）1.72s；最终 ASAN 5/5 13.69s、
UBSAN 5/5 5.79s，无报告。包含新增长 Markdown 中部滚动/受限树/选区测试，
以及原字体/DPI/主题/窗口/过程展开、跟随与手动浏览回归。Sanitizer 构建的
Mirage 和 EUI core/消费代码均按各自 preset 插桩；上游独立头测试的覆盖限制
已单列，不混用两种证据。本阶段早期构建暴露新增局部变量 shadow 和测试误用
eui namespace，已修正后再取得上述通过结果；一次误写 texture target 名称
未运行构建，重用实际 pet_texture_test 重跑。

最终截图从产品字体目录运行成功，85 张（新增 1 张视口选区）；其中与阶段一相同的
84 张有 53 张逐字节一致，其他差异 bbox 对应 2.3→2.4 秒用时或 pulse。
正常/最小尺寸 × light/dark 四张逐字节一致；新增段落中部选区已审阅。
一次截图误从仓库根启动导致字体断言失败，结果弃用；有效证据仅使用
`/tmp/mirage-viewport-capture-valid.log`。未重新设计界面或修改文案。

边界检查仍为 66 headers / 0 violations / 2 recorded seams；本阶段变更
C++ 文件 clang-format dry-run 与 git diff --check 通过。HEAD 原有无关
model_layer.cpp 全仓格式失败仍未混入。真实 compositor/硬件 GPU/Windows
未测；负责人 Mirage 维护者，取得目标环境后同输入补跑。首次完整选区构建、
变化长文/宽度变化仍可能超过帧预算，不宣称完全消除卡顿。

最终完整 native-release CTest **119/119 通过，287.97s**，含两项新增基准和
恢复好的 Openbox fixture；这是一次完整全绿，区别于授权前 117/118 + 单项重跑。
全量结果 `/tmp/mirage-viewport-full-test.log`。


### 最终产品配对结果：长历史与常规输入

固定原始产品 e966b13 + EUI 63117f7 与最终产品/EUI 19acab6，同一新基准源码、
相同 Release flags 和产品字体目录。重建基线时从原 EUI 导出旧公共头，以
SYSTEM include（与产品相同的第三方警告待遇）配合原静态库，避免误用新 API。
构建结束且全量/插桩/截图验证结束后，LP_NUM_THREADS=2、Xvfb/llvmpipe，
按 AB/BA/AB 各三轮；每个 case 五帧预热、六十帧整帧。下表取三轮 p50/p95
中位数，不是合并样本百分位。宿主桌面未停，未锁频；1 分钟 loadavg 1.95→4.15。
前后 SHA 和逐轮分阶段结果在 viewport/paired-meta.json、paired-raw.json；
原始可执行文件保留为 common-before/after-product，不含真实会话数据。

| 负载 | 原始多段 p50/p95 ms | 最终多段 | 原始无换行 | 最终无换行 |
| --- | --- | --- | --- | --- |
| 长历史 / 64 B | 39.66 / 57.53 | 13.23 / 15.20 | 36.26 / 50.50 | 10.72 / 12.45 |
| 长历史 / 4096 B | 54.09 / 61.99 | 11.21 / 13.71 | 53.40 / 90.61 | 13.34 / 15.93 |
| 长历史 / 16000 B | 55.48 / 95.11 | 11.56 / 13.54 | 41.76 / 69.17 | 20.06 / 32.02 |
| 长历史 / 短预览流式 | 50.68 / 60.88 | 13.23 / 17.01 | 50.58 / 55.55 | 10.20 / 11.20 |

| 常规负载 | 原始多段 p50/p95 ms | 最终多段 | 原始无换行 | 最终无换行 |
| --- | --- | --- | --- | --- |
| 0 轮 / 64 B | 5.03 / 8.53 | 4.66 / 7.83 | 4.55 / 5.25 | 4.92 / 6.40 |
| 0 轮 / 4096 B | 5.28 / 8.93 | 5.18 / 10.51 | 7.32 / 11.42 | 6.84 / 8.93 |
| 0 轮 / 16000 B | 5.31 / 9.12 | 5.55 / 7.25 | 13.79 / 18.11 | 10.09 / 11.60 |
| 40 轮 / 64 B | 8.74 / 10.74 | 7.80 / 11.74 | 8.83 / 10.99 | 8.60 / 10.06 |
| 40 轮 / 4096 B | 8.45 / 14.11 | 6.93 / 8.80 | 11.67 / 15.56 | 10.35 / 14.87 |
| 40 轮 / 16000 B | 8.24 / 10.95 | 6.76 / 10.27 | 17.57 / 19.02 | 16.93 / 19.30 |

长历史 16 KB 多段整帧 p50 降 79.2%、p95 降 85.8%；
该 profile 进程最大 RSS 三轮中位 188060→170788 KiB。
不宣称普遍 60 FPS：无换行 16 KB 的尾延迟仍高，常规 0 轮 4096 B 多段
p95 8.93→10.51 ms、0 轮 64 B 无换行 5.25→6.40 ms；这些波动同样保留。
原基线本轮与授权前不同轮次的绝对值有变化，不能跨轮拼接作收益比例。


### 产品单长段基准的准入修正

首次 `--long-visible` 用 18,000 B 调用 apply_preview，超出 ChatModel
16 KiB 上限后被拒收，得到空 pending 的错误低耗时；这组 long-visible
数据全部废弃，保留在 viewport/invalid-18k-*，不纳入性能结论。
同轮 short/short-unbroken/long/long-unbroken 未使用该路径，仍有效，
有效汇总/原始文件已剔除 long-visible；EUI 独立 18 KiB 单元基准不经过
ChatModel，不受影响。

修正产品单段为小于 16,000 B（给序号保留余量），每帧断言上限内且实际
messages.back().text 与提交预览完全相等，然后再测 compose/update/render。
前后均重建同一新基准。三项性能 CTest 加 fixtures **5/5，7.14s** 通过；
这在完整 119/119 之后补验，产品实现与原视图/插桩测试没有再修改。
最终单段独立 AB/BA/AB 的二进制 SHA、loadavg、逐轮结果分别记录于
viewport/valid16k-meta.json、valid16k-raw.json；不和作废的 18 KB 数字拼接。


### 准入修正后的单长段有效结果

输入正文 15,984 B（追加序号后仍在 16 KiB 内）、40 轮不同长历史与折叠思考，
每帧包含真实 apply_preview 接纳、精确文本断言、compose/update/render/glFinish。
三轮 AB/BA/AB；同机字体/配置，五次预热/六十次采样，1 分钟 loadavg 1.96→1.59。
无并行构建或测试。p50/p95 均取每轮统计值的中位数。

| 有效负载 | 原始 p50 / p95 ms | 最终 p50 / p95 ms |
| --- | --- | --- |
| 15,984 B 段落变化预览 | 387.02 / 409.20 | 60.86 / 62.38 |
| 随后短预览（长段仍在历史尾部） | 401.41 / 417.62 | 12.48 / 13.32 |

该 profile 最大 RSS 三轮中位 267472→182148 KiB；
长段变化 p95 降 84.8%，仍明显超过 16.7 ms，
不能声称消除全部掉帧。EUI 稳态复用与离屏树成本已收敛；变化内容仍有公开
estimateHeight/实际宽度的重新度量和布局成本，首次选择也需完整快照。
该剩余工作须按新证据单独立项，不在本轮暗中改内容、丢预览或降低正确性。

有效结果与原始二进制/脚本保存在 out/frontend-performance-20261023/viewport/；
生产功能与新基准已完成，M6-50 Linux 消费验收关闭，上游 PR 待评审合并。
产品构建已更新；未重启正在运行的前端实例，下一次启动使用新产物。
