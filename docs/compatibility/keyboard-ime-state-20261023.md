# 键盘与输入法增量验证（2026-10-23）

> 状态：In Progress
> 负责人：Mirage 维护者
> 关联：DEC-110、M7-26/28/31
> 工作树：`feat/keyboard-ime-state`，本轮未创建提交

## 交付与证据等级

新增平台中立 InputMethodProvider 与只读 `desktop.input.state` 1.0.0。Linux 桌面
surface 启用时提供查询；native Wayland 不依赖全局 InputProvider。无 GIO 构建不提供
能力；有 GIO 无 IBus SDK 时仍支持 Fcitx5 和显式 IBUS_ADDRESS。IBus SDK 仅供当前
登录会话的地址文件路径发现，没有 IBus 对象、watcher 或新生命周期 owner。

| 证据 | 可声明范围 |
| --- | --- |
| 私有 D-Bus 服务 | Fcitx5/IBus 协议映射、known/unknown、双层 variant、输入预算、取消、异常回包、服务退出/换代、地址传输守卫；不证明真实输入法模式 |
| 当前桌面只读查询 | 实际 IBus GlobalEngine=libpinyin 与 CurrentInputContext 已读取；转换模式保持 unknown，无用户输入或设置变更 |
| 私有 Xvfb | Shift、-/= 和数字键实际送达、停留与释放；不证明候选窗口行为 |
| Portal 派发夹具 | 同一词表及按下/释放映射；不证明 compositor 或候选支持 |
| 受控 ModelLayer + OCR 夹具 | 最新 OCR 结果驱动中英切换、短拼音、正反翻页、按当前页编号选字、独立核对输入框；不是真实 OCR/自主模型准确率验收 |

各状态字段携带 known/unknown、source、captured_at_ms、reason。Fcitx5 State 只表示
**最近**上下文的激活状态，零为 no_context；IBus context path 不等于窗口 ID，GlobalEngine
不等于当前中英模式。两框架分别投影，不能推断目标应用使用哪个框架。当前 layout、
conversion_mode、focused_window、preedit_active、candidates_visible 保持 unknown。
没有预编辑、候选或用户正文采集；字段不是原子快照，也不能替代新的输入授权/业务验证。

视觉策略复用现有截图、Mirador OCR 与 Mira Harness：看中/英，按当前配置点 Shift
并复观；中文输入短拼音，读取本页候选和显示编号，-/= 翻页后复观，再按当前编号
选字，最后核对目标输入框。未读清、页重复/无变化、焦点变化和未决预编辑需重新
规划；输入法自定义快捷键依其配置，不静默改偏好/剪贴板。候选提交不等于表单提交。

## 环境与复现

环境：Ubuntu 24.04.5 LTS，Linux x86_64，GCC 13，C++20；GIO 2.80.0，IBus SDK
1.5.29-rc2。依赖仍沿 dependencies.lock.json 原 pin；没有修改 third_party。
平台查询为现有 Executor 有限任务内的同步 continuation：独立 GLib context 只接收
平台完成回调，callback 仅保存 result；总预算默认 1 s，单次 D-Bus 查询 200 ms，
取消/期限后消费完成结果再退出，没有自建线程/队列/订阅/调度器。地址限制本地 unix，
不触发 D-Bus 服务自动启动，不回显远端错误/地址。延迟为协作式，未宣称硬期限。

```bash
cmake --preset debug
cmake --build --preset debug --target input_method_state_test desktop_image_supply_test x11_pointer_actions_test portal_input_test desktop_atom_toolset_test harness_desktop_tools_test provider_contract_test input_session_contract_test -j 4
ctest --preset debug -R '^(input_method_state_fixture_test|desktop_image_supply_test|desktop_atom_toolset_test|provider_contract_test|input_session_contract_test|portal_input_test|x11_pointer_actions_test|harness_desktop_tools_test)$'
```

测试执行结果见本节后续记录。首次扩大夹具时修正了两处测试问题：XStringToKeysym 的
命名键符与单字面字符不同，以及 ModelProvider 输出的 digest 应按其 wire payload
计算；生产解析/派发无需为此修改。负向 Provider 取消由 pinned registry 直接返回
Cancelled，而非失败回执，断言按公开契约修正。历史失败不算通过。

## 尚未验收

- 真实 IBus/Fcitx5 的“中/英”指示、候选窗口截图/OCR、-/= 自定义配置与最终中文落字。
- 真实应用切窗/预编辑恢复、焦点归属、不同布局和中英文混输。
- 真实 compositor Portal 交互、在线模型自主选字和 Windows TSF。

