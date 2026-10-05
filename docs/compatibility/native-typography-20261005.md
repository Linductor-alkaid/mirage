# 原生中文字体与会话基线：Linux验收

> 日期：2026-10-05
> 工作项：M6-12（Linux首步）
> 决策：[DEC-040](../decisions/DEC-040-native-typography.md)
> 负责人：Mirage维护者
> 环境：Linux x86_64；C++20/CMake；EUI-NEO dev 4691fc0a、Mira 13485151、内嵌Executor 2ae4fc89；依赖pin不变。

## ZCode字体依据

维护者本轮进一步要求选择与ZCode一样的字体。源码29628c9a的共享styles.css没有覆盖
--font-sans，沿用Tailwind4.2.2的ui-sans-serif/system-ui/sans-serif；Web bootstrap提到
Inter，但没有内置Inter字体。不能把官网或bootstrap的字体名称当成桌面中文实际字面。
本机fontconfig对system-ui匹配Noto Sans，对zh-cn无衬线匹配Noto Sans CJK SC（TTC索引2）。
这说明中文应选择SC字面；现用官方Noto Sans SC区域OTF对应同系列简体中文字形，避免
EUI仅加载TTC索引0的JP字面。未读取运行中ZCode的实际Rendered Fonts，也不宣称
Chromium与EUI逐像素一致或英文系统字面完全相同。Windows的ZCode系统回退随平台变化。

