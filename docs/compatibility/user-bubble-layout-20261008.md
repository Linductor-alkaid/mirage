# 用户消息气泡换行验收

日期：2026-10-08。负责人：Codex。工作项：M6-32。依据：DEC-050、DEC-040、DEC-048。

维护者截图显示宽页面上的“你会做什么”被拆成两行，末字落在气泡外。
修复前独立字形复现：逻辑内宽69.9997px、高22px，在1×为22px高，在2×实际为44px逻辑高。
逻辑字号的静态测量与实际物理字号的排字结果不同，导致错误换行及高度不足。

新实现复用EUI公开TextPrimitive实际布局；自然宽度按物理像素向上取整加1px，
正文明确使用相同maxWidth与实际行高。普通文字测量统一到UI拥有的128条/256KiB缓存。
未修改third_party、字号、依赖锁定或并发设施。

```bash
cmake --build build/native-release --target mirage-native native_conversation_view_test -j 2
(cd build/native-release/apps/native && ../../tests/native_conversation_view_test /tmp/mirage-bubble-final)
python3 .impeccable/review/conversation-process-20261007/instrument.py asan
ctest --test-dir build/native-release --output-on-failure -j 2
cmake --build build/native-release --target mirage-format-check mirage-boundary-check
git diff --check
```

原生渲染20,013 checks、0失败。新增112组实际字形场景：1180×800/860×620，
明暗主题，1×/1.25×/1.5×/2×，七类文字（五字中文、标点、混排、长西文、长中文、
显式/末尾换行、emoji与多行）。独立排字验证正文宽高、气泡包含性、12/8px留白和选择区域。
五字短句在所有组合中必须为一行；既有指针、选中、复制、设置和会话交互回归保留。
截图测试显式完成GLX缓冲区resize，并使用足以容纳2×窗口的Xvfb画布。

最终合成会话2×截图已目检：
[浅色](../../.impeccable/review/user-bubble-20261008/bubble-short-light-2x.png)、
[深色](../../.impeccable/review/user-bubble-20261008/bubble-short-dark-2x.png)。
未使用维护者真实会话作为提交附件，也未调用外部模型。
本机日志：/tmp/mirage-bubble-final-build.log、/tmp/mirage-bubble-final-render.log、
/tmp/mirage-bubble-final-checks.log。最终验证结论见下。

平台边界：已验证Linux X11/Xvfb软件OpenGL的真实物理尺寸及缩放布局。
Windows/Wayland实际显示器、多显示器DPI迁移未验证；负责人Codex，补跑条件为对应显示环境。
最终CI与合并记录见[PR#75](https://github.com/Linductor-alkaid/mirage/pull/75)。

最终native-release全套52/52通过（26.74秒）；原生渲染20,013、UI模型388、
集成303 checks，均0失败。ASAN/UBSAN对三个目标的全部自研依赖翻译单元插桩通过，
复用pinned Release库，EUI无RTTI故排除vptr；不声明全依赖/vptr覆盖。
日志：/tmp/mirage-bubble-final-all-{build,tests}.log、/tmp/mirage-bubble-final-asan-ubsan.log。
格式、52个公开头边界、六份受影响文档链接与diff检查通过。
完整Debug TSAN集成303 checks、24.80秒，无诊断/抑制规则，参见M6-31补验。
