# ROI OCR 输入法复合工具验收（2026-10-23）

> 状态：限定增量已交付；M7-35 整体 In Progress
> 负责人：Mirage 维护者
> 关联：[DEC-114](../decisions/DEC-114-roi-ocr-ime-text-tool.md)、M7-28/35
> 工作树：`feat/keyboard-ime-state`，未创建提交

## 工具与职责

新增 `desktop.input.fill` 1.0.0，Mira 一次指定当前共享交互截图的目标框 ROI、
`text`、`mode_roi` 与 `candidates_roi`。工具执行自然点击聚焦、全框替换、OCR 中/英
判断、必要时 Shift、拼音输入、`=` 翻页与可见数字选字，并核对每段累计落字。
首增量支持最多 32 个基本汉字/可打印 ASCII、256 UTF-8 字节；汉字段最多 8 字、
64 个拼音字母，优先常见词组，兼容逐字。每段最多 8 页，总计 256 次动作/ROI 观察、
120 秒且服从父期限；不执行 Enter 提交。

识别完全使用 `mirador::crop` 后的本地 Mirador OCR（真实试验采用 PP-OCR）：模式、候选、目标框分开裁剪，每块
不超过 512 Ki 像素，目标框与候选块不得重叠。内部图片和 OCR 正文不送给模型。
候选按 Mirador 公开 `merge_text_lines` 的行合并/排序组合（标记索引保留原框置信度），匹配目标和其可见编号的置信度均须至少 0.85；
无关装饰/预编辑的低置信度不能替代目标证据。目标值仅接受一条完整 OCR 行，
不去掉空格、不拼接分开的框，避免把空格/换行消除后形成假成功。编号与候选在两张严格更新的帧上
一致后才允许选字/翻页，最多观察三帧；其他刷新等待同样最多复读三帧，不重发输入。
重复页、歧义、低置信度、落字不符、取消、授权/authority 改变及预算耗尽均明确停止。

可选的一次 ModelGateway 纯文本拼音请求使用 model contract 2.0 的 Request 预算域；
只带原文，不带截图、OCR 或工具。JSON 必须精确覆盖原文并通过语言字符约束，才能
开始副作用。内部回复不显示成用户回答，用量/准入/取消仍沿原任务。不是另一个 Agent
循环或 Skill 发布体系；依赖依据为 pinned Mira DEC-040 和公开 ModelGateway API。

整个复合操作复用现有 Executor 有限工具任务和 ActionCoordinator 单写者 lease；
聚焦后的第二次 lease 必须接续 epoch。逐步重验权限与取消，完整按键释放后再观察。
释放失败沿已有 authority 围栏；已释放 input_lag 沿 InputRecovery 新帧恢复路径，
其他带不确定副作用的后端输入失败退役原 session。没有新线程、队列、timer 或依赖修改。

成功回执的 `text_verified=true`、`verification_method=roi_ocr_exact` 仅代表 OCR
文本证据；普通 `verified` 仍由既有动作条件决定。失败保留阶段与 `verified_bytes`
（UTF-8 已核对前缀字节数）、动作/观察计数（`roi_ocr_calls` 是含采集/授权失败的
ROI 观察尝试数，试验独立记录实际 OCR 调用数）；装饰后的 512 字节回执保留 `ime_progress`。
失败不把工具永久归类为平台缺失，也不允许盲目重发整段。

## 证据等级与真实试验限制

环境为 Ubuntu 24.04.5 / x86_64，GCC 13，IBus libpinyin、GTK3 和 bundled
PP-OCRv5 mobile。每轮创建私有 Xvfb 1100×720、D-Bus、XDG 配置与 GTK 输入框；
候选窗口和键盘输入是真实 IBus/X11，OCR 是生产 PP-OCR。测试专用 X11 shared-session
shim 仅为了运行同一个生产 fill 工具；它不代表产品已开放 X11 fill，也不是实际 Portal
合成器验收。运行入口要求私有测试标志，未向用户真实桌面注入输入或修改其配置。

正式真实样本使用预校验拼音计划，**不包含在线拼音生成准确率**；模式 ROI 是固定“中”
测试标志，**不包含实际输入法模式发现/切换准确率**。候选字体固定 Noto Sans CJK SC 28，
目标框字体 28px。词组样本为 30 组 2～4 字公开文本，轮转重复；同一轮的私有输入法
字典可学习，样本不是独立随机应用抽样。逐字压力样本为 10 组双字文本分两段输入。

成功定义必须同时满足：工具成功、`text_verified=true`、独立 GTK `Entry.get_text()`
与原文精确一致。工具失败但实际已落字仍计失败；工具成功但 GTK 不符计错误接受。
不使用 VLM 判读截图，也不以受控 mock 的通过数替代真实准确率。

## 重复试验结果

逐条公开文本结果、失败代码、源码和产物 SHA-256 见
[结构化证据](data/roi-ime-fill-20261023.json)。校准轮和最终轮分列，不混合成一个成功率。

