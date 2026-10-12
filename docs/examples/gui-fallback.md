# 通用 GUI 回退示例（DEC-072）

快捷工具没有暴露或明确未派发时，模型先观察，选择等价 GUI 路线。不要在权限被拒后
绕路操作，也不要自动重复未知副作用。以下为输入契约示例，scope 和 ID 必须取自当前结果。

1. 调用 `desktop.screen.list_displays`，选实际显示器；`desktop.screen.capture`
   的 `source=display`、`source_id=<显示器>` 取得图片。确认 `region_actionable=true`，
   当前模型请求确实包含这张图片，再从图片中选择图标的矩形。
2. 点击显示上下文中的图标：

```json
{
  "image_scope": "<最新捕获的 image_scope>",
  "environment_epoch": 1,
  "target_context": "display",
  "display_id": "<实际显示器 ID>",
  "x": 20, "y": 20, "width": 40, "height": 40,
  "verify_kind": "foreground_changed"
}
```

数字只是示例；矩形使用供给图片的像素，产品依据源比例映射到桌面。窗口内目标改用
`target_context=window` 和真实 `window_id`。条件在输入前固定；`foreground_changed`
仅验证前台身份/标题/几何推进，不证明浏览器身份或加载页面。菜单文字可用
`display_text_exists` + `display_id` + `expected_text`，没有 OCR 时不得选此条件。

3. 使用返回 `action_id` 调用 `desktop.action.verify`。只读重验可吸收窗口晚于输入
   出现的情况；未验证时不能继续新修改。取得新 `desktop.window.front`/窗口列表和
   页面证据，确认目标应用；不得激活无关窗口。
4. 每次先取得新的 `observation_scope/environment_epoch`，再对目标窗口依次调用
   `desktop.input.key`（`ctrl+l`）、`desktop.input.type_text`（URL）、
   `desktop.input.key`（`enter`）。局部 `window_focused` 只证明输入留在目标窗口，
   还要取得动作后的截图/语义/OCR，验证网址与页面。

区域动作无需 OCR 编号。执行前必须新捕获所选区域并核对原像素；移动目标或变化区域
拒绝后重新观察。供图五秒，已供图意图最多六十秒；新图片使旧 image_scope 失效。
本地知识重复目标应复用 M7-11 的新语义/OCR/模板解析，不能持久保存本例坐标和 scope。
