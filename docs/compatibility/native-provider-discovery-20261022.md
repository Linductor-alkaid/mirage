# 原生窗口与应用清单修复验证

> 状态：Completed（Linux Backend 限定范围）
> 日期：2026-10-22
> 负责人：Mirage 维护者
> 工作项：M7-18、M9-01/12
> 基线：a4c73d7 后续工作树
> 依据：DEC-015、DEC-076、DEC-081

最近会话重复返回空窗口表和 ChatGPT running=false。AT-SPI 实机树的 desktop
直接子节点是 application；旧 Provider 只检查这一层的 frame/window/dialog。
本次遍历 application 的直接顶层窗口，保留直接窗口兼容；不遍历完整控件树。
访问节点上限 4096，与 max_windows 的输出上限分开；超额和取消不返回部分清单。
不改变 AT-SPI 激活能力或声明 Wayland 绝对几何已校准。

本机优先生效的 desktop Exec 使用 env 包裹实际命令，旧身份取到了 env。现在有界
解开赋值、-i、-u/--unset、--；未知选项不给出可执行身份。应用清单允许 ASCII
大小写等值匹配；running_state、launch 去重和 terminate 的 PID 选择保持精确匹配，
不以报告性别名扩大控制能力。

隔离 AT-SPI fixture 新增 Component 协议及原生 Provider 用例；应用 fixture 验证
外部 env 实例和大小写清单别名，后者不能成为终止目标。补 application_backend_test
到 m205_process_helper 的构建依赖，避免定向构建缺失 helper。

Debug 全量 110/110 通过；ASan/UBSan/TSan 各包含两项 Provider 测试的专项 8/8
通过，native-release 专项 9/9 通过。TSan 使用 setarch x86_64 -R，无新增 suppression。
只读实机探针链接当前 Debug 库，返回 windows_ok=1、windows=13、apps_ok=1、
chatgpt_found=1、chatgpt_running=1，退出 0；不输出窗口标题/用户正文，不执行输入。
日志位于 /tmp/mirage-native-provider-probe.log。未重启用户运行中的旧托盘，未执行
真实窗口裁剪/激活、Windows 或长时间 soak；M7-18/M9 整项仍保持开放。

## 同日后续：窗口枚举到语义快照

M7-18 将纯 Wayland 的同一个 AT-SPI WindowProvider 传给 Accessibility Backend，
修复已枚举窗口仍 not_found 的接线。activation_supported=false 同时约束 Workflow
与 Harness 的激活工具注册；同标题多 ID 的 v1 映射拒绝快照，不任取首个窗口。

本轮组合工作树的 atspi_backend_test / observation_e2e_test 在 Debug、ASan、
UBSan、TSan、native-release 各 4/4（含隔离夹具）；TSan 仅 setarch -R、既有
GLib suppression。最后 native-release 产品已重建。原始日志前缀为
/tmp/mirage-recovery-atspi-。宿主只读枚举成功 7 个窗口，第三个候选的语义快照
463 节点；仅保存计数，不证明 Firefox/Snap 控件树可用。真实跨平台及稳定对象
身份继续按 M7-18 开放。
