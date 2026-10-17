# 原生输入性能定位与优化

> 状态：In Progress（本机修复/性能矩阵已完成；上游评审及真实桌面/Windows验收开放）
> 日期：2026-10-17
> 负责人：Mirage 维护者
> 工作项：M6-42
> 依据：DEC-033/035/040/041；基线提交 7b61f27，优化为未提交工作树。

## 原因与处理

原 `PlainTextLayout::measure` 在缓存未命中时构造临时 `TextPrimitive`，调用
`measuredSize()`。核对 pinned EUI 公开 `core/render/text.h` 及实现后，确认该路径
会完整排版、加载/光栅化字形、写入共享 atlas，并构造绘制用行数据。每次草稿
变化都会产生新键，全文高度测量因而进入绘制路径。

改用公开 `TextPrimitive::measureTextSize`，保留物理字号、宽度、行高和现有
128 项/256 KiB 的 UI owner 缓存边界。没有自己实现 shaping、换行或字体后端。

输入框最高 168px，其中内容高度只需计算到 144px。新增 `height_up_to`：只取
足以达到上限的完整硬换行段落交给 EUI；每段至少一行，后续段落不可能让高度
下降。普通 14EM 行高下至多测量前六段；继续在后部输入可复用相同前缀。单个
无换行长段仍交给 EUI 完整排版，不截断 UTF-8、连字或段内 shaping 上下文。
保留原输入事件、IME、撤销、选区、滚动、草稿限制及 50–168px 几何。

已核对 Mira API 模块地图与 Mirador 视觉模块边界：本项是 Mirage 原生产品视图
适配，字体排版由已授权 UI 依赖 EUI 承载。不新增异步任务、线程、队列或
调度器，不改变 Executor owner/关闭路径，也不修改第三方 pin。

## 固定方法与环境

Linux x86_64、Intel Core Ultra 5 225H、GCC 13.3.0、CMake 3.28.3，Release `-O3`。
Mira a29374aa、Mirador cb45a102、EUI bcfaa2ea。隔离 Xvfb 1180×800，私有
XDG 状态，不连接 Runtime/模型、不访问用户桌面。固定中文字体 Noto Sans SC
2.004，默认 14EM，1×；减少动态效果。

`native_input_performance_test` 测试 64/4096/16000 字节档位的中英文草稿，分别
在无历史和 40 轮（80 条）合成历史下输入 65 个“字”，舍弃前五个，保留 60 样本。
档位是种子上界，实际种子保留完整句子；输入后仍小于 16 KiB。测量编辑回调、
compose、update、render + glFinish 及总耗时，输出 p50/p95/max。`--unbroken`
将种子的换行替换为空格，以同样方法覆盖单段长文本。历史内容固定且重复，
不代表任意 Markdown、不同全文历史、工具展开或真实流式负载。

总耗时包含软件渲染完成，**不含 OS 输入派发、VSync、合成器呈现或 IPC/模型延迟**，
不能换算成真实桌面 FPS 或宣称 60fps。基线执行时无本轮并行构建；中间诊断
与 sanitizer 编译并行，不用于最终前后对比。

```bash
cmake --build --preset native-release --target native_input_performance_test native_conversation_view_test -j 3
(cd build/native-release/apps/native && ../../tests/native_input_performance_test)
(cd build/native-release/apps/native && ../../tests/native_input_performance_test --unbroken)
ctest --test-dir build/native-release --output-on-failure \
  -R '^(native_conversation_view_test|native_chat_model_test|native_input_performance_test)$'
```

## 回归与限制

尺寸回归将只读测量与旧 drawable 的实际尺寸对比：空文本、中英文、尾换行、
CRLF、Tab、空格、组合字符/连字；12/14/24/32EM、1×/1.25×/2×、无宽度/
80/320px；同时验证高度上限和长文档前缀裁剪。保留现有输入、历史、滚动、
设置及明暗/最小窗口实际渲染回归。

ASAN 首轮在并行构建负载下达到原 30s 测试预算而超时，未记为通过；新增尺寸
矩阵与完整视图的 sanitizer 预算改为 120s，不更改产品超时。测试的旧 drawable
oracle 大字号 rasterization 独立持有并释放 atlas，避免占用后续截图的字形预算。

