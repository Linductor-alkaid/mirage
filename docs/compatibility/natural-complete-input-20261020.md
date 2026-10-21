# 完整滚动、拖动与键盘自然时序（2026-10-20）

> 状态：Linux 限定实现、全量/定向回归与产品构建通过；真实桌面模型/手感验收开放
> 工作项：M7-25/26/31
> 依据：[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)、DEC-054/056/064/077/100
> 基线：e63c8de + feat/desktop-input-policy 工作树
> 负责人：Mirage 维护者

## 范围与契约

X11/Portal 既有完整滚动、拖动、chord 和物理文本输入采用 input_cadence_policy v1。
新增纯函数 input_cadence.hpp/.cpp，在宿主局部种子上确定停留与间隔；没有模型时间字段。
点击的 pointer_motion_policy v2 与 held-session budget_version=1 均保持原版本。
PointerMotionVariation 新增默认 true 的 allow_overshoot；拖动设为 false，不改变点击
既有随机策略。C++ 使用方需重编译，未修改 pinned 依赖。

模型工具 vision.scroll/drag、scroll_region/drag_region、session.key/type_text/scroll/drag
及 input.key/type_text 升为 4.0.0；原物理点击 4.0.0 保留。工具参数、输入所有权与
DEC-056 v2 分层验证不变，普通输入不因此新增 OCR/业务后条件。

滚动与拖动先沿既有自然接近路径到达起点，在同一 action lease 中重验来源/目标，再
执行完整手势。源与目的地仍受原窗口/共享范围、2048 像素和 16 步总滚动预算限制。
Portal 未知同流起点在 lease 前拒绝，不用瞬移建立起点；到达后的新画面仍受来源序号
和目标稳定性约束。滚轮每次发送一个正/负步，方向与步数之和保持一致，无惯性像素或
滚动距离保证。拖动按下停留后走 32 点默认、64 点上限的曲线，220～900 ms，按窗口/
授权流边界裁剪，中间不释放，精确到达并停留后再释放。

按键停留 45～85 ms，字符间隔 35～85 ms，修饰键按下间隔 12～28 ms、正常逆序释放
间隔 8～18 ms；滚轮停留 8～16 ms、步间隔 30～65 ms；拖动按下 45～80 ms、落点
停留 35～65 ms。真实默认详见 DEC-110。数值是产品策略，未用人类样本校准。

工具完整动作预算 3 s，文本 15 s，并服从父 operation 的更早期限。底层 InputLimits
默认 1 s 仍是调用者预算，模型工具主动传入新预算；直接 C++ 使用方应按文本长度显式
选择合适上限。完整时序计划无法放入剩余预算时在本阶段输入前拒绝，不截断必然超时
的文本。移动本身的 hover 副作用仍如实报告。外部取消/期限等可能在已派发后触发，
不将 admission 检查当作无部分执行保证。

纯计划不拥有任务。Linux 等待是既有有限 Executor 调用中的同步平台 continuation，
每次 poll ≤4 ms 后重新检查取消/权限/期限；不新建线程、队列、timer 或调度 owner。
平台往返和 OS 调度可能超过该轮询间隔，不声明硬实时。到期/取消/派发失败后立即尝试
清理拥有的按键，跳过正常释放间隔；清理失败仍封锁后续输入。动作内已观察到的取消
保留到回执，不能因释放后探针恢复 false 而丢失。

Portal 仍是当前 keymap 的 printable ASCII NotifyKeyboardKeysym 文本路径；非 ASCII
继续在派发前报 text_transport_unavailable。X11 沿现有 keymap/shift 映射，并在字符
边界检查焦点。没有引入剪贴板、Unicode 合成、IME、裸修饰键或跨工具持有。
Windows 后端未实现本增量，不因此声明自然输入能力。

## 验证方法与结果

环境：Linux 7.0.0-38-generic x86_64、GCC 13.3.0、CMake 3.28.3，私有 Xvfb 与隔离
Portal Notify seam；native-release 为 native/pet + PP-OCR，Debug/ASan/UBSan 为
native/OCR OFF。Mira、Mirador、kairo 与 eui-neo pin 保持不变。

- input_cadence_test：128 个固定种子，全部阶段边界、重放、chord/滚轮总预算、拖动
  时间、无前向过冲与精确落点属性。
- x11_pointer_actions_test：独立 X 客户端收到的服务端事件时间，滚轮停留/步间隔、
  拖动按下/在途/到达/释放、chord 逆序释放、大小写文本、按住期间取消与焦点变化；
  保留原按钮、所有权、物理竞争、范围、超时/异常、接管及 Executor 关闭/拒绝用例。
