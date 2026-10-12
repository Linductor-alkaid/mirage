# M7-07 固定 OCR 条件与真实 Canvas 闭环

> 状态：Verified（Linux 条件/受控 Canvas 增量；M7-07 In Progress，完整权重 TSAN 业务链路待复验）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：[M7-07/08](../plans/m7-visual-tools-and-input.md)
> 依据：[DEC-059](../decisions/DEC-059-visual-text-postconditions.md)

## 验收范围

新增 `visual_text_exists/expected_text` 后条件，动作前固定，后观察、只读重验和终态
都经过 screen.capture 权限并重新截图/识别。固定服务显示器与模型 identity；检查请求内
单调时间区间、递增 backend 调用计数、未使用过的 scope、焦点/窗口几何与 epoch。
目标窗口必须完整位于显示器内。最多 128 区域、1024 UTF-8 字节/文本、64 次视觉
后观察/轮；协作预算 5 秒，受任务/当前工具的取消和 deadline 限制。完整窗内、OCR
来源、confidence ≥ 0.8、文本精确相同且恰好一处命中才成功。

Runtime 复用现有 PpOcrPipeline/VisualSessionHost 和唯一 kairo owner。Mirador
`kRefresh` 强制重新执行，Mira `evaluate_observation` 核验新鲜度。无新 worker、队列
或依赖变更。只有镜像身份、调用计数与 digest 进入普通过程投影；原始条件/识别文本
不进入该投影。原有 uncertain/释放失败围栏保持。

## 真实 Canvas 证据

[验收脚本](../../tests/manual/visual_canvas_acceptance.py)将公开测试文本绘制到私有
Xvfb 的普通 X11 Canvas（520×220 显示器、480×180 窗口、原点 (20,20)），随后运行
[平台测试](../../tests/platform/x11_visual_marks_test.cpp)的真实 PP-OCR 模式。

实际链路为 ScreenProvider 捕获 → PP-OCR/ncnn → Mirador fusion/SoM → PNG supplier
→ Mira ConversationLoop 脚本 provider 选择编号 1 → XTest 完整 press/release → 应用
收到事件并重绘业务文本 → 只读 verify 的新截图/OCR → 终态再次核验。窗口标题始终
为 Canvas，没有 AccessibilityProvider；动作前的图不能充当动作后的业务证据。
脚本 provider 只保证可复现工具选择，并不证明在线模型视觉推理能力。

| 样本 | 点击前文字 | 固定预期 | 字号 | 结果 |
| --- | --- | --- | --- | --- |
| English | Submit task | Task saved | 32 px | verified |
| 中文 | 提交任务 | 任务完成 | 32 px | verified |
| 小字号 | 保存文件 | 文件已保存 | 16 px | verified |
| 中英混排 | 提交 Submit | 完成 Done | 24 px | verified |
| 不重绘负例 | Submit task | Task saved | 32 px | unverified，宿主 failed，成功回答被抑制 |

5/5 全部退出 0。每例一个 PNG 进入模型输入，真实按下/释放均观测到；backend 计数
1（编号）→2（派发后）→3（重验）→4（终态），scope 依次更新。初次后观察发生在
应用处理事件前，正例通过只读重验恢复 verified；负例在终态仍无法通过。实际 mark
anchor 从浮点映射并取整，平台测试只在框中心 ±1 px 内接受按下事件，且不修改产品
坐标或条件判定；首跑中文/小字号因测试端 floor 与产品 lround 差异失败，修正后复跑。

外部清单 `/tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json`：文件 SHA-256
`0210022d7f85dc0f7e0f8caebf1b5e5a9af89db49c87c25ab877d3a39e9ce6a4`；规范 JSON
model revision `51769fd8e9146ef45e7455d27e6f60b4de76ccf2e1a14bc4ede784e2db2322d1`。
字体 SHA-256 `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`。
报告含样本 SHA、规范动作/验证回执，见[归档](data/visual-canvas-20261010.json)。
权重与字体依据沿用 [M7-06](real-ocr-correctness-20261009.md)，无新增模型下载/分发。

```bash
python3 tests/manual/visual_canvas_acceptance.py --binary build/ocr-debug/tests/x11_visual_marks_test --manifest /tmp/mirage-ppocrv5-resources-logits-20261009/manifest.json --font build/native-debug/apps/native/assets/NotoSansSC-Regular.otf --output /tmp/mirage-m7-07-canvas-20261010
```

## 回归与故障验证

