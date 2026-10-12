# 本地语义 Workflow 预览（M7-11 / DEC-067）

已有 Linux Xorg 产品 Runtime 会在 scoped Accessibility、Window 与动作 owner 都可用
时暴露 `desktop.knowledge.activate`。本例将应用、页面、目标与固定后条件写入 Mira IR；
每次运行取得新语义快照和执行引用，经过 input.inject 权限、单写者和取消门禁，只执行一次
语义激活，再用新语义树验证唯一后条件。执行已知步骤不需要模型或 OCR 资源。

可用目录通过 `mirage workflow atoms` 核对。以下示例对应受控 GTK fixture，不是任意
应用的通用「保存」动作；应用名、窗口标题、控件角色/名称和后条件必须改成目标应用实际
提供的精确语义。不得保存 @e/@v、scope 或坐标。条件已存在、重名、禁用和未知状态失败。

```bash
/home/linductor/mirage/build/native-release/apps/mirage workflow publish \
  --file /home/linductor/mirage/docs/examples/local-semantic-save.workflow.json
/home/linductor/mirage/build/native-release/apps/mirage workflow run \
  75c0d066a3be49a99297e83c65ce7110 --policy recoverable
/home/linductor/mirage/build/native-release/apps/mirage workflow runs
```

publish 是不派发动作的 DryRun，不证明目标业务已完成。运行需要目标窗口位于前台，
input.inject 的现有权限判定允许；默认 confirm 的环境仍需现有确认入口。成功回执为
canonical JSON 字符串，`Contains "verified":true` 是 Mira 标量谓词兼容形式；宿主只有
新后条件成立才返回该成功回执，不能以调用成功、模型回答或 run_parameter 替代证据。

本例 max_attempts=1，不声明 Retry/循环；不要对不确定副作用自动重发。recoverable
只把失败交为 waiting_agent，当前产品不自动选知识或调用模型恢复。运行取消沿用
`mirage workflow cancel <run-id>`，接管和 shutdown 仍由 Runtime owner 管理。

IR 文件可长期保存在本地；当前 Mira 版本库与 Mirage workflow 目录是进程内投影，
重启后需要重新 publish。OCR/模板知识、多页 AppModel 导航、原生会话选择与自动跨重启
恢复仍属 M7-11 后续增量。完整复现无需操作用户桌面：

```bash
python3 /home/linductor/mirage/tests/manual/local_semantic_workflow_acceptance.py \
  --build /home/linductor/mirage/build/native-release --output /tmp/mirage-local-workflow-check
```

范围与验收见 [DEC-067](../decisions/DEC-067-local-semantic-workflow-grounding.md)和
[证据记录](../compatibility/local-semantic-workflow-20261011.md)。

## OCR／模板后续

M7-11 第二增量已追加 workflow-only desktop.knowledge.visual_click，参见
[本地视觉 Workflow](local-visual-workflow.md)。语义原子的精确应用/页面/控件契约不变，
无语义树的 Canvas 可以使用窗口标题加唯一 OCR 页面文字的视觉路径；两者分别选择。