Windows、物理高 DPI、真实 X11/XWayland compositor 与硬件 GPU 的输入到呈现
延迟仍待维护者补验；条件为运行本次构建并用实际输入法、长草稿和长历史取样。
单段长文本的 EUI 排版、历史 compose 开销继续以基准数据判断，不从本轮多段
收益推断所有场景已消除卡顿。未替换安装包、重启现有应用或更改桌面入口。

## 2026-10-17 最终结果

以下为同一方法的 total p50 / p95（ms），基线和最终运行均未与本轮编译并行。
完整分阶段统计保存在 [before](data/native-input-performance-20261017/before.jsonl)、
[after](data/native-input-performance-20261017/after.jsonl)、
[无换行长段](data/native-input-performance-20261017/unbroken.jsonl)。

| 草稿档位 / 历史 | 基线多段 | 优化后多段 | 优化后单段 |
| --- | --- | --- | --- |
| 64 B / 0 轮 | 3.45 / 5.01 | 3.33 / 5.70 | 4.35 / 6.57 |
| 4096 B / 0 轮 | 10.47 / 19.25 | 2.07 / 2.80 | 19.98 / 33.98 |
| 16000 B / 0 轮 | 15.17 / 28.30 | 1.93 / 2.57 | 37.45 / 50.25 |
| 64 B / 40 轮 | 12.69 / 26.61 | 17.40 / 31.72 | 21.65 / 28.05 |
| 4096 B / 40 轮 | 17.22 / 32.08 | 20.71 / 28.06 | 22.58 / 34.55 |
| 16000 B / 40 轮 | 72.54 / 93.95 | 21.56 / 28.44 | 52.11 / 74.12 |

长草稿/40轮多段样本 total 中位耗时下降约70%，P95下降约70%；compose 中位
64.09→15.28ms。无历史长草稿中位15.17→1.93ms。**短草稿/40轮并未显示改善**，
中位12.69→17.40ms、P95 26.61→31.72ms；没有把选择性收益宣传为全场景提升。
单次前后取样未锁定CPU频率/核，不足以区分短样本波动与历史布局退化，后续需
配对重复测试。长历史及单段输入仍可能超出16.7ms，M6-42继续开放。