[条件测试](../../tests/integration/visual_text_verification_test.cpp)覆盖低置信度、重复/
跨窗/裁切区域、非 OCR 标签、坏身份/UTF-8/框/预算、旧时间/scope/backend 计数、
权限拒绝、异常、取消、源/模型变化、前台/窗口/epoch 竞态和 64 次预算。
固定条件不能被 verify 更换；只读重验和终态不会重复输入。实际 mark→click→新 OCR
工具链额外验证 1024 字节条件成功、1025 字节在输入前拒绝；发现并统一了视觉点击
schema 原 256 字符限制与冻结条件上限的不一致。负原点与 confidence=0.8
边界通过；同文本位于另一个窗口不造成目标窗口歧义。
[会话测试](../../tests/integration/post_action_verification_test.cpp)新增视觉通过、重复/
不匹配/权限失败的宿主终态与条件文本脱敏验证。

| 验证 | 实际结果 |
| --- | --- |
| native-debug 全量 | 60/60，0 failure，26.34 s |
| native-release 全量 | 60/60，0 failure，22.60 s |
| opt-in ocr-debug 全量 | 59/59，0 failure，24.12 s |
| opt-in scalar ncnn ASAN/UBSAN 定向 | 6/6，0 failure，14.28 s |
| opt-in scalar ncnn TSAN 定向 | 6/6，0 failure，14.74 s |
| 外部真实权重 Canvas（ocr-debug） | 5/5，0 failure |
| 同 Canvas ASAN/UBSAN 复跑 | 5/5，0 failure |
| 同 Canvas 完整权重 TSAN 额外复跑 | 0/5，工具取消或未验证；未作为业务通过证据 |

定向六项为 visual_text_verification_test、post_action_verification_test、visual_marks_test、
x11_visual_marks_test、ppocr_pipeline_test、visual_session_test。ASAN 开启 detect_leaks=1，
UBSAN/TSAN 开启 halt_on_error=1；TSAN 沿已登记环境做法通过进程级 setarch -R 运行，
未改系统设置。六项含真实 X11 与真实小型 ncnn 合成权重；不冒充完整权重 TSAN 业务验收。

额外完整权重 TSAN 样本保留 5 秒协作/30 秒 owner 预算，返回 tool execution was
cancelled 或业务未验证；日志无 ThreadSanitizer race 报告，但不能据此记业务成功。
正常与 ASAN 5/5、TSAN 定向 6/6 是已通过范围；完整权重插桩时延/预算复验由维护者
负责，在能保持原期限完成链路的目标环境补跑，不扩大预算掩盖该限制。归档保留这次
0/5 和每项 exit code；该环境限制不登记为依赖能力缺口。

```bash
cmake --build --preset native-debug -j 3
ctest --preset native-debug -j 3
cmake --build --preset native-release -j 3
ctest --preset native-release -j 3
cmake --build --preset ocr-debug -j 3
ctest --preset ocr-debug -j 3
cmake --build build/ocr-asan --target visual_text_verification_test post_action_verification_test visual_marks_test x11_visual_marks_test ppocr_pipeline_test visual_session_test -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/ocr-asan -R '^(visual_text_verification_test|post_action_verification_test|visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|visual_session_test)$' --output-on-failure -j 1
cmake --build build/ocr-tsan --target visual_text_verification_test post_action_verification_test visual_marks_test x11_visual_marks_test ppocr_pipeline_test visual_session_test -j 2
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R ctest --test-dir build/ocr-tsan -R '^(visual_text_verification_test|post_action_verification_test|visual_marks_test|x11_visual_marks_test|ppocr_pipeline_test|visual_session_test)$' --output-on-failure -j 1
```

真实 ASAN/UBSAN 与 TSAN 额外复跑分别给前述 Python 命令加相同 sanitizer 环境，
binary 改用 build/ocr-asan 或 build/ocr-tsan 的 x11_visual_marks_test，后者通过
setarch x86_64 -R 启动 Python（子进程继承）；output 使用独立目录。

本轮 11 个 C++ 文件 clang-format --dry-run --Werror、11 份文档的 251 个本地链接/
标题/fence 检查及 git diff --check 均通过。三份依赖子模块源码工作树干净；staged
为空。本轮没有新增 pin 变更、commit、push 或 MR；保留既有 M7 在途与无关用户改动。
Mira/Mirador/kairo 身份记入归档；其公开能力可承载本增量，没有新增依赖反馈。

## 剩余门禁

M7-07 仍 In Progress。维护者在目标环境补在线模型读取实际 OCR 编号图与用户宿主
Canvas；此处 Xvfb 是真实 Provider 的受控应用验收，不是用户桌面或物理 DPI 证明。
图标/template 的产品配置尚未装配；4K/局部预采集仍受当前全显示器 8 MiB 上限限制。
Windows/Wayland、跨显示器/混合 DPI、彩色/多行/旋转文字及 OCR 权重分发许可分别
由 M7-10/供应链审计承接。具备目标环境/权限与相应资源后执行同源/坐标/输入/后条件
测试并补记录，才可扩大声明。未新增上游缺口；mira/mirador 子模块源码未改。