- portal_input_test：独立 Notify 时间、2 倍缩放的曲线与最终落点、逐步滚轮符号总数、
  组合键与逐字间隔；完整计划不足/长文本在无事件阶段拒绝，以及已派发后取消、异常、
  超时、撤权与释放失败。零距离 Move 无输入；瞬时取消探针清理后仍回报 cancelled。
- desktop_image_supply_test：共享滚动/拖动新增起点缺失、到达目标变化及无手势派发
  负例；来源序号/epoch/条件验证复用继续回归。
- visual_marks/action_contract/desktop_action_policy 与既有视觉/文本工作流：新工具版本、
  自然接近与到达重验、可选条件及失败围栏保持一致。

首轮定向 10 项（8 用例 + setup/cleanup）中，X11 新增瞬时取消测试检出回执原因在
清理后丢失；修复动作内取消保留后，同组 10/10 通过。真实副作用与释放断言未削弱。
完整产品、最终扩展回归、全量 Debug 与 sanitizer 的执行结果见下方。

2026-10-20 补充：全量 Debug 首轮 100/102，另两项是旧 Atom 统一 1.0.0 断言，以及
1 ms chord 预算仍预期已派发后超时的旧用例。前者按文本 4.0.0 契约修订；后者保留
运行期超时/释放检查，改为先准入完整预算、真实 modifier down 后注入延迟，另加
1 ms 无副作用准入拒绝。两项及夹具 4/4 复验通过，没有以新准入拒绝取代运行期清理测试。

最终核对补充了文本规划的周期取消检查和单调预算前缀提前拒绝，避免对必然超预算的
大段文本继续生成无用时序；仍先验证全文字符/映射。新增 X11/Portal 规划阶段取消及
长文本无输入断言，并统一重建后执行最终回归。

实际构建与回归命令：

```bash
cmake --preset debug -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset debug -j 3 --target harness_desktop_tools_test desktop_atom_toolset_test
ctest --preset debug --rerun-failed --output-on-failure -j 2
cmake --build --preset debug --target mirage-boundary-check
cmake --build --preset native-release -j 3 --target mirage-native-product input_cadence_test portal_input_test x11_pointer_actions_test action_contract_test desktop_action_policy_test desktop_image_supply_test visual_marks_test visual_glide_tools_test smooth_move_schedule_test x11_smooth_move_test post_action_verification_test gui_fallback_test local_visual_workflow_test portal_stream_retention_test portal_screen_test
ctest --preset native-release -R '^(input_cadence|portal_input|x11_pointer_actions|action_contract|desktop_action_policy|desktop_image_supply|visual_marks|visual_glide_tools|smooth_move_schedule|x11_smooth_move|post_action_verification|gui_fallback|local_visual_workflow|portal_stream_retention)_test$|^portal_screen_fixture$' --output-on-failure -j 4
```

ASan/UBSan 沿用本轮开始前已配置的 native/OCR OFF 预设，各自执行（实际为独立构建目录
并行执行相同命令）：

```bash
for preset in asan ubsan; do
    cmake --build --preset "$preset" -j 3 --target input_cadence_test portal_input_test x11_pointer_actions_test desktop_image_supply_test visual_marks_test action_contract_test desktop_action_policy_test
    ctest --preset "$preset" -R '^(input_cadence|portal_input|x11_pointer_actions|desktop_image_supply|visual_marks|action_contract|desktop_action_policy)_test$' --output-on-failure -j 3
done
```

最终 ASan/UBSan 各 9/9（7 用例 + setup/cleanup）通过，无 sanitizer 报告；固定种子
属性矩阵 79108/79108。完整 native-release product 已重建（CLI/服务/托盘/native/pet），
CLI --version 可运行；初次及最终扩展回归均 17/17（15 用例 + setup/cleanup）通过。
最终 Debug 全量 102/102（含夹具）通过，无 skip。public header 边界检查 64 个头文件、
0 违规（保留 DEC-096 两条已登记 seam）。变更 C++ 范围的 clang-format、git diff --check
及受影响 Markdown/JSON 检查通过；未重跑 TSAN，本轮没有新增跨上下文共享状态或关闭路径。
上述测试通过后仅调整一处 C++ 表达式换行，四种配置重新构建成功；该纯格式调整没有
重复运行测试。产品进程在本次开始/结束时均未运行，未启动或重启用户产品。

## 验收限制

本轮仅在私有显示/协议夹具中发送输入，没有操作用户真实桌面或重启产品。真实
GNOME/其他 compositor 的在线模型滚动、拖动/拖放、快捷键、文本效果与人工手感仍
待 Mirage 维护者在授权、公开测试应用和模型就绪后按 M7-31 取证。测试不代替该门禁。
点击的按压/多击间隔未在本增量调整；独立修饰键、组合交叠、跨观察持键、IME 和
Windows 继续开放。
