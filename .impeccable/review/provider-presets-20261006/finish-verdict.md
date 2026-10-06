# M6-22内联复核

- disposition: ship（Linux本地且下列限制内）
- reviewer: 本轮实现Agent；不声称独立评审
- evidence: docs/compatibility/provider-presets-and-vision-20261006.md
- visual: 一轮初始实际窗口/renderer检查，修复窄导航品牌辨识和滚动条遮挡，再一轮确认；正常/最小明暗、空草稿/预设、会话轴已检查。
- behavior: 11项预设、草稿保留、Messages真实请求、保存ACK/刷新、键盘协议选择与旧OpenAI回归。
- live: MiniMax只填Key从空目录保存后真实流式会话；真实工具往返；两张随机图片全部正确，失败请求保留诊断记录。
- limits: 会话图片上传尚未交付；extended thinking/服务端工具不支持；其他厂商是未实测配置预设；无ZCode本机像素对照、Windows/Wayland/高DPI验证。
