# 会话菜单仅三个模式（2026-10-12）

> 状态：Completed（Linux 本地）
> 工作项：M6-38
> 决策：[DEC-080](../decisions/DEC-080-conversation-desktop-access-modes.md) 同日澄清
> 负责人：Mirage 维护者

维护者要求会话页面的选择中只有三个选项。菜单现在仅显示对话模式、视觉模式、
完全访问，尺寸 280×120，选择后收起。删除该菜单的桌面授权状态、管理按钮、说明、
分隔线和滚动区，也删除无用滚动状态。打开菜单不查询系统授权。

每轮能力限制、运行/提交/授权等待期间门禁和完全访问选择的既有授权联动保留；
管理入口仍在设置页。管理响应的说明回到现有会话提示，不引入菜单里的设置区域，
不使旧授权提示覆盖会话任务错误。未修改服务、平台、持久偏好契约、依赖或异步生命周期。

Native Release 构建及定向 CTest 6/6：native renderer、ChatModel、native Agent、
persistence 四业务测试，加私有状态 setup/cleanup。原生 OpenGL 渲染 24,005 检查，
零失败；真实指针选择视觉/对话后收起，保存模式与草稿保留，完全访问离线失败提示，
等待/运行时拒绝切换均通过。测试自建私有 XDG 和 Xvfb，不连接用户 Runtime。
1180×800 浅色与 860×620 深色成组视觉检查通过，菜单无授权区域：
[浅色](data/conversation-mode-selector-20261012/composer-three-modes-light.png)、
[小窗口深色](data/conversation-mode-selector-20261012/composer-three-modes-small-dark.png)。
Impeccable 源码检测一次无报告项，git diff --check 通过。

最初测试构建因局部 bounds 遮蔽既有变量被 -Werror 拒绝，已重命名。首次渲染断言
使用了要求对象必须存在的 helper 来检查已删除控件，导致八条夹具失败；改为直接
查询不存在对象后通过。失败日志保留 /tmp/mirage-m6-38-compact-{build,ctest}.log，
最终日志分别为 build-verified、ctest-verified、ui.log；未扩大测试期限或删除应有断言。

DEB 重建到 out/packages/mirage_0.1.0_amd64.deb，四个包内可执行 ELF 与最终 Release
SHA-256 匹配。未安装、重启、终止用户进程或发起真实系统授权。
本次只改 UI，未重复上一轮服务/平台全量 sanitizer 验证；其范围和 GNOME 待验项仍见
[前轮链路证据](conversation-access-and-cua-continuity-20261012.md)。Windows 物理界面
未验；M7-10 实机门禁不据 Linux 渲染关闭。未提交/推送。

复现（先构建；直接渲染时工作目录为 build/native-release/apps/native）：

```sh
cmake --build build/native-release --target mirage-native native_conversation_view_test -j2
ctest --preset native-release -j2 -R '^(native_conversation_view_test|native_chat_model_test|native_agent_integration_test|persistence_test)$'
../../tests/native_conversation_view_test /tmp/mirage-m6-38-compact-ui
cmake --build build/native-release --target package
```

机器摘要：[summary.json](data/conversation-mode-selector-20261012/summary.json)。

2026-10-12 同日后续：菜单锚点与模型默认/任务预算继续修正，当前包和最终几何
见[后续验收](reasoning-and-menu-anchor-20261012.md)；本记录图片与 SHA 保留为当轮快照。
