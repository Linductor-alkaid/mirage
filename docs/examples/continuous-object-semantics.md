# 连续观察中的对象语义

关联 M9-13/21、DEC-091/109/115。需要本地感知与已授权、可复用的任务桌面共享
会话；首增量每对象一个标签，依赖识别文字/类别，不含永久记忆或外观/关系依赖。
实际模型可见工具名沿 alias 规则用下划线，例如 `desktop_vision_annotate`。

1. 首次桌面截图建立共享会话后，正常模型请求自动收到 `semantic_scene`。
2. 对缺失或变化、且与任务相关的含义调用标定工具。`object_ref` 必须复制自当前
   实际供给的对象，不能使用坐标、entity 编号、旧图片或自行生成的摘要代替。
3. 后续请求读取 `semantic_state`、位置、年龄和 `annotation_source`。同身份移动
   继承标签；文字/类别变化或已知 gap 挂起旧标签。`suspended` 仅为历史解释。
4. 新内容需要新解释时，使用新供给的引用重新标定；不逐帧重复标定。

示例输入（占位符不可直接执行）：

```json
{"object_ref":"<当前 semantic_scene.objects 中的引用>","label":"搜索框"}
```

`applied=true` 只表示本地接受模型解释，`input_actionable=false`。模型请求来源由
宿主记录。未供给、内容/源/实例已变、身份不确定、撤权、取消或期限已到时拒绝；
晚到但同身份/同内容的移动可以接受。标签不覆盖真实 role，也不证明输入成功。
执行动作仍使用当前平台/图像输入契约和新证据，不能拿 object_ref 点击。

完整场景默认 256 对象/512 KiB；单次最多 16 对象、约 3 KiB。文字/类别各截至
256 字节并标注 `content_truncated`；完整自动观察块仍在 8 KiB 内。
`truncated=true` 表示本轮未展示全部对象；已标定与未标定对象均有界轮转，未展示
的引用不能标定。静态跳过不会刷新证据时间，120 秒过期。

本机真实采帧/OCR 的脚本标定验证：

```bash
/usr/bin/python3 tests/manual/continuous_perception_acceptance.py \
  --semantic \
  --binary build/native-release/tests/continuous_perception_acceptance \
  --manifest build/native-release/share/mirage/vision/ppocrv5-mobile-v1/manifest.json \
  --output out/semantic-wiring-20261026/real-ocr.json
```

该验证创建私有 Xvfb/GTK，并按识别文字生成确定性标签；验证视觉身份与绑定
继承，不是在线模型语义质量或 Portal/Windows 验收。
