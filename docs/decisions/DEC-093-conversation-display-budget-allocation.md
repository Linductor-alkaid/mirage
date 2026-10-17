# DEC-093：会话展示预算分类与淘汰策略

状态：Proposed → Accepted。日期：2026-10-15。工作项：M9-14（BUG-20261015-001）。

## 背景

DEC-049 定义每轮展示投影最多 96 段、每字段 16 KiB、内容总量 64 KiB。
BUG-20261015-001 证实长思考占满 64 KiB 后，后续工具结果被无差别截断，
连元数据都无法保留。这使调试、审计和验收无法定位工具是否被调用、参数/结果
是否完整，以及错误是否被记录。

## 决策

### 分类预算

展示投影 64 KiB 总预算按内容类型分配，保证关键信息可见：

1. **工具调用预算**（40 KiB）：
   - 包含 `input` + `output` 字段的总和
   - 优先级最高：工具是任务执行的证据
   - 每个工具调用至少保留 1 KiB（工具名、状态、参数摘要）

2. **最终正文预算**（16 KiB）：
   - 包含 `kind=text` 的段落
   - 优先级高：模型的最终回答
   - 至少保留前 4 KiB 和后 4 KiB

3. **思考预算**（8 KiB）：
   - 包含 `kind=thinking` 的段落
   - 优先级低：辅助调试，但不影响结果理解
   - 只保留前 2 KiB 和后 2 KiB，中间标记为省略

### 淘汰策略

当单类内容超过其预算时：

1. **工具调用**：
   - 保留所有工具的名称、状态、参数类型
   - 大参数/结果按 `desktop.vision.ocr`、`desktop.screen.capture` 等规则投影摘要
   - 如果仍超限，优先保留最近 N 个工具的完整记录
   - 旧工具保留元数据，参数/结果标记为 `omitted_for_display_budget`

2. **最终正文**：
   - 保留前 4 KiB（任务开头）和后 4 KiB（最终结论）
   - 中间部分标记为 `... [omitted ${bytes} bytes] ...`

3. **思考**：
   - 保留前 2 KiB 和后 2 KiB
   - 中间标记为 `... [thinking details omitted for display budget] ...`
   - 如果总预算仍不足，可完全省略思考，只显示 `[thinking omitted]`

### 段数限制

96 段限制保持不变，但淘汰优先级改为：
1. 先淘汰思考段
2. 再压缩工具参数（保留元数据）
3. 最后才压缩最终正文

### 标记与区分

- `truncated=true`：该段本身被裁剪（字段超过 16 KiB 或 UTF-8 边界）
- `omitted_for_display_budget`：该段因预算不足被省略，但原始数据可能完整
- 界面文案区分"展示摘要省略"（预算限制）和"供应商输出不完整"（模型截断）

## 影响

### 代码变更

- `runtime/conversation/include/mirage/conversation.hpp`：
  - 新增 `max_tool_budget = 40 * 1024`
  - 新增 `max_text_budget = 16 * 1024`
  - 新增 `max_thinking_budget = 8 * 1024`

- `integration/mira/src/conversation_capture.hpp`：
  - 修改 `bound()` 方法，按类型分配预算
  - 新增 `categorize_budget()` 方法，统计各类已用容量
  - 修改 `add()` 方法，在预算不足时应用淘汰策略

- `apps/native/app.cpp`：
  - 区分 `truncated`（字段裁剪）和 `omitted`（预算省略）的展示文案

### 验收条件

验证场景（来自分析文档）：

1. 长思考后截图：思考被限制在 8 KiB，后续截图元数据完整
2. 超 96 段：按淘汰策略，最近工具和最终正文可见
3. UTF-8 边界：裁剪不破坏 UTF-8 多字节字符
4. 历史/IPC 字节上限：编码后 JSON 不超过 1 MiB 帧和 8 MiB 文件
5. 工具错误：即使思考很长，工具失败信息仍可见
6. 最终正文：即使前面有大量工具，最终回答仍完整

## 兼容性

- 旧客户端读取新历史时，只看到标准字段；`omitted_for_display_budget` 被忽略
- 新客户端读取旧历史时，无差别 `truncated` 仍按旧方式显示
- IPC 协议保持兼容，只是投影策略变化

## 非目标

- 不提高 64 KiB 总预算（避免 IPC/存储膨胀）
- 不把展示文本当作模型历史或动作授权（展示投影不是完整记录）
- 不保存供图像素到展示历史（完整供图证据在 artifact，不在展示 JSON）

## 参考

- [BUG-20261015-001 调查](../analysis/native-session-truncation-pet-ime-20261015.md)
- [DEC-049：会话过程投影与折叠渲染](DEC-049-conversation-process-rendering.md)
- [M9 计划：持续任务与本地语义感知闭环](../plans/m9-continuous-tasks-and-local-perception.md)

## 2026-10-17：第二阶段近期思考展示（DEC-098）

本轮落实思考优先近期内容：每个长段最多 4KiB 的 UTF-8 首尾摘要，明确中间省略；
8KiB 总预算不足时，从最早思考起替换为省略提示。96 段耗尽时优先移除最早思考，
再移除已结算旧活动，保留正在执行的工具。`omitted_for_display_budget` 以布尔可选
字段落地；不引入未实现的省略枚举。旧空截断段也明确解释展示预算，不冒充供应商
生成失败。工具40KiB、正文16KiB不变；工具/正文自身的完整摘要与淘汰门禁继续开放。
