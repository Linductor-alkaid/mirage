# 过程记录与桌面任务用途

[DEC-137](../decisions/DEC-137-session-evidence-and-task-purpose.md)在 Runtime 产品启用
用途声明；零桌面 Provider 的纯对话环境保留原工具集合。普通聊天不增加工具调用；桌面发现可以先做，实际读图/读树或动作须先
声明。声明可放在首次已有工具批次，不需要宿主额外发起一次模型推理。

只查找或打开页面：

```json
{"action":"navigation"}
```

以上调用 `desktop.vision.content` 1.2.0。导航回答明确披露未证明内容观看/理解。
若用户要求观看片段或回答内容问题，则使用：

```json
{"action":"begin","scope":"observed_samples","requires_audio":false}
```

产品省略 auto_storyboard 时默认 true；显式 false 保留手动模式，非产品嵌入默认
false。用户要求完整视频时使用 entire_video，要求声音则 requires_audio=true，
不能为通过门禁而弱化需求。目前完整视频/音频证据不足，仍会明确失败。
导航可以升级为内容；已声明内容不能降为导航。没有常驻 feed 时仍可声明导航，
内容不会凭空获得证据。自动供图、5秒冷却、实际 Completed 后消费和 assess 方式见
[自动供图](automatic-content-visual-supply.md)。用途由模型解释，尚未独立验证与
用户意图一致；界面状态不代表视频内容已被理解。

语义树先用 max_depth 读取前沿。若同一快照返回 sidebar=@e2、main=@e3，需同时
展开两处，则对 `desktop.accessibility.semantic_snapshot` 1.1.0 使用：

```json
{"window_id":"<observed window id>","subtrees":["@e2","@e3"],"base_scope":"<source backend_scope>","max_depth":3}
```

最多4根，共享4096节点预算；失败不更换旧表，成功返回单一新代际、新编号和各
子树父索引。下一步使用新观察的 observation_scope/environment_epoch/@e 引用。
旧单 subtree API 兼容；每次成功快照都退役所有旧引用，不能将旧兄弟节点继续
传给下一次展开或动作。后端不支持时明确拒绝，不扩大采集范围。

过程日志仍最多96行、工具40KiB/文本16KiB/思考8KiB。超过预算先省略旧工具详情，
保留行身份、执行状态及小摘要，最新结果优先；omitted_for_display_budget/truncated
明确标记。省略的详情不能恢复为执行引用。计时器统计真实阶段次数，不等于日志
保留行数，也不等于模型图片数。终态回答以 terminal.reply 保留，避免与早期过程
正文混淆。该机制不把失败或未执行改为成功。

生产构建：

```bash
cmake --build build/native-release -j2 --target mirage-native-product
```

已有目标包含 launcher/service/tray/native/pet，桌面入口实际通过 tray 承载服务。
构建不替换已运行进程的映像；下次正常启动使用新产物。复测时同时记录进程启动
时间与 /proc/<pid>/exe 摘要，不能只检查磁盘文件。

嵌入侧若需要采用同一产品策略，可显式设置
`DesktopToolContext::require_task_purpose=true`；默认 false 保持原手动 begin/供图
契约。展示消费者可先调用 `conversation::bound_parts(parts)` 再用 `valid_parts`
校验；前者只收敛详情预算，不修复身份重复或非法状态；剩余投影须经严格 JSON/UTF-8 校验，不扩大执行
权限。Runtime 在既有过程事件和终态路径使用此顺序。
