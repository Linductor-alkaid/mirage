# 基础 Agent 工具操作说明（exec / read / write / edit，DEC-095）

M10 为会话交付四个基础工具：`read`（窗口化读文本）、`write`（新建文件）、
`edit`（观察校验后的精确替换）、`exec`（工作区内执行命令）。工具契约、错误码与
预算以 [DEC-095](../decisions/DEC-095-basic-agent-tools.md) 为准；本文面向产品
部署与运维：如何启用、配置边界、排查常见失败。当前 Linux 先行（M10-10），
Windows 服务对这些旗标响亮拒绝，不会静默半启用。

## 启用

工具只对 `mirage-service` 生效，两条途径：

1. 命令行旗标（整体覆盖设置文件中的同名字段）：

```bash
mirage-service \
  --read-root /home/user/Documents \
  --enable-basic-tools \
  --enable-exec --workspace /home/user/work \
  --write-root /home/user/work/out
```

2. 设置文件 `~/.config/mirage/service.json` 的 `basic_tools` 块（仅在未传相关
   旗标时生效；配置了非空 `workspace` 即视为启用意图）：

```json
{
  "basic_tools": {
    "enabled": true,
    "exec_enabled": true,
    "workspace": "/home/user/work",
    "write_roots": ["/home/user/work/out"]
  }
}
```

组合关系：`--enable-exec` 隐含 `--enable-basic-tools`；`exec` 必须有 workspace，
否则服务启动即以退出码 2 拒绝并给出提示，而不是运行后静默缺失工具。`--write-root`
可重复以声明多个写根；`--read-root` 只授予读权限，永远不会隐含写权限。

## 权限与确认

工具复用既有桌面权限门（DEC-010），能力名为 `filesystem.read`、
`filesystem.write`、`process.execute`：

```bash
--perm filesystem.read=allow --perm filesystem.write=allow --perm process.execute=confirm
```

`confirm` 走确认面：默认 `--confirm deny`（fail-closed），`allow` 放行，
`ipc` 把确认请求广播为 `permission.request` 事件并在 `--confirm-wait-ms`
（默认 120000）内等待回应，超时拒绝。被拒绝的动作不产生副作用：exec 在启动
进程前拒绝，write/edit 在提交前拒绝。

## 工具预算（DEC-095 冻结值）

| 工具 | 预算 |
| --- | --- |
| read | 每窗口 2000 行 / 50 KiB；全文件扫描 16 MiB / 5 s；单行 >50 KiB 拒绝 |
| write / edit | 单文件 ≤1 MiB；edit 匹配视图按 `\r\n`→`\n` 归一 |
| exec | 命令行 ≤64 KiB；超时默认 30 s、最大 120 s；每会话并发 1 |
| 输出留存 | 每流 4 MiB、每会话 32 MiB、全局 128 MiB；终态后保留 1 h |

`read` 返回分页窗口与 `next_offset`，模型按需续读；`exec` 输出先截断为预览，
完整输出以 `output_ref` 留存，模型用 `read` 续读，1 小时后过期
（`output_expired`）。

## 常见失败与恢复

失败以结构化错误码返回，模型可据此自行恢复；运维侧按码排查：

- `stale_file`：观察之后文件被外部修改。模型应重新 `read` 再 `edit`；这是
  多轮可恢复路径，不是故障。
- `workspace_required`：exec 未配置 workspace。补 `--workspace` 或设置文件。
- `not_observed` / `not_found` / `ambiguous_match` / `match_not_found`：
  edit 前置观察缺失、路径不存在、锚点匹配多处/无处。引导模型先读后改。
- `file_too_large` / `line_too_long` / `scan_budget_exceeded`：超出上表预算。
  拆分文件或用 exec 处理（如 `grep`/`sed`）。
- `busy`：同一路径已有未完成的写事务（单写者租约）。稍后重试。
- `output_expired`：exec 输出留存过期，需重新执行命令。
- `capacity_exceeded`：输出留存达到全局上限。等待过期回收或重启服务
  （留存目录仅存运行态引用，见下节）。

## 运行状态与清理

留存输出位于状态目录 `~/.local/state/mirage/basic-output`（`XDG_STATE_HOME`
可改）。`output_ref` 是运行态引用：服务每次启动时清空该目录，旧引用立即
`output_expired`，不提供跨重启读取。目录内单文件 0600。

## 取消与崩溃语义

会话取消（IPC `cancel`）会回收运行中的 exec：工具结果带 `terminated=cancelled`，
子进程不留存；等待、命令与平台动作均有可解除阻塞路径。文件提交走同目录临时
文件 + `renameat`/`renameat2(RENAME_NOREPLACE)` 原子落位：服务进程在任何阶段
被杀死（如 `kill -9`），目标文件只可能是完整的旧内容或完整的新内容，残留只
允许是 `.mirage-tmp-*.tmp` 临时文件，重启后可安全删除。

## 安全边界

`exec` 以宿主用户全部权限在 workspace 内运行命令，**没有沙箱**：只在可信
用户与可信工作区开启。写路径拒绝符号链接/硬链接替换与越出写根；路径含
`..` 或换链不被放大。全部工具都贯穿会话取消与产品确认，权限拒绝优先于一切
副作用。

## 验证

真实模型端到端回归（M10-09）：

```bash
cmake --build --preset debug --target mirage-service m10_basic_tools_e2e -j 6
BUILD_DIR=build/debug tests/manual/m10_basic_tools_e2e_run.sh
```

脚本以隔离 socket/state 启动真实服务，驱动三个场景：四工具链（read → edit →
write → exec → read，逐字节断言落盘效果）、exec 运行中产品取消（无残留进程）、
turn 间外部修改后的 stale 检测与恢复。`SCENARIOS=123` 可选场景，
`BUILD_DIR` 可指向其他构建目录（如 `build/native-release`）。