单段热点已登记 [EUI-20261017-001](../dependency_feedback/eui-ledger.md#eui-20261017-001长文本输入重复加载相同字形轮廓)。
方案为字体测量层有界 advance 缓存及一致性/资源/输入回归；按 AGENTS.md 等待
明确修改 EUI 和更新 pin/锁文件的授权，不在应用复制输入器或字体实现。

实际验证：

- Release 定向 CTest **5/5**（三项功能/性能目标与 setup/cleanup）；原生视图
  单独截图运行 **27694 checks, 0 failures**。正常/最小、明暗、中英文混排
  第二轮截图复核文字完整、输入高度和控件布局正常。
- ASAN（detect_leaks=1、halt_on_error=1）**4/4**，原生视图17.07s；
  UBSAN（halt_on_error=1、print_stacktrace=1）**4/4**，原生视图14.95s，
  无 sanitizer 报告。两者使用 native=ON、PPOCR/BUNDLE_VISION=OFF 的 Debug
  preset，仅验证本项前端路径，不作为产品视觉资源包门禁。
- 改动 C++ 的 clang-format、git diff --check、文档相对链接检查通过；
  Impeccable detector 返回空列表。没有新增并发路径，未运行TSAN。
- 日志：`/tmp/mirage-input-release-final-test.log`、
  `/tmp/mirage-input-{asan,ubsan}-final-{build,test}.log`；第二轮截图在
  `/tmp/mirage-input-visual-final/`。首轮截图因尺寸oracle耗尽测试共享atlas出现
  缺字，已为oracle明确持有/释放atlas并复验；未把首轮作为通过证据。
- 本轮只改 native 视图、尺寸缓存、对应测试和文档。工作树同期出现的 pet
  bubble/状态测试与M8文档为其他工作，未纳入本项归属或提交。

sanitizer 复跑命令：

```bash
cmake --preset asan -DMIRAGE_ENABLE_NATIVE_FRONTEND=ON -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_BUNDLE_VISION=OFF
cmake --build --preset asan --target native_conversation_view_test native_chat_model_test -j 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan --output-on-failure \
  -R '^(native_conversation_view_test|native_chat_model_test)$'
cmake --preset ubsan -DMIRAGE_ENABLE_NATIVE_FRONTEND=ON -DMIRAGE_WITH_PPOCR=OFF -DMIRAGE_BUNDLE_VISION=OFF
cmake --build --preset ubsan --target native_conversation_view_test native_chat_model_test -j 3
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/ubsan --output-on-failure \
  -R '^(native_conversation_view_test|native_chat_model_test)$'
```

## 2026-10-17 第二阶段：最新dev修复、上游反馈与最终集成

维护者已明确授权修改EUI，并要求先切最新dev、修复及验证通用收益，之后才创建
issue/PR。按该顺序完成：dev `ad7e9e7` 建立基线，修复 `b59b6e9` 经本机四组合、
FreeType oracle及重复A/B后，创建 [issue #113](https://github.com/sudoevolve/EUI-NEO/issues/113)
与 [PR #114](https://github.com/sudoevolve/EUI-NEO/pull/114)。
[远程四后端CI](https://github.com/sudoevolve/EUI-NEO/actions/runs/39017165821)
全部通过；PR保持开放，没有执行合并。Mirage最终pin `63117f7` 合并保留原Windows
修复 f052729/bcfaa2e，锁文件同步；来源、许可证和实际构建边界见
[升级审计](../supply-chain/eui-text-performance-20261017.md)。

EUI每个固定字号face增加512槽、约6KiB的直接映射advance/height缓存，保持Tab、
组合字符、彩色fallback高度钳制和绘制slot语义。两种字体、普通/大字号6类场景，
3轮交错A/B的72组测量指纹逐位一致；常见文字测量约10–75倍加速，3000个不同
汉字工作集基本持平。原始144条统计已随性能PR提交，缓存增加少量内存，未宣称
所有工作集加速或零成本。

EUI单独修复后，长历史页仍有约20ms组合成本，4KiB多段/40轮的中位样本甚至比
基线略慢。因此继续分析，发现Mirage没有使用公开 `ScrollViewBuilder::contentKey`，
每次输入仍创建两个历史测量UI。最终应用接入该公开高度缓存：精确、长度编码的
key包含消息/part正文与状态、展开项、字体/代码字号、缩放和精确视口尺寸，不含
无关草稿/滚动/计时变化。64KiB上限，超限返回空key，保持旧自动测量；组件每会话
最多4键、UI最多24会话，key逻辑存量上限约6MiB。没有复制测量算法或自建缓存调度器。
新增回归检查草稿/滚动不失效，正文、状态、工具输出、展开、字号与0.25px视口变化
必须失效，超预算回退；既有流式、阅读保位和滚动测试继续通过。

### 最终配对性能

对比项是**最新dev＋上一轮应用优化**与**EUI修复＋历史高度缓存的最终版本**，
同机同Release配置；3轮次序为前/后、后/前、前/后，每case预热5次、统计60次，
无并行编译。表中为三轮各自p50/p95的中位数，单位ms。
[完整360条分阶段原始统计](data/native-input-performance-20261017/final-paired.jsonl)
保留mode/run/version、p50/p95/max、样本数和每次进程峰值RSS。

| 草稿档位 / 历史 | 多段基线 p50/p95 | 多段最终 p50/p95 | 单段基线 p50/p95 | 单段最终 p50/p95 |
| --- | --- | --- | --- | --- |
| 64 B / 0轮 | 3.07 / 5.23 | 2.02 / 3.27 | 3.95 / 5.77 | 1.99 / 3.36 |
| 4096 B / 0轮 | 2.19 / 3.46 | 1.52 / 2.03 | 17.64 / 32.08 | 3.61 / 4.92 |
| 16000 B / 0轮 | 1.98 / 2.82 | 1.46 / 2.24 | 38.83 / 52.23 | 6.65 / 9.28 |
| 64 B / 40轮 | 21.26 / 29.13 | 12.88 / 17.31 | 20.50 / 29.61 | 14.20 / 18.24 |
| 4096 B / 40轮 | 21.52 / 30.47 | 13.67 / 18.41 | 28.70 / 45.77 | 14.83 / 22.20 |
| 16000 B / 40轮 | 22.75 / 30.03 | 15.00 / 21.00 | 51.64 / 63.50 | 16.98 / 24.06 |

最终12种输入场景的p50/p95均低于对应基线；单段16KB/40轮中位下降约67%、P95约62%。
长历史的软件渲染P95仍可能超过16.7ms，不能据此宣称60fps或所有卡顿消失。64KiB
以上的历史高度key会安全退回自动测量，尚未覆盖任意巨大Markdown/工具展开负载。
当前宿主未锁频/固定核心，数据用于同方法配对比较，绝对延迟不作为跨设备预算。

### 最终验证状态

- 独立EUI修复四组合各36/36、安装SDK消费者各1/1；最终集成树四组合各36通过、
  1个Windows-only透明测试显式skip，未宣称Windows实机通过。
- 最终Mirage Release **5/5**；新增key失效检查后原生视图 **27703 checks, 0 failures**。
  ASAN **4/4**（视图10.55s）、UBSAN **4/4**（视图5.29s），无sanitizer报告。
  保留第一阶段和并行编译期间诊断结果，不将诊断样本混入最终A/B统计。
- 正常/最小、明暗四张截图与前阶段逐字节一致：`/tmp/mirage-eui-history-visual/`。
  36项独立后端测试覆盖输入编辑、IME布局/事件、滚动与渲染探针；没有访问用户窗口
  或调用模型。真实用户IME/compositor、硬件GPU、高DPI和Windows仍由维护者取得
  目标环境后补验，不以软件渲染结果代替。
- `dependencies.lock.json`锁门禁、改动C++格式及文档链接检查均执行。对应日志为
  `/tmp/mirage-eui-history-{release,asan,ubsan}-test.log`、`/tmp/mirage-eui-lock-test.log`；
  原始配对调用保留在 `/tmp/eui-perf-final-paired/`。
- 未替换已安装应用或重启现有会话；Native Release二进制已重建，依赖pin可从维护者
  fork复现。未提交/混入同期pet工作树修改。

## 2026-10-17：native-release本机更新与Mirage合入准备

维护者明确授权提交Mirage更新、替换native-release应用，并在Mirage PR的CI全部
通过后合入master。本轮提交仅包含M6-42性能代码、对应EUI pin/锁、测试、CI入口
和验证文档，保留同期pet bubble及其测试/文档工作树修改。

```bash
cmake --build --preset native-release --target mirage-native \
  native_input_performance_test native_conversation_view_test -j 3
ctest --test-dir build/native-release --output-on-failure \
  -R '^(native_conversation_view_test|native_chat_model_test|native_input_performance_test|dependency_lock_gate_test)$'
./build/native-release/apps/mirage start \
  --shell /home/linductor/mirage/build/native-release/apps/native/mirage-native \
  --tray /home/linductor/mirage/build/native-release/apps/tray/mirage-tray
```

Release构建与资源同步成功，定向CTest **6/6**（含setup/cleanup）。桌面入口已指向
该native-release路径；未安装系统DEB。首次省略shell/tray显式路径的CLI尝试因
默认查找路径不匹配而拒绝启动；改用桌面入口已有的完整参数后成功。启动时原托盘
已退出，本次就绪托盘PID799175、原生前端PID799245；前端实际exe路径与目标构建
一致，运行映像SHA-256为
`2914a11341c2a86a80fb4579c8404dd4896df517139fae52a1c28aeaaa95f58c`。
X11确认所属窗口已映射、2360×1600；这证明新版启动/呈现，不等于实测端到端输入
延迟或用户手动验收。没有提交模型任务或修改模型配置。

CI的native job增加新性能用例的构建/执行。构建/测试/启动证据分别在
`/tmp/mirage-perf-deploy-{build,test}.log`、`/tmp/mirage-perf-native-start.log`、
`/tmp/mirage-perf-native-running.json`；Mirage PR的CI与合入结果由该PR的checks和
merge记录提供，CI未全绿时不执行合并。