| 版本/样本 | 完整成功 | 错误接受 | 说明 |
| --- | --- | --- | --- |
| 初始扩展词组 | 91/100 | 0 | 增加候选稳定性检查前 |
| 稳定性词组复测 | 94/100、93/100 | 各 0 | 每轮 30 组词轮转，后处理重构前 |
| 初始/后续逐字压力 | 2/10、4/10、4/10 | 各 0 | 双字文本分两段输入 |
| 行合并集成失败轮：词组 | 1/30 | 0 | 编号原框顺序接线错误，未作为通过版本 |
| 同失败版本：逐字 | 1/10 | **1** | “中囯”被 OCR 读成“中国”，误接受保留 |
| **最终源码：词组** | **28/30（93.3%）** | **0** | 30 组不同文本；2 次候选编号低置信度停止 |
| **最终源码：逐字压力** | **4/10（40%）** | **0** | 3 次落字不确认、2 次候选低置信度、1 次候选缺失 |

首轮扩展词组测试 91/100 成功，0 次错误接受：6 次目标候选/编号置信度不足、
2 次实际正确落字但 ROI OCR 置信度不足、1 次拼音末字母刷新未完成便翻页。
因此增加两帧编号候选一致性检查。后续复用 Mirador 行合并时，集成端未保留原候选
框内的左右次序，真实试验与拆分框回归都暴露该错误；已按合并成员的原 x 坐标修正
编号绑定，最终源码再次完整运行 30+10 次。候选/落字相近字仍可能同时被高置信度
OCR 误读：失败轮中的 `囯`（U+56EF）与 `国`（U+56FD）就是实际反例，不能用
最终小样本 0 次误接受推导不存在该风险。准确语义值证明仍沿 DEC-111 的独立路径。
逐字质量明显弱于词组，尚不足以宣称任意中文输入已可靠交付。

## 自动化与复现

受控 controller 100 次变化初始模式/二页第三号选字；完整 registry 工具 20 次；
完整 ModelLayer 单次复合调用另验：只捕获初始图片，内部规划仅有文本，ROI 图片
不进入模型，规划 JSON 不进入用户回复。负向覆盖错误拼音、ROI 重叠、重复页、
重复编号/目标、拆分文本框、时间数字、低置信度、部分 OCR、刷新迟到、错误落字、
未关闭候选、ASCII/混输、取消、撤权、释放失败、provider 异常、迟到及预算耗尽。
Executor 正常 future、异常传播、关闭后拒绝复用原公开路径验证。

```bash
cmake --build --preset debug --target desktop_image_supply_test ime_text_input_test harness_desktop_tools_test post_action_verification_test input_method_state_test -j 2
ctest --preset debug -R '^(ime_text_input_test|desktop_image_supply_test|harness_desktop_tools_test|post_action_verification_test|action_contract_test|input_method_state_fixture_test)$'
cmake --build --preset native-release --target ime_fill_acceptance mirage-service mirage-native -j 4
/usr/bin/python3 tests/manual/ime_fill_acceptance.py --binary build/native-release/tests/ime_fill_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --output out/ime-fill-repeat --trials 100
/usr/bin/python3 tests/manual/ime_fill_acceptance.py --binary build/native-release/tests/ime_fill_acceptance --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json --output out/ime-fill-characters --trials 10 --split-characters
```

ASAN/UBSAN 各构建 `ime_text_input_test desktop_image_supply_test` 并运行相同两项；
TSAN 同样构建后使用 `setarch x86_64 -R ctest --preset tsan -R
'^(ime_text_input_test|desktop_image_supply_test)$'`，沿用已记录的 ASLR 运行环境限制。
最终 Debug 为 6 项功能测试（含两项状态隔离 fixture 共 8/8）；ASAN、UBSAN、
TSAN 各两项功能测试（含 fixture 共 4/4）。最终结果、耗时及产物标识见结构化证据。
67 个公开头文件边界检查零违规，C++ 格式与 diff 空白检查通过。
`native-release` 的 `ime_fill_acceptance`、`mirage-service` 与 `mirage-native` 构建成功；
没有更新安装包或重启运行中产品。最终源码实测时各源码/二进制标识保存在上述 JSON。

## 尚未验收及补跑条件

- 实际中/英指示、Shift 切换、混输、其他候选字体/主题与独立单字的真实成功率。
- 实际 Portal 授权、compositor 截图/输入、焦点变化/切窗、Fcitx5 与 Windows TSF。
- 在线模型拼音规划、端到端自主任务与复杂输入框（占位符、多行、滚动、富文本）。

负责人均为 Mirage 维护者。目标会话/应用/输入法/OCR 资源及在线配置具备后补跑；
每项沿同一精确 GTK/目标应用读值核对及错误接受定义，保留脱敏回执与配置。上述限制
保留开放，M7-28/31/35 不因私有 X11 shim 或受控模型测试而勾选全部完成。
