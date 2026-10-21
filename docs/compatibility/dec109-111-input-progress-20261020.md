# DEC-109～111：后台变化准入、独立修饰键与文本值契约（2026-10-20）

> 状态：本轮有限增量构建与回归通过；未关闭三个专项的真实平台/模型门禁
> 负责人：Mirage 维护者
> 工作项：M9-07、M7-25/26/32；M7-27/31/33/34 保持开放
> 基线：e63c8de + feat/desktop-input-policy 工作树（包含前一轮完整自然时序）
> 依据：DEC-109/110/111、DEC-054/056/064/077

## 实现与边界

### Shift 完整点按

`desktop.session.key` 的 `key_name="shift"` 与 `desktop.input.key` 的 `key="shift"`
执行一次完整点按：逻辑左侧 Shift down → 本地 45～85 ms 自然停留 → up → 回执。
裸 ctrl/alt/meta 同样接受。既有 ctrl+shift+t 组合仍按阶段派发和逆序释放；
ctrl+shift、shift+shift 等修饰键结尾组合拒绝。基词表仍不把修饰键当普通字符，
左右侧名称与跨观察 down/up 属于尚未接线的受控会话能力。公开完整工具沿用本轮
尚未发布的 4.0.0，不新增模型时间字段。

X11/Portal 复用当前完整动作的 ownership、自然时序、取消和释放。X11 物理 Shift
已按住时在输入前拒绝，不能释放用户拥有的键；输入后取消立即释放本动作拥有的键，
取消事实不因探针在 release 后恢复 false 而丢失。Win32 只同步四个键的静态映射，
本机未编译/运行 Windows，不声明 Windows 完整动作/自然时序已交付。

### DEC-109 / M9-07 后台变化准入

VisualSessionHost 增加显式 `when_changed` 策略及非零 analysis_policy_revision，
默认 `always`；可信宿主在阶段参数、目标位置或授权变化时推进 revision。跨版本
完整执行。启用变化分析且成功建立基线后，以 Mirador 公开签名/差分 API 比较最后
完整分析与本帧；跳过不会推进基线，连续小变化达到阈值后可触发分析。

无变化返回独立 kSkippedUnchanged：OCR、检测、几何、模板探测、融合均不执行，
没有当前快照/正文/marks，不把空结果当“目标消失”，也不把旧对象升级为当前证据。
Refresh、模板登记、marks 强制执行；默认观察与现有动作/核验路径不启用后台近似
筛选。取消/期限/失败/普通分析清除背景基线，重试不能被先前失败的 change 推进吞掉。
未知策略、缺少 revision 或关闭变化分析时明确拒绝。

新增签名每源固定 64×64 灰度，持久像素预算 4096 字节，比较另需一个等大的临时
签名，不保留整屏副本；占用通过 cache_stats.background_signature_bytes 报告。
固定关闭 dHash 提前退出，采用 Mirador 默认块阈值。这仍是近似后台筛选，不证明
微小文字或颜色状态未变，不能授权输入或证明目标值。相邻帧 change 与 background
admission_change、admission_baseline_sequence 分开记录。

没有新增 producer、线程、队列、owner 或周期模型调用。沿同一受管 blocking worker
与 LatestMailbox 接纳、取消、settle 和 stop；没有修改任何 pinned 代码。真实后台
消费者、无需求停产、静态退避、ROI 增量及全产品资源门禁尚待接线，不能据此声明
当前生产 OCR/检测次数或 CPU 已下降。

### DEC-111 / M7-32 平台无关目标值首个契约

新增 AccessibilityProvider 独立 opt-in 的 read_text_value SPI，默认显式不支持；
不从 set_text、树可用性或系统平台推断支持。一次 verify_text_value 仅向 Provider
传递确切 scope/ref 与预算，不传预期值；要求同一目标、本次调用内的新单调采集
区间、完整/已提交/非受保护的有效 UTF-8 值。大小写、空格、换行、顺序严格比较，
空值有效。预期值与正文各最多 4096 字节，整个调用最多五秒且受更短预算/取消约束。

结果 v1 区分 matched/mismatched/verification_unavailable，只含身份、来源、
时间及固定原因码，不保留实际或期望正文，不回传平台异常/错误中的敏感文本。
只读重验重新读一次，无轮询、OCR、复制、输入或模糊放行；不能清除输入执行未知、
任务、权限或 epoch 围栏。

此阶段没有启用 AT-SPI/UIA 真实读值：可靠控件/窗口 incarnation 与焦点、IME 提交、
权限及任务/epoch 接线尚未完成。没有注册 text_value_equals wire 条件或改变默认
业务验证。M7-32 保持 In Progress，复制事务 M7-33 与选路/真实应用 M7-34 保持
Planned，不以纯比较器测试冒充跨平台目标文本验证完成。

## 验证

