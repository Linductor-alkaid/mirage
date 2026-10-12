# 本地 OCR／模板 Workflow 预览（M7-11 / DEC-069）

Linux Xorg 原生产品默认视觉包提供 OCR；完整 Window/Screen/Input、共享动作 owner 与
运行中的视觉管线均存在时，workflow 目录显示 desktop.knowledge.visual_click。
无这些能力时不会注册。它暂不出现在模型会话工具目录中。

知识保存精确 window_title、page_text、target_kind、target、expected_text，执行前取得
窗口内新 OCR/模板证据，唯一定位目标，再走现有编号点击与新 OCR 核验。不会保存
坐标、@v、编号或 scope；整个已知步骤无需模型。窗口标题加唯一 OCR 页面文字用于
页面校验，不等于进程身份认证。目标文字/页面置信度至少 0.8，模板区域置信度至少 0.9；
模板 ID 还需已有 Mirador VisualIndex 资源命中。低置信度同名目标同样构成歧义。

示例对应公开 Canvas fixture，其页面为 Document、目标 Save、成功状态 Saved，
必须改成目标应用实际新观察中可唯一识别的文字。先将目标窗口放到前台；已有权限判定
仍会执行。不要对副作用步骤设置 Retry/循环或自动重发不确定结果。

```sh
./build/native-release/apps/mirage workflow atoms
./build/native-release/apps/mirage workflow publish --file docs/examples/local-visual-save.workflow.json
./build/native-release/apps/mirage workflow run 75c0d069a3be49a99297e83c65ce7110 --policy recoverable
./build/native-release/apps/mirage workflow runs
```

publish 为 DryRun，不产生点击。成功为宿主固定生成的 canonical JSON 字符串；示例
Contains `"verified":true` 仅用于 Mira 当前标量谓词兼容，真正证据由复合原子内部取得。
expected_text 先前已存在、页面/目标重名、超预算、焦点/标题/几何/像素变化等都明确失败。
失败交为 waiting_agent 时当前仍需明确干预，不会自动选择知识或启动模型恢复。

目标为模板时，将 target_kind 改为 template、target 改为已装配清单中的精确模板 ID。
默认包不携带应用专属模板；沿用 DEC-061 的已校验本地模板资源清单即可，不能把图标
编号当模板 ID。模板只提供外观事实，执行权限仍由本地知识及动作门禁判断。

原始窗口 ROI／PNG 的 8 MiB 和 128 区域预算不变；大窗口拒绝时缩小受控窗口后重新
观察。不会把屏幕预览的缩放坐标转成点击目标。本地解析、点击和后观察合计受五秒 owner
预算约束；后条件必须由新 OCR 证明，不能用缓存旧结果。首次后观察早于重绘时最多
追加两次只读新 OCR，共享五秒上限，不重复点击；更慢的应用更新仍可能返回未验证。

IR 文件可持久保存在本地，当前 workflow 版本目录仍为进程内投影；重启后重新发布。
原生会话选择、自动恢复、跨重启资产恢复和多页 AppModel 导航留在后续范围。私有验收：

```sh
python3 tests/manual/local_visual_workflow_acceptance.py --build build/native-release --output /tmp/mirage-local-visual-workflow
```

该脚本在私有 XDG/DBus/Xvfb 中运行实际产品，先用默认包 OCR，再用公开模板资产运行
位置交换正例，不使用模型或凭据，也不操作用户桌面。契约见
[DEC-069](../decisions/DEC-069-local-visual-workflow-grounding.md)，实际结果见
[验收记录](../compatibility/local-visual-workflow-20261011.md)。