依据：[ZCode共享样式](https://github.com/zai-org/ZCode/blob/29628c9acdb81b703bbd4080c207a0e7ce5e276e/packages/ui/src/styles.css)、
[Tailwind字体栈](https://github.com/tailwindlabs/tailwindcss/blob/v4.2.2/packages/tailwindcss/theme.css)、
[Noto官方区域版本](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/README.md)。

## 实际变更

原界面按系统文件选择字体，本机命中NotoSansCJK-Regular.ttc的第0字面。现在固定使用
Noto Sans SC Regular：官方简体中文区域字面2.004；
中文/拉丁/数字/标点由同一字面承担。保留现有字号（正文16EM/24px）、布局、主题、图标
和代码monospace；pinned EUI的fontWeight仍不切换独立粗体文件，不宣称交付了新字重。

EUI把请求字号乘1000/1448再加载Noto SC，直接传16会缩小字形。typography.hpp
将设计EM字号乘1.448交给EUI（正文请求23.168），所有绘制/测量/换行/输入/选区使用
同一换算。正文行高24保留；输入高度最小52、内边距12，单行无需滚动；composer测量与InputBuilder同用请求字号乘1.2，约27.80px；
Markdown标题行高依EUI为请求字号加6。图标/代码不套中文换算，行内代码还原独立字号并
整段居中，按恢复后的实际字体测量缩小背景和移动同一行后续段；保留上游保守换行/高度预算。原生测试断言字体EM/ascender/descender，避免升级后
静默套错系数。见EUI-20261005-007。

第二个原因是Markdown将中文拆为单字，每字用Center对齐各自的ink bounds，导致“口”、
标点与较高文字上下跳动。UI Adapter经公开DSL把行内.text改为Top并使用统一行框内边距，
包括带背景或装饰的段；标签和图标继续整体居中。没有改动第三方代码、Mira或运行服务，
没有增加并发路径。[本地EUI反馈](../dependency_feedback/eui-ledger.md#eui-20261005-006markdown行内段按各自轮廓居中导致基线跳动)。

字体采用官方简体中文区域版本，不做应用专用删字子集，未改字形/度量/名称。仓库以5838016字节的xz/tar保存，
CMake离线解包出8331336字节的OTF，配置时同时校验归档与字体SHA256。Linux DEB及Windows
NSIS资源包含字体与OFL许可。来源、精确提交、摘要、许可见
[provenance](../../apps/native/assets/fonts/provenance.json)和[OFL](../../apps/native/assets/fonts/OFL-NotoSansSC.txt)。
上游：[Noto Sans SC](https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/SubsetOTF/SC)。

## 已执行验证

| 验证 | 命令/范围 | 结果 |
| --- | --- | --- |
| Debug/Release构建 | 对应preset的mirage-native、native_conversation_view_test | 通过，离线字体解包/复制成功 |
| Debug相关回归 | ctest --preset native-debug -R 'native_chat_model_test\|native_conversation_view_test' --output-on-failure | 2/2通过 |
| Release相关回归 | 同两项，native-release | 2/2通过 |
| ASAN构建/回归 | 同两项，asan；默认泄漏检查保持开启 | 2/2通过，无sanitizer诊断 |
| 原生渲染与交互 | native_conversation_view_test / 自有Xvfb+EUI Runtime+GLFW/OpenGL | 44 checks / 0 failures；真实字体加载/字形覆盖、悬停、选段、编辑/取消、滚动后选行通过 |
| 视觉检查 | 1180×800、860×620，明暗、混排、Markdown、输入/编辑 | 字面和基线一致，无新文字溢出；12帧见下 |
| Linux DEB | 开启MIRAGE_ENABLE_PACKAGING，构建产品目标，cpack，再dpkg-deb提取 | 完整字体摘要、OFL逐字节一致；无旧装饰字体；生成后恢复本地packaging OFF |
| Windows资源 | NSIS清单与CMake目标资源核对 | 两项已同步；未执行Windows构建/安装/渲染 |
| 公共头边界 | cmake/BoundaryCheck.cmake | 49 headers / 0 violations |
| 格式与Git差异 | cmake/CheckFormat.cmake；git diff --check | 通过 |
| 凭据/文件检查 | 仅文件内容扫描与大小门禁 | 无凭据命中、唯一字体归档5.6MiB，来源/摘要/许可核验；无其他大文件 |

本次没有重新运行后端完整回归、UBSAN/TSAN或在线模型调用：只改变UI字体/对齐与资源配置，
没有状态/协议/并发变更；上一轮证据不冒充本轮结果。

## 截图与可复跑入口

```sh
cd build/native-debug/apps/native
../../tests/native_conversation_view_test /tmp/mirage-typography-noto-20261005
```

自身GL framebuffer输出PPM后无损编码PNG；合成会话不连接模型，不操作用户桌面。
混排场景同时显示用户正文、Markdown普通/强调/链接/代码、简繁字符与输入框。字体缺失时
FreeType加载断言失败，不把系统回退作为成功。此次截图不等同于真实IME或高DPI验证。

- [混排正常浅色](../../.impeccable/review/typography-20261005/mixed-normal-light.png)
- [混排正常深色](../../.impeccable/review/typography-20261005/mixed-normal-dark.png)
- [混排最小浅色](../../.impeccable/review/typography-20261005/mixed-minimum-light.png)
- [混排最小深色](../../.impeccable/review/typography-20261005/mixed-minimum-dark.png)
- [实际编辑输入](../../.impeccable/review/typography-20261005/editing-last-input.png)
- [同一普通会话的新字体](../../.impeccable/review/typography-20261005/normal-light.png)
- [该会话上一轮字体](../../.impeccable/review/conversation-20261005/normal-light.png)

impeccable finish-reviewer/documenter采用内联降级角色，未使用独立agent；复核仅覆盖字体
变更，初次Noto复核fix指出字号缩小与记录问题，换算后两项resolved；随后修正输入滚动条
和代码背景两个回归，最终disposition=ship。四份DESIGN/JSON保留既有视觉规则，仅更新实际字体/基线和资源
来源。[初次复核](../../.impeccable/review/typography-20261005/finish-review.md)、
[评分](../../.impeccable/review/typography-20261005/finish-verdict.md)、
[文档提取](../../.impeccable/review/typography-20261005/documentation.md)。

## 未执行与限制

- Windows窗口、打包运行与真实中文IME：未执行，当前Linux环境；负责人维护者；补跑条件
  Windows native preset、NSIS安装及真实IME编辑/选段环境。
- 高DPI/跨屏与真实桌面输入：未执行；负责人维护者；补跑条件对应显示器与桌面环境。
- 多字体间精确ascender匹配、缺失字形/emoji回退、代码字体跨主机一致性：未宣称；代码
  仍用系统monospace，主字体之外由pinned EUI回退。
- 上游既有链接中文字间距/保守断行仍由EUI及既有Adapter控制，本次没有扩大间距修复范围。
- 当前已运行的前端需重新打开以加载新资源；整体入口/托盘生命周期继续挂M6-04。