首轮定向 8/8（6 用例 + state setup/cleanup）通过，之后扩充了 Shift 占用/取消、
策略 revision、累计微变和资源字节断言。完整回归与 sanitizer 结果见下方最终记录。

- provider/input_session：新旧词表边界，modifier tap 与 base/session 键区分。
- X11/Portal：Shift down/up 次数、45 ms 策略下限（整毫秒观测允许 1 ms 舍入）、物理占用拒绝和运行期取消释放；
  既有完整 scroll/drag/chord/text 及失败矩阵继续回归。
- visual_session：ReadOnly 空缓存下，首帧 OCR/detector 各一次，静态门控后调用数
  不增；Refresh 强制再调用，revision 换代重新分析。累计每帧灰度 +2，最后相对
  基线 +8 时执行，跳过期间基线 sequence 不变；失败/取消/期限后重新执行。
- target_text_value：真实值相等/错误/空白/大小写/空值/UTF-8/4096 字节，身份与
  scope 替换、旧/倒置/未来采集时间、超限/不完整/预编辑/受保护值、异常、取消、
  期限和独立重验；不发送输入，不从其他字段或 OCR 推导相等。

实际构建与测试命令（sanitizer native/OCR 关闭，native-release 产品配置保持开启）：

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset debug --target mirage-boundary-check
cmake --build --preset native-release --target mirage-native-product target_text_value_test provider_contract_test input_session_contract_test visual_session_test portal_input_test x11_pointer_actions_test visual_observation_loop_test desktop_image_supply_test -j 3
ctest --preset native-release -R '^(target_text_value|provider_contract|input_session_contract|visual_session|portal_input|x11_pointer_actions|visual_observation_loop|desktop_image_supply)_test$' --output-on-failure -j 3
```

ASan/UBSan 各自执行同一组八个用例及其自动夹具：

```bash
cmake --build --preset asan --target target_text_value_test provider_contract_test input_session_contract_test visual_session_test portal_input_test x11_pointer_actions_test visual_observation_loop_test desktop_image_supply_test -j 3
ctest --preset asan -R '^(target_text_value|provider_contract|input_session_contract|visual_session|portal_input|x11_pointer_actions|visual_observation_loop|desktop_image_supply)_test$' --output-on-failure -j 3
cmake --build --preset ubsan --target target_text_value_test provider_contract_test input_session_contract_test visual_session_test portal_input_test x11_pointer_actions_test visual_observation_loop_test desktop_image_supply_test -j 3
ctest --preset ubsan -R '^(target_text_value|provider_contract|input_session_contract|visual_session|portal_input|x11_pointer_actions|visual_observation_loop|desktop_image_supply)_test$' --output-on-failure -j 3
```

初次三种定向构建误写不存在的 visual_pipeline_test，Ninja 在编译前拒绝；按实际
CMake target 改为 visual_observation_loop_test 后重建。该命令错误不计为测试失败，
也不计为构建成功。

首轮 native-release/ASan/UBSan 定向各 9/10：新加入的 Portal Shift 取消用例沿用了
文本的 15 秒预算，Key 预算校验正确拒绝，未产生输入。修正夹具：先显式断言该
无副作用拒绝，再改为 3 秒按键预算，保留真实 down 后取消、up 清理与回执断言。
未修改产品预算或取消实现来迎合用例。

最终 Debug 全量 103/103（含夹具，71.59 秒）通过，无 skip。native-release、
ASan、UBSan 聚焦各 10/10（8 用例 + state setup/cleanup），耗时分别为
15.22 / 17.28 / 15.37 秒，sanitizer 无报告。关键矩阵：target_text_value 115、
visual_session 281、X11 pointer/actions 929、Portal input 558 检查均零失败。

四种配置最终构建成功；完整 native-release product（CLI/service/tray/native/pet）
已更新，CLI --version 可运行。Provider vtable 与分析 Result 的 C++ 布局改变，
调用方需要重编译；本轮产品和所测用例均已重编译。产品进程本轮未启动/重启，
只在私有显示/协议夹具中发送输入。

公开头文件边界检查 65 个头文件、0 违规，保留 DEC-096 两条已登记 seam；变更
C++ 范围及新文件共 34 份的 clang-format 检查通过。git diff --check、受影响
JSON、11 份 Markdown 的成对 fenced blocks 及 415 个本地链接检查通过；依赖
pin 和 lock 未改动。本轮没有新增跨上下文共享状态/关闭路径，后台基线只在已有
串行 worker 访问，未重跑 TSAN；不以 ASan/UBSan 宣称并发竞态验收完成。

## 仍需验收

没有向用户真实桌面发送输入。真实 GNOME/其他 compositor、在线模型与人工手感、
Windows 编译/运行、跨调用 Shift hold、IME、目标控件读值、受控复制及 B 站任务均
未执行，不声明完成。负责人 Mirage 维护者；各平台实现与授权测试应用/设备就绪后
按 M7-27/31/32～34 和 M9-07/11/12 逐项补跑。