负责人均为 Mirage 维护者；补跑条件为目标输入法/应用/显示会话与 OCR 模型可用，使用
实际键盘工具逐步完成指示确认、正反翻页、数字选字、切窗复观及输入框验字，保留脱敏
截图/回执/版本信息。当前受控夹具及 IBus 只读查询不能关闭上述门禁；M7-28/31 保持开放。

## API 依据

实现核对本机 IBus 公共头（ibusshare.h 的 socket path）、pinned Mira/mirador 公开
接口和 kairo integration 的 tasks-and-lifecycle card。输入法属于 OS Backend 适配，
依赖没有对应桌面输入法查询能力，不构成依赖能力缺口。

协议语义以 [IBus 1.5.30 的公开服务实现](https://github.com/ibus/ibus/blob/1.5.30/bus/ibusimpl.c)、
[Fcitx5 的 D-Bus 接口实现](https://github.com/fcitx/fcitx5/blob/master/src/modules/dbus/dbusmodule.cpp)
和 [Fcitx5 State 的最近上下文语义](https://github.com/fcitx/fcitx5/blob/master/src/lib/fcitx/instance.cpp)
交叉核对。工程实现没有复制上游源码或更改输入法配置。

## 最终执行结果

基础 HEAD `2af42c5` 上的本轮工作树；GCC 实测为 13.3.0。最终 Debug 选择的 8 个
功能测试全部通过，连同隔离状态 setup/cleanup 为 CTest 10/10（73.73 s）。

| 配置/检查 | 实际结果 |
| --- | --- |
| Debug：上述 8 个功能测试 | 全部通过；包括视觉模型/OCR 正反翻页、Xvfb 键送达、Portal、状态契约、Harness 回归 |
| ASAN：desktop_image_supply_test、portal_input_test、input_method_state_fixture_test | 分批 3 项均通过，无 sanitizer 诊断；状态项修正取消断言后最终 1/1（2.50 s） |
| UBSAN：同 ASAN 的 3 项 | 分批 3 项均通过，无 sanitizer 诊断；状态项最终 1/1（2.13 s） |
| TSAN：desktop_image_supply_test、input_method_state_fixture_test | 默认 ASLR 下启动失败（unexpected memory mapping）；按 CI 关闭 ASLR 后 2 项均通过，含 setup/cleanup 为 4/4（68.33 s），无 TSAN 诊断 |
| native-release | mirage-service 与 mirage-native 实际构建通过；没有安装/重启现有进程或生成新发布包 |
| 无 GIO stub / 有 GIO 无 IBus SDK | 两种宏组合对实际 Backend 源码执行 C++20、-Wall/-Wextra/-Werror 语法编译，退出 0；这是源码编译证据，不是运行验收 |
| 格式、文档与差异 | git clang-format 报未修改文件；5 份变更文档的链接/一级标题/围栏检查通过；git diff --check 通过 |

Sanitizer 与产品构建的复现命令：

```bash
cmake --preset asan
cmake --build --preset asan --target input_method_state_test desktop_image_supply_test portal_input_test -j 3
ctest --preset asan -R '^(input_method_state_fixture_test|desktop_image_supply_test|portal_input_test)$'
cmake --preset ubsan
cmake --build --preset ubsan --target input_method_state_test desktop_image_supply_test portal_input_test -j 3
ctest --preset ubsan -R '^(input_method_state_fixture_test|desktop_image_supply_test|portal_input_test)$'
cmake --preset tsan
cmake --build --preset tsan --target input_method_state_test desktop_image_supply_test -j 2
setarch x86_64 -R ctest --preset tsan -R '^(input_method_state_fixture_test|desktop_image_supply_test)$'
cmake --preset native-release
cmake --build --preset native-release --target mirage-service mirage-native -j 3
```

状态夹具还实际验证了 IBus 服务换代后整项证据退役，以及 unix socket 已连接但
D-Bus 认证一直不完成时的总超时/运行中取消；无地址回显或旧引擎缓存。IBus 地址
发现启用时服务新增系统运行库 libibus-1.0-5：实际 `ldd` 无缺失，使用临时最小
Debian control 对 native-release 服务执行 `dpkg-shlibdeps -O -e <绝对二进制路径>`
退出 0，结果包含 libibus-1.0-5；现有 CPack SHLIBDEPS 自动检测可承载该系统依赖，
没有新增 pinned 仓库，也未更新/安装现有发布包。

## 同日后续：ROI OCR 复合工具

维护者追加的自动输入工具、真实候选/落字测试及其限定成功率由
[ROI OCR 输入法工具验收](roi-ime-fill-20261023.md)续接。该私有 IBus/GTK 证据更新了
上文候选/落字未验范围，不关闭实际中/英标志、Portal、Windows 和在线规划门禁。
